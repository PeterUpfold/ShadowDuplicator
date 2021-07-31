// ShadowDuplicator.cpp : This file contains the 'main' function. Program execution begins and ends there.
//


#include <iostream>
#include <windows.h>
#include <winerror.h>
#include <vss.h>
#include <vswriter.h>
#include <vsbackup.h>
#include <cassert>


int main()
{

    IVssBackupComponents* backupComponents = nullptr;
    IVssAsync* vssAsync = nullptr;
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
    if (result != S_OK) {
        printf("Result of CreateVssBackupComponents was %x\n", result);
        goto bail;
    }

    assert(backupComponents != nullptr);

    // InitializeForBackup
    /*
    */
    result = backupComponents->InitializeForBackup();
    if (result != S_OK) {
        printf("Result of InitializeForBackup was %x\n", result);
        goto bail;
    }


    // gather writer metadata
    result = backupComponents->GatherWriterMetadata(&vssAsync);
    if (result != S_OK) {
        printf("Result of GatherWriterMetadata was %x\n", result);
        goto bail;
    }

    // we must await the result
    assert(vssAsync != nullptr);

    

    while (asyncResult != VSS_S_ASYNC_CANCELLED && asyncResult != VSS_S_ASYNC_FINISHED) {
        Sleep(500);
        result = vssAsync->QueryStatus(&asyncResult, NULL);
        if (result != S_OK) {
            printf("Unable to query vss async status -- %x\n", result);
            goto bail;
        }
        OutputDebugString(L"Waiting for async VSS status...");
    }

    printf("Got async result final: %x\n", asyncResult);

    if (asyncResult == VSS_S_ASYNC_CANCELLED) {
        printf("Operation was cancelled.");
        goto bail;
    }

    asyncResult = E_FAIL;

    // completion of setup
    result = backupComponents->SetBackupState(false, false, VSS_BT_FULL, false);
    if (result != S_OK) {
        printf("Result of SetBackupState was %x\n", result);
        goto bail;
    }

    // notify writers of impending backup
    result = backupComponents->PrepareForBackup(&vssAsync);

    if (result != S_OK) {
        printf("Result of PrepareForBackup was %x\n", result);
        goto bail;
    }

    while (asyncResult != VSS_S_ASYNC_CANCELLED && asyncResult != VSS_S_ASYNC_FINISHED) {
        Sleep(500);
        result = vssAsync->QueryStatus(&asyncResult, NULL);
        if (result != S_OK) {
            printf("Unable to query vss async status -- %x\n", result);
            goto bail;
        }
        OutputDebugString(L"Waiting for PrepareForBackup VSS status...");
    }

    if (asyncResult == VSS_S_ASYNC_CANCELLED) {
        printf("Operation was cancelled.");
        goto bail;
    }

    asyncResult = E_FAIL;
    
    //TODO we should verify writer status

    //TODO add volume to snapshot set AddToSnapshotSet

    // request shadow copy

    result = backupComponents->DoSnapshotSet(&vssAsync);

    if (result != S_OK) {
        printf("Result of DoSnapshotSet was %x\n", result);
        goto bail;
    }

    //TODO we should verify writer status 



bail:
    printf("Bail\n");

    if (vssAsync != nullptr) {
        vssAsync->Release();
        vssAsync = nullptr;
    }
    backupComponents->Release();
    backupComponents = nullptr;
}
