// abi_oads_file_funcs_test.cpp
//
// Exercises ALL 6 oads_*() C functions (the legacy filesystem aliases):
//   oads_FOpen, oads_FCreate, oads_FClose, oads_FRead, oads_FWrite, oads_FSeek
//
// Tests run in two modes:
//   1. LOCAL  — in-process engine against a temp data dir (always runs).
//   2. REMOTE — against an openads_serverd. Gated on OPENADS_TEST_REMOTE.
//
// Build:
//   cmake --build build --target openads_unit_tests
//   cmake --build build --target remote_server   (to get openads_serverd)
//
// Run local only:
//   openads_unit_tests -tc="oads_*"
//
// Run remote (start server first):
//   openads_serverd --port 16262 --data /path/to/data
//   set OPENADS_TEST_REMOTE=tcp://127.0.0.1:16262//path/to/data
//   openads_unit_tests -tc="oads_*"
//
// Remote against iMac (LAN):
//   openads_serverd --port 6262 --data /Users/anto/OpenADS/data
//   set OPENADS_TEST_REMOTE=tcp://192.168.18.184:6262//Users/anto/OpenADS/data
//   openads_unit_tests -tc="oads_*"

#include "doctest.h"
#include "openads/ace.h"
#include "openads/error.h"
#include "network/server.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// -- helpers ----------------------------------------------------------

namespace {

const char* remote_uri_env()
{
    return std::getenv( "OPENADS_TEST_REMOTE" );
}

ADSHANDLE local_connect( const fs::path& dir )
{
    std::string path = dir.string();
    std::vector<UNSIGNED8> buf( path.begin(), path.end() );
    buf.push_back( 0 );
    ADSHANDLE h = 0;
    REQUIRE( AdsConnect60( buf.data(), ADS_LOCAL_SERVER, nullptr, nullptr, 0,
                           &h ) == 0 );
    REQUIRE( h != 0 );
    return h;
}

ADSHANDLE remote_connect_embedded( const fs::path& dir, std::uint16_t port )
{
    std::string uri = "tcp://127.0.0.1:" + std::to_string( port ) + "/" +
                      dir.generic_string();
    std::vector<UNSIGNED8> buf( uri.begin(), uri.end() );
    buf.push_back( 0 );
    ADSHANDLE h = 0;
    REQUIRE( AdsConnect60( buf.data(), ADS_REMOTE_SERVER, nullptr, nullptr, 0,
                           &h ) == 0 );
    REQUIRE( h != 0 );
    return h;
}

ADSHANDLE remote_connect_live( const char* uri )
{
    std::vector<UNSIGNED8> buf( uri, uri + std::strlen( uri ) + 1 );
    ADSHANDLE h = 0;
    UNSIGNED32 rc = AdsConnect60( buf.data(), ADS_REMOTE_SERVER, nullptr,
                                 nullptr, 0, &h );
    INFO( "remote connect rc=", rc, " h=", h, " uri=", uri );
    REQUIRE( rc == 0 );
    REQUIRE( h != 0 );
    return h;
}

// -- shared test body -------------------------------------------------
// Runs the full oads_* round-trip against any connected handle.

void oads_round_trip( ADSHANDLE hConn, const char* tag )
{
    INFO( "tag=", tag );

    // --- oads_FCreate: create a file ----------------------------------
    ADSHANDLE hf = 0;
    UNSIGNED32 rc = oads_FCreate( hConn, (UNSIGNED8*)"oads_test.txt", 0, &hf );
    INFO( "FCreate rc=", rc, " hf=", hf );
    REQUIRE( rc == 0 );
    REQUIRE( hf != 0 );

    // --- oads_FWrite: write "Hello OADS!" -----------------------------
    const char* msg = "Hello OADS!";
    UNSIGNED32 nw = 0;
    rc = oads_FWrite( hf, msg, 11, &nw );
    INFO( "FWrite rc=", rc, " nw=", nw );
    REQUIRE( rc == 0 );
    CHECK( nw == 11 );

    // --- oads_FClose --------------------------------------------------
    REQUIRE( oads_FClose( hf ) == 0 );
    hf = 0;

    // --- oads_FOpen: reopen for reading --------------------------------
    rc = oads_FOpen( hConn, (UNSIGNED8*)"oads_test.txt", ADS_READONLY, &hf );
    INFO( "FOpen rc=", rc, " hf=", hf );
    REQUIRE( rc == 0 );
    REQUIRE( hf != 0 );

    // --- oads_FRead: read everything back -----------------------------
    char buf[64] = {};
    UNSIGNED32 nr = 0;
    rc = oads_FRead( hf, buf, sizeof( buf ), &nr );
    INFO( "FRead rc=", rc, " nr=", nr );
    REQUIRE( rc == 0 );
    CHECK( nr == 11 );
    CHECK( std::string( buf, nr ) == "Hello OADS!" );

    // --- oads_FSeek: seek to offset 6 (-> "OADS!") --------------------
    UNSIGNED32 pos = 0;
    rc = oads_FSeek( hf, 6, 0 /*SET*/, &pos );
    INFO( "FSeek(6,SET) rc=", rc, " pos=", pos );
    REQUIRE( rc == 0 );
    CHECK( pos == 6 );

    // read from offset 6
    std::memset( buf, 0, sizeof( buf ) );
    nr = 0;
    rc = oads_FRead( hf, buf, sizeof( buf ), &nr );
    INFO( "FRead after seek rc=", rc, " nr=", nr );
    REQUIRE( rc == 0 );
    CHECK( nr == 5 );
    CHECK( std::string( buf, nr ) == "OADS!" );

    // --- oads_FSeek: SEEK_CUR by 0 (no move) -------------------------
    rc = oads_FSeek( hf, 0, 1 /*CUR*/, &pos );
    INFO( "FSeek(0,CUR) rc=", rc, " pos=", pos );
    REQUIRE( rc == 0 );
    CHECK( pos == 6 );

    // --- oads_FSeek: SEEK_END -----------------------------------------
    rc = oads_FSeek( hf, 0, 2 /*END*/, &pos );
    INFO( "FSeek(0,END) rc=", rc, " pos=", pos );
    REQUIRE( rc == 0 );
    CHECK( pos == 11 );

    // --- oads_FClose --------------------------------------------------
    REQUIRE( oads_FClose( hf ) == 0 );

    // --- verify oads_FOpen with ADS_READONLY denies write -------------
    rc = oads_FOpen( hConn, (UNSIGNED8*)"oads_test.txt", ADS_READONLY, &hf );
    REQUIRE( rc == 0 );
    REQUIRE( hf != 0 );
    nw = 0;
    rc = oads_FWrite( hf, "x", 1, &nw );
    INFO( "FWrite on readonly rc=", rc, " nw=", nw );
    CHECK( rc != 0 );  // must fail
    CHECK( nw == 0 );
    oads_FClose( hf );
}

} // anonymous namespace

// -- LOCAL tests ------------------------------------------------------

TEST_CASE( "oads_* local: full round-trip (create/write/close/open/read/seek)" )
{
    auto dir = fs::temp_directory_path() / "oads_file_local";
    std::error_code ec;
    fs::remove_all( dir, ec );
    fs::create_directories( dir );

    ADSHANDLE h = local_connect( dir );
    oads_round_trip( h, "local" );
    REQUIRE( AdsDisconnect( h ) == 0 );
    fs::remove_all( dir, ec );
}

TEST_CASE( "oads_* local: null args return error" )
{
    ADSHANDLE hf = 0;

    // null name
    UNSIGNED32 rc = oads_FCreate( 0, nullptr, 0, &hf );
    CHECK( rc != 0 );
    CHECK( hf == 0 );

    rc = oads_FOpen( 0, nullptr, 0, &hf );
    CHECK( rc != 0 );
    CHECK( hf == 0 );

    // null phFile
    rc = oads_FCreate( 0, (UNSIGNED8*)"x", 0, nullptr );
    CHECK( rc != 0 );
}

TEST_CASE( "oads_* local: write then read back in chunks" )
{
    auto dir = fs::temp_directory_path() / "oads_file_chunks";
    std::error_code ec;
    fs::remove_all( dir, ec );
    fs::create_directories( dir );

    ADSHANDLE h = local_connect( dir );
    ADSHANDLE hf = 0;

    // create and write 20 bytes
    REQUIRE( oads_FCreate( h, (UNSIGNED8*)"chunks.dat", 0, &hf ) == 0 );
    const char* data = "0123456789ABCDEF0123";  // 20 bytes
    UNSIGNED32 nw = 0;
    REQUIRE( oads_FWrite( hf, data, 20, &nw ) == 0 );
    CHECK( nw == 20 );
    REQUIRE( oads_FClose( hf ) == 0 );

    // reopen and read in 8-byte chunks
    REQUIRE( oads_FOpen( h, (UNSIGNED8*)"chunks.dat", ADS_READONLY, &hf ) == 0 );
    char buf[32] = {};
    UNSIGNED32 nr = 0;

    // chunk 1: bytes 0-7
    REQUIRE( oads_FRead( hf, buf, 8, &nr ) == 0 );
    CHECK( nr == 8 );
    CHECK( std::string( buf, 8 ) == "01234567" );

    // chunk 2: bytes 8-15
    REQUIRE( oads_FRead( hf, buf, 8, &nr ) == 0 );
    CHECK( nr == 8 );
    CHECK( std::string( buf, 8 ) == "89ABCDEF" );

    // chunk 3: bytes 16-19 (only 4 left)
    REQUIRE( oads_FRead( hf, buf, 8, &nr ) == 0 );
    CHECK( nr == 4 );
    CHECK( std::string( buf, 4 ) == "0123" );

    // at EOF: read returns 0
    REQUIRE( oads_FRead( hf, buf, 8, &nr ) == 0 );
    CHECK( nr == 0 );

    REQUIRE( oads_FClose( hf ) == 0 );
    REQUIRE( AdsDisconnect( h ) == 0 );
    fs::remove_all( dir, ec );
}

TEST_CASE( "oads_* local: seek round-trip (SET/CUR/END)" )
{
    auto dir = fs::temp_directory_path() / "oads_file_seek";
    std::error_code ec;
    fs::remove_all( dir, ec );
    fs::create_directories( dir );

    ADSHANDLE h = local_connect( dir );
    ADSHANDLE hf = 0;

    REQUIRE( oads_FCreate( h, (UNSIGNED8*)"seektest.dat", 0, &hf ) == 0 );
    const char* data = "ABCDEFGHIJ";  // 10 bytes
    UNSIGNED32 nw = 0;
    REQUIRE( oads_FWrite( hf, data, 10, &nw ) == 0 );
    REQUIRE( oads_FClose( hf ) == 0 );

    REQUIRE( oads_FOpen( h, (UNSIGNED8*)"seektest.dat", ADS_READONLY, &hf ) == 0 );
    UNSIGNED32 pos = 0;
    char buf[16] = {};
    UNSIGNED32 nr = 0;

    SUBCASE( "SEEK_SET to 0" )
    {
        REQUIRE( oads_FSeek( hf, 0, 0, &pos ) == 0 );
        CHECK( pos == 0 );
        REQUIRE( oads_FRead( hf, buf, 1, &nr ) == 0 );
        CHECK( buf[0] == 'A' );
    }

    SUBCASE( "SEEK_SET to 5" )
    {
        REQUIRE( oads_FSeek( hf, 5, 0, &pos ) == 0 );
        CHECK( pos == 5 );
        REQUIRE( oads_FRead( hf, buf, 1, &nr ) == 0 );
        CHECK( buf[0] == 'F' );
    }

    SUBCASE( "SEEK_CUR forward by 3 from pos 2" )
    {
        REQUIRE( oads_FSeek( hf, 2, 0, &pos ) == 0 );
        CHECK( pos == 2 );
        REQUIRE( oads_FSeek( hf, 3, 1, &pos ) == 0 );
        CHECK( pos == 5 );
        REQUIRE( oads_FRead( hf, buf, 1, &nr ) == 0 );
        CHECK( buf[0] == 'F' );
    }

    SUBCASE( "SEEK_END -1 (last byte)" )
    {
        REQUIRE( oads_FSeek( hf, -1, 2, &pos ) == 0 );
        CHECK( pos == 9 );
        REQUIRE( oads_FRead( hf, buf, 1, &nr ) == 0 );
        CHECK( buf[0] == 'J' );
    }

    SUBCASE( "SEEK_END 0 (at end, read returns 0)" )
    {
        REQUIRE( oads_FSeek( hf, 0, 2, &pos ) == 0 );
        CHECK( pos == 10 );
        REQUIRE( oads_FRead( hf, buf, 1, &nr ) == 0 );
        CHECK( nr == 0 );
    }

    SUBCASE( "SEEK_SET to end then back to 0" )
    {
        REQUIRE( oads_FSeek( hf, 0, 2, &pos ) == 0 );
        CHECK( pos == 10 );
        REQUIRE( oads_FSeek( hf, 0, 0, &pos ) == 0 );
        CHECK( pos == 0 );
        REQUIRE( oads_FRead( hf, buf, 10, &nr ) == 0 );
        CHECK( nr == 10 );
        CHECK( std::string( buf, 10 ) == "ABCDEFGHIJ" );
    }

    REQUIRE( oads_FClose( hf ) == 0 );
    REQUIRE( AdsDisconnect( h ) == 0 );
    fs::remove_all( dir, ec );
}

TEST_CASE( "oads_* local: FCreate overwrites existing file" )
{
    auto dir = fs::temp_directory_path() / "oads_file_overwrite";
    std::error_code ec;
    fs::remove_all( dir, ec );
    fs::create_directories( dir );

    ADSHANDLE h = local_connect( dir );
    ADSHANDLE hf = 0;

    // write "AAAA"
    REQUIRE( oads_FCreate( h, (UNSIGNED8*)"over.txt", 0, &hf ) == 0 );
    UNSIGNED32 nw = 0;
    REQUIRE( oads_FWrite( hf, "AAAA", 4, &nw ) == 0 );
    REQUIRE( oads_FClose( hf ) == 0 );

    // overwrite with "BB"
    REQUIRE( oads_FCreate( h, (UNSIGNED8*)"over.txt", 0, &hf ) == 0 );
    REQUIRE( oads_FWrite( hf, "BB", 2, &nw ) == 0 );
    REQUIRE( oads_FClose( hf ) == 0 );

    // read back — must be "BB", not "BBAA"
    REQUIRE( oads_FOpen( h, (UNSIGNED8*)"over.txt", ADS_READONLY, &hf ) == 0 );
    char buf[16] = {};
    UNSIGNED32 nr = 0;
    REQUIRE( oads_FRead( hf, buf, sizeof( buf ), &nr ) == 0 );
    CHECK( nr == 2 );
    CHECK( std::string( buf, nr ) == "BB" );
    REQUIRE( oads_FClose( hf ) == 0 );

    REQUIRE( AdsDisconnect( h ) == 0 );
    fs::remove_all( dir, ec );
}

TEST_CASE( "oads_* local: bad handle returns error" )
{
    char buf[4] = {};
    UNSIGNED32 nr = 0;
    CHECK( oads_FRead( 99999, buf, 4, &nr ) != 0 );
    CHECK( nr == 0 );

    CHECK( oads_FClose( 99999 ) != 0 );

    UNSIGNED32 nw = 0;
    CHECK( oads_FWrite( 99999, "x", 1, &nw ) != 0 );
    CHECK( nw == 0 );

    UNSIGNED32 pos = 0;
    CHECK( oads_FSeek( 99999, 0, 0, &pos ) != 0 );
}

// -- REMOTE tests (embedded server) ----------------------------------

TEST_CASE( "oads_* remote embedded: full round-trip" )
{
    using openads::network::Server;
    auto data = fs::temp_directory_path() / "oads_file_remote_emb";
    auto app  = fs::temp_directory_path() / "oads_file_remote_emb_app";
    fs::remove_all( data );
    fs::remove_all( app );
    fs::create_directories( data );
    fs::create_directories( app );

    Server srv;
    srv.set_data_dir( data.string() );
    srv.set_enable_file_func( true );
    REQUIRE( srv.start( "127.0.0.1", 0 ).has_value() );

    auto prev = fs::current_path();
    fs::current_path( app );

    ADSHANDLE h = remote_connect_embedded( data, srv.port() );
    oads_round_trip( h, "remote-embedded" );

    // verify file lands in server data dir, not client cwd
    CHECK_FALSE( fs::exists( app / "oads_test.txt" ) );
    CHECK( fs::exists( data / "oads_test.txt" ) );

    REQUIRE( AdsDisconnect( h ) == 0 );
    fs::current_path( prev );
    fs::remove_all( data );
    fs::remove_all( app );
}

TEST_CASE( "oads_* remote embedded: EnableFileFunc off denies" )
{
    using openads::network::Server;
    auto data = fs::temp_directory_path() / "oads_file_remote_deny";
    fs::remove_all( data );
    fs::create_directories( data );

    Server srv;
    srv.set_data_dir( data.string() );
    srv.set_enable_file_func( false );
    REQUIRE( srv.start( "127.0.0.1", 0 ).has_value() );

    ADSHANDLE h = remote_connect_embedded( data, srv.port() );
    ADSHANDLE hf = 0;
    UNSIGNED32 rc = oads_FCreate( h, (UNSIGNED8*)"nope.txt", 0, &hf );
    INFO( "FCreate with file func disabled rc=", rc );
    CHECK( rc != 0 );
    CHECK( hf == 0 );

    rc = oads_FOpen( h, (UNSIGNED8*)"nope.txt", ADS_READONLY, &hf );
    INFO( "FOpen with file func disabled rc=", rc );
    CHECK( rc != 0 );

    REQUIRE( AdsDisconnect( h ) == 0 );
    fs::remove_all( data );
}

// -- REMOTE tests (live server — iMac on LAN) ------------------------

TEST_CASE( "oads_* remote live: full round-trip against iMac"
           * doctest::skip( remote_uri_env() == nullptr ) )
{
    const char* uri = remote_uri_env();
    ADSHANDLE h = remote_connect_live( uri );
    oads_round_trip( h, "remote-live" );
    REQUIRE( AdsDisconnect( h ) == 0 );
}
