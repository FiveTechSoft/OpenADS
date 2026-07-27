<?php
/**
 * api/ri_meta.php — RI object metadata and table list.
 *
 * GET ?dd=&ri=<name>              → { ri:{…}, parentTags:[…], childTags:[…] }
 * GET ?dd=&action=tables          → { tables: ["Table1", …] }
 * GET ?dd=&action=tags&table=<t>  → { tags: ["Tag1", …] }
 *
 * Tag names come from parsing the physical CDX/ADI index files referenced
 * in system.indexes — the .add binary has no 'Key'-type records.
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';
require_once __DIR__ . '/openads_stubs.php';

$ddName = trim($_GET['dd']     ?? '');
$action = trim($_GET['action'] ?? '');
$riName = trim($_GET['ri']     ?? '');

if (!isset($_SESSION['connections'][$ddName])) {
    http_response_code(401);
    echo json_encode(['error' => "Not connected to '$ddName'"]);
    exit;
}
if ($ddName === '') {
    http_response_code(400);
    echo json_encode(['error' => 'dd is required']);
    exit;
}

$c    = $_SESSION['connections'][$ddName];
$opts = api_ads_connect_opts($c);

const JSON_FLAGS = JSON_UNESCAPED_UNICODE | JSON_INVALID_UTF8_SUBSTITUTE | JSON_PARTIAL_OUTPUT_ON_ERROR;

// SAP system.relations exposes RI_UpdateRule / RI_DeleteRule as numeric
// codes that are ASYMMETRIC between update and delete: update Cascade=1,
// delete Cascade=2, SetNull=3 for both, anything else Restrict. Words are
// still accepted for back-compat.
function ruleLabel(string $v, bool $isDelete = false): string {
    $v = trim($v);
    if ($v === '3' || strcasecmp($v, 'SetNull')  === 0) return 'SetNull';
    if (strcasecmp($v, 'Cascade')  === 0)               return 'Cascade';
    if (strcasecmp($v, 'Restrict') === 0)               return 'Restrict';
    if ($isDelete) return $v === '2' ? 'Cascade' : 'Restrict';
    return $v === '1' ? 'Cascade' : 'Restrict';
}

function safeStr(mixed $v): string {
    $s = trim((string)($v ?? ''));
    return mb_convert_encoding($s, 'UTF-8', 'UTF-8');
}

// ── CDX parser ────────────────────────────────────────────────────────────────
//
// A compound CDX file starts with a 1024-byte file header. Bytes 0-3 (u32 LE)
// give the offset of the structure-tag root leaf page (512 bytes). That leaf
// uses CDX's "compact" encoding with key_size = 10 and holds one entry per tag;
// each entry's key (trimmed) is the tag name.

function decodeCdxStructLeaf(string $leaf): array {
    if (strlen($leaf) < 512) return [];

    $attr  = unpack('v', substr($leaf, 0, 2))[1];
    if (!($attr & 2)) return [];           // CDX_NODE_LEAF = 2

    $nkeys     = unpack('v', substr($leaf, 2, 2))[1];
    $dup_bits  = ord($leaf[21]);
    $trl_bits  = ord($leaf[22]);
    $key_bytes = ord($leaf[23]);
    $key_size  = 10;                       // CDX_STRUCT_KEY_LEN

    if ($nkeys === 0 || $key_bytes === 0) return [];

    $CDX_EXT = 24;                         // CDX_EXT_HEADSIZE
    $buf_pos  = 512 - $CDX_EXT;           // = 488; suffix area right-edge
    $prev     = str_repeat(' ', $key_size);
    $tags     = [];
    $shift    = 32 - $dup_bits - $trl_bits;
    $trl_mask = $trl_bits > 0 ? ((1 << $trl_bits) - 1) : 0;
    $dup_mask = $dup_bits > 0 ? ((1 << $dup_bits) - 1) : 0;

    for ($i = 0; $i < $nkeys; $i++) {
        $e_off = $CDX_EXT + $i * $key_bytes;
        if ($e_off + $key_bytes > 512) break;

        // dup/trl are packed in the last 4 bytes of the entry
        if ($key_bytes < 4) break;
        $last4 = unpack('V', substr($leaf, $e_off + $key_bytes - 4, 4))[1];
        $tmp   = $shift < 32 ? (($last4 >> $shift) & 0xFFFF) : 0;
        $trl   = $tmp & $trl_mask;
        $dup   = ($tmp >> $trl_bits) & $dup_mask;

        if ($dup + $trl > $key_size) break;
        $suffix_len = $key_size - $dup - $trl;
        if ($buf_pos < $suffix_len) break;
        $buf_pos -= $suffix_len;

        // Build full key from prefix (prev) + suffix + trailing spaces
        $key = $prev;
        if ($suffix_len > 0) {
            $suffix = substr($leaf, $CDX_EXT + $buf_pos, $suffix_len);
            for ($j = 0; $j < $suffix_len; $j++) {
                $key[$dup + $j] = $suffix[$j];
            }
        }
        for ($t = $key_size - $trl; $t < $key_size; $t++) {
            $key[$t] = ' ';
        }
        $prev = $key;
        $tag  = rtrim($key);
        if ($tag !== '') $tags[] = $tag;
    }
    return $tags;
}

function fetchTagsFromCdx(string $path): array {
    $bin = @file_get_contents($path);
    if ($bin === false || strlen($bin) < 1536) return [];

    // File header (1024 bytes). Bytes 0-3 = struct_root page offset.
    $struct_root = unpack('V', substr($bin, 0, 4))[1];
    if ($struct_root === 0 || $struct_root + 512 > strlen($bin)) return [];

    $tags = decodeCdxStructLeaf(substr($bin, $struct_root, 512));
    sort($tags);
    return $tags;
}

// ── ADI parser ────────────────────────────────────────────────────────────────
//
// ADI is the ADT compound index format. Page 2 (offset 1024) is the tag
// directory. Each 6-byte entry encodes a "fmarker" page number; that page
// contains "F<n>" or "F<n1>;F<n2>" giving 1-based field numbers. The tag
// name is the field name of the first component, read from the companion .adt
// file header (hdr_len at byte 32; fields at byte 400, 200 bytes each).

function fetchTagsFromAdi(string $adiPath): array {
    $PAGE = 512;
    $DIR_ENTRY_START = 24;
    $DIR_ENTRY_SIZE  = 6;

    $adi = @file_get_contents($adiPath);
    if ($adi === false || strlen($adi) < 3 * $PAGE) return [];

    // Tag directory: page 2
    $pg2   = substr($adi, 2 * $PAGE, $PAGE);
    $count = unpack('v', substr($pg2, 2, 2))[1];
    if ($count === 0 || $count > 200) return [];

    // Collect first-component field numbers from fmarker pages
    $fnums = [];
    for ($i = 0; $i < $count; $i++) {
        $off = $DIR_ENTRY_START + $i * $DIR_ENTRY_SIZE;
        if ($off + 1 > $PAGE) break;
        $xx     = ord($pg2[$off]);
        $fmk_pg = $xx + 1;
        $fmk_off = $fmk_pg * $PAGE;
        if ($fmk_off + $PAGE > strlen($adi)) continue;
        $fmk = substr($adi, $fmk_off, $PAGE);
        if ($fmk[0] !== 'F') continue;
        // Parse "F<n>" or "F<n1>;F<n2>..."
        $j = 1;
        while ($j < $PAGE && $fmk[$j] >= '1' && $fmk[$j] <= '9') {
            $n = 0;
            while ($j < $PAGE && $fmk[$j] >= '0' && $fmk[$j] <= '9') {
                $n = $n * 10 + (ord($fmk[$j]) - 48);
                $j++;
            }
            if ($n > 0 && $n <= 255) { $fnums[] = $n; break; } // first field only
        }
    }

    if (empty($fnums)) return [];

    // Read ADT companion file to resolve field numbers to names
    $adtPath = preg_replace('/\\.adi$/i', '.adt', $adiPath);
    if (!file_exists($adtPath)) return [];

    $hdr = @file_get_contents($adtPath, false, null, 0, 400);
    if ($hdr === false || strlen($hdr) < 40) return [];

    $hdr_len    = unpack('V', substr($hdr, 32, 4))[1];
    $num_fields = (int)(($hdr_len - 400) / 200);
    if ($num_fields <= 0) return [];

    $fld_data = @file_get_contents($adtPath, false, null, 400, $num_fields * 200);
    if ($fld_data === false) return [];

    $field_names = [];
    for ($i = 0; $i < $num_fields; $i++) {
        $d    = substr($fld_data, $i * 200, 200);
        $name = rtrim(substr($d, 0, 128), "\0 ");
        $field_names[] = $name;
    }

    $tags = [];
    foreach ($fnums as $fnum) {
        if ($fnum >= 1 && $fnum <= count($field_names) && $field_names[$fnum - 1] !== '') {
            $tags[] = $field_names[$fnum - 1];
        }
    }

    $tags = array_values(array_unique($tags));
    sort($tags);
    return $tags;
}

// ── Index tag resolver ────────────────────────────────────────────────────────

// Resolve index file path; must exist and stay under the DD directory.
function resolveIndexPath(string $idxFile, string $addDir): string {
    return api_resolve_path_under_root($idxFile, $addDir) ?? '';
}

// Primary path: ask the engine (auto-opens sibling .adi/.cdx like AdsOpenTable).
function fetchIndexTagsFromEngine(AdsConnection $conn, string $tableName): array {
    if ($tableName === '') return [];
    try {
        $tbl  = AdsTable::open($conn, $tableName, 0);
        $raw  = $tbl->getIndexTags();
        $tbl->close();
        $tags = [];
        foreach ($raw as $entry) {
            $tag = trim((string)($entry['tag'] ?? ''));
            if ($tag !== '') $tags[] = $tag;
        }
        $tags = array_values(array_unique($tags));
        sort($tags);
        return $tags;
    } catch (Throwable) {
        return [];
    }
}

// Fallback: parse CDX/ADI from system.indexes paths or DD directory probes.
function fetchIndexTags(AdsConnection $conn, string $addDir, string $tableName): array {
    if ($tableName === '') return [];
    if (!preg_match('/^[A-Za-z_][A-Za-z0-9_]*$/', $tableName)) {
        return [];
    }

    $engine = fetchIndexTagsFromEngine($conn, $tableName);
    if (!empty($engine)) return $engine;

    try {
        $tags = [];
        $found = false;
        $stmt = $conn->query("SELECT TABLE_NAME, INDEX_FILE FROM system.indexes");
        while ($row = $stmt->fetchAssoc()) {
            $tbl = trim((string)($row['TABLE_NAME'] ?? ''));
            if (strcasecmp($tbl, $tableName) !== 0) continue;
            $idxFile = trim((string)($row['INDEX_FILE'] ?? ''));
            if ($idxFile === '') continue;
            $path = resolveIndexPath($idxFile, $addDir);
            if (!file_exists($path)) continue;
            $found = true;
            $ext = strtolower(pathinfo($path, PATHINFO_EXTENSION));
            if ($ext === 'cdx') {
                foreach (fetchTagsFromCdx($path) as $t) $tags[] = $t;
            } elseif ($ext === 'adi') {
                foreach (fetchTagsFromAdi($path) as $t) $tags[] = $t;
            }
        }
        $stmt->close();

        // Fallback: if system.indexes had no entry, probe the DD directory directly.
        if (!$found) {
            $lower = strtolower($tableName);
            foreach (['adi', 'cdx'] as $ext) {
                // Try exact case, then lowercase, then uppercase
                foreach ([$tableName, $lower, strtoupper($tableName)] as $stem) {
                    $path = $addDir . DIRECTORY_SEPARATOR . $stem . '.' . $ext;
                    if (file_exists($path)) {
                        foreach (($ext === 'adi' ? fetchTagsFromAdi($path) : fetchTagsFromCdx($path)) as $t) {
                            $tags[] = $t;
                        }
                        break 2;
                    }
                }
            }
        }

        $tags = array_values(array_unique($tags));
        sort($tags);
        return $tags;
    } catch (Throwable) {
        return [];
    }
}

// ── Request dispatch ──────────────────────────────────────────────────────────

try {
    $conn   = AdsConnection::connect($opts);
    $addDir = dirname($c['path']);

    // ── Return all tables ──────────────────────────────────────────────────
    if ($action === 'tables') {
        $tables = [];
        $stmt   = $conn->query("SELECT Name FROM system.tables ORDER BY Name");
        while ($row = $stmt->fetchAssoc()) {
            $t = safeStr($row['Name']);
            if ($t !== '') $tables[] = $t;
        }
        $conn->close();
        echo json_encode(['tables' => $tables], JSON_FLAGS);
        exit;
    }

    // ── Return tag names for a table (user changed table select) ──────────
    if ($action === 'tags') {
        $table = trim($_GET['table'] ?? '');
        if ($table === '') {
            http_response_code(400);
            echo json_encode(['error' => 'table is required'], JSON_FLAGS);
            exit;
        }
        api_validate_identifier($table, 'table name');
        $tags  = fetchIndexTags($conn, $addDir, $table);
        $conn->close();
        echo json_encode(['tags' => $tags], JSON_FLAGS);
        exit;
    }

    // ── Return RI object by name, with embedded index tags ─────────────────
    if ($riName === '') {
        http_response_code(400);
        echo json_encode(['error' => 'ri name or action required']);
        exit;
    }
    api_validate_identifier($riName, 'RI name');

    $ri   = null;
    $stmt = $conn->query("SELECT * FROM system.relations");
    while ($row = $stmt->fetchAssoc()) {
        if (strcasecmp(safeStr($row['Name'] ?? ''), $riName) === 0) {
            $ri = [
                'name'       => safeStr($row['Name']             ?? ''),
                'parent'     => safeStr($row['RI_Primary_Table'] ?? ''),
                'child'      => safeStr($row['RI_Foreign_Table'] ?? ''),
                'parent_tag' => safeStr($row['RI_Primary_Index'] ?? ''),
                'child_tag'  => safeStr($row['RI_Foreign_Index'] ?? ''),
                'update_opt' => ruleLabel(safeStr($row['RI_UpdateRule'] ?? ''), false),
                'delete_opt' => ruleLabel(safeStr($row['RI_DeleteRule'] ?? ''), true),
                'fail_table' => '',   // SAP system.relations has no fail-table column
            ];
            break;
        }
    }

    if ($ri === null) {
        $ri = [
            'name' => $riName, 'parent' => '', 'child' => '',
            'parent_tag' => '', 'child_tag' => '',
            'update_opt' => 'Restrict', 'delete_opt' => 'Restrict', 'fail_table' => '',
        ];
    }

    $parentTags = fetchIndexTags($conn, $addDir, $ri['parent']);
    $childTags  = fetchIndexTags($conn, $addDir, $ri['child']);

    $conn->close();
    echo json_encode([
        'ri'         => $ri,
        'parentTags' => $parentTags,
        'childTags'  => $childTags,
    ], JSON_FLAGS);

} catch (Throwable $e) {
    api_exception(500, $e);
}
