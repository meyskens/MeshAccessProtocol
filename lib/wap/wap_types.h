/**
 * wap_types.h - WAP/WSP Type Definitions
 * 
 * Common types for WAP/WSP (Wireless Session Protocol) implementation.
 * Based on Kannel's WAP implementation.
 * 
 * Reference: WAP-230-WSP-20010705-a (WSP Specification)
 */

#ifndef WAP_TYPES_H
#define WAP_TYPES_H

#ifdef ARDUINO
#include <Arduino.h>
#else
// Native build compatibility
#include <cstdint>
#include <cstddef>
#include <cstring>
#endif

#include <cstdint>
#include <cstddef>

// WSP PDU Types (from Kannel wsp_pdu.def)
enum WSPPduType {
    WSP_PDU_CONNECT       = 0x01,
    WSP_PDU_CONNECT_REPLY = 0x02,
    WSP_PDU_REDIRECT      = 0x03,
    WSP_PDU_REPLY         = 0x04,
    WSP_PDU_DISCONNECT    = 0x05,
    WSP_PDU_PUSH          = 0x06,
    WSP_PDU_CONFIRMED_PUSH = 0x07,
    WSP_PDU_SUSPEND       = 0x08,
    WSP_PDU_RESUME        = 0x09
};

// WSP GET subtypes (from Kannel wsp_pdu.def: GET_METHODS = 0x40)
enum WSPGetSubtype {
    WSP_GET     = 0x00,  // GET method
    WSP_OPTIONS = 0x01,
    WSP_HEAD    = 0x02,
    WSP_DELETE  = 0x03,
    WSP_TRACE   = 0x04
};

// WSP POST subtypes (from Kannel wsp_pdu.def: POST_METHODS = 0x60)
enum WSPPostSubtype {
    WSP_POST = 0x00,
    WSP_PUT  = 0x01
};

// WSP well-known header field codes (from Kannel wsp_strings.def)
enum WSPHeaderCode {
    WSP_HEADER_ACCEPT         = 0x00,
    WSP_HEADER_ACCEPT_CHARSET = 0x01,
    WSP_HEADER_ACCEPT_ENCODING = 0x02,
    WSP_HEADER_ACCEPT_LANGUAGE = 0x03,
    WSP_HEADER_ACCEPT_RANGES  = 0x04,
    WSP_HEADER_AGE            = 0x05,
    WSP_HEADER_ALLOW          = 0x06,
    WSP_HEADER_AUTHORIZATION  = 0x07,
    WSP_HEADER_CACHE_CONTROL  = 0x08,
    WSP_HEADER_CONNECTION     = 0x09,
    WSP_HEADER_CONTENT_BASE   = 0x0A,
    WSP_HEADER_CONTENT_ENCODING = 0x0B,
    WSP_HEADER_CONTENT_LANGUAGE = 0x0C,
    WSP_HEADER_CONTENT_LENGTH = 0x0D,
    WSP_HEADER_CONTENT_LOCATION = 0x0E,
    WSP_HEADER_CONTENT_MD5    = 0x0F,
    WSP_HEADER_CONTENT_RANGE  = 0x10,
    WSP_HEADER_CONTENT_TYPE   = 0x11,
    WSP_HEADER_DATE           = 0x12,
    WSP_HEADER_ETAG           = 0x13,
    WSP_HEADER_EXPIRES        = 0x14,
    WSP_HEADER_FROM           = 0x15,
    WSP_HEADER_HOST           = 0x16,
    WSP_HEADER_IF_MODIFIED_SINCE = 0x17,
    WSP_HEADER_IF_MATCH       = 0x18,
    WSP_HEADER_IF_NONE_MATCH  = 0x19,
    WSP_HEADER_IF_RANGE       = 0x1A,
    WSP_HEADER_IF_UNMODIFIED_SINCE = 0x1B,
    WSP_HEADER_LOCATION       = 0x1C,
    WSP_HEADER_LAST_MODIFIED  = 0x1D,
    WSP_HEADER_MAX_FORWARDS   = 0x1E,
    WSP_HEADER_PRAGMA         = 0x1F,
    WSP_HEADER_PROXY_AUTHENTICATE = 0x20,
    WSP_HEADER_PROXY_AUTHORIZATION = 0x21,
    WSP_HEADER_PUBLIC         = 0x22,
    WSP_HEADER_RANGE          = 0x23,
    WSP_HEADER_REFERER        = 0x24,
    WSP_HEADER_RETRY_AFTER    = 0x25,
    WSP_HEADER_SERVER         = 0x26,
    WSP_HEADER_TRANSFER_ENCODING = 0x27,
    WSP_HEADER_UPGRADE        = 0x28,
    WSP_HEADER_USER_AGENT     = 0x29,
    WSP_HEADER_VARY           = 0x2A,
    WSP_HEADER_VIA            = 0x2B,
    WSP_HEADER_WARNING        = 0x2C,
    WSP_HEADER_WWW_AUTHENTICATE = 0x2D,
    WSP_HEADER_CONTENT_DISPOSITION = 0x2E,
    WSP_HEADER_X_WAP_APPLICATION_ID = 0x2F,
    WSP_HEADER_X_WAP_CONTENT_URI = 0x30,
    WSP_HEADER_X_WAP_INITIATOR_URI = 0x31,
    WSP_HEADER_ACCEPT_APPLICATION = 0x32,
    WSP_HEADER_BEARER_INDICATION = 0x33,
    WSP_HEADER_PUSH_FLAG      = 0x34,
    WSP_HEADER_PROFILE        = 0x35,
    WSP_HEADER_PROFILE_DIFF   = 0x36,
    WSP_HEADER_PROFILE_WARNING = 0x37,
    WSP_HEADER_EXPECT         = 0x38,
    WSP_HEADER_TE             = 0x39,
    WSP_HEADER_TRAILER        = 0x3A,
    WSP_HEADER_ENCODING_VERSION = 0x47
};

// WSP Content-Type well-known values (from Kannel wsp_strings.def, table 40)
// Note: Binary token values start at 0x80, but assigned numbers start at 0x00
// So 0x80 = "*/*" (index 0), 0x82 = "text/html" (index 2), etc.
enum WSPContentTypeCode {
    WSP_CT_ANY              = 0x00,  // */*
    WSP_CT_TEXT_ANY         = 0x01,  // text/*
    WSP_CT_TEXT_HTML        = 0x02,  // text/html
    WSP_CT_TEXT_PLAIN       = 0x03,  // text/plain
    WSP_CT_TEXT_X_HDML      = 0x04,  // text/x-hdml
    WSP_CT_TEXT_X_TTML      = 0x05,  // text/x-ttml
    WSP_CT_TEXT_X_VCALENDAR = 0x06,  // text/x-vCalendar
    WSP_CT_TEXT_X_VCARD     = 0x07,  // text/x-vCard
    WSP_CT_TEXT_VND_WAP_WML = 0x08,  // text/vnd.wap.wml
    WSP_CT_TEXT_VND_WAP_WMLSCRIPT = 0x09,  // text/vnd.wap.wmlscript
    WSP_CT_APP_VND_WAP_CATC = 0x0A,  // application/vnd.wap.catc
    WSP_CT_MULTIPART_ANY    = 0x0B,  // multipart/*
    WSP_CT_MULTIPART_MIXED  = 0x0C,  // multipart/mixed
    WSP_CT_MULTIPART_FORM   = 0x0D,  // multipart/form-data
    WSP_CT_MULTIPART_BYTERANGES = 0x0E, // multipart/byteranges
    WSP_CT_MULTIPART_ALT    = 0x0F,  // multipart/alternative
    WSP_CT_APP_ANY          = 0x10,  // application/*
    WSP_CT_APP_JAVA_VM      = 0x11,  // application/java-vm
    WSP_CT_APP_X_WWW_FORM   = 0x12,  // application/x-www-form-urlencoded
    WSP_CT_APP_X_HDMLC      = 0x13,  // application/x-hdmlc
    WSP_CT_APP_VND_WAP_WMLC = 0x14,  // application/vnd.wap.wmlc
    WSP_CT_APP_VND_WAP_WMLSCRIPTC = 0x15,  // application/vnd.wap.wmlscriptc
    WSP_CT_IMAGE_ANY        = 0x1C,  // image/*
    WSP_CT_IMAGE_GIF        = 0x1D,  // image/gif
    WSP_CT_IMAGE_JPEG       = 0x1E,  // image/jpeg
    WSP_CT_IMAGE_TIFF       = 0x1F,  // image/tiff
    WSP_CT_IMAGE_PNG        = 0x20,  // image/png
    WSP_CT_IMAGE_VND_WAP_WBMP = 0x21,  // image/vnd.wap.wbmp
    WSP_CT_APP_XML          = 0x27,  // application/xml
    WSP_CT_TEXT_XML         = 0x28,  // text/xml
    WSP_CT_APP_VND_WAP_WBXML = 0x29,  // application/vnd.wap.wbxml
    WSP_CT_APP_XHTML_XML    = 0x3B,  // application/xhtml+xml
    WSP_CT_TEXT_CSS         = 0x3D,  // text/css
    WSP_CT_APP_OCTET_STREAM = 0x5A   // application/octet-stream
};

/**
 * HTTP Response structure for decoded WSP responses
 */
struct HTTPResponse {
    int statusCode;                  // HTTP status code (200, 404, etc.)
    char statusText[32];             // Status text (OK, Not Found, etc.)
    char contentType[64];            // Content-Type header value
    size_t contentLength;            // Content-Length (body size)
    char server[64];                 // Server header
    char date[64];                   // Date header
    char location[256];              // Location header (for redirects)
    const uint8_t* body;             // Pointer to body data
    size_t bodyLen;                  // Body length
    
    // Raw WSP data for debugging
    uint8_t wspStatus;               // Original WSP status
    const uint8_t* rawHeaders;       // Pointer to raw WSP headers
    size_t rawHeadersLen;            // Raw headers length
};

#endif // WAP_TYPES_H
