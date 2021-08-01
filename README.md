# ShadowDuplicator

Playing around with creating VSS snapshots within a C++ project.

## Licence

Apache 2.0. Please see `LICENSE`.

## Disclaimer

This code is **not** production quality. I am learning how to work with Win32 APIs and work
with memory management etc. There is certainly plenty of potential for improvement of this code,
but beyond that, it may even be insecure, destructive or cause you other serious problems. There 
is no warranty.

## Limitations

Not currently recursively copying.

File handling is limited by `MAX_PATH`.