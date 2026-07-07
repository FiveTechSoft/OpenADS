<?php
/**
 * api/create_dd.php — create a new data dictionary on disk
 * POST { name, path, password, connType }
 *
 * Calls AdsDDCreate(path, encrypt, password) via the php_openads extension,
 * then registers the new DD in config/dictionaries.json.
 *
 * NOTE: ads_dd_create() must be exposed as a standalone PHP function in the
 * php_openads extension (wrapping AdsDDCreate from openace64.dll).
 * If it is not yet available this endpoint returns HTTP 501 with a clear message.
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';

api_require_session();

$body     = json_decode(file_get_contents('php://input'), true) ?? [];
$name     = trim($body['name']     ?? '');
$path     = trim($body['path']     ?? '');
$password = $body['password']      ?? '';
$connType = trim($body['connType'] ?? 'local');
$host     = trim($body['host'] ?? '');
$portRaw  = $body['port'] ?? '';
$port     = ($portRaw !== '' && $portRaw !== null && ctype_digit((string)$portRaw)) ? (int)$portRaw : null;

if ($name === '' || $path === '') {
    api_error(400, 'name and path are required');
}
api_validate_identifier($name, 'dictionary name');
api_reject_unsafe_path($path, 'path');

if (!function_exists('ads_dd_create')) {
    http_response_code(501);
    echo json_encode([
        'error' => 'ads_dd_create() is not yet exposed in this build of php_openads. '
                 . 'Add a wrapper for AdsDDCreate(pucDictionary, bEncrypt, pucAdminPassword) '
                 . 'to the extension and recompile.',
    ]);
    exit;
}

try {
    $encrypt = ($password !== '') ? 1 : 0;
    ads_dd_create($path, $encrypt, $password);
} catch (AdsException $e) {
    api_exception(500, $e);
}

// Register in dictionaries.json
$configFile = __DIR__ . '/../config/dictionaries.json';
$raw    = file_exists($configFile) ? file_get_contents($configFile) : '[]';
$dicts  = json_decode($raw, true) ?? [];

foreach ($dicts as $d) {
    if ($d['name'] === $name) {
        // Already registered — just return OK (DD was just created on disk)
        echo json_encode(['ok' => true]);
        exit;
    }
}

$entry = [
    'name'      => $name,
    'path'      => $path,
    'username'  => 'AdsSysAdmin',
    'connType'  => in_array($connType, ['local', 'remote'], true) ? $connType : 'local',
    'entryType' => 'dd',
];
if ($host !== '') $entry['host'] = $host;
if ($port !== null) $entry['port'] = $port;
$dicts[] = $entry;
file_put_contents($configFile,
    json_encode(array_values($dicts), JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES));

echo json_encode(['ok' => true]);
