#pragma once

#include "util/result.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace openads::network {

// M12.1 — Phase 2 wire-protocol skeleton for the TCP server.
//
// Frame layout (big-endian length, fixed-size header):
//
//   bytes 0..3 : payload length (uint32 BE, excludes header)
//   byte  4    : opcode
//   bytes 5..N : payload
//
// Total frame size = 5 + payload_length. The opcode space is
// reserved here even though the Phase 2 server is not yet wired
// up — apps and tests can already round-trip frames through
// encode_frame / decode_frame.

enum class Opcode : std::uint8_t {
    Hello              = 0x01,
    HelloAck           = 0x02,
    Connect            = 0x10,
    ConnectAck         = 0x11,
    Disconnect         = 0x12,
    OpenTable          = 0x20,
    OpenTableAck       = 0x21,
    CloseTable         = 0x22,
    CloseTableAck      = 0x23,
    ExecuteSQL         = 0x30,
    ExecuteSQLAck      = 0x31,
    Fetch              = 0x32,
    FetchAck           = 0x33,
    // M12.4 — remote table navigation + read.
    GotoTop            = 0x40,
    GotoTopAck         = 0x41,
    Skip               = 0x42,
    SkipAck            = 0x43,
    GetField           = 0x44,
    GetFieldAck        = 0x45,
    GetRecordCount     = 0x46,
    GetRecordCountAck  = 0x47,
    AtEOF              = 0x48,
    AtEOFAck           = 0x49,
    // M12.14 — remote field metadata + extended cursor state.
    // DescribeTable returns the full per-column schema in one
    // round-trip so rddads' adsOpen path doesn't need 5 × num_fields
    // hops to populate AdsGetFieldName/Type/Length/Decimals.
    DescribeTable      = 0x4A,
    DescribeTableAck   = 0x4B,
    AtBOF              = 0x4C,
    AtBOFAck           = 0x4D,
    GetRecordNum       = 0x4E,
    GetRecordNumAck    = 0x4F,
    IsRecordDeleted    = 0x62,
    IsRecordDeletedAck = 0x63,
    GotoBottom         = 0x64,
    GotoBottomAck      = 0x65,
    // M12.15 — remote info / lock / maintenance / AOF.
    IsFound            = 0x66,
    IsFoundAck         = 0x67,
    RefreshRecord      = 0x68,
    RefreshRecordAck   = 0x69,
    GetTableType       = 0x6A,
    GetTableTypeAck    = 0x6B,
    GetRecordLength    = 0x6C,
    GetRecordLengthAck = 0x6D,
    GetNumIndexes      = 0x6E,
    GetNumIndexesAck   = 0x6F,
    GetLastAutoinc     = 0x70,
    GetLastAutoincAck  = 0x71,
    LockRecord         = 0x72,
    LockRecordAck      = 0x73,
    UnlockRecord       = 0x74,
    UnlockRecordAck    = 0x75,
    LockTable          = 0x76,
    LockTableAck       = 0x77,
    UnlockTable        = 0x78,
    UnlockTableAck     = 0x79,
    PackTable          = 0x7A,
    PackTableAck       = 0x7B,
    ZapTable           = 0x7C,
    ZapTableAck        = 0x7D,
    FlushFileBuffers   = 0x7E,
    FlushFileBuffersAck= 0x7F,
    CloseAllIndexes    = 0x80,
    CloseAllIndexesAck = 0x81,
    SetAOF             = 0x82,
    SetAOFAck          = 0x83,
    ClearAOFRemote     = 0x84,
    ClearAOFRemoteAck  = 0x85,
    GetAOFOptLevel     = 0x86,
    GetAOFOptLevelAck  = 0x87,
    // M12.16 — remote index handle subsystem.
    OpenIndex          = 0x88,
    OpenIndexAck       = 0x89,
    CloseIndex         = 0x8A,
    CloseIndexAck      = 0x8B,
    SetOrder           = 0x8C,
    SetOrderAck        = 0x8D,
    SetOrderByName     = 0x8E,
    SetOrderByNameAck  = 0x8F,
    Seek               = 0x90,
    SeekAck            = 0x91,
    SeekLast           = 0x92,
    SeekLastAck        = 0x93,
    CreateIndex        = 0x94,
    CreateIndexAck     = 0x95,
    SkipUnique         = 0x96,
    SkipUniqueAck      = 0x97,
    SetScope           = 0x98,
    SetScopeAck        = 0x99,
    ClearScope         = 0x9A,
    ClearScopeAck      = 0x9B,
    // M12.17 — single-frame whole-record read. xbrowse-style
    // viewers paint W cols × H rows = W*H FieldGet calls per
    // repaint; without this op every cell costs one TCP RTT
    // (~5-15 ms LAN), so a 20×12 grid stalls 1-4 s. With this op
    // RemoteTable caches the full row server-side and one
    // FetchCurrentRow RTT serves every subsequent FieldGet on
    // the same record.
    FetchCurrentRow    = 0x9C,
    FetchCurrentRowAck = 0x9D,
    // M12.6 — remote write surface.
    AppendBlank        = 0x50,
    AppendBlankAck     = 0x51,
    SetField           = 0x52,
    SetFieldAck        = 0x53,
    DeleteRecord       = 0x54,
    DeleteRecordAck    = 0x55,
    RecallRecord       = 0x56,
    RecallRecordAck    = 0x57,
    GotoRecord         = 0x58,
    GotoRecordAck      = 0x59,
    FlushTable         = 0x5A,
    FlushTableAck      = 0x5B,
    // M12.8 — remote index ops (CREATE INDEX is already covered by
    // M12.7's ExecuteSQL `CREATE INDEX` DDL path; Reindex isn't in
    // SQL grammar so it needs a dedicated opcode).
    Reindex            = 0x60,
    ReindexAck         = 0x61,
    // M12.24 — remote AdsGetLastTableUpdate. Reply payload is the
    // DBF header date packed big-endian-ish into 4 bytes:
    // (year << 16) | (month << 8) | day.
    GetLastTableUpdate    = 0x9E,
    GetLastTableUpdateAck = 0x9F,

    // M9.25 — management telemetry channel.
    MgConnect          = 0xA0,
    MgConnectAck       = 0xA1,
    MgRequest          = 0xA2,
    MgReplyAck         = 0xA3,

    // Tier-2 server-side filtered scan. Like Fetch (0x32) but the
    // server evaluates a Clipper-style FOR predicate per row and
    // returns only matching rows + the requested columns, walking
    // the table server-side until `max_rows` matches or EOF. Removes
    // the per-record Skip/GetField round-trips for a `SET FILTER` /
    // `COUNT FOR` / `LOCATE FOR` scan whose predicate is outside the
    // index-optimisable AOF subset (where the client RDD would
    // otherwise filter row-by-row over the wire).
    FetchWhere         = 0xA4,
    FetchWhereAck      = 0xA5,

    // Tier-3 server-side aggregation. The server scans the whole table
    // once, evaluating an xBase-style FOR predicate per row, and folds
    // each matching row into the requested COUNT / SUM / AVG / MIN / MAX
    // accumulators — returning just the scalars instead of streaming
    // every matching row back. Collapses `COUNT FOR` / `SUM .. FOR` /
    // `AVERAGE` / totalling reports from O(matched rows over the wire)
    // to a single round-trip. Request payload:
    //   [u32 tid][u16 forlen][for_expr][u8 n_aggs]
    //     per agg: [u8 fn_type][u8 nlen][field_name]   (nlen=0 => COUNT(*))
    // Reply (AggregateAck):
    //   [u8 n_aggs] per agg: [u8 result_type][u16 vlen][val]
    // fn_type: 0=COUNT 1=SUM 2=AVG 3=MIN 4=MAX (engine::AggFn).
    // result_type: 0=empty/null 1=numeric(ASCII) 2=string (engine::AggType).
    Aggregate          = 0xA6,
    AggregateAck       = 0xA7,

    // M12.25 — raw record image read/write (AdsGetRecord / AdsSetRecord).
    // Request GetRecord:  [u32 tid]
    // Reply GetRecordAck: [u16 len][record bytes]
    // Request SetRecord:  [u32 tid][u16 len][record bytes]
    // Reply SetRecordAck: (empty)
    GetRecord          = 0xA8,
    GetRecordAck       = 0xA9,
    SetRecord          = 0xAA,
    SetRecordAck       = 0xAB,

    // M12.26 — AdsCustomizeAOF: flip individual records in/out of the
    // active AOF bitmap. Payload:
    //   [u32 tid][u8 option][u16 nrecs][u32 recno]...
    // option: 1=ADD (ADS_AOF_ADD_RECORD), 2=REMOVE (ADS_AOF_REMOVE_RECORD).
    CustomizeAOF       = 0xAC,
    CustomizeAOFAck    = 0xAD,

    // M12.27 — AdsGetRecordCRC over the wire. Request: [u32 tid]
    // Reply GetRecordCRAck: [u32 crc LE] (IEEE CRC-32 over record_buffer).
    GetRecordCRC       = 0xAE,
    GetRecordCRAck     = 0xAF,

    // M12.28 -- remote AdsGetKeyCount: filtered key count from the
    // active index order (conditional FOR tags index only matching
    // rows, so this differs from GetRecordCount's physical row count).
    // Request:  [u32 table_id]
    // Reply:    [u32 key_count LE]
    GetKeyCount        = 0xB0,
    GetKeyCountAck     = 0xB1,
    // M12.29 — remote AdsGetKeyNum: current key number in the active
    // order's walk. Server computes via pos_of_recno_cached() → O(1).
    // Request:  [table_id u32 LE]
    // Response: [key_num u32 LE]  (0 = no order / not positioned)
    GetKeyNum          = 0x03,
    GetKeyNumAck       = 0x04,

    // M12.33 — remote AdsFindFirstTable / AdsFindNextTable / AdsFindClose.
    // List table files matching a glob mask in the session data directory.
    // Unlike the Directory opcode (0xEA), this is NOT gated by
    // EnableFileFunc — listing tables is a core database operation that
    // arc32 and other DBA tools need.
    // Request FindTables:  [u16 maskLen][mask]
    // Reply FindTablesAck: [u32 nFiles][for each: u16 nameLen][name]
    // The client caches the full list; FindNext/FindClose iterate locally.
    FindTables         = 0x05,
    FindTablesAck      = 0x06,

    // M12.29 — AdsDD* Data Dictionary property API, phase 1. Previously
    // every AdsDD* getter/setter silently returned empty/no-op over a
    // remote connection (dd_from_handle() only resolves a LOCAL Connection
    // handle) instead of erroring or forwarding — see docs/wire-protocol.md
    // §9. These opcodes cover the ~19 Get/Set*Property functions (all share
    // the same name[,subName]+propertyId+value shape — see DDObjectKind
    // below) plus the exact create/drop calls DA-Web exercises today.
    // Everything else in the AdsDD* surface (user/group/link/RI CRUD,
    // permissions bitmask, index-file add/remove) is deferred to a phase 2
    // — those already work remotely via SQL EXECUTE PROCEDURE in DA-Web.
    //
    // Request DDGetProperty:  [u8 objKind][u16 nameLen][name]
    //                         [u16 subNameLen][subName][u16 propId]
    // Reply DDGetPropertyAck: [u32 valLen][value bytes]
    DDGetProperty      = 0xB2,
    DDGetPropertyAck   = 0xB3,
    // Request DDSetProperty:  [u8 objKind][u16 nameLen][name]
    //                         [u16 subNameLen][subName][u16 propId]
    //                         [u32 valLen][value bytes]
    // Reply DDSetPropertyAck: (empty)
    DDSetProperty      = 0xB4,
    DDSetPropertyAck   = 0xB5,
    // Request DDCreateProc: [u16 nameLen][name][u16 containerLen][container]
    //   [u16 procNameLen][procName][u16 inParamsLen][inParams]
    //   [u16 outParamsLen][outParams][u16 commentsLen][comments]
    // Reply DDCreateProcAck: (empty)
    DDCreateProc       = 0xB6,
    DDCreateProcAck    = 0xB7,
    // Request DDCreateFunction: [u16 nameLen][name][u16 containerLen][container]
    //   [u16 implLen][implementation][u16 retTypeLen][retType]
    //   [u16 inParamsLen][inParams][u16 commentLen][comment]
    // Reply DDCreateFunctionAck: (empty)
    DDCreateFunction   = 0xB8,
    DDCreateFunctionAck= 0xB9,
    // Request DDCreateTrigger: [u16 nameLen][name][u16 tableLen][table]
    //   [u32 type][u16 containerLen][container][u16 procedureLen][procedure]
    //   [u32 priority]
    // Reply DDCreateTriggerAck: (empty)
    DDCreateTrigger    = 0xBA,
    DDCreateTriggerAck = 0xBB,
    // Request DDDropTrigger: [u16 nameLen][name]
    // Reply DDDropTriggerAck: (empty)
    DDDropTrigger      = 0xBC,
    DDDropTriggerAck   = 0xBD,
    // Request DDDropView: [u16 nameLen][name]
    // Reply DDDropViewAck: (empty)
    DDDropView         = 0xBE,
    DDDropViewAck      = 0xBF,
    // Request DDDropLink: [u16 nameLen][name]
    // Reply DDDropLinkAck: (empty)
    DDDropLink         = 0xC0,
    DDDropLinkAck      = 0xC1,

    // M12.30 — AdsDD* Data Dictionary property API, phase 2. Covers the
    // remaining CRUD calls deferred by phase 1 (M12.29, see wire-protocol.md
    // §5.24/§5.25): user/group management, links, referential integrity
    // create, views, index-file registration, and permissions. Two more of
    // the deferred functions (GetIndexProperty, GetUserTableRights/
    // SetUserTableRights) reuse the existing DDGetProperty/DDSetProperty
    // opcodes via two new DDObjectKind values below — no new opcode needed
    // for those. AdsDDRevokePermission is a pure local wrapper around
    // AdsDDGrantPermission(..., 0) and needs no wire support of its own.
    //
    // Request DDCreateUser: [u16 groupLen][group][u16 userLen][user]
    //   [u16 pwdLen][pwd][u16 descLen][desc]
    // Reply DDCreateUserAck: (empty)
    DDCreateUser        = 0xC2,
    DDCreateUserAck     = 0xC3,
    // Generic drop-by-name, parallel to DDGetProperty's DDObjectKind tag.
    // Covers AdsDDDeleteUser(User), AdsDDRemoveRefIntegrity(RefIntegrity),
    // AdsDDDropProcedure(Proc), AdsDDDropFunction(Function) — the four
    // remaining plain "drop by name" calls (Trigger/View/Link already have
    // their own phase-1 opcodes).
    // Request DDDropObject: [u8 objKind][u16 nameLen][name]
    // Reply DDDropObjectAck: (empty)
    DDDropObject        = 0xC4,
    DDDropObjectAck     = 0xC5,
    // Request DDAddUserToGroup: [u16 groupLen][group][u16 userLen][user]
    // Reply DDAddUserToGroupAck: (empty)
    DDAddUserToGroup    = 0xC6,
    DDAddUserToGroupAck = 0xC7,
    // Request DDRemoveUserFromGroup: [u16 groupLen][group][u16 userLen][user]
    // Reply DDRemoveUserFromGroupAck: (empty)
    DDRemoveUserFromGroup    = 0xC8,
    DDRemoveUserFromGroupAck = 0xC9,
    // Request DDCreateLink: [u16 aliasLen][alias][u16 pathLen][path]
    //   [u16 userLen][user][u16 pwdLen][pwd]
    // Reply DDCreateLinkAck: (empty)
    DDCreateLink        = 0xCA,
    DDCreateLinkAck     = 0xCB,
    // Request DDModifyLink: same payload as DDCreateLink.
    // Reply DDModifyLinkAck: (empty)
    DDModifyLink        = 0xCC,
    DDModifyLinkAck     = 0xCD,
    // Request DDCreateRefIntegrity: [u16 nameLen][name][u16 failLen][fail]
    //   [u16 parentLen][parent][u16 parentTagLen][parentTag]
    //   [u16 childLen][child][u16 childTagLen][childTag]
    //   [u16 updateRule][u16 deleteRule]
    // Reply DDCreateRefIntegrityAck: (empty)
    DDCreateRefIntegrity    = 0xCE,
    DDCreateRefIntegrityAck = 0xCF,
    // Request DDCreateView: [u16 nameLen][name][u16 commentsLen][comments]
    //   [u32 sqlLen][sql]  (u32: view SQL can be long, unlike short names)
    // Reply DDCreateViewAck: (empty)
    DDCreateView        = 0xD0,
    DDCreateViewAck     = 0xD1,
    // Request DDAddIndexFile: [u16 tableLen][table][u16 indexLen][index]
    //   [u16 commentLen][comment]
    // Reply DDAddIndexFileAck: (empty)
    DDAddIndexFile      = 0xD2,
    DDAddIndexFileAck   = 0xD3,
    // Request DDRemoveIndexFile: [u16 tableLen][table][u16 indexLen][index]
    // Reply DDRemoveIndexFileAck: (empty)
    DDRemoveIndexFile      = 0xD4,
    DDRemoveIndexFileAck   = 0xD5,
    // Request DDGetPermissions: [u16 granteeLen][grantee][u16 objType]
    //   [u16 objNameLen][objName][u8 getInherited]
    // Reply DDGetPermissionsAck: [u32 permissions]
    DDGetPermissions     = 0xD6,
    DDGetPermissionsAck  = 0xD7,
    // Request DDGrantPermission: [u16 objType][u16 objNameLen][objName]
    //   [u16 granteeLen][grantee][u32 permissions]
    // Reply DDGrantPermissionAck: (empty). Revoke = grant with permissions=0
    // (matches AdsDDRevokePermission's local implementation).
    DDGrantPermission     = 0xD8,
    DDGrantPermissionAck  = 0xD9,

    // M12.31 — AdsShowDeleted (SET DELETED ON/OFF) over the wire.
    // Without this, the client's SET DELETED flag never reaches
    // openads_serverd: scoped/ordered GotoTop/Skip on the server
    // walk index keys with show_deleted=true and return rows flagged
    // deleted even though LOCAL mode hides them.
    // Request:  [u8 show]  (0 = hide deleted, 1 = show deleted)
    // Reply ShowDeletedAck: (empty)
    ShowDeleted        = 0xDA,
    ShowDeletedAck     = 0xDB,

    // Remote AdsCreateTable / AdsDropTable. Without these, a client that
    // AdsConnect60'd to tcp://… fell through AdsCreateTable's local
    // Connection lookup, wrote the .dbf next to the client app (cwd of
    // the default local connection), then AdsOpenTable (which *does*
    // route remote) failed with AE_TABLE_CORRUPTED (5103) because the
    // file never landed under the server data directory.
    //
    // Request CreateTable:
    //   [u16 tableType][u16 charType][u16 memoBlockSize]
    //   [u16 nameLen][name][u16 fieldsLen][fields]
    // Reply CreateTableAck: (empty) — client re-opens via OpenTable so
    //   production-bag auto-open / GotoTop reuse the existing path.
    CreateTable        = 0xDC,
    CreateTableAck     = 0xDD,
    // Request DropTable: [u16 nameLen][name][u16 deleteFiles]
    // Reply DropTableAck: (empty)
    DropTable          = 0xDE,
    DropTableAck       = 0xDF,

    // Server filesystem API (oads_* / Ads*) — gated by EnableFileFunc.
    // Paths are relative to the session data directory (jail).
    // FileExists:  [u16 pathLen][path] → FileExistsAck: [u8 exists]
    FileExists         = 0xE0,
    FileExistsAck      = 0xE1,
    // FileErase:   [u16 pathLen][path] → empty ack
    FileErase          = 0xE2,
    FileEraseAck       = 0xE3,
    // FileRename:  [u16 oldLen][old][u16 newLen][new] → empty ack
    FileRename         = 0xE4,
    FileRenameAck      = 0xE5,
    // FileSize:    [u16 pathLen][path] → FileSizeAck: [u64 size LE]
    FileSize           = 0xE6,
    FileSizeAck        = 0xE7,
    // FileMTime:   [u16 pathLen][path] → [u16 y][u8 mon day hh mm ss]
    FileMTime          = 0xE8,
    FileMTimeAck       = 0xE9,
    // Directory:   [u16 maskLen][mask][u16 attr] → [u32 n][entries…]
    Directory          = 0xEA,
    DirectoryAck       = 0xEB,
    DirExist           = 0xEC,
    DirExistAck        = 0xED,
    DirMake            = 0xEE,
    DirMakeAck         = 0xEF,
    DirRemove          = 0xF0,
    DirRemoveAck       = 0xF1,
    // FOpen:  [u16 pathLen][path][u16 mode] → [u32 file_id]
    FOpen              = 0xF2,
    FOpenAck           = 0xF3,
    // FCreate:[u16 pathLen][path][u16 attr] → [u32 file_id]
    FCreate            = 0xF4,
    FCreateAck         = 0xF5,
    // FClose: [u32 file_id]
    FClose             = 0xF6,
    FCloseAck          = 0xF7,
    // FRead:  [u32 file_id][u32 nbytes] → [u32 nread][bytes]
    FRead              = 0xF8,
    FReadAck           = 0xF9,
    // FWrite: [u32 file_id][u32 nbytes][bytes] → [u32 nwritten]
    FWrite             = 0xFA,
    FWriteAck          = 0xFB,
    // FSeek:  [u32 file_id][i32 offset][u8 origin] → [u32 position]
    FSeek              = 0xFC,
    FSeekAck           = 0xFD,

    Error              = 0xFF,
};

// M12.29 — object kind discriminator for DDGetProperty/DDSetProperty.
// Every Get/Set*Property function in the AdsDD* ABI shares the same
// name[,subName]+propertyId+value shape; this tags which local
// AdsDDGet*Property/AdsDDSet*Property function the server should call.
// subName is only meaningful for Field (the field name within the table
// named by `name`); every other kind sends an empty subName.
//
// M12.30 additions: Index (name=table, subName=index — GetIndexProperty
// only; SetIndexProperty is a permanent local stub regardless of
// connection type, so it isn't wired) and UserTableRights (name=table,
// subName=user, propId unused, value = 4-byte LE access level — the
// underlying local functions take/return a raw UNSIGNED32, not a
// property-id-keyed buffer, but the wire shape is identical so it reuses
// DDGetProperty/DDSetProperty instead of adding two more opcodes).
enum class DDObjectKind : std::uint8_t {
    Database        = 1,
    User            = 2,
    Table           = 3,
    Field           = 4,
    Trigger         = 5,
    Proc            = 6,
    Function        = 7,
    View            = 8,
    RefIntegrity    = 9,
    Index           = 10,
    UserTableRights = 11,
};

// Request flags for FetchWhere (Opcode::FetchWhere = 0xA4).
// Set WANT_RECNO in the flags byte to make the server emit a u32 LE
// record number before each row's column data in the FetchWhereAck
// payload. A flags value of 0 produces a reply byte-identical to the
// v1.4.0 wire (no recno field) — full backward compatibility.
namespace FetchWhereFlags {
    constexpr std::uint8_t WANT_RECNO = 0x01;
}

// Inbound cap — symmetric with encode_frame's outbound check; prevents
// multi-gigabyte resize on a malicious 4-byte length prefix.
inline constexpr std::uint32_t kMaxFramePayload = 16u * 1024u * 1024u;

// Upper bound on field count in a single wire row — guards against
// malicious/corrupt servers or clients allocating unbounded vectors.
inline constexpr std::uint16_t kMaxWireFields = 4096u;

// M12.21 option C — client capability flags, advertised as a trailing
// [u32 LE] appended to the Connect payload (after the password field).
// A server only piggybacks the sequential-prefetch lookahead block onto
// forward-Skip acks for clients that set kCapPrefetchConsume, because
// draining that queue locally requires the consumed-counter cursor
// resync. Older clients omit the field (caps = 0) and keep the
// one-round-trip-per-Skip behavior, so the wire stays backward
// compatible in every version-mix direction.
inline constexpr std::uint32_t kCapPrefetchConsume = 0x00000001u;

// Tier-3: the client understands the Aggregate / AggregateAck opcodes
// (0xA6/0xA7). A server only routes a `COUNT/SUM/.. FOR` to the
// server-side accumulator path for clients that advertise this; older
// servers never see a 0xA6 frame, so the wire stays backward compatible.
inline constexpr std::uint32_t kCapAggregate = 0x00000002u;

// RCB 07/15/2026: M12.25 — the client can drain a BACKWARD lookahead block
// (PgUp). This MUST be its own capability, not a free extension of
// kCapPrefetchConsume, because it is a correctness gate rather than an
// optimization: the read-ahead block is a plain [count][rows] list with no
// direction marker on the wire, and a kCapPrefetchConsume-only client (which
// only knows how to drain a block forward) would pop a backward-walked row on
// its next Skip(+1) and serve the WRONG record. A server therefore attaches a
// backward block only to clients that set this bit; everyone else keeps paying
// one round-trip per backward step, exactly as before. Both mix directions stay
// safe: an old server ignores the bit, and a new server never sends a backward
// block to a client that did not advertise it.
inline constexpr std::uint32_t kCapPrefetchBackward = 0x00000004u;

// M12.x — OpenTable mode pass-through. When the client sets this bit, it
// prepends a [u16 LE mode] to every OpenTable payload.  The server reads
// that prefix and opens the table in the requested mode (Shared / Exclusive /
// ReadOnly).  Old servers ignore the prefix because they treat the entire
// payload as the table name and the leading null bytes make the path invalid;
// the open_table fallback then fails with AE_TABLE_NOT_FOUND (5018).
inline constexpr std::uint32_t kCapOpenTableMode = 0x00000008u;

// RCB 07/14/2026: M12.23 — AdsCacheRecords support. Why a bare trailing field
// and not a new opcode or a capability bit: the Skip request grew an OPTIONAL
// trailing [u16 LE] carrying the caller's requested read-ahead depth:
//
//     Skip: [u32 tid][i32 step]            <- pre-M12.23, still valid
//     Skip: [u32 tid][i32 step][u16 depth] <- M12.23
//
// No capability bit is needed, because this is compatible in BOTH
// version-mix directions by construction:
//   * new client -> old server: the old handler length-checks (`size() < 8`)
//     and reads only the first 8 bytes, so the trailing field is ignored and
//     the server keeps choosing the depth itself.
//   * old client -> new server: the field is simply absent, which the new
//     handler reads as kPrefetchDepthAuto (below).
//
// RCB 07/14/2026: this sentinel is 0xFFFF and deliberately NOT 0. SAP gives 0
// a real meaning — "0 (or 1) effectively turns read-ahead record caching off"
// (ace_adscacherecords.htm). So "the caller said nothing" and "the caller said
// stop caching" are DIFFERENT instructions and the server has to tell them
// apart: the first ramps, the second sends no block at all. Had I let an
// absent field decode as 0, every pre-M12.23 client — which sends no field —
// would have silently lost read-ahead the day this shipped. Pinned by
// tests/unit/network_skip_depth_test.cpp, which hand-builds a legacy 8-byte
// Skip because the ABI client can no longer emit one.
inline constexpr std::uint16_t kPrefetchDepthAuto = 0xFFFFu;

// RCB 07/14/2026: safety cap on a caller-requested depth. usRecords is a u16,
// so an app can ask for 65534 rows in one block. The byte budget would stop
// that from becoming an oversized frame, but NOT from walking 65534 records on
// the server thread first — the cost we actually care about. SAP's own
// guidance is that "read-ahead values greater than 100 records are not
// beneficial", so this bound sits far above anything useful and exists purely
// to keep one wire request from turning into an unbounded scan.
inline constexpr std::uint16_t kPrefetchDepthMax = 512u;

struct Frame {
    Opcode                    opcode = Opcode::Hello;
    std::vector<std::uint8_t> payload;
};

// Encode `f` to a flat byte buffer ready to send over TCP.
util::Result<std::vector<std::uint8_t>> encode_frame(const Frame& f);

// Decode `f` from `buf` (must contain at least one full frame).
// Returns the decoded frame plus the number of bytes consumed via
// the optional `consumed` out-param.
util::Result<Frame> decode_frame(const std::uint8_t* buf,
                                  std::size_t size,
                                  std::size_t* consumed = nullptr);

} // namespace openads::network
