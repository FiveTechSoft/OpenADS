# S3 cursor-semantics probe battery. Default: SAP ace64.dll (oracle).
#   .\s3_probes.ps1                     → SAP
#   .\s3_probes.ps1 -Lib <openace64.dll> -Db <dir>   → OpenADS parity run
# Each probe is a self-contained script; output is one labeled JSON line.
# Table t1(id INTEGER, name CHAR(10)) with rows (1,aa)(2,bb)(3,cc) pre-exists.
param(
    [string]$Lib = "F:\ads11\ace64.dll",
    [string]$Db  = "F:\tmp\s3probe"
)
$dd = ".\dd_meta_dump.exe"
$sap = $Lib
$db = $Db

$probes = [ordered]@{

# --- basic lifecycle -------------------------------------------------------
"C1_basic_open_fetch_field" =
  "DECLARE c CURSOR AS SELECT * FROM t1; OPEN c; FETCH c; SELECT c.name FROM system.iota";

"C2_while_fetch_endwhile" =
  "DECLARE c CURSOR AS SELECT * FROM t1; DECLARE @n INTEGER; @n = 0; OPEN c; WHILE FETCH c DO @n = @n + c.id; END WHILE; CLOSE c; SELECT @n FROM system.iota";

"C3_while_fetch_end_semi" =
  "DECLARE c CURSOR AS SELECT * FROM t1; DECLARE @n INTEGER; @n = 0; OPEN c; WHILE FETCH c DO @n = @n + c.id; END; CLOSE c; SELECT @n FROM system.iota";

# --- FETCH forms (Q5) ------------------------------------------------------
"C4_fetch_next" =
  "DECLARE c CURSOR AS SELECT * FROM t1; OPEN c; FETCH NEXT c; SELECT c.id FROM system.iota";

"C5_fetch_into" =
  "DECLARE c CURSOR AS SELECT * FROM t1; DECLARE @v INTEGER; OPEN c; FETCH c INTO @v; SELECT @v FROM system.iota";

# --- EOF behavior ----------------------------------------------------------
"C6_fetch_past_eof_then_field" =
  "DECLARE c CURSOR AS SELECT * FROM t1; OPEN c; FETCH c; FETCH c; FETCH c; FETCH c; SELECT c.id FROM system.iota";

"C7_field_before_first_fetch" =
  "DECLARE c CURSOR AS SELECT * FROM t1; OPEN c; SELECT c.id FROM system.iota";

"C8_field_without_open" =
  "DECLARE c CURSOR AS SELECT * FROM t1; SELECT c.id FROM system.iota";

# --- OPEN/CLOSE edge cases -------------------------------------------------
"C9_open_twice" =
  "DECLARE c CURSOR AS SELECT * FROM t1; OPEN c; OPEN c; FETCH c; SELECT c.id FROM system.iota";

"C10_close_not_open" =
  "DECLARE c CURSOR AS SELECT * FROM t1; CLOSE c; SELECT 1 FROM system.iota";

"C11_reopen_after_close_rescans" =
  "DECLARE c CURSOR AS SELECT * FROM t1; DECLARE @n INTEGER; @n = 0; OPEN c; WHILE FETCH c DO @n = @n + 1; END WHILE; CLOSE c; OPEN c; WHILE FETCH c DO @n = @n + 1; END WHILE; CLOSE c; SELECT @n FROM system.iota";

"C12_declare_no_as_open_as" =
  "DECLARE c CURSOR; OPEN c AS SELECT * FROM t1; FETCH c; SELECT c.name FROM system.iota";

"C13_declare_no_as_open_bare" =
  "DECLARE c CURSOR; OPEN c; SELECT 1 FROM system.iota";

"C14_open_undeclared" =
  "OPEN c AS SELECT * FROM t1; FETCH c; SELECT c.id FROM system.iota";

"C15_open_as_rebinds" =
  "DECLARE c CURSOR AS SELECT * FROM t1 WHERE id = 1; OPEN c AS SELECT * FROM t1 WHERE id = 3; FETCH c; SELECT c.id FROM system.iota";

# --- field access forms ----------------------------------------------------
"C16_bracketed_field" =
  "DECLARE c CURSOR AS SELECT id, name AS [my name] FROM t1; OPEN c; FETCH c; SELECT c.[my name] FROM system.iota";

"C17_at_prefixed_cursor" =
  "DECLARE @c CURSOR AS SELECT * FROM t1; OPEN @c; FETCH @c; SELECT @c.name FROM system.iota";

"C18_case_insensitive_names" =
  "DECLARE AllCols CURSOR AS SELECT * FROM t1; OPEN allcols; FETCH ALLCOLS; SELECT allCols.NAME FROM system.iota";

"C19_unknown_field" =
  "DECLARE c CURSOR AS SELECT * FROM t1; OPEN c; FETCH c; SELECT c.nosuch FROM system.iota";

# --- cursor field in expressions / embedded SQL ----------------------------
"C20_field_in_assignment_expr" =
  "DECLARE c CURSOR AS SELECT * FROM t1; DECLARE @s CHAR(20); OPEN c; FETCH c; @s = 'x' + TRIM(c.name); SELECT @s FROM system.iota";

"C21_field_in_embedded_insert" =
  "CREATE TABLE t2 (id INTEGER); DECLARE c CURSOR AS SELECT * FROM t1; OPEN c; WHILE FETCH c DO INSERT INTO t2 (id) VALUES (c.id); END WHILE; CLOSE c; SELECT COUNT(*) FROM t2";

"C22_field_in_embedded_where" =
  "DECLARE c CURSOR AS SELECT * FROM t1 WHERE id = 2; DECLARE @s CHAR(10); OPEN c; FETCH c; @s = (SELECT name FROM t1 WHERE id = c.id); SELECT @s FROM system.iota";

# --- cursor over EXECUTE PROCEDURE ----------------------------------------
"C23_cursor_over_exec_proc" =
  "DECLARE c CURSOR AS EXECUTE PROCEDURE sp_mgGetAllTables(); OPEN c; FETCH c; SELECT 'opened-ok' FROM system.iota";

# --- FETCH as condition/expression ----------------------------------------
"C24_if_fetch" =
  "DECLARE c CURSOR AS SELECT * FROM t1; OPEN c; IF FETCH c THEN SELECT 'got-row' FROM system.iota; ENDIF";

"C25_fetch_returns_false_at_end" =
  "DECLARE c CURSOR AS SELECT * FROM t1 WHERE id = 99; DECLARE @r CHAR(5); OPEN c; IF FETCH c THEN @r = 'yes'; ELSE @r = 'no'; ENDIF; SELECT @r FROM system.iota";

# --- updatability (corpus never uses; quick check) -------------------------
"C26_update_where_current_of" =
  "DECLARE c CURSOR AS SELECT * FROM t1; OPEN c; FETCH c; UPDATE t1 SET name = 'zz' WHERE CURRENT OF c; SELECT 1 FROM system.iota";

# --- nested cursors (pmsys sp_mgGetAllLocks pattern) -----------------------
"C27_nested_reopen_in_loop" =
  "DECLARE a CURSOR AS SELECT id FROM t1; DECLARE b CURSOR; DECLARE @n INTEGER; @n = 0; OPEN a; WHILE FETCH a DO OPEN b AS SELECT * FROM t1 WHERE id >= a.id; WHILE FETCH b DO @n = @n + 1; END WHILE; CLOSE b; END WHILE; CLOSE a; SELECT @n FROM system.iota";
}

foreach ($k in $probes.Keys) {
    $out = & $dd --lib $sap --db $db --sql $probes[$k] 2>&1
    "{0,-36} {1}" -f $k, ($out -join " ")
}
