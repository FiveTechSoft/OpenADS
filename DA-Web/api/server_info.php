<?php
/**
 * api/server_info.php — server activity / connection info via AdsMg* API.
 *
 * GET  ?dd=<name>
 *   Returns:
 *     { ok:true, connType:'local'|'remote', users:[...], tables:[...],
 *       queries:[...], activity:{...} }
 *
 * POST {action:'kill', dd:<name>, connNo:<n>, user:<name>}
 *   Calls AdsMgKillUser. Local connections are a documented engine
 *   no-op (AE_SUCCESS without effect); only remote (tcp://) connections
 *   actually disconnect a session.
 *
 * POST {action:'sql', dd:<name>, threadNo:<n>}
 *   Calls AdsMgGetThreadSql — on-demand SQL text + start time for one
 *   queries[] row (threadNo comes from the GET response above).
 *
 * Calls AdsMgConnect + AdsMgGetActivityInfo + AdsMgGetUserNames +
 * AdsMgGetUserAvgCost + AdsMgGetOpenTables + AdsMgGetWorkerThreadActivity +
 * AdsMgGetThreadSql + AdsMgKillUser via PHP FFI against openace64.dll. The
 * mgmt connection targets the same host:port as the DD's own connection
 * when it's remote, so activity reflects the actual server the DD is
 * talking to.
 *
 * Requires: php.ini  ffi.enable=true  (or ffi.enable=preload)
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';

api_require_session();

$method = $_SERVER['REQUEST_METHOD'];
if ($method === 'POST') {
    $body   = json_decode(file_get_contents('php://input'), true) ?? [];
    $ddName = trim($body['dd'] ?? '');
} else {
    $body   = [];
    $ddName = trim($_GET['dd'] ?? '');
}

if ($ddName === '' || !isset($_SESSION['connections'][$ddName])) {
    http_response_code(401);
    echo json_encode(['error' => "Not connected to '$ddName'"]);
    exit;
}

$connInfo = $_SESSION['connections'][$ddName];
$connType = strtolower($connInfo['connType'] ?? 'local') === 'remote' ? 'remote' : 'local';
$mgServer = ($connType === 'remote' && !empty($connInfo['host']) && !empty($connInfo['port']))
    ? ($connInfo['host'] . ':' . $connInfo['port'])
    : null;

// ── Locate openace64.dll ──────────────────────────────────────────────────────
if (!extension_loaded('ffi')) {
    http_response_code(501);
    echo json_encode(['error' => 'PHP FFI extension is not available. Enable ffi.enable=true in php.ini.']);
    exit;
}

$dllName  = (PHP_OS_FAMILY === 'Windows') ? 'openace64.dll' : 'libopenace64.so';
$dllPaths = [
    getenv('OPENADS_DLL') ?: null,
    'C:\\php\\' . $dllName,
    'C:\\php\\ext\\' . $dllName,
    '/usr/lib/' . $dllName,
    '/usr/local/lib/' . $dllName,
];
$dllPath = null;
foreach ($dllPaths as $p) {
    if ($p && is_file($p)) { $dllPath = $p; break; }
}
if (!$dllPath) {
    http_response_code(500);
    echo json_encode(['error' => 'openace64.dll not found. Set OPENADS_DLL env var or copy to C:\\php\\']);
    exit;
}

// ── FFI declarations ─────────────────────────────────────────────────────────
$cdef = '
typedef unsigned long long ADSHANDLE;
typedef unsigned short UNSIGNED16;
typedef unsigned int   UNSIGNED32;
typedef unsigned char  UNSIGNED8;
typedef unsigned long long UNSIGNED64;

typedef struct {
    UNSIGNED16 usDays;
    UNSIGNED16 usHours;
    UNSIGNED16 usMinutes;
    UNSIGNED16 usSeconds;
} ADS_MGMT_TIME_STRUCT;

typedef struct {
    UNSIGNED32 ulInUse;
    UNSIGNED32 ulMaxUsed;
    UNSIGNED32 ulRejected;
} ADS_MGMT_USAGE_STRUCT;

typedef struct {
    UNSIGNED32            ulOperations;
    UNSIGNED32            ulLoggedErrors;
    ADS_MGMT_TIME_STRUCT  stUpTime;
    ADS_MGMT_USAGE_STRUCT stUsers;
    ADS_MGMT_USAGE_STRUCT stConnections;
    ADS_MGMT_USAGE_STRUCT stWorkAreas;
    ADS_MGMT_USAGE_STRUCT stTables;
    ADS_MGMT_USAGE_STRUCT stIndexes;
    ADS_MGMT_USAGE_STRUCT stLocks;
    ADS_MGMT_USAGE_STRUCT stTpsHeaderElems;
    ADS_MGMT_USAGE_STRUCT stTpsVisElems;
    ADS_MGMT_USAGE_STRUCT stTpsMemoElems;
    ADS_MGMT_USAGE_STRUCT stWorkerThreads;
} ADS_MGMT_ACTIVITY_INFO;

typedef struct {
    UNSIGNED8  aucUserName        [32];
    UNSIGNED16 usConnNumber;
    UNSIGNED8  aucAddress         [64];
    UNSIGNED8  aucTSAddress       [64];
    UNSIGNED8  aucOSUserLoginName [64];
    UNSIGNED8  aucAuthUserName    [64];
} ADS_MGMT_USER_INFO;

typedef struct {
    UNSIGNED8  aucTableName [256];
    UNSIGNED8  aucUserName  [32];
    UNSIGNED16 usConnNumber;
    UNSIGNED16 usOpenMode;
    UNSIGNED16 usLockType;
} ADS_MGMT_TABLE_INFO;

typedef struct {
    UNSIGNED32 ulThreadNumber;
    UNSIGNED16 usOpCode;
    UNSIGNED8  aucUserName        [32];
    UNSIGNED16 usConnNumber;
    UNSIGNED16 usReserved1;
    UNSIGNED8  aucOSUserLoginName [64];
} ADS_MGMT_THREAD_ACTIVITY;

UNSIGNED32 AdsMgConnect(UNSIGNED8* pucServer, UNSIGNED8* pucUser,
                        UNSIGNED8* pucPassword, ADSHANDLE* phMgmt);
UNSIGNED32 AdsMgDisconnect(ADSHANDLE hMgmt);
UNSIGNED32 AdsMgGetActivityInfo(ADSHANDLE hMgmt, ADS_MGMT_ACTIVITY_INFO* pstActivity,
                                UNSIGNED16* pusStructSize);
UNSIGNED32 AdsMgGetUserNames(ADSHANDLE hMgmt, UNSIGNED8* pucFileName,
                             ADS_MGMT_USER_INFO* pstUserInfo,
                             UNSIGNED16* pusArrayLen, UNSIGNED16* pusStructSize);
UNSIGNED32 AdsMgGetOpenTables(ADSHANDLE hMgmt, UNSIGNED8* pucUserName,
                              UNSIGNED16 usConnNumber,
                              ADS_MGMT_TABLE_INFO* pstTableInfo,
                              UNSIGNED16* pusArrayLen, UNSIGNED16* pusStructSize);
UNSIGNED32 AdsMgGetWorkerThreadActivity(ADSHANDLE hMgmt,
                              ADS_MGMT_THREAD_ACTIVITY* pstThreadInfo,
                              UNSIGNED16* pusArrayLen, UNSIGNED16* pusStructSize);
UNSIGNED32 AdsMgKillUser(ADSHANDLE hMgmt, UNSIGNED8* pucUserName,
                         UNSIGNED16 usConnNumber);
UNSIGNED32 AdsMgGetUserAvgCost(ADSHANDLE hMgmt, UNSIGNED32* pulCosts,
                              UNSIGNED16* pusCount, UNSIGNED16* pusSize);
UNSIGNED32 AdsMgGetThreadSql(ADSHANDLE hMgmt, UNSIGNED32 ulThreadNumber,
                             UNSIGNED8* pucBuf, UNSIGNED32* pulLen,
                             UNSIGNED64* pullStartEpoch);
';

try {
    $ffi = FFI::cdef($cdef, $dllPath);
} catch (Throwable $e) {
    http_response_code(500);
    echo json_encode(['error' => 'FFI init failed: ' . $e->getMessage()]);
    exit;
}

// Read a null-terminated C UNSIGNED8 array as a PHP string.
function ffiStr(FFI\CData $arr, int $maxLen): string {
    $bytes = [];
    for ($i = 0; $i < $maxLen; $i++) {
        $b = (int)$arr[$i];
        if ($b === 0) break;
        $bytes[] = $b;
    }
    return empty($bytes) ? '' : pack('C*', ...$bytes);
}

// Build a NUL-terminated UNSIGNED8[] buffer from a PHP string, for
// out-parameters typed UNSIGNED8* (FFI's automatic char*<->string
// marshalling only applies to the literal `char*` type, not typedefs
// of it, so ADS_MGMT strings need an explicit byte buffer).
function ffiCStr(FFI $ffi, string $s): FFI\CData {
    $buf = $ffi->new('UNSIGNED8[' . (strlen($s) + 1) . ']');
    FFI::memcpy($buf, $s, strlen($s));
    $buf[strlen($s)] = 0;
    return $buf;
}

// ── Connect to management interface ──────────────────────────────────────────
// Empty server string → local-mode management (reports this process's
// state). A remote DD (host:port from the session's own connection)
// connects the mgmt handle to that same server, so activity/kill reflect
// the server actually serving the DD instead of this PHP process.
// Pass the DD's own connected username so this mgmt session registers
// under that name instead of showing up as "(anonymous)" alongside the
// user's real connections in the Connected Users / Active Queries grids.
$hMgmt = $ffi->new('ADSHANDLE');
$hMgmt->cdata = 0;
$serverArg = $mgServer !== null ? ffiCStr($ffi, $mgServer) : null;
$mgUsername = trim((string)($connInfo['username'] ?? ''));
$userArg = $mgUsername !== '' ? ffiCStr($ffi, $mgUsername) : null;
$rc = $ffi->AdsMgConnect($serverArg, $userArg, null, FFI::addr($hMgmt));
if ($rc !== 0) {
    http_response_code(500);
    echo json_encode(['error' => "AdsMgConnect failed (rc=$rc)"]);
    exit;
}
$h = $hMgmt->cdata;

try {
    if ($method === 'POST') {
        $action = $body['action'] ?? '';

        if ($action === 'sql') {
            // On-demand detail for one Active Queries row — see
            // AdsMgGetThreadSql. Caller passes the threadNo from a row in
            // the queries[] array the last GET returned.
            $threadNo = (int)($body['threadNo'] ?? 0);
            if ($threadNo <= 0) {
                http_response_code(400);
                echo json_encode(['error' => 'threadNo is required']);
                exit;
            }
            $cap = 65536;
            $buf = $ffi->new("UNSIGNED8[$cap]");
            $len = $ffi->new('UNSIGNED32');
            $len->cdata = $cap;
            $epoch = $ffi->new('UNSIGNED64');
            $epoch->cdata = 0;
            $rc = $ffi->AdsMgGetThreadSql($h, $threadNo, $buf, FFI::addr($len), FFI::addr($epoch));
            if ($rc !== 0) {
                echo json_encode(['error' => "AdsMgGetThreadSql failed (rc=$rc)"]);
                exit;
            }
            $n = min((int)$len->cdata, $cap);
            $sql = $n > 0 ? FFI::string($buf, $n) : '';
            $epochVal = (int)$epoch->cdata;
            echo json_encode([
                'ok'        => true,
                'sql'       => $sql,
                'startedAt' => $epochVal > 0 ? date('Y-m-d H:i:s', $epochVal) : null,
            ]);
            exit;
        }

        if ($action !== 'kill') {
            http_response_code(400);
            echo json_encode(['error' => 'unknown action']);
            exit;
        }
        $connNo = (int)($body['connNo'] ?? 0);
        $user   = trim((string)($body['user'] ?? ''));
        if ($connNo <= 0 && $user === '') {
            http_response_code(400);
            echo json_encode(['error' => 'connNo or user is required']);
            exit;
        }
        $userArg = $user !== '' ? ffiCStr($ffi, $user) : null;
        $rc = $ffi->AdsMgKillUser($h, $userArg, $connNo);
        if ($rc !== 0) {
            echo json_encode(['error' => "AdsMgKillUser failed (rc=$rc)"]);
            exit;
        }
        echo json_encode([
            'ok'   => true,
            'note' => $connType === 'local'
                ? 'Local connections cannot be disconnected (engine reports success but takes no action).'
                : null,
        ]);
        exit;
    }

    // ── Activity info (counts) ────────────────────────────────────────────────
    $actInfo  = $ffi->new('ADS_MGMT_ACTIVITY_INFO');
    $actSize  = $ffi->new('UNSIGNED16');
    $actSize->cdata = (int)FFI::sizeof($actInfo);
    $ffi->AdsMgGetActivityInfo($h, FFI::addr($actInfo), FFI::addr($actSize));

    $activity = [
        'operations'    => (int)$actInfo->ulOperations,
        'loggedErrors'  => (int)$actInfo->ulLoggedErrors,
        'upTime'        => [
            'days'    => (int)$actInfo->stUpTime->usDays,
            'hours'   => (int)$actInfo->stUpTime->usHours,
            'minutes' => (int)$actInfo->stUpTime->usMinutes,
            'seconds' => (int)$actInfo->stUpTime->usSeconds,
        ],
        'users'         => ['inUse' => (int)$actInfo->stUsers->ulInUse,    'maxUsed' => (int)$actInfo->stUsers->ulMaxUsed],
        'connections'   => ['inUse' => (int)$actInfo->stConnections->ulInUse, 'maxUsed' => (int)$actInfo->stConnections->ulMaxUsed],
        'tables'        => ['inUse' => (int)$actInfo->stTables->ulInUse,   'maxUsed' => (int)$actInfo->stTables->ulMaxUsed],
        'indexes'       => ['inUse' => (int)$actInfo->stIndexes->ulInUse,  'maxUsed' => (int)$actInfo->stIndexes->ulMaxUsed],
        'locks'         => ['inUse' => (int)$actInfo->stLocks->ulInUse,    'maxUsed' => (int)$actInfo->stLocks->ulMaxUsed],
        'workerThreads' => ['inUse' => (int)$actInfo->stWorkerThreads->ulInUse, 'maxUsed' => (int)$actInfo->stWorkerThreads->ulMaxUsed],
    ];

    // ── User list ─────────────────────────────────────────────────────────────
    $maxUsers  = 256;
    $userArr   = $ffi->new("ADS_MGMT_USER_INFO[$maxUsers]");
    $userCount = $ffi->new('UNSIGNED16');
    $userCount->cdata = $maxUsers;
    $userSize  = $ffi->new('UNSIGNED16');
    $userSize->cdata  = (int)FFI::sizeof($ffi->new('ADS_MGMT_USER_INFO'));
    $ffi->AdsMgGetUserNames($h, null, $userArr, FFI::addr($userCount), FFI::addr($userSize));

    // Average per-frame cost (microseconds), same order/count as the user
    // list just fetched above — a non-SAP extension (AdsMgGetUserAvgCost),
    // since ADS_MGMT_USER_INFO's fixed layout has no room for it.
    $costArr   = $ffi->new("UNSIGNED32[$maxUsers]");
    $costCount = $ffi->new('UNSIGNED16');
    $costCount->cdata = $maxUsers;
    $costSize  = $ffi->new('UNSIGNED16');
    $costSize->cdata  = (int)FFI::sizeof($ffi->new('UNSIGNED32'));
    $ffi->AdsMgGetUserAvgCost($h, $costArr, FFI::addr($costCount), FFI::addr($costSize));

    $users = [];
    $n = (int)$userCount->cdata;
    $nc = (int)$costCount->cdata;
    for ($i = 0; $i < $n && $i < $maxUsers; $i++) {
        $u = $userArr[$i];
        $avgCostMicros = ($i < $nc) ? (int)$costArr[$i] : 0;
        $users[] = [
            'name'     => ffiStr($u->aucUserName,         32),
            'address'  => ffiStr($u->aucAddress,          64),
            'authUser' => ffiStr($u->aucAuthUserName,     64),
            'connNo'   => (int)$u->usConnNumber,
            'avgCostMs' => round($avgCostMicros / 1000, 3),
        ];
    }

    // ── Open tables ──────────────────────────────────────────────────────────
    $maxTbls  = 512;
    $tblArr   = $ffi->new("ADS_MGMT_TABLE_INFO[$maxTbls]");
    $tblCount = $ffi->new('UNSIGNED16');
    $tblCount->cdata = $maxTbls;
    $tblSize  = $ffi->new('UNSIGNED16');
    $tblSize->cdata  = (int)FFI::sizeof($ffi->new('ADS_MGMT_TABLE_INFO'));
    $ffi->AdsMgGetOpenTables($h, null, 0, $tblArr, FFI::addr($tblCount), FFI::addr($tblSize));

    $tables = [];
    $nt = (int)$tblCount->cdata;
    for ($i = 0; $i < $nt && $i < $maxTbls; $i++) {
        $t = $tblArr[$i];
        $tables[] = [
            'name'   => ffiStr($t->aucTableName, 256),
            'user'   => ffiStr($t->aucUserName,   32),
            'connNo' => (int)$t->usConnNumber,
        ];
    }

    // ── Active queries (worker thread activity) ──────────────────────────────
    $maxThreads = 256;
    $thrArr     = $ffi->new("ADS_MGMT_THREAD_ACTIVITY[$maxThreads]");
    $thrCount   = $ffi->new('UNSIGNED16');
    $thrCount->cdata = $maxThreads;
    $thrSize    = $ffi->new('UNSIGNED16');
    $thrSize->cdata  = (int)FFI::sizeof($ffi->new('ADS_MGMT_THREAD_ACTIVITY'));
    $ffi->AdsMgGetWorkerThreadActivity($h, $thrArr, FFI::addr($thrCount), FFI::addr($thrSize));

    $queries = [];
    $nq = (int)$thrCount->cdata;
    for ($i = 0; $i < $nq && $i < $maxThreads; $i++) {
        $t = $thrArr[$i];
        $queries[] = [
            'threadNo' => (int)$t->ulThreadNumber,
            'opcode'   => (int)$t->usOpCode,
            'user'     => ffiStr($t->aucUserName, 32),
            'connNo'   => (int)$t->usConnNumber,
            'osLogin'  => ffiStr($t->aucOSUserLoginName, 64),
            // Non-SAP reuse of usReserved1: 1 while this thread is still
            // inside dispatch() for usOpCode, 0 if it's the last
            // *completed* operation. Stale rows are expected and fine —
            // matches SAP Data Architect's own Active Queries view.
            'active'   => ((int)$t->usReserved1) === 1,
        ];
    }

    echo json_encode([
        'ok'       => true,
        'connType' => $connType,
        'activity' => $activity,
        'users'    => $users,
        'tables'   => $tables,
        'queries'  => $queries,
    ]);

} finally {
    $ffi->AdsMgDisconnect($h);
}
