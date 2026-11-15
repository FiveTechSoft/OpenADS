# vfp_cdx_check

Checks the `.cdx` indexes in a folder with Visual FoxPro 9 itself. Use it
after a reindex (or after changing the CDX writer) to confirm VFP accepts
what OpenADS wrote. Requires VFP 9 on the machine.

```
tools\vfp_cdx_check\vfp_cdx_check.bat D:\copy\of\data
tools\vfp_cdx_check\vfp_cdx_check.bat D:\copy\of\data TRANS.dbf
```

Run it against a **copy** of the data. Tables are opened read-only
(`NOUPDATE`), but a copy avoids lock contention with a running server.

## What it checks

For every table in the folder that has tags, VFP opens the table (the
structural CDX opens with it). For each tag:

| Check | How |
|---|---|
| Count | walks the tag and compares the entry count with `COUNT FOR <FOR expr>` (`RECCOUNT()` for an unconditional tag) |
| Order | evaluates the key on every walked row; each must be >= the previous (<= for a `DESCENDING` tag) |
| Seek | jumps to ~300 records spread across the table and `SEEK`s each one's key through the tag |

Keys, FOR results and seeks all come from VFP's own engine.

## Reading the report

The report goes to `<folder>\vfp_cdx_check.txt` (or the third argument).

```
TENDHIST.DBF  recs=76815 tags=8 cdx=TENDHIST.CDX
ok   TICK       key=TICK for=.NOT.DELETED()  walked=76809 expected=76809 orderErr=0 seekMiss=0/301
BAD  CCN        key=TYPE+RIGHT(TRIM(CCN),4)  walked=76809 expected=76815 orderErr=0 seekMiss=1/301
AUTODIST.DBF  OPEN FAILED 114 Index does not match the table. ...
SUMMARY tables=40 tags=115 ok=115 bad=0 err=0 openfail=0
```

- `ok`: the tag passed every check.
- `BAD`: `walked` vs `expected` is a count mismatch (missing entries, or a
  FOR clause VFP does not see); `orderErr` counts out-of-order keys;
  `seekMiss` counts keys VFP could not find (usually a wrong key width or
  key value).
- `ERR`: VFP raised an error on the tag. A key or FOR expression that calls
  an application function (a Harbour UDF) cannot be evaluated by VFP and
  lands here. That is expected and not an index defect.
- `OPEN FAILED`: VFP refused the table or its CDX (error 114 = the index
  header does not match what VFP expects).

The exit code is 0 when every tag is `ok`, 1 when anything is `BAD`, `ERR`
or `OPEN FAILED`, and 2 for usage errors or when no report was written.

A lone `orderErr=1` on a date-based tag usually means one record holds a
corrupt date (bytes that are not `YYYYMMDD`): VFP's `DTOS()` reads such a
value differently from the raw bytes stored in the key. Check the data
before suspecting the index.
