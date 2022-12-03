# Copyright (c) Microsoft Corporation. All rights reserved.
# SPDX-License-Identifier: MIT

<#
.SYNOPSIS
	Updates the Azure SDK for C code referenced by this Arduino library.
.DESCRIPTION
	Update-Library does:
	- copy over the code from azure/azure-sdk-for-c into the flat file structure stored under .\src.
	- Change the code from azure/azure-sdk-for-c (copied locally) to reference headers in the .\src directory.
	- Update the version of this Arduino library.
.PARAMETER SdkBranch
	Commit-ish (e.g., branch or tag) from azure/azure-sdk-for-c to use during the local file update.
.PARAMETER SdkVersion
	String with the version information to update in this Arduino library library.properties/paragraph.
.PARAMETER NewLibraryVersion
	String with the new version of this Arduino library (updated in library.properties/version).
.EXAMPLE
	Update-Library -SdkBranch main -SdkVersion 1.3.2 -NewLibraryVersion 1.0.0
.EXAMPLE
	Update-Library -SdkBranch feature/adu -SdkVersion 1.3.2 -NewLibraryVersion 1.1.0-beta.1
.EXAMPLE
	Update-Library -SdkBranch 1.4.0 -SdkVersion 1.4.0 -NewLibraryVersion 1.1.0
#>

param(
  $SdkBranch = $(throw "SdkBranch not provided"),
  $SdkVersion = $(throw "SdkVersion not provided"),
  $NewLibraryVersion = $(throw "NewLibraryVersion not provided")
)

$SrcFolder = "..\src"
$LibConfigFile = "..\library.properties"

Write-Host "Cloning azure-sdk-for-c repository."

git config --local core.autocrlf false
git clone -b $SdkBranch https://github.com/Azure/azure-sdk-for-c sdkrepo

Write-Host "Flattening the azure-sdk-for-c file structure and updating src/."

# Filtering out files not needed/supported on Arduino.
$Files = gci -Recurse -Include *.h, *.c .\sdkrepo\sdk  | ?{ $_.DirectoryName -INOTMATCH "tests|sample" -AND $_.Name -INOTMATCH "curl|win32|az_posix" }

rm -Force -Exclude "azure_ca.h" $SrcFolder/*

copy -Verbose $Files $SrcFolder

# Fixing headers to work as a flat structure.
Get-ChildItem -Recurse -Include *.c,*.h -Path $SrcFolder | %{
	$(Get-Content -Raw $_ ) -replace "<azure`/(iot`/internal|core`/internal|iot|core|storage)`/", "<" | out-file -Encoding ascii -Force -NoNewline $_
}

Write-Host "Removing clone of azure-sdk-for-c."

rm -Recurse -Force sdkrepo/

Write-Host "Updating versions."

# Update Arduino library version with SDK version.
$(gc -Raw $LibConfigFile) -replace "version=[0-9]\.[0-9]\.[0-9][a-zA-Z0-9.-]*", "version=$NewLibraryVersion" | out-file -Encoding ascii -Force -NoNewline $LibConfigFile
$(gc -Raw $LibConfigFile) -replace "\([0-9]\.[0-9]\.[0-9][^\)]*\)", "($SdkVersion)" | out-file -Encoding ascii -Force -NoNewline $LibConfigFile

Write-Host "You must manually update the library.properties with any new includes such as az_core.h, az_iot.h"
