<?php
/**
 * api/user_groups.php — list groups a DD user belongs to.
 * GET ?dd=&user=
 *
 * Returns {
 *   groups:        ["Group1", "Group2", ...],   // editable regular groups
 *   builtinGroups: ["DB:Public", ...],           // SAP built-in groups (read-only)
 *   allGroups:     ["A","B",...],                // all regular groups in DD (no DB: prefix)
 *   dbGroupNote:   string                        // explanation of DB: group limitations
 * }
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';

/** Read a system-table column regardless of AdsGetFieldName casing. */
function daweb_row_field(array $row, string $col): string
{
    if (isset($row[$col])) {
        return trim((string)$row[$col]);
    }
    foreach ($row as $k => $v) {
        if (strcasecmp((string)$k, $col) === 0) {
            return trim((string)$v);
        }
    }
    return '';
}

$ddName   = trim($_GET['dd']   ?? '');
$userName = trim($_GET['user'] ?? '');

if (!isset($_SESSION['connections'][$ddName])) {
    http_response_code(401);
    echo json_encode(['error' => "Not connected to '$ddName'"]);
    exit;
}
if ($ddName === '' || $userName === '') {
    http_response_code(400);
    echo json_encode(['error' => 'dd and user are required']);
    exit;
}

$c    = $_SESSION['connections'][$ddName];
$opts = api_ads_connect_opts($c);

try {
    $conn = AdsConnection::connect($opts);

    // Filter memberships in PHP — avoids padded CHAR / case mismatches on WHERE.
    $groups        = [];
    $builtinGroups = [];
    $userKey       = strtolower($userName);
    $stmt = $conn->query('SELECT GROUP_NAME, USER_NAME FROM system.usergroupmembers');
    while ($row = $stmt->fetchAssoc()) {
        $u = strtolower(daweb_row_field($row, 'USER_NAME'));
        if ($u !== $userKey) continue;
        $g = daweb_row_field($row, 'GROUP_NAME');
        if ($g === '') continue;
        if (strncasecmp($g, 'DB:', 3) === 0) {
            $builtinGroups[] = $g;
        } else {
            $groups[] = $g;
        }
    }
    $stmt->close();
    sort($groups);
    sort($builtinGroups);

    // SAP: every authenticated user is in DB:Public even when the row is missing.
    $hasPublic = false;
    foreach ($builtinGroups as $g) {
        if (strcasecmp($g, 'DB:Public') === 0) {
            $hasPublic = true;
            break;
        }
    }
    if (!$hasPublic) {
        $builtinGroups[] = 'DB:Public';
        sort($builtinGroups);
    }

    $allGroups = [];
    // Name is SAP's column; GROUP_NAME was the old OpenADS-only spelling.
    $stmt3 = $conn->query('SELECT Name FROM system.usergroups ORDER BY Name');
    while ($row = $stmt3->fetchAssoc()) {
        $g = daweb_row_field($row, 'Name');
        if ($g === '') $g = daweb_row_field($row, 'GROUP_NAME');
        if ($g !== '' && strncasecmp($g, 'DB:', 3) !== 0) {
            $allGroups[] = $g;
        }
    }
    $stmt3->close();

    $conn->close();
    echo json_encode([
        'groups'        => $groups,
        'builtinGroups' => $builtinGroups,
        'allGroups'     => $allGroups,
        'dbGroupNote'   => 'DB:Admin, DB:Backup and DB:Debug memberships are encoded with a '
                         . 'per-user cipher in the binary .add file that OpenADS cannot decode '
                         . 'without the SAP DLL. They will not appear here even if the user '
                         . 'is a member. DB:Public is always shown (all users are members).',
    ]);
} catch (Throwable $e) {
    api_exception(500, $e);
}
