# Preserve destinations on failed archive writes

ZIP creation writes to a file in an exclusively created sibling staging directory,
then publishes the completed archive after close succeeds. Extraction uses the
same pattern per file and publishes only after stream, length and CRC checks pass.
Failures remove staging artifacts instead of deleting an existing destination.
On POSIX, no-overwrite publication uses a hard link that refuses an existing
name; overwrite uses rename. Windows uses MoveFileEx without or with replacement.
Extraction rechecks the target's resolved jail path before publication.

This preserves the previous file on ordinary streaming, validation and publish
failures. An archive may include its old output path as an input without deleting
that input before reading it. Filesystems without hard links fail no-overwrite
publication safely rather than replacing an existing name. Successful replacement
installs a new file with its staging permissions; prior ACLs, ownership and hard-link
identity are not retained. Windows staging inherits the parent directory's ACL.

This is per-file replacement, not an archive-wide transaction or crash durability
promise. A crash can leave staging directories. Concurrent ancestor replacement
and symlink races still need a separate handle-relative filesystem design.
The security audit is not complete.
