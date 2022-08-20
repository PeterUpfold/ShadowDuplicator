# ShadowDuplicator

Rudimentary Volume Shadow Copy backup client which (non-recursively) copies files from a source directory to
a destination directory, having first created a Volume Shadow Copy. Useful for backing up files which are typically
locked for reading.

Perhaps this is even just a useful substitute for the lack of a `vssadmin create shadow` command on Windows client SKUs. ;)

ShadowDuplicator is written in C++, working directly with the Volume Shadow Copy API and other Win32 APIs for copying files. The
software is configured using simple INI files.

## Licence

Apache 2.0. Please see `LICENSE`.

## Usage

    Usage: ShadowDuplicator.exe [OPTIONS] INI-FILE
    or single file mode:
    Usage: ShadowDuplicator.exe -s [SOURCE] [DEST_DIRECTORY]

    Multi File Example:  ShadowDuplicator.exe -q BackupConfig.ini
    Single File Example: ShadowDuplicator.exe -q -s SourceFile.txt D:\DestDirectory



    Options:
    -h, --help, -?, /?, --usage     Print this help message
    -q                              Silence the banner and any progress messages
    -s, --singlefile                Single file mode -- copy one source file to the destination directory only

    The path to the INI file must not begin with '-'.
    The INI file should be as follows:

    [FileSet]
    Source = C:\Users\Public\Documents
    Destination = D:\test
    Do not include trailing slashes in paths.

    In single-file mode, the destination file will always have the same basename + extension
    as the original source file. Only provide the destination DIRECTORY as the second argument.

    WARNING: Copies will always overwrite items in the destination.

Please install the [latest supported Visual C++ redistributable (x64)](https://docs.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170#visual-studio-2015-2017-2019-and-2022) before trying to launch.

## Exit Codes

To aid automated usage (in addition to `-q` for quiet operation), ShadowDuplicator will exit with a
process exit code that is the Win32 error code of the last operation that failed.

You should be able to assume that when ShadowDuplicator exits with code `0`, that all copy operations
have been completed successfully.

Additionally, the following exit codes are specific to ShadowDuplicator and indicate the following
conditions:

| Code (hex) | Code (dec) | Constant                       | Meaning                                                |
| ---------- | ---------- | ------------------------------ | ------------------------------------------------------ |
| 0x20000001 | 536870913  | SDEXIT_NO_DEST_DIR_SPECIFIED   | No destination directory specified on command line.    |
| 0x20000002 | 536870914  | SDEXIT_NO_FIRST_FILE_IN_SOURCE | Could not find any files in the source directory.      |
| 0x20000003 | 536870915  | SDEXIT_NO_SOURCE_SPECIFIED     | No source file or directory specified on command line. |

## Disclaimer

This code is **not** production quality, however, _I_ am using it in production at my own
risk. You would be using it at your own risk! I am learning how to work with Win32 APIs and work
with memory management etc. There is certainly plenty of potential for improvement of this code,
but beyond that, it may even be insecure, destructive or cause you other serious problems. There 
is no warranty.

## Limitations

Recursive copying of directories is not yet supported.

File handling is limited by `MAX_PATH`.