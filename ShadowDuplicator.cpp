/* ShadowDuplicator -- a simple VC++ Volume Shadow Copy requestor for backing up
locked files

Copyright (C) 2021 Peter Upfold.

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
#include "ShadowDuplicator.h"

#define assert(expression) if (!(expression)) { printf("assert on %d", __LINE__); bail(250); }

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
/// The source directory to back up.
/// </summary>
LPSTR sourceDirectory = nullptr;

/// <summary>
/// The destination directory where backed up files should be copied.
/// </summary>
LPSTR destDirectory = nullptr;

/// <summary>
/// Wide string version of source directory to back up.
/// </summary>
LPWSTR sourceDirectoryWide = nullptr;

/// <summary>
/// The source directory without the drive specifier.
/// </summary>
LPWSTR sourceDirectoryWithoutDrive = nullptr;

/// <summary>
/// Wide string version of destination directory.
/// </summary>
LPWSTR destDirectoryWide = nullptr;

/// <summary>
/// The source drive of which to make a shadow copy.
/// </summary>
LPSTR sourceDrive = nullptr;

/// <summary>
/// The canonical, fully qualified path to the INI file which provides
/// options for this execution run.
/// </summary>
LPSTR canonicalINIPath = nullptr;

/// <summary>
/// Is COM initialized? For only calling CoUninitialize() in bail() if we 
/// have initialized it.
/// </summary>
BOOL comInitialized = FALSE;

/// <summary>
/// Keep global state for a visible spinner to show progress.
/// </summary>
int progressMarker = 0;

/// <summary>
/// Whether to display progres and verbose status.
/// </summary>
BOOL quiet = FALSE;

#define SHORT_SLEEP 500
#define LONG_SLEEP 1500

/// <summary>
/// Entry point
/// </summary>
/// <param name="argc"></param>
/// <param name="argv"></param>
/// <returns></returns>
int main(int argc, char** argv)
{
    HRESULT result = E_FAIL;
    HRESULT asyncResult = E_FAIL;
    VSS_SNAPSHOT_PROP snapshotProp{};
    
    DWORD fileAttributes = INVALID_FILE_ATTRIBUTES;
    DWORD error = 0;
    LPWSTR errorBuffer = nullptr;

    WIN32_FIND_DATA findData{};

    // loop over command line options -- _very_ simple parsing
    if (argc < 2) {
        usage();
        exit(0);
    }
    for (int i = 1; i < argc; i++) {
        if (argv[i] == "/?") {
            usage();
            exit(0);
        }
        // handle switches
        if (argv[i][0] == (char)"-") {
            if (argv[i] == "-q") {
                quiet = TRUE;
            }
            if (argv[i] == "-h" || argv[i] == "--help" || argv[i] == "-?" || argv[i] == "--usage") {
                usage();
                exit(0);
            }
        }
        // check INI file
        else {
            fileAttributes = GetFileAttributesA(argv[i]);

            if (fileAttributes == INVALID_FILE_ATTRIBUTES) {
                error = GetLastError();

                errorBuffer = (LPWSTR)malloc(MAX_PATH * 2); /* wide */
                assert(errorBuffer != nullptr);

                FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                    NULL,
                    error,
                    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                    errorBuffer,
                    MAX_PATH,
                    NULL);

                wprintf(L"Failed to check INI file: 0x%x %s", error, errorBuffer);
                free(errorBuffer);
                errorBuffer = nullptr;

                exit(error);
            }

            canonicalINIPath = (LPSTR)malloc(MAX_PATH);

            // canonicalise path
            if (!(GetFullPathNameA(argv[i], MAX_PATH, canonicalINIPath, NULL))) {
                error = GetLastError();

                errorBuffer = (LPWSTR)malloc(MAX_PATH * 2); /* wide */
                assert(errorBuffer != nullptr);

                FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                    NULL,
                    error,
                    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                    errorBuffer,
                    MAX_PATH,
                    NULL);

                wprintf(L"Failed to get full path name of specified INI file: 0x%x %s", error, errorBuffer);
                free(errorBuffer);
                errorBuffer = nullptr;

                exit(error);
            }

            // allocate space for source and destination paths
            sourceDirectory = (LPSTR)malloc(MAX_PATH);
            destDirectory = (LPSTR)malloc(MAX_PATH);

            assert(sourceDirectory != nullptr);
            assert(destDirectory != nullptr);
            
            // get source from INI
            GetPrivateProfileStringA(
                "FileSet",
                "Source",
                "",
                sourceDirectory,
                MAX_PATH,
                canonicalINIPath
            );

            error = GetLastError();
            if (error) {
                errorBuffer = (LPWSTR)malloc(MAX_PATH*2) /* wide string */;
                assert(errorBuffer != nullptr);

                FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                    NULL,
                    error,
                    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                    errorBuffer,
                    MAX_PATH,
                    NULL);

                wprintf(L"Failed to import Source from INI file: 0x%x %s", error, errorBuffer);
                free(errorBuffer);
                errorBuffer = nullptr;

                exit(error);
            }

            // get dest from INI
            GetPrivateProfileStringA(
                "FileSet",
                "Destination",
                "",
                destDirectory,
                MAX_PATH,
                canonicalINIPath
            );

            error = GetLastError();
            if (error) {
                errorBuffer = (LPWSTR)malloc(MAX_PATH * 2) /* wide string */;
                assert(errorBuffer != nullptr);

                FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                    NULL,
                    error,
                    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                    errorBuffer,
                    MAX_PATH,
                    NULL);

                wprintf(L"Failed to import Destination from INI file: 0x%x %s", error, errorBuffer);
                free(errorBuffer);
                errorBuffer = nullptr;

                exit(error);
            }


            // get source drive from source directory
            sourceDrive = (LPSTR)malloc(MAX_PATH);
            assert(sourceDrive != nullptr);

            if (!GetVolumePathNameA(sourceDirectory, sourceDrive, MAX_PATH)) {
                error = GetLastError();
                if (error) {
                    errorBuffer = (LPWSTR)malloc(MAX_PATH * 2) /* wide string */;
                    assert(errorBuffer != nullptr);

                    FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                        NULL,
                        error,
                        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                        errorBuffer,
                        MAX_PATH,
                        NULL);

                    wprintf(L"Failed to get Source Drive from Source Directory: 0x%x %s", error, errorBuffer);
                    free(errorBuffer);
                    errorBuffer = nullptr;

                    exit(error);
                }
            }

        }
    }

    

    if (!quiet) {
        banner();
    }

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
    

    while (asyncResult != VSS_S_ASYNC_CANCELLED && asyncResult != VSS_S_ASYNC_FINISHED) {
        Sleep(500);
        result = vssAsync->QueryStatus(&asyncResult, NULL);
        if (result != S_OK) {
            printf("Unable to query vss async status -- %x\n", result);
            vssAsync->Release();
            vssAsync = nullptr;
            bail(result);
        }
        OutputDebugString(L"Waiting for async VSS status...");
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

    snapshotId = (VSS_ID*)malloc(sizeof(VSS_ID));
    assert(snapshotId != nullptr);

    // add volume to snapshot set AddToSnapshotSet
    VSS_PWSZ sourceDriveWide = nullptr;
    int sourceDriveWideBufferSize = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, sourceDrive, -1, sourceDriveWide, 0); // get size first
    sourceDriveWide = (VSS_PWSZ)malloc(sourceDriveWideBufferSize*2); // alloc that size (*2 -- returns number of chars)
    assert(sourceDriveWide != nullptr);
    // re-run to actually get chars into buffer
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, sourceDrive, -1, sourceDriveWide, sourceDriveWideBufferSize);
    
    result = backupComponents->AddToSnapshotSet(sourceDriveWide, GUID_NULL, snapshotId);
    free(sourceDriveWide);
    sourceDriveWide = nullptr;
    genericFailCheck("AddToSnapshotSet", result);

    // notify writers of impending backup
    result = backupComponents->PrepareForBackup(&vssAsync);
    genericFailCheck("PrepareForBackup", result);
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

    // verify writer status
    result = backupComponents->GatherWriterStatus(&vssAsync);
    // TODO finish this

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

    //TODO we should verify writer status 

    // GetSnapshotProperties to get device to copy from
    result = backupComponents->GetSnapshotProperties(*snapshotId, &snapshotProp);
    genericFailCheck("GetSnapshotProperties", result);

    OutputDebugString(snapshotProp.m_pwszSnapshotDeviceObject);

    // make wide versions of source and dest
    int sourceDirectoryWideBufferSize = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, sourceDirectory, -1, sourceDirectoryWide, 0);
    sourceDirectoryWide = (LPWSTR)malloc(sourceDirectoryWideBufferSize * 2);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, sourceDirectory, -1, sourceDirectoryWide, sourceDirectoryWideBufferSize);
    int destDirectoryWideBufferSize = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, destDirectory, -1, destDirectoryWide, 0);
    destDirectoryWide = (LPWSTR)malloc(destDirectoryWideBufferSize * 2);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, destDirectory, -1, destDirectoryWide, destDirectoryWideBufferSize);

    // remove the device specification (C:\) from sourceDirectoryWide, so that it concats properly into the VSS device object specification

    sourceDriveWideBufferSize = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, sourceDrive, -1, sourceDriveWide, 0); // get size first
    sourceDriveWide = (VSS_PWSZ)malloc(sourceDriveWideBufferSize * 2); // alloc that size (*2 -- returns number of chars)
    assert(sourceDriveWide != nullptr);
    // re-run to actually get chars into buffer
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, sourceDrive, -1, sourceDriveWide, sourceDriveWideBufferSize);

    sourceDirectoryWithoutDrive = (LPWSTR)malloc(wcslen(sourceDirectoryWide) * 2);
    assert(sourceDirectoryWithoutDrive != nullptr);
    // pull into  new string starting at sourceDriveWide position
    wcsncpy_s(sourceDirectoryWithoutDrive,
        wcslen(sourceDirectoryWide),
        &sourceDirectoryWide[wcslen(sourceDriveWide)],
        wcslen(sourceDirectoryWide) - wcslen(sourceDriveWide)
    );

    free(sourceDriveWide);
    sourceDriveWide = nullptr;

    // calculate file paths
    WCHAR sourcePathWithDeviceObject[MAX_PATH] = L"";
    StringCbPrintf(sourcePathWithDeviceObject, MAX_PATH, L"%s\\%s\\*", snapshotProp.m_pwszSnapshotDeviceObject, sourceDirectoryWithoutDrive);

    // find files in directory
    HANDLE findHandle = INVALID_HANDLE_VALUE;
    findHandle = FindFirstFile(sourcePathWithDeviceObject, &findData);

    if (findHandle == INVALID_HANDLE_VALUE) {
        printf("Unable to find the first file in the source.\n");
        bail(2);
    }

    WCHAR sourcePathFile[MAX_PATH]{};
    WCHAR destinationPathFile[MAX_PATH]{};

    do {
        if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0) {
            continue;
        }

        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // does not currently back up sub directories
            continue;
        }

        // build source and destination path for files
        StringCbPrintf((WCHAR*)&(sourcePathFile), MAX_PATH, L"%s\\%s\\%s", snapshotProp.m_pwszSnapshotDeviceObject, sourceDirectoryWithoutDrive, findData.cFileName);
        StringCbPrintf((WCHAR*)&(destinationPathFile), MAX_PATH, L"%s\\%s", destDirectoryWide, findData.cFileName);

        if (!quiet) {
            wprintf(L"%s -> %s\n", sourcePathFile, destinationPathFile);
        }        
        
        BOOL copyResult = CopyFileEx(sourcePathFile, destinationPathFile, (LPPROGRESS_ROUTINE)&copyProgress, NULL, FALSE, 0);

        if (!copyResult) {
            error = GetLastError();
            if (error) {
                errorBuffer = (LPWSTR)malloc(MAX_PATH * 2) /* wide string */;
                assert(errorBuffer != nullptr);

                FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                    NULL,
                    error,
                    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                    errorBuffer,
                    MAX_PATH,
                    NULL);

                wprintf(L"Failed to copy to %s: 0x%x %s", destinationPathFile, error, errorBuffer);
                free(errorBuffer);
                errorBuffer = nullptr;
                bail(error);
            }
        }

    } while (FindNextFile(findHandle, &findData) != 0);
    
    // free writer metadata
    result = backupComponents->FreeWriterMetadata();
    genericFailCheck("FreeWriterMetadata", result);

    VssFreeSnapshotProperties(&snapshotProp);

    if (!quiet) {
        printf("Completed all copy operations successfully.\n");
    }

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
    if (sourceDirectory != nullptr) {
        free(sourceDirectory);
        sourceDirectory = nullptr;
    }

    if (destDirectory != nullptr) {
        free(destDirectory);
        destDirectory = nullptr;
    }
    if (sourceDirectoryWide != nullptr) {
        free(sourceDirectoryWide);
        sourceDirectoryWide = nullptr;
    }
    if (sourceDirectoryWithoutDrive != nullptr) {
        free(sourceDirectoryWithoutDrive);
        sourceDirectoryWithoutDrive = nullptr;
    }
    if (destDirectoryWide != nullptr) {
        free(destDirectoryWide);
        destDirectoryWide = nullptr;
    }

    if (canonicalINIPath != nullptr) {
        free(canonicalINIPath);
        canonicalINIPath = nullptr;
    }

    if (sourceDrive != nullptr) {
        free(sourceDrive);
        sourceDrive = nullptr;
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
        backupComponents->Release();
        backupComponents = nullptr;
    }
    
    if (comInitialized) {
        CoUninitialize();
    }
    exit(exitCode);
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
    printf("ShadowDuplicator -- Copyright (C) 2021 Peter Upfold\n");
    printf("\n");
}

/// <summary>
/// Print a usage statement
/// </summary>
/// <param name=""></param>
void usage(void) {
    printf("Usage: ShadowDuplicator.exe [OPTIONS] INI-FILE\n");
    printf("Example: ShadowDuplicator.exe -q BackupConfig.ini\n");
    printf("\n");
    printf("\n");
    printf("\n");
    printf("Options:\n");
    printf("-h, --help, -?, /?, --usage     Print this help message\n");
    printf("-q                              Silence the banner and any progress messages\n");
    printf("\n");
    printf("The path to the INI file must not begin with '-'.\n");
    printf("The INI file should be as follows:\n\n");
    printf("[FileSet]\nSource = C:\\Users\\Public\\Documents\nDestination = D:\\test\n");
    printf("Do not include trailing slashes in paths.\n");
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