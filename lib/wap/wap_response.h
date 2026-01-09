/**
 * wap_response.h - WAP/WSP Response Decoder for MeshAccessProtocol
 * 
 * Decodes WSP (Wireless Session Protocol) Reply PDUs to HTTP format.
 * Based on Kannel's WAP implementation (wsp_pdu.c, wsp_strings.c).
 * 
 */

#ifndef WAP_RESPONSE_H
#define WAP_RESPONSE_H

#include "wap_types.h"

/**
 * WAPResponse - Decodes WSP PDU responses to HTTP
 * 
 * Parses WSP Reply PDUs and extracts HTTP-style response data including
 * status code, headers, and body content.
 */
class WAPResponse {
public:
    /**
     * Decode a complete WSP response PDU (including transaction ID) to HTTP.
     * 
     * @param pdu Raw PDU data from WAPBox
     * @param pduLen Length of PDU
     * @param response Output: Decoded HTTP response
     * @return true if decoding succeeded
     */
    static bool decode(const uint8_t* pdu, size_t pduLen, HTTPResponse* response);
    
    /**
     * Decode WSP response PDU without transaction ID.
     * 
     * @param data PDU data (transaction ID already stripped)
     * @param len Length of data
     * @param response Output: Decoded HTTP response
     * @return true if decoding succeeded
     */
    static bool decodeWithoutTID(const uint8_t* data, size_t len, HTTPResponse* response);
    
    /**
     * Convert WSP content-type code to string.
     * 
     * @param code WSP content-type code (0-127 range, maps from 0x80+ tokens)
     * @return Content-type string or "application/octet-stream" for unknown
     */
    static const char* contentTypeToString(uint8_t code);
    
    /**
     * Convert HTTP status code to status text.
     * 
     * @param code HTTP status code
     * @return Status text (e.g., "OK", "Not Found")
     */
    static const char* httpStatusToText(int code);
    
    /**
     * Parse WSP headers and populate HTTPResponse fields.
     * 
     * @param headers Raw WSP headers data
     * @param headersLen Length of headers
     * @param response Output: HTTPResponse to populate
     * @return true if parsing succeeded
     */
    static bool parseHeaders(const uint8_t* headers, size_t headersLen, HTTPResponse* response);
    
    /**
     * Format HTTP response as string for printing.
     * Generates an HTTP/1.1 style response.
     * 
     * @param response Decoded HTTP response
     * @param buffer Output buffer
     * @param bufferSize Size of buffer
     * @return Number of bytes written, or 0 on error
     */
    static size_t formatAsHTTP(const HTTPResponse* response, char* buffer, size_t bufferSize);
    
    /**
     * Print HTTP response to stdout (for testing/debugging).
     * 
     * @param response Decoded HTTP response
     */
    static void print(const HTTPResponse* response);
};

#endif // WAP_RESPONSE_H
