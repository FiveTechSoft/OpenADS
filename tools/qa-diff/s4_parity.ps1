# S4 parity gate — run identical script-engine workloads on SAP ace64.dll
# and OpenADS openace64.dll against per-engine sandbox copies of the pmsys
# data, and diff the answers. SAP is the oracle.
#
# Usage: .\s4_parity.ps1  (expects F:\tmp\parity\{sap_data,oa_data} set up
#        and pmsys_OpenADS.add already converted in oa_data)
param(
    [string]$SapLib = "F:\ads11\ace64.dll",
    [string]$SapDb  = "F:\tmp\parity\sap_data\pmsys.add",
    [string]$OaLib  = "F:\OpenADS\build\ninja-clang-local\src\openace64.dll",
    [string]$OaDb   = "F:\tmp\parity\oa_data\pmsys_OpenADS.add"
)
$dd = ".\dd_meta_dump.exe"
$user = "adssys"; $pw = "pmsys"

$cases = [ordered]@{
# ---- functions (8 with args; CurrentLease exercises SELECT TOP 1 inside) --
"fn_DaysInMonth"    = "SELECT DaysInMonth(2,2026) AS v FROM system.iota";
"fn_DaysInMonth_ly" = "SELECT DaysInMonth(2,2024) AS v FROM system.iota";
"fn_BoY"            = "SELECT BoY(2026) AS v FROM system.iota";
"fn_EoY"            = "SELECT EoY(2026) AS v FROM system.iota";
"fn_EoM"            = "SELECT EoM(2,2026) AS v FROM system.iota";
"fn_EoM_dec"        = "SELECT EoM(12,2025) AS v FROM system.iota";
"fn_MonthsRented"   = "SELECT MonthsRented('16201 Ashely Park Plc', {d '2016-01-01'}, {d '2018-01-01'}) AS v FROM system.iota";
"fn_MonthsOnMarket" = "SELECT MonthsOnTheMarket('16201 Ashely Park Plc', {d '2016-01-01'}, {d '2018-01-01'}) AS v FROM system.iota";
"fn_CurrentLease"   = "SELECT CurrentLease('16201 Ashely Park Plc', {d '2017-01-15'}) AS v FROM system.iota";
"fn_CurrentLease2"  = "SELECT CurrentLease('2503 E. Curtis St.', {d '2017-06-01'}) AS v FROM system.iota";
"fn_PhysPos"        = "SELECT PhysPos('CKwdsBAAAAAQAAAAAB') AS v FROM system.iota";

# ---- read-only / result-set procs ----------------------------------------
"sp_GetPhysicalPath" = "EXECUTE PROCEDURE sp_GetPhysicalPath()";
"sp_mgAllLocks"      = "EXECUTE PROCEDURE sp_mgGetAllLocksAllTablesAllUsers()";

# ---- writing procs: run, then verify the observable side effects ---------
# sp_SaveIntoAuditLog is in $bothFailCases: standalone (outside trigger
# context) BOTH engines reject at the proc's `... FROM __new` statement —
# SAP 5154→5004 missing __new.adt, OA embedded-SQL failure. Text parity
# is the error-wrap gap.
"chk_auditlog_count"   = "SELECT COUNT(*) AS n FROM auditlog WHERE TableKey = 'PARITYKEY01'";
"sp_ChargeMonthlyRent" = "EXECUTE PROCEDURE sp_ChargeMonthlyRent({d '2026-07-01'})";
"chk_charges_month"    = "SELECT COUNT(*) AS n FROM encitems WHERE CatCode = 'RENT'";
"sp_ChargeLateFees"    = "EXECUTE PROCEDURE sp_ChargeLateFees({d '2026-07-10'})";
"sp_createrem"         = "EXECUTE PROCEDURE sp_createremforunchargedrents({d '2026-07-01'})";
"chk_reminders"        = "SELECT COUNT(*) AS n FROM reminders";

# ---- NewSeqKey (writes sequences) ----------------------------------------
"fn_NewSeqKey"       = "SELECT NewSeqKey('rmkey') AS v FROM system.iota";

# ---- trigger firing via DML (Insert/Update/Delete AuditLog on properties) -
# trg_ins is in $bothFailCases below: SAP rejects it 7200/5147 (LandLordID
# NOT NULL) and OA rejects it 5147 — semantics equal, error TEXT can't
# match until the AQE error-wrap gap closes.
"chk_ins"  = "SELECT COUNT(*) AS n FROM auditlog WHERE TableKey = 'ZZ PARITY TEST'";
"trg_upd"  = "UPDATE properties SET Notes = 'parity' WHERE PropertyID = 'ZZ PARITY TEST'";
"chk_upd"  = "SELECT COUNT(*) AS n FROM auditlog WHERE TableKey = 'ZZ PARITY TEST'";
"trg_del"  = "DELETE FROM properties WHERE PropertyID = 'ZZ PARITY TEST'";
"chk_del"  = "SELECT COUNT(*) AS n FROM auditlog WHERE TableKey = 'ZZ PARITY TEST'";
}

# ---- NEWIDSTRING: values are random, so parity = both engines produce the
# ---- same SHAPE (oracle-verified: lowercase hex, v4 nibble at byte 7 in hex
# ---- forms / byte 6 in base64 forms, M padded to 24, F url-safe 22) --------
$shapeCases = [ordered]@{
"guid_N"    = @("SELECT NEWIDSTRING(N) AS v FROM system.iota",           '^\[\{"v":"[0-9a-f]{14}4[0-9a-f][89ab][0-9a-f]{15}"\}\]$');
"guid_D"    = @("SELECT NEWIDSTRING(D) AS v FROM system.iota",           '^\[\{"v":"[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{2}4[0-9a-f]-[89ab][0-9a-f]{3}-[0-9a-f]{12}"\}\]$');
"guid_dflt" = @("SELECT NEWIDSTRING() AS v FROM system.iota",            '^\[\{"v":"[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{2}4[0-9a-f]-[89ab][0-9a-f]{3}-[0-9a-f]{12}"\}\]$');
"guid_B"    = @("SELECT NEWIDSTRING(B) AS v FROM system.iota",           '^\[\{"v":"\[[0-9a-f-]{36}\]"\}\]$');
"guid_P"    = @("SELECT NEWIDSTRING(P) AS v FROM system.iota",           '^\[\{"v":"\([0-9a-f-]{36}\)"\}\]$');
"guid_C"    = @("SELECT NEWIDSTRING(CURLYBRACES) AS v FROM system.iota", '^\[\{"v":"\{[0-9a-f-]{36}\}"\}\]$');
"guid_M"    = @("SELECT NEWIDSTRING(MIME) AS v FROM system.iota",        '^\[\{"v":"[A-Za-z0-9+/]{22}=="\}\]$');
"guid_F"    = @("SELECT NEWIDSTRING(FILE) AS v FROM system.iota",        '^\[\{"v":"[A-Za-z0-9_-]{22}"\}\]$');
}

# ---- cases where BOTH engines must reject (any rc): OA reports native
# ---- codes while SAP wraps as 7200; message parity is the error-wrap gap --
$bothFailCases = [ordered]@{
"trg_ins" = "INSERT INTO properties (PropertyID) VALUES ('ZZ PARITY TEST')";
"sp_SaveIntoAuditLog" = "EXECUTE PROCEDURE sp_SaveIntoAuditLog('properties','PARITYKEY01','INSERT')";
}

# ---- NEWIDSTRING error parity: message text differs (known gap), rc must
# ---- agree (SAP 7200/2159 invalid arg; 7200/2158 fn not found) -------------
$rcCases = [ordered]@{
"guid_err_quoted"  = "SELECT NEWIDSTRING('D') AS v FROM system.iota";
"guid_err_badfmt"  = "SELECT NEWIDSTRING(X) AS v FROM system.iota";
"guid_err_partial" = "SELECT NEWIDSTRING(DELIM) AS v FROM system.iota";
"guid_err_newid"   = "SELECT NEWID() AS v FROM system.iota";
}

$pass = 0; $fail = 0
foreach ($k in $cases.Keys) {
    $q = $cases[$k]
    $a = (& $dd --lib $SapLib --db $SapDb --user $user --password $pw --sql $q 2>&1) -join " "
    $b = (& $dd --lib $OaLib  --db $OaDb  --user $user --password $pw --sql $q 2>&1) -join " "
    if ($a -eq $b) {
        "PASS  {0,-22} {1}" -f $k, ($a.Substring(0, [Math]::Min(90, $a.Length)))
        $pass++
    } else {
        "DIFF  {0,-22}" -f $k
        "      SAP: " + $a.Substring(0, [Math]::Min(160, $a.Length))
        "      OA : " + $b.Substring(0, [Math]::Min(160, $b.Length))
        $fail++
    }
}
foreach ($k in $shapeCases.Keys) {
    $q  = $shapeCases[$k][0]; $rx = $shapeCases[$k][1]
    $a = (& $dd --lib $SapLib --db $SapDb --user $user --password $pw --sql $q 2>&1) -join " "
    $b = (& $dd --lib $OaLib  --db $OaDb  --user $user --password $pw --sql $q 2>&1) -join " "
    if ($a -cmatch $rx -and $b -cmatch $rx) {
        "PASS  {0,-22} shape ok  OA: {1}" -f $k, ($b.Substring(0, [Math]::Min(60, $b.Length)))
        $pass++
    } else {
        "DIFF  {0,-22} (shape)" -f $k
        "      SAP: " + $a.Substring(0, [Math]::Min(160, $a.Length))
        "      OA : " + $b.Substring(0, [Math]::Min(160, $b.Length))
        $fail++
    }
}

foreach ($k in $bothFailCases.Keys) {
    $q = $bothFailCases[$k]
    $a = (& $dd --lib $SapLib --db $SapDb --user $user --password $pw --sql $q 2>&1) -join " "
    $b = (& $dd --lib $OaLib  --db $OaDb  --user $user --password $pw --sql $q 2>&1) -join " "
    $fa = $a -match '"error"'; $fb = $b -match '"error"'
    if ($fa -and $fb) {
        "PASS  {0,-22} both reject" -f $k
        $pass++
    } else {
        "DIFF  {0,-22} (reject)  SAP failed=$fa  OA failed=$fb" -f $k
        "      SAP: " + $a.Substring(0, [Math]::Min(160, $a.Length))
        "      OA : " + $b.Substring(0, [Math]::Min(160, $b.Length))
        $fail++
    }
}

foreach ($k in $rcCases.Keys) {
    $q = $rcCases[$k]
    $a = (& $dd --lib $SapLib --db $SapDb --user $user --password $pw --sql $q 2>&1) -join " "
    $b = (& $dd --lib $OaLib  --db $OaDb  --user $user --password $pw --sql $q 2>&1) -join " "
    $ra = if ($a -match '"rc=(\d+)"') { $Matches[1] } else { "ok" }
    $rb = if ($b -match '"rc=(\d+)"') { $Matches[1] } else { "ok" }
    if ($ra -eq $rb) {
        "PASS  {0,-22} rc=$ra on both" -f $k
        $pass++
    } else {
        "DIFF  {0,-22} (rc)  SAP rc=$ra  OA rc=$rb" -f $k
        "      SAP: " + $a.Substring(0, [Math]::Min(160, $a.Length))
        "      OA : " + $b.Substring(0, [Math]::Min(160, $b.Length))
        $fail++
    }
}

""
"==== $pass identical, $fail diverged ===="
