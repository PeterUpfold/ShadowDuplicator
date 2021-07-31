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
    
    result = CreateVssBackupComponents(&backupComponents);
    if (result == E_ACCESSDENIED) {
        printf("Failed to create the VSS backup components as access was denied. Is this being run with elevated permissions?");
        exit(E_ACCESSDENIED);
    }
    if (result != S_OK) {
        printf("Result of CreateVssBackupComponents was %x", result);
        goto bail;
    }

    assert(backupComponents != nullptr);

    // InitializeForBackup
    /*
    this actually works if NT AUTHORITY\SYSTEM -- need permissions in Computer\HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\VSS\VssAccessControl ??

    This step will fail with 0x80042302 otherwise.
    */
    result = backupComponents->InitializeForBackup();
    if (result != S_OK) {
        printf("Result of InitializeForBackup was %x", result);
        goto bail;
    }


    // gather writer metadata
    result = backupComponents->GatherWriterMetadata(&vssAsync);
    if (result != S_OK) {
        printf("Result of GatherWriterMetadata was %x", result);

        // do I need to release the vssAsync?
        goto bail;
    }


    // completion of setup
    result = backupComponents->SetBackupState(false, false, VSS_BT_FULL, false);
    if (result != S_OK) {
        printf("Result of SetBackupState was %x", result);
        goto bail;
    }

    

    bail:
    backupComponents->Release();
}
