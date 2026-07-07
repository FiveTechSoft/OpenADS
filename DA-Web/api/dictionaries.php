<?php
/**
 * api/dictionaries.php — CRUD for stored data dictionary definitions
 * GET             → list all
 * POST action=add → add a DD
 * POST action=remove → remove a DD by name
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';

$configFile = __DIR__ . '/../config/dictionaries.json';

function loadDicts(string $file): array {
    if (!file_exists($file)) return [];
    $raw = file_get_contents($file);
    return json_decode($raw, true) ?? [];
}

function saveDicts(string $file, array $dicts): void {
    file_put_contents($file, json_encode(array_values($dicts), JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES));
}

$method = $_SERVER['REQUEST_METHOD'];

if ($method === 'GET') {
    echo json_encode(loadDicts($configFile));
    exit;
}

if ($method === 'POST') {
    api_require_session();
    $body = json_decode(file_get_contents('php://input'), true) ?? [];
    $action = $body['action'] ?? '';

    if ($action === 'add') {
        $name      = trim($body['name']      ?? '');
        $path      = trim($body['path']      ?? '');
        $username  = trim($body['username']  ?? '');
        $connType  = strtolower(trim($body['connType']  ?? 'local'));
        $entryType = trim($body['entryType'] ?? 'dd');
        $host      = trim($body['host'] ?? '');
        $portRaw   = $body['port'] ?? '';
        $port      = ($portRaw !== '' && $portRaw !== null && ctype_digit((string)$portRaw)) ? (int)$portRaw : null;
        if ($name === '' || $path === '') {
            api_error(400, 'name and path are required');
        }
        api_validate_identifier($name, 'dictionary name');
        if (!in_array($connType,  ['local', 'remote'], true)) $connType  = 'local';
        if (!in_array($entryType, ['dd', 'free'],       true)) $entryType = 'dd';
        $dicts = loadDicts($configFile);
        foreach ($dicts as $d) {
            if ($d['name'] === $name) {
                http_response_code(409);
                echo json_encode(['error' => "Dictionary '$name' already exists"]);
                exit;
            }
        }
        $entry = ['name' => $name, 'path' => $path, 'username' => $username,
                   'connType' => $connType, 'entryType' => $entryType];
        if ($host !== '') $entry['host'] = $host;
        if ($port !== null) $entry['port'] = $port;
        $dicts[] = $entry;
        saveDicts($configFile, $dicts);
        echo json_encode(['ok' => true]);
        exit;
    }

    if ($action === 'update') {
        $name      = trim($body['name']      ?? '');
        $path      = trim($body['path']      ?? '');
        $username  = trim($body['username']  ?? '');
        $connType  = strtolower(trim($body['connType']  ?? 'local'));
        $host      = trim($body['host'] ?? '');
        $portRaw   = $body['port'] ?? '';
        $port      = ($portRaw !== '' && $portRaw !== null && ctype_digit((string)$portRaw)) ? (int)$portRaw : null;
        if ($name === '' || $path === '') {
            api_error(400, 'name and path are required');
        }
        api_validate_identifier($name, 'dictionary name');
        if (!in_array($connType, ['local', 'remote'], true)) $connType = 'local';
        $dicts = loadDicts($configFile);
        $found = false;
        foreach ($dicts as &$d) {
            if ($d['name'] === $name) {
                $d['path']     = $path;
                $d['username'] = $username;
                $d['connType'] = $connType;
                if ($host !== '') { $d['host'] = $host; } else { unset($d['host']); }
                if ($port !== null) { $d['port'] = $port; } else { unset($d['port']); }
                $found = true;
                break;
            }
        }
        unset($d);
        if (!$found) {
            http_response_code(404);
            echo json_encode(['error' => "Dictionary '$name' not found"]);
            exit;
        }
        saveDicts($configFile, $dicts);
        echo json_encode(['ok' => true]);
        exit;
    }

    if ($action === 'remove') {
        $name = trim($body['name'] ?? '');
        if ($name !== '') {
            api_validate_identifier($name, 'dictionary name');
        }
        if ($name === '') {
            api_error(400, 'name is required');
        }
        api_validate_identifier($name, 'dictionary name');
        $dicts = loadDicts($configFile);
        $filtered = array_filter($dicts, fn($d) => $d['name'] !== $name);
        saveDicts($configFile, $filtered);
        echo json_encode(['ok' => true]);
        exit;
    }

    api_error(400, 'unknown action');
}

http_response_code(405);
echo json_encode(['error' => 'method not allowed']);
