#pragma once

#include <cstdint>

// openads/ace.h defines a subset of these names as preprocessor
// macros for clipper / Harbour-style consumers. Undefining them
// here keeps the enum members below from getting macro-expanded
// when both headers are pulled into the same translation unit.
#undef AE_SUCCESS
#undef AE_INTERNAL_ERROR
#undef AE_FUNCTION_NOT_AVAILABLE
#undef AE_LOCKED
#undef AE_LOCK_FAILED
#undef AE_NO_CONNECTION
#undef AE_NO_FILE_FOUND
#undef AE_COLUMN_NOT_FOUND
#undef AE_TABLE_NOT_FOUND
#undef AE_TABLE_CORRUPTED
#undef AE_INVALID_CONNECTION_HANDLE
#undef AE_PARSE_ERROR
#undef AE_INVALID_SQL_TOKEN
#undef AE_TYPE_MISMATCH
#undef AE_DIVISION_BY_ZERO
#undef AE_LOGIN_FAILED
#undef AE_REMOTE_ERROR
#undef AE_SAP_PERMS_NEED_IMPORT
#undef AE_UNIQUE_INDEX_VIOLATION
#undef AE_INVALID_WORKAREA
#undef AE_NO_CURRENT_RECORD
#undef AE_INSUFFICIENT_BUFFER
#undef AE_INVALID_EXPRESSION
#undef AE_INVALID_PROPERTY_ID
#undef AE_PROPERTY_NOT_SET
#undef AE_COLUMN_CANNOT_BE_NULL
#undef AE_NOT_VFP_NULLABLE_FIELD

namespace openads {

// Mirror of the ACE error code surface OpenADS will emit.
// See README "Error handling" section for the full table.
enum : std::uint32_t {
    AE_SUCCESS                  = 0,
    AE_INTERNAL_ERROR           = 5000,
    AE_FUNCTION_NOT_AVAILABLE   = 5004,
    AE_LOCKED                   = 5012,
    AE_LOCK_FAILED              = 5013,
    AE_NO_CONNECTION            = 5036,
    AE_NO_FILE_FOUND            = 5018,
    // SAP ACE returns 5059 ("no matching file" / "no more files") from
    // AdsFindFirstTable / AdsFindNextTable when a mask matches nothing or
    // the enumeration is exhausted. 5018 there makes ARC abort its table
    // listing (verified against ace32.dll.sap, 2026-08-13).
    AE_NO_MATCHING_FILE         = 5059,
    AE_COLUMN_NOT_FOUND         = 5063,
    AE_TABLE_NOT_FOUND          = 5066,
    AE_TABLE_CORRUPTED          = 5103,
    AE_INVALID_CONNECTION_HANDLE = 4097,
    AE_PARSE_ERROR              = 7200,
    AE_INVALID_SQL_TOKEN        = 7201,
    AE_TYPE_MISMATCH            = 7041,
    AE_DIVISION_BY_ZERO         = 7042,
    AE_LOGIN_FAILED             = 7077,
    AE_ACCESS_DENIED            = 7079,
    AE_REMOTE_ERROR             = 5172,
    AE_SAP_PERMS_NEED_IMPORT    = 5174, // DD has SAP-format permissions; run import tool
    AE_RI_VIOLATION             = 508,  // referential integrity constraint failed
    AE_UNIQUE_INDEX_VIOLATION   = 5135, // duplicate key in unique/candidate index
    // 5026 = AE_INVALID_WORKAREA (SAP SDK). Previously mislabelled as
    // AE_NO_CURRENT_RECORD in this codebase; kept here for completeness.
    AE_INVALID_WORKAREA         = 5026,
    // 5068 = AE_NO_CURRENT_RECORD (SAP SDK). Harbour rddads special-cases
    // this exact code to return blank-typed values at BOF/EOF; any other
    // error code causes a hard runtime error in the caller.
    AE_NO_CURRENT_RECORD        = 5068,
    // 5051 = AE_INSUFFICIENT_BUFFER (SAP SDK). Returned by AdsGetRecord and
    // peers when the caller's buffer is smaller than the data; the required
    // size is written back through the length out-parameter so the caller
    // can resize and retry.
    AE_INSUFFICIENT_BUFFER      = 5051,
    // 5079 = AE_INVALID_EXPRESSION (SAP SDK). Returned by AdsSetAOF when the
    // filter expression cannot be optimised into a server-side AOF, causing
    // the stock rddads RDD to fall back to its client-side row filter.
    AE_INVALID_EXPRESSION       = 5079,
    // 5126 = AE_INVALID_PROPERTY_ID (SAP SDK). AdsDDGet/Set*Property with
    // a usPropertyID that is not valid for the object kind.
    AE_INVALID_PROPERTY_ID      = 5126,
    // 5138 = AE_PROPERTY_NOT_SET (SAP SDK). AdsDDGet*Property when the
    // requested property has no value for the object (e.g. a field with
    // no default value).
    AE_PROPERTY_NOT_SET         = 5138,
    // 5147 = AE_COLUMN_CANNOT_BE_NULL (SAP SDK). Setting NULL on a column
    // that can never hold one (ADT AutoInc / RowVersion).
    AE_COLUMN_CANNOT_BE_NULL    = 5147,
    // 5205 = AE_NOT_VFP_NULLABLE_FIELD (SAP SDK). AdsSetNull on a VFP
    // column that was not declared with the NULL option.
    AE_NOT_VFP_NULLABLE_FIELD   = 5205
};

} // namespace openads
