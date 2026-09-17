* Server-side UDF test module for OpenADS HRB bridge tests.
* Built with: harbour udf_test.prg -gh -oudftest.hrb
*
* CONSTRAINTS (verified against the embedded VM):
* - Only RTL builtins resolvable in a bare host (Len/SubStr/DToS/
*   Date/arithmetic). Statements compiling to unregistered
*   internals (e.g. RELEASE -> __MVXRELEASE) fail module LINK.
* - No module STATICs: statics do not resolve when HRB functions
*   run outside hb_hrbDo module context (writes land in an implicit
*   memvar, later reads fail). Server index UDFs must be
*   stateless/pure anyway.
* - No PUBLICs for the same link reason; lifecycle is proven
*   through UDF_Init's return value instead.

FUNCTION UDF_Init
   RETURN .T.

FUNCTION UDF_Exit
   RETURN Nil

* Byte-reverse loop (Clipper-era shape, cf. Vouch Reverse()).
FUNCTION TESTREV( cStr )
   LOCAL cOut := "", i
   FOR i := Len( cStr ) TO 1 STEP -1
      cOut += SubStr( cStr, i, 1 )
   NEXT
   RETURN cOut

* Numeric round-trip (both args and result).
FUNCTION TESTADD( a, b )
   RETURN a + b

* String passthrough incl. trailing blanks.
FUNCTION TESTECHO( cStr )
   RETURN cStr

* Deliberate runtime error (division by zero) for bridge
* error-containment: the call must fail cleanly, the VM stays up.
FUNCTION TESTBOOM()
   RETURN 1 / 0

* Date result mapping (bridge formats YYYYMMDD).
FUNCTION TESTDTOR()
   RETURN DToS( Date() )
