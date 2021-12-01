# Copyright (c) Microsoft Corporation. All rights reserved.
# SPDX-License-Identifier: MIT

git clone https://github.com/Azure/azure-sdk-for-c sdkrepo

$Files = gci -Recurse -Include *.h, *.c .\sdkrepo\sdk  | ?{ $_.DirectoryName -INOTMATCH "tests|sample" -AND $_.Name -INOTMATCH "curl|win32|az_posix" }

mkdir src

copy $Files .\src\

cp .\src\az_iot.h .\src\azure-sdk-for-c.h 

Get-ChildItem -Recurse -Include *.c,*.h -Path .\src\ | %{
	$(Get-Content -Raw $_ ) -replace "<azure`/(iot`/internal|core`/internal|iot|core)`/", "<" | out-file -Encoding ascii -Force $_
}

rm -Recurse -Force sdkrepo/

rm -Recurse -Force ../src

mv src ..


