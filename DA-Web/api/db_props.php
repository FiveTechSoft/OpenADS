<?php
/**
 * api/db_props.php — get or set DD-level database properties.
 *
 * GET  ?dd=   → JSON object with all readable properties
 * POST {...}  → { saved: N }
 *
 * This API passes SAP ABI property ids (ADS_DD_* in ace.h: generic 1-3,
 * database 100-122). The engine's db_prop_storage_key translates them
 * to the STABLE on-disk prop_N storage keys below (SP_MODIFYDATABASE
 * numbering — the AdsConnect60 login check reads prop_5/prop_16
 * directly), so existing DDs keep working:
 *
 *   prop_1   COMMENT                (string)
 *   prop_3   DEFAULT_TABLE_PATH     (string)
 *   prop_5   LOG_IN_REQUIRED        (UNSIGNED16 bool) — engine reads this
 *   prop_8   VERIFY_ACCESS_RIGHTS   (UNSIGNED16 bool)
 *   prop_10  ENCRYPT_NEW_TABLE      (UNSIGNED16 bool)
 *   prop_12  TEMP_TABLE_PATH        (string)
 *   prop_13  ENCRYPT_TABLE_PASSWORD (string)
 *   prop_14  VERSION_MAJOR          (UNSIGNED16)
 *   prop_15  VERSION_MINOR          (UNSIGNED16)
 *   prop_16  LOGINS_DISABLED        (UNSIGNED16 bool)
 *   prop_17  LOGINS_DISABLED_ERRSTR (string)
 *   prop_18  FTS_DELIMITERS         (string)
 *   prop_19  FTS_NOISE              (string)
 *   prop_20  FTS_DROP_CHARS         (string)
 *   prop_21  FTS_CONDITIONAL_CHARS  (string)
 *   prop_22  ENCRYPTED              (UNSIGNED16 bool, read-only)
 *   prop_23  ENCRYPT_INDEXES        (UNSIGNED16 bool)
 *   prop_25  ENCRYPT_COMMUNICATION  (UNSIGNED16 bool)
 *   prop_26  USER_DEFINED_PROP      (string)
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';
require_once __DIR__ . '/openads_stubs.php';

$isPost = $_SERVER['REQUEST_METHOD'] === 'POST';
if ($isPost) {
    $body   = json_decode(file_get_contents('php://input'), true) ?? [];
    $ddName = trim($body['dd'] ?? '');
} else {
    $body   = [];
    $ddName = trim($_GET['dd'] ?? '');
}

if (!isset($_SESSION['connections'][$ddName])) {
    http_response_code(401);
    echo json_encode(['error' => "Not connected to '$ddName'"]);
    exit;
}

$c    = $_SESSION['connections'][$ddName];
$opts = api_ads_connect_opts($c);

/**
 * Read a string property; return '' on error or not-set.
 */
function readStr(AdsDictionary $dict, int $prop): string {
    try { return (string)$dict->getDatabaseProperty($prop); }
    catch (Throwable) { return ''; }
}

/**
 * Read a UNSIGNED16 property stored as a plain decimal string ("0", "1", …).
 * The import tool decodes SAP's raw little-endian bytes to decimal; the UI also
 * writes decimal strings.  A legacy raw 2-byte value is handled as a fallback.
 */
function readU16(AdsDictionary $dict, int $prop): int {
    try {
        $raw = $dict->getDatabaseProperty($prop);
        if ($raw === '') return 0;
        if (ctype_digit($raw) || ($raw[0] === '-' && ctype_digit(substr($raw, 1))))
            return (int)$raw;
        // Fallback: raw 2-byte little-endian left over from an old import.
        if (strlen($raw) >= 2)
            return unpack('v', substr($raw, 0, 2))[1];
        return (int)$raw;
    } catch (Throwable) { return 0; }
}

/**
 * Write a UNSIGNED16 int as a plain decimal string.
 */
function writeU16(AdsDictionary $dict, int $prop, int $val): void {
    $dict->setDatabaseProperty($prop, (string)max(0, min(65535, $val)));
}

try {
    $conn = AdsConnection::connect($opts);
    $dict = AdsDictionary::fromConnection($conn);

    if ($isPost) {
        $saved = 0;

        // Guard: refuse to enable loginRequired if no user has a stored password.
        $wantLogin = !empty($body['loginRequired']);
        if ($wantLogin) {
            $currentLoginReq = readU16($dict, 103) !== 0;   // ADS_DD_LOG_IN_REQUIRED
            if (!$currentLoginReq) {
                // A SQL query() on the same connection/handle that has
                // already been used for an AdsDictionary property read
                // (e.g. the readU16($dict, 5) just above) fails remote
                // connections with "ExecuteSQL: server-side exec failed" —
                // a real engine/binding bug, isolated 2026-07-07. Use a
                // dedicated connection for this query so the guard doesn't
                // depend on tracking down that interaction first.
                $hasPassword = false;
                try {
                    $userCheckConn = AdsConnection::connect($opts);
                    $stmt = $userCheckConn->query("SELECT user_name FROM system.users");
                    $rows = $stmt->fetchAll();
                    $stmt->close();
                    foreach ($rows as $row) {
                        $uname = $row['user_name'] ?? ($row['USER_NAME'] ?? '');
                        if ($uname === '') continue;
                        try {
                            $pwd = $dict->getUserProperty($uname, 1101);
                            if ($pwd !== '') { $hasPassword = true; break; }
                        } catch (Throwable) {}
                    }
                    $userCheckConn->close();
                } catch (Throwable) {}

                if (!$hasPassword) {
                    $conn->close();
                    http_response_code(409);
                    echo json_encode([
                        'error' => 'Cannot enable Login Required: no users have a '
                                 . 'password stored in this dictionary.  Set a '
                                 . 'password for at least one user first, then '
                                 . 're-enable Login Required.',
                        'code'  => 5000,
                    ]);
                    exit;
                }
            }
        }

        // String properties — SAP ABI ids (the engine translates them to
        // the stable prop_N storage keys).
        try { $dict->setDatabaseProperty(1,   (string)($body['description']          ?? '')); $saved++; } catch (Throwable) {}
        try { $dict->setDatabaseProperty(100, (string)($body['defaultTablePath']     ?? '')); $saved++; } catch (Throwable) {}
        try { $dict->setDatabaseProperty(102, (string)($body['tempTablePath']        ?? '')); $saved++; } catch (Throwable) {}
        try { $dict->setDatabaseProperty(105, (string)($body['encryptTablePassword'] ?? '')); $saved++; } catch (Throwable) {}
        try { $dict->setDatabaseProperty(114, (string)($body['loginsDisabledErrstr'] ?? '')); $saved++; } catch (Throwable) {}
        try { $dict->setDatabaseProperty(115, (string)($body['ftsDelimiters']        ?? '')); $saved++; } catch (Throwable) {}
        try { $dict->setDatabaseProperty(116, (string)($body['ftsNoise']             ?? '')); $saved++; } catch (Throwable) {}
        try { $dict->setDatabaseProperty(117, (string)($body['ftsDropChars']         ?? '')); $saved++; } catch (Throwable) {}
        try { $dict->setDatabaseProperty(118, (string)($body['ftsConditionalChars']  ?? '')); $saved++; } catch (Throwable) {}
        try { $dict->setDatabaseProperty(3,   (string)($body['userDefinedProp']      ?? '')); $saved++; } catch (Throwable) {}

        // UNSIGNED16 bool properties
        try { writeU16($dict, 103, $wantLogin                          ? 1 : 0); $saved++; } catch (Throwable) {}
        try { writeU16($dict, 104, !empty($body['verifyAccessRights']) ? 1 : 0); $saved++; } catch (Throwable) {}
        try { writeU16($dict, 106, !empty($body['encryptNewTable'])    ? 1 : 0); $saved++; } catch (Throwable) {}
        try { writeU16($dict, 113, !empty($body['loginsDisabled'])     ? 1 : 0); $saved++; } catch (Throwable) {}
        try { writeU16($dict, 120, !empty($body['encryptIndexes'])     ? 1 : 0); $saved++; } catch (Throwable) {}
        try { writeU16($dict, 122, !empty($body['encryptCommunication']) ? 1 : 0); $saved++; } catch (Throwable) {}
        // ADS_DD_ENCRYPTED (119) is read-only (set by the encryption engine)

        // UNSIGNED16 version numbers
        try { writeU16($dict, 111, (int)($body['versionMajor'] ?? 0)); $saved++; } catch (Throwable) {}
        try { writeU16($dict, 112, (int)($body['versionMinor'] ?? 0)); $saved++; } catch (Throwable) {}

        $conn->close();
        echo json_encode(['saved' => $saved]);

    } else {
        // SAP ABI ids (engine translates to the stable prop_N storage keys).
        $result = [
            'description'           => readStr($dict,   1),
            'defaultTablePath'      => readStr($dict, 100),
            'tempTablePath'         => readStr($dict, 102),
            'encryptTablePassword'  => readStr($dict, 105),
            'loginRequired'         => readU16($dict, 103) !== 0,
            'verifyAccessRights'    => readU16($dict, 104) !== 0,
            'encryptNewTable'       => readU16($dict, 106) !== 0,
            'versionMajor'          => readU16($dict, 111),
            'versionMinor'          => readU16($dict, 112),
            'loginsDisabled'        => readU16($dict, 113) !== 0,
            'loginsDisabledErrstr'  => readStr($dict, 114),
            'ftsDelimiters'         => readStr($dict, 115),
            'ftsNoise'              => readStr($dict, 116),
            'ftsDropChars'          => readStr($dict, 117),
            'ftsConditionalChars'   => readStr($dict, 118),
            'encrypted'             => readU16($dict, 119) !== 0,
            'encryptIndexes'        => readU16($dict, 120) !== 0,
            'encryptCommunication'  => readU16($dict, 122) !== 0,
            'userDefinedProp'       => readStr($dict,   3),
        ];
        $conn->close();
        echo json_encode($result);
    }
} catch (Throwable $e) {
    api_exception(500, $e);
}
