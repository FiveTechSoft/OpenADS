<?php
/**
 * api/save_index.php — drop and recreate an index tag with new settings.
 *
 * POST { dd, table, tag, expression, descending, unique }
 *
 * Uses EXECUTE PROCEDURE sp_CreateIndex90 after dropping the existing tag.
 * sp_CreateIndex90(table, indexFile, tag, expression, condition, flags, pageSize, collation)
 *
 * Flags bitmask (ADS index flags):
 *   ADS_UNIQUE        = 0x0002
 *   ADS_DESCENDING    = 0x0010
 *   ADS_BINARY_KEY    = 0x0004 (case-sensitive / binary)
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';

$body       = json_decode(file_get_contents('php://input'), true) ?? [];
$ddName     = trim($body['dd']         ?? '');
$table      = trim($body['table']      ?? '');
$tag        = trim($body['tag']        ?? '');
$origTag    = trim($body['origTag']    ?? '') ?: $tag;  // original name for DROP (rename support)
$expression = trim($body['expression'] ?? '');
$descending = strcasecmp(trim($body['descending'] ?? 'No'), 'Yes') === 0;
$unique     = strcasecmp(trim($body['unique']     ?? 'No'), 'Yes') === 0;
$binary     = strcasecmp(trim($body['binary']     ?? 'No'), 'Yes') === 0;
$primary    = strcasecmp(trim($body['primary']    ?? 'No'), 'Yes') === 0;

if (!isset($_SESSION['connections'][$ddName])) {
    http_response_code(401);
    echo json_encode(['error' => "Not connected to '$ddName'"]);
    exit;
}
if ($ddName === '' || $table === '' || $tag === '' || $expression === '') {
    http_response_code(400);
    echo json_encode(['error' => 'dd, table, tag and expression are required']);
    exit;
}

$c    = $_SESSION['connections'][$ddName];
$opts = ['path' => $c['path']];
if (($c['username'] ?? '') !== '') $opts['user']     = $c['username'];
if (($c['password'] ?? '') !== '') $opts['password'] = $c['password'];

// Determine index file extension: .cdx for DBF tables, .adi for ADT.
// For free-table directories, probe the filesystem; for DD connections
// tables are ADT so default to .adi.
function indexExtForTable(string $dir, string $table): string {
    $base = rtrim($dir, '/\\') . DIRECTORY_SEPARATOR . $table;
    foreach (['.dbf', '.DBF'] as $ext) {
        if (file_exists($base . $ext)) return '.cdx';
    }
    return '.adi';
}

$isFree    = (($c['entryType'] ?? '') === 'free');
$indexExt  = $isFree ? indexExtForTable($c['path'], $table) : '.adi';
$indexFile = strtolower($table) . $indexExt;

try {
    $conn = AdsConnection::connect($opts);

    // Build flags bitmask
    $flags = 2;  // base flag
    if ($unique)     $flags |= 0x0800;  // ADS_UNIQUE
    if ($descending) $flags |= 0x0010;  // ADS_DESCENDING
    if ($binary)     $flags |= 0x0004;  // ADS_BINARY_KEY

    // Drop existing tag using origTag (handles renames; ignore error if not found)
    if ($origTag !== '') {
        try {
            $conn->execute("DROP INDEX $origTag ON $table");
        } catch (Throwable $e) {}
    }

    // Recreate index
    $esc = fn($s) => str_replace("'", "''", $s);
    $sql = "EXECUTE PROCEDURE sp_CreateIndex90('{$esc($table)}', '{$esc($indexFile)}', '{$esc($tag)}', '{$esc($expression)}', '', $flags, 512, '')";
    $conn->execute($sql);

    // Update primary key property if requested (ADS_DD_TABLE_PRIMARY_KEY = 202)
    if ($primary) {
        $dict = AdsDictionary::fromConnection($conn);
        $dict->setTableProperty($table, 202, $tag);
    }

    $conn->close();
    echo json_encode(['saved' => true]);
} catch (Throwable $e) {
    api_exception(500, $e);
}
