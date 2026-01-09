/**
 * test_wap_request.cpp - Tests for WAP Request Builder
 * 
 * This test can be run on a native platform (not ESP32) to verify
 * the WSP PDU creation logic.
 * 
 * Compile and run with:
 *   g++ -std=c++11 -I. test/test_wap_request.cpp src/wap_request.cpp -o test_wap_request && ./test_wap_request
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <cstdint>

#include "wap_request.h"

// Test result tracking
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(condition, message) do { \
    if (!(condition)) { \
        printf("  FAIL: %s\n", message); \
        tests_failed++; \
    } else { \
        printf("  PASS: %s\n", message); \
        tests_passed++; \
    } \
} while(0)

// Helper to print hex dump
void hexDump(const uint8_t* data, size_t len, const char* label) {
    printf("  %s (%zu bytes): ", label, len);
    for (size_t i = 0; i < len; i++) {
        printf("%02X ", data[i]);
    }
    printf("\n");
}

// Test uintvar encoding
void testUintvarEncoding() {
    printf("\n=== Test: Uintvar Encoding ===\n");
    
    uint8_t buffer[8];
    size_t len;
    
    // Test single-byte values (0-127)
    len = WAPRequest::encodeUintvar(0, buffer, sizeof(buffer));
    TEST_ASSERT(len == 1 && buffer[0] == 0x00, "Uintvar 0 = 0x00");
    
    len = WAPRequest::encodeUintvar(127, buffer, sizeof(buffer));
    TEST_ASSERT(len == 1 && buffer[0] == 0x7F, "Uintvar 127 = 0x7F");
    
    // Test two-byte values (128-16383)
    len = WAPRequest::encodeUintvar(128, buffer, sizeof(buffer));
    TEST_ASSERT(len == 2 && buffer[0] == 0x81 && buffer[1] == 0x00, 
                "Uintvar 128 = 0x81 0x00");
    
    len = WAPRequest::encodeUintvar(200, buffer, sizeof(buffer));
    hexDump(buffer, len, "Uintvar 200");
    TEST_ASSERT(len == 2, "Uintvar 200 is 2 bytes");
    
    // Test larger values
    len = WAPRequest::encodeUintvar(0x3FFF, buffer, sizeof(buffer));
    TEST_ASSERT(len == 2, "Uintvar 0x3FFF (16383) is 2 bytes");
    
    len = WAPRequest::encodeUintvar(0x4000, buffer, sizeof(buffer));
    TEST_ASSERT(len == 3, "Uintvar 0x4000 (16384) is 3 bytes");
}

// Test uintvar decoding
void testUintvarDecoding() {
    printf("\n=== Test: Uintvar Decoding ===\n");
    
    unsigned long value;
    size_t len;
    
    // Single byte
    uint8_t data1[] = {0x00};
    len = WAPRequest::decodeUintvar(data1, sizeof(data1), &value);
    TEST_ASSERT(len == 1 && value == 0, "Decode uintvar 0");
    
    uint8_t data2[] = {0x7F};
    len = WAPRequest::decodeUintvar(data2, sizeof(data2), &value);
    TEST_ASSERT(len == 1 && value == 127, "Decode uintvar 127");
    
    // Two bytes
    uint8_t data3[] = {0x81, 0x00};
    len = WAPRequest::decodeUintvar(data3, sizeof(data3), &value);
    TEST_ASSERT(len == 2 && value == 128, "Decode uintvar 128");
    
    // Three bytes
    uint8_t data4[] = {0x81, 0x80, 0x00};
    len = WAPRequest::decodeUintvar(data4, sizeof(data4), &value);
    TEST_ASSERT(len == 3 && value == 16384, "Decode uintvar 16384");
}

// Test host extraction from URL
void testHostExtraction() {
    printf("\n=== Test: Host Extraction ===\n");
    
    char host[64];
    bool result;
    
    result = WAPRequest::extractHostFromUrl("http://wap.bevelgacom.be/", host, sizeof(host));
    TEST_ASSERT(result && strcmp(host, "wap.bevelgacom.be") == 0, 
                "Extract host from http://wap.bevelgacom.be/");
    
    result = WAPRequest::extractHostFromUrl("https://example.com:8080/path", host, sizeof(host));
    TEST_ASSERT(result && strcmp(host, "example.com") == 0, 
                "Extract host from https://example.com:8080/path");
    
    result = WAPRequest::extractHostFromUrl("http://localhost/test", host, sizeof(host));
    TEST_ASSERT(result && strcmp(host, "localhost") == 0, 
                "Extract host from http://localhost/test");
    
    result = WAPRequest::extractHostFromUrl("wap.test.com/page", host, sizeof(host));
    TEST_ASSERT(result && strcmp(host, "wap.test.com") == 0, 
                "Extract host from URL without protocol");
}

// Test GET request creation
void testGetRequestCreation() {
    printf("\n=== Test: GET Request Creation ===\n");
    
    uint8_t buffer[256];
    size_t len;
    
    // Create a simple GET request to wap.bevelgacom.be
    const char* testUri = "http://wap.bevelgacom.be/";
    uint8_t tid = 0x42;
    
    len = WAPRequest::createGetRequest(testUri, tid, buffer, sizeof(buffer), true);
    
    TEST_ASSERT(len > 0, "GET request created successfully");
    
    if (len > 0) {
        hexDump(buffer, len, "GET PDU");
        
        // Verify structure:
        // [0] = Transaction ID
        TEST_ASSERT(buffer[0] == tid, "Transaction ID is correct");
        
        // [1] = Type (0x4) | Subtype (0x0) = 0x40 for GET
        TEST_ASSERT(buffer[1] == 0x40, "PDU type is GET (0x40)");
        
        // [2+] = URI length as uintvar
        unsigned long uriLen;
        size_t uintvarLen = WAPRequest::decodeUintvar(&buffer[2], len - 2, &uriLen);
        TEST_ASSERT(uintvarLen > 0, "URI length uintvar decoded");
        TEST_ASSERT(uriLen == strlen(testUri), "URI length matches");
        
        // Verify URI is present
        size_t uriOffset = 2 + uintvarLen;
        TEST_ASSERT(memcmp(&buffer[uriOffset], testUri, uriLen) == 0, "URI content matches");
        
        printf("  URI offset: %zu, URI length: %lu\n", uriOffset, uriLen);
    }
}

// Test GET request without headers
void testGetRequestNoHeaders() {
    printf("\n=== Test: GET Request Without Headers ===\n");
    
    uint8_t buffer[256];
    size_t len;
    
    const char* testUri = "http://test.com/page";
    
    len = WAPRequest::createGetRequestWithHeaders(testUri, 0x01, nullptr, 0, 
                                                   buffer, sizeof(buffer));
    
    TEST_ASSERT(len > 0, "GET request without headers created");
    
    if (len > 0) {
        hexDump(buffer, len, "GET PDU (no headers)");
        
        // Minimum size: 1 (TID) + 1 (type) + 1 (uri_len) + strlen(uri)
        size_t minExpected = 1 + 1 + 1 + strlen(testUri);
        TEST_ASSERT(len == minExpected, "PDU size matches minimum expected");
    }
}

// Test header creation
void testHeaderCreation() {
    printf("\n=== Test: Header Creation ===\n");
    
    uint8_t buffer[128];
    size_t len;
    
    // Test Host header
    len = WAPRequest::createHostHeader("wap.bevelgacom.be", buffer, sizeof(buffer));
    TEST_ASSERT(len > 0, "Host header created");
    if (len > 0) {
        hexDump(buffer, len, "Host header");
        // Should be: 0x96 (Host|0x80) + "wap.bevelgacom.be" + 0x00
        TEST_ASSERT(buffer[0] == (WSP_HEADER_HOST | 0x80), "Host header code correct");
    }
    
    // Test User-Agent header
    len = WAPRequest::createUserAgentHeader("TestAgent/1.0", buffer, sizeof(buffer));
    TEST_ASSERT(len > 0, "User-Agent header created");
    if (len > 0) {
        hexDump(buffer, len, "User-Agent header");
        TEST_ASSERT(buffer[0] == (WSP_HEADER_USER_AGENT | 0x80), "UA header code correct");
    }
    
    // Test Accept-Charset header for UTF-8 (IANA code 106 = 0x6A)
    len = WAPRequest::createAcceptCharsetHeader(106, buffer, sizeof(buffer));
    TEST_ASSERT(len == 2, "Accept-Charset UTF-8 header is 2 bytes");
    if (len >= 2) {
        hexDump(buffer, len, "Accept-Charset UTF-8");
        TEST_ASSERT(buffer[0] == (WSP_HEADER_ACCEPT_CHARSET | 0x80), "Accept-Charset header code correct (0x81)");
        TEST_ASSERT(buffer[1] == (106 | 0x80), "UTF-8 charset code correct (0xEA)");
    }
    
    // Test Accept-Charset header for ISO-8859-1 (IANA code 4)
    len = WAPRequest::createAcceptCharsetHeader(4, buffer, sizeof(buffer));
    TEST_ASSERT(len == 2, "Accept-Charset ISO-8859-1 header is 2 bytes");
    if (len >= 2) {
        hexDump(buffer, len, "Accept-Charset ISO-8859-1");
        TEST_ASSERT(buffer[0] == (WSP_HEADER_ACCEPT_CHARSET | 0x80), "Accept-Charset header code correct");
        TEST_ASSERT(buffer[1] == (4 | 0x80), "ISO-8859-1 charset code correct (0x84)");
    }
}

// Test that createAcceptAllHeaders includes charset headers (for Kannel compatibility)
void testAcceptAllHeaders() {
    printf("\n=== Test: Accept-All Headers (Kannel Compatibility) ===\n");
    
    uint8_t buffer[64];
    size_t len;
    
    len = WAPRequest::createAcceptAllHeaders(buffer, sizeof(buffer));
    TEST_ASSERT(len > 0, "Accept-All headers created");
    
    if (len > 0) {
        hexDump(buffer, len, "Accept-All headers");
        
        // Check that Accept-Charset headers are present
        bool hasAcceptCharset = false;
        bool hasUtf8 = false;
        bool hasIso8859 = false;
        bool hasWmlc = false;
        
        for (size_t i = 0; i < len - 1; i++) {
            if (buffer[i] == (WSP_HEADER_ACCEPT_CHARSET | 0x80)) {
                hasAcceptCharset = true;
                if (buffer[i+1] == (106 | 0x80)) hasUtf8 = true;  // UTF-8
                if (buffer[i+1] == (4 | 0x80)) hasIso8859 = true; // ISO-8859-1
            }
            if (buffer[i] == (WSP_HEADER_ACCEPT | 0x80)) {
                if (buffer[i+1] == (WSP_CT_APP_VND_WAP_WMLC | 0x80)) hasWmlc = true;
            }
        }
        
        TEST_ASSERT(hasAcceptCharset, "Contains Accept-Charset header");
        TEST_ASSERT(hasUtf8, "Contains Accept-Charset: UTF-8 (fixes Kannel warning)");
        TEST_ASSERT(hasIso8859, "Contains Accept-Charset: ISO-8859-1");
        TEST_ASSERT(hasWmlc, "Contains Accept: application/vnd.wap.wmlc (fixes 'content-type not supported')");
        
        printf("  Headers include charset and WMLC support for Kannel\n");
    }
}

// Test Reply PDU parsing
void testReplyParsing() {
    printf("\n=== Test: Reply PDU Parsing ===\n");
    
    // Construct a sample Reply PDU (without TID - that's stripped before parsing)
    // Reply: TYPE(0x04) STATUS(0x20=OK) HEADERS_LEN(0x01) HEADERS(0x94=text/html) DATA("Hello")
    uint8_t replyPdu[] = {
        0x04,                   // Type = Reply
        0x20,                   // Status = 200 OK (WSP encoding)
        0x01,                   // Headers length = 1
        0x84,                   // Content-Type = text/html (short form)
        'H', 'e', 'l', 'l', 'o' // Body
    };
    
    int status;
    const uint8_t* body;
    size_t bodyLen;
    
    bool result = WAPRequest::parseReplyPDU(replyPdu, sizeof(replyPdu), 
                                            &status, &body, &bodyLen);
    
    TEST_ASSERT(result, "Reply PDU parsed successfully");
    TEST_ASSERT(status == 200, "Status code is 200");
    TEST_ASSERT(bodyLen == 5, "Body length is 5");
    TEST_ASSERT(body != nullptr && memcmp(body, "Hello", 5) == 0, "Body content matches");
    
    // Test with empty body
    uint8_t replyNoBody[] = {
        0x04,   // Type = Reply
        0x44,   // Status = 404 Not Found
        0x00    // Headers length = 0
    };
    
    result = WAPRequest::parseReplyPDU(replyNoBody, sizeof(replyNoBody),
                                       &status, &body, &bodyLen);
    TEST_ASSERT(result, "Reply PDU with no body parsed");
    TEST_ASSERT(status == 404, "Status code is 404");
    TEST_ASSERT(bodyLen == 0, "Body length is 0");
}

// Test WSP status code conversion
void testStatusConversion() {
    printf("\n=== Test: WSP Status Conversion ===\n");
    
    TEST_ASSERT(WAPRequest::wspStatusToHttp(0x20) == 200, "WSP 0x20 = HTTP 200");
    TEST_ASSERT(WAPRequest::wspStatusToHttp(0x31) == 301, "WSP 0x31 = HTTP 301");
    TEST_ASSERT(WAPRequest::wspStatusToHttp(0x32) == 302, "WSP 0x32 = HTTP 302");
    TEST_ASSERT(WAPRequest::wspStatusToHttp(0x44) == 404, "WSP 0x44 = HTTP 404");
    TEST_ASSERT(WAPRequest::wspStatusToHttp(0x60) == 500, "WSP 0x60 = HTTP 500");
}

// Integration test: Full GET request to wap.bevelgacom.be
void testFullGetRequest() {
    printf("\n=== Test: Full GET Request to wap.bevelgacom.be ===\n");
    
    uint8_t buffer[256];
    size_t len;
    
    // Create GET request for wap.bevelgacom.be via WAPBOX_HOST
    // This would be sent to WAPBOX_HOST:WAPBOX_PORT via UDP
    const char* uri = "http://wap.bevelgacom.be/";
    uint8_t transactionId = 0x01;
    
    len = WAPRequest::createGetRequest(uri, transactionId, buffer, sizeof(buffer), true);
    
    TEST_ASSERT(len > 0, "Full GET request created for wap.bevelgacom.be");
    
    if (len > 0) {
        printf("\n  Ready to send to WAPBOX at 206.83.40.166:9200\n");
        hexDump(buffer, len, "Complete GET PDU");
        
        // Validate overall structure
        printf("\n  PDU Structure Analysis:\n");
        printf("    Transaction ID: 0x%02X\n", buffer[0]);
        printf("    PDU Type: 0x%02X (", buffer[1]);
        if ((buffer[1] & 0xF0) == 0x40) {
            printf("GET");
            if ((buffer[1] & 0x0F) == 0) printf(" method");
            else if ((buffer[1] & 0x0F) == 2) printf("/HEAD method");
        }
        printf(")\n");
        
        unsigned long uriLen;
        size_t uintvarLen = WAPRequest::decodeUintvar(&buffer[2], len - 2, &uriLen);
        printf("    URI Length: %lu (encoded in %zu bytes)\n", uriLen, uintvarLen);
        printf("    URI: %.*s\n", (int)uriLen, &buffer[2 + uintvarLen]);
        
        size_t headersStart = 2 + uintvarLen + uriLen;
        if (headersStart < len) {
            printf("    Headers: %zu bytes\n", len - headersStart);
        }
    }
}

// Test WAPResponse parsing
void testWAPResponseBasic() {
    printf("\n=== Test: WAPResponse Basic Parsing ===\n");
    
    // Sample WSP Reply PDU with text/html content
    uint8_t pdu[] = {
        0x01,                       // Transaction ID
        0x04,                       // Type = Reply
        0x20,                       // Status = 200 OK
        0x01,                       // Headers length = 1
        0x82,                       // Content-Type = text/html (0x02 + 0x80)
        '<', 'h', 't', 'm', 'l', '>', 'H', 'i', '<', '/', 'h', 't', 'm', 'l', '>'
    };
    
    HTTPResponse response;
    bool result = WAPResponse::decode(pdu, sizeof(pdu), &response);
    
    TEST_ASSERT(result, "WAPResponse decode succeeded");
    TEST_ASSERT(response.statusCode == 200, "Status code is 200");
    TEST_ASSERT(strcmp(response.statusText, "OK") == 0, "Status text is OK");
    TEST_ASSERT(strcmp(response.contentType, "text/html") == 0, "Content-Type is text/html");
    TEST_ASSERT(response.bodyLen == 15, "Body length is 15");
}

// Test WAPResponse with various status codes
void testWAPResponseStatus() {
    printf("\n=== Test: WAPResponse Status Codes ===\n");
    
    struct TestCase {
        uint8_t wspStatus;
        int expectedHttp;
        const char* expectedText;
    } testCases[] = {
        {0x20, 200, "OK"},
        {0x31, 301, "Moved Permanently"},
        {0x32, 302, "Found"},
        {0x44, 404, "Not Found"},
        {0x46, 406, "Not Acceptable"},
        {0x60, 500, "Internal Server Error"},
    };
    
    for (const auto& tc : testCases) {
        uint8_t pdu[] = {0x01, 0x04, tc.wspStatus, 0x00};
        HTTPResponse response;
        WAPResponse::decode(pdu, sizeof(pdu), &response);
        
        char msg[64];
        snprintf(msg, sizeof(msg), "WSP 0x%02X -> HTTP %d", tc.wspStatus, tc.expectedHttp);
        TEST_ASSERT(response.statusCode == tc.expectedHttp, msg);
    }
}

// Test content-type decoding
void testContentTypeDecoding() {
    printf("\n=== Test: Content-Type Decoding ===\n");
    
    TEST_ASSERT(strcmp(WAPResponse::contentTypeToString(0x02), "text/html") == 0, 
                "0x02 = text/html");
    TEST_ASSERT(strcmp(WAPResponse::contentTypeToString(0x03), "text/plain") == 0,
                "0x03 = text/plain");
    TEST_ASSERT(strcmp(WAPResponse::contentTypeToString(0x08), "text/vnd.wap.wml") == 0,
                "0x08 = text/vnd.wap.wml");
    TEST_ASSERT(strcmp(WAPResponse::contentTypeToString(0x1D), "image/gif") == 0,
                "0x1D = image/gif");
    TEST_ASSERT(strcmp(WAPResponse::contentTypeToString(0x1E), "image/jpeg") == 0,
                "0x1E = image/jpeg");
}

// Test HTTP formatting
void testHTTPFormatting() {
    printf("\n=== Test: HTTP Response Formatting ===\n");
    
    uint8_t pdu[] = {
        0x01, 0x04, 0x20, 0x01, 0x82, 
        'T', 'e', 's', 't'
    };
    
    HTTPResponse response;
    WAPResponse::decode(pdu, sizeof(pdu), &response);
    
    char httpBuffer[512];
    size_t httpLen = WAPResponse::formatAsHTTP(&response, httpBuffer, sizeof(httpBuffer));
    
    TEST_ASSERT(httpLen > 0, "HTTP formatting produced output");
    TEST_ASSERT(strstr(httpBuffer, "HTTP/1.1 200 OK") != nullptr, "Contains status line");
    TEST_ASSERT(strstr(httpBuffer, "Content-Type: text/html") != nullptr, "Contains Content-Type");
    TEST_ASSERT(strstr(httpBuffer, "Test") != nullptr, "Contains body");
}

int main() {
    printf("======================================\n");
    printf("  WAP Request Builder Test Suite\n");
    printf("======================================\n");
    
    testUintvarEncoding();
    testUintvarDecoding();
    testHostExtraction();
    testGetRequestCreation();
    testGetRequestNoHeaders();
    testHeaderCreation();
    testAcceptAllHeaders();
    testReplyParsing();
    testStatusConversion();
    testFullGetRequest();
    
    // New WAPResponse tests
    testWAPResponseBasic();
    testWAPResponseStatus();
    testContentTypeDecoding();
    testHTTPFormatting();
    
    printf("\n======================================\n");
    printf("  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("======================================\n");
    
    return tests_failed > 0 ? 1 : 0;
}
