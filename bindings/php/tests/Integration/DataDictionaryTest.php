<?php
declare(strict_types=1);

namespace OpenADS\Tests\Integration;

use OpenADS\Connection;
use OpenADS\Ffi\AceTypes;

final class DataDictionaryTest extends IntegrationTestCase
{
    private string $ddPath;

    protected function setUp(): void
    {
        parent::setUp();
        $dir          = $this->tempDataDir();
        $this->ddPath = $dir . '/test.add';
    }

    public function testCreateDictionary(): void
    {
        $conn = Connection::createDictionary($this->ddPath);
        self::assertTrue($conn->isOpen());
        self::assertFileExists($this->ddPath);
        $conn->close();
    }

    public function testAddAndQueryTable(): void
    {
        // Prepare a real table file in the same dir so the DD path resolves.
        $dir     = dirname($this->ddPath);
        $tblPath = $dir . '/orders.dbf';
        $conn    = Connection::createDictionary($this->ddPath);
        $dd      = $conn->dd();

        // Add a table alias pointing at a relative path.
        $dd->addTable('ORDERS', 'orders.dbf');
        $rel = $dd->getTableRelativePath('ORDERS');
        self::assertSame('orders.dbf', $rel);

        // Absolute path should include the data dir.
        $abs = $dd->getTablePath('ORDERS');
        self::assertStringContainsString('orders.dbf', $abs);

        $conn->close();
    }

    public function testRemoveTable(): void
    {
        $conn = Connection::createDictionary($this->ddPath);
        $dd   = $conn->dd();

        $dd->addTable('ORDERS', 'orders.dbf');
        $dd->removeTable('ORDERS');

        // After removal, getTableProperty should throw (table not found).
        $this->expectException(\OpenADS\Exception\OpenAdsException::class);
        $dd->getTableRelativePath('ORDERS');

        $conn->close();
    }

    public function testUserManagement(): void
    {
        $conn = Connection::createDictionary($this->ddPath);
        $dd   = $conn->dd();

        $dd->createUser('alice', 'secret123');
        $dd->setUserPassword('alice', 'newpass');
        // Passwords are write-only and must never be exposed by getters.
        try {
            $dd->getUserProperty('alice', AceTypes::ADS_DD_USER_PASSWORD);
            self::fail('Password getter must reject disclosure');
        } catch (\OpenADS\Exception\OpenAdsException $e) {
            self::assertSame(5138, $e->aceCode());
        }

        $dd->createUser('bob');
        $dd->addUserToGroup('bob', 'admins');
        $groups = $dd->getUserGroups('bob');
        self::assertSame('admins', $groups);

        $dd->removeUserFromGroup('bob', 'admins');
        $groups = $dd->getUserGroups('bob');
        self::assertSame('', $groups);

        $dd->deleteUser('alice');
        $dd->deleteUser('bob');

        $conn->close();
    }

    public function testTablePermissions(): void
    {
        $conn = Connection::createDictionary($this->ddPath);
        $dd   = $conn->dd();

        $dd->addTable('ORDERS', 'orders.dbf');
        $dd->createUser('alice');

        $dd->setTablePermission('ORDERS', 'alice', AceTypes::ADS_DD_TABLE_PERMISSION_READ);
        $level = $dd->getTablePermission('ORDERS', 'alice');
        self::assertSame(AceTypes::ADS_DD_TABLE_PERMISSION_READ, $level);

        $conn->close();
    }

    public function testViewManagement(): void
    {
        $conn = Connection::createDictionary($this->ddPath);
        $dd   = $conn->dd();

        $dd->createView('ActiveOrders', 'SELECT * FROM orders WHERE status=1', 'Open orders');
        $sql     = $dd->getViewProperty('ActiveOrders', AceTypes::ADS_DD_VIEW_STMT);
        $comment = $dd->getViewProperty('ActiveOrders', AceTypes::ADS_DD_VIEW_COMMENT);
        self::assertSame('SELECT * FROM orders WHERE status=1', $sql);
        self::assertSame('Open orders', $comment);

        $dd->dropView('ActiveOrders');
        $conn->close();
    }

    public function testProcedureManagement(): void
    {
        $conn = Connection::createDictionary($this->ddPath);
        $dd   = $conn->dd();

        $dd->createProcedure('GetOrder', 'mylib.dll', 'GetOrderFn', 'orderId INTEGER', 'result VARCHAR');
        $container = $dd->getProcProperty('GetOrder', AceTypes::ADS_DD_PROC_CONTAINER);
        $fn        = $dd->getProcProperty('GetOrder', AceTypes::ADS_DD_PROC_PROC_NAME);
        self::assertSame('mylib.dll', $container);
        self::assertSame('GetOrderFn', $fn);

        $dd->dropProcedure('GetOrder');
        $conn->close();
    }

    public function testDatabaseProperty(): void
    {
        $conn = Connection::createDictionary($this->ddPath);
        $dd   = $conn->dd();

        $dd->setDatabaseProperty(AceTypes::ADS_DD_COMMENT, 'Test database');
        $val = $dd->getDatabaseProperty(AceTypes::ADS_DD_COMMENT);
        self::assertSame('Test database', $val);

        $conn->close();
    }
}
