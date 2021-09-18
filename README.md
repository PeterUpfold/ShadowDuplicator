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
    Example: ShadowDuplicator.exe -q BackupConfig.ini



    Options:
    -h, --help, -?, /?, --usage     Print this help message
    -q                              Silence the banner and any progress messages

    The path to the INI file must not begin with '-'.
    The INI file should be as follows:

    [FileSet]
    Source = C:\Users\Public\Documents
    Destination = D:\test
    Do not include trailing slashes in paths.

An example INI configuration file:

    [FileSet]
    Source=C:\Users\Public\Documents
    Destination=H:\test

## Disclaimer

This code is **not** production quality. I am learning how to work with Win32 APIs and work
with memory management etc. There is certainly plenty of potential for improvement of this code,
but beyond that, it may even be insecure, destructive or cause you other serious problems. There 
is no warranty.

## Limitations

Not currently recursively copying.

File handling is limited by `MAX_PATH`.