<?php
/**
 * Smoke test for DataDictionary — no phpunit, run directly:
 *   php tests/dd_smoke.php
 * Requires OPENADS_ACE_LIB to point at ace64.dll.
 */
declare(strict_types=1);

require __DIR__ . '/../vendor/autoload.php';

use OpenADS\Connection;
use OpenADS\Ffi\AceTypes;

$lib = getenv('OPENADS_ACE_LIB');
if (!$lib || !is_file($lib)) {
    fwrite(STDERR, "Set OPENADS_ACE_LIB to the ace64.dll path.\n");
    exit(1);
}

$dir = sys_get_temp_dir() . '/openads_dd_smoke_' . bin2hex(random_bytes(4));
mkdir($dir);
$ddPath = $dir . '/test.add';

function pass(string $msg): void { echo "  PASS: $msg\n"; }
function fail(string $msg, Throwable $e): void {
    echo "  FAIL: $msg\n    " . $e->getMessage() . "\n";
    exit(1);
}

echo "DataDictionary smoke test\n";

// 1. Create dictionary
try {
    $conn = Connection::createDictionary($ddPath);
    pass("createDictionary");
} catch (Throwable $e) { fail("createDictionary", $e); }

$dd = $conn->dd();

// 2. Add table
try {
    $dd->addTable('ORDERS', 'orders.dbf');
    pass("addTable");
} catch (Throwable $e) { fail("addTable", $e); }

// 3. Get relative path
try {
    $rel = $dd->getTableRelativePath('ORDERS');
    assert($rel === 'orders.dbf', "expected 'orders.dbf', got '$rel'");
    pass("getTableRelativePath → $rel");
} catch (Throwable $e) { fail("getTableRelativePath", $e); }

// 4. Get absolute path
try {
    $abs = $dd->getTablePath('ORDERS');
    assert(str_contains($abs, 'orders.dbf'), "expected path containing 'orders.dbf', got '$abs'");
    pass("getTablePath → $abs");
} catch (Throwable $e) { fail("getTablePath", $e); }

// 5. Database property
try {
    $dd->setDatabaseProperty(AceTypes::ADS_DD_COMMENT, 'Smoke test DB');
    $val = $dd->getDatabaseProperty(AceTypes::ADS_DD_COMMENT);
    assert($val === 'Smoke test DB', "expected 'Smoke test DB', got '$val'");
    pass("setDatabaseProperty / getDatabaseProperty → $val");
} catch (Throwable $e) { fail("database property", $e); }

// 6. Create user
try {
    $dd->createUser('alice', 'secret123');
    pass("createUser alice");
} catch (Throwable $e) { fail("createUser", $e); }

// 7. Set/get password
try {
    $dd->setUserPassword('alice', 'newpass');
    try {
        $dd->getUserProperty('alice', AceTypes::ADS_DD_USER_PASSWORD);
        throw new RuntimeException('Password getter must reject disclosure');
    } catch (\OpenADS\Exception\OpenAdsException $e) {
        assert($e->aceCode() === 5138);
    }
    pass("setUserPassword / write-only password property");
} catch (Throwable $e) { fail("user password", $e); }

// 8. Group membership
try {
    $dd->addUserToGroup('alice', 'admins');
    $groups = $dd->getUserGroups('alice');
    assert($groups === 'admins', "expected 'admins', got '$groups'");
    pass("addUserToGroup / getUserGroups → $groups");
} catch (Throwable $e) { fail("group membership", $e); }

// 9. Remove from group
try {
    $dd->removeUserFromGroup('alice', 'admins');
    $groups = $dd->getUserGroups('alice');
    assert($groups === '', "expected '', got '$groups'");
    pass("removeUserFromGroup / getUserGroups → empty");
} catch (Throwable $e) { fail("removeUserFromGroup", $e); }

// 10. Table permissions
try {
    $dd->setTablePermission('ORDERS', 'alice', AceTypes::ADS_DD_TABLE_PERMISSION_READ);
    $lvl = $dd->getTablePermission('ORDERS', 'alice');
    assert($lvl === AceTypes::ADS_DD_TABLE_PERMISSION_READ, "expected ".AceTypes::ADS_DD_TABLE_PERMISSION_READ.", got $lvl");
    pass("setTablePermission / getTablePermission → $lvl");
} catch (Throwable $e) { fail("table permissions", $e); }

// 11. Create view
try {
    $dd->createView('ActiveOrders', 'SELECT * FROM orders WHERE status=1', 'Open orders');
    $sql  = $dd->getViewProperty('ActiveOrders', AceTypes::ADS_DD_VIEW_STMT);
    $cmt  = $dd->getViewProperty('ActiveOrders', AceTypes::ADS_DD_VIEW_COMMENT);
    assert($sql === 'SELECT * FROM orders WHERE status=1', "sql mismatch: $sql");
    assert($cmt === 'Open orders', "comment mismatch: $cmt");
    pass("createView / getViewProperty SQL='$sql'");
} catch (Throwable $e) { fail("createView", $e); }

// 12. Drop view
try {
    $dd->dropView('ActiveOrders');
    pass("dropView");
} catch (Throwable $e) { fail("dropView", $e); }

// 13. Create procedure
try {
    $dd->createProcedure('GetOrder', 'mylib.dll', 'GetOrderFn', 'orderId INT', 'result VARCHAR');
    $ctn = $dd->getProcProperty('GetOrder', AceTypes::ADS_DD_PROC_CONTAINER);
    $fn  = $dd->getProcProperty('GetOrder', AceTypes::ADS_DD_PROC_PROC_NAME);
    assert($ctn === 'mylib.dll', "container mismatch: $ctn");
    assert($fn  === 'GetOrderFn', "function mismatch: $fn");
    pass("createProcedure / getProcProperty container='$ctn' fn='$fn'");
} catch (Throwable $e) { fail("createProcedure", $e); }

// 14. Drop procedure
try {
    $dd->dropProcedure('GetOrder');
    pass("dropProcedure");
} catch (Throwable $e) { fail("dropProcedure", $e); }

// 15. Create trigger
try {
    $dd->createTrigger(
        'BeforeInsertOrder', 'ORDERS',
        0x01, // event bitmask
        'triggers.dll', 'OnBeforeInsert',
        10, true, 'Trigger comment'
    );
    $tbl = $dd->getTriggerProperty('BeforeInsertOrder', AceTypes::ADS_DD_TRIGGER_TABLE);
    $cmt = $dd->getTriggerProperty('BeforeInsertOrder', AceTypes::ADS_DD_TRIGGER_COMMENT);
    assert($tbl === 'ORDERS', "table mismatch: $tbl");
    assert($cmt === 'Trigger comment', "comment mismatch: $cmt");
    pass("createTrigger / getTriggerProperty table='$tbl'");
} catch (Throwable $e) { fail("createTrigger", $e); }

// 16. Drop trigger
try {
    $dd->dropTrigger('BeforeInsertOrder');
    pass("dropTrigger");
} catch (Throwable $e) { fail("dropTrigger", $e); }

// 17. Remove table
try {
    $dd->removeTable('ORDERS');
    pass("removeTable");
} catch (Throwable $e) { fail("removeTable", $e); }

// 18. Delete user
try {
    $dd->deleteUser('alice');
    pass("deleteUser");
} catch (Throwable $e) { fail("deleteUser", $e); }

$conn->close();

echo "\nAll checks passed.\n";
