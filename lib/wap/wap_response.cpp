/**
 * wap_response.cpp - WAP/WSP Response Decoder Implementation
 * 
 * Decodes WSP (Wireless Session Protocol) Reply PDUs to HTTP format.
 * Based on Kannel's WAP implementation (wsp_pdu.c, wsp_strings.c).
 * 
 */

#include "wap_response.h"
#include "wap_request.h"
#include <cstring>
#include <cstdio>

/**
 * Content-type lookup table from Kannel wsp_strings.def
 * Index = WSP code (without 0x80 offset), Value = MIME type string
 */
static const char* contentTypeTable[] = {
    "*/*",                                  // 0x00
    "text/*",                               // 0x01
    "text/html",                            // 0x02
    "text/plain",                           // 0x03
    "text/x-hdml",                          // 0x04
    "text/x-ttml",                          // 0x05
    "text/x-vCalendar",                     // 0x06
    "text/x-vCard",                         // 0x07
    "text/vnd.wap.wml",                     // 0x08
    "text/vnd.wap.wmlscript",               // 0x09
    "application/vnd.wap.catc",             // 0x0A
    "multipart/*",                          // 0x0B
    "multipart/mixed",                      // 0x0C
    "multipart/form-data",                  // 0x0D
    "multipart/byteranges",                 // 0x0E
    "multipart/alternative",                // 0x0F
    "application/*",                        // 0x10
    "application/java-vm",                  // 0x11
    "application/x-www-form-urlencoded",    // 0x12
    "application/x-hdmlc",                  // 0x13
    "application/vnd.wap.wmlc",             // 0x14
    "application/vnd.wap.wmlscriptc",       // 0x15
    "application/vnd.wap.wsic",             // 0x16
    "application/vnd.wap.uaprof",           // 0x17
    "application/vnd.wap.wtls-ca-certificate", // 0x18
    "application/vnd.wap.wtls-user-certificate", // 0x19
    "application/x-x509-ca-cert",           // 0x1A
    "application/x-x509-user-cert",         // 0x1B
    "image/*",                              // 0x1C
    "image/gif",                            // 0x1D
    "image/jpeg",                           // 0x1E
    "image/tiff",                           // 0x1F
    "image/png",                            // 0x20
    "image/vnd.wap.wbmp",                   // 0x21
    "application/vnd.wap.multipart.*",      // 0x22
    "application/vnd.wap.multipart.mixed",  // 0x23
    "application/vnd.wap.multipart.form-data", // 0x24
    "application/vnd.wap.multipart.byteranges", // 0x25
    "application/vnd.wap.multipart.alternative", // 0x26
    "application/xml",                      // 0x27
    "text/xml",                             // 0x28
    "application/vnd.wap.wbxml",            // 0x29
    "application/x-x968-cross-cert",        // 0x2A
    "application/x-x968-ca-cert",           // 0x2B
    "application/x-x968-user-cert",         // 0x2C
    "text/vnd.wap.si",                      // 0x2D
    "application/vnd.wap.sic",              // 0x2E
    "text/vnd.wap.sl",                      // 0x2F
    "application/vnd.wap.slc",              // 0x30
    "text/vnd.wap.co",                      // 0x31
    "application/vnd.wap.coc",              // 0x32
    "application/vnd.wap.multipart.related", // 0x33
    "application/vnd.wap.sia",              // 0x34
    "text/vnd.wap.connectivity-xml",        // 0x35
    "application/vnd.wap.connectivity-wbxml", // 0x36
    "application/pkcs7-mime",               // 0x37
    "application/vnd.wap.hashed-certificate", // 0x38
    "application/vnd.wap.signed-certificate", // 0x39
    "application/vnd.wap.cert-response",    // 0x3A
    "application/xhtml+xml",                // 0x3B
    "application/wml+xml",                  // 0x3C
    "text/css",                             // 0x3D
    "application/vnd.wap.mms-message",      // 0x3E
    "application/vnd.wap.rollover-certificate", // 0x3F
};

static const size_t contentTypeTableSize = sizeof(contentTypeTable) / sizeof(contentTypeTable[0]);

/**
 * Header name lookup table from Kannel wsp_strings.def
 */
static const char* headerNameTable[] = {
    "Accept",               // 0x00
    "Accept-Charset",       // 0x01
    "Accept-Encoding",      // 0x02
    "Accept-Language",      // 0x03
    "Accept-Ranges",        // 0x04
    "Age",                  // 0x05
    "Allow",                // 0x06
    "Authorization",        // 0x07
    "Cache-Control",        // 0x08
    "Connection",           // 0x09
    "Content-Base",         // 0x0A
    "Content-Encoding",     // 0x0B
    "Content-Language",     // 0x0C
    "Content-Length",       // 0x0D
    "Content-Location",     // 0x0E
    "Content-MD5",          // 0x0F
    "Content-Range",        // 0x10
    "Content-Type",         // 0x11
    "Date",                 // 0x12
    "Etag",                 // 0x13
    "Expires",              // 0x14
    "From",                 // 0x15
    "Host",                 // 0x16
    "If-Modified-Since",    // 0x17
    "If-Match",             // 0x18
    "If-None-Match",        // 0x19
    "If-Range",             // 0x1A
    "If-Unmodified-Since",  // 0x1B
    "Location",             // 0x1C
    "Last-Modified",        // 0x1D
    "Max-Forwards",         // 0x1E
    "Pragma",               // 0x1F
    "Proxy-Authenticate",   // 0x20
    "Proxy-Authorization",  // 0x21
    "Public",               // 0x22
    "Range",                // 0x23
    "Referer",              // 0x24
    "Retry-After",          // 0x25
    "Server",               // 0x26
    "Transfer-Encoding",    // 0x27
    "Upgrade",              // 0x28
    "User-Agent",           // 0x29
    "Vary",                 // 0x2A
    "Via",                  // 0x2B
    "Warning",              // 0x2C
    "WWW-Authenticate",     // 0x2D
    "Content-Disposition",  // 0x2E
};

static const size_t headerNameTableSize = sizeof(headerNameTable) / sizeof(headerNameTable[0]);

const char* WAPResponse::contentTypeToString(uint8_t code) {
    if (code < contentTypeTableSize) {
        return contentTypeTable[code];
    }
    return "application/octet-stream";
}

const char* WAPResponse::httpStatusToText(int code) {
    switch (code) {
        case 100: return "Continue";
        case 101: return "Switching Protocols";
        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 203: return "Non-Authoritative Information";
        case 204: return "No Content";
        case 205: return "Reset Content";
        case 206: return "Partial Content";
        case 300: return "Multiple Choices";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 303: return "See Other";
        case 304: return "Not Modified";
        case 305: return "Use Proxy";
        case 307: return "Temporary Redirect";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 402: return "Payment Required";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 406: return "Not Acceptable";
        case 407: return "Proxy Authentication Required";
        case 408: return "Request Timeout";
        case 409: return "Conflict";
        case 410: return "Gone";
        case 411: return "Length Required";
        case 412: return "Precondition Failed";
        case 413: return "Request Entity Too Large";
        case 414: return "Request-URI Too Long";
        case 415: return "Unsupported Media Type";
        case 416: return "Requested Range Not Satisfiable";
        case 417: return "Expectation Failed";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        case 504: return "Gateway Timeout";
        case 505: return "HTTP Version Not Supported";
        default: return "Unknown";
    }
}

bool WAPResponse::parseHeaders(const uint8_t* headers, size_t headersLen, HTTPResponse* response) {
    if (headers == nullptr || response == nullptr || headersLen == 0) {
        return true;  // No headers to parse is not an error
    }
    
    size_t pos = 0;
    
    // First byte of headers is typically Content-Type
    // If high bit set (>= 0x80), it's a well-known content-type
    if (pos < headersLen) {
        uint8_t firstByte = headers[pos];
        
        if (firstByte >= 0x80) {
            // Well-known content-type (short-integer)
            uint8_t ctCode = firstByte & 0x7F;
            const char* ct = contentTypeToString(ctCode);
            strncpy(response->contentType, ct, sizeof(response->contentType) - 1);
            response->contentType[sizeof(response->contentType) - 1] = '\0';
            pos++;
        } else if (firstByte < 0x20) {
            // Value-length followed by content-type with parameters
            unsigned long valueLen = firstByte;
            pos++;
            if (pos < headersLen && headers[pos] >= 0x80) {
                uint8_t ctCode = headers[pos] & 0x7F;
                const char* ct = contentTypeToString(ctCode);
                strncpy(response->contentType, ct, sizeof(response->contentType) - 1);
                response->contentType[sizeof(response->contentType) - 1] = '\0';
            }
            pos += valueLen;
        } else {
            // Text string content-type
            const char* ctStr = (const char*)&headers[pos];
            size_t ctLen = strnlen(ctStr, headersLen - pos);
            if (ctLen < sizeof(response->contentType)) {
                strncpy(response->contentType, ctStr, ctLen);
                response->contentType[ctLen] = '\0';
            }
            pos += ctLen + 1;  // Skip NUL terminator
        }
    }
    
    // Parse remaining headers
    while (pos < headersLen) {
        uint8_t fieldByte = headers[pos];
        
        if (fieldByte >= 0x80) {
            // Well-known header field
            uint8_t fieldCode = fieldByte & 0x7F;
            pos++;
            
            if (pos >= headersLen) break;
            
            // Read value
            uint8_t valueByte = headers[pos];
            
            if (fieldCode == WSP_HEADER_SERVER && fieldCode < headerNameTableSize) {
                // Server header - text string
                if (valueByte < 0x80) {
                    const char* serverStr = (const char*)&headers[pos];
                    size_t serverLen = strnlen(serverStr, headersLen - pos);
                    if (serverLen < sizeof(response->server)) {
                        strncpy(response->server, serverStr, serverLen);
                        response->server[serverLen] = '\0';
                    }
                    pos += serverLen + 1;
                } else {
                    pos++;
                }
            } else if (fieldCode == WSP_HEADER_LOCATION) {
                // Location header - text string
                if (valueByte < 0x80) {
                    const char* locStr = (const char*)&headers[pos];
                    size_t locLen = strnlen(locStr, headersLen - pos);
                    if (locLen < sizeof(response->location)) {
                        strncpy(response->location, locStr, locLen);
                        response->location[locLen] = '\0';
                    }
                    pos += locLen + 1;
                } else {
                    pos++;
                }
            } else if (fieldCode == WSP_HEADER_CONTENT_LENGTH) {
                // Content-Length - integer
                if (valueByte >= 0x80) {
                    response->contentLength = valueByte & 0x7F;
                    pos++;
                } else if (valueByte < 31) {
                    // Multi-octet integer
                    unsigned long len = valueByte;
                    pos++;
                    unsigned long value = 0;
                    for (unsigned long i = 0; i < len && pos < headersLen; i++) {
                        value = (value << 8) | headers[pos++];
                    }
                    response->contentLength = value;
                } else {
                    pos++;
                }
            } else {
                // Skip unknown header value
                if (valueByte >= 0x80) {
                    pos++;
                } else if (valueByte < 31) {
                    pos += 1 + valueByte;
                } else {
                    // Text string - find NUL
                    while (pos < headersLen && headers[pos] != 0) pos++;
                    pos++;  // Skip NUL
                }
            }
        } else if (fieldByte < 0x20) {
            // Shift-delimiter or value-length
            pos++;
        } else {
            // Text string header name - skip to value
            while (pos < headersLen && headers[pos] != 0) pos++;
            pos++;  // Skip NUL
            // Skip value
            if (pos < headersLen) {
                uint8_t valueByte = headers[pos];
                if (valueByte >= 0x80) {
                    pos++;
                } else {
                    while (pos < headersLen && headers[pos] != 0) pos++;
                    pos++;
                }
            }
        }
    }
    
    return true;
}

bool WAPResponse::decode(const uint8_t* pdu, size_t pduLen, HTTPResponse* response) {
    if (pdu == nullptr || pduLen < 4 || response == nullptr) {
        return false;
    }
    
    // Initialize response
    memset(response, 0, sizeof(HTTPResponse));
    
    // First byte is transaction ID - skip it
    return decodeWithoutTID(&pdu[1], pduLen - 1, response);
}

bool WAPResponse::decodeWithoutTID(const uint8_t* data, size_t len, HTTPResponse* response) {
    if (data == nullptr || len < 3 || response == nullptr) {
        return false;
    }
    
    // Initialize response if not already done
    if (response->statusCode == 0) {
        memset(response, 0, sizeof(HTTPResponse));
    }
    
    size_t pos = 0;
    
    // Check PDU type (should be 0x04 for Reply)
    if (data[pos] != 0x04) {
        return false;
    }
    pos++;
    
    // WSP Status code (8 bits)
    response->wspStatus = data[pos++];
    response->statusCode = WAPRequest::wspStatusToHttp(response->wspStatus);
    strncpy(response->statusText, httpStatusToText(response->statusCode), 
            sizeof(response->statusText) - 1);
    
    // Headers length (uintvar)
    unsigned long headersLen;
    size_t uintvarBytes = WAPRequest::decodeUintvar(&data[pos], len - pos, &headersLen);
    if (uintvarBytes == 0) {
        return false;
    }
    pos += uintvarBytes;
    
    // Store raw headers info
    response->rawHeaders = &data[pos];
    response->rawHeadersLen = headersLen;
    
    // Parse headers
    if (headersLen > 0 && pos + headersLen <= len) {
        parseHeaders(&data[pos], headersLen, response);
        pos += headersLen;
    }
    
    // REST is the body
    response->body = (pos < len) ? &data[pos] : nullptr;
    response->bodyLen = (pos < len) ? (len - pos) : 0;
    
    // Set content-length if not set in headers
    if (response->contentLength == 0) {
        response->contentLength = response->bodyLen;
    }
    
    return true;
}

size_t WAPResponse::formatAsHTTP(const HTTPResponse* response, char* buffer, size_t bufferSize) {
    if (response == nullptr || buffer == nullptr || bufferSize < 64) {
        return 0;
    }
    
    size_t pos = 0;
    int written;
    
    // Status line
    written = snprintf(&buffer[pos], bufferSize - pos, 
                       "HTTP/1.1 %d %s\r\n",
                       response->statusCode, response->statusText);
    if (written < 0 || (size_t)written >= bufferSize - pos) return 0;
    pos += written;
    
    // Content-Type header
    if (response->contentType[0] != '\0') {
        written = snprintf(&buffer[pos], bufferSize - pos,
                           "Content-Type: %s\r\n", response->contentType);
        if (written < 0 || (size_t)written >= bufferSize - pos) return 0;
        pos += written;
    }
    
    // Content-Length header
    written = snprintf(&buffer[pos], bufferSize - pos,
                       "Content-Length: %zu\r\n", response->bodyLen);
    if (written < 0 || (size_t)written >= bufferSize - pos) return 0;
    pos += written;
    
    // Server header
    if (response->server[0] != '\0') {
        written = snprintf(&buffer[pos], bufferSize - pos,
                           "Server: %s\r\n", response->server);
        if (written < 0 || (size_t)written >= bufferSize - pos) return 0;
        pos += written;
    }
    
    // Location header (for redirects)
    if (response->location[0] != '\0') {
        written = snprintf(&buffer[pos], bufferSize - pos,
                           "Location: %s\r\n", response->location);
        if (written < 0 || (size_t)written >= bufferSize - pos) return 0;
        pos += written;
    }
    
    // Empty line separating headers from body
    written = snprintf(&buffer[pos], bufferSize - pos, "\r\n");
    if (written < 0 || (size_t)written >= bufferSize - pos) return 0;
    pos += written;
    
    // Body
    if (response->body != nullptr && response->bodyLen > 0) {
        size_t bodyToCopy = response->bodyLen;
        if (pos + bodyToCopy >= bufferSize) {
            bodyToCopy = bufferSize - pos - 1;
        }
        memcpy(&buffer[pos], response->body, bodyToCopy);
        pos += bodyToCopy;
        buffer[pos] = '\0';
    }
    
    return pos;
}

void WAPResponse::print(const HTTPResponse* response) {
    if (response == nullptr) {
        printf("(null response)\n");
        return;
    }
    
    printf("HTTP/1.1 %d %s\n", response->statusCode, response->statusText);
    
    if (response->contentType[0] != '\0') {
        printf("Content-Type: %s\n", response->contentType);
    }
    
    printf("Content-Length: %zu\n", response->bodyLen);
    
    if (response->server[0] != '\0') {
        printf("Server: %s\n", response->server);
    }
    
    if (response->location[0] != '\0') {
        printf("Location: %s\n", response->location);
    }
    
    printf("\n");  // Empty line before body
    
    // Print body (if text content)
    if (response->body != nullptr && response->bodyLen > 0) {
        // Check if it's likely text content
        bool isText = (strstr(response->contentType, "text/") != nullptr ||
                       strstr(response->contentType, "xml") != nullptr ||
                       strstr(response->contentType, "wml") != nullptr);
        
        if (isText) {
            // Print as text (up to reasonable limit)
            size_t printLen = response->bodyLen;
            if (printLen > 4096) printLen = 4096;
            printf("%.*s\n", (int)printLen, (const char*)response->body);
            if (response->bodyLen > 4096) {
                printf("... (%zu more bytes)\n", response->bodyLen - 4096);
            }
        } else {
            printf("(%zu bytes of binary data)\n", response->bodyLen);
        }
    }
}
