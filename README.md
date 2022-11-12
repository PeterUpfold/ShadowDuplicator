# ShadowDuplicator

Command line Volume Shadow Copy backup client which has two modes:

 * (non-recursively) copies files from a source directory to a destination directory (provide an INI file to configure)
 * copies selected files provided on the command line to the destination directory (last command line argument)

This is useful for backing up files which are typically locked for reading and creating crash-consistent
copies of content, for example virtual machine hard disk files without shutting the VMs down.

Perhaps this is even just a useful substitute for the lack of a `vssadmin create shadow` command on Windows
client SKUs. 😉

ShadowDuplicator is written in C++, working directly with the Volume Shadow Copy API and other Win32 APIs for copying files.

This software comes with no warranty and no assumptions should be made about its stability or fitness
for any particular purpose.

## Licence

Apache 2.0. Please see `LICENSE`.

## Usage

    Usage: ShadowDuplicator.exe [OPTIONS] INI-FILE
    
    or selected files mode:

    Usage: ShadowDuplicator.exe -s SOURCE [SOURCE2 [SOURCE3] ...] DEST_DIRECTORY

    Whole Folder Mode Example:  ShadowDuplicator.exe -q BackupConfig.ini
    Selected Files Example: ShadowDuplicator.exe -q -s SourceFile.txt SourceFile2.txt D:\DestDirectory


    Options:
    -h, --help, -?, /?, --usage     Print this help message
    -q                              Silence the banner and any progress messages
    -s, --selected                  Selected files mode -- copy source files to the destination directory (the last command line argument)

    The path to the INI file or any source file must not begin with '-'.
    The INI file should be as follows:

    [FileSet]
    Source = C:\Users\Public\Documents
    Destination = D:\test
    Do not include trailing slashes in paths.

    In selected-files mode, you must provide the destination directory path only.

    WARNING: Copies will always overwrite items in the destination without confirmation.

Please install the [latest supported Visual C++ redistributable (x64)](https://docs.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170#visual-studio-2015-2017-2019-and-2022) before trying to launch.

## Exit Codes

To aid automated usage (in addition to `-q` for quiet operation), ShadowDuplicator will exit with a
process exit code that is the Win32 error code of the last operation that failed.

You should be able to assume that when ShadowDuplicator exits with code `0`, that all copy operations
have been completed successfully.

Additionally, the following exit codes are specific to ShadowDuplicator and indicate the following
conditions:

| Code (hex) | Code (dec) | Constant                                 | Meaning                                                |
| ---------- | ---------- | ---------------------------------------- | ------------------------------------------------------ |
| 0x20000001 | 536870913  | SDEXIT_NO_DEST_DIR_SPECIFIED             | No destination directory specified on command line.    |
| 0x20000002 | 536870914  | SDEXIT_NO_FIRST_FILE_IN_SOURCE           | Could not find any files in the source directory.      |
| 0x20000003 | 536870915  | SDEXIT_NO_SOURCE_SPECIFIED               | No source file or directory specified on command line. |
| 0x20000004 | 536870916  | SDEXIT_SOURCE_FILES_ON_DIFFERENT_VOLUMES | All source files must be on the same volume. This error is returned if this constraint is violated. |
| 0x20000005 | 536870917  | SDEXIT_INVALID_ARGS                      | Arguments could not be parsed from command line. Usage message will have been displayed. |

## Disclaimer

This code is **not** production quality, however, _I_ am using it in production at my own
risk. You would be using it at your own risk! I am learning how to work with Win32 APIs and work
with memory management etc. There is certainly plenty of potential for improvement of this code,
but beyond that, it may even be insecure, destructive or cause you other serious problems. There 
is no warranty.

## Limitations

Recursive copying of directories is not yet supported.

File handling is limited by `MAX_PATH`.
