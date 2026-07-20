<?php
/**
 * openads extension stubs for IDE only.
 *
 * This file is intentionally placed in the workspace so PHP language servers
 * (Intelephense, PHP Language Server, Psalm, etc.) can index the classes
 * provided by the `php_openads` extension. It does nothing at runtime when
 * the real extension is available.
 */

if (!extension_loaded('openads') && !class_exists('AdsConnection')) {
    class AdsException extends Exception {}

    class AdsConnection
    {
        /** @param array $opts keys: path, user, password, server_type, connType */
        public static function connect(array $opts): static {}
        public function close(): void {}
        public function query(string $sql): AdsStatement {}
        public function execute(string $sql): void {}
        public function isAlive(): bool {}
    }

    class AdsStatement
    {
        public function fetchAssoc(): ?array {}
        public function fetchAll(): array {}
        public function close(): void {}
    }

    class AdsTable
    {
        /** @param int $mode 0=read/write, 1=read-only */
        public static function open(AdsConnection $conn, string $table, int $mode = 0): static {}
        public function close(): void {}
        /** @return array[] each: ['tag'=>string,'expression'=>string,'descending'=>bool] */
        public function getIndexTags(): array {}
        public function recordCount(): int {}
        // Navigation
        public function gotoTop(): void {}
        public function gotoBottom(): void {}
        public function skip(int $n): void {}
        public function atEOF(): bool {}
        public function atBOF(): bool {}
        // Record I/O
        public function getRecord(): array {}
        public function appendRecord(): void {}
        public function writeRecord(): void {}
        public function deleteRecord(): void {}
        // Field setters
        public function setString(string $field, string $value): void {}
        public function setLong(string $field, int $value): void {}
        public function setDouble(string $field, float $value): void {}
        public function setLogical(string $field, bool $value): void {}
    }

    class AdsDictionary
    {
        public static function open(string $path): static {}
        public static function fromConnection(AdsConnection $conn): static {}
        public function close(): void {}
        public function getTableProperty(string $table, int $property): string {}
        public function setTableProperty(string $table, int $property, string $value): void {}
        public function getFieldProperty(string $table, string $field, int $property): string {}
        public function setFieldProperty(string $table, string $field, int $property, string $value): void {}
        public function getDatabaseProperty(int $property): string {}
        public function setDatabaseProperty(int $property, string $value): void {}
        // User property codes: 1101=password, 1102=group_membership, 1103=bad_logins, 1=comment
        public function getUserProperty(string $user, int $property): string {}
        public function setUserProperty(string $user, int $property, string $value): void {}
        public function addTable(string $alias, string $path, int $tableType = 3, int $charType = 1, string $indexPath = '', string $comment = ''): void {}
        public function removeTable(string $alias, bool $deleteFiles = false): void {}
        public function addIndexFile(string $table, string $indexPath, string $comment = ''): void {}
        public function removeIndexFile(string $table, string $indexPath): void {}
        // Index property codes (SAP ABI): 400=FILE_NAME 401=EXPRESSION
        // 402=CONDITION 403=OPTIONS(u32 bitmask) 404=KEY_LENGTH(u16)
        // 405=KEY_TYPE(u16)
        public function getIndexProperty(string $table, string $indexName, int $property): string {}
        public function setIndexProperty(string $table, string $indexName, int $property, string $value): void {}
        // Trigger property codes: 1401=event_mask(u32) 1402=timing(u32) 1404=body(container) 1405=proc_name 1408=table
        public function getTriggerProperty(string $name, int $property): string {}
        public function setTriggerProperty(string $name, int $property, string $value): void {}
        public function createTrigger(string $name, string $table, int $type, string $container = '', string $procedure = '', int $priority = 1): void {}
        public function dropTrigger(string $name): void {}
        // Proc property codes: 800=input_params 801=output_params 802=container 803=body 805=comment
        public function getProcProperty(string $name, int $property): string {}
        public function setProcProperty(string $name, int $property, string $value): void {}
        public function createProcedure(string $name, string $container, string $procedure, string $input = '', string $output = '', string $comment = ''): void {}
        public function dropProcedure(string $name): void {}
        // Function property codes: 700=body 701=input_params 702=return_type 703=container 704=comment
        public function getFunctionProperty(string $name, int $property): string {}
        public function setFunctionProperty(string $name, int $property, string $value): void {}
        public function createFunction(string $name, string $container, string $implementation, string $retType = '', string $input = '', string $comment = ''): void {}
        public function dropFunction(string $name): void {}
    }
}
