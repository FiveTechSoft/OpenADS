/*
 * openads_cdx_invoice_fixture.prg
 *
 * Build a set of related DBF/CDX tables for OpenADS smoke tests:
 *
 *   <exe-dir>\..\..\..\testdata\invoices\
 *     customer.dbf / .cdx   - 100 customers
 *     items.dbf    / .cdx   - 20 catalogue items
 *     invoices.dbf / .cdx   - 1000 invoices linked to customers
 *     invoicedetail.dbf/.cdx- 3+ detail lines per invoice
 *
 * Data directory is always relative to the executable so the fixture
 * works on any drive without editing source.
 *
 * Usage:
 *   openads_cdx_invoice_fixture.exe          -- unattended / CI
 *   openads_cdx_invoice_fixture.exe BROWSE   -- browse each table interactively
 */

#include "ads.ch"
#include "inkey.ch"

#require "rddads"
REQUEST ADS
REQUEST ADSCDX
#define DATA_RELPATH       "..\..\..\testdata\invoices"
#define CUSTOMER_COUNT     100
#define INVOICE_COUNT      1000
#define ITEM_COUNT         20
#define MIN_DETAIL_PER_INV 3
#define BOX_WIDTH          64

//----------------------------------------------------------------------------

PROCEDURE Main()
   LOCAL cDataDir := hb_PathNormalize( hb_DirBase() + DATA_RELPATH )
   LOCAL lBrowse  := ( PCount() > 0 )
   LOCAL nTotal

   SetMode( 25, 120 )
   ErrorBlock( { |oErr| MyHandler( oErr ) } )

   SET FILETYPE TO CDX
   RDDSETDEFAULT( "ADSCDX" )

   PrintBanner()
   ? "  Data dir : " + cDataDir
   ? "  Mode     : " + IIF( lBrowse, "interactive  (browse after each table)", "unattended" )
   ? "  " + Sep()

   IF ! hb_DirExists( cDataDir )
      hb_DirCreate( cDataDir )
      IF ! hb_DirExists( cDataDir )
         ? "  ERROR: Could not create data directory: " + cDataDir
         RETURN
      ENDIF
   ENDIF

   ?
   IF ! BuildTables( cDataDir, lBrowse )
      RETURN
   ENDIF

   nTotal := CUSTOMER_COUNT + ITEM_COUNT + INVOICE_COUNT + ;
             INVOICE_COUNT * MIN_DETAIL_PER_INV
   ?
   ? "  " + Sep( Chr(205) )
   ? "  Fixture complete.  4 tables  " + LTrim( Str( nTotal ) ) + " records."
   ? "  " + Sep( Chr(205) )
   RETURN

//----------------------------------------------------------------------------

STATIC FUNCTION BuildTables( cDataDir, lBrowse )
   LOCAL cLegacy  := cDataDir + "\.cdx"
   LOCAL aCustomers, aItems, aInvoices, aDetails

   /* Remove the legacy shared .cdx that old engine builds left behind
      before per-table naming was fixed. */
   IF File( cLegacy ) ; FErase( cLegacy ) ; ENDIF

   aCustomers := GenerateCustomerRows()
   aItems     := GenerateItems()
   aInvoices  := GenerateInvoices( aCustomers )
   aDetails   := GenerateInvoiceDetails( aInvoices, aItems )

   IF ! CreateCustomerTable(     cDataDir, aCustomers, lBrowse ) ; RETURN .F. ; ENDIF
   IF ! CreateItemTable(         cDataDir, aItems,     lBrowse ) ; RETURN .F. ; ENDIF
   IF ! CreateInvoiceTable(      cDataDir, aInvoices,  lBrowse ) ; RETURN .F. ; ENDIF
   IF ! CreateInvoiceDetailTable(cDataDir, aDetails,   lBrowse ) ; RETURN .F. ; ENDIF

   RETURN .T.

//----------------------------------------------------------------------------

STATIC FUNCTION CreateCustomerTable( cDataDir, aCustomers, lBrowse )
   LOCAL cFile := cDataDir + "\customer.dbf"
   LOCAL i

   ?? "  " + Chr(16) + " customer.dbf "
   DeleteExistingFixtureFiles( cFile )

   DbCreate( cFile, ;
      { { "CUSTNO",  "C",  8, 0 }, ;
        { "NAME",    "C", 50, 0 }, ;
        { "COUNTRY", "C", 24, 0 }, ;
        { "CREATED", "D",  8, 0 }, ;
        { "BALANCE", "N", 12, 2 } } )

   USE ( cFile ) ALIAS CUSTOMER SHARED NEW
   IF Select( "CUSTOMER" ) == 0
      ? "  ERROR: Failed to open " + cFile
      RETURN .F.
   ENDIF

   INDEX ON CUSTOMER->CUSTNO TAG CUSTNO TO "customer"
   INDEX ON CUSTOMER->NAME   TAG CUSTNAME TO "customer"

   FOR i := 1 TO Len( aCustomers )
      CUSTOMER->( DbAppend() )
      CUSTOMER->CUSTNO  := aCustomers[i][1]
      CUSTOMER->NAME    := aCustomers[i][2]
      CUSTOMER->COUNTRY := aCustomers[i][3]
      CUSTOMER->CREATED := aCustomers[i][4]
      CUSTOMER->BALANCE := aCustomers[i][5]
   NEXT

   CUSTOMER->( DbCommit() )
   CUSTOMER->( DbGoTop() )
   ? Str( Len(aCustomers), 5 ) + " records   [CUSTNO, CUSTNAME]"

   IF lBrowse
      SELECT CUSTOMER
      BrowseTable( "customer.dbf", Len(aCustomers) )
   ENDIF

   customer->( DbCloseArea() )
   RETURN .T.

//----------------------------------------------------------------------------

STATIC FUNCTION CreateItemTable( cDataDir, aItems, lBrowse )
   LOCAL cFile := cDataDir + "\items.dbf"
   LOCAL j

   ?? "  " + Chr(16) + " items.dbf    "
   DeleteExistingFixtureFiles( cFile )

   DbCreate( cFile, ;
      { { "ITEMNO", "C",  8, 0 }, ;
        { "DESCR",  "C", 60, 0 }, ;
        { "PRICE",  "N", 10, 2 } } )

   USE ( cFile ) ALIAS ITEMS SHARED NEW
   IF Select( "ITEMS" ) == 0
      ? "  ERROR: Failed to open " + cFile
      RETURN .F.
   ENDIF

   INDEX ON ITEMS->ITEMNO TAG ITEMNO TO "items"

   FOR j := 1 TO Len( aItems )
      ITEMS->( DbAppend() )
      ITEMS->ITEMNO := aItems[j][1]
      ITEMS->DESCR  := aItems[j][2]
      ITEMS->PRICE  := aItems[j][3]
   NEXT

   ITEMS->( DbCommit() )
   ITEMS->( DbGoTop() )
   ? Str( Len(aItems), 5 ) + " records   [ITEMNO]"

   IF lBrowse
      SELECT ITEMS
      BrowseTable( "items.dbf", Len(aItems) )
   ENDIF

   items->( DbCloseArea() )
   RETURN .T.

//----------------------------------------------------------------------------

STATIC FUNCTION CreateInvoiceTable( cDataDir, aInvoices, lBrowse )
   LOCAL cFile := cDataDir + "\invoices.dbf"
   LOCAL k

   ?? "  " + Chr(16) + " invoices.dbf "
   DeleteExistingFixtureFiles( cFile )

   DbCreate( cFile, ;
      { { "INVNO",   "C", 12, 0 }, ;
        { "CUSTNO",  "C",  8, 0 }, ;
        { "INVDATE", "D",  8, 0 }, ;
        { "TOTAL",   "N", 12, 2 }, ;
        { "STATUS",  "C",  1, 0 } } )

   USE ( cFile ) ALIAS INVOICES SHARED NEW
   IF Select( "INVOICES" ) == 0
      ? "  ERROR: Failed to open " + cFile
      RETURN .F.
   ENDIF

   INDEX ON INVOICES->INVNO   TAG INVNO TO "invoices"
   INDEX ON INVOICES->CUSTNO  TAG CUSTNO TO "invoices"
   INDEX ON INVOICES->INVDATE TAG INVDATE TO "invoices"

   FOR k := 1 TO Len( aInvoices )
      INVOICES->( DbAppend() )
      INVOICES->INVNO   := aInvoices[k][1]
      INVOICES->CUSTNO  := aInvoices[k][2]
      INVOICES->INVDATE := aInvoices[k][3]
      INVOICES->TOTAL   := aInvoices[k][4]
      INVOICES->STATUS  := aInvoices[k][5]
   NEXT

   INVOICES->( DbCommit() )
   INVOICES->( OrdSetFocus( "INVNO" ) )
   INVOICES->( DbGoTop() )
   ? Str( Len(aInvoices), 5 ) + " records   [INVNO, CUSTNO, INVDATE]"

   IF lBrowse
      SELECT INVOICES
      BrowseTable( "invoices.dbf", Len(aInvoices), ;
         { { "INVNO", 1 }, { "CUSTNO", 2 }, { "INVDATE", 3 } } )
   ENDIF

   invoices->( DbCloseArea() )
   RETURN .T.

//----------------------------------------------------------------------------

STATIC FUNCTION CreateInvoiceDetailTable( cDataDir, aDetails, lBrowse )
   LOCAL cFile := cDataDir + "\invoicedetail.dbf"
   LOCAL n

   ?? "  " + Chr(16) + " invoicedetail.dbf "
   DeleteExistingFixtureFiles( cFile )

   DbCreate( cFile, ;
      { { "INVNO",  "C", 12, 0 }, ;
        { "LINENO", "N",  4, 0 }, ;
        { "ITEMNO", "C",  8, 0 }, ;
        { "QTY",    "N",  5, 0 }, ;
        { "PRICE",  "N", 10, 2 }, ;
        { "AMOUNT", "N", 12, 2 } } )

   USE ( cFile ) ALIAS DETAIL SHARED NEW
   IF Select( "DETAIL" ) == 0
      ? "  ERROR: Failed to open " + cFile
      RETURN .F.
   ENDIF

   INDEX ON DETAIL->INVNO  TAG INVNO TO "invoicedetail"
   INDEX ON DETAIL->ITEMNO TAG ITEMIDX TO "invoicedetail"

   FOR n := 1 TO Len( aDetails )
      DETAIL->( DbAppend() )
      DETAIL->INVNO  := aDetails[n][1]
      DETAIL->LINENO := aDetails[n][2]
      DETAIL->ITEMNO := aDetails[n][3]
      DETAIL->QTY    := aDetails[n][4]
      DETAIL->PRICE  := aDetails[n][5]
      DETAIL->AMOUNT := aDetails[n][6]
   NEXT

   DETAIL->( DbCommit() )
   DETAIL->( DbGoTop() )
   ? Str( Len(aDetails), 5 ) + " records   [INVNO, ITEMIDX]"

   IF lBrowse
      SELECT DETAIL
      BrowseTable( "invoicedetail.dbf", Len(aDetails) )
   ENDIF

   detail->( DbCloseArea() )
   RETURN .T.

//----------------------------------------------------------------------------

STATIC PROCEDURE BrowseTable( cTable, nRecs, aTags )
   LOCAL oBrw, oCol, nKey, lExit, i, nW
   LOCAL nOrdIdx  := 1
   LOCAL cOrdName, cFooter

   hb_Default( @aTags, {} )

   CLS

   @ 0, 0 SAY PadR( "  " + cTable + "  " + LTrim( Str(nRecs) ) + " records", MaxCol() + 1 ) COLOR "GR+/B"

   cFooter := "  " + Chr(24) + Chr(25) + " navigate   PgUp/PgDn   Ctrl+PgDn/PgUp first/last   Esc close"
   IF Len( aTags ) > 0
      cFooter += "   Space: cycle index"
   ENDIF
   @ MaxRow(), 0 SAY PadR( cFooter, MaxCol() + 1 ) COLOR "N+/W"

   oBrw := TBrowseDB( 1, 0, MaxRow() - 1, MaxCol() )
   oBrw:colorSpec := "W/N,GR+/B,W/N,GR+/B"
   oBrw:headSep   := Chr(205)
   oBrw:colSep    := Chr(179)

   FOR i := 1 TO FCount()
      nW   := Min( FieldLen(i), 22 )
      oCol := TBColumn():New( FieldName(i), FieldBlock( FieldName(i) ) )
      oCol:Width := nW
      oBrw:AddColumn( oCol )
   NEXT

   lExit := .F.
   DO WHILE ! lExit
      DO WHILE ! oBrw:Stabilize() ; ENDDO

      cOrdName := OrdName()
      IF Empty( cOrdName ) ; cOrdName := "natural" ; ENDIF
      @ 0, 45 SAY PadR( "[" + cOrdName + "]", 15 ) COLOR "GR+/B"
      @ 0, MaxCol() - 17 SAY " Rec " + ;
         PadL( LTrim( Str( RecNo() ) ), 6 ) + "/" + ;
         PadL( LTrim( Str( LastRec() ) ), 6 ) + " " COLOR "GR+/B"

      nKey := InKey( 0 )
      DO CASE
         CASE nKey == K_ESC       ;  lExit := .T.
         CASE nKey == K_DOWN      ;  oBrw:Down()
         CASE nKey == K_UP        ;  oBrw:Up()
         CASE nKey == K_PGDN      ;  oBrw:PageDown()
         CASE nKey == K_PGUP      ;  oBrw:PageUp()
         CASE nKey == K_CTRL_PGDN ;  oBrw:GoBottom()
         CASE nKey == K_CTRL_PGUP ;  oBrw:GoTop()
         CASE nKey == K_LEFT      ;  oBrw:Left()
         CASE nKey == K_RIGHT     ;  oBrw:Right()
         CASE nKey == K_HOME      ;  oBrw:Home()
         CASE nKey == K_END       ;  oBrw:End()
         CASE nKey == K_SPACE .AND. Len( aTags ) > 0
            nOrdIdx := IIF( nOrdIdx >= Len( aTags ), 1, nOrdIdx + 1 )
            OrdSetFocus( aTags[nOrdIdx][1] )
            oBrw:RefreshAll()
            oBrw:GoTop()
      ENDCASE
   ENDDO

   CLS
   RETURN

//----------------------------------------------------------------------------

STATIC PROCEDURE PrintBanner()
   LOCAL cTitle := "OpenADS DBF/CDX Invoice Fixture Generator"
   LOCAL nW     := BOX_WIDTH
   LOCAL cH     := Chr(205)   /* ═ */
   LOCAL cV     := Chr(186)   /* ║ */
   LOCAL cTL    := Chr(201)   /* ╔ */
   LOCAL cTR    := Chr(187)   /* ╗ */
   LOCAL cBL    := Chr(200)   /* ╚ */
   LOCAL cBR    := Chr(188)   /* ╝ */

   ?
   ? cTL + Replicate( cH, nW - 2 ) + cTR
   ? cV  + PadC( cTitle, nW - 2 )  + cV
   ? cBL + Replicate( cH, nW - 2 ) + cBR
   ?
   RETURN

//----------------------------------------------------------------------------

STATIC FUNCTION Sep( cChar )
   hb_Default( @cChar, Chr(196) )   /* ─ */
   RETURN Replicate( cChar, BOX_WIDTH )

//----------------------------------------------------------------------------

STATIC PROCEDURE DeleteExistingFixtureFiles( cDbf )
   LOCAL cCdx := hb_FNameExtSet( cDbf, ".cdx" )
   LOCAL cFpt := hb_FNameExtSet( cDbf, ".fpt" )

   IF File( cDbf ) ; FErase( cDbf ) ; ENDIF
   IF File( cCdx ) ; FErase( cCdx ) ; ENDIF
   IF File( cFpt ) ; FErase( cFpt ) ; ENDIF
   RETURN

//----------------------------------------------------------------------------

STATIC FUNCTION GenerateCustomerRows()
   LOCAL aResult  := {}
   LOCAL aFirst   := { "Alex", "Beth", "Carl", "Dana", "Evan", "Fiona", ;
                       "Gabe", "Hana", "Iris", "Joel", "Kara", "Liam", ;
                       "Mia",  "Nate", "Owen", "Pia",  "Quinn","Rita", ;
                       "Sean", "Tina", "Udo",  "Vera", "Will", "Zara" }
   LOCAL aLast    := { "Bishop",  "Clark",   "Dawson",  "Evans",      "Frost", ;
                       "Garcia",  "Harris",  "Iverson", "Jones",      "Kline", ;
                       "Lee",     "Miller",  "Norton",  "Owens",      "Parker", ;
                       "Quincy",  "Reed",    "Stone",   "Turner",     "Underwood", ;
                       "Vega",    "Walker",  "Young",   "Zimmer" }
   LOCAL aCountry := { "USA", "Canada", "UK", "Germany", "France", ;
                       "Spain", "Italy", "Australia", "Brazil", "Sweden" }
   LOCAL i, cCustNo, cName, nDays, dCreated, nBalance

   hb_randomSeed( Int( Seconds() ) )

   FOR i := 1 TO CUSTOMER_COUNT
      cName    := aFirst[ 1 + Int( Len( aFirst )   * hb_random() ) ] + " " + ;
                  aLast[  1 + Int( Len( aLast )    * hb_random() ) ]
      cCustNo  := "C" + StrZero( i, 5 )
      nDays    := Int( 365 * 5 * hb_random() )
      dCreated := Date() - nDays
      nBalance := Round( 50 + 10000 * hb_random(), 2 )
      AAdd( aResult, { cCustNo, cName, ;
                       aCountry[ 1 + Int( Len( aCountry ) * hb_random() ) ], ;
                       dCreated, nBalance } )
   NEXT

   RETURN aResult

//----------------------------------------------------------------------------

STATIC FUNCTION GenerateItems()
   LOCAL aResult := {}
   LOCAL aWords  := { "Widget",    "Gadget",  "Service", "Kit",    "Module", ;
                      "Component", "Unit",    "Package", "Bundle", "Accessory" }
   LOCAL i, cItemNo, cDescr, nPrice

   hb_randomSeed( Int( Seconds() ) + 1234 )

   FOR i := 1 TO ITEM_COUNT
      cItemNo := "I" + StrZero( i, 4 )
      cDescr  := aWords[ 1 + Int( Len( aWords ) * hb_random() ) ] + " " + ;
                 aWords[ 1 + Int( Len( aWords ) * hb_random() ) ]
      nPrice  := Round( 5 + 195 * hb_random(), 2 )
      AAdd( aResult, { cItemNo, cDescr, nPrice } )
   NEXT

   RETURN aResult

//----------------------------------------------------------------------------

STATIC FUNCTION GenerateInvoices( aCustomers )
   LOCAL aResult  := {}
   LOCAL aStatus  := { "O", "P", "C" }
   LOCAL i, cInvNo, cCustNo, dDate, nStatusIdx

   hb_randomSeed( Int( Seconds() ) + 2345 )

   FOR i := 1 TO INVOICE_COUNT
      cInvNo     := "INV" + StrZero( i, 7 )
      cCustNo    := aCustomers[ 1 + Int( Len( aCustomers ) * hb_random() ) ][1]
      dDate      := Date() - Int( 365 * hb_random() )
      nStatusIdx := 1 + Int( Len( aStatus ) * hb_random() )
      AAdd( aResult, { cInvNo, cCustNo, dDate, 0.0, aStatus[nStatusIdx] } )
   NEXT

   RETURN aResult

//----------------------------------------------------------------------------

/* Builds detail rows; accumulates per-invoice totals into aInvoices[][4]. */
STATIC FUNCTION GenerateInvoiceDetails( aInvoices, aItems )
   LOCAL aResult := {}
   LOCAL i, nLine, cInvNo, cItemNo, nQty, nPrice, nAmount, nInvTotal

   hb_randomSeed( Int( Seconds() ) + 3456 )

   FOR i := 1 TO Len( aInvoices )
      cInvNo    := aInvoices[i][1]
      nInvTotal := 0.0
      FOR nLine := 1 TO MIN_DETAIL_PER_INV
         cItemNo  := aItems[ 1 + Int( Len( aItems ) * hb_random() ) ][1]
         nQty     := 1 + Int( 10 * hb_random() )
         nPrice   := aItems[ 1 + Int( Len( aItems ) * hb_random() ) ][3]
         nAmount  := Round( nQty * nPrice, 2 )
         nInvTotal += nAmount
         AAdd( aResult, { cInvNo, nLine, cItemNo, nQty, nPrice, nAmount } )
      NEXT
      aInvoices[i][4] := Round( nInvTotal, 2 )
   NEXT

   RETURN aResult

//----------------------------------------------------------------------------

STATIC PROCEDURE MyHandler( oErr )
   ? "ERROR:", oErr:Description
   ? "       gencode=",  oErr:GenCode, " subcode=", oErr:SubCode
   ? "       operation=", oErr:Operation
   ErrorLevel( 1 )
   QUIT
   RETURN
