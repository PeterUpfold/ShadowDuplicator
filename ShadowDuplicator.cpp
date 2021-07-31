// ShadowDuplicator.cpp : This file contains the 'main' function. Program execution begins and ends there.
//


#include <iostream>
#include <windows.h>
#include <winerror.h>
#include <vss.h>
#include <vswriter.h>
#include <vsbackup.h>
#include <cassert>
#include "ShadowDuplicator.h"

IVssBackupComponents* backupComponents = nullptr;
IVssAsync* vssAsync = nullptr;
VSS_ID* snapshotSetId = nullptr;
VSS_ID* snapshotId = nullptr;

int main()
{
    HRESULT result = E_FAIL;
    HRESULT asyncResult = E_FAIL;
    
    // initialize COM (must do before InitializeForBackup works)
    result = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    if (result != S_OK) {
        printf("Unable to initialize COM -- 0x%x\n", result);
        exit(result);
    }
    
    result = CreateVssBackupComponents(&backupComponents);
    if (result == E_ACCESSDENIED) {
        printf("Failed to create the VSS backup components as access was denied. Is this being run with elevated permissions?\n");
        exit(E_ACCESSDENIED);
    }
    genericFailCheck("CreateVssBackupComponents", result);

    assert(backupComponents != nullptr);

    // InitializeForBackup
    result = backupComponents->InitializeForBackup();
    genericFailCheck("InitializeForBackup", result);

    // gather writer metadata
    result = backupComponents->GatherWriterMetadata(&vssAsync);
    genericFailCheck("GatherWriterMetadata", result);

    // we must await the result
    assert(vssAsync != nullptr);

    while (asyncResult != VSS_S_ASYNC_CANCELLED && asyncResult != VSS_S_ASYNC_FINISHED) {
        Sleep(500);
        result = vssAsync->QueryStatus(&asyncResult, NULL);
        if (result != S_OK) {
            printf("Unable to query vss async status -- %x\n", result);
            bail(result);
        }
        OutputDebugString(L"Waiting for async VSS status...");
    }

    //printf("Got async result final: %x\n", asyncResult);
    if (asyncResult == VSS_S_ASYNC_CANCELLED) {
        printf("Operation was cancelled.");
        bail(result);
    }

    asyncResult = E_FAIL;

    // completion of setup
    result = backupComponents->SetBackupState(false, false, VSS_BT_FULL, false);
    genericFailCheck("SetBackupState", result);

    // start a new snapshot set

    snapshotSetId = (VSS_ID*)malloc(sizeof(VSS_ID));
    assert(snapshotSetId != 0); 

    result = backupComponents->StartSnapshotSet(snapshotSetId);
    genericFailCheck("StartSnapshotSet", result);

    snapshotId = (VSS_ID*)malloc(sizeof(VSS_ID));
    assert(snapshotId != nullptr);

    // add volume to snapshot set AddToSnapshotSet
    result = backupComponents->AddToSnapshotSet((WCHAR*)L"C:\\", GUID_NULL, snapshotId);
    genericFailCheck("AddToSnapshotSet", result);

    // notify writers of impending backup
    result = backupComponents->PrepareForBackup(&vssAsync);
    genericFailCheck("PrepareForBackup", result);

    while (asyncResult != VSS_S_ASYNC_CANCELLED && asyncResult != VSS_S_ASYNC_FINISHED) {
        Sleep(500);
        result = vssAsync->QueryStatus(&asyncResult, NULL);
        if (result != S_OK) {
            printf("Unable to query vss async status -- %x\n", result);
            bail(result);
        }
        OutputDebugString(L"Waiting for PrepareForBackup VSS status...");
    }

    if (asyncResult == VSS_S_ASYNC_CANCELLED) {
        printf("Operation was cancelled.");
        bail(asyncResult);
    }

    asyncResult = E_FAIL;
    
    //TODO we should verify writer status

    

    // request shadow copy

    result = backupComponents->DoSnapshotSet(&vssAsync);
    genericFailCheck("DoSnapshotSet", result);

    //TODO we should verify writer status 


    bail(0);
}

/// <summary>
/// Perform a generic check for the HRESULT S_OK, and bail out if it is not S_OK.
/// </summary>
/// <param name="operationName">Human-readable operation name or function call</param>
/// <param name="result">The operation result</param>
void genericFailCheck(const char* operationName, HRESULT result) {
    if (result != S_OK) {
        printf("Result of %s was 0x%x\n", operationName, result);
        bail(result);
    }
}

/// <summary>
/// Tidy up any objects and uninitialize before an exit.
/// </summary>
/// <param name="exitCode">The exit code to provide to the OS.</param>
void bail(HRESULT exitCode) {
    printf("Bail\n");

    if (snapshotSetId != nullptr) {
        free(snapshotSetId);
        snapshotSetId = nullptr;
    }

    if (snapshotId != nullptr) {
        free(snapshotId);
        snapshotId = nullptr;
    }

    if (vssAsync != nullptr) {
        vssAsync->Release();
        vssAsync = nullptr;
    }

    backupComponents->Release();
    backupComponents = nullptr;
    CoUninitialize();
    exit(exitCode);
}
