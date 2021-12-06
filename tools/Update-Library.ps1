# Copyright (c) Microsoft Corporation. All rights reserved.
# SPDX-License-Identifier: MIT

param(
	$SdkVersion = $(throw "SdkVersion not provided"),
	$NewLibraryVersion = $(throw "NewLibraryVersion not provided")
)

$SrcFolder = "..\src"
$LibConfigFile = "..\library.properties"

Write-Host "Cloning azure-sdk-for-c repository."

git clone -b $SdkVersion https://github.com/Azure/azure-sdk-for-c sdkrepo

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
$(gc -raw $LibConfigFile) -replace "url=[a-zA-Z0-9\/:.-]+", "url=https://github.com/Azure/azure-sdk-for-c/tree/$SdkVersion" | out-file -Encoding ascii -Force -NoNewline $LibConfigFile

Write-Host "You must manually update the library.properties with any new includes such as az_core.h, az_iot.h"
