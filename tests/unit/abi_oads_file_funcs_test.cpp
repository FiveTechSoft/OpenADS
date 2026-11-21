// abi_oads_file_funcs_test.cpp
//
// Exercises ALL 16 oads_*() C functions (the legacy filesystem aliases):
//   oads_FOpen, oads_FCreate, oads_FClose, oads_FRead, oads_FWrite, oads_FSeek,
//   oads_CheckExistence, oads_DeleteFile, oads_RenameFile, oads_CopyFile,
//   oads_GetFileSize, oads_GetFileDate, oads_GetFileTime,
//   oads_DirMake, oads_DirRemove, oads_DirExist, oads_Directory
//
// Tests run in two modes:
//   1. LOCAL  – in-process engine against a temp data dir (always runs).
//   2. REMOTE – against an openads_serverd. Gated on OPENADS_TEST_REMOTE.
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
#include "platform/path.h"

#include <chrono>
#include <atomic>
#include <thread>
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
    // The FRead above consumed 5 bytes from offset 6, so the current
    // position is 11 (reads ADVANCE the file position, POSIX-style).
    rc = oads_FSeek( hf, 0, 1 /*CUR*/, &pos );
    INFO( "FSeek(0,CUR) rc=", rc, " pos=", pos );
    REQUIRE( rc == 0 );
    CHECK( pos == 11 );

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

// -- LOCAL tests: original 6 functions ---------------------------------

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

    // read back – must be "BB", not "BBAA"
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

// -- LOCAL tests: new functions (CheckExistence through Directory) -----

TEST_CASE( "oads_* local: CheckExistence detects existing file" )
{
    auto dir = fs::temp_directory_path() / "oads_file_exist";
    std::error_code ec;
    fs::remove_all( dir, ec );
    fs::create_directories( dir );

    ADSHANDLE h = local_connect( dir );
    ADSHANDLE hf = 0;

    // file does not exist yet
    UNSIGNED16 usExists = 1;
    oads_CheckExistence( h, (UNSIGNED8*)"ghost.txt", &usExists );
    CHECK( usExists == 0 );

    // create file
    REQUIRE( oads_FCreate( h, (UNSIGNED8*)"ghost.txt", 0, &hf ) == 0 );
    UNSIGNED32 nw = 0;
    oads_FWrite( hf, "hi", 2, &nw );
    oads_FClose( hf );

    // now it exists
    usExists = 0;
    REQUIRE( oads_CheckExistence( h, (UNSIGNED8*)"ghost.txt", &usExists ) == 0 );
    CHECK( usExists == 1 );

    REQUIRE( AdsDisconnect( h ) == 0 );
    fs::remove_all( dir, ec );
}

TEST_CASE( "oads_* local: DeleteFile removes a file" )
{
    auto dir = fs::temp_directory_path() / "oads_file_delete";
    std::error_code ec;
    fs::remove_all( dir, ec );
    fs::create_directories( dir );

    ADSHANDLE h = local_connect( dir );
    ADSHANDLE hf = 0;

    // create file
    REQUIRE( oads_FCreate( h, (UNSIGNED8*)"deleteme.txt", 0, &hf ) == 0 );
    UNSIGNED32 nw = 0;
    oads_FWrite( hf, "bye", 3, &nw );
    oads_FClose( hf );

    // delete it
    REQUIRE( oads_DeleteFile( h, (UNSIGNED8*)"deleteme.txt" ) == 0 );

    // confirm gone
    UNSIGNED16 usExists = 1;
    oads_CheckExistence( h, (UNSIGNED8*)"deleteme.txt", &usExists );
    CHECK( usExists == 0 );

    REQUIRE( AdsDisconnect( h ) == 0 );
    fs::remove_all( dir, ec );
}

TEST_CASE( "oads_* local: RenameFile renames a file" )
{
    auto dir = fs::temp_directory_path() / "oads_file_rename";
    std::error_code ec;
    fs::remove_all( dir, ec );
    fs::create_directories( dir );

    ADSHANDLE h = local_connect( dir );
    ADSHANDLE hf = 0;

    // create file
    REQUIRE( oads_FCreate( h, (UNSIGNED8*)"old.txt", 0, &hf ) == 0 );
    UNSIGNED32 nw = 0;
    oads_FWrite( hf, "data", 4, &nw );
    oads_FClose( hf );

    // rename
    REQUIRE( oads_RenameFile( h, (UNSIGNED8*)"old.txt",
                                    (UNSIGNED8*)"new.txt" ) == 0 );

    // old gone
    UNSIGNED16 usExists = 1;
    oads_CheckExistence( h, (UNSIGNED8*)"old.txt", &usExists );
    CHECK( usExists == 0 );

    // new exists
    usExists = 0;
    REQUIRE( oads_CheckExistence( h, (UNSIGNED8*)"new.txt", &usExists ) == 0 );
    CHECK( usExists == 1 );

    // read content from new
    ADSHANDLE hf2 = 0;
    REQUIRE( oads_FOpen( h, (UNSIGNED8*)"new.txt", ADS_READONLY, &hf2 ) == 0 );
    char buf[16] = {};
    UNSIGNED32 nr = 0;
    oads_FRead( hf2, buf, sizeof( buf ), &nr );
    CHECK( nr == 4 );
    CHECK( std::string( buf, nr ) == "data" );
    oads_FClose( hf2 );

    REQUIRE( AdsDisconnect( h ) == 0 );
    fs::remove_all( dir, ec );
}

TEST_CASE( "oads_* local: CopyFile copies a file" )
{
    auto dir = fs::temp_directory_path() / "oads_file_copy";
    std::error_code ec;
    fs::remove_all( dir, ec );
    fs::create_directories( dir );

    ADSHANDLE h = local_connect( dir );
    ADSHANDLE hf = 0;

    // create source
    REQUIRE( oads_FCreate( h, (UNSIGNED8*)"src.txt", 0, &hf ) == 0 );
    UNSIGNED32 nw = 0;
    oads_FWrite( hf, "data", 4, &nw );
    oads_FClose( hf );

    // copy
    REQUIRE( oads_CopyFile( h, (UNSIGNED8*)"src.txt",
                                  (UNSIGNED8*)"dst.txt" ) == 0 );

    // source still exists
    UNSIGNED16 usExists = 0;
    REQUIRE( oads_CheckExistence( h, (UNSIGNED8*)"src.txt", &usExists ) == 0 );
    CHECK( usExists == 1 );

    // target exists with same content
    usExists = 0;
    REQUIRE( oads_CheckExistence( h, (UNSIGNED8*)"dst.txt", &usExists ) == 0 );
    CHECK( usExists == 1 );
    ADSHANDLE hf2 = 0;
    REQUIRE( oads_FOpen( h, (UNSIGNED8*)"dst.txt", ADS_READONLY, &hf2 ) == 0 );
    char buf[16] = {};
    UNSIGNED32 nr = 0;
    oads_FRead( hf2, buf, sizeof( buf ), &nr );
    CHECK( nr == 4 );
    CHECK( std::string( buf, nr ) == "data" );
    oads_FClose( hf2 );

    // overwrite: shorter content replaces, not appends
    REQUIRE( oads_FCreate( h, (UNSIGNED8*)"src2.txt", 0, &hf ) == 0 );
    oads_FWrite( hf, "xy", 2, &nw );
    oads_FClose( hf );
    REQUIRE( oads_CopyFile( h, (UNSIGNED8*)"src2.txt",
                                  (UNSIGNED8*)"dst.txt" ) == 0 );
    UNSIGNED32 ulSize = 0;
    REQUIRE( oads_GetFileSize( h, (UNSIGNED8*)"dst.txt", &ulSize ) == 0 );
    CHECK( ulSize == 2 );

    // missing source fails
    CHECK( oads_CopyFile( h, (UNSIGNED8*)"gone.txt",
                                (UNSIGNED8*)"dst2.txt" ) != 0 );

    REQUIRE( AdsDisconnect( h ) == 0 );
    fs::remove_all( dir, ec );
}

TEST_CASE( "oads_* local: Load Save ReadAt WriteAt Flush Eof" )
{
    auto dir = fs::temp_directory_path() / "oads_file_g2";
    std::error_code ec;
    fs::remove_all( dir, ec );
    fs::create_directories( dir );

    ADSHANDLE h = local_connect( dir );

    // FSave creates a whole file in one call
    REQUIRE( oads_SaveFile( h, (UNSIGNED8*)"w.txt", "hello", 5 ) == 0 );
    UNSIGNED32 ulSize = 0;
    REQUIRE( oads_GetFileSize( h, (UNSIGNED8*)"w.txt", &ulSize ) == 0 );
    CHECK( ulSize == 5 );

    // FSave truncates an existing file
    REQUIRE( oads_SaveFile( h, (UNSIGNED8*)"w.txt", "ab", 2 ) == 0 );
    ulSize = 0;
    REQUIRE( oads_GetFileSize( h, (UNSIGNED8*)"w.txt", &ulSize ) == 0 );
    CHECK( ulSize == 2 );

    // FLoad returns the whole content
    char lbuf[64] = {};
    UNSIGNED32 ulCap = sizeof( lbuf );
    REQUIRE( oads_LoadFile( h, (UNSIGNED8*)"w.txt", lbuf, &ulCap ) == 0 );
    CHECK( ulCap == 2 );
    CHECK( std::string( lbuf, ulCap ) == "ab" );

    // FLoad with a short buffer reports the needed size
    char tiny[1] = {};
    UNSIGNED32 ulNeed = sizeof( tiny );
    CHECK( oads_LoadFile( h, (UNSIGNED8*)"w.txt", tiny, &ulNeed ) ==
           (UNSIGNED32) openads::AE_INSUFFICIENT_BUFFER );
    CHECK( ulNeed == 2 );

    // FLoad of a missing file fails
    UNSIGNED32 ulX = sizeof( lbuf );
    CHECK( oads_LoadFile( h, (UNSIGNED8*)"gone.txt", lbuf, &ulX ) != 0 );

    // Positional I/O on a handle
    ADSHANDLE hf = 0;
    REQUIRE( oads_FCreate( h, (UNSIGNED8*)"p.txt", 0, &hf ) == 0 );
    UNSIGNED32 nw = 0;
    REQUIRE( oads_FWrite( hf, "0123456789", 10, &nw ) == 0 );

    char rbuf[8] = {};
    UNSIGNED32 nr = 0;
    REQUIRE( oads_FReadAt( hf, 4, rbuf, 3, &nr ) == 0 );
    CHECK( nr == 3 );
    CHECK( std::string( rbuf, nr ) == "456" );

    // ReadAt past the end reads nothing
    nr = 99;
    REQUIRE( oads_FReadAt( hf, 50, rbuf, 3, &nr ) == 0 );
    CHECK( nr == 0 );

    // WriteAt overwrites in place; verify through the same handle
    // (a second reader can lag uncommitted CRT writes on Windows).
    REQUIRE( oads_FWriteAt( hf, 2, "XY", 2, &nw ) == 0 );
    CHECK( nw == 2 );
    REQUIRE( oads_FFlush( hf ) == 0 );
    char all[16] = {};
    UNSIGNED32 ulAll = 0;
    REQUIRE( oads_FReadAt( hf, 0, all, 10, &ulAll ) == 0 );
    CHECK( ulAll == 10 );
    CHECK( std::string( all, ulAll ) == "01XY456789" );

    // Eof: not at end, then seek to end and it flips
    UNSIGNED16 bEof = 1;
    REQUIRE( oads_FSeek( hf, 0, 0, &nw ) == 0 );
    REQUIRE( oads_FEof( hf, &bEof ) == 0 );
    CHECK( bEof == 0 );
    REQUIRE( oads_FSeek( hf, 0, 2, &nw ) == 0 );
    bEof = 0;
    REQUIRE( oads_FEof( hf, &bEof ) == 0 );
    CHECK( bEof == 1 );

    oads_FClose( hf );
    CHECK( oads_FEof( hf, &bEof ) != 0 );  // closed handle fails

    // After the close every byte is on disk for a fresh reader too.
    char disk[16] = {};
    UNSIGNED32 ulDisk = sizeof( disk );
    REQUIRE( oads_LoadFile( h, (UNSIGNED8*)"p.txt", disk, &ulDisk ) == 0 );
    CHECK( std::string( disk, ulDisk ) == "01XY456789" );

    REQUIRE( AdsDisconnect( h ) == 0 );
    fs::remove_all( dir, ec );
}

static fs::path lock85_probe_path()
{
    // Resolve before any case changes the working directory: dladdr can
    // return a relative loader path when the test binary is invoked by a
    // relative path, and absolute() would then anchor at the wrong cwd.
    auto module = openads::platform::module_directory();
    REQUIRE( module.has_value() );
    fs::path probe = fs::absolute( fs::path( *module ) ) / "openads_fs_lock85_probe";
#if defined(_WIN32)
    probe += ".exe";
#endif
    return probe;
}

static void lock_round_trip( ADSHANDLE h, const fs::path& dir, const fs::path& probe )
{
    ADSHANDLE a = 0, b = 0, c = 0;
    REQUIRE( oads_FCreate( h, (UNSIGNED8*)"locks.bin", 0, &a ) == 0 );
    REQUIRE( oads_FOpen( h, (UNSIGNED8*)"locks.bin", 2, &b ) == 0 );
    REQUIRE( oads_FOpen( h, (UNSIGNED8*)"locks.bin", 2, &c ) == 0 );
    REQUIRE( oads_FLock( a, 0, 16, 0 ) == 0 );
    CHECK( oads_FLock( b, 0, 16, 0 ) == 5012 );
    CHECK( oads_FLock( b, 0, 16, 0x100 ) == 5012 );
    SIGNED32 result = -1;
    REQUIRE( oads_FLockTest( b, 0, 16, 0, &result ) == 0 );
    CHECK( result > 0 );
    // Locking must not block I/O through the owning native handle.
    UNSIGNED32 wrote = 0, got = 0;
    REQUIRE( oads_FWriteAt( a, 0, "owned", 5, &wrote ) == 0 );
    CHECK( wrote == 5 );
    char buf[8] = {};
    REQUIRE( oads_FReadAt( a, 0, buf, 5, &got ) == 0 );
    CHECK( std::string( buf, got ) == "owned" );
    // A disjoint range can be held concurrently.
    REQUIRE( oads_FLock( b, 64, 16, 0 ) == 0 );
    REQUIRE( oads_FUnlock( b, 64, 16 ) == 0 );

    CHECK( oads_FLock( b, 0, 16, 1 ) != 0 );
    CHECK( oads_FLock( b, 0x7FFFFFFFFFFFFFFFULL, 1, 0 ) != 0 );
    // Harbour refuses own/process-local overlapping relocks, even WAIT.
    CHECK( oads_FLock( b, 0, 16, 0x200 ) == 5012 );
    REQUIRE( oads_FUnlock( a, 0, 16 ) == 0 );
    REQUIRE( oads_FLock( a, 0, 16, 0x100 ) == 0 );
    CHECK( oads_FLock( b, 0, 16, 0x100 ) == 5012 );
    REQUIRE( oads_FUnlock( a, 0, 16 ) == 0 );
    // Adjacent locks merge in the registry. Unix can split that merged
    // range with partial unlock; Windows preserves native exact unlock.
    REQUIRE( oads_FLock( a, 0, 8, 0 ) == 0 );
    REQUIRE( oads_FLock( a, 8, 8, 0 ) == 0 );
#if defined(_WIN32)
    CHECK( oads_FUnlock( a, 4, 4 ) != 0 );
    REQUIRE( oads_FUnlock( a, 0, 8 ) == 0 );
    REQUIRE( oads_FUnlock( a, 8, 8 ) == 0 );
#else
    REQUIRE( oads_FUnlock( a, 4, 4 ) == 0 );
    REQUIRE( oads_FLockTest( b, 4, 4, 0, &result ) == 0 );
    CHECK( result == 0 );
    REQUIRE( oads_FUnlock( a, 0, 4 ) == 0 );
    REQUIRE( oads_FUnlock( a, 8, 8 ) == 0 );
    REQUIRE( oads_FLock( a, 64, 0, 0 ) == 0 ); // to EOF on Unix
    REQUIRE( oads_FUnlock( a, 64, 0 ) == 0 );
#endif
    // A real other process holds byte 128 until it exits. WAIT must
    // complete after that release, without blocking the server thread.
    REQUIRE( fs::exists(probe) );
    auto marker = dir / "holder-ready";
    fs::remove(marker);
    std::string command = "\"" + probe.string() + "\" hold \"" +
                           (dir / "locks.bin").string() + "\" \"" + marker.string() + "\"";
#if defined(_WIN32)
    command = "\"" + command + "\"";
#endif
    std::atomic<int> child_rc{-1};
    std::thread holder([&] { child_rc.store(std::system(command.c_str())); });
    bool ready = false;
    for (int i = 0; i < 300; ++i) {
        if (fs::exists(marker)) { ready = true; break; }
        if (child_rc.load() != -1) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    UNSIGNED32 wait_rc = 999;
    if (ready) {
        CHECK( oads_FLock( a, 128, 16, 0 ) == 5012 );
        CHECK( oads_FLockTest( a, 128, 16, 0, &result ) == 0 );
        CHECK( result > 0 ); // POSIX PID, Windows 1
        wait_rc = oads_FLock( a, 128, 16, 0x200 );
        if (wait_rc == 0) oads_FUnlock( a, 128, 16 );
    }
    holder.join();
    CHECK( ready );
    CHECK( child_rc.load() == 0 );
    CHECK( wait_rc == 0 );
    REQUIRE( oads_FLock( a, 32, 16, 0 ) == 0 );
    REQUIRE( oads_FClose(a) == 0 ); // other references retain merged locks
    CHECK( oads_FLock( b, 32, 16, 0 ) == 5012 );
    REQUIRE( oads_FUnlock( b, 32, 16 ) == 0 );
    REQUIRE( oads_FLockTest( c, 0, 16, 0, &result ) == 0 );
    CHECK( result == 0 );
    REQUIRE( oads_FClose(b) == 0 );
    REQUIRE( oads_FClose(c) == 0 );
    CHECK( oads_FLock( b, 0, 16, 0 ) != 0 );
}

TEST_CASE( "oads_* local: FS byte locks shared exclusive wait and owner IO" )
{
    auto dir = fs::temp_directory_path() / "oads_file_g4";
    fs::remove_all(dir);
    fs::create_directories(dir);
    auto h = local_connect(dir);
    lock_round_trip(h, dir, lock85_probe_path());
    REQUIRE( AdsDisconnect(h) == 0 );
    fs::remove_all(dir);
}

static void metadata_round_trip( ADSHANDLE h, const fs::path& dir )
{
    REQUIRE( oads_SaveFile( h, (UNSIGNED8*)"m.txt", "0123456789", 10 ) == 0 );

    // SetFileAttr: read-only clears write bits, clearing restores them
    auto p = dir / "m.txt";
    REQUIRE( oads_SetFileAttr( h, (UNSIGNED8*)"m.txt", 1 ) == 0 );
    CHECK( ( fs::status( p ).permissions() & fs::perms::owner_write ) ==
           fs::perms::none );
    REQUIRE( oads_SetFileAttr( h, (UNSIGNED8*)"m.txt", 0 ) == 0 );
    CHECK( ( fs::status( p ).permissions() & fs::perms::owner_write ) !=
           fs::perms::none );
    CHECK( oads_SetFileAttr( h, (UNSIGNED8*)"gone.txt", 1 ) != 0 );

    // SetFileTime: round-trip via GetFileDate/GetFileTime
    REQUIRE( oads_SetFileTime( h, (UNSIGNED8*)"m.txt",
                               2021, 3, 4, 5, 6, 7 ) == 0 );
    UNSIGNED8 aucDate[32] = {};
    UNSIGNED16 usLen = sizeof( aucDate );
    REQUIRE( oads_GetFileDate( h, (UNSIGNED8*)"m.txt", aucDate, &usLen ) == 0 );
    REQUIRE( usLen == 9 ); // existing getter includes the terminating NUL
    CHECK( aucDate[8] == 0 );
    CHECK( std::string( (char*) aucDate, 8 ) == "20210304" );
    UNSIGNED8 aucTime[32] = {};
    usLen = sizeof( aucTime );
    REQUIRE( oads_GetFileTime( h, (UNSIGNED8*)"m.txt", aucTime, &usLen ) == 0 );
    CHECK( std::string( (char*) aucTime, usLen ).substr( 0, 5 ) == "05:06" );

    // FTrunc shrinks, then grows while preserving the prefix.
    ADSHANDLE hf = 0;
    REQUIRE( oads_FCreate( h, (UNSIGNED8*)"t.txt", 0, &hf ) == 0 );
    UNSIGNED32 nw = 0;
    REQUIRE( oads_FWrite( hf, "0123456789", 10, &nw ) == 0 );
    REQUIRE( oads_FTrunc( hf, 4 ) == 0 );
    UNSIGNED32 ulSize = 0;
    REQUIRE( oads_GetFileSize( h, (UNSIGNED8*)"t.txt", &ulSize ) == 0 );
    CHECK( ulSize == 4 );
    char buf[16] = {};
    UNSIGNED32 nr = 0;
    REQUIRE( oads_FReadAt( hf, 0, buf, 4, &nr ) == 0 );
    CHECK( nr == 4 );
    CHECK( std::string( buf, nr ) == "0123" );
    REQUIRE( oads_FTrunc( hf, 6 ) == 0 );
    ulSize = 0;
    REQUIRE( oads_GetFileSize( h, (UNSIGNED8*)"t.txt", &ulSize ) == 0 );
    CHECK( ulSize == 6 );
    oads_FClose( hf );
    CHECK( oads_FTrunc( hf, 4 ) != 0 );  // closed handle fails

    // TempFile: unique names that exist, under the jail root
    char t1[4096] = {};
    UNSIGNED16 t1len = sizeof( t1 );
    REQUIRE( oads_TempFile( h, (UNSIGNED8*)"", (UNSIGNED8*)"tmp",
                            (UNSIGNED8*)t1, &t1len ) == 0 );
    CHECK( t1len > 0 );
    ADSHANDLE ht = 0;
    REQUIRE( oads_FOpen( h, (UNSIGNED8*)t1, ADS_READONLY, &ht ) == 0 );
    CHECK( oads_FTrunc( ht, 10 ) != 0 );
    REQUIRE( oads_FClose( ht ) == 0 );
    CHECK( fs::exists( dir / std::string( t1, t1len ) ) );
    CHECK( std::string( t1, t1len ).find( "tmp" ) != std::string::npos );
    char t2[4096] = {};
    UNSIGNED16 t2len = sizeof( t2 );
    REQUIRE( oads_TempFile( h, (UNSIGNED8*)"", (UNSIGNED8*)"tmp",
                            (UNSIGNED8*)t2, &t2len ) == 0 );
    CHECK( std::string( t2, t2len ) != std::string( t1, t1len ) );

    // DirSpace: capacity positive, available/free no more than capacity
    UNSIGNED64 ullCap = 0, ullAvail = 0, ullFree = 0;
    REQUIRE( oads_DirSpace( h, (UNSIGNED8*)"", 3, &ullCap ) == 0 );
    REQUIRE( oads_DirSpace( h, (UNSIGNED8*)"", 0, &ullAvail ) == 0 );
    REQUIRE( oads_DirSpace( h, (UNSIGNED8*)"", 1, &ullFree ) == 0 );
    CHECK( ullCap > 0 );
    CHECK( ullAvail <= ullCap );
    CHECK( ullFree <= ullCap );
    UNSIGNED64 ullUsed = 0;
    REQUIRE( oads_DirSpace( h, (UNSIGNED8*)"", 2, &ullUsed ) == 0 );
    CHECK( ullUsed <= ullCap );
    // Disk use can move between requests; allow 64 MiB of runner activity.
    auto sum = ullUsed + ullFree;
    CHECK( (sum > ullCap ? sum - ullCap : ullCap - sum) < 64ull*1024ull*1024ull );

    CHECK( oads_SetFileAttr( h, (UNSIGNED8*)"../outside.txt", 1 ) != 0 );
    CHECK( oads_SetFileTime( h, (UNSIGNED8*)"../outside.txt", 2021, 3, 4, 5, 6, 7 ) != 0 );
    UNSIGNED16 deniedLen = sizeof( t1 );
    CHECK( oads_TempFile( h, (UNSIGNED8*)"..", (UNSIGNED8*)"deny", (UNSIGNED8*)t1, &deniedLen ) != 0 );
    CHECK( oads_DirSpace( h, (UNSIGNED8*)"..", 0, &ullAvail ) != 0 );
}

TEST_CASE( "oads_* local: SetAttr SetTime FTrunc TempFile DirSpace" )
{
    auto dir = fs::temp_directory_path() / "oads_file_g3";
    std::error_code ec;
    fs::remove_all( dir, ec );
    fs::create_directories( dir );

    ADSHANDLE h = local_connect( dir );

    metadata_round_trip( h, dir );

    REQUIRE( AdsDisconnect( h ) == 0 );
    fs::remove_all( dir, ec );
}

TEST_CASE( "oads_* local: GetFileSize returns correct size" )
{
    auto dir = fs::temp_directory_path() / "oads_file_size";
    std::error_code ec;
    fs::remove_all( dir, ec );
    fs::create_directories( dir );

    ADSHANDLE h = local_connect( dir );
    ADSHANDLE hf = 0;

    // create file with known content
    REQUIRE( oads_FCreate( h, (UNSIGNED8*)"sized.txt", 0, &hf ) == 0 );
    const char* data = "Hello, world!";
    UNSIGNED32 nw = 0;
    oads_FWrite( hf, data, (UNSIGNED32) std::strlen( data ), &nw );
    oads_FClose( hf );

    // check size
    UNSIGNED32 ulSize = 0;
    REQUIRE( oads_GetFileSize( h, (UNSIGNED8*)"sized.txt", &ulSize ) == 0 );
    CHECK( ulSize == 13 );

    REQUIRE( AdsDisconnect( h ) == 0 );
    fs::remove_all( dir, ec );
}

TEST_CASE( "oads_* local: GetFileDate and GetFileTime return data" )
{
    auto dir = fs::temp_directory_path() / "oads_file_datetime";
    std::error_code ec;
    fs::remove_all( dir, ec );
    fs::create_directories( dir );

    ADSHANDLE h = local_connect( dir );
    ADSHANDLE hf = 0;

    // create file
    REQUIRE( oads_FCreate( h, (UNSIGNED8*)"dated.txt", 0, &hf ) == 0 );
    UNSIGNED32 nw = 0;
    oads_FWrite( hf, "x", 1, &nw );
    oads_FClose( hf );

    // GetFileDate
    UNSIGNED8 aucDate[32] = {};
    UNSIGNED16 usLen = sizeof( aucDate );
    REQUIRE( oads_GetFileDate( h, (UNSIGNED8*)"dated.txt", aucDate, &usLen ) == 0 );
    CHECK( usLen >= 8 );  // at least "MM/DD/YY"

    // GetFileTime
    UNSIGNED8 aucTime[32] = {};
    usLen = sizeof( aucTime );
    REQUIRE( oads_GetFileTime( h, (UNSIGNED8*)"dated.txt", aucTime, &usLen ) == 0 );
    CHECK( usLen >= 4 );  // at least "HH:MM"

    REQUIRE( AdsDisconnect( h ) == 0 );
    fs::remove_all( dir, ec );
}

TEST_CASE( "oads_* local: DirMake + DirExist + DirRemove round-trip" )
{
    auto dir = fs::temp_directory_path() / "oads_file_dirops";
    std::error_code ec;
    fs::remove_all( dir, ec );
    fs::create_directories( dir );

    ADSHANDLE h = local_connect( dir );
    const char* dirname = "testdir";

    // dir does not exist yet
    UNSIGNED16 usExists = 1;
    oads_DirExist( h, (UNSIGNED8*) dirname, &usExists );
    CHECK( usExists == 0 );

    // create
    REQUIRE( oads_DirMake( h, (UNSIGNED8*) dirname ) == 0 );

    // exists now
    usExists = 0;
    REQUIRE( oads_DirExist( h, (UNSIGNED8*) dirname, &usExists ) == 0 );
    CHECK( usExists == 1 );

    // remove
    REQUIRE( oads_DirRemove( h, (UNSIGNED8*) dirname ) == 0 );

    // gone
    usExists = 1;
    oads_DirExist( h, (UNSIGNED8*) dirname, &usExists );
    CHECK( usExists == 0 );

    REQUIRE( AdsDisconnect( h ) == 0 );
    fs::remove_all( dir, ec );
}

TEST_CASE( "oads_* local: Directory listing returns file info" )
{
    auto dir = fs::temp_directory_path() / "oads_file_dirlist";
    std::error_code ec;
    fs::remove_all( dir, ec );
    fs::create_directories( dir );

    ADSHANDLE h = local_connect( dir );
    ADSHANDLE hf = 0;

    // create a file so we can list it
    REQUIRE( oads_FCreate( h, (UNSIGNED8*)"listme.txt", 0, &hf ) == 0 );
    UNSIGNED32 nw = 0;
    oads_FWrite( hf, "x", 1, &nw );
    oads_FClose( hf );

    // first call: get required buffer size
    UNSIGNED32 ulLen = 0;
    UNSIGNED32 rc = oads_Directory( h, (UNSIGNED8*)"listme.*", 0, nullptr, &ulLen );
    INFO( "Directory size probe rc=", rc, " ulLen=", ulLen );
    // The function may return error when buffer is null; ulLen should be > 0

    // second call: fill buffer
    std::vector<UNSIGNED8> buf( ulLen > 0 ? ulLen : 4096 );
    ulLen = (UNSIGNED32) buf.size();
    rc = oads_Directory( h, (UNSIGNED8*)"listme.*", 0, buf.data(), &ulLen );
    INFO( "Directory fill rc=", rc, " ulLen=", ulLen );
    CHECK( rc == 0 );
    CHECK( ulLen > 0 );

    REQUIRE( AdsDisconnect( h ) == 0 );
    fs::remove_all( dir, ec );
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
    const fs::path probe85 = lock85_probe_path();
    fs::current_path( app );

    ADSHANDLE h = remote_connect_embedded( data, srv.port() );
    oads_round_trip( h, "remote-embedded" );
    metadata_round_trip( h, data );
    lock_round_trip( h, data, probe85 );

    // Different client connections are separate lock owners, like two
    // Harbour processes. Same-client opens still share merged ranges.
    auto peer = remote_connect_embedded(data, srv.port());
    ADSHANDLE left = 0, right = 0;
    REQUIRE( oads_FOpen(h, (UNSIGNED8*)"locks.bin", 2, &left) == 0 );
    REQUIRE( oads_FOpen(peer, (UNSIGNED8*)"locks.bin", 2, &right) == 0 );
    REQUIRE( oads_FLock(left, 0, 16, 0x100) == 0 );
    REQUIRE( oads_FLock(right, 0, 16, 0x100) == 0 );
    REQUIRE( oads_FUnlock(left, 0, 16) == 0 );
    REQUIRE( oads_FUnlock(right, 0, 16) == 0 );
    REQUIRE( oads_FLock(left, 0, 16, 0) == 0 );
    CHECK( oads_FLock(right, 0, 16, 0) == 5012 );
    std::atomic<bool> done{false};
    std::atomic<UNSIGNED32> wait_rc{999};
    std::thread waiter([&] { wait_rc.store(oads_FLock(right,0,16,0x200)); done.store(true); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK_FALSE(done.load());
    CHECK( oads_FUnlock(left,0,16) == 0 );
    for (int i=0; i<300 && !done.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (!done.load()) oads_FClose(right);
    waiter.join();
    CHECK(wait_rc.load() == 0);
    CHECK(oads_FUnlock(right,0,16) == 0);
    CHECK(oads_FLock(left,32,16,0) == 0);
    CHECK(oads_FClose(left) == 0);
    REQUIRE(AdsDisconnect(h) == 0);
    CHECK(oads_FLock(right,32,16,0) == 0);
    CHECK(oads_FUnlock(right,32,16) == 0);
    CHECK(oads_FClose(right) == 0);
    REQUIRE(AdsDisconnect(peer) == 0);
    h = remote_connect_embedded(data,srv.port());

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

    CHECK( oads_SetFileAttr( h, (UNSIGNED8*)"nope.txt", 0 ) != 0 );
    CHECK( oads_SetFileTime( h, (UNSIGNED8*)"nope.txt", 2021, 3, 4, 5, 6, 7 ) != 0 );
    char name[4096] = {};
    UNSIGNED16 nameLen = sizeof( name );
    CHECK( oads_TempFile( h, (UNSIGNED8*)"", (UNSIGNED8*)"no", (UNSIGNED8*)name, &nameLen ) != 0 );
    UNSIGNED64 bytes = 0;
    CHECK( oads_DirSpace( h, (UNSIGNED8*)"", 0, &bytes ) != 0 );
    REQUIRE( AdsDisconnect( h ) == 0 );
    fs::remove_all( data );
}

// -- REMOTE tests (live server - iMac on LAN) ------------------------

TEST_CASE( "oads_* remote live: full round-trip against iMac"
           * doctest::skip( remote_uri_env() == nullptr ) )
{
    const char* uri = remote_uri_env();
    ADSHANDLE h = remote_connect_live( uri );
    oads_round_trip( h, "remote-live" );
    REQUIRE( AdsDisconnect( h ) == 0 );
}