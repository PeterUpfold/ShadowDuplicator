/* ShadowDuplicator -- a simple VC++ Volume Shadow Copy requestor for backing up
locked files

Copyright (C) 2021-2022 Peter Upfold.

Licensed under the Apache 2.0 Licence. See the LICENSE file in the project root for details.

This code is **not** production quality. There is certainly plenty of potential for improvement of this code,
but beyond that, it may even be insecure, destructive or cause you other serious problems. There 
is no warranty.
*/


#include <iostream>
#include <windows.h>
#include <winerror.h>
#include <vss.h>
#include <vswriter.h>
#include <vsbackup.h>
#include <strsafe.h>
#include <shlwapi.h>
#include "ShadowDuplicator.h"

#define assert(expression) if (!(expression)) { printf("assert on %d", __LINE__); bail(250); }

#define SDVERSION L"v0.5-wide"

// A linked list of source drives or source paths
typedef struct sourceList {
    LPWSTR source;
    struct sourceList *next;
} t_sourceList;

/// <summary>
/// The backup components VSS object.
/// </summary>
IVssBackupComponents* backupComponents = nullptr;

/// <summary>
/// An object used to query status of async operations within VSS.
/// </summary>
IVssAsync* vssAsync = nullptr;

/// <summary>
/// The snapshot set overall has a GUID.
/// </summary>
VSS_ID* snapshotSetId = nullptr;

/// <summary>
/// The GUID of a snapshot within the set.
/// </summary>
VSS_ID* snapshotId = nullptr;


/// <summary>
/// The destination directory where backed up files should be copied.
/// </summary>
LPWSTR destDirectory = nullptr;

/// <summary>
/// The canonical, fully qualified path to the INI file which provides
/// options for this execution run.
/// </summary>
LPWSTR canonicalINIPath = nullptr;

/// <summary>
/// Is COM initialized? For only calling CoUninitialize() in bail() if we 
/// have initialized it.
/// </summary>
BOOL comInitialized = FALSE;

/// <summary>
/// If we fail after backup start but before completion, we will call AbortBackup.
/// If the backup completes, we set this to FALSE so that we will not call AbortBackup.
/// This starts false as we have not yet started a backup.
/// </summary>
BOOL shouldAbortBackupOnBail = FALSE;

/// <summary>
/// Keep global state for a visible spinner to show progress.
/// </summary>
int progressMarker = 0;

/// <summary>
/// Whether to display progres and verbose status.
/// </summary>
BOOL quiet = FALSE;

/// <summary>
/// Head of source drive specifications (C:\, D:\) for each source file. Linked list structure.
/// </summary>
t_sourceList* sourceDrives = nullptr;

/// <summary>
/// Head of source filenames in the linked list structure.
/// </summary>
t_sourceList* sourceFilenames = nullptr;

/// <summary>
/// Pointer to the current drive we are dealing with.
/// </summary>
t_sourceList* currentSourceDrive = sourceDrives;

/// <summary>
/// Pointer to the current filename we are dealing with.
/// </summary>
t_sourceList* currentSourceFilename = sourceFilenames;

/// <summary>
/// The previous pointer to the drive we were working with. This is so we can update the next pointer.
/// </summary>
t_sourceList* previousSourceDrive = sourceDrives;

/// <summary>
/// The previous pointer to the filename we were working with.
/// </summary>
t_sourceList* previousSourceFilename = sourceFilenames;

/// <summary>
/// Head of list of source filenames with the source drive specification sliced off.
/// </summary>
t_sourceList* sourceFilenamesWithoutDrives = nullptr;

/// <summary>
/// Current pointer of where we are in the source filenames with the source drive specification sliced off.
/// </summary>
t_sourceList* currentSourceFilenameWithoutDrive = sourceFilenamesWithoutDrives;

/// <summary>
/// The previous pointer to the source filenames with the source drive specification sliced off.
/// </summary>
t_sourceList* previousSourceFilenameWithoutDrive = sourceFilenamesWithoutDrives;


#define SHORT_SLEEP 500
#define LONG_SLEEP 1500

// exit codes
#define SDEXIT_NO_DEST_DIR_SPECIFIED 1 | 0x20000000 // customer bit in HRESULT
#define SDEXIT_NO_FIRST_FILE_IN_SOURCE 2 | 0x20000000
#define SDEXIT_NO_SOURCE_SPECIFIED 3 | 0x20000000
#define SDEXIT_SOURCE_FILES_ON_DIFFERENT_VOLUMES 4 | 0x20000000


/// <summary>
/// Entry point
/// </summary>
/// <param name="argc"></param>
/// <param name="argv"></param>
/// <returns></returns>
int wmain(int argc, WCHAR** argv)
{
    HRESULT result = E_FAIL;
    HRESULT asyncResult = E_FAIL;
    VSS_SNAPSHOT_PROP snapshotProp{};
    
    DWORD fileAttributes = INVALID_FILE_ATTRIBUTES;
    DWORD error = 0;
    DWORD copyError = 0;
    BOOL selectedFilesMode = FALSE;

    WIN32_FIND_DATA findData{};

    int lastSwitchArgument = 1; // the index of the last command line arg that was a switch
    BOOL switchArgumentsComplete = FALSE;



    // loop over command line options -- _very_ simple parsing
    if (argc < 2) {
        usage();
        exit(0);
    }


    for (int i = 1; i < argc; i++) {

        if (wcscmp(argv[i], L"/?") == 0) {
            usage();
            exit(0);
        }
        // handle switches
        if (argv[i][0] == L'-') {
            //TODO: check if use of strcmp is safe here -- can we assume arguments are null terminated correctly?
            if (wcscmp(argv[i], L"-q") == 0) {
                quiet = TRUE;
            }
            if (wcscmp(argv[i], L"-h") == 0 || wcscmp(argv[i], L"--help") == 0 || wcscmp(argv[i], L"-?") == 0 || wcscmp(argv[i], L"--usage") == 0) {
                usage();
                exit(0);
            }
            if (wcscmp(argv[i], L"--singlefile") == 0 || wcscmp(argv[i], L"-s") == 0) {
                selectedFilesMode = TRUE;
            }
            ++lastSwitchArgument;
        }
        
        else {
            if (!switchArgumentsComplete) {
                lastSwitchArgument = lastSwitchArgument - 1;
                switchArgumentsComplete = TRUE;
            }

            if (selectedFilesMode) { // get source and dest path
                
                // allocate space for destination path
                if (destDirectory == nullptr) {
                    destDirectory = (LPWSTR)malloc(MAX_PATH * sizeof(WCHAR));
                }
                
                assert(destDirectory != nullptr);

                // usage: ShadowDuplicator -s [source] [source] [source] [dest]
                if (i != (argc - 1)) {
                    // allocate a new source directory and filename
                    currentSourceDrive = (t_sourceList*)malloc(sizeof(t_sourceList));
                    assert(currentSourceDrive != nullptr);
                    ZeroMemory(currentSourceDrive, sizeof(t_sourceList));
                    if (sourceDrives == nullptr) { // if the head of the list is not yet set, set it
                        sourceDrives = currentSourceDrive;
                    }

                    currentSourceFilename = (t_sourceList*)malloc(sizeof(t_sourceList));
                    assert(currentSourceFilename != nullptr);
                    ZeroMemory(currentSourceFilename, sizeof(t_sourceList));
                    if (sourceFilenames == nullptr) { // if the head of the list is not yet set, set it
                        sourceFilenames = currentSourceFilename;
                    }

                    // get source drive from source directory
                    currentSourceDrive->source = (LPWSTR)malloc(MAX_PATH * sizeof(WCHAR));
                    assert(currentSourceDrive->source != nullptr);

                    if (!GetVolumePathNameW(argv[i], currentSourceDrive->source, MAX_PATH)) {
                        error = GetLastError();
                        if (error) {
                            friendlyError(L"Failed to get Source Drive from Source Directory", error);
                        }
                    }

                    // copy filename into filename buffer
                    currentSourceFilename->source = (LPWSTR)malloc(MAX_PATH * sizeof(WCHAR));
                    assert(currentSourceFilename->source != nullptr);
                    StringCbPrintf(currentSourceFilename->source, MAX_PATH * sizeof(WCHAR), argv[i]);

                    // now set the previous item's pointer to point to these items
                    if (previousSourceDrive != nullptr) {
                        previousSourceDrive->next = currentSourceDrive;
                    }
                    if (previousSourceFilename != nullptr) {
                        previousSourceFilename->next = currentSourceFilename;
                    }
                    previousSourceDrive = currentSourceDrive;
                    previousSourceFilename = currentSourceFilename;

                }
                else { // last argument is dest
                    StringCbPrintfW(destDirectory, MAX_PATH, L"%s", argv[i]);
                }
            }
            else { // directory mode -- check INI file
                fileAttributes = GetFileAttributesW(argv[i]);

                if (fileAttributes == INVALID_FILE_ATTRIBUTES) {
                    error = GetLastError();
                    friendlyError(L"Failed to check INI file", error);
                    exit(error);
                }

                canonicalINIPath = (LPWSTR)malloc(MAX_PATH * sizeof(WCHAR));

                // canonicalise path
                if (!(GetFullPathNameW(argv[i], MAX_PATH, canonicalINIPath, NULL))) {
                    error = GetLastError();
                    friendlyError(L"Failed to get full path name of specified INI file", error);
                    exit(error);
                }

                // allocate space for source and destination paths
                destDirectory = (LPWSTR)malloc(MAX_PATH * sizeof(WCHAR));

                assert(destDirectory != nullptr);

                // allocate source 
                currentSourceDrive = (t_sourceList*)malloc(sizeof(t_sourceList));
                assert(currentSourceDrive != nullptr);
                ZeroMemory(currentSourceDrive, sizeof(t_sourceList));
                if (sourceDrives == nullptr) { // if the head of the list is not yet set, set it
                    sourceDrives = currentSourceDrive;
                }

                currentSourceFilename = (t_sourceList*)malloc(sizeof(t_sourceList));
                assert(currentSourceFilename != nullptr);
                ZeroMemory(currentSourceFilename, sizeof(t_sourceList));
                if (sourceFilenames == nullptr) { // if the head of the list is not yet set, set it
                    sourceFilenames = currentSourceFilename;
                }
            
                // get source from INI
                currentSourceFilename->source = (LPWSTR)malloc(MAX_PATH * sizeof(WCHAR));
                assert(currentSourceFilename->source != nullptr);
                GetPrivateProfileStringW(
                    L"FileSet",
                    L"Source",
                    L"",
                    currentSourceFilename->source,
                    MAX_PATH,
                    canonicalINIPath
                );

                error = GetLastError();
                if (error) {
                    friendlyError(L"Failed to import Source from INI file", error);
                    exit(error);
                }

                // get dest from INI
                GetPrivateProfileStringW(
                    L"FileSet",
                    L"Destination",
                    L"",
                    destDirectory,
                    MAX_PATH,
                    canonicalINIPath
                );

                error = GetLastError();
                if (error) {
                    friendlyError(L"Failed to import Destination from INI file", error);
                }


                // get source drive from source directory
                currentSourceDrive->source = (LPWSTR)malloc(MAX_PATH * sizeof(WCHAR));
                assert(currentSourceDrive->source != nullptr);

                if (!GetVolumePathNameW(argv[i], currentSourceDrive->source, MAX_PATH)) {
                    error = GetLastError();
                    if (error) {
                        friendlyError(L"Failed to get Source Drive from Source Directory", error);
                    }
                }
            }
        }
    }

    if (!quiet) {
        banner();
    }

    // check the dest directory existence before we bother to set up VSS
    if (sourceFilenames == nullptr) {
        printf("No source files were specified.\n"); // friendlyError is not appropriate as this looks up Win32 error codes
        bail(SDEXIT_NO_SOURCE_SPECIFIED);
    }
    if (sourceDrives == nullptr) {
        printf("No source drives were specified.\n"); // friendlyError is not appropriate as this looks up Win32 error codes
        bail(SDEXIT_NO_SOURCE_SPECIFIED);
    }
    if (destDirectory == nullptr) {
        printf("No destination directory was specified.\n"); // friendlyError is not appropriate as this looks up Win32 error codes
        bail(SDEXIT_NO_DEST_DIR_SPECIFIED);
    }
    if (!selectedFilesMode && !PathFileExistsW(destDirectory)) { //TODO: can we add to this checking dest dir in selected file mode?
        error = GetLastError();
        if (error) {
            friendlyError(L"The destination directory does not seem to exist.", error); //friendlyError will bail
        }
    }

    currentSourceFilename = sourceFilenames;
    do {
        assert(currentSourceFilename->source != nullptr);
        if (!PathFileExistsW(currentSourceFilename->source)) {
            error = GetLastError();
            if (error) {
                friendlyError(L"The source file does not seem to exist.", error); //friendlyError will bail //TODO say which file
            }
        }
        currentSourceFilename = currentSourceFilename->next;
    } while (currentSourceFilename != nullptr);

    // initialize COM (must do before InitializeForBackup works)
    result = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    if (result != S_OK) {
        printf("Unable to initialize COM -- 0x%x\n", result);
        exit(result);
    }
    comInitialized = TRUE;

    result = CreateVssBackupComponents(&backupComponents);
    if (result == E_ACCESSDENIED) {
        printf("Failed to create the VSS backup components as access was denied. Is this being run with elevated permissions?\n");
        CoUninitialize();
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
    

    if (!quiet) {
        printf("Waiting for VSS writers to provide metadata...\n");
    }
    while (asyncResult != VSS_S_ASYNC_CANCELLED && asyncResult != VSS_S_ASYNC_FINISHED) {
        Sleep(SHORT_SLEEP);
        result = vssAsync->QueryStatus(&asyncResult, NULL);
        if (result != S_OK) {
            printf("Unable to query vss async status -- %x\n", result);
            vssAsync->Release();
            vssAsync = nullptr;
            bail(result);
        }
        OutputDebugString(L"Waiting for async VSS status...");
        if (!quiet) {
            spinProgress();
        }
    }

    if (asyncResult == VSS_S_ASYNC_CANCELLED) {
        printf("Operation was cancelled.");
        vssAsync->Release();
        vssAsync = nullptr;
        bail(result);
    }

    vssAsync->Release();
    vssAsync = nullptr;

    asyncResult = E_FAIL;

    // completion of setup
    result = backupComponents->SetBackupState(false, false, VSS_BT_FULL, false);
    genericFailCheck("SetBackupState", result);

    // start a new snapshot set
    snapshotSetId = (VSS_ID*)malloc(sizeof(VSS_ID));
    assert(snapshotSetId != 0);

    result = backupComponents->StartSnapshotSet(snapshotSetId);
    genericFailCheck("StartSnapshotSet", result);

    // from StartSnapshotSet until backup completion, if we fail, we must call AbortBackup inside bail
    shouldAbortBackupOnBail = TRUE;

    snapshotId = (VSS_ID*)malloc(sizeof(VSS_ID));
    assert(snapshotId != nullptr);

    // add volumes to snapshot set AddToSnapshotSet -- we will only add the first drive spec -- all source files must be on the same volume
    currentSourceDrive = sourceDrives;
    currentSourceFilename = sourceFilenames;
    WCHAR volumeAddedToSnapshot[4]{};
    do {
        assert(currentSourceDrive->source != nullptr);

        if (wcslen(volumeAddedToSnapshot) < 1) {
            // this is the volume we will add to the snapshot -- make a note
            StringCbPrintfW(volumeAddedToSnapshot, 4 * sizeof(WCHAR), L"%s", currentSourceDrive->source);
        }
        else if (wcscmp(volumeAddedToSnapshot, currentSourceDrive->source) == 0) {
            // on the same volume, skip
            currentSourceDrive = currentSourceDrive->next;
            currentSourceFilename = currentSourceFilename->next;
            continue;
        }
        else {
            // it is not the same volume, bail
            backupComponents->AbortBackup();
            wprintf(L"All source files must be on the same volume. The following file is not on the same volume as previous source files:\n%s\n", currentSourceFilename->source);  

            bail(SDEXIT_SOURCE_FILES_ON_DIFFERENT_VOLUMES);
        }
        result = backupComponents->AddToSnapshotSet(currentSourceDrive->source, GUID_NULL, snapshotId);
        genericFailCheck("AddToSnapshotSet", result);
        currentSourceDrive = currentSourceDrive->next;
        currentSourceFilename = currentSourceFilename->next;
    } while (currentSourceDrive != nullptr && currentSourceFilename != nullptr);
  

    // notify writers of impending backup
    result = backupComponents->PrepareForBackup(&vssAsync);
    genericFailCheck("PrepareForBackup", result);
    assert(vssAsync != nullptr);

    if (!quiet) {
        printf("Waiting for VSS writers to be ready for impending backup...\n");
    }
    while (asyncResult != VSS_S_ASYNC_CANCELLED && asyncResult != VSS_S_ASYNC_FINISHED) {
        Sleep(SHORT_SLEEP);
        result = vssAsync->QueryStatus(&asyncResult, NULL);
        if (result != S_OK) {
            printf("Unable to query vss async status -- %x\n", result);
            vssAsync->Release();
            vssAsync = nullptr;
            bail(result);
        }
        if (!quiet) {
            OutputDebugString(L"Waiting for PrepareForBackup VSS status...\n");
            spinProgress();
        }
    }

    if (asyncResult == VSS_S_ASYNC_CANCELLED) {
        printf("Operation was cancelled.");
        vssAsync->Release();
        vssAsync = nullptr;
        bail(asyncResult);
    }

    asyncResult = E_FAIL;
    vssAsync->Release();
    vssAsync = nullptr;

    // verify all VSS writers are in the correct state
    VerifyWriterStatus();

    // request shadow copy

    if (!quiet) {
        printf("Asking the OS to create a shadow copy...\n");
    }

    result = backupComponents->DoSnapshotSet(&vssAsync);
    genericFailCheck("DoSnapshotSet", result);

    while (asyncResult != VSS_S_ASYNC_CANCELLED && asyncResult != VSS_S_ASYNC_FINISHED) {
        Sleep(LONG_SLEEP);
        result = vssAsync->QueryStatus(&asyncResult, NULL);
        if (result != S_OK) {
            printf("Unable to query vss async status -- %x\n", result);
            vssAsync->Release();
            vssAsync = nullptr;
            bail(result);
        }
        
        if (!quiet) {
            OutputDebugString(L"Waiting for DoSnapshotSet status...\n");
            spinProgress();
        }        
    }

    if (asyncResult == VSS_S_ASYNC_CANCELLED) {
        printf("Operation was cancelled.");
        vssAsync->Release();
        vssAsync = nullptr;
        bail(asyncResult);
    }

    asyncResult = E_FAIL;
    vssAsync->Release();
    vssAsync = nullptr;

    // verify all VSS writers are in the correct state
    VerifyWriterStatus();

    // GetSnapshotProperties to get device to copy from
    result = backupComponents->GetSnapshotProperties(*snapshotId, &snapshotProp);
    genericFailCheck("GetSnapshotProperties", result);

    OutputDebugString(snapshotProp.m_pwszSnapshotDeviceObject);


    // remove the device specification (C:\) from each source file, so that it concats properly into the VSS device object specification
    currentSourceFilename = sourceFilenames; // point to the beginnings of the list
    currentSourceDrive = sourceDrives;
    do {
        // allocate a new sourceWithoutDrive
        if (sourceFilenamesWithoutDrives == nullptr) {
            sourceFilenamesWithoutDrives = (t_sourceList *)malloc(sizeof(t_sourceList));
            assert(sourceFilenamesWithoutDrives != nullptr);
            ZeroMemory(sourceFilenamesWithoutDrives, sizeof(t_sourceList));
            currentSourceFilenameWithoutDrive = sourceFilenamesWithoutDrives;
        }
        else {
            currentSourceFilenameWithoutDrive = (t_sourceList*)malloc(sizeof(t_sourceList));
            assert(currentSourceFilenameWithoutDrive != nullptr);
            ZeroMemory(currentSourceFilenameWithoutDrive, sizeof(t_sourceList));
        }

        currentSourceFilenameWithoutDrive->source = (LPWSTR)malloc(wcslen(currentSourceFilename->source) * sizeof(WCHAR));
        assert(currentSourceFilenameWithoutDrive->source != nullptr);
        // pull into new string starting at sourceDrive position in the filename string -- slice off the drive spec
        wcsncpy_s(currentSourceFilenameWithoutDrive->source,
            wcslen(currentSourceFilename->source),
            &currentSourceFilename->source[wcslen(currentSourceDrive->source)],
            wcslen(currentSourceFilename->source) - wcslen(currentSourceDrive->source)
        );

        // update tail pointer
        if (previousSourceFilenameWithoutDrive != nullptr) {
            previousSourceFilenameWithoutDrive->next = currentSourceFilenameWithoutDrive;
        }
        previousSourceFilenameWithoutDrive = currentSourceFilenameWithoutDrive;

        // loop to next item
        currentSourceDrive = currentSourceDrive->next;
        currentSourceFilename = currentSourceFilename->next;
    } while (currentSourceDrive != nullptr && currentSourceFilename != nullptr);
    
  
    if (selectedFilesMode)
    {
        currentSourceFilename = sourceFilenames; // point to the beginnings of the lists
        currentSourceDrive = sourceDrives;
        currentSourceFilenameWithoutDrive = sourceFilenamesWithoutDrives;

        // expect the lists to not be empty
        assert(currentSourceDrive != nullptr && currentSourceFilename != nullptr && currentSourceFilenameWithoutDrive != nullptr);

        // loop over each source drive/filename/filename without drive triplet and copy
        do {
            WCHAR sourcePathFile[MAX_PATH]{};
            WCHAR destinationPathFile[MAX_PATH]{};
            WCHAR baseNameAndExt[MAX_PATH]{};

            // build source and dest path
            StringCbPrintf((WCHAR*)&(sourcePathFile), MAX_PATH * sizeof(WCHAR), L"%s\\%s", snapshotProp.m_pwszSnapshotDeviceObject, currentSourceFilenameWithoutDrive->source);


            // get basename&ext of source file to make its final destination path from dir + basename
            WCHAR* lastBackslash = wcsrchr(sourcePathFile, L'\\');
            wcsncpy_s(
                baseNameAndExt,
                MAX_PATH * sizeof(WCHAR),
                lastBackslash,
                wcslen(sourcePathFile)
            );


            StringCbPrintf((WCHAR*)&*(destinationPathFile), MAX_PATH * sizeof(WCHAR), L"%s\%s", destDirectory, baseNameAndExt);

            copyError = ShadowCopyFile(sourcePathFile, destinationPathFile);
            if (copyError) {
                bail(copyError);
            }

            // loop to next items
            currentSourceDrive = currentSourceDrive->next;
            currentSourceFilename = currentSourceFilename->next;
            currentSourceFilenameWithoutDrive = currentSourceFilenameWithoutDrive->next;
        } while (currentSourceDrive != nullptr && currentSourceFilename != nullptr && currentSourceFilenameWithoutDrive != nullptr);
    }
    else
    {
        // multi-file mode

        WCHAR sourcePathFile[MAX_PATH]{};
        WCHAR destinationPathFile[MAX_PATH]{};
        currentSourceFilenameWithoutDrive = sourceFilenamesWithoutDrives;
        assert(currentSourceFilenameWithoutDrive != nullptr);

        // calculate file paths
        WCHAR sourceShadowPathWithWildcard[MAX_PATH] = L"";
        WCHAR directorySpec[3] = L"";
        StringCbPrintf(directorySpec, (3 * sizeof(WCHAR)) /* turns out this in bytes */, L"\\*"); // this adds the "*" wildcard to copy all items

        StringCbPrintf(sourceShadowPathWithWildcard, MAX_PATH * sizeof(WCHAR), L"%s\\%s%s", snapshotProp.m_pwszSnapshotDeviceObject, currentSourceFilenameWithoutDrive->source, directorySpec);

         
        // find files in directory
        HANDLE findHandle = INVALID_HANDLE_VALUE;
        findHandle = FindFirstFile(sourceShadowPathWithWildcard, &findData);

        if (findHandle == INVALID_HANDLE_VALUE) {
            printf("Unable to find the first file in the source.\n");
            bail(SDEXIT_NO_FIRST_FILE_IN_SOURCE);
        }

        do {
            if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0) {
                continue;
            }

            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                // does not currently back up sub directories
                if (!quiet) {
                    printf("WARNING: ShadowDuplicator does not presently back up subdirectories.\n");
                }
                continue;
            }

            // build source and destination path for files
            StringCbPrintf((WCHAR*)&(sourcePathFile), MAX_PATH * sizeof(WCHAR), L"%s\\%s\\%s", snapshotProp.m_pwszSnapshotDeviceObject, currentSourceFilenameWithoutDrive->source, findData.cFileName);
            StringCbPrintf((WCHAR*)&(destinationPathFile), MAX_PATH * sizeof(WCHAR), L"%s\\%s", destDirectory, findData.cFileName);

            copyError = ShadowCopyFile(sourcePathFile, destinationPathFile);
            if (copyError) {
                bail(copyError);
            }

        } while (FindNextFile(findHandle, &findData) != 0);
    }
    
    // free writer metadata
    result = backupComponents->FreeWriterMetadata();
    genericFailCheck("FreeWriterMetadata", result);

    VssFreeSnapshotProperties(&snapshotProp);

    if (!quiet) {
        printf("Completed all copy operations successfully.\n\n");
        printf("Notifying VSS components of the completion of the backup...\n");
    }

    // set backup succeeded

    asyncResult = E_FAIL;
    result = backupComponents->BackupComplete(&vssAsync);
    genericFailCheck("BackupComplete", result);

    shouldAbortBackupOnBail = FALSE;

    while (asyncResult != VSS_S_ASYNC_CANCELLED && asyncResult != VSS_S_ASYNC_FINISHED) {
        Sleep(SHORT_SLEEP);
        result = vssAsync->QueryStatus(&asyncResult, NULL);
        if (result != S_OK) {
            printf("Unable to query vss async status -- %x\n", result);
            bail(result);
        }

        if (!quiet) {
            OutputDebugString(L"Waiting for BackupComplete status...\n");
            spinProgress();
        }
    }

    // final verification of writer status
    VerifyWriterStatus();

    if (!quiet) {
        printf("All operations completed.\n");
    }

    bail(0);
}

/// <summary>
/// Perform the copy of a file from the source path to the destination.
/// </summary>
/// <param name="sourcePathFile">The source path, with the VSS snapshot device object already substituted in</param>
/// <param name="destinationPathFile">The destination path</param>
/// <returns>0 on success, or the DWORD from GetLastError() upon failure</returns>
DWORD ShadowCopyFile(WCHAR  sourcePathFile[MAX_PATH], WCHAR  destinationPathFile[MAX_PATH])
{
    DWORD error = 0;

    if (!quiet) {
        wprintf(L"%s -> %s\n", sourcePathFile, destinationPathFile);
    }

    BOOL copyResult = CopyFileEx(sourcePathFile, destinationPathFile, (LPPROGRESS_ROUTINE)&copyProgress, NULL, FALSE, 0);

    if (!copyResult) {
        error = GetLastError();
        if (error) {
            friendlyCopyError(L"Failed to copy to ", destinationPathFile, error); // friendlyCopyError does not bail for us
            return error;
        }
    }

    return error;
}

/// <summary>
/// Display a formatted error string, looking up the Win32 error code and displaying
/// its explanation.
/// </summary>
/// <param name="ourErrorDescription">The ShadowDuplicator error description</param>
/// <param name="error">The error code as returned from GetLastError()</param>
void friendlyError(LPCWSTR ourErrorDescription, const DWORD error)
{
    LPTSTR errorBuffer = nullptr;

    FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_ALLOCATE_BUFFER,
        NULL,
        error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&errorBuffer,
        MAX_PATH,
        NULL);

    assert(errorBuffer != nullptr);

    wprintf(L"%s: 0x%x %s", ourErrorDescription, error, errorBuffer);
    LocalFree(errorBuffer);
    errorBuffer = nullptr;

    bail(error);
}

/// <summary>
/// Display a formatted error string for a copy failure, looking up the Win32 error code and displaying
/// its explanation. DOES NOT EXIT itself
/// </summary>
/// <param name="ourErrorDescription">The ShadowDuplicator error description</param>
/// <param name="error">The error code as returned from GetLastError()</param>
void friendlyCopyError(LPCWSTR ourErrorDescription, LPWSTR destinationFile, const DWORD error)
{
    LPTSTR errorBuffer = nullptr;

    FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_ALLOCATE_BUFFER,
        NULL,
        error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&errorBuffer,
        MAX_PATH,
        NULL);

    assert(errorBuffer != nullptr);

    wprintf(L"%s \"%s\": 0x%x %s", ourErrorDescription, destinationFile, error, errorBuffer);
    LocalFree(errorBuffer);
    errorBuffer = nullptr;
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
/// Free all source filename and drive structures.
/// </summary>
/// <param name=""></param>
void FreeSourceStructures(void) {
    t_sourceList* tempNextItem;

    currentSourceDrive = sourceDrives;
    currentSourceFilename = sourceFilenames;
    currentSourceFilenameWithoutDrive = sourceFilenamesWithoutDrives;

    while (currentSourceDrive != nullptr) {
        tempNextItem = currentSourceDrive->next;
        free(currentSourceDrive->source);
        currentSourceDrive->source = nullptr;
        free(currentSourceDrive);
        currentSourceDrive = tempNextItem;
    }

    while (currentSourceFilename != nullptr) {
        tempNextItem = currentSourceFilename->next;
        free(currentSourceFilename->source);
        currentSourceFilename->source = nullptr;
        free(currentSourceFilename);
        currentSourceFilename = tempNextItem;
    }
    while (currentSourceFilenameWithoutDrive != nullptr) {
        tempNextItem = currentSourceFilenameWithoutDrive->next;
        free(currentSourceFilenameWithoutDrive->source);
        currentSourceFilenameWithoutDrive->source = nullptr;
        free(currentSourceFilenameWithoutDrive);
        currentSourceFilenameWithoutDrive = tempNextItem;
    }

    sourceDrives = nullptr;
    sourceFilenames = nullptr;
    sourceFilenamesWithoutDrives = nullptr;
    currentSourceDrive = nullptr;
    currentSourceFilename = nullptr;
    currentSourceFilenameWithoutDrive = nullptr;
    previousSourceDrive = nullptr;
    previousSourceFilename = nullptr;
    previousSourceFilenameWithoutDrive = nullptr;
}

/// <summary>
/// Tidy up any objects and uninitialize before an exit.
/// </summary>
/// <param name="exitCode">The exit code to provide to the OS.</param>
void bail(HRESULT exitCode) {
    FreeSourceStructures();
    if (destDirectory != nullptr) {
        free(destDirectory);
        destDirectory = nullptr;
    }
    if (canonicalINIPath != nullptr) {
        free(canonicalINIPath);
        canonicalINIPath = nullptr;
    }

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
    

    if (backupComponents != nullptr) {
        if (shouldAbortBackupOnBail) {
            HRESULT abortResult = backupComponents->AbortBackup();
            if (abortResult != S_OK) {
                wprintf(L"Failed to abort the backup with error 0x%x\n", abortResult);
            }
        }

        // free writer metadata
        backupComponents->FreeWriterMetadata();

        backupComponents->Release();
        backupComponents = nullptr;
    }
    
    if (comInitialized) {
        CoUninitialize();
    }
    exit(exitCode);
}

/// <summary>
/// Verify the VSS writers are all in the correct state.
/// </summary>
/// <param name=""></param>
void VerifyWriterStatus(void) {
    HRESULT result = E_FAIL;
    HRESULT asyncResult = E_FAIL;

    // verify writer status
    result = backupComponents->GatherWriterStatus(&vssAsync);
    genericFailCheck("GatherWriterStatus", result);
    assert(vssAsync != nullptr);
    while (asyncResult != VSS_S_ASYNC_CANCELLED && asyncResult != VSS_S_ASYNC_FINISHED) {
        Sleep(SHORT_SLEEP);
        result = vssAsync->QueryStatus(&asyncResult, NULL);
        if (result != S_OK) {
            printf("Unable to query vss async status -- %x\n", result);
            vssAsync->Release();
            vssAsync = nullptr;
            bail(result);
        }
        if (!quiet) {
            OutputDebugString(L"Waiting for GatherWriterStatus VSS status...\n");
            spinProgress();
        }
    }

    if (asyncResult == VSS_S_ASYNC_CANCELLED) {
        printf("Operation was cancelled.");
        vssAsync->Release();
        vssAsync = nullptr;
        bail(asyncResult);
    }

    // get count of writers
    UINT writerCount = 0;
    result = backupComponents->GetWriterStatusCount(&writerCount);
    if (result != S_OK) {
        printf("Unable to get count of writers -- %x\n", result);
        backupComponents->FreeWriterStatus();
        vssAsync->Release();
        vssAsync = nullptr;
        bail(result);
    }

    // check status of writers
    for (UINT i = 0; i < writerCount; i++) {
        VSS_ID pidInstance = {};
        VSS_ID pidWriter = {};
        BSTR nameOfWriter = nullptr;
        VSS_WRITER_STATE state = { };
        HRESULT vssFailure = {};
        WCHAR writerDebugString[512] = {};
        result = backupComponents->GetWriterStatus(i, &pidInstance, &pidWriter, &nameOfWriter, &state, &vssFailure);
        if (result != S_OK) {
            printf("Unable to get status of VSS writer %i -- %x\n", i, result);
            SysFreeString(nameOfWriter); //safe even if nameOfWriter == nullptr
            backupComponents->FreeWriterStatus();
            vssAsync->Release();
            vssAsync = nullptr;
            bail(result);
        }

        if (!quiet) {
            StringCbPrintf(writerDebugString, 511 * sizeof(WCHAR), L"Status of writer %i (%s) is 0x%x.\n", i, nameOfWriter, vssFailure);
            OutputDebugString(writerDebugString);
        }

        if (vssFailure == 0) {
            // this writer is happy
            SysFreeString(nameOfWriter);
            continue;
        }

        StringCbPrintf(writerDebugString, 511 * sizeof(WCHAR), L"Unable to proceed, as the status of VSS writer %i (%s) is 0x%x.\n", i, nameOfWriter, vssFailure);
        OutputDebugString(writerDebugString);
        wprintf(writerDebugString);

        SysFreeString(nameOfWriter);
        backupComponents->FreeWriterStatus();
        vssAsync->Release();
        vssAsync = nullptr;
        bail(vssFailure);
    }

    backupComponents->FreeWriterStatus();
}

/// <summary>
/// An ASCII art banner because one must have one of these.
/// </summary>
/// <param name=""></param>
void banner(void) {
    const char banner[] = " #####                                     ######                                                            \n"
"#     # #    #   ##   #####   ####  #    # #     # #    # #####  #      #  ####    ##   #####  ####  #####   \n"
"#       #    #  #  #  #    # #    # #    # #     # #    # #    # #      # #    #  #  #    #   #    # #    #  \n"
" #####  ###### #    # #    # #    # #    # #     # #    # #    # #      # #      #    #   #   #    # #    #  \n"
"      # #    # ###### #    # #    # # ## # #     # #    # #####  #      # #      ######   #   #    # #####   \n"
"#     # #    # #    # #    # #    # ##  ## #     # #    # #      #      # #    # #    #   #   #    # #   #   \n"
" #####  #    # #    # #####   ####  #    # ######   ####  #      ###### #  ####  #    #   #    ####  #    #  \n";
    
    
    printf("%s\n", banner);
    printf("ShadowDuplicator -- Copyright (C) 2021-2022 Peter Upfold\n");
    wprintf(SDVERSION);
    printf("\n\n");
    printf("https://peter.upfold.org.uk/projects/shadowduplicator\n");
    printf("\n");
}

/// <summary>
/// Print a usage statement
/// </summary>
/// <param name=""></param>
void usage(void) {
    printf("ShadowDuplicator -- Copyright (C) 2021-2022 Peter Upfold\n");
    wprintf(SDVERSION);
    printf("\n\n");
    printf("Usage: ShadowDuplicator.exe [OPTIONS] INI-FILE\n");
    printf(" or single file mode:\n");
    printf("Usage: ShadowDuplicator.exe -s [SOURCE] [DEST_DIRECTORY_AND_FILENAME]\n");
    printf("\n");
    printf("Multi File Example:  ShadowDuplicator.exe -q BackupConfig.ini\n");
    printf("Single File Example: ShadowDuplicator.exe -q -s SourceFile.txt D:\\DestDirectory\\DestFile.txt\n");
    printf("\n");
    printf("\n");
    printf("\n");
    printf("Options:\n");
    printf("-h, --help, -?, /?, --usage     Print this help message\n");
    printf("-q                              Silence the banner and any progress messages\n");
    printf("-s, --singlefile                Single file mode -- copy one source file to the destination directory only\n");
    printf("\n");
    printf("The path to the INI file must not begin with '-'.\n");
    printf("The INI file should be as follows:\n\n");
    printf("[FileSet]\nSource = C:\\Users\\Public\\Documents\nDestination = D:\\test\n");
    printf("Do not include trailing slashes in paths.\n");
    printf("\n");
    printf("In single-file mode, you must provide the full destination path, including destination file name in the\n");
    printf("directory.\n");
    printf("\n");
    printf("WARNING: Copies will always overwrite items in the destination.\n");
}

/// <summary>
/// Update a progress spinner.
/// </summary>
/// <param name=""></param>
void spinProgress(void) {
    switch (progressMarker) {
    case 0:
        printf("/ \r");
        break;
    case 1:
        printf("- \r");
        break;
    case 2:
        printf("\\ \r");
        break;
    case 3:
        printf("| \r");
        break;
    }

    progressMarker++;
    if (progressMarker > 3) {
        progressMarker = 0;
    }
}

/// <summary>
/// Spin a determinate progress indicator for file copies.
/// </summary>
/// <param name="total">Total number of bytes</param>
/// <param name="transferred">Number of bytes transferred</param>
void determinateProgress(LARGE_INTEGER total, LARGE_INTEGER transferred) {
    LONGLONG totalLong = total.QuadPart;
    totalLong /= 1024 * 1024;
    LONGLONG transferredLong = transferred.QuadPart;
    transferredLong /= 1024 * 1024;

    printf("%lld/%lld MiB copied... \r", transferredLong, totalLong);
}

/// <summary>
/// Callback for the file copy progress.
/// </summary>
/// <param name="TotalFileSize"></param>
/// <param name="TotalBytesTransferred"></param>
/// <param name="StreamSize"></param>
/// <param name="StreamBytesTransferred"></param>
/// <param name="dwStreamNumber"></param>
/// <param name="dwCallbackReason"></param>
/// <param name="hSourceFile"></param>
/// <param name="hDestinationFile"></param>
/// <param name="lpData"></param>
/// <returns></returns>
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
) {
    if (!quiet) {
        determinateProgress(TotalFileSize, TotalBytesTransferred);
    }
    return PROGRESS_CONTINUE;
}