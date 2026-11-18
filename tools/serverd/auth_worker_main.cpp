#include "engine/hrb_harbour.h"
#include <iostream>
#include "hbapigt.h"
#include "hbapierr.h"
#include <algorithm>
#include <iterator>
#include <string>
#include <filesystem>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <aclapi.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/resource.h>
#endif
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
// Worker-only string RTL surface requested by Pritpal Bedi.
// Explicit references keep the selected static Harbour RTL objects linked.
HB_FUNC_EXTERN( ALLTRIM );
HB_FUNC_EXTERN( ASC );
HB_FUNC_EXTERN( AT );
HB_FUNC_EXTERN( BIN2I );
HB_FUNC_EXTERN( BIN2L );
HB_FUNC_EXTERN( BIN2W );
HB_FUNC_EXTERN( CHR );
HB_FUNC_EXTERN( DESCEND );
HB_FUNC_EXTERN( HARDCR );
HB_FUNC_EXTERN( HB_ANSITOOEM );
HB_FUNC_EXTERN( HB_ASCIIISALPHA );
HB_FUNC_EXTERN( HB_ASCIIISDIGIT );
HB_FUNC_EXTERN( HB_ASCIIISLOWER );
HB_FUNC_EXTERN( HB_ASCIIISUPPER );
HB_FUNC_EXTERN( HB_ASCIILOWER );
HB_FUNC_EXTERN( HB_ASCIIUPPER );
HB_FUNC_EXTERN( HB_AT );
HB_FUNC_EXTERN( HB_ATI );
HB_FUNC_EXTERN( HB_ATOKENS );
HB_FUNC_EXTERN( HB_ATX );
HB_FUNC_EXTERN( HB_BASE64DECODE );
HB_FUNC_EXTERN( HB_BASE64ENCODE );
HB_FUNC_EXTERN( HB_BAT );
HB_FUNC_EXTERN( HB_BCHAR );
HB_FUNC_EXTERN( HB_BCODE );
HB_FUNC_EXTERN( HB_BLEFT );
HB_FUNC_EXTERN( HB_BLEN );
HB_FUNC_EXTERN( HB_BPADC );
HB_FUNC_EXTERN( HB_BPADL );
HB_FUNC_EXTERN( HB_BPADR );
HB_FUNC_EXTERN( HB_BPEEK );
HB_FUNC_EXTERN( HB_BPOKE );
HB_FUNC_EXTERN( HB_BRAT );
HB_FUNC_EXTERN( HB_BRIGHT );
HB_FUNC_EXTERN( HB_BSTUFF );
HB_FUNC_EXTERN( HB_BSUBSTR );
HB_FUNC_EXTERN( HB_HEXTONUM );
HB_FUNC_EXTERN( HB_HEXTOSTR );
HB_FUNC_EXTERN( HB_ISREGEX );
HB_FUNC_EXTERN( HB_JSONDECODE );
HB_FUNC_EXTERN( HB_JSONENCODE );
HB_FUNC_EXTERN( HB_LEFTEQ );
HB_FUNC_EXTERN( HB_LEFTEQI );
HB_FUNC_EXTERN( HB_MLEVAL );
HB_FUNC_EXTERN( HB_NTOC );
HB_FUNC_EXTERN( HB_NTOS );
HB_FUNC_EXTERN( HB_NUMTOHEX );
HB_FUNC_EXTERN( HB_OEMTOANSI );
HB_FUNC_EXTERN( HB_RAT );
HB_FUNC_EXTERN( HB_REGEX );
HB_FUNC_EXTERN( HB_REGEXALL );
HB_FUNC_EXTERN( HB_REGEXATX );
HB_FUNC_EXTERN( HB_REGEXCOMP );
HB_FUNC_EXTERN( HB_REGEXHAS );
HB_FUNC_EXTERN( HB_REGEXLIKE );
HB_FUNC_EXTERN( HB_REGEXMATCH );
HB_FUNC_EXTERN( HB_REGEXSPLIT );
HB_FUNC_EXTERN( HB_STRCDECODE );
HB_FUNC_EXTERN( HB_STRCLEAR );
HB_FUNC_EXTERN( HB_STRDECODESCAPE );
HB_FUNC_EXTERN( HB_STRFORMAT );
HB_FUNC_EXTERN( HB_STRISUTF8 );
HB_FUNC_EXTERN( HB_STRREPLACE );
HB_FUNC_EXTERN( HB_STRSHRINK );
HB_FUNC_EXTERN( HB_STRTOEXP );
HB_FUNC_EXTERN( HB_STRTOHEX );
HB_FUNC_EXTERN( HB_STRTOUTF8 );
HB_FUNC_EXTERN( HB_STRXOR );
HB_FUNC_EXTERN( HB_TOKENCOUNT );
HB_FUNC_EXTERN( HB_TOKENGET );
HB_FUNC_EXTERN( HB_TOKENPTR );
HB_FUNC_EXTERN( HB_TRANSLATE );
HB_FUNC_EXTERN( HB_UAT );
HB_FUNC_EXTERN( HB_UCHAR );
HB_FUNC_EXTERN( HB_UCODE );
HB_FUNC_EXTERN( HB_ULEFT );
HB_FUNC_EXTERN( HB_ULEN );
HB_FUNC_EXTERN( HB_UPADC );
HB_FUNC_EXTERN( HB_UPADL );
HB_FUNC_EXTERN( HB_UPADR );
HB_FUNC_EXTERN( HB_UPEEK );
HB_FUNC_EXTERN( HB_UPOKE );
HB_FUNC_EXTERN( HB_URIGHT );
HB_FUNC_EXTERN( HB_USTUFF );
HB_FUNC_EXTERN( HB_USUBSTR );
HB_FUNC_EXTERN( HB_UTF8ASC );
HB_FUNC_EXTERN( HB_UTF8AT );
HB_FUNC_EXTERN( HB_UTF8CHR );
HB_FUNC_EXTERN( HB_UTF8LEFT );
HB_FUNC_EXTERN( HB_UTF8LEN );
HB_FUNC_EXTERN( HB_UTF8PEEK );
HB_FUNC_EXTERN( HB_UTF8POKE );
HB_FUNC_EXTERN( HB_UTF8RAT );
HB_FUNC_EXTERN( HB_UTF8RIGHT );
HB_FUNC_EXTERN( HB_UTF8STRTRAN );
HB_FUNC_EXTERN( HB_UTF8STUFF );
HB_FUNC_EXTERN( HB_UTF8SUBSTR );
HB_FUNC_EXTERN( HB_UTF8TOSTR );
HB_FUNC_EXTERN( HB_VAL );
HB_FUNC_EXTERN( HB_VALTOSTR );
HB_FUNC_EXTERN( HB_WILDMATCH );
HB_FUNC_EXTERN( HB_WILDMATCHI );
HB_FUNC_EXTERN( I2BIN );
HB_FUNC_EXTERN( ISALPHA );
HB_FUNC_EXTERN( ISDIGIT );
HB_FUNC_EXTERN( ISLOWER );
HB_FUNC_EXTERN( ISUPPER );
HB_FUNC_EXTERN( L2BIN );
HB_FUNC_EXTERN( LEFT );
HB_FUNC_EXTERN( LEN );
HB_FUNC_EXTERN( LOWER );
HB_FUNC_EXTERN( LTRIM );
HB_FUNC_EXTERN( MEMOLINE );
HB_FUNC_EXTERN( MEMOTRAN );
HB_FUNC_EXTERN( MLCOUNT );
HB_FUNC_EXTERN( MLCTOPOS );
HB_FUNC_EXTERN( MLPOS );
HB_FUNC_EXTERN( MPOSTOLC );
HB_FUNC_EXTERN( PAD );
HB_FUNC_EXTERN( PADC );
HB_FUNC_EXTERN( PADL );
HB_FUNC_EXTERN( PADR );
HB_FUNC_EXTERN( RAT );
HB_FUNC_EXTERN( REPLICATE );
HB_FUNC_EXTERN( RIGHT );
HB_FUNC_EXTERN( RTRIM );
HB_FUNC_EXTERN( SOUNDEX );
HB_FUNC_EXTERN( SPACE );
HB_FUNC_EXTERN( STR );
HB_FUNC_EXTERN( STRTRAN );
HB_FUNC_EXTERN( STRZERO );
HB_FUNC_EXTERN( STUFF );
HB_FUNC_EXTERN( SUBSTR );
HB_FUNC_EXTERN( TRANSFORM );
HB_FUNC_EXTERN( TRIM );
HB_FUNC_EXTERN( UPPER );
HB_FUNC_EXTERN( VAL );

// Same byte-reverse semantics as the native OpenADS expression builtin.
// Preserve embedded NULs; this is not Unicode grapheme reversal.
HB_FUNC( REVERSE ) {
    if (!HB_ISCHAR(1)) {
        hb_errRT_BASE_SubstR(EG_ARG, 3012, nullptr, "REVERSE", HB_ERR_ARGS_BASEPARAMS);
        return;
    }
    std::string value(hb_parc(1), static_cast<std::size_t>(hb_parclen(1)));
    std::reverse(value.begin(), value.end());
    hb_retclen(value.data(), static_cast<HB_SIZE>(value.size()));
}
HB_FUNC_EXTERN( HB_SHA256 );
HB_FUNC_EXTERN( HB_SHA384 );
HB_FUNC_EXTERN( HB_SHA512 );
HB_FUNC_EXTERN( HB_HMAC_SHA384 );
HB_FUNC_EXTERN( HB_HMAC_SHA512 );
HB_FUNC_EXTERN( HB_MD5 );
HB_FUNC_EXTERN( HB_HMAC_MD5 );
HB_FUNC_EXTERN( HB_BLOWFISHKEY );
HB_FUNC_EXTERN( HB_BLOWFISHENCRYPT );
HB_FUNC_EXTERN( HB_BLOWFISHDECRYPT );
HB_FUNC_EXTERN( HB_BLOWFISHENCRYPT_CFB );
HB_FUNC_EXTERN( HB_BLOWFISHDECRYPT_CFB );
HB_FUNC_EXTERN( ABS );
HB_FUNC_EXTERN( INT );
HB_FUNC_EXTERN( ROUND );
HB_FUNC_EXTERN( MIN );
HB_FUNC_EXTERN( MAX );
HB_FUNC_EXTERN( EXP );
HB_FUNC_EXTERN( LOG );
HB_FUNC_EXTERN( SQRT );
HB_FUNC_EXTERN( CTOD );
HB_FUNC_EXTERN( DTOC );
HB_FUNC_EXTERN( DTOS );
HB_FUNC_EXTERN( HB_STOD );
HB_FUNC_EXTERN( YEAR );
HB_FUNC_EXTERN( MONTH );
HB_FUNC_EXTERN( DAY );
HB_FUNC_EXTERN( DOW );
HB_FUNC_EXTERN( DATE );
HB_FUNC_EXTERN( TIME );
HB_FUNC_EXTERN( HB_DATE );
HB_FUNC_EXTERN( HB_DTOT );
HB_FUNC_EXTERN( HB_TTOD );
HB_FUNC_EXTERN( HB_TTON );
HB_FUNC_EXTERN( HB_NTOT );
HB_FUNC_EXTERN( HB_TTOC );
HB_FUNC_EXTERN( HB_CTOT );
HB_FUNC_EXTERN( HB_HOUR );
HB_FUNC_EXTERN( HB_MINUTE );
HB_FUNC_EXTERN( HB_SEC );
HB_FUNC_EXTERN( HB_WEEK );
HB_FUNC_EXTERN( ARRAY );
HB_FUNC_EXTERN( AADD );
HB_FUNC_EXTERN( ASIZE );
HB_FUNC_EXTERN( ATAIL );
HB_FUNC_EXTERN( AINS );
HB_FUNC_EXTERN( ADEL );
HB_FUNC_EXTERN( AFILL );
HB_FUNC_EXTERN( ASCAN );
HB_FUNC_EXTERN( HB_ASCAN );
HB_FUNC_EXTERN( HB_RASCAN );
HB_FUNC_EXTERN( HB_AINS );
HB_FUNC_EXTERN( HB_ADEL );
HB_FUNC_EXTERN( ACOPY );
HB_FUNC_EXTERN( ACLONE );
HB_FUNC_EXTERN( HB_HASH );
HB_FUNC_EXTERN( HB_HHASKEY );
HB_FUNC_EXTERN( HB_HPOS );
HB_FUNC_EXTERN( HB_HGET );
HB_FUNC_EXTERN( HB_HGETDEF );
HB_FUNC_EXTERN( HB_HSET );
HB_FUNC_EXTERN( HB_HDEL );
HB_FUNC_EXTERN( HB_HKEYAT );
HB_FUNC_EXTERN( HB_HVALUEAT );
HB_FUNC_EXTERN( HB_HKEYS );
HB_FUNC_EXTERN( HB_HVALUES );
HB_FUNC_EXTERN( HB_HCLEAR );
HB_FUNC_EXTERN( HB_HFILL );
HB_FUNC_EXTERN( HB_HCLONE );
HB_FUNC_EXTERN( HB_HCOPY );
HB_FUNC_EXTERN( EMPTY );
HB_FUNC_EXTERN( VALTYPE );
HB_FUNC_EXTERN( HB_ISNIL );
HB_FUNC_EXTERN( HB_ISNUMERIC );
HB_FUNC_EXTERN( HB_ISSTRING );
HB_FUNC_EXTERN( HB_ISDATE );
HB_FUNC_EXTERN( HB_ISTIMESTAMP );
HB_FUNC_EXTERN( HB_ISLOGICAL );
HB_FUNC_EXTERN( HB_ISARRAY );
HB_FUNC_EXTERN( HB_ISHASH );
HB_FUNC_EXTERN( HB_ISBLOCK );
HB_FUNC_EXTERN( HB_ISPOINTER );
HB_FUNC_EXTERN( HB_HMAC_SHA256 );
HB_FUNC_EXTERN( HB_DATETIME );
HB_FUNC_EXTERN( HB_TSTOUTC );
HB_FUNC_EXTERN( HB_TSTOSTR );
HB_FUNC_EXTERN( HB_STRTOTS );
HB_FUNC_EXTERN( HB_TTOS );
HB_FUNC_EXTERN( HB_STOT );
HB_FUNC_EXTERN( HB_UTCOFFSET );
HB_FUNC_EXTERN( HB_UTCTOTS );
// Worker-only CSPRNG. Never aliases Harbour's non-cryptographic hb_Random.
#ifdef OPENADS_WITH_TLS
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/platform_util.h>
#endif
HB_FUNC( OADS_RANDOMBYTES ) {
    hb_ret();
    // Return NIL on invalid input or entropy failure. Never weak fallback.
    if (!HB_ISNUM(1)) return;
    const double count = hb_parnd(1);
    if (!(count >= 1 && count <= 1024)) return;
    const auto n = static_cast<std::size_t>(count);
    if (count != static_cast<double>(n)) return;
#ifdef OPENADS_WITH_TLS
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context rng;
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&rng);
    const unsigned char tag[] = "OpenADS-auth-worker-random-v1";
    unsigned char bytes[1024]{};
    int rc = mbedtls_ctr_drbg_seed(&rng, mbedtls_entropy_func, &entropy,
                                  tag, sizeof(tag)-1);
    if (rc == 0) rc = mbedtls_ctr_drbg_random(&rng, bytes, n);
    if (rc == 0) hb_retclen(reinterpret_cast<const char*>(bytes), n);
    mbedtls_platform_zeroize(bytes, sizeof(bytes));
    mbedtls_ctr_drbg_free(&rng);
    mbedtls_entropy_free(&entropy);
#endif
}
HB_INIT_SYMBOLS_BEGIN( openads_auth_worker_rtl__InitSymbols )
{ "HB_MD5", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_MD5 ) }, nullptr },
{ "HB_HMAC_MD5", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_HMAC_MD5 ) }, nullptr },
{ "HB_BLOWFISHKEY", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_BLOWFISHKEY ) }, nullptr },
{ "HB_BLOWFISHENCRYPT", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_BLOWFISHENCRYPT ) }, nullptr },
{ "HB_BLOWFISHDECRYPT", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_BLOWFISHDECRYPT ) }, nullptr },
{ "HB_BLOWFISHENCRYPT_CFB", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_BLOWFISHENCRYPT_CFB ) }, nullptr },
{ "HB_BLOWFISHDECRYPT_CFB", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_BLOWFISHDECRYPT_CFB ) }, nullptr },
{ "ABS", { HB_FS_PUBLIC }, { HB_FUNCNAME( ABS ) }, nullptr },
{ "INT", { HB_FS_PUBLIC }, { HB_FUNCNAME( INT ) }, nullptr },
{ "ROUND", { HB_FS_PUBLIC }, { HB_FUNCNAME( ROUND ) }, nullptr },
{ "MIN", { HB_FS_PUBLIC }, { HB_FUNCNAME( MIN ) }, nullptr },
{ "MAX", { HB_FS_PUBLIC }, { HB_FUNCNAME( MAX ) }, nullptr },
{ "EXP", { HB_FS_PUBLIC }, { HB_FUNCNAME( EXP ) }, nullptr },
{ "LOG", { HB_FS_PUBLIC }, { HB_FUNCNAME( LOG ) }, nullptr },
{ "SQRT", { HB_FS_PUBLIC }, { HB_FUNCNAME( SQRT ) }, nullptr },
{ "CTOD", { HB_FS_PUBLIC }, { HB_FUNCNAME( CTOD ) }, nullptr },
{ "DTOC", { HB_FS_PUBLIC }, { HB_FUNCNAME( DTOC ) }, nullptr },
{ "DTOS", { HB_FS_PUBLIC }, { HB_FUNCNAME( DTOS ) }, nullptr },
{ "HB_STOD", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_STOD ) }, nullptr },
{ "YEAR", { HB_FS_PUBLIC }, { HB_FUNCNAME( YEAR ) }, nullptr },
{ "MONTH", { HB_FS_PUBLIC }, { HB_FUNCNAME( MONTH ) }, nullptr },
{ "DAY", { HB_FS_PUBLIC }, { HB_FUNCNAME( DAY ) }, nullptr },
{ "DOW", { HB_FS_PUBLIC }, { HB_FUNCNAME( DOW ) }, nullptr },
{ "DATE", { HB_FS_PUBLIC }, { HB_FUNCNAME( DATE ) }, nullptr },
{ "TIME", { HB_FS_PUBLIC }, { HB_FUNCNAME( TIME ) }, nullptr },
{ "HB_DATE", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_DATE ) }, nullptr },
{ "HB_DTOT", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_DTOT ) }, nullptr },
{ "HB_TTOD", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_TTOD ) }, nullptr },
{ "HB_TTON", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_TTON ) }, nullptr },
{ "HB_NTOT", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_NTOT ) }, nullptr },
{ "HB_TTOC", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_TTOC ) }, nullptr },
{ "HB_CTOT", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_CTOT ) }, nullptr },
{ "HB_HOUR", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_HOUR ) }, nullptr },
{ "HB_MINUTE", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_MINUTE ) }, nullptr },
{ "HB_SEC", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_SEC ) }, nullptr },
{ "HB_WEEK", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_WEEK ) }, nullptr },
{ "ARRAY", { HB_FS_PUBLIC }, { HB_FUNCNAME( ARRAY ) }, nullptr },
{ "AADD", { HB_FS_PUBLIC }, { HB_FUNCNAME( AADD ) }, nullptr },
{ "ASIZE", { HB_FS_PUBLIC }, { HB_FUNCNAME( ASIZE ) }, nullptr },
{ "ATAIL", { HB_FS_PUBLIC }, { HB_FUNCNAME( ATAIL ) }, nullptr },
{ "AINS", { HB_FS_PUBLIC }, { HB_FUNCNAME( AINS ) }, nullptr },
{ "ADEL", { HB_FS_PUBLIC }, { HB_FUNCNAME( ADEL ) }, nullptr },
{ "AFILL", { HB_FS_PUBLIC }, { HB_FUNCNAME( AFILL ) }, nullptr },
{ "ASCAN", { HB_FS_PUBLIC }, { HB_FUNCNAME( ASCAN ) }, nullptr },
{ "HB_ASCAN", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_ASCAN ) }, nullptr },
{ "HB_RASCAN", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_RASCAN ) }, nullptr },
{ "HB_AINS", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_AINS ) }, nullptr },
{ "HB_ADEL", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_ADEL ) }, nullptr },
{ "ACOPY", { HB_FS_PUBLIC }, { HB_FUNCNAME( ACOPY ) }, nullptr },
{ "ACLONE", { HB_FS_PUBLIC }, { HB_FUNCNAME( ACLONE ) }, nullptr },
{ "HB_HASH", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_HASH ) }, nullptr },
{ "HB_HHASKEY", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_HHASKEY ) }, nullptr },
{ "HB_HPOS", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_HPOS ) }, nullptr },
{ "HB_HGET", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_HGET ) }, nullptr },
{ "HB_HGETDEF", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_HGETDEF ) }, nullptr },
{ "HB_HSET", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_HSET ) }, nullptr },
{ "HB_HDEL", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_HDEL ) }, nullptr },
{ "HB_HKEYAT", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_HKEYAT ) }, nullptr },
{ "HB_HVALUEAT", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_HVALUEAT ) }, nullptr },
{ "HB_HKEYS", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_HKEYS ) }, nullptr },
{ "HB_HVALUES", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_HVALUES ) }, nullptr },
{ "HB_HCLEAR", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_HCLEAR ) }, nullptr },
{ "HB_HFILL", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_HFILL ) }, nullptr },
{ "HB_HCLONE", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_HCLONE ) }, nullptr },
{ "HB_HCOPY", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_HCOPY ) }, nullptr },
{ "EMPTY", { HB_FS_PUBLIC }, { HB_FUNCNAME( EMPTY ) }, nullptr },
{ "VALTYPE", { HB_FS_PUBLIC }, { HB_FUNCNAME( VALTYPE ) }, nullptr },
{ "HB_ISNIL", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_ISNIL ) }, nullptr },
{ "HB_ISNUMERIC", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_ISNUMERIC ) }, nullptr },
{ "HB_ISSTRING", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_ISSTRING ) }, nullptr },
{ "HB_ISDATE", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_ISDATE ) }, nullptr },
{ "HB_ISTIMESTAMP", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_ISTIMESTAMP ) }, nullptr },
{ "HB_ISLOGICAL", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_ISLOGICAL ) }, nullptr },
{ "HB_ISARRAY", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_ISARRAY ) }, nullptr },
{ "HB_ISHASH", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_ISHASH ) }, nullptr },
{ "HB_ISBLOCK", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_ISBLOCK ) }, nullptr },
{ "HB_ISPOINTER", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_ISPOINTER ) }, nullptr },

{ "HB_SHA256", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_SHA256 ) }, nullptr },
{ "HB_SHA384", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_SHA384 ) }, nullptr },
{ "HB_SHA512", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_SHA512 ) }, nullptr },
{ "HB_HMAC_SHA384", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_HMAC_SHA384 ) }, nullptr },
{ "HB_HMAC_SHA512", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_HMAC_SHA512 ) }, nullptr },
{ "OADS_RANDOMBYTES", { HB_FS_PUBLIC }, { HB_FUNCNAME( OADS_RANDOMBYTES ) }, nullptr },

{ "ALLTRIM", { HB_FS_PUBLIC }, { HB_FUNCNAME( ALLTRIM ) }, nullptr },
{ "ASC", { HB_FS_PUBLIC }, { HB_FUNCNAME( ASC ) }, nullptr },
{ "AT", { HB_FS_PUBLIC }, { HB_FUNCNAME( AT ) }, nullptr },
{ "BIN2I", { HB_FS_PUBLIC }, { HB_FUNCNAME( BIN2I ) }, nullptr },
{ "BIN2L", { HB_FS_PUBLIC }, { HB_FUNCNAME( BIN2L ) }, nullptr },
{ "BIN2W", { HB_FS_PUBLIC }, { HB_FUNCNAME( BIN2W ) }, nullptr },
{ "CHR", { HB_FS_PUBLIC }, { HB_FUNCNAME( CHR ) }, nullptr },
{ "DESCEND", { HB_FS_PUBLIC }, { HB_FUNCNAME( DESCEND ) }, nullptr },
{ "HARDCR", { HB_FS_PUBLIC }, { HB_FUNCNAME( HARDCR ) }, nullptr },
{ "HB_ANSITOOEM", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_ANSITOOEM ) }, nullptr },
{ "HB_ASCIIISALPHA", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_ASCIIISALPHA ) }, nullptr },
{ "HB_ASCIIISDIGIT", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_ASCIIISDIGIT ) }, nullptr },
{ "HB_ASCIIISLOWER", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_ASCIIISLOWER ) }, nullptr },
{ "HB_ASCIIISUPPER", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_ASCIIISUPPER ) }, nullptr },
{ "HB_ASCIILOWER", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_ASCIILOWER ) }, nullptr },
{ "HB_ASCIIUPPER", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_ASCIIUPPER ) }, nullptr },
{ "HB_AT", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_AT ) }, nullptr },
{ "HB_ATI", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_ATI ) }, nullptr },
{ "HB_ATOKENS", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_ATOKENS ) }, nullptr },
{ "HB_ATX", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_ATX ) }, nullptr },
{ "HB_BASE64DECODE", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_BASE64DECODE ) }, nullptr },
{ "HB_BASE64ENCODE", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_BASE64ENCODE ) }, nullptr },
{ "HB_BAT", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_BAT ) }, nullptr },
{ "HB_BCHAR", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_BCHAR ) }, nullptr },
{ "HB_BCODE", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_BCODE ) }, nullptr },
{ "HB_BLEFT", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_BLEFT ) }, nullptr },
{ "HB_BLEN", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_BLEN ) }, nullptr },
{ "HB_BPADC", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_BPADC ) }, nullptr },
{ "HB_BPADL", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_BPADL ) }, nullptr },
{ "HB_BPADR", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_BPADR ) }, nullptr },
{ "HB_BPEEK", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_BPEEK ) }, nullptr },
{ "HB_BPOKE", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_BPOKE ) }, nullptr },
{ "HB_BRAT", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_BRAT ) }, nullptr },
{ "HB_BRIGHT", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_BRIGHT ) }, nullptr },
{ "HB_BSTUFF", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_BSTUFF ) }, nullptr },
{ "HB_BSUBSTR", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_BSUBSTR ) }, nullptr },
{ "HB_HEXTONUM", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_HEXTONUM ) }, nullptr },
{ "HB_HEXTOSTR", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_HEXTOSTR ) }, nullptr },
{ "HB_ISREGEX", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_ISREGEX ) }, nullptr },
{ "HB_JSONDECODE", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_JSONDECODE ) }, nullptr },
{ "HB_JSONENCODE", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_JSONENCODE ) }, nullptr },
{ "HB_LEFTEQ", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_LEFTEQ ) }, nullptr },
{ "HB_LEFTEQI", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_LEFTEQI ) }, nullptr },
{ "HB_MLEVAL", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_MLEVAL ) }, nullptr },
{ "HB_NTOC", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_NTOC ) }, nullptr },
{ "HB_NTOS", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_NTOS ) }, nullptr },
{ "HB_NUMTOHEX", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_NUMTOHEX ) }, nullptr },
{ "HB_OEMTOANSI", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_OEMTOANSI ) }, nullptr },
{ "HB_RAT", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_RAT ) }, nullptr },
{ "HB_REGEX", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_REGEX ) }, nullptr },
{ "HB_REGEXALL", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_REGEXALL ) }, nullptr },
{ "HB_REGEXATX", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_REGEXATX ) }, nullptr },
{ "HB_REGEXCOMP", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_REGEXCOMP ) }, nullptr },
{ "HB_REGEXHAS", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_REGEXHAS ) }, nullptr },
{ "HB_REGEXLIKE", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_REGEXLIKE ) }, nullptr },
{ "HB_REGEXMATCH", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_REGEXMATCH ) }, nullptr },
{ "HB_REGEXSPLIT", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_REGEXSPLIT ) }, nullptr },
{ "HB_STRCDECODE", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_STRCDECODE ) }, nullptr },
{ "HB_STRCLEAR", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_STRCLEAR ) }, nullptr },
{ "HB_STRDECODESCAPE", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_STRDECODESCAPE ) }, nullptr },
{ "HB_STRFORMAT", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_STRFORMAT ) }, nullptr },
{ "HB_STRISUTF8", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_STRISUTF8 ) }, nullptr },
{ "HB_STRREPLACE", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_STRREPLACE ) }, nullptr },
{ "HB_STRSHRINK", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_STRSHRINK ) }, nullptr },
{ "HB_STRTOEXP", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_STRTOEXP ) }, nullptr },
{ "HB_STRTOHEX", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_STRTOHEX ) }, nullptr },
{ "HB_STRTOUTF8", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_STRTOUTF8 ) }, nullptr },
{ "HB_STRXOR", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_STRXOR ) }, nullptr },
{ "HB_TOKENCOUNT", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_TOKENCOUNT ) }, nullptr },
{ "HB_TOKENGET", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_TOKENGET ) }, nullptr },
{ "HB_TOKENPTR", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_TOKENPTR ) }, nullptr },
{ "HB_TRANSLATE", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_TRANSLATE ) }, nullptr },
{ "HB_UAT", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_UAT ) }, nullptr },
{ "HB_UCHAR", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_UCHAR ) }, nullptr },
{ "HB_UCODE", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_UCODE ) }, nullptr },
{ "HB_ULEFT", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_ULEFT ) }, nullptr },
{ "HB_ULEN", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_ULEN ) }, nullptr },
{ "HB_UPADC", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_UPADC ) }, nullptr },
{ "HB_UPADL", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_UPADL ) }, nullptr },
{ "HB_UPADR", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_UPADR ) }, nullptr },
{ "HB_UPEEK", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_UPEEK ) }, nullptr },
{ "HB_UPOKE", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_UPOKE ) }, nullptr },
{ "HB_URIGHT", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_URIGHT ) }, nullptr },
{ "HB_USTUFF", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_USTUFF ) }, nullptr },
{ "HB_USUBSTR", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_USUBSTR ) }, nullptr },
{ "HB_UTF8ASC", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_UTF8ASC ) }, nullptr },
{ "HB_UTF8AT", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_UTF8AT ) }, nullptr },
{ "HB_UTF8CHR", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_UTF8CHR ) }, nullptr },
{ "HB_UTF8LEFT", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_UTF8LEFT ) }, nullptr },
{ "HB_UTF8LEN", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_UTF8LEN ) }, nullptr },
{ "HB_UTF8PEEK", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_UTF8PEEK ) }, nullptr },
{ "HB_UTF8POKE", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_UTF8POKE ) }, nullptr },
{ "HB_UTF8RAT", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_UTF8RAT ) }, nullptr },
{ "HB_UTF8RIGHT", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_UTF8RIGHT ) }, nullptr },
{ "HB_UTF8STRTRAN", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_UTF8STRTRAN ) }, nullptr },
{ "HB_UTF8STUFF", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_UTF8STUFF ) }, nullptr },
{ "HB_UTF8SUBSTR", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_UTF8SUBSTR ) }, nullptr },
{ "HB_UTF8TOSTR", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_UTF8TOSTR ) }, nullptr },
{ "HB_VAL", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_VAL ) }, nullptr },
{ "HB_VALTOSTR", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_VALTOSTR ) }, nullptr },
{ "HB_WILDMATCH", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_WILDMATCH ) }, nullptr },
{ "HB_WILDMATCHI", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_WILDMATCHI ) }, nullptr },
{ "I2BIN", { HB_FS_PUBLIC }, { HB_FUNCNAME( I2BIN ) }, nullptr },
{ "ISALPHA", { HB_FS_PUBLIC }, { HB_FUNCNAME( ISALPHA ) }, nullptr },
{ "ISDIGIT", { HB_FS_PUBLIC }, { HB_FUNCNAME( ISDIGIT ) }, nullptr },
{ "ISLOWER", { HB_FS_PUBLIC }, { HB_FUNCNAME( ISLOWER ) }, nullptr },
{ "ISUPPER", { HB_FS_PUBLIC }, { HB_FUNCNAME( ISUPPER ) }, nullptr },
{ "L2BIN", { HB_FS_PUBLIC }, { HB_FUNCNAME( L2BIN ) }, nullptr },
{ "LEFT", { HB_FS_PUBLIC }, { HB_FUNCNAME( LEFT ) }, nullptr },
{ "LEN", { HB_FS_PUBLIC }, { HB_FUNCNAME( LEN ) }, nullptr },
{ "LOWER", { HB_FS_PUBLIC }, { HB_FUNCNAME( LOWER ) }, nullptr },
{ "LTRIM", { HB_FS_PUBLIC }, { HB_FUNCNAME( LTRIM ) }, nullptr },
{ "MEMOLINE", { HB_FS_PUBLIC }, { HB_FUNCNAME( MEMOLINE ) }, nullptr },
{ "MEMOTRAN", { HB_FS_PUBLIC }, { HB_FUNCNAME( MEMOTRAN ) }, nullptr },
{ "MLCOUNT", { HB_FS_PUBLIC }, { HB_FUNCNAME( MLCOUNT ) }, nullptr },
{ "MLCTOPOS", { HB_FS_PUBLIC }, { HB_FUNCNAME( MLCTOPOS ) }, nullptr },
{ "MLPOS", { HB_FS_PUBLIC }, { HB_FUNCNAME( MLPOS ) }, nullptr },
{ "MPOSTOLC", { HB_FS_PUBLIC }, { HB_FUNCNAME( MPOSTOLC ) }, nullptr },
{ "PAD", { HB_FS_PUBLIC }, { HB_FUNCNAME( PAD ) }, nullptr },
{ "PADC", { HB_FS_PUBLIC }, { HB_FUNCNAME( PADC ) }, nullptr },
{ "PADL", { HB_FS_PUBLIC }, { HB_FUNCNAME( PADL ) }, nullptr },
{ "PADR", { HB_FS_PUBLIC }, { HB_FUNCNAME( PADR ) }, nullptr },
{ "RAT", { HB_FS_PUBLIC }, { HB_FUNCNAME( RAT ) }, nullptr },
{ "REPLICATE", { HB_FS_PUBLIC }, { HB_FUNCNAME( REPLICATE ) }, nullptr },
{ "RIGHT", { HB_FS_PUBLIC }, { HB_FUNCNAME( RIGHT ) }, nullptr },
{ "HB_HMAC_SHA256", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_HMAC_SHA256 ) }, nullptr },
{ "HB_DATETIME", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_DATETIME ) }, nullptr },
{ "HB_TSTOUTC", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_TSTOUTC ) }, nullptr },
{ "HB_TSTOSTR", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_TSTOSTR ) }, nullptr },
{ "HB_STRTOTS", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_STRTOTS ) }, nullptr },
{ "HB_TTOS", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_TTOS ) }, nullptr },
{ "HB_STOT", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_STOT ) }, nullptr },
{ "HB_UTCOFFSET", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_UTCOFFSET ) }, nullptr },
{ "HB_UTCTOTS", { HB_FS_PUBLIC }, { HB_FUNCNAME( HB_UTCTOTS ) }, nullptr },
{ "RTRIM", { HB_FS_PUBLIC }, { HB_FUNCNAME( RTRIM ) }, nullptr },
{ "SOUNDEX", { HB_FS_PUBLIC }, { HB_FUNCNAME( SOUNDEX ) }, nullptr },
{ "SPACE", { HB_FS_PUBLIC }, { HB_FUNCNAME( SPACE ) }, nullptr },
{ "STR", { HB_FS_PUBLIC }, { HB_FUNCNAME( STR ) }, nullptr },
{ "STRTRAN", { HB_FS_PUBLIC }, { HB_FUNCNAME( STRTRAN ) }, nullptr },
{ "STRZERO", { HB_FS_PUBLIC }, { HB_FUNCNAME( STRZERO ) }, nullptr },
{ "STUFF", { HB_FS_PUBLIC }, { HB_FUNCNAME( STUFF ) }, nullptr },
{ "SUBSTR", { HB_FS_PUBLIC }, { HB_FUNCNAME( SUBSTR ) }, nullptr },
{ "TRANSFORM", { HB_FS_PUBLIC }, { HB_FUNCNAME( TRANSFORM ) }, nullptr },
{ "TRIM", { HB_FS_PUBLIC }, { HB_FUNCNAME( TRIM ) }, nullptr },
{ "UPPER", { HB_FS_PUBLIC }, { HB_FUNCNAME( UPPER ) }, nullptr },
{ "VAL", { HB_FS_PUBLIC }, { HB_FUNCNAME( VAL ) }, nullptr },
{ "REVERSE", { HB_FS_PUBLIC }, { HB_FUNCNAME( REVERSE ) }, nullptr },
HB_INIT_SYMBOLS_END( openads_auth_worker_rtl__InitSymbols )

namespace {
#ifdef _WIN32
bool read_auth_module(const std::string& path, std::string& contents) {
    const auto wide = std::filesystem::path(path).wstring();
    HANDLE file = CreateFileW(wide.c_str(), GENERIC_READ | READ_CONTROL, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) { std::fprintf(stderr, "Auth module protected open failed (%lu).\n", GetLastError()); return false; }
    BY_HANDLE_FILE_INFORMATION fi{};
    LARGE_INTEGER size{};
    if (!GetFileInformationByHandle(file, &fi) ||
        (fi.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        !GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 1024 * 1024) {
        std::fprintf(stderr, "Auth module file metadata rejected (%lu).\n", GetLastError()); CloseHandle(file); return false;
    }
    PSECURITY_DESCRIPTOR sd = nullptr; PACL acl = nullptr;
    if (GetSecurityInfo(file, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
        nullptr, nullptr, &acl, nullptr, &sd) != ERROR_SUCCESS) { std::fprintf(stderr, "Auth module security descriptor read failed.\n"); CloseHandle(file); return false; }
    HANDLE token = nullptr; DWORD bytes = 0; bool ok = acl != nullptr;
    std::vector<unsigned char> info;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) { std::fprintf(stderr, "Auth module token read failed.\n"); ok = false; }
    if (ok) {
        GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
        info.resize(bytes);
        ok = bytes != 0 && GetTokenInformation(token, TokenUser, info.data(), bytes, &bytes);
    }
    for (DWORD i = 0; ok && i < acl->AceCount; ++i) {
        void* ptr = nullptr; if (!GetAce(acl, i, &ptr)) { ok = false; break; }
        const auto* header = static_cast<ACE_HEADER*>(ptr);
        if (header->AceType == ACCESS_DENIED_ACE_TYPE) continue;
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) { std::fprintf(stderr, "Auth module unexpected ACE type %u.\n", static_cast<unsigned>(header->AceType)); ok = false; break; }
        auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(ptr);
        PSID sid = &ace->SidStart;
        if (!IsValidSid(sid) || (!EqualSid(sid, reinterpret_cast<TOKEN_USER*>(info.data())->User.Sid) &&
            !IsWellKnownSid(sid, WinLocalSystemSid) && !IsWellKnownSid(sid, WinBuiltinAdministratorsSid))) { std::fprintf(stderr, "Auth module ACL has an unapproved principal.\n"); ok = false; }
    }
    if (token) CloseHandle(token);
    if (sd) LocalFree(sd);
    if (ok) {
        contents.resize(static_cast<std::size_t>(size.QuadPart));
        DWORD received = 0;
        ok = ReadFile(file, &contents[0], static_cast<DWORD>(contents.size()), &received, nullptr) && received == contents.size();
    }
    CloseHandle(file);
    if (!ok) { std::fprintf(stderr, "Auth module protected ACL/read validation failed.\n"); std::fill(contents.begin(), contents.end(), '\0'); contents.clear(); }
    return ok;
}
#else
bool read_auth_module(const std::string& path,std::string& bytes) {
    int fd=open(path.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC);struct stat st{};
    if(fd<0)return false;
    if(fstat(fd,&st)!=0 || !S_ISREG(st.st_mode) || st.st_uid!=geteuid() ||
       (st.st_mode&(S_IRWXG|S_IRWXO))!=0 || st.st_size<=0 || st.st_size>1024*1024){close(fd);return false;}
    bytes.resize(static_cast<std::size_t>(st.st_size));std::size_t off=0;
    while(off<bytes.size()){auto n=read(fd,&bytes[off],bytes.size()-off);if(n<=0)break;off+=static_cast<std::size_t>(n);}
    close(fd);return off==bytes.size();
}
#endif
}
int main(int argc,char** argv){
#ifndef _WIN32
    // Before VM startup or untrusted HRB loading. No disk core dump.
    const rlimit memory{256ULL*1024*1024, 256ULL*1024*1024};
    const rlimit cpu{1,1}, core{0,0};
    if (setrlimit(RLIMIT_AS, &memory) || setrlimit(RLIMIT_CPU, &cpu) ||
        setrlimit(RLIMIT_CORE, &core)) return 1;
#endif
#ifdef _WIN32
    _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
#endif
    if(argc!=2)return 1;
    hb_gtSetDefault("NUL");
    // Isolated process owns its own backend. Never register with index UDFs.
    openads::engine::hrb_udf::HarbourBackend backend;std::string err;
    std::string module;
    if(!read_auth_module(argv[1],module))return 1;
    // HB_HRBLOAD accepts in-memory HRB bytes: no check/reopen race.
    if(!backend.load(module,err))return 1;
    if(!backend.has("OADS_AUTH_V1"))return 1;
    std::string input;char b[1024];
    while(std::cin.read(b,sizeof(b))||std::cin.gcount()){input.append(b,static_cast<std::size_t>(std::cin.gcount()));if(input.size()>16384)return 1;}
    if(input=="P"){std::cout<<"READY";return 0;}
    if(input.size()<41||input[0]!='A')return 1;
    using Scalar=openads::engine::hrb_udf::Scalar;Scalar args[3],out;
    args[0].kind=Scalar::Kind::String;args[0].s=input.substr(1,32);
    std::size_t pos=33;
    for(unsigned i=1;i<3;++i){if(pos+4>input.size())return 1;std::size_t n=0;for(unsigned j=0;j<4;++j)n|=std::size_t(static_cast<unsigned char>(input[pos++]))<<(8*j);if(n>input.size()-pos)return 1;args[i].kind=Scalar::Kind::String;args[i].s=input.substr(pos,n);pos+=n;}
    if(pos!=input.size())return 1;
    const char* hook="OADS_AUTH_V1";
    if(!backend.has(hook)||!backend.call(hook,args,3,out,err)||out.kind!=Scalar::Kind::String||out.s.size()>4363)return 1;
    std::cout.write(out.s.data(),static_cast<std::streamsize>(out.s.size()));std::cout.flush();
    // Process exits immediately; no reusable Harbour heap holding credentials.
    return std::cout.good()?0:1;
}
