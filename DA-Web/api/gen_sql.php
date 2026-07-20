<?php
/**
 * api/gen_sql.php — generate a CREATE TABLE DDL script for a DD table.
 * GET ?dd=&table=
 *
 * Returns { sql: "..." } with a full SAP-compatible DDL script including
 * field definitions, index creation calls, table/field property calls,
 * and trigger definitions.
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';
require_once __DIR__ . '/openads_stubs.php';

$ddName = trim($_GET['dd']    ?? '');
$table  = trim($_GET['table'] ?? '');

if ($table === '') {
    api_error(400, 'table is required');
}
api_validate_identifier($table, 'table name');

$c = api_require_connection($ddName);
$opts = api_ads_connect_opts($c);

// ── ADS trigger helper (returns trigger list via API) ────────────────────────
function readTriggers(AdsConnection $conn, AdsDictionary $dict2, string $table): array {
    $stmt = $conn->query(
        "SELECT TRIG_NAME FROM system.triggers WHERE TABLE_NAME = '"
        . api_sql_quote($table) . "'"
    );
    $names = [];
    while ($row = $stmt->fetchAssoc()) $names[] = $row['TRIG_NAME'];
    $stmt->close();

    $trigs = [];
    foreach ($names as $trigName) {
        $evRaw   = $dict2->getTriggerProperty($trigName, 1401);
        $timRaw  = $dict2->getTriggerProperty($trigName, 1402);
        $body    = $dict2->getTriggerProperty($trigName, 1404);
        $event   = strlen($evRaw)  >= 4 ? unpack('V', substr($evRaw,  0, 4))[1] : 0;
        $timing  = strlen($timRaw) >= 4 ? unpack('V', substr($timRaw, 0, 4))[1] : 0;
        // TRIG_NAME is "table::name" composite key; strip prefix for DDL output
        $dispName = strpos($trigName, '::') !== false ? substr($trigName, strpos($trigName, '::') + 2) : $trigName;
        $trigs[] = [
            'name'    => $dispName,
            'timing'  => match($timing) { 1=>'BEFORE', 2=>'INSTEAD OF', 4=>'AFTER', default=>'' },
            'event'   => match($event)  { 1=>'INSERT', 2=>'UPDATE', 3=>'DELETE', default=>'' },
            'body'    => rtrim($body),
            'priority'=> 1,
        ];
    }
    return $trigs;
}

// ── ADS CREATE TABLE type strings ─────────────────────────────────────────
function createTypeStr(string $raw, int $len, int $dec): string {
    switch ($raw) {
        case 'C': return "CIChar( $len )";
        case 'N': return $dec > 0 ? "Numeric( $len, $dec )" : "Numeric( $len )";
        case 'F': return $dec > 0 ? "Float( $len, $dec )"   : "Float( $len )";
        case 'L': return 'Logical';
        case 'D': return 'Date';
        case 'M': return 'Memo';
        case 'I': return 'Integer';
        case 'Y': return 'Money';
        case 'B': return 'Double';
        case 'V': return "Varchar( $len )";
        case 'Q': return "Varbinary( $len )";
        case 'T': return 'DateTime';
        case '@': return 'TimeStamp';
        case '+': return 'AutoIncrement';
        case 'W': return 'Blob';
        case 'G': return 'Binary';
    }
    switch (ord(substr($raw, 0, 1))) {
        case  1: return 'Logical';
        case  3: return 'Date';
        case  4: return "Char( $len )";
        case  5: return 'Memo';
        case  6: return 'Binary';
        case 10: return 'Double';
        case 11: return 'Integer';
        case 12: return 'ShortInt';
        case 13: return 'Time';
        case 14: return 'TimeStamp';
        case 15: return 'AutoIncrement';
        case 18: return 'Money';
        case 20: return "CIChar( $len )";
        default: return $raw ?: 'Char(1)';
    }
}


try {
    $conn = AdsConnection::connect($opts);
    $lines = [];

    // ── 1. Field definitions ────────────────────────────────────────────────
    $stmt = $conn->query("SELECT TABLE_NAME, COL_NAME, COL_NUM, COL_TYPE, COL_LEN, COL_DEC FROM system.columns");
    $fields = [];
    while ($row = $stmt->fetchAssoc()) {
        if (strcasecmp($row['TABLE_NAME'], $table) !== 0) continue;
        $fields[] = [
            'num'  => (int)$row['COL_NUM'],
            'name' => $row['COL_NAME'],
            'raw'  => trim($row['COL_TYPE']),
            'len'  => (int)$row['COL_LEN'],
            'dec'  => (int)$row['COL_DEC'],
        ];
    }
    usort($fields, fn($a, $b) => $a['num'] <=> $b['num']);

    if (empty($fields)) {
        $conn->close();
        echo json_encode(['sql' => "-- Table '$table' not found or has no columns."]);
        exit;
    }

    $lines[] = "CREATE TABLE $table (";
    $last = count($fields) - 1;
    foreach ($fields as $i => $f) {
        $typeStr = createTypeStr($f['raw'], $f['len'], $f['dec']);
        $comma   = ($i < $last) ? ',' : '';
        $lines[] = "      {$f['name']} $typeStr$comma";
    }
    $lines[] = "      ) IN DATABASE;";
    $lines[] = '';

    // ── 2. Index definitions ───────────────────────────────────────────────
    $primaryKeyTag = '';
    try {
        $dict = AdsDictionary::fromConnection($conn);
        // ADS_DD_TABLE_PRIMARY_KEY = 202
        $primaryKeyTag = strtoupper(trim($dict->getTableProperty($table, 202)));
    } catch (Throwable $e) {}

    try {
        $tbl  = AdsTable::open($conn, $table, 0);
        $tags = $tbl->getIndexTags();
        $tbl->close();

        // Derive index file name: same base as table + .adi
        $indexFile = strtolower($table) . '.adi';

        foreach ($tags as $t) {
            $tagUpper  = strtoupper($t['tag']);
            $isPrimary = ($primaryKeyTag !== '' && $tagUpper === $primaryKeyTag);
            $flags     = $isPrimary ? 2051 : 2;  // 2051≈unique+primary, 2=regular
            $lines[] = "EXECUTE PROCEDURE sp_CreateIndex90(";
            $lines[] = "   '$table',";
            $lines[] = "   '$indexFile',";
            $lines[] = "   '{$t['tag']}',";
            $lines[] = "   '{$t['expression']}',";
            $lines[] = "   '',";
            $lines[] = "   $flags,";
            $lines[] = "   512,";
            $lines[] = "   '' ); ";
            $lines[] = '';
            $lines[] = '';
        }
    } catch (Throwable $e) {}

    // ── 3. Table properties ────────────────────────────────────────────────
    $failTable = strtolower($table) . 'fail';
    $tblProps  = [];
    try {
        $dict3 = AdsDictionary::fromConnection($conn);
        // 202=Table_Primary_Key, 213=Table_Default_Index, 216=Table_Permission_Level
        $propCodes = [202 => 'Table_Primary_Key', 213 => 'Table_Default_Index', 216 => 'Table_Permission_Level'];
        foreach ($propCodes as $code => $propName) {
            try {
                $val = trim($dict3->getTableProperty($table, $code));
                if ($val !== '') {
                    $lines[] = "EXECUTE PROCEDURE sp_ModifyTableProperty( '$table', ";
                    $lines[] = "   '$propName', ";
                    $lines[] = "   '$val', 'APPEND_FAIL', '$failTable');";
                    $lines[] = '';
                }
            } catch (Throwable $e) {}
        }
    } catch (Throwable $e) {}

    // ── 4. Field properties (Required / Default) ───────────────────────────
    try {
        $dict4 = AdsDictionary::fromConnection($conn);
        foreach ($fields as $f) {
            $fieldName = $f['name'];
            $propLines = [];
            try {
                // ADS_DD_FIELD_CAN_NULL = 301 (SAP ABI): UNSIGNED16,
                // 0 = NULL not allowed. Only emit when NOT NULL — the
                // nullable state is the default.
                $raw = $dict4->getFieldProperty($table, $fieldName, 301);
                $canNull = strlen($raw) >= 2 ? unpack('v', $raw)[1] : 1;
                if ($canNull === 0) {
                    $propLines[] = "EXECUTE PROCEDURE sp_ModifyFieldProperty ( '$table', ";
                    $propLines[] = "      '$fieldName', 'Field_Can_Be_Null', ";
                    $propLines[] = "      'False', 'APPEND_FAIL', '$failTable' ); ";
                }
            } catch (Throwable $e) {}
            try {
                // ADS_DD_FIELD_DEFAULT_VALUE = 300 (SAP ABI); throws
                // AE_PROPERTY_NOT_SET when there is no default.
                $def = trim($dict4->getFieldProperty($table, $fieldName, 300));
                if ($def !== '') {
                    $propLines[] = "EXECUTE PROCEDURE sp_ModifyFieldProperty ( '$table', ";
                    $propLines[] = "      '$fieldName', 'Field_Default_Value', ";
                    $propLines[] = "      '$def', 'APPEND_FAIL', '$failTable' ); ";
                }
            } catch (Throwable $e) {}
            if (!empty($propLines)) {
                foreach ($propLines as $pl) $lines[] = $pl;
                $lines[] = '';
            }
        }
    } catch (Throwable $e) {}

    // ── 5. Triggers — via AdsDictionary API ────────────────────────────────────
    $dictTrig = AdsDictionary::fromConnection($conn);
    $trigs    = readTriggers($conn, $dictTrig, $table);
    usort($trigs, fn($a, $b) => strcmp($a['name'], $b['name']));

    foreach ($trigs as $tr) {
        $name   = $tr['name'];
        $timing = $tr['timing'];
        $event  = $tr['event'];
        $body   = $tr['body'];
        $prio   = $tr['priority'];

        $lines[] = "CREATE TRIGGER [$name]";
        $lines[] = "   ON $table";
        $lines[] = "   $timing ";
        $lines[] = "   $event ";
        $lines[] = "BEGIN ";
        if ($body !== '') {
            foreach (explode("\n", $body) as $bl) $lines[] = rtrim($bl);
        } else {
            $lines[] = "   -- (body unavailable)";
        }
        $lines[] = "END";
        $lines[] = "   PRIORITY $prio;";
        $lines[] = '';
        $lines[] = '';
    }

    $conn->close();
    $sql = implode("\n", $lines);
    echo json_encode(['sql' => $sql], JSON_UNESCAPED_UNICODE | JSON_INVALID_UTF8_SUBSTITUTE);
} catch (Throwable $e) {
    api_exception(500, $e);
}
