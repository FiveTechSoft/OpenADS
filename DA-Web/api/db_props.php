<?php
/**
 * api/db_props.php — get or set DD-level database properties.
 *
 * GET  ?dd=   → JSON object with all readable properties
 * POST {...}  → { saved: N }
 *
 * OpenADS internal property keys (prop_N) come from the SP_MODIFYDATABASE
 * mapping in ace_exports.cpp and the engine's login check (prop_5).
 * SAP native IDs (100-131 in ace.h) differ and are only used in import_dd.
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
            $currentLoginReq = readU16($dict, 5) !== 0;
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

        // String properties
        try { $dict->setDatabaseProperty(1,  (string)($body['description']          ?? '')); $saved++; } catch (Throwable) {}
        try { $dict->setDatabaseProperty(3,  (string)($body['defaultTablePath']     ?? '')); $saved++; } catch (Throwable) {}
        try { $dict->setDatabaseProperty(12, (string)($body['tempTablePath']        ?? '')); $saved++; } catch (Throwable) {}
        try { $dict->setDatabaseProperty(13, (string)($body['encryptTablePassword'] ?? '')); $saved++; } catch (Throwable) {}
        try { $dict->setDatabaseProperty(17, (string)($body['loginsDisabledErrstr'] ?? '')); $saved++; } catch (Throwable) {}
        try { $dict->setDatabaseProperty(18, (string)($body['ftsDelimiters']        ?? '')); $saved++; } catch (Throwable) {}
        try { $dict->setDatabaseProperty(19, (string)($body['ftsNoise']             ?? '')); $saved++; } catch (Throwable) {}
        try { $dict->setDatabaseProperty(20, (string)($body['ftsDropChars']         ?? '')); $saved++; } catch (Throwable) {}
        try { $dict->setDatabaseProperty(21, (string)($body['ftsConditionalChars']  ?? '')); $saved++; } catch (Throwable) {}
        try { $dict->setDatabaseProperty(26, (string)($body['userDefinedProp']      ?? '')); $saved++; } catch (Throwable) {}

        // UNSIGNED16 bool properties
        try { writeU16($dict,  5, $wantLogin                          ? 1 : 0); $saved++; } catch (Throwable) {}
        try { writeU16($dict,  8, !empty($body['verifyAccessRights']) ? 1 : 0); $saved++; } catch (Throwable) {}
        try { writeU16($dict, 10, !empty($body['encryptNewTable'])    ? 1 : 0); $saved++; } catch (Throwable) {}
        try { writeU16($dict, 16, !empty($body['loginsDisabled'])     ? 1 : 0); $saved++; } catch (Throwable) {}
        try { writeU16($dict, 23, !empty($body['encryptIndexes'])     ? 1 : 0); $saved++; } catch (Throwable) {}
        try { writeU16($dict, 25, !empty($body['encryptCommunication']) ? 1 : 0); $saved++; } catch (Throwable) {}
        // prop_22 = ENCRYPTED is read-only (set by encryption engine, not UI)

        // UNSIGNED16 version numbers
        try { writeU16($dict, 14, (int)($body['versionMajor'] ?? 0)); $saved++; } catch (Throwable) {}
        try { writeU16($dict, 15, (int)($body['versionMinor'] ?? 0)); $saved++; } catch (Throwable) {}

        $conn->close();
        echo json_encode(['saved' => $saved]);

    } else {
        $result = [
            'description'           => readStr($dict,  1),
            'defaultTablePath'      => readStr($dict,  3),
            'tempTablePath'         => readStr($dict, 12),
            'encryptTablePassword'  => readStr($dict, 13),
            'loginRequired'         => readU16($dict,  5) !== 0,
            'verifyAccessRights'    => readU16($dict,  8) !== 0,
            'encryptNewTable'       => readU16($dict, 10) !== 0,
            'versionMajor'          => readU16($dict, 14),
            'versionMinor'          => readU16($dict, 15),
            'loginsDisabled'        => readU16($dict, 16) !== 0,
            'loginsDisabledErrstr'  => readStr($dict, 17),
            'ftsDelimiters'         => readStr($dict, 18),
            'ftsNoise'              => readStr($dict, 19),
            'ftsDropChars'          => readStr($dict, 20),
            'ftsConditionalChars'   => readStr($dict, 21),
            'encrypted'             => readU16($dict, 22) !== 0,
            'encryptIndexes'        => readU16($dict, 23) !== 0,
            'encryptCommunication'  => readU16($dict, 25) !== 0,
            'userDefinedProp'       => readStr($dict, 26),
        ];
        $conn->close();
        echo json_encode($result);
    }
} catch (Throwable $e) {
    api_exception(500, $e);
}
