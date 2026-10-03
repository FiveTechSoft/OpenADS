# Traditional ZIP encryption header validation

Extraction checks the 12-byte ZipCrypto header's last verification byte before
opening an output file. Encrypted entries require a nonempty password; unsupported
strong-encryption flags are rejected. The expected byte is the CRC high byte, or
the DOS-time high byte when the data-descriptor flag is set. Plain entries ignore
a supplied password rather than accidentally decrypting their payload.

This closes the missing-header-check path where an encrypted empty file succeeded
with no password or a wrong password. Traditional ZIP encryption still has only
an 8-bit password check, so collisions are possible (especially for empty files
with no payload CRC evidence). ZipCrypto is not authenticated encryption and is
not suitable for protecting sensitive backups against determined attackers.
The archive security audit continues; concurrent symlink races and general
failed-overwrite preservation remain separate items.
