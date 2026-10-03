# ZIP extraction length verification

Extraction checks the actual bytes written against each entry's declared
uncompressed size, in addition to minizip's CRC check. An early deflate EOF
can occur with a wrong password or corrupt data while minizip still has bytes
remaining and therefore skips CRC verification. It is now an error, and the
partial output is removed. Reported byte counts no longer accept that case.

This fixes the reproduced premature-EOF failure, not every archive risk.
Traditional ZipCrypto is compatibility encryption, not modern authenticated
encryption. Empty encrypted entries and archive resource limits remain separate
audit surfaces. Extraction still writes directly to the target; this change
does not add transactional restore of an overwritten file on failure.
