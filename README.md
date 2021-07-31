# ShadowDuplicator

Playing around with creating VSS snapshots within a C project.

Seems to require invocation as `NT AUTHORITY\SYSTEM` -- something to do with permissions on `Computer\HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\VSS\VssAccessControl`? 
Therefore, successful invocation should be done through `psexec` or some other mechanism to elevate to `NT AUTHORITY\SYSTEM`.

    psexec -s -i ShadowDuplicator.exe