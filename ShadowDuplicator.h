/* ShadowDuplicator -- a simple VC++ Volume Shadow Copy requestor for backing up
locked files

Copyright (C) 2021-22 Peter Upfold.

Licensed under the Apache 2.0 Licence. See the LICENSE file in the project root for details.

This code is **not** production quality. There is certainly plenty of potential for improvement of this code,
but beyond that, it may even be insecure, destructive or cause you other serious problems. There
is no warranty.
*/
#pragma once
#include <windows.h>
#include <winerror.h>
#include <vss.h>
#include <vswriter.h>
#include <vsbackup.h>
#include <cassert>

void genericFailCheck(const char* operationName, HRESULT result);
void friendlyError(LPCWSTR ourErrorDescription, const DWORD error);
DWORD ShadowCopyFile(WCHAR  sourcePathFile[260], WCHAR  destinationPathFile[260]);
void friendlyCopyError(LPCWSTR ourErrorDescription, LPWSTR destinationFile, const DWORD error);
void bail(HRESULT exitCode);
void banner(void);
void usage(void);
void spinProgress(void);
LPPROGRESS_ROUTINE copyProgress(
	LARGE_INTEGER TotalFileSize,
	LARGE_INTEGER TotalBytesTransferred,
	LARGE_INTEGER StreamSize,
	LARGE_INTEGER StreamBytesTransferred,
	DWORD dwStreamNumber,
	DWORD dwCallbackReason,
	HANDLE hSourceFile,
	HANDLE hDestinationFile,
	LPVOID lpData
);
void VerifyWriterStatus(void);