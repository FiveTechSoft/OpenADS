Built for Pritpal Bedi - bedipritpal/OpenADS, forked from FiveTechSoft/OpenADS.

# v1.09.70-mtfix22 - index focus test build

Bug report supplied by Tim Stone and relayed by Pritpal Bedi. This is a test build, not a production recommendation. Use matching client and server versions and back up binaries and data before testing. No fork-main or upstream merge is part of this build. Tim's exact ECLMST test remains the application gate.

## Changes

LOCAL tag lookup now compares names without regard to case, matching REMOTE. Stored/displayed spelling and numeric tag order are unchanged. This covers CDX, native ADI and ADI routed through CDX.

Stock Harbour rddads sets order 0 or empty focus by changing its current handle to the table handle, without sending an ACE reset. Table-handle GoTop, GoBottom and Skip now stop inheriting an order activated only by index-handle navigation. Index handles remain open, and record edits/appends still maintain all open tags. Explicit AdsSetIndexOrder/ByHandle retains ordered table-handle navigation. Newly created indexes keep the existing immediate table-navigation convention until index-handle navigation takes over; the stock zero-focus path then becomes natural.

Parking a tag retains its scope and runtime direction. REMOTE direct natural Skip reanchors at the client-visible physical record when an order reset or locally served boundary pair left the server cursor elsewhere. Boundary-pair answers remain available; the next natural Skip synchronizes before stepping.

## Validation

Local Linux GCC used -O0/-g0 with warnings-as-errors disabled because of existing SQL/cache warnings; shipped platform flags are not changed. Focus/order/scope/navigation checks passed 152 cases and 61,760 assertions. The CDX-format ADI focused navigation run passed 23 cases and 2,395 assertions. New regression covers stored/requested tag cases, numeric lookup, repeated implicit zero/empty-focus call sequences, direct Skip, explicit APIs, retained scopes, creation-to-index-handle transition, and append maintenance across LOCAL/REMOTE ADT/CDX.

Full-suite chunks covered 1,605 enabled cases and 16 disabled cases. MT reader/appender, Exclusive-held OpenIndex return-code, and remote-create storm tests failed intermittently; these failure classes also reproduced on mtfix21. Some tests were corrected to select an explicit order before navigating by table handle, rather than depending on implicit index focus. The normal CTest suite passes under its existing slow/flaky exclusions. No result from Tim's actual Windows 64-bit binary or ECLMST files is claimed.

## Application check

Repeat Tim's report with CDX-format ECLMST.adi (adt_cdx_index=1): select eclcom by name and number, check OrdName/IndexOrd, then set order 0 and empty focus and verify the physical record walk. Repeat LOCAL and REMOTE, switch back to a tag, test scopes and append/write maintenance. Report exact handles, recnos and return codes for any mismatch. Pritpal does not use ADT and is not providing application feedback for this fix.
