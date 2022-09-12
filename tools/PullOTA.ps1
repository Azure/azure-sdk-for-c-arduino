# Copyright (c) Microsoft Corporation. All rights reserved.
# SPDX-License-Identifier: MIT

param(
  $SDKPath = $(throw "Please pass path to embedded SDK with ADU")
)

$SrcFolder = "..\src"
$LibConfigFile = "..\library.properties"

Write-Host "Taking files from $SDKPath"

Write-Host "Flattening the azure-sdk-for-c file structure and updating src/."

# Filtering out files not needed/supported on Arduino.
$Files = gci -Recurse -Include *.h, *.c $SDKPath\sdk  | ?{ $_.DirectoryName -INOTMATCH "tests|sample" -AND $_.Name -INOTMATCH "curl|win32|az_posix" }

rm -Force -Exclude "azure_ca.h" $SrcFolder/*

copy -Verbose $Files $SrcFolder

# Fixing headers to work as a flat structure.
Get-ChildItem -Recurse -Include *.c,*.h -Path $SrcFolder | %{
	$(Get-Content -Raw $_ ) -replace "<azure`/(iot`/internal|core`/internal|iot|core|storage)`/", "<" | out-file -Encoding ascii -Force -NoNewline $_
}

Write-Host "You must manually update the library.properties with any new includes such as az_core.h, az_iot.h"
