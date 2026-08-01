# S4 parity gate (mp corpus) - second data-level corpus alongside pmsys.
#
# Same design as s4_parity.ps1: run identical workloads on SAP ace64.dll and
# OpenADS openace64.dll against per-engine sandbox copies of the mp data and
# diff the answers. SAP is the oracle.
#
# Why a second corpus: pmsys is property management (ADT-only, 7912 permission
# rows). mp is medical billing - 95 tables, 9.65M rows, mixed ADT/DBF+NTX/CDX,
# 63801 permission rows, 32 triggers, 11 RI rules, and script-engine functions
# that use MERGE / CASE / cursor loops / TimeStampAdd, none of which pmsys
# exercises. See TODO.parity.md.
#
# Setup (see tools/qa-diff/README.md):
#   F:\tmp\parity\sap_data_mp\mp.add          <- copy of the SAP data dir
#   F:\tmp\parity\oa_data_mp\mp_OpenADS.add   <- same copy, run through import_dd
#
# Usage: .\s4_parity_mp.ps1
#
# PSAvoidUsingPlainTextForPassword is suppressed deliberately: dd_meta_dump.exe
# takes --password as a command-line argument, so the value has to be plaintext
# at the point of use. A SecureString here would only be decoded a few lines
# later and would add ceremony without adding protection. What matters is that
# the value is never *stored* in the repo — hence the env-var default below.
[Diagnostics.CodeAnalysis.SuppressMessageAttribute(
    'PSAvoidUsingPlainTextForPassword', 'Password')]
param(
    [string]$SapLib = "F:\ads11\ace64.dll",
    [string]$SapDb  = "F:\tmp\parity\sap_data_mp\mp.add",
    [string]$OaLib  = "F:\OpenADS\build\ninja-clang-local\src\openace64.dll",
    [string]$OaDb   = "F:\tmp\parity\oa_data_mp\mp_OpenADS.add",
    [string]$User   = "adssys",
    # NEVER hardcode this. mp is a live medical-billing dictionary and this repo
    # is public: a literal here is published, mirrored by every fork, and cannot
    # be recalled by deleting the commit. Supply it per-run instead:
    #   $env:OPENADS_PARITY_PW = 'secret'; .\s4_parity_mp.ps1
    # or  .\s4_parity_mp.ps1 -Password 'secret'
    [string]$Password = $env:OPENADS_PARITY_PW
)

if ([string]::IsNullOrEmpty($Password)) {
    Write-Error ("No dictionary password supplied. Set `$env:OPENADS_PARITY_PW " +
                 "or pass -Password. Do not hardcode it - this repo is public.")
    exit 2
}
$dd = ".\dd_meta_dump.exe"
$user = $User; $pw = $Password

# ---------------------------------------------------------------------------
# Exact-match cases: SAP and OpenADS must return byte-identical JSON.
# ---------------------------------------------------------------------------
$cases = [ordered]@{
# ---- scalar/script functions ---------------------------------------------
# DaysInMonth: CONVERT + REPEAT + TimeStampAdd + TimeStampDiff round trip
"fn_DaysInMonth_feb"    = "SELECT DaysInMonth(2,2026) AS v FROM system.iota";
"fn_DaysInMonth_leap"   = "SELECT DaysInMonth(2,2024) AS v FROM system.iota";
"fn_DaysInMonth_dec"    = "SELECT DaysInMonth(12,2025) AS v FROM system.iota";
"fn_EoM"                = "SELECT EoM(2,2026) AS v FROM system.iota";
"fn_EoM_leap"           = "SELECT EoM(2,2024) AS v FROM system.iota";
# PhysRecNum/recno: position()+substring() base64 arithmetic over a literal
"fn_PhysRecNum"         = "SELECT PhysRecNum('CKwdsBAAAAAQAAAAAB') AS v FROM system.iota";
"fn_recno"              = "SELECT recno('CKwdsBAAAAAQAAAAAB') AS v FROM system.iota";
# find_icd10_parent: cursor reopened inside a WHILE loop over icd10cm (96k rows)
"fn_icd10_parent_l1"    = "SELECT find_icd10_parent('A0100',1) AS v FROM system.iota";
"fn_icd10_parent_l2"    = "SELECT find_icd10_parent('A0100',2) AS v FROM system.iota";

# ---- views ----------------------------------------------------------------
# SumByClaim: GROUP BY + aggregate over an expression + [real] bracketed
# reserved-word column identifier
"vw_SumByClaim"         = "SELECT TOP 5 Total, ClaimKey FROM SumByClaim ORDER BY ClaimKey";
"vw_TestView"           = "SELECT TOP 5 recno, Last, Name FROM TestView ORDER BY recno";
# the same shape written inline, to separate view resolution from the SQL
"agg_inline_sumbyclaim" = "SELECT TOP 5 Sum([real] * units) AS Total, ClaimKey FROM prcLines GROUP BY ClaimKey ORDER BY ClaimKey";

# ---- aggregates over real volume -----------------------------------------
"agg_counts"            = "SELECT COUNT(*) AS n FROM service";
"agg_sum_payfile"       = "SELECT COUNT(*) AS n, SUM(CHARGES) AS s FROM payfile";
"agg_mixed_aggs"        = "SELECT COUNT(*) AS n, SUM(INS_PAY) AS ins, MIN(PAY_DATE) AS lo, MAX(PAY_DATE) AS hi FROM payfile";
"agg_group_claimstatus" = "SELECT TOP 10 Status, COUNT(*) AS n FROM claimsstatus GROUP BY Status ORDER BY Status";
"agg_distinct_ins"      = "SELECT COUNT(DISTINCT insurance) AS n FROM service";

# ---- joins across the RI graph -------------------------------------------
"join_pat_admit"        = "SELECT TOP 5 p.RECNO, a.ADM_NUM FROM patients p JOIN admit a ON p.RECNO = a.RECNO ORDER BY p.RECNO, a.ADM_NUM";
"join_3way"             = "SELECT TOP 5 s.CLAIMKEY, s.ADM_NUM, l.UNITS FROM service s JOIN prclines l ON s.CLAIMKEY = l.CLAIMKEY JOIN admit a ON s.ADM_NUM = a.ADM_NUM ORDER BY s.CLAIMKEY";
"join_left"             = "SELECT TOP 5 s.CLAIMKEY, c.STATUS FROM service s LEFT JOIN claimsstatus c ON s.CLAIMKEY = c.CLAIMKEY ORDER BY s.CLAIMKEY";

# ---- non-ADT table types (pmsys is ADT-only) ------------------------------
# paytemp is a DBF with an NTX index (331k rows); pat_err is DBF+CDX
"ntx_paytemp_count"     = "SELECT COUNT(*) AS n FROM paytemp";
"cdx_paterr_count"      = "SELECT COUNT(*) AS n FROM pat_err";
"ntx_provider"          = "SELECT TOP 5 * FROM provider";

# ---- DD catalogs: SAP column names + row structure -----------------------
"cat_tables_n"          = "SELECT COUNT(*) AS n FROM system.tables";
"cat_columns_n"         = "SELECT COUNT(*) AS n FROM system.columns";
"cat_indexes_n"         = "SELECT COUNT(*) AS n FROM system.indexes";
"cat_triggers_n"        = "SELECT COUNT(*) AS n FROM system.triggers";
"cat_relations_n"       = "SELECT COUNT(*) AS n FROM system.relations";
"cat_users_n"           = "SELECT COUNT(*) AS n FROM system.users";
"cat_groups_n"          = "SELECT COUNT(*) AS n FROM system.usergroups";
"cat_perms_n"           = "SELECT COUNT(*) AS n FROM system.permissions";
# Object_Type = 4 (columns). Use the RANGE form, not `= 4`: SAP's AQE returns
# 255 for `Object_Type = 4` while its own GROUP BY says 55131, and `= 1` / `= 8`
# are correct - a SAP bug on that one literal. `>= 4 AND <= 4` gets the right
# answer out of both engines, so the case measures OpenADS instead of SAP.
"cat_perms_type4_range" = "SELECT COUNT(*) AS n FROM system.permissions WHERE Object_Type >= 4 AND Object_Type <= 4";
# ...and the shape that exposes the SAP bug, kept as a documented known-diff.
"cat_perms_type4_eq"    = "SELECT COUNT(*) AS n FROM system.permissions WHERE Object_Type = 4";
# grantee count: SAP 51, OA 50 - DB:Debug is not imported
"cat_perms_grantees"    = "SELECT COUNT(DISTINCT Grantee) AS n FROM system.permissions";
# the DB: built-in groups; DB:Debug is the one that goes missing
"cat_db_groups"         = "SELECT Grantee FROM system.permissions WHERE Object_Type >= 19 AND Object_Type <= 19 AND Grantee LIKE 'DB:%' ORDER BY Grantee";
# type 12 = LINK; SAP has the 'Ver8L' link object, OA only the LINK root
"cat_perms_links"       = "SELECT Name FROM system.permissions WHERE Object_Type >= 12 AND Object_Type <= 12 AND Grantee = 'General' ORDER BY Name";
# user-name casing: SAP preserves declared case, OA lowercases all 27 mixed-case users
"cat_perms_grantee_case" = "SELECT Grantee FROM system.permissions WHERE Object_Type >= 19 AND Object_Type <= 19 AND Grantee LIKE 'RCB' ORDER BY Grantee";
# task #4 missed the user/group catalogs: OA has USER_NAME / GROUP_NAME, SAP has Name
"cat_users_by_name"     = "SELECT Name FROM system.users ORDER BY Name";
"cat_groups_by_name"    = "SELECT Name FROM system.usergroups ORDER BY Name";
# Full SAP column set + order on the three catalogs task #4 had missed. These
# resolve on both engines now; adssys is excluded because OpenADS's importer
# adds it and SAP omits it from these catalogs (tracked by cat_users_n), and
# it sorts first so it would shift every TOP-N window.
"cat_users_shape"       = "SELECT TOP 1 Name, Enable_Internet, Logins_Disabled, Comment, User_Defined_Prop, Require_Old_Password FROM system.users WHERE Name <> 'adssys' ORDER BY Name";
"cat_groups_shape"      = "SELECT Name, Comment FROM system.usergroups ORDER BY Name";
"cat_members_shape"     = "SELECT TOP 10 User_Name, Group_Name FROM system.usergroupmembers WHERE User_Name <> 'adssys' ORDER BY User_Name, Group_Name";
"cat_rel_names"         = "SELECT Name, RI_Primary_Table, RI_Foreign_Table FROM system.relations ORDER BY Name";
"cat_trig_names"        = "SELECT TOP 10 Name, Trig_TableName FROM system.triggers ORDER BY Trig_TableName, Name";

# ---- scalar-function surface on real columns ------------------------------
"scalar_upper_trim"     = "SELECT TOP 5 Upper(Trim(LAST)) AS u FROM patients ORDER BY RECNO";
"scalar_date"           = "SELECT TOP 5 Year(ADM_DATE) AS y, Month(ADM_DATE) AS m FROM admit ORDER BY ADM_NUM";
# Length() is a documented SAP scalar function; OpenADS raises 2158 (unknown
# function) even on a literal. Isolated so the gate names the gap precisely.
"scalar_length_literal" = "SELECT Length('abc') AS n FROM system.iota";

# ---- deleted-record visibility on DBF tables ------------------------------
# SAP counts deleted rows (SET DELETED OFF is the default); OpenADS filters
# them. users.dbf = 18 physical / 8 deleted / 10 live, provider.dbf = 20/4/16,
# Forms.dbf = 64/0/64 (the control - both agree when nothing is deleted).
# pmsys is ADT-only so this never surfaced there.
"del_users_dbf"         = "SELECT COUNT(*) AS n FROM users";
"del_provider_dbf"      = "SELECT COUNT(*) AS n FROM provider";
"del_forms_dbf_ctrl"    = "SELECT COUNT(*) AS n FROM Forms";
}

# ---------------------------------------------------------------------------
# Shape cases: values are legitimately engine/sandbox specific.
# ---------------------------------------------------------------------------
$shapeCases = [ordered]@{
# mp's sp_GetPhysicalPath takes NO arguments (pmsys's takes a table name) -
# check system.storedprocedures.Proc_Input before copying a case across corpora.
# The path itself is legitimately per-sandbox, hence a shape case; the COLUMN
# NAME is not, so the regex pins it. OpenADS returns `databasepa` because the
# proc __output temp is a DBF (10-char field names) - TODO.parity.md backlog
# item 2. This case is EXPECTED TO DIFF until that is fixed; it used to pass
# only because the old regex checked the path and ignored the column name.
"sp_GetPhysicalPath" = @(
    "EXECUTE PROCEDURE sp_GetPhysicalPath()",
    '"databasepath":"F:\\\\tmp\\\\parity\\\\(sap|oa)_data_mp'
);
}

# ---------------------------------------------------------------------------
# Both engines must reject these.
# ---------------------------------------------------------------------------
$bothFailCases = [ordered]@{
"err_no_such_table"  = "SELECT * FROM no_such_table_xyz";
"err_no_such_col"    = "SELECT no_such_col_xyz FROM patients";
"err_ambiguous"      = "SELECT RECNO FROM patients p JOIN admit a ON p.RECNO = a.RECNO";
}

# ---------------------------------------------------------------------------
# Same return code on both.
# ---------------------------------------------------------------------------
$rcCases = [ordered]@{
"rc_bad_func_arg"    = "SELECT DaysInMonth('x','y') AS v FROM system.iota";
}

# Note: s4_parity.ps1 also has an $oaCorrectCases bucket (SAP's own pmsys DD
# stores some UDF bodies truncated, so SAP fails where OpenADS is right). mp has
# no such case, so that bucket and its loop are omitted here rather than left
# declared-and-unused.

$pass = 0; $fail = 0

foreach ($k in $cases.Keys) {
    $q = $cases[$k]
    $a = (& $dd --lib $SapLib --db $SapDb --user $user --password $pw --sql $q 2>&1) -join " "
    $b = (& $dd --lib $OaLib  --db $OaDb  --user $user --password $pw --sql $q 2>&1) -join " "
    if ($a -ceq $b) {
        "PASS  {0,-22} {1}" -f $k, ($b.Substring(0, [Math]::Min(90, $b.Length)))
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
