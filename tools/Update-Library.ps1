# Copyright (c) Microsoft Corporation. All rights reserved.
# SPDX-License-Identifier: MIT

$SrcFolder = "..\src"
$LibConfigFile = "..\library.properties"

git clone https://github.com/Azure/azure-sdk-for-c sdkrepo

$Files = gci -Recurse -Include *.h, *.c .\sdkrepo\sdk  | ?{ $_.DirectoryName -INOTMATCH "tests|sample" -AND $_.Name -INOTMATCH "curl|win32|az_posix" }

copy $Files $SrcFolder

Get-ChildItem -Recurse -Include *.c,*.h -Path $SrcFolder | %{
	$(Get-Content -Raw $_ ) -replace "<azure`/(iot`/internal|core`/internal|iot|core)`/", "<" | out-file -Encoding ascii -Force -NoNewline $_
}

rm -Recurse -Force sdkrepo/

# Update Arduino library version with SDK version.
$SdkVersion = $(gc $SrcFolder\az_version.h | ?{ $_ -IMATCH "AZ_SDK_VERSION_STRING" }).Split()[2].Replace("`"", "")
$(gc -Raw $LibConfigFile) -replace "\([0-9]\.[0-9]\.[0-9][^\)]*\)", "($SdkVersion)" | out-file -Encoding ascii -Force -NoNewline $LibConfigFile

Write-Host "You must manually update the library.properties with any new includes such as az_core.h, az_iot.h"
