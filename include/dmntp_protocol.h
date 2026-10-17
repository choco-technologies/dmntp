/**
 * @file dmntp_protocol.h
 * @brief NTP protocol definitions and types.
 *
 * This header contains NTP timestamp structures, packet formats, and
 * constants per RFC 5905.
 */

#ifndef DMNTP_PROTOCOL_H
#define DMNTP_PROTOCOL_H

#include <stdint.h>

/**
 * NTP timestamp: 32-bit seconds plus a 32-bit binary fraction.
 *
 * NTP uses 1900-01-01 as epoch, unlike Unix 1970-01-01.
 * Seconds are unsigned 32-bit (wrap in 2036), fraction is 2^-32 seconds.
 */
typedef struct __attribute__((packed))
{
    uint32_t seconds;      /**< Seconds since NTP epoch (1900-01-01). */
    uint32_t fraction;     /**< Fractional part, 2^-32 seconds. */
} dmntp_timestamp_t;

/**
 * NTP packet (48 bytes, no authentication).
 *
 * Per RFC 5905 Section 7.3.
 */
typedef struct __attribute__((packed))
{
    /* Byte 0 */
    uint8_t  li_vn_mode;   /**< [7:6] Leap Indicator, [5:3] Version, [2:0] Mode */

    /* Byte 1 */
    uint8_t  stratum;      /**< Stratum (0=unspecified, 1=primary, 2+=secondary) */

    /* Byte 2 */
    int8_t   poll;         /**< Poll interval (signed, in log2 seconds) */

    /* Byte 3 */
    int8_t   precision;    /**< Clock precision (signed, in log2 seconds) */

    /* Bytes 4-11 */
    uint32_t root_delay;       /**< Root delay (32-bit, fixed point) */
    uint32_t root_dispersion;  /**< Root dispersion (32-bit, fixed point) */

    /* Bytes 12-15 */
    uint32_t ref_id;           /**< Reference clock identifier */

    /* Bytes 16-23 */
    dmntp_timestamp_t ref;     /**< Reference timestamp */

    /* Bytes 24-31 */
    dmntp_timestamp_t orig;    /**< Origin timestamp (T1) */

    /* Bytes 32-39 */
    dmntp_timestamp_t recv;    /**< Receive timestamp (T2) */

    /* Bytes 40-47 */
    dmntp_timestamp_t trans;   /**< Transmit timestamp (T3) */
} dmntp_packet_t;

/** NTP packet size (48 bytes without authentication). */
#define DMNTP_PACKET_SIZE 48

/**
 * Leap Indicator values.
 */
#define DMNTP_LI_NO_WARNING   0  /**< No warning */
#define DMNTP_LI_LAST_MIN_61 1  /**< Last minute has 61 seconds */
#define DMNTP_LI_LAST_MIN_59 2  /**< Last minute has 59 seconds */
#define DMNTP_LI_ALARM       3  /**< Clock not synchronized */

/**
 * NTP Mode values.
 */
#define DMNTP_MODE_RESERVED    0
#define DMNTP_MODE_SYMMETRIC   1  /**< Symmetric active */
#define DMNTP_MODE_PASSIVE      2  /**< Symmetric passive */
#define DMNTP_MODE_CLIENT       3  /**< Client */
#define DMNTP_MODE_SERVER       4  /**< Server */
#define DMNTP_MODE_BROADCAST    5  /**< Broadcast */
#define DMNTP_MODE_RESERVED_N   6
#define DMNTP_MODE_RESERVED_N2  7

/**
 * Stratum values.
 */
#define DMNTP_STRATUM_UNSPECIFIED  0  /**< Unspecified or unavailable */
#define DMNTP_STRATUM_PRIMARY      1  /**< Primary server (e.g., atomic clock) */
#define DMNTP_STRATUM_MAX         15 /**< Maximum valid stratum (2-15 are secondary) */
#define DMNTP_STRATUM_KISS         0 /**< Kiss-o'-Death (to be discarded) */
#define DMNTP_STRATUM_UNSYNCHRONIZED 16

/**
 * NTP version (current is 4).
 */
#define DMNTP_VERSION_4 4

/**
 * NTP port (123/UDP).
 */
#define DMNTP_PORT 123

/**
 * NTP epoch offset: seconds between 1900-01-01 and 1970-01-01.
 * 2208988800 = 70 years * 365.25 days/year * 24 hours * 60 min * 60 sec
 */
#define DMNTP_EPOCH_OFFSET 2208988800U

/**
 * Max valid root distance (in seconds).
 * RFC 5905 suggests 1 second as a reasonable maximum.
 */
#define DMNTP_MAX_ROOT_DISTANCE 1.0

/**
 * Extract Leap Indicator from li_vn_mode byte.
 */
#define DMNTP_LI(p) (((p)->li_vn_mode >> 6) & 0x3)

/**
 * Extract NTP Version from li_vn_mode byte.
 */
#define DMNTP_VN(p) (((p)->li_vn_mode >> 3) & 0x7)

/**
 * Extract NTP Mode from li_vn_mode byte.
 */
#define DMNTP_MODE(p) ((p)->li_vn_mode & 0x7)

/**
 * Build li_vn_mode byte.
 */
#define DMNTP_LI_VN_MODE(li, vn, mode) (((li) << 6) | (((vn) & 0x7) << 3) | ((mode) & 0x7))

#endif /* DMNTP_PROTOCOL_H */
