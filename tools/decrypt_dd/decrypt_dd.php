<?php
/**
 * decrypt_dd.php — bulk-decrypt every encrypted table in a SAP Advantage data dictionary.
 *
 * Uses the SAP ACE client (php_ads extension) and SAP's own documented mechanism.
 * Everything goes through SQL — see "Why SQL only" below.
 *
 *   - SELECT ... FROM system.dictionary          read DD-level encryption settings
 *   - SELECT Name, Table_Encryption
 *       FROM system.tables                       which tables are encrypted
 *   - EXECUTE PROCEDURE sp_DecryptTable('t')     decrypt one DD-bound table (no password
 *                                                arg: the engine reads the key from the DD)
 *   - EXECUTE PROCEDURE sp_ModifyDatabase(p,v)   turn off ENCRYPT_NEW_TABLE / ENCRYPT_INDEXES
 *                                                and clear ENCRYPT_TABLE_PASSWORD
 *
 * There is no single "decrypt everything" call in ADS — sp_DecryptTable is per table,
 * so the loop below is the supported way to do it.
 *
 * WHY SQL ONLY (do not "simplify" this back to AdsDictionary):
 *   php_ads's AdsDictionary::getTableProperty / ::getDatabaseProperty hardcode the
 *   input buffer length to sizeof(buf)-1 == 1023 (see F:\php_advantage\src\ads_misc.c,
 *   AdsDictionary::getTableProperty). SAP validates that length against the property's
 *   declared width, so every 2-byte Boolean property — including
 *   ADS_DD_TABLE_ENCRYPTION (214) — fails with:
 *       5133 "The supplied buffer is not the expected size for the specified property.
 *             The table encryption flag is a 2 byte Boolean value."
 *   The setters are fine (they pass the real value length); only the getters are broken.
 *   system.dictionary / system.tables expose the exact same fields over SQL with no
 *   buffer involved, so we read there instead.
 *
 * ENGINE SELECTION — this box has two PHP configurations sharing one php.exe, and
 * BOTH extensions register the same class names (AdsConnection, AdsDictionary),
 * so class_exists() cannot tell them apart. Always select the ini explicitly:
 *
 *     C:\php\php_sapads.ini    extension=php_ads.dll      -> extension "ads"     (SAP ACE)
 *     C:\php\php_openads.ini   extension=php_openads.dll  -> extension "openads" (OpenADS)
 *     C:\php\php.ini           loads both; openads loses the duplicate-name race
 *
 * Decrypting a SAP dictionary must go through the SAP client (OpenADS ships
 * AdsDecryptTable as a stub), hence -c ...php_sapads.ini. The --engine check
 * below hard-fails if the wrong one is loaded.
 *
 * Requirements / cautions (from SAP's docs, see README.md next to this file):
 *   - Connect as the dictionary administrator (ADSSYS) or a user with ALTER on the tables.
 *   - Each table is decrypted in place and needs EXCLUSIVE access — no other users.
 *   - Cannot run inside a transaction.
 *   - BACK UP THE DATA DIRECTORY FIRST. This rewrites every record of every table.
 *
 * Usage:
 *   php -c C:\php\php_sapads.ini decrypt_dd.php \
 *       --path='\\172.16.0.138:6262\e$\adsdata\sfi\mp.add' \
 *       --user=adssys --password=SECRET
 *   (dry run: lists what would be decrypted, changes nothing)
 */

declare(strict_types=1);

/* Unbuffered output: this job runs for a long time and is normally redirected to a
 * file. Without this, progress lines sit in the buffer and a Ctrl-C / kill throws
 * away the tail — you cannot tell how far it actually got. */
ob_implicit_flush(true);
while (ob_get_level() > 0) {
    ob_end_flush();
}

/* ---- ACE error numbers we treat as benign ---- */
const AE_TABLE_NOT_ENCRYPTED = 5163;

/* ------------------------------------------------------------------ arg parsing */

$opt = getopt('', [
    'path:', 'user::', 'password::', 'servertype::', 'engine::',
    'only::', 'apply', 'disable-dd', 'reindex', 'help',
]);

if (isset($opt['help']) || !isset($opt['path'])) {
    fwrite(STDERR, <<<TXT
    decrypt_dd.php — decrypt every encrypted table in an Advantage data dictionary

      --path=<dd>        required, e.g. '\\\\172.16.0.138:6262\\e$\\adsdata\\sfi\\mp.add'
      --user=<u>         dictionary user (default: adssys)
      --password=<p>     dictionary password (default: empty)
      --servertype=<n>   ACE server type (default: remote)
      --engine=<e>       required client extension: sap | openads | any
                         (default: sap — decrypting a SAP DD needs the SAP client)
      --only=a,b,c       only these tables
      --apply            perform the decryption (without it: dry run, read only)
      --disable-dd       AFTER all tables are verified decrypted, turn off
                         ENCRYPT_NEW_TABLE / ENCRYPT_INDEXES and clear
                         ENCRYPT_TABLE_PASSWORD. Skipped if anything is still encrypted.
      --reindex          run sp_Reindex on each table after decrypting (needed to
                         strip encryption from indexes if ENCRYPT_INDEXES was on)

    Run it with the SAP ini:
      php -c C:\\php\\php_sapads.ini decrypt_dd.php --path='...' --user=adssys --password=...

    TXT);
    exit(1);
}

/* ---- engine check FIRST: everything below depends on extension-registered symbols ----
 *
 * php_ads.dll (SAP) and php_openads.dll (OpenADS) export the SAME class names, so
 * class_exists('AdsConnection') proves nothing about which engine you are talking to.
 * The extension NAME is the only reliable discriminator.
 */
$wantEngine = strtolower((string)($opt['engine'] ?? 'sap'));
if (!in_array($wantEngine, ['sap', 'openads', 'any'], true)) {
    fwrite(STDERR, "--engine must be one of: sap, openads, any\n");
    exit(1);
}
$sapLoaded = extension_loaded('ads');
$oaLoaded  = extension_loaded('openads');
$activeExt = $sapLoaded ? 'ads (SAP ACE)' : ($oaLoaded ? 'openads (OpenADS)' : '(none)');

if (!$sapLoaded && !$oaLoaded) {
    fwrite(STDERR, "No Advantage client extension is loaded.\n"
        . "Run with:  php -c C:\\php\\php_sapads.ini " . basename(__FILE__) . " ...\n");
    exit(1);
}
if ($wantEngine === 'sap' && !$sapLoaded) {
    fwrite(STDERR, "Refusing to run: --engine=sap but the loaded extension is '$activeExt'.\n"
        . "A SAP dictionary must be decrypted through the SAP client. Re-run with:\n"
        . "  php -c C:\\php\\php_sapads.ini " . basename(__FILE__) . " ...\n"
        . "(Both extensions define AdsConnection/AdsDictionary, so the ini is what decides.)\n");
    exit(1);
}
if ($wantEngine === 'openads' && !$oaLoaded) {
    fwrite(STDERR, "Refusing to run: --engine=openads but the loaded extension is '$activeExt'.\n"
        . "Re-run with:  php -c C:\\php\\php_openads.ini " . basename(__FILE__) . " ...\n");
    exit(1);
}

$ddPath     = $opt['path'];
$user       = $opt['user']     ?? 'adssys';
$password   = $opt['password'] ?? '';
$serverType = isset($opt['servertype']) ? (int)$opt['servertype'] : ADS_REMOTE_SERVER;
$apply      = isset($opt['apply']);
$disableDd  = isset($opt['disable-dd']);
$reindex    = isset($opt['reindex']);
$only       = isset($opt['only'])
    ? array_map('strtolower', array_filter(array_map('trim', explode(',', (string)$opt['only']))))
    : null;

/* ------------------------------------------------------------------ helpers */

/** ADS Logical columns arrive as bool, int, or a "T"/"F"-ish string depending on driver. */
function logicalVal(mixed $v): ?bool
{
    if ($v === null) {
        return null;
    }
    if (is_bool($v)) {
        return $v;
    }
    if (is_int($v) || is_float($v)) {
        return (bool)$v;
    }
    $s = strtoupper(trim((string)$v));
    if ($s === '') {
        return null;
    }
    if (in_array($s, ['T', 'TRUE', 'Y', 'YES', '1', '.T.'], true)) {
        return true;
    }
    if (in_array($s, ['F', 'FALSE', 'N', 'NO', '0', '.F.'], true)) {
        return false;
    }
    return null;
}

function yesNo(?bool $b): string
{
    return $b === null ? '?' : ($b ? 'yes' : 'no');
}

/** Case-insensitive column fetch — catalog column casing varies between engines. */
function col(array $row, string $name): mixed
{
    foreach ($row as $k => $v) {
        if (strcasecmp((string)$k, $name) === 0) {
            return $v;
        }
    }
    return null;
}

function sqlLiteral(string $s): string
{
    return "'" . str_replace("'", "''", $s) . "'";
}

function say(string $s): void
{
    echo $s, "\n";
}

/**
 * Read Name + Table_Encryption for every table in the dictionary.
 *
 * @return array<string,?bool> table name => encrypted?
 */
function readTableEncryption(AdsConnection $conn): array
{
    $out  = [];
    $stmt = $conn->query('SELECT Name, Table_Encryption FROM system.tables');
    while (($row = $stmt->fetchAssoc()) !== false) {
        $name = trim((string)col($row, 'Name'));
        if ($name !== '') {
            $out[$name] = logicalVal(col($row, 'Table_Encryption'));
        }
    }
    $stmt->close();
    uksort($out, 'strnatcasecmp');
    return $out;
}

/* ------------------------------------------------------------------ connect */

say(sprintf('Client     : %s', $activeExt));
say(sprintf('php.ini    : %s', php_ini_loaded_file() ?: '(none)'));
say(sprintf('Dictionary : %s', $ddPath));
say(sprintf('User       : %s', $user));
say(sprintf('Mode       : %s', $apply ? 'APPLY (tables will be rewritten)' : 'DRY RUN (read only)'));
say('');

try {
    $conn = AdsConnection::connect([
        'path'       => $ddPath,
        'user'       => $user,
        'password'   => $password,
        'serverType' => $serverType,
    ]);
} catch (AdsException $e) {
    fwrite(STDERR, sprintf("Connect failed (%d): %s\n", $e->getCode(), $e->getMessage()));
    exit(2);
}

/* ------------------------------------------------------------------ DD-level state */

$encTypeNames = [3 => 'RC4', 5 => 'AES128', 6 => 'AES256'];

say('Dictionary settings (system.dictionary)');
try {
    $stmt = $conn->query('SELECT * FROM system.dictionary');
    $ddRow = $stmt->fetchAssoc();
    $stmt->close();

    if ($ddRow === false) {
        say('  (no row returned)');
    } else {
        $encType = col($ddRow, 'Data_Encryption_Type');
        // Encrypt_Table_Password is readable on an admin connection — never print it.
        $hasPw = trim((string)col($ddRow, 'Encrypt_Table_Password')) !== '';
        say(sprintf('  dictionary file encrypted   : %s', yesNo(logicalVal(col($ddRow, 'Dictionary_Encrypted')))));
        say(sprintf('  encrypt new tables          : %s', yesNo(logicalVal(col($ddRow, 'Encrypt_New_Table')))));
        say(sprintf('  encrypt indexes             : %s', yesNo(logicalVal(col($ddRow, 'Encrypt_Indexes')))));
        say(sprintf('  encrypt communication       : %s', yesNo(logicalVal(col($ddRow, 'Encrypt_Communication')))));
        say(sprintf('  data encryption type        : %s',
            $encType === null ? '?' : ($encTypeNames[(int)$encType] ?? (string)$encType)));
        say(sprintf('  table encryption password   : %s', $hasPw ? 'set' : 'not set'));
    }
} catch (AdsException $e) {
    say(sprintf('  could not read system.dictionary (%d): %s', $e->getCode(), trim($e->getMessage())));
}
say('');

/* ------------------------------------------------------------------ enumerate + classify */

try {
    $state = readTableEncryption($conn);
} catch (AdsException $e) {
    fwrite(STDERR, sprintf("Could not read system.tables (%d): %s\n", $e->getCode(), $e->getMessage()));
    exit(2);
}

if ($only !== null) {
    $state = array_filter(
        $state,
        fn(string $t): bool => in_array(strtolower($t), $only, true),
        ARRAY_FILTER_USE_KEY
    );
}

$encrypted = array_keys(array_filter($state, fn(?bool $e): bool => $e === true));
$plain     = array_keys(array_filter($state, fn(?bool $e): bool => $e === false));
$unknown   = array_keys(array_filter($state, fn(?bool $e): bool => $e === null));

say(sprintf('Tables in dictionary: %d', count($state)));
say(sprintf('  encrypted : %d', count($encrypted)));
say(sprintf('  plain     : %d', count($plain)));
if ($unknown) {
    say(sprintf('  unknown   : %d (Table_Encryption did not parse)', count($unknown)));
    foreach ($unknown as $t) {
        say('    ' . $t);
    }
}
say('');

foreach ($encrypted as $t) {
    say('  ENCRYPTED  ' . $t);
}
if ($encrypted) {
    say('');
    // The work list is derived from live catalog state, not from a saved position,
    // so an interrupted run is resumed simply by running the same command again:
    // already-decrypted tables come back as plain and drop out of the list.
    say('This list is read live from system.tables — if a run is interrupted, just');
    say('re-run the same command and it picks up where it left off.');
    say('');
}

if (!$encrypted) {
    say('Nothing to decrypt.');
}

if (!$apply) {
    say('Dry run — no changes made. Re-run with --apply to decrypt.');
    $conn->close();
    exit(0);
}

/* ------------------------------------------------------------------ decrypt */

$ok     = [];
$failed = [];

$runStart = microtime(true);

foreach ($encrypted as $i => $t) {
    printf("[%2d/%d] %s decrypting %-32s ", $i + 1, count($encrypted), date('H:i:s'), $t);
    $t0 = microtime(true);
    try {
        // Single argument = DD-bound table; the engine pulls the key from the dictionary.
        $conn->execute('EXECUTE PROCEDURE sp_DecryptTable(' . sqlLiteral($t) . ')');
        printf("ok (%.1fs)\n", microtime(true) - $t0);
        $ok[] = $t;
    } catch (AdsException $e) {
        if ($e->getCode() === AE_TABLE_NOT_ENCRYPTED) {
            printf("already plain (%.1fs)\n", microtime(true) - $t0);
            $ok[] = $t;
            continue;
        }
        printf("FAILED (%d) %s\n", $e->getCode(), trim($e->getMessage()));
        $failed[$t] = sprintf('%d: %s', $e->getCode(), trim($e->getMessage()));
        continue;
    }

    if ($reindex) {
        try {
            // sp_Reindex takes TWO args: table name and index page size.
            // 0 = keep the table's existing page size. Omitting it is a 7200 AQE error.
            $conn->execute('EXECUTE PROCEDURE sp_Reindex(' . sqlLiteral($t) . ', 0)');
            say('        reindexed');
        } catch (AdsException $e) {
            say(sprintf('        reindex FAILED (%d) %s', $e->getCode(), trim($e->getMessage())));
        }
    }
}

say('');
say(sprintf('Decrypted %d, failed %d, in %.1f min.',
    count($ok), count($failed), (microtime(true) - $runStart) / 60));
foreach ($failed as $t => $why) {
    say(sprintf('  FAILED %-30s %s', $t, $why));
}

/* ------------------------------------------------------------------ verify */

say('');
say('Verifying (re-reading system.tables)...');
$stillEncrypted = [];
$verifyFailed   = false;
try {
    foreach (readTableEncryption($conn) as $t => $enc) {
        if ($enc === true) {
            $stillEncrypted[] = $t;
        }
    }
} catch (AdsException $e) {
    $verifyFailed = true;
    say(sprintf('  verification query FAILED (%d) %s', $e->getCode(), trim($e->getMessage())));
}

if ($verifyFailed) {
    say('  Encryption state UNKNOWN — treating as not verified.');
} elseif ($stillEncrypted) {
    say(sprintf('  %d table(s) still encrypted:', count($stillEncrypted)));
    foreach ($stillEncrypted as $t) {
        say('    ' . $t);
    }
} else {
    say('  All tables report unencrypted.');
}

/* ------------------------------------------------------------------ DD-level cleanup */

if ($disableDd) {
    say('');
    say('Clearing dictionary encryption settings...');

    if ($verifyFailed || $stillEncrypted || $failed) {
        say('  SKIPPED — not every table is confirmed decrypted. ADS refuses to clear the');
        say('  table encryption password while any table is still encrypted (error 5130),');
        say('  and flipping the other flags first would only muddy the dictionary state.');
        say('  Fix the failures above, then re-run with --disable-dd.');
    } else {
        // Clearing the password sets ENCRYPT_NEW_TABLE False as a documented side effect,
        // so do it FIRST — if it fails, the other flags are left untouched.
        $cleared = false;
        try {
            $conn->execute("EXECUTE PROCEDURE sp_ModifyDatabase('ENCRYPT_TABLE_PASSWORD', NULL)");
            say('  table encryption password -> cleared (ENCRYPT_NEW_TABLE forced off)');
            $cleared = true;
        } catch (AdsException $e) {
            say(sprintf('  clearing table encryption password FAILED (%d) %s',
                $e->getCode(), trim($e->getMessage())));
            say('  Leaving ENCRYPT_NEW_TABLE / ENCRYPT_INDEXES untouched.');
        }

        if ($cleared) {
            foreach (['ENCRYPT_NEW_TABLE', 'ENCRYPT_INDEXES'] as $prop) {
                try {
                    $conn->execute("EXECUTE PROCEDURE sp_ModifyDatabase('$prop', 'FALSE')");
                    say("  $prop -> off");
                } catch (AdsException $e) {
                    say(sprintf('  %s FAILED (%d) %s', $prop, $e->getCode(), trim($e->getMessage())));
                }
            }
        }
    }
}

$conn->close();

exit(($failed || $stillEncrypted || $verifyFailed) ? 3 : 0);
