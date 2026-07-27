<?php
/**
 * api/trigger_body.php — read trigger metadata + full SQL body via AdsDictionary API.
 *
 * POST { dd, table }
 *   Returns { triggers: [ { name, timing, event, priority, enabled, body } ] }
 *
 * Uses system.triggers virtual table for list + getTriggerProperty for full body.
 *   Property codes: 1401=event_mask(u32) 1402=timing(u32) 1404=body 1407=options(u32) 1408=table
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';
require_once __DIR__ . '/openads_stubs.php';

$req    = json_decode(file_get_contents('php://input'), true) ?? [];
$ddName = trim($req['dd']    ?? '');
$table  = trim($req['table'] ?? '');

if ($table === '') {
    api_error(400, 'table is required');
}
api_validate_identifier($table, 'table name');

$c = api_require_connection($ddName);
$opts = api_ads_connect_opts($c);

function timing_str(int $t): string {
    return match($t) { 1 => 'BEFORE', 2 => 'INSTEAD OF', 4 => 'AFTER', default => '' };
}
function event_str(int $e): string {
    return match($e) { 1 => 'INSERT', 2 => 'UPDATE', 3 => 'DELETE', default => '' };
}

try {
    $conn = AdsConnection::connect($opts);
    $dict = AdsDictionary::fromConnection($conn);

    // Get trigger list filtered by table from system.triggers
    $stmt = $conn->query("SELECT Name FROM system.triggers WHERE Trig_TableName = '"
                          . api_sql_quote($table) . "'");
    $names = [];
    while ($row = $stmt->fetchAssoc()) {
        $names[] = $row['Name'];
    }
    $stmt->close();

    $triggers = [];
    foreach ($names as $trigName) {
        // Always use composite "table::name" key — plain-name lookup in find_trigger
        // returns null when multiple tables share the same trigger name (e.g. "Insert AuditLog").
        $trigKey  = $table . '::' . $trigName;
        // 1401=event_type (1=INSERT 2=UPDATE 3=DELETE), 1402=timing (1=BEFORE 2=INSTEAD_OF 4=AFTER)
        $evRaw     = $dict->getTriggerProperty($trigKey, 1401);
        $timRaw    = $dict->getTriggerProperty($trigKey, 1402);
        $optsRaw   = $dict->getTriggerProperty($trigKey, 1407);
        $prioRaw   = $dict->getTriggerProperty($trigKey, 1406);
        $enblRaw   = $dict->getTriggerProperty($trigKey, 505);
        $body      = $dict->getTriggerProperty($trigKey, 1404);

        $eventMask = strlen($evRaw)   >= 4 ? unpack('V', substr($evRaw,   0, 4))[1] : 0;
        $timing    = strlen($timRaw)  >= 4 ? unpack('V', substr($timRaw,  0, 4))[1] : 0;
        $options   = strlen($optsRaw) >= 4 ? unpack('V', substr($optsRaw, 0, 4))[1] : 3;
        $priority  = strlen($prioRaw) >= 4 ? unpack('V', substr($prioRaw, 0, 4))[1] : 1;
        $enabled   = strlen($enblRaw) >= 4 ? (unpack('V', substr($enblRaw, 0, 4))[1] ? 'Yes' : 'No') : 'Yes';

        $triggers[] = [
            'name'       => $trigName,
            'timing'     => timing_str($timing),
            'event'      => event_str($eventMask),
            'priority'   => (int)$priority,
            'enabled'    => $enabled,
            'body'       => rtrim($body),
            'options'    => $options,
            '_rawEvent'  => $eventMask,
            '_rawTiming' => $timing,
        ];
    }

    $conn->close();
    usort($triggers, fn($a, $b) => strcmp($a['name'], $b['name']));
} catch (Throwable $e) {
    api_exception(500, $e);
}

echo json_encode(
    ['triggers' => $triggers],
    JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES | JSON_INVALID_UTF8_SUBSTITUTE
);
