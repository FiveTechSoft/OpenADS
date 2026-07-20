<?php
declare(strict_types=1);

namespace OpenADS;

use FFI;
use OpenADS\Exception\OpenAdsException;
use OpenADS\Ffi\AceLibrary;
use OpenADS\Ffi\AceTypes;

/**
 * Wraps AdsDD* functions for Data Dictionary management.
 * Obtain via Connection::dd().
 */
final class DataDictionary
{
    private AceLibrary $lib;
    private FFI        $ffi;
    private int        $handle;

    public function __construct(private Connection $conn)
    {
        $this->lib    = $conn->library();
        $this->ffi    = $this->lib->ffi();
        $this->handle = $conn->handle();
    }

    // -------------------------------------------------------------------------
    // Tables
    // -------------------------------------------------------------------------

    /** Register a table file under $alias in the DD. */
    public function addTable(string $alias, string $path): void
    {
        $rc = $this->ffi->AdsDDAddTable(
            $this->handle,
            Connection::cstr($this->ffi, $alias),
            Connection::cstr($this->ffi, $path),
            0, 0, null, null
        );
        $this->checkOk($rc, "AdsDDAddTable($alias)");
    }

    /** Remove a table alias from the DD (does not delete the file). */
    public function removeTable(string $alias): void
    {
        $rc = $this->ffi->AdsDDRemoveTable(
            $this->handle,
            Connection::cstr($this->ffi, $alias),
            0
        );
        $this->checkOk($rc, "AdsDDRemoveTable($alias)");
    }

    /**
     * Get a table property.
     * Returns a string; for uint16/uint32 properties decode with unpack().
     */
    public function getTableProperty(string $alias, int $prop): string
    {
        return $this->getPropStr(
            fn($buf, $len) => $this->ffi->AdsDDGetTableProperty(
                $this->handle,
                Connection::cstr($this->ffi, $alias),
                $prop, $buf, $len
            ),
            "AdsDDGetTableProperty($alias, $prop)"
        );
    }

    /** Convenience: absolute path of table file. */
    public function getTablePath(string $alias): string
    {
        return $this->getTableProperty($alias, AceTypes::ADS_DD_TABLE_PATH);
    }

    /** Convenience: relative path stored in DD. */
    public function getTableRelativePath(string $alias): string
    {
        return $this->getTableProperty($alias, AceTypes::ADS_DD_TABLE_RELATIVE_PATH);
    }

    /** Effective permission level (0-4) for a user on a table. */
    public function getTablePermission(string $alias, string $user): int
    {
        $level = $this->ffi->new('UNSIGNED32');
        $rc    = $this->ffi->AdsDDGetUserTableRights(
            $this->handle,
            Connection::cstr($this->ffi, $alias),
            Connection::cstr($this->ffi, $user),
            FFI::addr($level)
        );
        $this->checkOk($rc, "AdsDDGetUserTableRights($alias, $user)");
        return (int) $level->cdata;
    }

    /** Set permission level (ADS_DD_TABLE_PERMISSION_* constants) for user on table. */
    public function setTablePermission(string $alias, string $user, int $level): void
    {
        $rc = $this->ffi->AdsDDSetUserTableRights(
            $this->handle,
            Connection::cstr($this->ffi, $alias),
            Connection::cstr($this->ffi, $user),
            $level
        );
        $this->checkOk($rc, "AdsDDSetUserTableRights($alias, $user)");
    }

    // -------------------------------------------------------------------------
    // Fields
    // -------------------------------------------------------------------------

    /**
     * Get a field property (ADS_DD_FIELD_* constants, SAP ABI numbering).
     * Structural properties (TYPE, LENGTH, DECIMAL, DEFINITION) are read
     * live from the table file. Returns raw bytes; UNSIGNED16 properties
     * (CAN_NULL, TYPE, LENGTH, DECIMAL) need unpack('v', ...).
     * DEFAULT_VALUE / MIN / MAX throw when the property is unset
     * (AE_PROPERTY_NOT_SET, 5138).
     */
    public function getFieldProperty(string $alias, string $field, int $prop): string
    {
        return $this->getPropStr(
            fn($buf, $len) => $this->ffi->AdsDDGetFieldProperty(
                $this->handle,
                Connection::cstr($this->ffi, $alias),
                Connection::cstr($this->ffi, $field),
                $prop, $buf, $len
            ),
            "AdsDDGetFieldProperty($alias.$field, $prop)"
        );
    }

    /**
     * Set a stored field property (DEFAULT_VALUE, CAN_NULL, MIN/MAX,
     * VALIDATION_MSG, OA_FIELD_VALIDATION_RULE, or the generic
     * ADS_DD_COMMENT). For CAN_NULL pass pack('v', 0|1) — the ABI takes
     * an UNSIGNED16 (text T/F is tolerated by OpenADS as an extension).
     */
    public function setFieldProperty(string $alias, string $field, int $prop, string $value): void
    {
        $buf = Connection::cstr($this->ffi, $value);
        $rc  = $this->ffi->AdsDDSetFieldProperty(
            $this->handle,
            Connection::cstr($this->ffi, $alias),
            Connection::cstr($this->ffi, $field),
            $prop, $buf, strlen($value)
        );
        $this->checkOk($rc, "AdsDDSetFieldProperty($alias.$field, $prop)");
    }

    // -------------------------------------------------------------------------
    // Indexes
    // -------------------------------------------------------------------------

    /** Get an index property (ADS_DD_INDEX_* constants). */
    public function getIndexProperty(string $alias, string $index, int $prop): string
    {
        return $this->getPropStr(
            fn($buf, $len) => $this->ffi->AdsDDGetIndexProperty(
                $this->handle,
                Connection::cstr($this->ffi, $alias),
                Connection::cstr($this->ffi, $index),
                $prop, $buf, $len
            ),
            "AdsDDGetIndexProperty($alias, $index, $prop)"
        );
    }

    /** Register an external index file for a table in the DD. */
    public function addIndexFile(string $alias, string $indexPath, string $comment = ''): void
    {
        $rc = $this->ffi->AdsDDAddIndexFile(
            $this->handle,
            Connection::cstr($this->ffi, $alias),
            Connection::cstr($this->ffi, $indexPath),
            Connection::cstr($this->ffi, $comment)
        );
        $this->checkOk($rc, "AdsDDAddIndexFile($alias)");
    }

    /** Remove an index file association from the DD. */
    public function removeIndexFile(string $alias, string $indexPath): void
    {
        $rc = $this->ffi->AdsDDRemoveIndexFile(
            $this->handle,
            Connection::cstr($this->ffi, $alias),
            Connection::cstr($this->ffi, $indexPath),
            0
        );
        $this->checkOk($rc, "AdsDDRemoveIndexFile($alias)");
    }

    // -------------------------------------------------------------------------
    // Users
    // -------------------------------------------------------------------------

    /**
     * Create a user. $group is the initial group (nullable).
     * $password and $description are optional.
     */
    public function createUser(
        string  $user,
        ?string $password    = null,
        ?string $group       = null,
        ?string $description = null
    ): void {
        $rc = $this->ffi->AdsDDCreateUser(
            $this->handle,
            $group       !== null ? Connection::cstr($this->ffi, $group)       : null,
            Connection::cstr($this->ffi, $user),
            $password    !== null ? Connection::cstr($this->ffi, $password)    : null,
            $description !== null ? Connection::cstr($this->ffi, $description) : null
        );
        $this->checkOk($rc, "AdsDDCreateUser($user)");
    }

    /** Delete a user from the DD. */
    public function deleteUser(string $user): void
    {
        $rc = $this->ffi->AdsDDDeleteUser(
            $this->handle,
            Connection::cstr($this->ffi, $user)
        );
        $this->checkOk($rc, "AdsDDDeleteUser($user)");
    }

    /** Get a user property (ADS_DD_USER_* constants). Returns raw bytes. */
    public function getUserProperty(string $user, int $prop): string
    {
        return $this->getPropStr(
            fn($buf, $len) => $this->ffi->AdsDDGetUserProperty(
                $this->handle,
                Connection::cstr($this->ffi, $user),
                $prop, $buf, $len
            ),
            "AdsDDGetUserProperty($user, $prop)"
        );
    }

    /** Set a user property (ADS_DD_USER_PASSWORD, ADS_DD_USER_GROUP_MEMBERSHIP, etc.). */
    public function setUserProperty(string $user, int $prop, string $value): void
    {
        $buf = Connection::cstr($this->ffi, $value);
        $rc  = $this->ffi->AdsDDSetUserProperty(
            $this->handle,
            Connection::cstr($this->ffi, $user),
            $prop, $buf, strlen($value)
        );
        $this->checkOk($rc, "AdsDDSetUserProperty($user, $prop)");
    }

    /** Convenience: set user password. */
    public function setUserPassword(string $user, string $password): void
    {
        $this->setUserProperty($user, AceTypes::ADS_DD_USER_PASSWORD, $password);
    }

    /**
     * Convenience: get comma-separated group membership string for $user.
     * Returns '' if not a member of any group.
     */
    public function getUserGroups(string $user): string
    {
        return $this->getUserProperty($user, AceTypes::ADS_DD_USER_GROUP_MEMBERSHIP);
    }

    /** Add $user to $group. */
    public function addUserToGroup(string $user, string $group): void
    {
        $rc = $this->ffi->AdsDDAddUserToGroup(
            $this->handle,
            Connection::cstr($this->ffi, $group),
            Connection::cstr($this->ffi, $user)
        );
        $this->checkOk($rc, "AdsDDAddUserToGroup($user -> $group)");
    }

    /** Remove $user from $group. */
    public function removeUserFromGroup(string $user, string $group): void
    {
        $rc = $this->ffi->AdsDDRemoveUserFromGroup(
            $this->handle,
            Connection::cstr($this->ffi, $group),
            Connection::cstr($this->ffi, $user)
        );
        $this->checkOk($rc, "AdsDDRemoveUserFromGroup($user from $group)");
    }

    // -------------------------------------------------------------------------
    // Database properties
    // -------------------------------------------------------------------------

    /** Get a database property (ADS_DD_COMMENT, ADS_DD_LOG_IN_REQUIRED, etc.). */
    public function getDatabaseProperty(int $prop): string
    {
        return $this->getPropStr(
            fn($buf, $len) => $this->ffi->AdsDDGetDatabaseProperty(
                $this->handle, $prop, $buf, $len
            ),
            "AdsDDGetDatabaseProperty($prop)"
        );
    }

    /** Set a database property. */
    public function setDatabaseProperty(int $prop, string $value): void
    {
        $buf = Connection::cstr($this->ffi, $value);
        $rc  = $this->ffi->AdsDDSetDatabaseProperty(
            $this->handle, $prop, $buf, strlen($value)
        );
        $this->checkOk($rc, "AdsDDSetDatabaseProperty($prop)");
    }

    // -------------------------------------------------------------------------
    // Links
    // -------------------------------------------------------------------------

    /** Create a linked dictionary alias. */
    public function createLink(
        string  $alias,
        string  $path,
        ?string $user = null,
        ?string $pwd  = null
    ): void {
        $rc = $this->ffi->AdsDDCreateLink(
            $this->handle,
            Connection::cstr($this->ffi, $alias),
            Connection::cstr($this->ffi, $path),
            $user !== null ? Connection::cstr($this->ffi, $user) : null,
            $pwd  !== null ? Connection::cstr($this->ffi, $pwd)  : null,
            0
        );
        $this->checkOk($rc, "AdsDDCreateLink($alias)");
    }

    /** Drop a linked dictionary alias. */
    public function dropLink(string $alias): void
    {
        $rc = $this->ffi->AdsDDDropLink(
            $this->handle,
            Connection::cstr($this->ffi, $alias),
            0
        );
        $this->checkOk($rc, "AdsDDDropLink($alias)");
    }

    /** Modify a linked dictionary alias. */
    public function modifyLink(
        string  $alias,
        string  $path,
        ?string $user = null,
        ?string $pwd  = null
    ): void {
        $rc = $this->ffi->AdsDDModifyLink(
            $this->handle,
            Connection::cstr($this->ffi, $alias),
            Connection::cstr($this->ffi, $path),
            $user !== null ? Connection::cstr($this->ffi, $user) : null,
            $pwd  !== null ? Connection::cstr($this->ffi, $pwd)  : null,
            0
        );
        $this->checkOk($rc, "AdsDDModifyLink($alias)");
    }

    // -------------------------------------------------------------------------
    // Referential integrity
    // -------------------------------------------------------------------------

    /**
     * Create an RI rule.
     * $updateRule / $deleteRule: ADS_DD_RI_CASCADE|RESTRICT|SETNULL|SETDEFAULT.
     */
    public function createRefIntegrity(
        string $name,
        string $failTable,
        string $parent,
        string $parentKey,
        string $child,
        string $childKey,
        int    $updateRule = AceTypes::ADS_DD_RI_RESTRICT,
        int    $deleteRule = AceTypes::ADS_DD_RI_RESTRICT
    ): void {
        $rc = $this->ffi->AdsDDCreateRefIntegrity(
            $this->handle,
            Connection::cstr($this->ffi, $name),
            Connection::cstr($this->ffi, $failTable),
            Connection::cstr($this->ffi, $parent),
            Connection::cstr($this->ffi, $parentKey),
            Connection::cstr($this->ffi, $child),
            Connection::cstr($this->ffi, $childKey),
            $updateRule,
            $deleteRule
        );
        $this->checkOk($rc, "AdsDDCreateRefIntegrity($name)");
    }

    /** Remove an RI rule by name. */
    public function removeRefIntegrity(string $name): void
    {
        $rc = $this->ffi->AdsDDRemoveRefIntegrity(
            $this->handle,
            Connection::cstr($this->ffi, $name)
        );
        $this->checkOk($rc, "AdsDDRemoveRefIntegrity($name)");
    }

    // -------------------------------------------------------------------------
    // Triggers
    // -------------------------------------------------------------------------

    /**
     * Create a trigger.
     * $event: bitmask of ADS_BEFORE/AFTER_INSERT/UPDATE/DELETE event constants.
     * C signature: (hConn, name, table, ulType, ulOptions, container, procedure, priority)
     * Enabled/comment must be set via setTriggerProperty after creation.
     */
    public function createTrigger(
        string $name,
        string $table,
        int    $event,
        string $container,
        string $function,
        int    $priority = 0,
        bool   $enabled  = true,
        string $comment  = ''
    ): void {
        $rc = $this->ffi->AdsDDCreateTrigger(
            $this->handle,
            Connection::cstr($this->ffi, $name),
            Connection::cstr($this->ffi, $table),
            $event,
            0,  // ulOptions — unused
            Connection::cstr($this->ffi, $container),
            Connection::cstr($this->ffi, $function),
            $priority
        );
        $this->checkOk($rc, "AdsDDCreateTrigger($name)");
        if (!$enabled) {
            $this->setTriggerProperty($name, AceTypes::ADS_DD_TRIGGER_ENABLED, '0');
        }
        if ($comment !== '') {
            $this->setTriggerProperty($name, AceTypes::ADS_DD_TRIGGER_COMMENT, $comment);
        }
    }

    /** Drop a trigger by name. */
    public function dropTrigger(string $name): void
    {
        $rc = $this->ffi->AdsDDDropTrigger(
            $this->handle,
            Connection::cstr($this->ffi, $name)
        );
        $this->checkOk($rc, "AdsDDDropTrigger($name)");
    }

    /** Get a trigger property (ADS_DD_TRIGGER_* constants). */
    public function getTriggerProperty(string $name, int $prop): string
    {
        return $this->getPropStr(
            fn($buf, $len) => $this->ffi->AdsDDGetTriggerProperty(
                $this->handle,
                Connection::cstr($this->ffi, $name),
                $prop, $buf, $len
            ),
            "AdsDDGetTriggerProperty($name, $prop)"
        );
    }

    /** Set a trigger property. */
    public function setTriggerProperty(string $name, int $prop, string $value): void
    {
        $buf = Connection::cstr($this->ffi, $value);
        $rc  = $this->ffi->AdsDDSetTriggerProperty(
            $this->handle,
            Connection::cstr($this->ffi, $name),
            $prop, $buf, strlen($value)
        );
        $this->checkOk($rc, "AdsDDSetTriggerProperty($name, $prop)");
    }

    // -------------------------------------------------------------------------
    // Stored procedures
    // -------------------------------------------------------------------------

    /** Create a stored procedure entry in the DD. */
    public function createProcedure(
        string $name,
        string $container,
        string $function,
        string $inputParams  = '',
        string $outputParams = '',
        string $comment      = ''
    ): void {
        // C signature: (hConn, name, container, procedure, input, output) — no comment param
        $rc = $this->ffi->AdsDDCreateProcedure(
            $this->handle,
            Connection::cstr($this->ffi, $name),
            Connection::cstr($this->ffi, $container),
            Connection::cstr($this->ffi, $function),
            Connection::cstr($this->ffi, $inputParams),
            Connection::cstr($this->ffi, $outputParams)
        );
        $this->checkOk($rc, "AdsDDCreateProcedure($name)");
        if ($comment !== '') {
            $this->setProcProperty($name, AceTypes::ADS_DD_PROC_COMMENT, $comment);
        }
    }

    /** Drop a stored procedure entry. */
    public function dropProcedure(string $name): void
    {
        $rc = $this->ffi->AdsDDDropProcedure(
            $this->handle,
            Connection::cstr($this->ffi, $name)
        );
        $this->checkOk($rc, "AdsDDDropProcedure($name)");
    }

    /** Get a procedure property (ADS_DD_PROC_* constants). */
    public function getProcProperty(string $name, int $prop): string
    {
        return $this->getPropStr(
            fn($buf, $len) => $this->ffi->AdsDDGetProcProperty(
                $this->handle,
                Connection::cstr($this->ffi, $name),
                $prop, $buf, $len
            ),
            "AdsDDGetProcProperty($name, $prop)"
        );
    }

    /** Set a procedure property. */
    public function setProcProperty(string $name, int $prop, string $value): void
    {
        $buf = Connection::cstr($this->ffi, $value);
        $rc  = $this->ffi->AdsDDSetProcProperty(
            $this->handle,
            Connection::cstr($this->ffi, $name),
            $prop, $buf, strlen($value)
        );
        $this->checkOk($rc, "AdsDDSetProcProperty($name, $prop)");
    }

    // -------------------------------------------------------------------------
    // Views
    // -------------------------------------------------------------------------

    /** Create a view in the DD. */
    public function createView(string $name, string $sql, string $comment = ''): void
    {
        // C signature: (hConn, name, comments, sql) — comment comes before sql
        $rc = $this->ffi->AdsDDCreateView(
            $this->handle,
            Connection::cstr($this->ffi, $name),
            Connection::cstr($this->ffi, $comment),
            Connection::cstr($this->ffi, $sql)
        );
        $this->checkOk($rc, "AdsDDCreateView($name)");
    }

    /** Drop a view by name. */
    public function dropView(string $name): void
    {
        $rc = $this->ffi->AdsDDDropView(
            $this->handle,
            Connection::cstr($this->ffi, $name)
        );
        $this->checkOk($rc, "AdsDDDropView($name)");
    }

    /** Get a view property (ADS_DD_VIEW_STMT, ADS_DD_VIEW_COMMENT). */
    public function getViewProperty(string $name, int $prop): string
    {
        return $this->getPropStr(
            fn($buf, $len) => $this->ffi->AdsDDGetViewProperty(
                $this->handle,
                Connection::cstr($this->ffi, $name),
                $prop, $buf, $len
            ),
            "AdsDDGetViewProperty($name, $prop)"
        );
    }

    /** Set a view property. */
    public function setViewProperty(string $name, int $prop, string $value): void
    {
        $buf = Connection::cstr($this->ffi, $value);
        $rc  = $this->ffi->AdsDDSetViewProperty(
            $this->handle,
            Connection::cstr($this->ffi, $name),
            $prop, $buf, strlen($value)
        );
        $this->checkOk($rc, "AdsDDSetViewProperty($name, $prop)");
    }

    // -------------------------------------------------------------------------
    // Internals
    // -------------------------------------------------------------------------

    /**
     * Generic property getter: calls $fetcher($buf, $lenPtr) into a 512-byte
     * buffer, returns the filled string. Grows to 4096 bytes if needed.
     *
     * @param callable $fetcher fn(FFI\CData $buf, FFI\CData $lenPtr): int
     */
    private function getPropStr(callable $fetcher, string $ctx): string
    {
        foreach ([512, 4096] as $cap) {
            $buf = $this->ffi->new("UNSIGNED8[$cap]");
            $len = $this->ffi->new('UNSIGNED16');
            $len->cdata = $cap;
            $rc  = $fetcher($buf, FFI::addr($len));
            $this->checkOk($rc, $ctx);
            $actual = (int) $len->cdata;
            if ($actual <= $cap) {
                if ($actual === 0) {
                    return '';
                }
                $ptr = FFI::cast('char*', FFI::addr($buf[0]));
                return FFI::string($ptr, $actual);
            }
        }
        return '';
    }

    private function checkOk(int $rc, string $ctx): void
    {
        if ($rc !== AceTypes::AE_SUCCESS) {
            [$code, $text] = $this->lib->lastError();
            throw new OpenAdsException(
                "$ctx failed: " . AceTypes::errorName($code) . " ($code): $text",
                $code
            );
        }
    }
}
