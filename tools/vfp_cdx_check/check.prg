* vfp_cdx_check -- verify CDX indexes with Visual FoxPro 9 itself.
*
* Run through vfp_cdx_check.bat (it sets the environment variables below
* and starts "vfp9.exe -t check.prg"). For every table in the folder VFP
* opens the table read-only (NOUPDATE; the structural CDX opens with it),
* then for every tag:
*   - walks the tag and compares the entry count with COUNT FOR <FOR expr>
*     (RECCOUNT() for an unconditional tag),
*   - evaluates the key on every walked row and checks the order
*     (ascending, or descending for a DESCENDING tag),
*   - SEEKs the key of ~300 records spread over the table.
* Keys, FOR results and seeks are all computed by VFP's own engine.
*
* Environment:
*   VFPCHK_DIR   folder holding the .dbf/.cdx files (required)
*   VFPCHK_OUT   result file (default: <VFPCHK_DIR>\vfp_cdx_check.txt)
*   VFPCHK_MASK  table mask (default: *.dbf)
*
* Result lines:
*   ok   <tag> ...   tag passed every check
*   BAD  <tag> ...   count / order / seek mismatch (numbers on the line)
*   ERR  <tag> ...   VFP raised an error on the tag (message on the line)
*   <table> OPEN FAILED <n> <message>   VFP refused the table or its CDX
*   SUMMARY tables=.. tags=.. ok=.. bad=.. err=.. openfail=..
LOCAL lcDir, lcMask, laF[1], lnF, i
SET TALK OFF
SET SAFETY OFF
SET NOTIFY OFF
SET RESOURCE OFF
SET EXCLUSIVE OFF
SET DELETED OFF
SET EXACT ON
SET COLLATE TO "MACHINE"
SET CPDIALOG OFF
SET TABLEVALIDATE TO 0
PUBLIC gcOut, gnTables, gnTags, gnOk, gnBad, gnErr, gnOpenFail
STORE 0 TO gnTables, gnTags, gnOk, gnBad, gnErr, gnOpenFail
lcDir = ALLTRIM(GETENV("VFPCHK_DIR"))
IF EMPTY(lcDir)
	QUIT
ENDIF
lcDir = ADDBS(lcDir)
gcOut = ALLTRIM(GETENV("VFPCHK_OUT"))
IF EMPTY(gcOut)
	gcOut = lcDir + "vfp_cdx_check.txt"
ENDIF
lcMask = ALLTRIM(GETENV("VFPCHK_MASK"))
IF EMPTY(lcMask)
	lcMask = "*.dbf"
ENDIF
ERASE (gcOut)
ON ERROR DO logerr WITH ERROR(), MESSAGE(), LINENO()
lnF = ADIR(laF, lcDir + lcMask)
FOR i = 1 TO lnF
	DO chktable WITH lcDir + laF[i, 1]
ENDFOR
DO out WITH "SUMMARY tables=" + TRANSFORM(gnTables) + " tags=" + TRANSFORM(gnTags) + ;
	" ok=" + TRANSFORM(gnOk) + " bad=" + TRANSFORM(gnBad) + " err=" + TRANSFORM(gnErr) + ;
	" openfail=" + TRANSFORM(gnOpenFail)
QUIT

PROCEDURE out(tc)
	=STRTOFILE(tc + CHR(13) + CHR(10), gcOut, .T.)
ENDPROC

PROCEDURE logerr(tnErr, tcMsg, tnLine)
	DO out WITH "   !! ON ERROR " + TRANSFORM(tnErr) + " " + tcMsg + " line " + TRANSFORM(tnLine)
ENDPROC

PROCEDURE chktable(tcT)
	LOCAL lnTags, j, loEx
	CLOSE TABLES ALL
	TRY
		USE (tcT) IN 0 ALIAS t NOUPDATE
	CATCH TO loEx
		gnOpenFail = gnOpenFail + 1
		DO out WITH JUSTFNAME(tcT) + "  OPEN FAILED " + TRANSFORM(loEx.ErrorNo) + " " + loEx.Message
	ENDTRY
	IF !USED("t")
		RETURN
	ENDIF
	SELECT t
	lnTags = TAGCOUNT()
	IF lnTags = 0
		USE IN t
		RETURN
	ENDIF
	gnTables = gnTables + 1
	DO out WITH JUSTFNAME(tcT) + "  recs=" + TRANSFORM(RECCOUNT()) + " tags=" + TRANSFORM(lnTags) + " cdx=" + JUSTFNAME(CDX(1))
	FOR j = 1 TO lnTags
		DO chktag WITH j
	ENDFOR
	USE IN t
ENDPROC

PROCEDURE chktag(tnTag)
	LOCAL lcTag, lcKey, lcFor, llDesc, llUniq, loEx, lnWalk, lnExp, lnBad, luPrev, luCur, llFirst, lnSeekBad, lnSeekN, lnR, lcRes
	gnTags = gnTags + 1
	lcTag = TAG(tnTag)
	lcKey = KEY(tnTag)
	lcFor = FOR(tnTag)
	llDesc = DESCENDING(tnTag)
	llUniq = UNIQUE(tnTag)
	lcRes = "  " + PADR(lcTag, 10) + " key=" + lcKey + IIF(EMPTY(lcFor), "", " for=" + lcFor) + IIF(llDesc, " DESC", "") + IIF(llUniq, " UNIQUE", "")
	TRY
		SET ORDER TO TAG (lcTag)
		IF EMPTY(lcFor)
			lnExp = RECCOUNT()
		ELSE
			SET ORDER TO
			COUNT FOR EVALUATE(lcFor) TO lnExp
			SET ORDER TO TAG (lcTag)
		ENDIF
		lnWalk = 0
		lnBad = 0
		llFirst = .T.
		GO TOP
		SCAN
			lnWalk = lnWalk + 1
			luCur = EVALUATE(lcKey)
			IF !llFirst
				IF IIF(llDesc, luCur > luPrev, luCur < luPrev)
					lnBad = lnBad + 1
				ENDIF
			ENDIF
			llFirst = .F.
			luPrev = luCur
		ENDSCAN
		lnSeekBad = 0
		lnSeekN = 0
		SET ORDER TO
		FOR lnR = 1 TO RECCOUNT() STEP MAX(1, INT(RECCOUNT() / 300))
			GO lnR
			IF !EMPTY(lcFor) AND !EVALUATE(lcFor)
				LOOP
			ENDIF
			luCur = EVALUATE(lcKey)
			lnSeekN = lnSeekN + 1
			IF !SEEK(luCur, "t", lcTag)
				lnSeekBad = lnSeekBad + 1
			ENDIF
		ENDFOR
		SET ORDER TO TAG (lcTag)
		lcRes = lcRes + "  walked=" + TRANSFORM(lnWalk) + IIF(llUniq, "", " expected=" + TRANSFORM(lnExp)) + " orderErr=" + TRANSFORM(lnBad) + " seekMiss=" + TRANSFORM(lnSeekBad) + "/" + TRANSFORM(lnSeekN)
		IF (!llUniq AND lnWalk <> lnExp) OR lnBad > 0 OR lnSeekBad > 0
			gnBad = gnBad + 1
			lcRes = "BAD" + lcRes
		ELSE
			gnOk = gnOk + 1
			lcRes = "ok " + lcRes
		ENDIF
	CATCH TO loEx
		gnErr = gnErr + 1
		lcRes = "ERR" + lcRes + "  -> " + TRANSFORM(loEx.ErrorNo) + " " + loEx.Message
	ENDTRY
	DO out WITH lcRes
ENDPROC
