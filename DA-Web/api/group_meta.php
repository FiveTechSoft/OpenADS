<?php
/**
 * api/group_meta.php — permissions granted to a DD group.
 * GET ?dd=&group=
 *
 * Returns { data: [ row, … ] } where each row is:
 *   { object, type, parent, canSelect, canInsert, canUpdate, canDelete, canExec }
 *
 * Object types returned:
 *   1  = Table          (parent = "")
 *   4  = Field/Column   (parent = table name)
 *   6  = View           (parent = "")
 *   10 = StoredProc     (parent = "")
 *   18 = Function       (parent = "")
 *
 * Field rows (type 4) are derived by the engine from table permissions and are
 * interleaved after their parent table's row.
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';

$ddName    = trim($_GET['dd']    ?? '');
$groupName = trim($_GET['group'] ?? '');

if (!isset($_SESSION['connections'][$ddName])) {
    http_response_code(401);
    echo json_encode(['error' => "Not connected to '$ddName'"]);
    exit;
}
if ($ddName === '' || $groupName === '') {
    http_response_code(400);
    echo json_encode(['error' => 'dd and group are required']);
    exit;
}

$c    = $_SESSION['connections'][$ddName];
$opts = api_ads_connect_opts($c);
$perf = api_perf_start();

try {
    $conn = AdsConnection::connect($opts);
    api_perf_mark($perf, 'connect');

    // ── Step 1: load all permissions for this group into a map ──────────────────
    // Key for top-level objects:  type . "\0" . lowercase(obj_name)
    // Key for field objects:      "4\0" . lowercase(parent) . "\0" . lowercase(obj_name)
    $permMap = [];
    // RCB 06/30/2026: Ask the server for this group's permission rows only.
    // OpenADS pushes this predicate into system.permissions materialization, so
    // DA-Web no longer scans the full compatibility matrix for every group.
    $qgroup = api_sql_quote($groupName);
    $stmt = $conn->query("SELECT * FROM system.permissions WHERE GRANTEE = '$qgroup'");
    while ($row = $stmt->fetchAssoc()) {
        // OBJ_TYPE is a DBF numeric column → may be float; cast to string for keys.
        $type   = (string)(int)($row['Object_Type'] ?? 0);
        $name   = strtolower(trim((string)($row['Name']  ?? '')));
        $parent = strtolower(trim((string)($row['PARENT']     ?? '')));
        if ($type === '4') {
            $key = "4\0{$parent}\0{$name}";
        } else {
            $key = "{$type}\0{$name}";
        }
        $permMap[$key] = $row;
    }
    $stmt->close();
    api_perf_mark($perf, 'permissions');

    // ── Step 2: helper to build one output row ──────────────────────────────────
    // Any non-'0' value ("1" = normal grant, "2" = admin/WITH-GRANT level) means granted.
    $hasGrant = fn($p, string $col): bool => $p && ($p[$col] ?? '0') !== '0';

    $buildRow = function (string $name, string $type, string $parent) use ($permMap, $hasGrant): array {
        if ($type === '4') {
            $key = "4\0" . strtolower($parent) . "\0" . strtolower($name);
        } else {
            $key = "{$type}\0" . strtolower($name);
        }
        $p = $permMap[$key] ?? null;
        return [
            'object'    => $name,
            'type'      => (int)$type,
            'parent'    => $parent,
            'canSelect' => $hasGrant($p, 'SELECT')  ? 'Yes' : 'No',
            'canInsert' => $hasGrant($p, 'INSERT')  ? 'Yes' : 'No',
            'canUpdate' => $hasGrant($p, 'UPDATE')  ? 'Yes' : 'No',
            'canDelete' => $hasGrant($p, 'DELETE')  ? 'Yes' : 'No',
            'canExec'   => $hasGrant($p, 'EXECUTE') ? 'Yes' : 'No',
            'canAlter'  => $hasGrant($p, 'ALTER')   ? 'Yes' : 'No',
            'canDrop'   => $hasGrant($p, 'DROP')    ? 'Yes' : 'No',
        ];
    };

    $rows = [];

    // ── Step 3: enumerate all objects and emit rows ─────────────────────────────
    // Tables + their fields (fields come from system.permissions directly)
    $tables = [];
    $stmt = $conn->query('SELECT Name FROM system.tables ORDER BY Name');
    while ($row = $stmt->fetchAssoc()) {
        $tables[] = trim($row['Name']);
    }
    $stmt->close();

    // Collect field rows per table from permMap (type 4).
    // OBJ_TYPE is a DBF numeric field → PHP float; cast to string before comparing.
    $fieldsByTable = [];
    foreach ($permMap as $key => $p) {
        if ((string)($p['Object_Type'] ?? '') !== '4') continue;
        $tbl = strtolower(trim((string)($p['PARENT'] ?? '')));
        $fn  = trim((string)($p['Name'] ?? ''));
        if ($tbl !== '' && $fn !== '') $fieldsByTable[$tbl][] = $fn;
    }

    foreach ($tables as $tbl) {
        $rows[] = $buildRow($tbl, '1', '');
        // Append field rows for this table, sorted
        $tlower = strtolower($tbl);
        if (!empty($fieldsByTable[$tlower])) {
            $fnames = $fieldsByTable[$tlower];
            sort($fnames);
            foreach ($fnames as $fn) {
                $rows[] = $buildRow($fn, '4', $tbl);
            }
        }
    }

    // Views
    try {
        $stmt = $conn->query('SELECT Name FROM system.views ORDER BY Name');
        while ($row = $stmt->fetchAssoc()) {
            $rows[] = $buildRow(trim($row['Name']), '6', '');
        }
        $stmt->close();
    } catch (Throwable $e) {}

    // Stored Procedures
    try {
        $stmt = $conn->query('SELECT Name FROM system.storedprocedures ORDER BY Name');
        while ($row = $stmt->fetchAssoc()) {
            $rows[] = $buildRow(trim($row['Name']), '10', '');
        }
        $stmt->close();
    } catch (Throwable $e) {}

    // Functions
    try {
        $stmt = $conn->query('SELECT Name FROM system.functions ORDER BY Name');
        while ($row = $stmt->fetchAssoc()) {
            $rows[] = $buildRow(trim($row['Name']), '18', '');
        }
        $stmt->close();
    } catch (Throwable $e) {}
    api_perf_mark($perf, 'objects');

    $conn->close();
    echo json_encode([
        'data' => $rows,
        'perf' => api_perf_finish($perf),
    ]);
} catch (Throwable $e) {
    api_exception(500, $e);
}
