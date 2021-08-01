/* ShadowDuplicator -- a simple VC++ Volume Shadow Copy requestor for backing up
locked files

Copyright (C) 2021 Peter Upfold.

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
void bail(HRESULT exitCode);
void banner(void);
void usage(void);
void spinProgress(void);