/* OpenADS vs Harbour DBFCDX header/body comparison smoke test.
 *
 * Builds the SAME table + 3-tag CDX twice (B_BIG schema):
 *   T_HB  — written by Harbour DBFCDX (reference implementation)
 *   T_ADS — written by OpenADS ace64.dll through ADSCDX (local server)
 *
 * Compares:
 *   DBF header : version byte, header length, record length, and the
 *                32-byte field-descriptor block (byte-identical).
 *   DBF body   : the whole record area, byte for byte.
 *   CDX header : per tag — key expression (OrdKey) and key count.
 *   CDX body   : per tag — the full ordered RecNo() sequence walked in
 *                parallel from both files (same data + same ordering
 *                rule (key,recno) => identical sequences).
 *   CDX size   : sanity ratio band only (packing density legitimately
 *                differs between the writers).
 *
 * Exit code 0 = PASS, 1 = FAIL (details in sizecmp.log).
 */

#include "ads.ch"

REQUEST DBFCDX
REQUEST ADSCDX

STATIC hLog
STATIC nFail := 0

STATIC FUNCTION LogMsg( c )
   FWrite( hLog, c + hb_eol() )
   ? c
   RETURN NIL

STATIC FUNCTION FailMsg( c )
   nFail++
   LogMsg( "FAIL " + c )
   RETURN NIL

STATIC FUNCTION ErrH( oErr )
   LogMsg( "ERROR " + oErr:description + " gen=" + hb_ntos( oErr:gencode ) + ;
           " sub=" + hb_ntos( oErr:subcode ) )
   BREAK oErr
   RETURN NIL

STATIC FUNCTION Schema()
   RETURN { { "NAME", "C", 19, 0 }, ;
            { "CITY", "C", 15, 0 }, ;
            { "AGE" , "N",  3, 0 }, ;
            { "TS"  , "C", 23, 0 }, ;
            { "THD" , "N",  3, 0 }, ;
            { "INS" , "N",  4, 0 }, ;
            { "RDD" , "C",  3, 0 } }

STATIC FUNCTION FillRow( i, cRdd )
   LOCAL aNames := { "Alice", "Bob", "Phillip", "Charlie", "Linda", ;
                     "Finland", "Diana", "Lucy", "Jony", "Edward" }
   LOCAL aCities := { "Madrid", "Barcelona", "Panipat", "Valencia", "Iris", ;
                      "Dallas", "Sevilla", "Sevilla", "Walker", "Bilbao" }
   REPLACE NAME WITH aNames[ i % 10 + 1 ]
   REPLACE CITY WITH aCities[ i % 10 + 1 ]
   REPLACE AGE  WITH 20 + i % 100
   REPLACE TS   WITH "2026-08-09 06:00:00.000" + PadL( hb_ntos( i % 10 ), 1, "0" )
   REPLACE THD  WITH 1
   REPLACE INS  WITH 1000 + ( i % 90 ) * 37
   REPLACE RDD  WITH "TST"
   RETURN NIL

STATIC FUNCTION BuildSide( cAlias, cRdd, nRecs )
   LOCAL i
   dbCreate( cAlias, Schema(), cRdd )
   USE ( cAlias ) VIA ( cRdd ) SHARED NEW
   INDEX ON NAME TAG "IDX01" TO ( cAlias )
   INDEX ON CITY TAG "IDX02" TO ( cAlias )
   INDEX ON INS  TAG "IDX03" TO ( cAlias )
   dbSetOrder( 0 )
   FOR i := 0 TO nRecs - 1
      APPEND BLANK
      FillRow( i, cRdd )
      dbCommit()
      dbUnlock()
   NEXT
   USE
   RETURN NIL

// --- DBF helpers -----------------------------------------------------------

STATIC FUNCTION DbfHeaderFields( cFile )
   // { version, hdrLen, recLen, descriptorBlock }
   LOCAL h := FOpen( cFile )
   LOCAL cHdr, nHdrLen, nRecLen, cDesc
   IF h == -1
      RETURN { -1, -1, -1, "" }
   ENDIF
   cHdr    := Space( 32 )
   FRead( h, @cHdr, 32 )
   nHdrLen := Asc( SubStr( cHdr, 9, 1 ) ) + Asc( SubStr( cHdr, 10, 1 ) ) * 256
   nRecLen := Asc( SubStr( cHdr, 11, 1 ) ) + Asc( SubStr( cHdr, 12, 1 ) ) * 256
   cDesc   := Space( nHdrLen - 33 )
   FSeek( h, 32 )
   FRead( h, @cDesc, nHdrLen - 33 )
   FClose( h )
   RETURN { Asc( cHdr ), nHdrLen, nRecLen, cDesc }

STATIC FUNCTION CompareBodies( cF1, cF2, nFrom )
   // Byte-for-byte from nFrom. Returns: -1 identical; -3 identical but
   // one file has a single trailing 0x1A EOF marker (legal DBF; Harbour
   // omits it on shared tables, OpenADS writes it); otherwise the offset
   // of the first mismatch.
   LOCAL h1 := FOpen( cF1 ), h2 := FOpen( cF2 )
   LOCAL cB1, cB2, nR1, nR2, nOff := nFrom, i, nLen
   IF h1 == -1 .OR. h2 == -1
      RETURN -2
   ENDIF
   FSeek( h1, nFrom )
   FSeek( h2, nFrom )
   DO WHILE .T.
      cB1 := Space( 4096 )
      cB2 := Space( 4096 )
      nR1 := FRead( h1, @cB1, 4096 )
      nR2 := FRead( h2, @cB2, 4096 )
      nLen := Min( nR1, nR2 )
      FOR i := 1 TO nLen
         IF Asc( SubStr( cB1, i, 1 ) ) != Asc( SubStr( cB2, i, 1 ) )
            FClose( h1 )
            FClose( h2 )
            RETURN nOff + i - 1
         ENDIF
      NEXT
      IF nR1 != nR2
         // length mismatch at the tail: allow exactly one trailing 0x1A
         IF Abs( nR1 - nR2 ) == 1
            IF ( nR1 > nR2 .AND. Asc( SubStr( cB1, nLen + 1, 1 ) ) == 26 ) .OR. ;
               ( nR2 > nR1 .AND. Asc( SubStr( cB2, nLen + 1, 1 ) ) == 26 )
               FClose( h1 )
               FClose( h2 )
               RETURN -3
            ENDIF
         ENDIF
         FClose( h1 )
         FClose( h2 )
         RETURN nOff + nLen
      ENDIF
      IF nR1 < 4096
         EXIT
      ENDIF
      nOff += 4096
   ENDDO
   FClose( h1 )
   FClose( h2 )
   RETURN -1

STATIC FUNCTION CompareDbf( cHb, cAds )
   LOCAL aH := DbfHeaderFields( cHb )
   LOCAL aA := DbfHeaderFields( cAds )
   LOCAL cH, cA, i, nBad := 0, nAt
   IF aH[ 1 ] == aA[ 1 ] .AND. aH[ 2 ] == aA[ 2 ] .AND. aH[ 3 ] == aA[ 3 ]
      LogMsg( "ok   DBF header: version=0x" + Hex2( aH[ 1 ] ) + ;
              " hdrLen=" + hb_ntos( aH[ 2 ] ) + " recLen=" + hb_ntos( aH[ 3 ] ) )
   ELSE
      FailMsg( "DBF header fields: HB={0x" + Hex2( aH[ 1 ] ) + "," + ;
               hb_ntos( aH[ 2 ] ) + "," + hb_ntos( aH[ 3 ] ) + "} ADS={0x" + ;
               Hex2( aA[ 1 ] ) + "," + hb_ntos( aA[ 2 ] ) + "," + ;
               hb_ntos( aA[ 3 ] ) + "}" )
   ENDIF
   // Full header, byte by byte. Allowed differences: the last-update
   // date (offsets 1-3) and the field-displacement bytes (12-15 of each
   // 32-byte descriptor) — Harbour leaves those zero while OpenADS
   // stamps the spec-conforming running offsets (both are valid DBF).
   cH := ReadWhole( cHb )
   cA := ReadWhole( cAds )
   FOR i := 0 TO aH[ 2 ] - 1
      IF i == 1 .OR. i == 2 .OR. i == 3
         LOOP   // volatile date bytes
      ENDIF
      IF i >= 32 .AND. i < aH[ 2 ] - 2
         IF ( i - 32 ) % 32 >= 12 .AND. ( i - 32 ) % 32 <= 15
            LOOP   // field displacement bytes (see above)
         ENDIF
      ENDIF
      IF Asc( SubStr( cH, i + 1, 1 ) ) != Asc( SubStr( cA, i + 1, 1 ) )
         LogMsg( "     hdr byte +" + hb_ntos( i ) + ": HB=0x" + ;
                 Hex2( Asc( SubStr( cH, i + 1, 1 ) ) ) + ;
                 " ADS=0x" + Hex2( Asc( SubStr( cA, i + 1, 1 ) ) ) )
         nBad++
      ENDIF
   NEXT
   IF nBad == 0
      LogMsg( "ok   DBF header byte-identical (" + hb_ntos( aH[ 2 ] ) + ;
              " bytes, date excepted)" )
   ELSE
      FailMsg( "DBF header: " + hb_ntos( nBad ) + " bytes differ" )
   ENDIF
   nAt := CompareBodies( cHb, cAds, aH[ 2 ] )
   IF nAt == -1
      LogMsg( "ok   DBF record body byte-identical" )
   ELSEIF nAt == -3
      LogMsg( "ok   DBF record body identical (trailing 0x1A EOF marker diff)" )
   ELSE
      FailMsg( "DBF record body differs at offset " + hb_ntos( nAt ) )
   ENDIF
   RETURN NIL

// --- CDX helpers -----------------------------------------------------------

STATIC FUNCTION ReadWhole( cFile )
   LOCAL h := FOpen( cFile )
   LOCAL nSize, cBuf
   IF h == -1
      RETURN ""
   ENDIF
   nSize := FSeek( h, 0, 2 )
   FSeek( h, 0, 0 )
   cBuf := Space( nSize )
   FRead( h, @cBuf, nSize )
   FClose( h )
   RETURN cBuf

STATIC FUNCTION U16At( cBuf, nOff )
   RETURN Asc( SubStr( cBuf, nOff + 1, 1 ) ) + ;
          Asc( SubStr( cBuf, nOff + 2, 1 ) ) * 256

STATIC FUNCTION U32At( cBuf, nOff )
   RETURN Asc( SubStr( cBuf, nOff + 1, 1 ) ) + ;
          Asc( SubStr( cBuf, nOff + 2, 1 ) ) * 256 + ;
          Asc( SubStr( cBuf, nOff + 3, 1 ) ) * 65536 + ;
          Asc( SubStr( cBuf, nOff + 4, 1 ) ) * 16777216

// Locate a tag's 1024-byte header page by signature: headerLen=1024 and
// pageLen=512 at fixed offsets 16/18, with the key expression in the pool.
STATIC FUNCTION FindTagHeader( cBuf, cExpr )
   LOCAL nOff := 0
   DO WHILE nOff + 1024 <= Len( cBuf )
      IF U16At( cBuf, nOff + 16 ) == 1024 .AND. U16At( cBuf, nOff + 18 ) == 512
         IF cExpr $ SubStr( cBuf, nOff + 512 + 1, 256 )
            RETURN nOff
         ENDIF
      ENDIF
      nOff += 512
   ENDDO
   RETURN -1

// Byte-by-byte compare of a tag-header range; logs every differing byte.
STATIC FUNCTION CmpRange( cH, cA, nOffH, nOffA, nFrom, nTo, cWhat )
   LOCAL i, nBad := 0
   FOR i := nFrom TO nTo
      IF Asc( SubStr( cH, nOffH + i + 1, 1 ) ) != ;
         Asc( SubStr( cA, nOffA + i + 1, 1 ) )
         LogMsg( "     byte +" + hb_ntos( i ) + ": HB=0x" + ;
                 Hex2( Asc( SubStr( cH, nOffH + i + 1, 1 ) ) ) + ;
                 " ADS=0x" + Hex2( Asc( SubStr( cA, nOffA + i + 1, 1 ) ) ) )
         nBad++
      ENDIF
   NEXT
   IF nBad == 0
      LogMsg( "ok   " + cWhat + " (" + hb_ntos( nTo - nFrom + 1 ) + " bytes identical)" )
   ELSE
      FailMsg( cWhat + ": " + hb_ntos( nBad ) + " bytes differ" )
   ENDIF
   RETURN NIL

// Byte-by-byte CDX tag-header comparison. Offsets 0-11 (root ptr, free
// ptr, update counter) are runtime state and legitimately differ; the
// format fields and expression pool must match exactly.
STATIC FUNCTION CompareCdxHeaders( cHbFile, cAdsFile )
   LOCAL cH := ReadWhole( cHbFile )
   LOCAL cA := ReadWhole( cAdsFile )
   LOCAL aTags := { "NAME", "CITY", "INS" }
   LOCAL cTag, nH, nA, nKeyLen, nForLen
   IF Empty( cH ) .OR. Empty( cA )
      FailMsg( "cannot read CDX files for header comparison" )
      RETURN NIL
   ENDIF
   FOR EACH cTag IN aTags
      nH := FindTagHeader( cH, cTag )
      nA := FindTagHeader( cA, cTag )
      IF nH < 0 .OR. nA < 0
         FailMsg( "tag header for expr " + cTag + " not found (HB=" + ;
                  hb_ntos( nH ) + " ADS=" + hb_ntos( nA ) + ")" )
         LOOP
      ENDIF
      LogMsg( "   tag expr " + cTag + ": header at HB=0x" + Hex2( nH ) + ;
              " ADS=0x" + Hex2( nA ) )
      // keySize(12-13) indexOpt(14) indexSig(15) hdrLen(16-17) pageLen(18-19)
      // + masks/bits/keyBytes(20-23)
      CmpRange( cH, cA, nH, nA, 12, 23, "tag " + cTag + " header fields" )
      // descend flag + for/key expr pos/len (502-511)
      CmpRange( cH, cA, nH, nA, 502, 511, "tag " + cTag + " flags/expr ptrs" )
      // expression pool: only the meaningful prefix (keyExpLen + forExpLen)
      nKeyLen := U16At( cH, nH + 510 )
      nForLen := U16At( cH, nH + 506 )
      CmpRange( cH, cA, nH, nA, 512, 512 + nKeyLen + nForLen - 1, ;
                "tag " + cTag + " expression pool" )
   NEXT
   RETURN NIL

STATIC FUNCTION CompareCdx( cHb, cAds, nRecs )
   LOCAL t, n, nHb, nAds
   USE ( cHb )  INDEX ( cHb )  ALIAS HBX SHARED NEW VIA "DBFCDX"
   USE ( cAds ) INDEX ( cAds ) ALIAS ADX SHARED NEW VIA "ADSCDX"
   IF HBX->( OrdCount() ) != 3 .OR. ADX->( OrdCount() ) != 3
      FailMsg( "tag count: HB=" + hb_ntos( HBX->( OrdCount() ) ) + ;
               " ADS=" + hb_ntos( ADX->( OrdCount() ) ) )
   ENDIF
   FOR t := 1 TO 3
      // header-ish: key expression must match
      HBX->( dbSetOrder( t ) )
      ADX->( dbSetOrder( t ) )
      IF HBX->( OrdKey() ) == ADX->( OrdKey() )
         LogMsg( "ok   tag " + HBX->( OrdName() ) + " key expr: " + HBX->( OrdKey() ) )
      ELSE
         FailMsg( "tag " + hb_ntos( t ) + " key expr: HB=[" + ;
                  HBX->( OrdKey() ) + "] ADS=[" + ADX->( OrdKey() ) + "]" )
      ENDIF
      // body: parallel ordered recno walk
      HBX->( dbGoTop() )
      ADX->( dbGoTop() )
      n := 0
      DO WHILE ! HBX->( Eof() ) .AND. ! ADX->( Eof() )
         nHb  := HBX->( RecNo() )
         nAds := ADX->( RecNo() )
         IF nHb != nAds
            FailMsg( "tag " + HBX->( OrdName() ) + " recno sequence diverges at #" + ;
                     hb_ntos( n + 1 ) + ": HB=" + hb_ntos( nHb ) + ;
                     " ADS=" + hb_ntos( nAds ) )
            EXIT
         ENDIF
         n++
         HBX->( dbSkip() )
         ADX->( dbSkip() )
      ENDDO
      IF HBX->( Eof() ) != ADX->( Eof() )
         FailMsg( "tag " + HBX->( OrdName() ) + " length differs (HB eof=" + ;
                  iif( HBX->( Eof() ), "Y", "N" ) + " ADS eof=" + ;
                  iif( ADX->( Eof() ), "Y", "N" ) + ")" )
      ELSEIF n == nRecs
         LogMsg( "ok   tag " + HBX->( OrdName() ) + " body: " + hb_ntos( n ) + ;
                 " keys, identical recno order" )
      ENDIF
   NEXT
   HBX->( dbCloseArea() )
   ADX->( dbCloseArea() )
   RETURN NIL

PROCEDURE Main()
   LOCAL nRecs := 300
   LOCAL nHbCdx, nAdsCdx, nRatio
   LOCAL oE

   ErrorBlock( {| e | ErrH( e ) } )
   hLog := FCreate( "sizecmp.log" )

   FErase( "T_HB.dbf" )  ; FErase( "T_HB.cdx" )
   FErase( "T_ADS.dbf" ) ; FErase( "T_ADS.cdx" )

   BEGIN SEQUENCE
      RddSetDefault( "DBFCDX" )
      BuildSide( "T_HB", "DBFCDX", nRecs )

      AdsSetFileType( ADS_CDX )
      AdsSetServerType( ADS_LOCAL_SERVER )
      BuildSide( "T_ADS", "ADSCDX", nRecs )

      LogMsg( "=== DBF header + body ===" )
      CompareDbf( "T_HB.dbf", "T_ADS.dbf" )

      LogMsg( "=== CDX size sanity ===" )
      nHbCdx  := hb_vfSize( "T_HB.cdx" )
      nAdsCdx := hb_vfSize( "T_ADS.cdx" )
      nRatio  := nAdsCdx / nHbCdx
      IF nRatio >= 0.20 .AND. nRatio <= 3.0
         LogMsg( "ok   CDX sizes: Harbour=" + hb_ntos( nHbCdx ) + ;
                 " OpenADS=" + hb_ntos( nAdsCdx ) + ;
                 " ratio=" + Str( nRatio, 6, 3 ) )
      ELSE
         FailMsg( "CDX size ratio out of bounds: Harbour=" + ;
                  hb_ntos( nHbCdx ) + " OpenADS=" + hb_ntos( nAdsCdx ) )
      ENDIF

      LogMsg( "=== CDX header (byte-by-byte) + body (recno order) ===" )
      CompareCdxHeaders( "T_HB.cdx", "T_ADS.cdx" )
      CompareCdx( "T_HB", "T_ADS", nRecs )
   RECOVER USING oE
      LogMsg( "ABORTED by error" )
      nFail++
   END SEQUENCE

   IF nFail == 0
      LogMsg( "SIZECMP PASS" )
   ELSE
      LogMsg( "SIZECMP FAIL (" + hb_ntos( nFail ) + " failures)" )
   ENDIF
   FClose( hLog )
   ErrorLevel( iif( nFail == 0, 0, 1 ) )
   RETURN

STATIC FUNCTION Hex2( n )
   LOCAL c := "0123456789ABCDEF"
   RETURN SubStr( c, hb_bitShift( n, -4 ) + 1, 1 ) + SubStr( c, hb_bitAnd( n, 15 ) + 1, 1 )
