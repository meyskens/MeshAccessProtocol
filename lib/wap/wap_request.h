/**
 * wap_request.h - WAP/WSP Request Builder for MeshAccessProtocol
 * 
 * Creates WSP (Wireless Session Protocol) PDUs for WAP requests.
 * Based on Kannel's WAP implementation (wsp_pdu.c, wsp_unit.c).
 * 
 * 
 * This implementation creates connectionless (Unit) mode PDUs that can be
 * sent via UDP to a WAPBox server running Kannel.
 */

#ifndef WAP_REQUEST_H
#define WAP_REQUEST_H

#include "wap_types.h"
#include "wmlc_decompiler.h"
#include "wap_response.h"

/**
 * WSP Request Builder class
 * 
 * Creates WSP PDUs for connectionless (Unit) mode requests.
 * Based on Kannel's wsp_pdu_pack() and wsp_unit.c implementation.
 */
class WAPRequest {
public:
    /**
     * Create a WSP GET request PDU for connectionless mode.
     * 
     * Based on Kannel's wsp_pdu.def Get PDU structure:
     *   TYPE(4, 0x4)              - Get PDU marker
     *   UINT(subtype, 4)          - GET=0, OPTIONS=1, HEAD=2, DELETE=3, TRACE=4
     *   UINTVAR(uri_len)          - Length of URI
     *   OCTSTR(uri, uri_len)      - URI bytes
     *   REST(headers)             - Optional request headers
     * 
     * For Unit mode (connectionless), a transaction ID byte is prepended.
     * 
     * @param uri The URI to request (e.g., "http://wap.bevelgacom.be/")
     * @param transactionId Transaction ID (0-255) for correlating response
     * @param outBuffer Output buffer to write the PDU to
     * @param outBufferSize Size of output buffer
     * @param addHostHeader If true, adds Host header from URI
     * @return Size of PDU written, or 0 on error
     */
    static size_t createGetRequest(const char* uri, uint8_t transactionId,
                                    uint8_t* outBuffer, size_t outBufferSize,
                                    bool addHostHeader = true);

    /**
     * Create a WSP GET request with custom headers.
     * 
     * @param uri The URI to request
     * @param transactionId Transaction ID for response correlation
     * @param headers Packed WSP headers (or nullptr for no headers)
     * @param headersLen Length of headers data
     * @param outBuffer Output buffer to write the PDU to
     * @param outBufferSize Size of output buffer
     * @return Size of PDU written, or 0 on error
     */
    static size_t createGetRequestWithHeaders(const char* uri, uint8_t transactionId,
                                               const uint8_t* headers, size_t headersLen,
                                               uint8_t* outBuffer, size_t outBufferSize);

    /**
     * Create a simple User-Agent header.
     * 
     * @param userAgent User-Agent string
     * @param outBuffer Output buffer for header
     * @param outBufferSize Size of output buffer
     * @return Size of header written
     */
    static size_t createUserAgentHeader(const char* userAgent,
                                         uint8_t* outBuffer, size_t outBufferSize);

    /**
     * Create a Host header from a hostname.
     * 
     * @param host Hostname (e.g., "wap.bevelgacom.be")
     * @param outBuffer Output buffer for header
     * @param outBufferSize Size of output buffer
     * @return Size of header written
     */
    static size_t createHostHeader(const char* host,
                                    uint8_t* outBuffer, size_t outBufferSize);

    /**
     * Create an Accept header for a well-known content type.
     * 
     * @param contentTypeCode WSP content type code (e.g., 0x00 for all types)
     * @param outBuffer Output buffer for header
     * @param outBufferSize Size of output buffer
     * @return Size of header written (2 bytes)
     */
    static size_t createAcceptHeader(uint8_t contentTypeCode,
                                      uint8_t* outBuffer, size_t outBufferSize);

    /**
     * Create an Accept-Charset header.
     * Required for Kannel to know the device supports UTF-8/ISO-8859-1.
     * 
     * @param charsetCode IANA charset code (e.g., 0x6A for UTF-8, 0x04 for ISO-8859-1)
     * @param outBuffer Output buffer for header
     * @param outBufferSize Size of output buffer
     * @return Size of header written
     */
    static size_t createAcceptCharsetHeader(uint16_t charsetCode,
                                             uint8_t* outBuffer, size_t outBufferSize);

    /**
     * Create Accept headers for all common content types.
     * Equivalent to HTTP Accept all.
     * 
     * @param outBuffer Output buffer for headers
     * @param outBufferSize Size of output buffer
     * @return Size of headers written
     */
    static size_t createAcceptAllHeaders(uint8_t* outBuffer, size_t outBufferSize);

    /**
     * Encode a value as a WSP uintvar (variable-length unsigned integer).
     * Based on Kannel's octstr_append_uintvar().
     * 
     * @param value Value to encode
     * @param outBuffer Output buffer
     * @param outBufferSize Size of output buffer
     * @return Number of bytes written
     */
    static size_t encodeUintvar(unsigned long value, uint8_t* outBuffer, size_t outBufferSize);

    /**
     * Parse a WSP Reply PDU to extract status and content.
     * Based on Kannel's wsp_pdu_unpack() for Reply PDU.
     * 
     * Reply PDU structure:
     *   TYPE(8, 4)                 - Reply PDU marker (0x04)
     *   UINT(status, 8)            - Status code
     *   UINTVAR(headers_len)       - Length of headers
     *   OCTSTR(headers, headers_len) - Content type and headers
     *   REST(data)                 - Reply body
     * 
     * @param data PDU data (with transaction ID byte already stripped)
     * @param len Length of PDU data
     * @param outStatus Output: HTTP-style status code
     * @param outBody Output: Pointer to body data start
     * @param outBodyLen Output: Length of body
     * @return true if parsing succeeded
     */
    static bool parseReplyPDU(const uint8_t* data, size_t len,
                              int* outStatus, const uint8_t** outBody, size_t* outBodyLen);

    /**
     * Decode a WSP uintvar.
     * 
     * @param data Input data
     * @param len Length of input data
     * @param outValue Output: decoded value
     * @return Number of bytes consumed, or 0 on error
     */
    static size_t decodeUintvar(const uint8_t* data, size_t len, unsigned long* outValue);

    /**
     * Convert WSP status code to HTTP status code.
     * Based on Kannel's status code table.
     * 
     * @param wspStatus WSP status code (0-63 range)
     * @return HTTP status code
     */
    static int wspStatusToHttp(uint8_t wspStatus);

    /**
     * Extract hostname from a URL.
     * 
     * @param url Full URL (e.g., "http://wap.bevelgacom.be/path")
     * @param hostBuffer Output buffer for hostname
     * @param hostBufferSize Size of host buffer
     * @return true if hostname extracted successfully
     */
    static bool extractHostFromUrl(const char* url, char* hostBuffer, size_t hostBufferSize);

private:
    // WSP type marker for GET PDU is 0x4 in high nibble
    static const uint8_t GET_PDU_TYPE = 0x40;
    
    // WSP type marker for Reply PDU
    static const uint8_t REPLY_PDU_TYPE = 0x04;
};

#endif // WAP_REQUEST_H
