# Native filesystem root capabilities (development ABI 225)

Roots are generic KyIlOS resources. Windows drive names and structures stay in
userspace. The existing filesystem, FD inventory, ORA contents and IPC engine
are reused; no second filesystem or Windows kernel object engine is added.

## Object, capability and backing

A root object owns a normalized namespace anchor and a monotonic in-memory
identity, independent of creator PID, local FD number and content hashes.
A typed FD owns a reference plus immutable rights and a captured filesystem
principal. Duplicate/fork and IPC retain the object; the last authorized
reference destroys it. Derivation cannot add a right. TRANSFER is checked by
the message engine, including handles derived into rooted file/directory FDs.

Rights: READ, WRITE, CREATE, ENUM, REMOVE, RENAME, EXEC and TRANSFER.
EXEC is reserved authority for later executable-resource integration; no root
execute operation is currently advertised. Its presence is not an implemented
Windows CreateProcess contract.

Because Vex directories currently have path-derived metadata rather than a
stable inode hierarchy, an anchor reserves its own name and ancestor names
until its last reference. Rename/delete/rebinding of those anchors fails;
descendant files/directories remain mutable. Directory marker removal is also
protected. This is an explicit namespace lease, not a guessed persistent inode.
No on-disk format change. Formatting a volume with live roots is refused.

OPEN normalizes a relative path, rejects traversal above the anchor, resolves
using the real FS backend and checks the resulting path under the same recursive
FS namespace transaction used by mutation. It never validates then opens outside
the transaction. Source pointers are copied/checked by native usercopy first.
OPEN_REGULAR rejects directories atomically.

## Sealing and authorized operations

FS_ROOT_SEAL permanently removes ambient pathname access for the process group.
It is inherited by threads/fork/vfork/new child groups and retained across exec.
There is no unseal operation. An authorized root operation supplies a private,
stack-local filesystem authority while its transaction runs. Applications
cannot inject this pointer. Permission checks use the captured grant principal
only inside that operation and only within its root and attenuated rights;
they do not change scheduler credentials or grant unrelated capabilities.

Existing resource references remain capabilities: opened files and mapped
backings retain their root after the original root FD is closed. Each file
retains only its access rights (plus authorized transfer), not a writable parent
capability accidentally exposed as a readable file. Raw OPEN_EX and path stat
cannot bypass a sealed group. Ordinary unsealed native applications retain
their existing permission behavior.

## ABI

`vex_fs_root_req_t` uses the tagged native syscall domain, number 225.
Operations: CREATE, QUERY, DERIVE, OPEN, MKDIR, STAT, SEAL and POLICY.
CREATE/DERIVE/OPEN return a new local FD or a negative native error; QUERY and
STAT write validated output buffers. Inputs and irrelevant fields are checked.
Existing native close/dup and typed FD attachments handle lifecycle/transfer.
No operation accepts a global root identity as authority.

RootInfo reports identity, rights, grant UID and actual native path. Root paths
are not Windows C: paths. The userspace Windows adapter binds C: to
/home/<native-user>/.windows/C and A: to the explicitly launched package folder
(read-only), verifies the actual grant identity/path, then seals before PE entry.

## Boundaries and remaining work

Current resource operations cover regular file open, directory open/enumeration,
metadata and mkdir. REMOVE/RENAME-specific root operations are not yet exposed.
Protected anchor mutation is refused through existing FS mutation APIs.
Legacy executable loaders still use path entry points: sealed Windows child
launch/dynamic DLL bootstrap needs explicit executable-resource integration;
existing unsealed exec/native/Linux programs are not rewritten around Windows.

The original native file backend has snapshot-style separate opens and path-
based metadata identity. This work does not declare full Windows file-sharing,
rename/unlink-open-file or persistent inode semantics solved. Windows adapters
must explicitly refuse unsupported contracts. Full Windows registry, object
namespaces and security descriptors also remain unimplemented.

## Validation

Real VM smoke: userspace/windows/tests/native_fs_roots.c, executable
/usr/native-tests/native_fs_roots_test. The 4 CPU run checks actual file bytes,
identity versus rights, attenuation, traversal/symlink escape refusal, live
anchor pinning, seal, parallel readers, fork inheritance, queued IPC transfer
before receiver consumption after producer exit, new receiver local handle,
non-transferable refusal, and file lifetime after root close.
Raw evidence: build/windows/fs-roots-4cpu-ipc-pass.log.
Further final 1/4 CPU and Windows adapter results are recorded separately.