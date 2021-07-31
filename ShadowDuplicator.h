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