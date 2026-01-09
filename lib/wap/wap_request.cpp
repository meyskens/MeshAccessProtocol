/**
 * wap_request.cpp - WAP/WSP Request Builder Implementation
 * 
 * Creates WSP (Wireless Session Protocol) PDUs for WAP requests.
 * Based on Kannel's WAP implementation (wsp_pdu.c, wsp_unit.c).
 * 
 */

 /*
 * Example usage:
 *
 * #include "wap_request.h"
 *
 * uint8_t pdu[256];
 * size_t len = WAPRequest::createGetRequest(
 *   "http://wap.bevelgacom.be/",  // URL
 *   0x01,                          // Transaction ID
 *   pdu, sizeof(pdu),
 *   true                           // Add Host header
 * );
 *
 */

#include "wap_request.h"
#include <cstring>
#include <cstdio>

/**
 * Encode a value as uintvar 
 * - Values 0-127 are encoded in 1 byte
 * - Larger values use continuation bits (MSB=1 means more bytes follow)
 */
size_t WAPRequest::encodeUintvar(unsigned long value, uint8_t* outBuffer, size_t outBufferSize) {
    uint8_t temp[5];  // Max 5 bytes for 32-bit value
    int tempLen = 0;
    
    // Build bytes in reverse order
    temp[tempLen++] = value & 0x7F;  // Last byte, no continuation bit
    value >>= 7;
    
    while (value > 0 && tempLen < 5) {
        temp[tempLen++] = (value & 0x7F) | 0x80;  // Set continuation bit
        value >>= 7;
    }
    
    if ((size_t)tempLen > outBufferSize) {
        return 0;  // Buffer too small
    }
    
    // Write bytes in correct order (reverse of temp)
    for (int i = 0; i < tempLen; i++) {
        outBuffer[i] = temp[tempLen - 1 - i];
    }
    
    return tempLen;
}

/**
 * Decode uintvar. 
 */
size_t WAPRequest::decodeUintvar(const uint8_t* data, size_t len, unsigned long* outValue) {
    if (len == 0 || outValue == nullptr) {
        return 0;
    }
    
    unsigned long value = 0;
    size_t i = 0;
    
    while (i < len && i < 5) {  // Max 5 bytes
        value = (value << 7) | (data[i] & 0x7F);
        if ((data[i] & 0x80) == 0) {
            // No continuation bit - this is the last byte
            *outValue = value;
            return i + 1;
        }
        i++;
    }
    
    return 0;  // Invalid uintvar (too long or missing terminator)
}

/**
 * Extract hostname from URL.
 */
bool WAPRequest::extractHostFromUrl(const char* url, char* hostBuffer, size_t hostBufferSize) {
    if (url == nullptr || hostBuffer == nullptr || hostBufferSize == 0) {
        return false;
    }
    
    // Skip protocol (http:// or https://)
    const char* hostStart = url;
    if (strncmp(url, "http://", 7) == 0) {
        hostStart = url + 7;
    } else if (strncmp(url, "https://", 8) == 0) {
        hostStart = url + 8;
    }
    
    // Find end of host (port or path)
    const char* hostEnd = hostStart;
    while (*hostEnd && *hostEnd != ':' && *hostEnd != '/' && *hostEnd != '?') {
        hostEnd++;
    }
    
    size_t hostLen = hostEnd - hostStart;
    if (hostLen == 0 || hostLen >= hostBufferSize) {
        return false;
    }
    
    memcpy(hostBuffer, hostStart, hostLen);
    hostBuffer[hostLen] = '\0';
    
    return true;
}

/**
 * Create a Host header.
 * 
 * WSP header format: [well-known field code | 0x80] [text-value with NUL terminator]
 */
size_t WAPRequest::createHostHeader(const char* host, uint8_t* outBuffer, size_t outBufferSize) {
    if (host == nullptr || outBuffer == nullptr) {
        return 0;
    }
    
    size_t hostLen = strlen(host);
    size_t needed = 1 + hostLen + 1;  // field code + host + NUL
    
    if (needed > outBufferSize) {
        return 0;
    }
    
    size_t pos = 0;
    
    // Well-known header code for Host (0x16) with high bit set
    outBuffer[pos++] = WSP_HEADER_HOST | 0x80;
    
    // Host value as text-string (NUL-terminated)
    memcpy(&outBuffer[pos], host, hostLen);
    pos += hostLen;
    outBuffer[pos++] = 0x00;  // NUL terminator
    
    return pos;
}

/**
 * Create a User-Agent header.
 */
size_t WAPRequest::createUserAgentHeader(const char* userAgent, uint8_t* outBuffer, size_t outBufferSize) {
    if (userAgent == nullptr || outBuffer == nullptr) {
        return 0;
    }
    
    size_t uaLen = strlen(userAgent);
    size_t needed = 1 + uaLen + 1;  // field code + UA + NUL
    
    if (needed > outBufferSize) {
        return 0;
    }
    
    size_t pos = 0;
    
    outBuffer[pos++] = WSP_HEADER_USER_AGENT | 0x80;
    
    // User-Agent value as text-string
    memcpy(&outBuffer[pos], userAgent, uaLen);
    pos += uaLen;
    outBuffer[pos++] = 0x00;  // NUL terminator
    
    return pos;
}

/**
 * Create an Accept header for a well-known content type.
 * 
 * WSP Accept header can use short-integer form for well-known types:
 * Header code (0x80) + content type code with high bit set
 */
size_t WAPRequest::createAcceptHeader(uint8_t contentTypeCode, uint8_t* outBuffer, size_t outBufferSize) {
    if (outBuffer == nullptr || outBufferSize < 2) {
        return 0;
    }
    
    // Well-known header code for Accept (0x00) with high bit set
    outBuffer[0] = WSP_HEADER_ACCEPT | 0x80;
    // Content type as short-integer (high bit set)
    outBuffer[1] = contentTypeCode | 0x80;
    
    return 2;
}

/**
 * Create an Accept-Charset header.
 * 
 * WSP Accept-Charset header uses well-known charset codes from IANA.
 * Common codes:
 *   0x6A (106) = UTF-8
 *   0x04       = ISO-8859-1
 *   0x03       = US-ASCII
 *   0x00       = Any charset (wildcard *)
 */
size_t WAPRequest::createAcceptCharsetHeader(uint16_t charsetCode, uint8_t* outBuffer, size_t outBufferSize) {
    if (outBuffer == nullptr) {
        return 0;
    }
    
    size_t pos = 0;
    
    // Well-known header code for Accept-Charset (0x01) with high bit set
    if (pos >= outBufferSize) return 0;
    outBuffer[pos++] = WSP_HEADER_ACCEPT_CHARSET | 0x80;
    
    // Charset code: if <= 127, use short-integer (value | 0x80)
    // If > 127, need to use value-length encoding
    if (charsetCode <= 127) {
        if (pos >= outBufferSize) return 0;
        outBuffer[pos++] = (uint8_t)(charsetCode | 0x80);
    } else {
        // For values > 127 (like UTF-8 = 106, which fits, but for future-proofing)
        // Use value-length + integer format
        // Length byte (1 = one byte follows)
        if (pos + 2 > outBufferSize) return 0;
        outBuffer[pos++] = 0x01;  // 1 byte follows
        outBuffer[pos++] = (uint8_t)(charsetCode & 0xFF);
    }
    
    return pos;
}

/**
 * Create Accept headers for all common WAP content types.
 * This is equivalent to "Accept: *\/*" in HTTP.
 */
size_t WAPRequest::createAcceptAllHeaders(uint8_t* outBuffer, size_t outBufferSize) {
    if (outBuffer == nullptr) {
        return 0;
    }
    
    size_t pos = 0;
    
    // Accept headers for WAP content types exactly like the Nokia 7110 sends them
    
    // application/vnd.wap.wmlc (0x14)
    if (pos + 2 <= outBufferSize) {
        pos += createAcceptHeader(WSP_CT_APP_VND_WAP_WMLC, &outBuffer[pos], outBufferSize - pos);
    }
    
    // application/vnd.wap.wmlscriptc (0x15)
    if (pos + 2 <= outBufferSize) {
        pos += createAcceptHeader(WSP_CT_APP_VND_WAP_WMLSCRIPTC, &outBuffer[pos], outBufferSize - pos);
    }
    
    // image/vnd.wap.wbmp (0x21)
    if (pos + 2 <= outBufferSize) {
        pos += createAcceptHeader(WSP_CT_IMAGE_VND_WAP_WBMP, &outBuffer[pos], outBufferSize - pos);
    }
    
    // text/plain (0x03)
    if (pos + 2 <= outBufferSize) {
        pos += createAcceptHeader(WSP_CT_TEXT_PLAIN, &outBuffer[pos], outBufferSize - pos);
    }
    
    // Add Accept-Charset: UTF-8 (IANA code 106 = 0x6A)
    if (pos + 2 <= outBufferSize) {
        pos += createAcceptCharsetHeader(106, &outBuffer[pos], outBufferSize - pos);
    }
    
    // Add Accept-Charset: ISO-8859-1 (IANA code 4)
    if (pos + 2 <= outBufferSize) {
        pos += createAcceptCharsetHeader(4, &outBuffer[pos], outBufferSize - pos);
    }
    
    return pos;
}

/**
 * Create a WSP GET request PDU.
 * 
 * Based on Kannel's wsp_pdu.def and wsp_pdu_pack():
 * 
 * Get PDU structure:
 *   TYPE(4, 0x4)              - 4 bits, value 0x4 (Get PDU marker)
 *   UINT(subtype, 4)          - 4 bits, GET=0
 *   UINTVAR(uri_len)          - URI length as variable-length int
 *   OCTSTR(uri, uri_len)      - URI bytes
 *   REST(headers)             - Optional headers
 * 
 * For connectionless (Unit) mode, transaction ID is prepended. We currently only support connectionless mode.
 */
size_t WAPRequest::createGetRequest(const char* uri, uint8_t transactionId,
                                     uint8_t* outBuffer, size_t outBufferSize,
                                     bool addHostHeader) {
    uint8_t headers[128];
    size_t headersLen = 0;
    
    if (addHostHeader) {
        char host[64];
        if (extractHostFromUrl(uri, host, sizeof(host))) {
            headersLen = createHostHeader(host, headers, sizeof(headers));
        }
    }
    
    // Add User-Agent header
    size_t uaLen = createUserAgentHeader("MAP/1.0", 
                                          &headers[headersLen], 
                                          sizeof(headers) - headersLen);
    headersLen += uaLen;
    
    // Add Accept: */* header (crucial for proper server response)
    size_t acceptLen = createAcceptAllHeaders(&headers[headersLen],
                                               sizeof(headers) - headersLen);
    headersLen += acceptLen;
    
    return createGetRequestWithHeaders(uri, transactionId, headers, headersLen,
                                        outBuffer, outBufferSize);
}

/**
 * Create a WSP GET request with custom headers.
 */
size_t WAPRequest::createGetRequestWithHeaders(const char* uri, uint8_t transactionId,
                                                const uint8_t* headers, size_t headersLen,
                                                uint8_t* outBuffer, size_t outBufferSize) {
    if (uri == nullptr || outBuffer == nullptr) {
        return 0;
    }
    
    size_t uriLen = strlen(uri);
    if (uriLen == 0) {
        return 0;
    }
    
    // Calculate maximum size needed:
    // 1 byte TID + 1 byte type/subtype + 5 bytes max uintvar + URI + headers
    size_t maxNeeded = 1 + 1 + 5 + uriLen + headersLen;
    if (maxNeeded > outBufferSize) {
        return 0;
    }
    
    size_t pos = 0;
    
    // Transaction ID byte (for connectionless/Unit mode)
    // Based on Kannel's wsp_unit.c: tid_byte prepended to PDU
    outBuffer[pos++] = transactionId;
    
    // Type (4 bits) = 0x4 (Get), Subtype (4 bits) = 0x0 (GET method)
    // Combined: 0x40 | 0x00 = 0x40
    outBuffer[pos++] = GET_PDU_TYPE | WSP_GET;
    
    // URI length as uintvar
    size_t uintvarLen = encodeUintvar(uriLen, &outBuffer[pos], outBufferSize - pos);
    if (uintvarLen == 0) {
        return 0;
    }
    pos += uintvarLen;
    
    // URI bytes
    if (pos + uriLen > outBufferSize) {
        return 0;
    }
    memcpy(&outBuffer[pos], uri, uriLen);
    pos += uriLen;
    
    // Headers (REST field - just appended)
    if (headers != nullptr && headersLen > 0) {
        if (pos + headersLen > outBufferSize) {
            return 0;
        }
        memcpy(&outBuffer[pos], headers, headersLen);
        pos += headersLen;
    }
    
    return pos;
}

/**
 * Convert WSP status to HTTP status code.
 * 
 * Based on Kannel's wsp_strings.c WSP status table.
 * WSP uses compact status codes that map to HTTP ranges.
 */
int WAPRequest::wspStatusToHttp(uint8_t wspStatus) {
    // WSP status is encoded as: high nibble = class, low nibble = detail
    // Class 0x10 = 1xx, 0x20 = 2xx, 0x30 = 3xx, 0x40 = 4xx, 0x50 = 5xx
    
    switch (wspStatus) {
        // 1xx Informational
        case 0x10: return 100;  // Continue
        case 0x11: return 101;  // Switching Protocols
        
        // 2xx Success
        case 0x20: return 200;  // OK
        case 0x21: return 201;  // Created
        case 0x22: return 202;  // Accepted
        case 0x23: return 203;  // Non-Authoritative Information
        case 0x24: return 204;  // No Content
        case 0x25: return 205;  // Reset Content
        case 0x26: return 206;  // Partial Content
        
        // 3xx Redirection
        case 0x30: return 300;  // Multiple Choices
        case 0x31: return 301;  // Moved Permanently
        case 0x32: return 302;  // Found (Moved Temporarily)
        case 0x33: return 303;  // See Other
        case 0x34: return 304;  // Not Modified
        case 0x35: return 305;  // Use Proxy
        case 0x37: return 307;  // Temporary Redirect
        
        // 4xx Client Error
        case 0x40: return 400;  // Bad Request
        case 0x41: return 401;  // Unauthorized
        case 0x42: return 402;  // Payment Required
        case 0x43: return 403;  // Forbidden
        case 0x44: return 404;  // Not Found
        case 0x45: return 405;  // Method Not Allowed
        case 0x46: return 406;  // Not Acceptable
        case 0x47: return 407;  // Proxy Authentication Required
        case 0x48: return 408;  // Request Timeout
        case 0x49: return 409;  // Conflict
        case 0x4A: return 410;  // Gone
        case 0x4B: return 411;  // Length Required
        case 0x4C: return 412;  // Precondition Failed
        case 0x4D: return 413;  // Request Entity Too Large
        case 0x4E: return 414;  // Request-URI Too Long
        case 0x4F: return 415;  // Unsupported Media Type
        case 0x50: return 416;  // Requested Range Not Satisfiable
        case 0x51: return 417;  // Expectation Failed
        
        // 5xx Server Error
        case 0x60: return 500;  // Internal Server Error
        case 0x61: return 501;  // Not Implemented
        case 0x62: return 502;  // Bad Gateway
        case 0x63: return 503;  // Service Unavailable
        case 0x64: return 504;  // Gateway Timeout
        case 0x65: return 505;  // HTTP Version Not Supported
        
        default:
            // Unknown status - return as-is or map to generic
            return 500;
    }
}

/**
 * Parse a WSP Reply PDU.
 * 
 * Based on Kannel's wsp_pdu_unpack() for Reply PDU.
 * Note: Transaction ID byte should already be stripped before calling this.
 * 
 * Reply PDU structure:
 *   TYPE(8, 4)                   - 8 bits, value 0x04 (Reply PDU)
 *   UINT(status, 8)              - 8-bit status code
 *   UINTVAR(headers_len)         - Length of headers
 *   OCTSTR(headers, headers_len) - Content type and headers
 *   REST(data)                   - Reply body
 */
bool WAPRequest::parseReplyPDU(const uint8_t* data, size_t len,
                               int* outStatus, const uint8_t** outBody, size_t* outBodyLen) {
    if (data == nullptr || len < 3) {  // Minimum: type + status + empty headers_len
        return false;
    }
    
    size_t pos = 0;
    
    // Check PDU type (should be 0x04 for Reply)
    if (data[pos] != REPLY_PDU_TYPE) {
        return false;
    }
    pos++;
    
    // Status code (8 bits)
    uint8_t wspStatus = data[pos++];
    if (outStatus != nullptr) {
        *outStatus = wspStatusToHttp(wspStatus);
    }
    
    // Headers length (uintvar)
    unsigned long headersLen;
    size_t uintvarBytes = decodeUintvar(&data[pos], len - pos, &headersLen);
    if (uintvarBytes == 0) {
        return false;
    }
    pos += uintvarBytes;
    
    // Skip headers
    if (pos + headersLen > len) {
        return false;  // Headers would exceed PDU length
    }
    pos += headersLen;
    
    // REST is the body
    if (outBody != nullptr) {
        *outBody = (pos < len) ? &data[pos] : nullptr;
    }
    if (outBodyLen != nullptr) {
        *outBodyLen = (pos < len) ? (len - pos) : 0;
    }
    
    return true;
}
