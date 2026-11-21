#ifndef OPENADS_VF_WIRE_H
#define OPENADS_VF_WIRE_H

/* Exact-vf extension carried on FileLockOp. The original mtfix85 lock
 * request remains exactly 23 bytes. VF requests have a distinct magic,
 * version, fixed 44-byte prefix and length-checked variable fields.
 * All integer fields are little endian. Signed offsets use two's complement.
 * No native pointers, OS handles or server-native paths cross this interface.
 *
 * Request:
 *   0 magic[4]="OVF1", 4 version:u8=1, 5 op:u8, 6 reserved:u16=0,
 *   8 handle:u32, 12 value1:i64, 20 value2:i64, 28 flags:u32,
 *   32 path1_len:u32, 36 path2_len:u32, 40 data_len:u32,
 *   44 path1 bytes, path2 bytes, data bytes (no NUL terminators).
 * Response:
 *   0 ferror:u32, 4 value1:i64, 12 value2:i64,
 *   20 data_len:u32, 24 data bytes.
 * A server sends a valid response even for a filesystem error. Transport
 * errors remain transport errors and cannot masquerade as a short read.
 */
#define OADS_VF_VERSION       1
#define OADS_VF_REQUEST_SIZE 44
#define OADS_VF_REPLY_SIZE   24
#define OADS_VF_PATH_MAX  65535
#define OADS_VF_CHUNK_MAX 1048576

enum oads_vf_operation {
    OADS_VF_OPEN = 1, OADS_VF_CLOSE, OADS_VF_READ, OADS_VF_WRITE,
    OADS_VF_READAT, OADS_VF_WRITEAT, OADS_VF_SEEK, OADS_VF_TRUNC,
    OADS_VF_SIZE, OADS_VF_EOF, OADS_VF_FLUSH, OADS_VF_COMMIT,
    OADS_VF_LOCK, OADS_VF_UNLOCK, OADS_VF_LOCKTEST,
    OADS_VF_EXISTS, OADS_VF_ERASE, OADS_VF_RENAME, OADS_VF_COPY,
    OADS_VF_DIREXISTS, OADS_VF_DIRMAKE, OADS_VF_DIRREMOVE,
    OADS_VF_DIRSPACE, OADS_VF_DIRECTORY,
    OADS_VF_TIMEGET, OADS_VF_TIMESET, OADS_VF_ATTRGET, OADS_VF_ATTRSET,
    OADS_VF_LINK, OADS_VF_LINKSYM, OADS_VF_LINKREAD, OADS_VF_TEMPSERVER, OADS_VF_BLOBREAD
};
#endif
