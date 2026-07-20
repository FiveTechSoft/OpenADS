<?php
/**
 * api/save_meta.php — save field property changes back to the DD.
 *
 * POST { dd, table, rows: [{ Field, Required, Default }] }
 *
 * Writes ADS_DD_FIELD_CAN_NULL (301, UNSIGNED16: 0 = required) and
 * ADS_DD_FIELD_DEFAULT_VALUE (300) for each supplied row via
 * AdsDictionary::setFieldProperty — SAP ABI numbering.
 */
header('Content-Type: application/json');
session_start();
require_once __DIR__ . '/common.php';

$body   = json_decode(file_get_contents('php://input'), true) ?? [];
$ddName = trim($body['dd']    ?? '');
$table  = trim($body['table'] ?? '');
$rows   = $body['rows'] ?? [];

if ($table === '' || !is_array($rows)) {
    api_error(400, 'invalid parameters');
}
api_validate_identifier($table, 'table name');

$c = api_require_connection($ddName);
$opts = api_ads_connect_opts($c);

try {
    $conn = AdsConnection::connect($opts);
    $dict = AdsDictionary::fromConnection($conn);
    $saved = 0;
    $errors = [];

    foreach ($rows as $row) {
        $field = trim($row['Field'] ?? '');
        if ($field === '') continue;
        if (!preg_match('/^[A-Za-z_][A-Za-z0-9_]*$/', $field)) continue;

        // ADS_DD_FIELD_CAN_NULL = 301 (UNSIGNED16; 0 = NOT NULL = required)
        if (isset($row['Required'])) {
            $required = (strcasecmp(trim($row['Required']), 'True') === 0);
            try {
                $dict->setFieldProperty($table, $field, 301,
                                        pack('v', $required ? 0 : 1));
                $saved++;
            } catch (Throwable $e) {
                $errors[] = "$field Required: " . $e->getMessage();
            }
        }

        // ADS_DD_FIELD_DEFAULT_VALUE = 300
        if (array_key_exists('Default', $row)) {
            $val = trim($row['Default'] ?? '');
            try {
                $dict->setFieldProperty($table, $field, 300, $val);
                $saved++;
            } catch (Throwable $e) {
                $errors[] = "$field Default: " . $e->getMessage();
            }
        }
    }

    $conn->close();

    if (!empty($errors)) {
        echo json_encode(['saved' => $saved, 'errors' => $errors]);
    } else {
        echo json_encode(['saved' => $saved]);
    }
} catch (Throwable $e) {
    api_exception(500, $e);
}
