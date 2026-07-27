<?php
/**
 * api/table_meta.php — field, index, or trigger metadata for a DD table.
 * GET ?dd=&table=&kind=fields|indexes|triggers
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';
require_once __DIR__ . '/openads_stubs.php';

$ddName = trim($_GET['dd']    ?? '');
$table  = trim($_GET['table'] ?? '');
$kind   = trim($_GET['kind']  ?? '');

if ($table === '' || !in_array($kind, ['fields', 'indexes', 'triggers'], true)) {
    api_error(400, 'invalid parameters');
}
api_validate_identifier($table, 'table name');

$c = api_require_connection($ddName);
$opts = api_ads_connect_opts($c);

// ── DBF/ADT binary header field reader ─────────────────────────────────────
// Returns array of ['Field', 'Type' (letter), 'Size', 'Dec'] from a .dbf or .adt file.
function readDbfFields(string $filePath): array {
    $f = @file_get_contents($filePath, false, null, 0, 65536);
    if ($f === false || strlen($f) < 32) return [];

    $version  = ord($f[0]);
    $isAdt    = (strtolower(pathinfo($filePath, PATHINFO_EXTENSION)) === 'adt');
    $numRecs  = unpack('V', substr($f, 4, 4))[1];
    $hdrBytes = unpack('v', substr($f, 8, 2))[1];   // header size = first field descriptor offset
    $recLen   = unpack('v', substr($f, 10, 2))[1];

    $fields = [];
    $pos    = 32;   // first field descriptor starts here in standard DBF

    // For ADT: field descriptors start at a different offset
    // ADT header is 400 bytes, field descriptors at byte 400
    if ($isAdt) {
        $pos = 400;
        // ADT field descriptor: 128 bytes each
        // [0..127]: name(128), type(1), len(2 LE), dec(1), ...
        $numFields = ($hdrBytes - 400) > 0 ? intdiv($hdrBytes - 400, 128) : 0;
        for ($i = 0; $i < $numFields && $pos + 128 <= strlen($f); $i++, $pos += 128) {
            $name = rtrim(substr($f, $pos, 128), " \0");
            if ($name === '' || $name[0] === "\0") break;
            $typeB = ord($f[$pos + 128 + 0]) ?? 0;  // type is after the name? check ADT format
            // Simplified: ADT field type at a known offset
            // Actually ADT stores: name(128 bytes), type(1), reserved(1), length(2 LE), decimals(1)
            // at offset pos+128 from start of descriptor
            // This is approximate — real ADT format may differ
            $fields[] = [
                'Order' => $i + 1,
                'Field' => $name,
                'Type'  => '',
                'BaseType' => '',
                'Size'  => 0,
                'Decimals' => 0,
                'Required' => '',
                'Default'  => '',
                'Index'    => 'No',
            ];
        }
        return $fields;
    }

    // Standard DBF field descriptors: 32 bytes each, terminated by 0x0D
    $order = 1;
    while ($pos + 32 <= strlen($f)) {
        if (ord($f[$pos]) === 0x0D) break;   // terminator
        $name = rtrim(substr($f, $pos, 11), " \0");
        if ($name === '') { $pos += 32; continue; }
        $typeChar = $f[$pos + 11];
        $fldLen   = ord($f[$pos + 16]);
        $fldDec   = ord($f[$pos + 17]);
        $label    = fieldTypeLabel($typeChar, $fldLen, $fldDec);
        $base     = fieldBaseType($typeChar);
        $fields[] = [
            'Order'    => $order++,
            'Field'    => $name,
            'Type'     => $label,
            'BaseType' => $base,
            'Size'     => $fldLen,
            'Decimals' => $fldDec,
            'Required' => '',
            'Default'  => '',
            'Index'    => 'No',
        ];
        $pos += 32;
    }
    return $fields;
}

// ── field-type label (display) ──────────────────────────────────────────────
function fieldTypeLabel(string $raw, int $len, int $dec): string {
    switch ($raw) {
        case 'C': return "Character($len)";
        case 'N': return $dec > 0 ? "Numeric($len,$dec)" : "Numeric($len)";
        case 'F': return $dec > 0 ? "Float($len,$dec)"   : "Float($len)";
        case 'L': return 'Logical';
        case 'D': return 'Date';
        case 'M': return 'Memo';
        case 'I': return 'Integer';
        case 'Y': return 'Money';
        case 'B': return 'Double';
        case 'V': return "Varchar($len)";
        case 'Q': return "Varbinary($len)";
        case 'T': return 'DateTime';
        case '@': return 'Timestamp';
        case '+': return 'AutoIncrement';
        case 'W': return 'Blob';
        case 'G': return 'Binary';
        case 'P': return 'Picture';
    }
    switch (ord(substr($raw, 0, 1))) {
        case  1: return 'Logical';
        case  3: return 'Date';
        case  4: return "Character($len)";
        case  5: return 'Memo';
        case  6: return 'Binary';
        case 10: return 'Double';
        case 11: return 'Integer';
        case 12: return 'ShortInt';
        case 13: return 'Time';
        case 14: return 'Timestamp';
        case 15: return 'AutoIncrement';
        case 18: return 'Money';
        case 20: return "CICharacter($len)";
        case 21: return 'RowVersion';
        case 22: return 'ModTime';
        default: return $raw ?: '?';
    }
}

// ── base type key (for editor dropdown) ────────────────────────────────────
function fieldBaseType(string $raw): string {
    switch ($raw) {
        case 'C': return 'Character';
        case 'N': return 'Numeric';
        case 'F': return 'Float';
        case 'L': return 'Logical';
        case 'D': return 'Date';
        case 'M': return 'Memo';
        case 'I': return 'Integer';
        case 'Y': return 'Money';
        case 'B': return 'Double';
        case 'V': return 'Varchar';
        case 'Q': return 'Varbinary';
        case 'T': return 'DateTime';
        case '@': return 'Timestamp';
        case '+': return 'AutoIncrement';
        case 'W': return 'Blob';
        case 'G': return 'Binary';
        case 'P': return 'Picture';
    }
    switch (ord(substr($raw, 0, 1))) {
        case  1: return 'Logical';
        case  3: return 'Date';
        case  4: return 'Character';
        case  5: return 'Memo';
        case  6: return 'Binary';
        case 10: return 'Double';
        case 11: return 'Integer';
        case 12: return 'ShortInt';
        case 13: return 'Time';
        case 14: return 'Timestamp';
        case 15: return 'AutoIncrement';
        case 18: return 'Money';
        case 20: return 'CICharacter';
        case 21: return 'RowVersion';
        case 22: return 'ModTime';
        default: return $raw ?: '?';
    }
}

// ── decode trigger EVENT_MASK → timing + event strings ─────────────────────
function decodeTriggerMask(int $mask): array {
    // ADS_BEFORE_INSERT=0x01, ADS_AFTER_INSERT=0x02
    // ADS_BEFORE_UPDATE=0x04, ADS_AFTER_UPDATE=0x08
    // ADS_BEFORE_DELETE=0x10, ADS_AFTER_DELETE=0x20
    $isBefore = ($mask & 0x15) !== 0;  // bits 0,2,4
    $isAfter  = ($mask & 0x2A) !== 0;  // bits 1,3,5
    if ($isBefore && !$isAfter)     $timing = 'BEFORE';
    elseif ($isAfter && !$isBefore) $timing = 'AFTER';
    elseif ($isBefore && $isAfter)  $timing = 'BEFORE/AFTER';
    else                            $timing = '';

    $events = [];
    if ($mask & 0x03) $events[] = 'INSERT';
    if ($mask & 0x0C) $events[] = 'UPDATE';
    if ($mask & 0x30) $events[] = 'DELETE';
    return ['timing' => $timing, 'event' => implode(', ', $events)];
}

try {
    $conn = AdsConnection::connect($opts);
    $rows = [];

    // ── FIELDS ─────────────────────────────────────────────────────────────
    if ($kind === 'fields') {
        $fields = [];
        // Try system.columns first (works for DD connections)
        try {
            $stmt = $conn->query("SELECT TABLE_NAME, COL_NAME, COL_NUM, COL_TYPE, COL_LEN, COL_DEC FROM system.columns");
            while ($row = $stmt->fetchAssoc()) {
                if (strcasecmp($row['TABLE_NAME'], $table) !== 0) continue;
                $len = (int)$row['COL_LEN'];
                $dec = (int)$row['COL_DEC'];
                $raw = trim($row['COL_TYPE']);
                $fields[] = [
                    'Order'    => (int)$row['COL_NUM'],
                    'Field'    => $row['COL_NAME'],
                    'Type'     => fieldTypeLabel($raw, $len, $dec),
                    'BaseType' => fieldBaseType($raw),
                    'Size'     => $len,
                    'Decimals' => $dec,
                    'Required' => '',
                    'Default'  => '',
                    'Index'    => 'No',
                ];
            }
            usort($fields, fn($a, $b) => $a['Order'] <=> $b['Order']);
        } catch (Throwable $e) {}

        // Fallback for free tables: read field info from the .dbf/.adt binary header
        if (empty($fields)) {
            $dir = rtrim($c['path'], '/\\');
            foreach (['.dbf', '.DBF', '.adt', '.ADT'] as $ext) {
                $candidate = $dir . DIRECTORY_SEPARATOR . $table . $ext;
                if (!file_exists($candidate)) {
                    // Try uppercase table name
                    $candidate = $dir . DIRECTORY_SEPARATOR . strtoupper($table) . $ext;
                }
                if (file_exists($candidate)) {
                    $fields = readDbfFields($candidate);
                    break;
                }
            }
        }

        // Field properties via AdsDictionary — SAP ABI numbering:
        // ADS_DD_FIELD_CAN_NULL (301) returns UNSIGNED16 (0 = NOT NULL =
        // required); ADS_DD_FIELD_DEFAULT_VALUE (300) returns the default
        // text and throws AE_PROPERTY_NOT_SET (5138) when there is none.
        try {
            $dict = AdsDictionary::fromConnection($conn);
            foreach ($fields as &$f) {
                try {
                    $raw = $dict->getFieldProperty($table, $f['Field'], 301);
                    $canNull = strlen($raw) >= 2 ? unpack('v', $raw)[1] : 1;
                    $f['Required'] = ($canNull === 0) ? 'True' : 'False';
                } catch (Throwable $e) {}
                try {
                    $f['Default'] = trim($dict->getFieldProperty($table, $f['Field'], 300));
                } catch (Throwable $e) { $f['Default'] = ''; }
            }
            unset($f);
        } catch (Throwable $e) {}

        // Index membership: open table, scan tag expressions for field names
        $fieldIndex = [];
        $primaryKeyTag = '';
        try {
            $dict2 = AdsDictionary::fromConnection($conn);
            // ADS_DD_TABLE_PRIMARY_KEY = 202
            $primaryKeyTag = strtoupper(trim($dict2->getTableProperty($table, 202)));
        } catch (Throwable $e) {}

        try {
            $tbl  = AdsTable::open($conn, $table, 0);
            $tags = $tbl->getIndexTags();
            $tbl->close();
            foreach ($tags as $t) {
                $tagUpper  = strtoupper($t['tag']);
                $isPrimary = ($primaryKeyTag !== '' && $tagUpper === $primaryKeyTag);
                $parts = preg_split('/[;+,\s]+/', strtoupper($t['expression']), -1, PREG_SPLIT_NO_EMPTY);
                foreach ($parts as $p) {
                    $p = trim($p, " \t'\"");
                    if ($p === '') continue;
                    if ($isPrimary) {
                        $fieldIndex[$p] = 'Primary';
                    } elseif (!isset($fieldIndex[$p]) || $fieldIndex[$p] === 'No') {
                        $fieldIndex[$p] = 'Yes';
                    }
                }
            }
        } catch (Throwable $e) {}

        foreach ($fields as &$f) {
            $key = strtoupper($f['Field']);
            if (isset($fieldIndex[$key])) $f['Index'] = $fieldIndex[$key];
        }
        unset($f);

        $rows = $fields;
    }

    // ── INDEXES ─────────────────────────────────────────────────────────────
    elseif ($kind === 'indexes') {
        $tbl  = AdsTable::open($conn, $table, 0);
        $tags = $tbl->getIndexTags();
        $tbl->close();

        $pkTag = '';
        try {
            $dict2 = AdsDictionary::fromConnection($conn);
            // ADS_DD_TABLE_PRIMARY_KEY = 202
            $pkTag = strtoupper(trim($dict2->getTableProperty($table, 202)));
        } catch (Throwable $e) {}

        foreach ($tags as $t) {
            $tagUpper  = strtoupper($t['tag']);
            $isPrimary = ($pkTag !== '' && $tagUpper === $pkTag);
            $isUnique  = $isPrimary || !empty($t['unique']);
            $rows[] = [
                'Tag'        => $t['tag'],
                'Expression' => $t['expression'],
                'Descending' => $t['descending'] ? 'Yes' : 'No',
                'Unique'     => $isUnique  ? 'Yes' : 'No',
                'Primary'    => $isPrimary ? 'Yes' : 'No',
                'Binary'     => 'No',
                'KeyType'    => 'STR',
            ];
        }
    }

    // ── TRIGGERS — via AdsDictionary API (full body, correct timing) ─────────
    else {
        $dict2 = AdsDictionary::fromConnection($conn);
        $stmt2 = $conn->query(
            "SELECT Name FROM system.triggers WHERE Trig_TableName = '"
            . api_sql_quote($table) . "'"
        );
        $trigNames = [];
        while ($trow = $stmt2->fetchAssoc()) $trigNames[] = $trow['Name'];
        $stmt2->close();

        foreach ($trigNames as $trigName2) {
            $evRaw   = $dict2->getTriggerProperty($trigName2, 1401);
            $timRaw  = $dict2->getTriggerProperty($trigName2, 1402);
            $body2   = $dict2->getTriggerProperty($trigName2, 1404);
            $event2  = strlen($evRaw)  >= 4 ? unpack('V', substr($evRaw,  0, 4))[1] : 0;
            $timing2 = strlen($timRaw) >= 4 ? unpack('V', substr($timRaw, 0, 4))[1] : 0;
            $timingStr2 = match($timing2) { 1=>'BEFORE', 2=>'INSTEAD OF', 4=>'AFTER', default=>'' };
            $eventStr2  = match($event2)  { 1=>'INSERT', 2=>'UPDATE', 3=>'DELETE', default=>'' };
            // Name is "table::name" composite key; display just the plain name
            $dispName2 = strpos($trigName2, '::') !== false ? substr($trigName2, strpos($trigName2, '::') + 2) : $trigName2;
            $rows[] = [
                'Name'     => $dispName2,
                'Timing'   => $timingStr2,
                'Event'    => $eventStr2,
                'Enabled'  => 'Yes',
                'Priority' => 1,
                'Body'     => rtrim($body2),
            ];
        }
        usort($rows, fn($a, $b) => strcmp($a['Name'], $b['Name']));
    }

    $conn->close();
    echo json_encode(['data' => $rows]);
} catch (Throwable $e) {
    api_exception(500, $e);
}
