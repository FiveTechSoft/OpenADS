<?php
/**
 * api/tree.php — jsTree lazy-load provider
 *
 * Actions:
 *   roots               → all configured DDs (connected/disconnected)
 *   dd_children         → category nodes for a connected DD
 *   category_children   → leaf nodes for a category (tables, users, etc.)
 *   table_children      → table sub-nodes (fields, indexes)
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';

$configFile = __DIR__ . '/../config/dictionaries.json';

function loadDicts(string $file): array {
    if (!file_exists($file)) return [];
    return json_decode(file_get_contents($file), true) ?? [];
}

function getConn(string $ddName): ?AdsConnection {
    if (!isset($_SESSION['connections'][$ddName])) return null;
    $c = $_SESSION['connections'][$ddName];
    $opts = api_ads_connect_opts($c);
    return AdsConnection::connect($opts);
}

$action = $_GET['action'] ?? '';
$ddName = $_GET['dd']       ?? '';
$cat    = $_GET['cat']      ?? '';
$table  = $_GET['table']    ?? '';

// ─── roots ───────────────────────────────────────────────────────────────────
if ($action === 'roots') {
    $dicts = loadDicts($configFile);
    $open  = array_keys($_SESSION['connections'] ?? []);
    $nodes = [];
    foreach ($dicts as $d) {
        $connected  = in_array($d['name'], $open, true);
        $entryType  = $d['entryType'] ?? 'dd';
        $isFree     = ($entryType === 'free');
        $icon = $isFree
            ? ($connected ? 'jstree-icon-free-open' : 'jstree-icon-free-closed')
            : ($connected ? 'jstree-icon-dd-open'   : 'jstree-icon-dd-closed');
        $nodes[] = [
            'id'       => 'dd_' . $d['name'],
            'text'     => $d['name'] . ($isFree ? ' 📂' : ''),
            'icon'     => $icon,
            'children' => $connected,
            'state'    => ['opened' => false],
            'li_attr'  => ['data-dd' => $d['name'], 'data-type' => 'dd'],
            'a_attr'   => ['data-dd'        => $d['name'],
                           'data-type'      => 'dd',
                           'data-entry'     => $entryType,
                           'data-connected' => $connected ? 'true' : 'false'],
        ];
    }
    echo json_encode($nodes);
    exit;
}

// ─── dd_children ─────────────────────────────────────────────────────────────
if ($action === 'dd_children') {
    if ($ddName === '') { echo json_encode([]); exit; }

    $dicts = loadDicts($configFile);
    $entryType = 'dd';
    foreach ($dicts as $d) {
        if ($d['name'] === $ddName) { $entryType = $d['entryType'] ?? 'dd'; break; }
    }

    // Free-tables directory: scan filesystem — no DD, no system.tables query.
    if ($entryType === 'free') {
        $c = $_SESSION['connections'][$ddName] ?? null;
        if (!$c) { echo json_encode([]); exit; }

        $dir   = rtrim($c['path'], '/\\');
        $nodes = [];
        $seen  = [];
        foreach (['*.adt', '*.ADT', '*.dbf', '*.DBF'] as $pat) {
            foreach (glob($dir . DIRECTORY_SEPARATOR . $pat) ?: [] as $file) {
                $base = strtoupper(pathinfo($file, PATHINFO_FILENAME));
                // Skip ADS temp tables and already-listed names
                if ($base === '' || $base[0] === '_' || isset($seen[$base])) continue;
                $seen[$base] = true;
                $nodes[] = [
                    'id'       => "tbl_{$ddName}_{$base}",
                    'text'     => $base,
                    'icon'     => 'jstree-icon-table',
                    'children' => true,
                    'a_attr'   => ['data-dd' => $ddName, 'data-type' => 'table',
                                   'data-table' => $base, 'data-entry' => 'free'],
                ];
            }
        }
        usort($nodes, fn($a, $b) => strcmp($a['text'], $b['text']));
        echo json_encode($nodes);
        exit;
    }

    // Data dictionary: return category nodes.
    $categories = [
        ['id' => "cat_{$ddName}_health",     'text' => 'Health',            'icon' => 'jstree-icon-health', 'leaf' => true],
        ['id' => "cat_{$ddName}_tables",     'text' => 'Tables',            'icon' => 'jstree-icon-tables'],
        ['id' => "cat_{$ddName}_views",      'text' => 'Views',             'icon' => 'jstree-icon-views'],
        ['id' => "cat_{$ddName}_procs",      'text' => 'Stored Procedures', 'icon' => 'jstree-icon-procs'],
        ['id' => "cat_{$ddName}_functions",  'text' => 'Functions',         'icon' => 'jstree-icon-procs'],
        // Triggers are shown per-table (under each table node) — no global category
        ['id' => "cat_{$ddName}_users",      'text' => 'Users',             'icon' => 'jstree-icon-users'],
        ['id' => "cat_{$ddName}_groups",     'text' => 'Groups',            'icon' => 'jstree-icon-groups'],
        ['id' => "cat_{$ddName}_ri",         'text' => 'RI Objects',        'icon' => 'jstree-icon-ri'],
        ['id' => "cat_{$ddName}_links",      'text' => 'Links',             'icon' => 'jstree-icon-links'],
    ];
    foreach ($categories as &$c) {
        $isLeaf = !empty($c['leaf']);
        unset($c['leaf']);
        $c['children'] = !$isLeaf;
        $c['a_attr']   = ['data-dd' => $ddName, 'data-type' => 'category',
                          'data-cat' => substr($c['id'], strlen("cat_{$ddName}_"))];
    }
    unset($c);
    echo json_encode($categories);
    exit;
}

// ─── category_children ───────────────────────────────────────────────────────
if ($action === 'category_children') {
    if ($ddName === '' || $cat === '') { echo json_encode([]); exit; }

    $conn = getConn($ddName);
    if ($conn === null) { echo json_encode([]); exit; }

    $nodes = [];
    try {
        switch ($cat) {
            case 'tables':
                $stmt = $conn->query("SELECT Name FROM system.tables ORDER BY Name");
                while ($row = $stmt->fetchAssoc()) {
                    $t = $row['Name'];
                    $nodes[] = [
                        'id'      => "tbl_{$ddName}_{$t}",
                        'text'    => $t,
                        'icon'    => 'jstree-icon-table',
                        'children'=> true,
                        'a_attr'  => ['data-dd' => $ddName, 'data-type' => 'table', 'data-table' => $t],
                    ];
                }
                break;

            case 'views':
                $stmt = $conn->query("SELECT Name FROM system.views ORDER BY Name");
                while ($row = $stmt->fetchAssoc()) {
                    $v = $row['Name'];
                    $nodes[] = ['id' => "view_{$ddName}_{$v}", 'text' => $v,
                                'icon' => 'jstree-icon-view', 'children' => false,
                                'a_attr' => ['data-dd' => $ddName, 'data-type' => 'view', 'data-name' => $v]];
                }
                break;

            case 'procs':
                $stmt = $conn->query("SELECT Name FROM system.storedprocedures ORDER BY Name");
                while ($row = $stmt->fetchAssoc()) {
                    $p = $row['Name'];
                    $nodes[] = ['id' => "proc_{$ddName}_{$p}", 'text' => $p,
                                'icon' => 'jstree-icon-proc', 'children' => false,
                                'a_attr' => ['data-dd' => $ddName, 'data-type' => 'proc', 'data-name' => $p]];
                }
                break;

            case 'functions':
                $stmt = $conn->query("SELECT Name FROM system.functions ORDER BY Name");
                while ($row = $stmt->fetchAssoc()) {
                    $p = $row['Name'];
                    $nodes[] = ['id' => "fn_{$ddName}_{$p}", 'text' => $p,
                                'icon' => 'jstree-icon-proc', 'children' => false,
                                'a_attr' => ['data-dd' => $ddName, 'data-type' => 'function', 'data-name' => $p]];
                }
                break;

            case 'triggers':
                $stmt = $conn->query("SELECT Name FROM system.triggers ORDER BY Name");
                while ($row = $stmt->fetchAssoc()) {
                    $tr = $row['Name'];
                    $nodes[] = ['id' => "trg_{$ddName}_{$tr}", 'text' => $tr,
                                'icon' => 'jstree-icon-trigger', 'children' => false,
                                'a_attr' => ['data-dd' => $ddName, 'data-type' => 'trigger', 'data-name' => $tr]];
                }
                break;

            case 'users':
                // Name is SAP's column on system.users; USER_NAME was the old
                // OpenADS-only spelling, kept as a fallback for older builds.
                $stmt = $conn->query("SELECT Name FROM system.users ORDER BY Name");
                while ($row = $stmt->fetchAssoc()) {
                    $u = trim((string)($row['Name'] ?? $row['USER_NAME'] ?? ''));
                    if ($u === '') continue;
                    $nodes[] = ['id' => "usr_{$ddName}_{$u}", 'text' => $u,
                                'icon' => 'jstree-icon-user', 'children' => false,
                                'a_attr' => ['data-dd' => $ddName, 'data-type' => 'user', 'data-name' => $u]];
                }
                break;

            case 'groups':
                $stmt = $conn->query("SELECT Name FROM system.usergroups ORDER BY Name");
                $seen = [];
                while ($row = $stmt->fetchAssoc()) {
                    $g = trim((string)($row['Name'] ?? $row['GROUP_NAME'] ?? ''));
                    if ($g === '' || isset($seen[$g])) continue;
                    $seen[$g] = true;
                    $nodes[] = ['id' => "grp_{$ddName}_{$g}", 'text' => $g,
                                'icon' => 'jstree-icon-group', 'children' => false,
                                'a_attr' => ['data-dd' => $ddName, 'data-type' => 'group', 'data-name' => $g]];
                }
                // ACE built-in groups are always present in every DD; surface them
                // even if system.usergroups omits them in this OpenADS build.
                foreach (['DB:Admin', 'DB:Backup', 'DB:Debug', 'DB:Public'] as $builtin) {
                    if (isset($seen[$builtin])) continue;
                    $seen[$builtin] = true;
                    $nodes[] = ['id' => "grp_{$ddName}_{$builtin}", 'text' => $builtin,
                                'icon' => 'jstree-icon-group', 'children' => false,
                                'a_attr' => ['data-dd' => $ddName, 'data-type' => 'group', 'data-name' => $builtin]];
                }
                usort($nodes, fn($a, $b) => strcmp($a['text'], $b['text']));
                break;

            case 'ri':
                $stmt = $conn->query("SELECT Name FROM system.relations ORDER BY Name");
                while ($row = $stmt->fetchAssoc()) {
                    $r = $row['Name'];
                    $nodes[] = ['id' => "ri_{$ddName}_{$r}", 'text' => $r,
                                'icon' => 'jstree-icon-ri', 'children' => false,
                                'a_attr' => ['data-dd' => $ddName, 'data-type' => 'ri', 'data-name' => $r]];
                }
                break;

            case 'links':
                $stmt = $conn->query("SELECT LINK_NAME FROM system.links ORDER BY LINK_NAME");
                while ($row = $stmt->fetchAssoc()) {
                    $l = $row['LINK_NAME'];
                    $nodes[] = ['id' => "lnk_{$ddName}_{$l}", 'text' => $l,
                                'icon' => 'jstree-icon-link', 'children' => false,
                                'a_attr' => ['data-dd' => $ddName, 'data-type' => 'link', 'data-name' => $l]];
                }
                break;
        }
    } catch (Throwable $e) {
        $nodes = [['id' => "err_{$ddName}_{$cat}", 'text' => '⚠ ' . $e->getMessage(),
                   'icon' => 'jstree-icon-error', 'children' => false,
                   'a_attr' => ['data-type' => 'error']]];
    } finally {
        $conn->close();
    }

    echo json_encode($nodes);
    exit;
}

// ─── table_children ──────────────────────────────────────────────────────────
if ($action === 'table_children') {
    if ($ddName === '' || $table === '') { echo json_encode([]); exit; }

    $entryType2 = $_GET['entry'] ?? '';   // 'free' for free-table directories
    $conn = getConn($ddName);
    if ($conn === null) { echo json_encode([]); exit; }

    $nodes = [];
    try {
        // Fields sub-node — leaf; click opens a metadata tab
        $isFree = ($entryType2 === 'free');
        $nodes[] = [
            'id'       => "fields_{$ddName}_{$table}",
            'text'     => 'Fields',
            'icon'     => 'jstree-icon-fields',
            'children' => false,
            'a_attr'   => ['data-dd' => $ddName, 'data-type' => 'fields',
                           'data-table' => $table, 'data-entry' => $entryType2],
        ];
        // Indexes sub-node — leaf; click opens a metadata tab
        $nodes[] = [
            'id'       => "indexes_{$ddName}_{$table}",
            'text'     => 'Indexes',
            'icon'     => 'jstree-icon-indexes',
            'children' => false,
            'a_attr'   => ['data-dd' => $ddName, 'data-type' => 'indexes',
                           'data-table' => $table, 'data-entry' => $entryType2],
        ];
        if (!$isFree) {
            // RCB 06/27/2026: DD tables expose persisted table properties as
            // a first-class table child, alongside fields/indexes/triggers.
            $nodes[] = [
                'id'       => "props_{$ddName}_{$table}",
                'text'     => 'Properties',
                'icon'     => 'jstree-icon-properties',
                'children' => false,
                'a_attr'   => ['data-dd' => $ddName, 'data-type' => 'table_properties', 'data-table' => $table],
            ];
            // Triggers — only for DD-connected tables
            $nodes[] = [
                'id'       => "tbl_triggers_{$ddName}_{$table}",
                'text'     => 'Triggers',
                'icon'     => 'jstree-icon-triggers',
                'children' => false,
                'a_attr'   => ['data-dd' => $ddName, 'data-type' => 'table_triggers', 'data-table' => $table],
            ];
            // Generate SQL — only for DD-connected tables
            $nodes[] = [
                'id'       => "gen_sql_{$ddName}_{$table}",
                'text'     => 'Generate SQL',
                'icon'     => 'jstree-icon-sql',
                'children' => false,
                'a_attr'   => ['data-dd' => $ddName, 'data-type' => 'gen_sql', 'data-table' => $table],
            ];
        }
    } finally {
        $conn->close();
    }

    echo json_encode($nodes);
    exit;
}

echo json_encode([]);
