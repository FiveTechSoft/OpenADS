<?php
declare(strict_types=1);

namespace OpenADS\Ffi;

/**
 * ACE numeric constants and error-code names. Values mirror
 * include/openads/ace.h and include/openads/error.h. Names only —
 * no proprietary help text.
 */
final class AceTypes
{
    public const ADS_LOCAL_SERVER  = 1;
    public const ADS_REMOTE_SERVER = 2;

    public const AE_SUCCESS = 0;

    /** ACE field-type codes returned by AdsGetFieldType (from ace.h). */
    public const ADS_LOGICAL   = 1;
    public const ADS_NUMERIC   = 2;
    public const ADS_DATE      = 3;
    public const ADS_STRING    = 4;
    public const ADS_MEMO      = 5;
    public const ADS_DOUBLE    = 10;
    public const ADS_INTEGER   = 11;
    public const ADS_TIME      = 13;
    public const ADS_TIMESTAMP = 14;

    /** Index key types (AdsSeek usKeyType). */
    public const ADS_STRINGKEY = 1;
    public const ADS_DOUBLEKEY = 2;

    /** Seek modes (AdsSeek usSeekType). */
    public const ADS_HARDSEEK = 0;
    public const ADS_SOFTSEEK = 1;

    /** Data Dictionary — database properties (AdsDDGetDatabaseProperty). */
    public const ADS_DD_COMMENT                  = 1;
    public const ADS_DD_ADMIN_PASSWORD           = 2;
    public const ADS_DD_DEFAULT_TABLE_PATH       = 3;
    public const ADS_DD_LOG_IN_REQUIRED          = 5;
    public const ADS_DD_VERIFY_ACCESS_RIGHTS     = 6;
    public const ADS_DD_MAX_FAILED_ATTEMPTS      = 21;

    /** Data Dictionary — RI rule options. */
    public const ADS_DD_RI_CASCADE               = 1;
    public const ADS_DD_RI_RESTRICT              = 2;
    public const ADS_DD_RI_SETNULL               = 3;
    public const ADS_DD_RI_SETDEFAULT            = 4;

    /** Data Dictionary — user properties. */
    public const ADS_DD_USER_PASSWORD            = 1101;
    public const ADS_DD_USER_GROUP_MEMBERSHIP    = 1102;
    public const ADS_DD_USER_BAD_LOGINS          = 1103;

    /** Data Dictionary — table properties (AdsDDGetTableProperty). */
    public const ADS_DD_TABLE_VALIDATION_EXPR    = 200;
    public const ADS_DD_TABLE_VALIDATION_MSG     = 201;
    public const ADS_DD_TABLE_PRIMARY_KEY        = 202;
    public const ADS_DD_TABLE_AUTO_CREATE        = 203;
    public const ADS_DD_TABLE_TYPE               = 204;
    public const ADS_DD_TABLE_PATH               = 205;
    public const ADS_DD_TABLE_FIELD_COUNT        = 206;
    public const ADS_DD_TABLE_OBJ_ID             = 208;
    public const ADS_DD_TABLE_IS_RI_PARENT       = 210;
    public const ADS_DD_TABLE_RELATIVE_PATH      = 211;
    public const ADS_DD_TABLE_CHAR_TYPE          = 212;
    public const ADS_DD_TABLE_DEFAULT_INDEX      = 213;
    public const ADS_DD_TABLE_ENCRYPTION         = 214;
    public const ADS_DD_TABLE_MEMO_BLOCK_SIZE    = 215;
    public const ADS_DD_TABLE_PERMISSION_LEVEL   = 216;

    /** Data Dictionary — table permission levels. */
    public const ADS_DD_TABLE_PERMISSION_NONE    = 0;
    public const ADS_DD_TABLE_PERMISSION_READ    = 1;
    public const ADS_DD_TABLE_PERMISSION_WRITE   = 2;
    public const ADS_DD_TABLE_PERMISSION_DELETE  = 3;
    public const ADS_DD_TABLE_PERMISSION_FULL    = 4;

    /**
     * Data Dictionary — field properties (AdsDDGetFieldProperty).
     * SAP ABI numbering (rddads ads.ch). CAN_NULL / TYPE / LENGTH /
     * DECIMAL return UNSIGNED16 (2 raw LE bytes); the rest are strings.
     * DEFAULT_VALUE / MIN / MAX raise AE_PROPERTY_NOT_SET (5138) when
     * the property is unset. Field comments use the generic
     * ADS_DD_COMMENT property.
     */
    public const ADS_DD_FIELD_DEFAULT_VALUE      = 300;
    public const ADS_DD_FIELD_CAN_NULL           = 301;
    public const ADS_DD_FIELD_MIN_VALUE          = 302;
    public const ADS_DD_FIELD_MAX_VALUE          = 303;
    public const ADS_DD_FIELD_VALIDATION_MSG     = 304;
    public const ADS_DD_FIELD_DEFINITION         = 305;
    public const ADS_DD_FIELD_TYPE               = 306;
    public const ADS_DD_FIELD_LENGTH             = 307;
    public const ADS_DD_FIELD_DECIMAL            = 308;
    /** OpenADS extension (not in the SAP ABI). */
    public const ADS_DD_OA_FIELD_VALIDATION_RULE = 390;

    /**
     * Data Dictionary — index properties (AdsDDGetIndexProperty).
     * SAP ABI numbering. OPTIONS returns an UNSIGNED32 bitmask
     * (ADS_UNIQUE=1, ADS_COMPOUND=2, ADS_CUSTOM=4, ADS_DESCENDING=8);
     * KEY_LENGTH / KEY_TYPE return UNSIGNED16.
     */
    public const ADS_DD_INDEX_FILE_NAME          = 400;
    public const ADS_DD_INDEX_EXPRESSION         = 401;
    public const ADS_DD_INDEX_CONDITION          = 402;
    public const ADS_DD_INDEX_OPTIONS            = 403;
    public const ADS_DD_INDEX_KEY_LENGTH         = 404;
    public const ADS_DD_INDEX_KEY_TYPE           = 405;

    /** Data Dictionary — trigger properties (AdsDDGetTriggerProperty). */
    public const ADS_DD_TRIGGER_TABLE            = 501;
    public const ADS_DD_TRIGGER_EVENT            = 502;
    public const ADS_DD_TRIGGER_CONTAINER        = 503;
    public const ADS_DD_TRIGGER_PROC_NAME        = 504;
    public const ADS_DD_TRIGGER_ENABLED          = 505;
    public const ADS_DD_TRIGGER_PRIORITY         = 506;
    public const ADS_DD_TRIGGER_COMMENT          = 507;

    /** Data Dictionary — procedure properties (AdsDDGetProcProperty). */
    public const ADS_DD_PROC_INPUT               = 601;
    public const ADS_DD_PROC_OUTPUT              = 602;
    public const ADS_DD_PROC_CONTAINER           = 603;
    public const ADS_DD_PROC_PROC_NAME           = 604;
    public const ADS_DD_PROC_COMMENT             = 605;

    /** Data Dictionary — view properties (AdsDDGetViewProperty). */
    public const ADS_DD_VIEW_STMT                = 701;
    public const ADS_DD_VIEW_COMMENT             = 702;

    /** Subset of AE_* codes worth naming in error messages. */
    private const ERROR_NAMES = [
        0    => 'AE_SUCCESS',
        5000 => 'AE_INTERNAL_ERROR',
        5004 => 'AE_FUNCTION_NOT_AVAILABLE',
        5026 => 'AE_NO_CURRENT_RECORD',
    ];

    public static function errorName(int $code): string
    {
        return self::ERROR_NAMES[$code] ?? "AE_UNKNOWN($code)";
    }
}
