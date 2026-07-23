// oads_imac_test.cpp
//
// Comprehensive test of ALL oads_*() C functions against the iMac
// on the local LAN running openads_serverd.
//
// The iMac server must be running:
//   ssh Anto@192.168.18.184 "openads_serverd --port 16262 --data /tmp/openads_mac"
//
// Build (from OpenADS root):
//   cmake --build build --target oads_imac_test
//
// Run:
//   oads_imac_test
//
// The test connects to tcp://192.168.18.184:16262//tmp/openads_mac
// and exercises every oads_*() function:
//   oads_FCreate, oads_FOpen, oads_FClose, oads_FWrite, oads_FRead, oads_FSeek

#include "openads/ace.h"
#include "openads/error.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// -- iMac connection details ------------------------------------------

static const char* IMAC_HOST   = "192.168.18.184";
static const int   IMAC_PORT   = 16262;
static const char* IMAC_DATA   = "/tmp/openads_mac";

// -- test bookkeeping -------------------------------------------------

static int g_passed = 0;
static int g_failed = 0;
static int g_total  = 0;

static void check( bool cond, const char* name )
{
    g_total++;
    if( cond )
    {
        g_passed++;
        printf( "  [PASS] %s\n", name );
    }
    else
    {
        g_failed++;
        printf( "  [FAIL] %s\n", name );
    }
}

// -- connect ----------------------------------------------------------

static ADSHANDLE imac_connect()
{
    char uri[512];
    snprintf( uri, sizeof( uri ),
              "tcp://%s:%d//%s", IMAC_HOST, IMAC_PORT, IMAC_DATA );

    printf( "\nConnecting to %s ...\n", uri );

    std::vector<UNSIGNED8> buf( uri, uri + strlen( uri ) + 1 );
    ADSHANDLE hConn = 0;
    UNSIGNED32 rc = AdsConnect60( buf.data(), ADS_REMOTE_SERVER,
                                 nullptr, nullptr, 0, &hConn );
    if( rc != 0 || hConn == 0 )
    {
        printf( "  FATAL: AdsConnect60 failed (rc=%u, h=%llu)\n",
                rc, (unsigned long long) hConn );
        printf( "  Make sure openads_serverd is running on the iMac:\n" );
        printf( "    ssh Anto@%s \"openads_serverd --port %d --data %s\"\n",
                IMAC_HOST, IMAC_PORT, IMAC_DATA );
        return 0;
    }
    printf( "  Connected!  hConn = %llu\n", (unsigned long long) hConn );
    return hConn;
}

// -- test: oads_FCreate + oads_FWrite + oads_FClose ------------------

static void test_create_write_close( ADSHANDLE hConn )
{
    printf( "\n=== oads_FCreate + oads_FWrite + oads_FClose ===\n" );

    ADSHANDLE hf = 0;
    UNSIGNED32 rc = oads_FCreate( hConn, (UNSIGNED8*)"oads_imac_test.txt",
                                  0, &hf );
    check( rc == 0 && hf != 0, "oads_FCreate -> success, valid handle" );

    const char* msg = "Hello from oads_imac_test!";
    UNSIGNED32 nw = 0;
    rc = oads_FWrite( hf, msg, (UNSIGNED32) strlen( msg ), &nw );
    check( rc == 0, "oads_FWrite -> AE_SUCCESS" );
    check( nw == strlen( msg ), "oads_FWrite -> correct byte count" );

    rc = oads_FClose( hf );
    check( rc == 0, "oads_FClose -> success" );
}

// -- test: oads_FOpen + oads_FRead + oads_FClose ---------------------

static void test_open_read_close( ADSHANDLE hConn )
{
    printf( "\n=== oads_FOpen + oads_FRead + oads_FClose ===\n" );

    ADSHANDLE hf = 0;
    UNSIGNED32 rc = oads_FOpen( hConn, (UNSIGNED8*)"oads_imac_test.txt",
                                ADS_READONLY, &hf );
    check( rc == 0 && hf != 0, "oads_FOpen(READONLY) -> success" );

    char buf[256] = {};
    UNSIGNED32 nr = 0;
    rc = oads_FRead( hf, buf, sizeof( buf ), &nr );
    check( rc == 0, "oads_FRead -> AE_SUCCESS" );
    check( nr == strlen( "Hello from oads_imac_test!" ),
           "oads_FRead -> correct byte count" );
    check( strcmp( buf, "Hello from oads_imac_test!" ) == 0,
           "oads_FRead -> content matches" );

    rc = oads_FClose( hf );
    check( rc == 0, "oads_FClose -> success" );
}

// -- test: oads_FSeek ------------------------------------------------

static void test_seek( ADSHANDLE hConn )
{
    printf( "\n=== oads_FSeek (SET, CUR, END) ===\n" );

    ADSHANDLE hf = 0;
    UNSIGNED32 rc = oads_FOpen( hConn, (UNSIGNED8*)"oads_imac_test.txt",
                                ADS_READONLY, &hf );
    check( rc == 0 && hf != 0, "FOpen for seek test" );

    UNSIGNED32 pos = 0;

    // SEEK_SET to 0
    rc = oads_FSeek( hf, 0, 0, &pos );
    check( rc == 0 && pos == 0, "FSeek(0, SET) -> pos=0" );

    // SEEK_SET to 6
    rc = oads_FSeek( hf, 6, 0, &pos );
    check( rc == 0 && pos == 6, "FSeek(6, SET) -> pos=6" );

    // read from offset 6
    char buf[256] = {};
    UNSIGNED32 nr = 0;
    rc = oads_FRead( hf, buf, sizeof( buf ), &nr );
    check( rc == 0, "FRead after SEEK_SET(6) -> success" );
    check( strstr( buf, "oads_imac_test!" ) != nullptr,
           "FRead after SEEK_SET(6) -> contains 'oads_imac_test!'" );

    // SEEK_CUR +0 (no move)
    rc = oads_FSeek( hf, 0, 1, &pos );
    check( rc == 0, "FSeek(0, CUR) -> success" );

    // SEEK_END -5
    rc = oads_FSeek( hf, -5, 2, &pos );
    check( rc == 0, "FSeek(-5, END) -> success" );
    rc = oads_FRead( hf, buf, sizeof( buf ), &nr );
    check( nr > 0 && nr <= 6 && strstr( buf, "test!" ) != nullptr,
           "FRead after SEEK_END(-5) -> contains 'test!'" );

    // SEEK_END 0 (at end, read returns 0 bytes)
    rc = oads_FSeek( hf, 0, 2, &pos );
    UNSIGNED32 total = (UNSIGNED32) strlen( "Hello from oads_imac_test!" );
    check( rc == 0 && pos == total, "FSeek(0, END) -> pos at EOF" );

    rc = oads_FRead( hf, buf, 1, &nr );
    check( rc == 0 && nr == 0, "FRead at EOF -> 0 bytes" );

    oads_FClose( hf );
}

// -- test: write/read round-trip with overwrite ----------------------

static void test_overwrite( ADSHANDLE hConn )
{
    printf( "\n=== oads_FCreate overwrites existing file ===\n" );

    ADSHANDLE hf = 0;
    UNSIGNED32 nw = 0;

    // write "AAAA"
    oads_FCreate( hConn, (UNSIGNED8*)"oads_imac_ow.txt", 0, &hf );
    oads_FWrite( hf, "AAAA", 4, &nw );
    oads_FClose( hf );

    // overwrite with "BB"
    oads_FCreate( hConn, (UNSIGNED8*)"oads_imac_ow.txt", 0, &hf );
    oads_FWrite( hf, "BB", 2, &nw );
    oads_FClose( hf );

    // read back
    oads_FOpen( hConn, (UNSIGNED8*)"oads_imac_ow.txt", ADS_READONLY, &hf );
    char buf[64] = {};
    UNSIGNED32 nr = 0;
    oads_FRead( hf, buf, sizeof( buf ), &nr );
    check( nr == 2 && strcmp( buf, "BB" ) == 0,
           "overwrite: file contains 'BB', not 'BBAA'" );
    oads_FClose( hf );
}

// -- test: large write/read (64 KB) ----------------------------------

static void test_large_io( ADSHANDLE hConn )
{
    printf( "\n=== oads_FWrite/FRead large buffer (64 KB) ===\n" );

    const int SIZE = 65536;
    std::vector<char> wdata( SIZE );
    for( int i = 0; i < SIZE; i++ )
        wdata[i] = (char) ( 'A' + ( i % 26 ) );

    ADSHANDLE hf = 0;
    UNSIGNED32 rc = oads_FCreate( hConn, (UNSIGNED8*)"oads_imac_big.bin", 0, &hf );
    check( rc == 0 && hf != 0, "FCreate for 64KB test" );

    UNSIGNED32 nw = 0;
    rc = oads_FWrite( hf, wdata.data(), SIZE, &nw );
    check( rc == 0 && nw == SIZE, "FWrite 64KB -> all bytes written" );
    oads_FClose( hf );

    // read back
    rc = oads_FOpen( hConn, (UNSIGNED8*)"oads_imac_big.bin", ADS_READONLY, &hf );
    check( rc == 0 && hf != 0, "FOpen for 64KB readback" );

    std::vector<char> rdata( SIZE, 0 );
    UNSIGNED32 nr = 0;
    rc = oads_FRead( hf, rdata.data(), SIZE, &nr );
    check( rc == 0 && nr == SIZE, "FRead 64KB -> all bytes read" );
    check( memcmp( wdata.data(), rdata.data(), SIZE ) == 0,
           "FRead 64KB -> content matches" );
    oads_FClose( hf );
}

// -- test: read in chunks --------------------------------------------

static void test_chunked_read( ADSHANDLE hConn )
{
    printf( "\n=== oads_FRead in chunks ===\n" );

    ADSHANDLE hf = 0;
    oads_FCreate( hConn, (UNSIGNED8*)"oads_imac_chunks.txt", 0, &hf );
    const char* data = "0123456789ABCDEF";  // 16 bytes
    UNSIGNED32 nw = 0;
    oads_FWrite( hf, data, 16, &nw );
    oads_FClose( hf );

    oads_FOpen( hConn, (UNSIGNED8*)"oads_imac_chunks.txt", ADS_READONLY, &hf );
    char buf[64] = {};
    UNSIGNED32 nr = 0;

    // chunk 1: 8 bytes
    oads_FRead( hf, buf, 8, &nr );
    check( nr == 8 && memcmp( buf, "01234567", 8 ) == 0,
           "chunk 1: '01234567'" );

    // chunk 2: 8 bytes
    oads_FRead( hf, buf, 8, &nr );
    check( nr == 8 && memcmp( buf, "89ABCDEF", 8 ) == 0,
           "chunk 2: '89ABCDEF'" );

    // chunk 3: at EOF
    oads_FRead( hf, buf, 8, &nr );
    check( nr == 0, "chunk 3: EOF (0 bytes)" );

    oads_FClose( hf );
}

// -- test: error handling --------------------------------------------

static void test_errors()
{
    printf( "\n=== Error handling ===\n" );

    char buf[4] = {};
    UNSIGNED32 nr = 0;

    // bad handle
    check( oads_FRead( 99999, buf, 4, &nr ) != 0,
           "FRead bad handle -> error" );
    check( nr == 0, "FRead bad handle -> 0 bytes read" );

    check( oads_FClose( 99999 ) != 0,
           "FClose bad handle -> error" );

    UNSIGNED32 nw = 0;
    check( oads_FWrite( 99999, "x", 1, &nw ) != 0,
           "FWrite bad handle -> error" );
    check( nw == 0, "FWrite bad handle -> 0 bytes written" );

    UNSIGNED32 pos = 0;
    check( oads_FSeek( 99999, 0, 0, &pos ) != 0,
           "FSeek bad handle -> error" );

    // null name
    ADSHANDLE hf = 0;
    check( oads_FCreate( 0, nullptr, 0, &hf ) != 0,
           "FCreate(null name) -> error" );
    check( oads_FOpen( 0, nullptr, 0, &hf ) != 0,
           "FOpen(null name) -> error" );
}

// -- cleanup ---------------------------------------------------------

static void cleanup( ADSHANDLE hConn )
{
    printf( "\n=== Cleaning up ===\n" );

    // delete test files
    const char* files[] = {
        "oads_imac_test.txt",
        "oads_imac_ow.txt",
        "oads_imac_big.bin",
        "oads_imac_chunks.txt",
        nullptr
    };

    for( int i = 0; files[i]; i++ )
    {
        UNSIGNED32 rc = AdsDeleteFile( hConn, (UNSIGNED8*) files[i] );
        printf( "  Deleted %s: %s\n", files[i],
                rc == 0 ? "OK" : "(not found or error)" );
    }

    AdsDisconnect( hConn );
    printf( "  Disconnected.\n" );
}

// -- main ------------------------------------------------------------

int main()
{
    setbuf( stdout, nullptr );
    setbuf( stderr, nullptr );

    printf( "========================================\n" );
    printf( "  oads_*() comprehensive test (iMac)\n" );
    printf( "  Target: %s:%d/%s\n", IMAC_HOST, IMAC_PORT, IMAC_DATA );
    printf( "========================================\n" );

    ADSHANDLE hConn = imac_connect();
    if( !hConn )
    {
        printf( "\n*** Could not connect to iMac server. ***\n" );
        printf( "*** Start the server and re-run this test. ***\n" );
        return 1;
    }

    test_create_write_close( hConn );
    test_open_read_close( hConn );
    test_seek( hConn );
    test_overwrite( hConn );
    test_large_io( hConn );
    test_chunked_read( hConn );
    test_errors();
    cleanup( hConn );

    printf( "\n========================================\n" );
    printf( "  RESULTS:  %d passed, %d failed, %d total\n",
            g_passed, g_failed, g_total );
    printf( "========================================\n" );

    if( g_failed == 0 )
        printf( "\n  *** ALL TESTS PASSED ***\n\n" );
    else
        printf( "\n  *** FAILURES DETECTED ***\n\n" );

    return g_failed;
}
