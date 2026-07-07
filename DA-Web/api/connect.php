<?php
/**
 * api/connect.php — open / close a DD connection
 * POST action=connect  → credentials stored in session, connection tested
 * POST action=disconnect → session credentials removed
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';

$configFile = __DIR__ . '/../config/dictionaries.json';

/**
 * Remember the connType/host/port that just worked, so the next Connect
 * dialog for this DD is prefilled with it instead of the bare defaults.
 * host/port are stored only when non-empty — a local connect (or a remote
 * one that used the plain default) leaves them out of the JSON entirely.
 */
function persistSuccessfulConnMeta(string $file, string $name, string $connType, string $host, ?int $port): void {
    if ($name === '' || !file_exists($file)) return;
    $dicts = json_decode(file_get_contents($file), true);
    if (!is_array($dicts)) return;

    $changed = false;
    foreach ($dicts as &$d) {
        if (($d['name'] ?? '') !== $name) continue;
        $d['connType'] = $connType;
        if ($host !== '') {
            $d['host'] = $host;
        } else {
            unset($d['host']);
        }
        if ($port !== null) {
            $d['port'] = $port;
        } else {
            unset($d['port']);
        }
        $changed = true;
        break;
    }
    unset($d);

    if ($changed) {
        file_put_contents($file, json_encode(array_values($dicts), JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES));
    }
}

if (!extension_loaded('openads')) {
    http_response_code(500);
    echo json_encode(['error' => "php_openads extension not loaded (check extension=php_openads in php.ini)"]);
    exit;
}

$body = json_decode(file_get_contents('php://input'), true) ?? [];
$action = $body['action'] ?? '';

if ($action === 'connect') {
    $name     = trim($body['name']     ?? '');
    $path     = trim($body['path']     ?? '');
    $username = trim($body['username'] ?? '');
    $password = trim($body['password'] ?? '');
    $connType = strtolower(trim($body['connType'] ?? 'local'));
    if (!in_array($connType, ['local', 'remote'], true)) $connType = 'local';
    $host     = trim($body['host'] ?? '');
    $portRaw  = $body['port'] ?? '';
    $port     = ($portRaw !== '' && $portRaw !== null && ctype_digit((string)$portRaw)) ? (int)$portRaw : null;

    if ($name === '' || $path === '') {
        http_response_code(400);
        echo json_encode(['error' => 'name and path are required']);
        exit;
    }

    try {
        $opts = api_ads_connect_opts([
            'path' => $path,
            'username' => $username,
            'password' => $password,
            'connType' => $connType,
            'host' => $host,
            'port' => $port,
        ]);
        $conn = AdsConnection::connect($opts);
        $conn->close();
    } catch (AdsException $e) {
        $ext = strtolower(pathinfo($path, PATHINFO_EXTENSION));
        $needs_import = $e->getCode() === 5174 ||
            ($e->getCode() === 5103 && $ext === 'add');
        api_exception(401, $e, $needs_import ? ['path' => $path] : []);
    }

    // Remember exactly what connected (opts may hold a smart-parsed host/port
    // even if the caller didn't supply one explicitly) so short-lived backend
    // connections opened later in this session reuse the same target.
    $effectiveHost = $host;
    $effectivePort = $port;
    if ($effectiveHost === '' && preg_match('#^(?:tcp|tls)://([^:/]+):(\d+)/#i', $opts['path'] ?? '', $m)) {
        $effectiveHost = $m[1];
        $effectivePort = (int)$m[2];
    }

    if (!isset($_SESSION['connections'])) {
        $_SESSION['connections'] = [];
    }
    $_SESSION['connections'][$name] = [
        'path'     => $opts['path'] ?? $path,
        'sourcePath' => $path,
        'username' => $username,
        'password' => $password,
        'connType' => $connType,
        'host'     => $effectiveHost,
        'port'     => $effectivePort,
    ];
    persistSuccessfulConnMeta($configFile, $name, $connType, $effectiveHost, $effectivePort);

    echo json_encode(['ok' => true]);
    exit;
}

if ($action === 'disconnect') {
    $name = trim($body['name'] ?? '');
    if (isset($_SESSION['connections'][$name])) {
        unset($_SESSION['connections'][$name]);
    }
    echo json_encode(['ok' => true]);
    exit;
}

http_response_code(400);
echo json_encode(['error' => 'unknown action']);
