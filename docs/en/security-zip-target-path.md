# ZIP destination path checks

Extraction resolves each target under the extraction directory before creating
or opening it. Existing file symlinks and symlinked parent directories pointing
outside that root are denied, including flat extraction with overwrite enabled.
Lexical entry-name checks still reject absolute names and '..' components.

This closes the reproduced pre-existing-symlink path, not every filesystem race.
An attacker able to replace directories or links concurrently can still race a
path check and open. Protect the data directory from untrusted filesystem writers;
race-safe directory-handle operations remain a separate audit surface. Archive
resource limits, ZipCrypto limitations and failed-overwrite recovery also remain
outside this fix. No security-complete claim is made.
