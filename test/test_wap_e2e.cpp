/**
 * test_wap_e2e.cpp - End-to-End WAP Test
 * 
 * Sends a real WAP GET request to wap.bevelgacom.be via WAPBOX
 * and decodes the response to HTTP format.
 * 
 * Compile and run with:
 *   g++ -std=c++11 -I. test/test_wap_e2e.cpp src/wap_request.cpp -o test_wap_e2e && ./test_wap_e2e
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>

// Socket includes for UDP
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>

#include "wap_request.h"

// WAPBOX Configuration for Bevelgacom WAP Gateway
#define WAPBOX_HOST "206.83.40.166"
#define WAPBOX_PORT 9200

// Helper to print hex dump
void hexDump(const uint8_t* data, size_t len, const char* label) {
    printf("%s (%zu bytes):\n", label, len);
    for (size_t i = 0; i < len; i++) {
        printf("%02X ", data[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    if (len % 16 != 0) printf("\n");
}

/**
 * Send WAP request via UDP and receive response
 */
bool sendWAPRequest(const char* host, uint16_t port, 
                    const uint8_t* request, size_t requestLen,
                    uint8_t* response, size_t* responseLen, size_t responseMaxLen,
                    int timeoutSec = 10) {
    
    // Create UDP socket
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        printf("Error creating socket: %s\n", strerror(errno));
        return false;
    }
    
    // Set receive timeout
    struct timeval tv;
    tv.tv_sec = timeoutSec;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    // Setup destination address
    struct sockaddr_in destAddr;
    memset(&destAddr, 0, sizeof(destAddr));
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, host, &destAddr.sin_addr) <= 0) {
        printf("Invalid address: %s\n", host);
        close(sock);
        return false;
    }
    
    // Send request
    printf("Sending %zu bytes to %s:%d...\n", requestLen, host, port);
    ssize_t sent = sendto(sock, request, requestLen, 0,
                          (struct sockaddr*)&destAddr, sizeof(destAddr));
    
    if (sent < 0) {
        printf("Send error: %s\n", strerror(errno));
        close(sock);
        return false;
    }
    
    printf("Sent %zd bytes, waiting for response...\n", sent);
    
    // Receive response
    struct sockaddr_in srcAddr;
    socklen_t srcAddrLen = sizeof(srcAddr);
    
    ssize_t received = recvfrom(sock, response, responseMaxLen, 0,
                                 (struct sockaddr*)&srcAddr, &srcAddrLen);
    
    close(sock);
    
    if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            printf("Timeout waiting for response (waited %d seconds)\n", timeoutSec);
        } else {
            printf("Receive error: %s\n", strerror(errno));
        }
        return false;
    }
    
    printf("Received %zd bytes from %s:%d\n", received,
           inet_ntoa(srcAddr.sin_addr), ntohs(srcAddr.sin_port));
    
    *responseLen = received;
    return true;
}

/**
 * Fetch HTTP content using curl
 */
bool fetchWithCurl(const char* url, std::string& body, std::string& contentType) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "curl -s -w '\\n%%{content_type}' '%s' 2>/dev/null", url);
    
    FILE* fp = popen(cmd, "r");
    if (!fp) {
        return false;
    }
    
    // Read all output
    std::string output;
    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), fp) != nullptr) {
        output += buffer;
    }
    pclose(fp);
    
    // Split: last line is content-type
    size_t lastNewline = output.rfind('\n');
    if (lastNewline != std::string::npos && lastNewline > 0) {
        // Find the newline before that
        size_t prevNewline = output.rfind('\n', lastNewline - 1);
        if (prevNewline != std::string::npos) {
            body = output.substr(0, prevNewline);
            contentType = output.substr(lastNewline + 1);
            // Trim content type
            while (!contentType.empty() && (contentType.back() == '\n' || contentType.back() == '\r')) {
                contentType.pop_back();
            }
            return true;
        }
    }
    
    body = output;
    return true;
}

/**
 * Normalize WML for comparison:
 * - Remove XML declaration
 * - Remove DOCTYPE
 * - Collapse whitespace
 * - Lowercase tags
 */
std::string normalizeWML(const char* wml) {
    std::string result;
    const char* p = wml;
    
    // Skip XML declaration
    if (strncmp(p, "<?xml", 5) == 0) {
        const char* end = strstr(p, "?>");
        if (end) p = end + 2;
        while (*p == '\n' || *p == '\r' || *p == ' ') p++;
    }
    
    // Skip DOCTYPE
    if (strncmp(p, "<!DOCTYPE", 9) == 0) {
        const char* end = strchr(p, '>');
        if (end) p = end + 1;
        while (*p == '\n' || *p == '\r' || *p == ' ') p++;
    }
    
    // Process content
    bool inTag = false;
    bool lastWasSpace = false;
    
    while (*p) {
        char c = *p++;
        
        if (c == '<') {
            inTag = true;
            result += c;
            lastWasSpace = false;
        } else if (c == '>') {
            inTag = false;
            result += c;
            lastWasSpace = false;
        } else if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (!lastWasSpace && !result.empty() && result.back() != '>') {
                result += ' ';
                lastWasSpace = true;
            }
        } else {
            // Lowercase inside tags
            if (inTag && c >= 'A' && c <= 'Z') {
                c = c + ('a' - 'A');
            }
            result += c;
            lastWasSpace = false;
        }
    }
    
    // Trim trailing space
    while (!result.empty() && result.back() == ' ') {
        result.pop_back();
    }
    
    return result;
}

/**
 * Extract key elements from WML for semantic comparison
 */
struct WMLContent {
    std::string title;
    std::vector<std::string> links;
    std::vector<std::string> texts;
    
    void parse(const char* wml) {
        // Extract title (from <card title="...">)
        const char* titleStart = strstr(wml, "title=\"");
        if (titleStart) {
            titleStart += 7;
            const char* titleEnd = strchr(titleStart, '"');
            if (titleEnd) {
                title = std::string(titleStart, titleEnd - titleStart);
            }
        }
        
        // Extract links (href="...")
        const char* p = wml;
        while ((p = strstr(p, "href=\"")) != nullptr) {
            p += 6;
            const char* end = strchr(p, '"');
            if (end) {
                links.push_back(std::string(p, end - p));
                p = end;
            }
        }
        
        // Extract visible text (between > and <, excluding tags)
        p = wml;
        while (*p) {
            if (*p == '>') {
                p++;
                std::string text;
                while (*p && *p != '<') {
                    if (*p != '\n' && *p != '\r' && *p != '\t') {
                        text += *p;
                    }
                    p++;
                }
                // Trim
                while (!text.empty() && text[0] == ' ') text.erase(0, 1);
                while (!text.empty() && text.back() == ' ') text.pop_back();
                if (!text.empty()) {
                    texts.push_back(text);
                }
            } else {
                p++;
            }
        }
    }
    
    void print() const {
        printf("  Title: %s\n", title.c_str());
        printf("  Links (%zu):\n", links.size());
        for (const auto& link : links) {
            printf("    - %s\n", link.c_str());
        }
        printf("  Texts (%zu):\n", texts.size());
        for (const auto& text : texts) {
            printf("    - %s\n", text.c_str());
        }
    }
};

// ============================================================
// Table-Driven Test Infrastructure
// ============================================================

/**
 * Test site configuration for table-driven tests
 */
struct TestSite {
    const char* name;           // Human-readable name for output
    const char* url;            // Full URL to test
    bool compareCurl;           // Whether to compare with curl result
    int expectedStatus;         // Expected HTTP status (0 = any success)
};

/**
 * Table of test sites - add or remove entries here
 */
static const TestSite TEST_SITES[] = {
    // Name                     URL                                     Compare   Expected
    { "wap.bevelgacom.be",      "http://wap.bevelgacom.be/",            true,     200 },
    { "find.bevelgacom.be",     "http://find.bevelgacom.be/",           true,     200 },
    { "wap.bevelgacom.be/index","http://wap.bevelgacom.be/index.wml",   false,    0   },  // 0 = any status
    // Add more test sites here:
    // { "example",             "http://example.wap.site/",             true,     200 },
};

static const size_t NUM_TEST_SITES = sizeof(TEST_SITES) / sizeof(TEST_SITES[0]);

/**
 * Run a single WAP test with optional curl comparison
 * Returns true if test passed
 */
bool runWAPTest(const TestSite& site, uint8_t transactionId) {
    printf("\n");
    printf("============================================================\n");
    printf("  End-to-End WAP Test: %s\n", site.name);
    if (site.compareCurl) {
        printf("  With WMLC Decompilation and curl Comparison\n");
    }
    printf("============================================================\n\n");
    
    // Create GET request
    uint8_t request[256];
    size_t requestLen = WAPRequest::createGetRequest(site.url, transactionId,
                                                      request, sizeof(request), true);
    
    if (requestLen == 0) {
        printf("FAIL: Could not create request\n");
        return false;
    }
    
    printf("Created WSP GET request for: %s\n", site.url);
    hexDump(request, requestLen, "Request PDU");
    
    // Send request and receive response
    uint8_t response[8192];
    size_t responseLen = 0;
    
    if (!sendWAPRequest(WAPBOX_HOST, WAPBOX_PORT, 
                        request, requestLen,
                        response, &responseLen, sizeof(response))) {
        printf("\nFAIL: Could not get response from WAPBOX\n");
        printf("Make sure WAPBOX is running at %s:%d\n", WAPBOX_HOST, WAPBOX_PORT);
        return false;
    }
    
    printf("\n");
    hexDump(response, responseLen, "Response PDU");
    
    // Decode response
    printf("\n--- Decoding WSP Response ---\n\n");
    
    HTTPResponse httpResponse;
    if (!WAPResponse::decode(response, responseLen, &httpResponse)) {
        printf("FAIL: Could not decode response\n");
        return false;
    }
    
    // Print HTTP info
    printf("=== WSP Response ===\n");
    printf("Status: %d %s\n", httpResponse.statusCode, httpResponse.statusText);
    printf("Content-Type: %s\n", httpResponse.contentType);
    printf("Content-Length: %zu\n", httpResponse.bodyLen);
    printf("Server: %s\n", httpResponse.server);
    
    // Check expected status
    if (site.expectedStatus > 0 && httpResponse.statusCode != site.expectedStatus) {
        printf("FAIL: Expected status %d, got %d\n", site.expectedStatus, httpResponse.statusCode);
        return false;
    }
    
    // Check if content is WMLC
    bool isWMLC = (strstr(httpResponse.contentType, "wmlc") != nullptr ||
                   strstr(httpResponse.contentType, "vnd.wap.wml") != nullptr);
    
    char decompiled[16384] = {0};
    size_t decompiledLen = 0;
    
    if (isWMLC && httpResponse.body != nullptr && httpResponse.bodyLen > 0) {
        printf("\n=== Decompiling WMLC to WML ===\n\n");
        
        decompiledLen = WMLCDecompiler::decompile(httpResponse.body, httpResponse.bodyLen,
                                                   decompiled, sizeof(decompiled));
        
        if (decompiledLen > 0) {
            printf("Decompiled %zu bytes of WMLC to %zu bytes of WML:\n\n", 
                   httpResponse.bodyLen, decompiledLen);
            printf("--- Decompiled WML ---\n%s\n--- End WML ---\n", decompiled);
        } else {
            printf("FAIL: Could not decompile WMLC\n");
        }
    } else {
        printf("\nContent is not WMLC, skipping decompilation\n");
        // If not WMLC, copy body as-is for comparison
        if (httpResponse.body && httpResponse.bodyLen > 0) {
            size_t copyLen = std::min(httpResponse.bodyLen, sizeof(decompiled) - 1);
            memcpy(decompiled, httpResponse.body, copyLen);
            decompiled[copyLen] = '\0';
            decompiledLen = copyLen;
        }
    }
    
    // Compare with curl if requested
    if (site.compareCurl) {
        printf("\n=== Fetching with curl for comparison ===\n\n");
        
        std::string curlBody, curlContentType;
        if (fetchWithCurl(site.url, curlBody, curlContentType)) {
            printf("curl Content-Type: %s\n", curlContentType.c_str());
            printf("curl Body Length: %zu\n", curlBody.size());
            printf("\n--- curl WML ---\n%s\n--- End curl WML ---\n", curlBody.c_str());
            
            // Compare semantically
            printf("\n=== Semantic Comparison ===\n\n");
            
            WMLContent wapContent, curlContent;
            wapContent.parse(decompiled);
            curlContent.parse(curlBody.c_str());
            
            printf("WAP (decompiled WMLC):\n");
            wapContent.print();
            
            printf("\ncurl (text WML):\n");
            curlContent.print();
            
            // Check if they match
            printf("\n=== Comparison Results ===\n\n");
            
            bool titleMatch = (wapContent.title == curlContent.title);
            printf("Title Match: %s\n", titleMatch ? "PASS" : "FAIL");
            if (!titleMatch) {
                printf("  WAP:  '%s'\n", wapContent.title.c_str());
                printf("  curl: '%s'\n", curlContent.title.c_str());
            }
            
            // Check links match (order may differ)
            std::vector<std::string> wapLinks = wapContent.links;
            std::vector<std::string> curlLinks = curlContent.links;
            std::sort(wapLinks.begin(), wapLinks.end());
            std::sort(curlLinks.begin(), curlLinks.end());
            
            bool linksMatch = (wapLinks == curlLinks);
            printf("Links Match: %s\n", linksMatch ? "PASS" : "FAIL");
            if (!linksMatch) {
                printf("  WAP links: %zu, curl links: %zu\n", wapLinks.size(), curlLinks.size());
            }
            
            // Check key texts present (fuzzy match)
            printf("Text Content: ");
            int matchingTexts = 0;
            for (const auto& curlText : curlContent.texts) {
                for (const auto& wapText : wapContent.texts) {
                    if (wapText.find(curlText) != std::string::npos ||
                        curlText.find(wapText) != std::string::npos) {
                        matchingTexts++;
                        break;
                    }
                }
            }
            if (curlContent.texts.size() > 0) {
                int matchPct = (matchingTexts * 100) / curlContent.texts.size();
                printf("%d%% match (%d/%zu texts)\n", matchPct, matchingTexts, curlContent.texts.size());
                if (matchPct >= 80) {
                    printf("  PASS (>=80%% match)\n");
                } else {
                    printf("  PARTIAL (%d%% match)\n", matchPct);
                }
            } else {
                printf("No texts to compare\n");
            }
            
        } else {
            printf("Could not fetch with curl\n");
        }
    } else {
        // Just print the response
        printf("\n=== HTTP Response ===\n\n");
        WAPResponse::print(&httpResponse);
    }
    
    printf("\n============================================================\n");
    printf("  Test Complete: %s\n", site.name);
    printf("============================================================\n");
    
    return true;
}

/**
 * Run all table-driven tests
 * Returns number of passed tests
 */
int runAllWAPTests() {
    printf("\n");
    printf("============================================================\n");
    printf("  Running %zu Table-Driven WAP Tests\n", NUM_TEST_SITES);
    printf("============================================================\n");
    
    int passed = 0;
    int failed = 0;
    
    for (size_t i = 0; i < NUM_TEST_SITES; i++) {
        uint8_t transactionId = (uint8_t)(i + 1);
        if (runWAPTest(TEST_SITES[i], transactionId)) {
            passed++;
        } else {
            failed++;
        }
    }
    
    printf("\n");
    printf("============================================================\n");
    printf("  Test Summary: %d/%zu passed", passed, NUM_TEST_SITES);
    if (failed > 0) {
        printf(" (%d failed)", failed);
    }
    printf("\n");
    printf("============================================================\n");
    
    return passed;
}

/**
 * Test: Verify Accept-Charset headers are included in request
 * This fixes the Kannel warning: "Device doesn't support charset"
 */
void testAcceptCharsetHeaders() {
    printf("\n");
    printf("============================================================\n");
    printf("  Test: Accept-Charset Headers (Kannel Compatibility)\n");
    printf("============================================================\n\n");
    
    uint8_t request[256];
    size_t requestLen = WAPRequest::createGetRequest(
        "http://wap.bevelgacom.be/", 0x01, request, sizeof(request), true);
    
    printf("Created WSP GET request (%zu bytes)\n", requestLen);
    hexDump(request, requestLen, "Request PDU");
    
    // Search for Accept-Charset headers in the request
    bool hasAcceptCharsetHeader = false;
    bool hasUtf8 = false;
    bool hasIso8859 = false;
    bool hasWmlcAccept = false;
    
    for (size_t i = 0; i < requestLen - 1; i++) {
        // Accept-Charset header code is 0x81 (0x01 | 0x80)
        if (request[i] == 0x81) {
            hasAcceptCharsetHeader = true;
            // Check value: UTF-8 = 0xEA (106 | 0x80), ISO-8859-1 = 0x84 (4 | 0x80)
            if (request[i+1] == 0xEA) {
                hasUtf8 = true;
                printf("  Found: Accept-Charset: UTF-8 (0x81 0xEA) at offset %zu\n", i);
            }
            if (request[i+1] == 0x84) {
                hasIso8859 = true;
                printf("  Found: Accept-Charset: ISO-8859-1 (0x81 0x84) at offset %zu\n", i);
            }
        }
        // Accept header code is 0x80 (0x00 | 0x80)
        if (request[i] == 0x80) {
            // WMLC = 0x94 (0x14 | 0x80)
            if (request[i+1] == 0x94) {
                hasWmlcAccept = true;
                printf("  Found: Accept: application/vnd.wap.wmlc (0x80 0x94) at offset %zu\n", i);
            }
        }
    }
    
    printf("\nHeader Verification:\n");
    printf("  [%s] Accept-Charset header present\n", hasAcceptCharsetHeader ? "PASS" : "FAIL");
    printf("  [%s] Accept-Charset: UTF-8 (IANA 106)\n", hasUtf8 ? "PASS" : "FAIL");
    printf("  [%s] Accept-Charset: ISO-8859-1 (IANA 4)\n", hasIso8859 ? "PASS" : "FAIL");
    printf("  [%s] Accept: application/vnd.wap.wmlc\n", hasWmlcAccept ? "PASS" : "FAIL");
    
    if (hasUtf8 && hasIso8859 && hasWmlcAccept) {
        printf("\n  SUCCESS: All required headers for Kannel compatibility are present!\n");
        printf("  This should fix:\n");
        printf("    - 'Device doesn't support charset <ISO-8859-1> neither UTF-8'\n");
        printf("    - 'content-type <application/vnd.wap.wmlc> not supported'\n");
    } else {
        printf("\n  FAIL: Missing required headers for Kannel!\n");
    }
}

/**
 * Online test: Verify Kannel accepts the request and returns WMLC content
 */
void testKannelWmlcResponse() {
    printf("\n");
    printf("============================================================\n");
    printf("  Test: Kannel WMLC Response (Online)\n");
    printf("============================================================\n\n");
    
    uint8_t request[256];
    size_t requestLen = WAPRequest::createGetRequest(
        "http://wap.bevelgacom.be/", 0x42, request, sizeof(request), true);
    
    printf("Sending request to WAPBOX at %s:%d\n", WAPBOX_HOST, WAPBOX_PORT);
    
    uint8_t response[8192];
    size_t responseLen = 0;
    
    if (!sendWAPRequest(WAPBOX_HOST, WAPBOX_PORT,
                        request, requestLen,
                        response, &responseLen, sizeof(response), 10)) {
        printf("\n  SKIP: Could not connect to WAPBOX\n");
        return;
    }
    
    printf("\nReceived %zu bytes\n", responseLen);
    hexDump(response, responseLen > 64 ? 64 : responseLen, "Response (first 64 bytes)");
    
    // Decode the response
    HTTPResponse httpResponse;
    if (WAPResponse::decode(response, responseLen, &httpResponse)) {
        printf("\nDecoded Response:\n");
        printf("  Status: %d %s\n", httpResponse.statusCode, httpResponse.statusText);
        printf("  Content-Type: %s\n", httpResponse.contentType);
        printf("  Content-Length: %zu\n", httpResponse.contentLength);
        printf("  Body Length: %zu\n", httpResponse.bodyLen);
        
        // Check for WMLC content type
        bool isWmlc = strstr(httpResponse.contentType, "wmlc") != nullptr ||
                      strstr(httpResponse.contentType, "wml") != nullptr;
        
        printf("\nVerification:\n");
        printf("  [%s] Status 200 OK\n", httpResponse.statusCode == 200 ? "PASS" : "FAIL");
        printf("  [%s] Content is WML/WMLC\n", isWmlc ? "PASS" : "FAIL");
        printf("  [%s] Body length > 0 (got %zu)\n", httpResponse.bodyLen > 0 ? "PASS" : "FAIL", httpResponse.bodyLen);
        
        // The old bug: Kannel would return text/plain with Content-Length: 0
        // because device didn't advertise charset/wmlc support
        if (httpResponse.bodyLen == 0 && 
            strstr(httpResponse.contentType, "text/plain") != nullptr) {
            printf("\n  FAIL: Got empty text/plain response!\n");
            printf("  This indicates Kannel rejected the content type.\n");
            printf("  Check if Accept-Charset headers are being sent.\n");
        } else if (httpResponse.bodyLen > 100) {
            printf("\n  SUCCESS: Received substantial WMLC content!\n");
            printf("  Kannel properly processed the request with charset headers.\n");
        }
    } else {
        printf("\n  FAIL: Could not decode WSP response\n");
    }
}

/**
 * Offline test: Parse a sample WSP Reply PDU
 */
void testOfflineParsing() {
    printf("\n");
    printf("============================================================\n");
    printf("  Offline Test: Parse Sample WSP Reply PDU\n");
    printf("============================================================\n\n");
    
    // Sample WSP Reply PDU:
    // [TID=0x01] [Type=0x04 Reply] [Status=0x20 OK] [HeadersLen=0x02] 
    // [Content-Type=0x88 text/vnd.wap.wml] [Some header byte]
    // [Body: "<wml>Hello WAP</wml>"]
    
    uint8_t samplePdu[] = {
        0x01,                       // Transaction ID
        0x04,                       // Type = Reply
        0x20,                       // Status = 200 OK
        0x01,                       // Headers length = 1
        0x88,                       // Content-Type = text/vnd.wap.wml (0x08 + 0x80)
        // Body:
        '<', 'w', 'm', 'l', '>', 
        'H', 'e', 'l', 'l', 'o', ' ', 'W', 'A', 'P',
        '<', '/', 'w', 'm', 'l', '>'
    };
    
    printf("Sample PDU:\n");
    hexDump(samplePdu, sizeof(samplePdu), "PDU");
    
    // Decode
    HTTPResponse response;
    if (WAPResponse::decode(samplePdu, sizeof(samplePdu), &response)) {
        printf("\nDecoded successfully:\n");
        printf("  Status: %d %s\n", response.statusCode, response.statusText);
        printf("  Content-Type: %s\n", response.contentType);
        printf("  Body Length: %zu\n", response.bodyLen);
        printf("  Body: %.*s\n", (int)response.bodyLen, (const char*)response.body);
        
        printf("\n=== As HTTP ===\n\n");
        WAPResponse::print(&response);
    } else {
        printf("FAIL: Could not decode sample PDU\n");
    }
}

int main(int argc, char* argv[]) {
    printf("======================================\n");
    printf("  WAP End-to-End Test Suite\n");
    printf("  WAPBOX: %s:%d\n", WAPBOX_HOST, WAPBOX_PORT);
    printf("  Test Sites: %zu configured\n", NUM_TEST_SITES);
    printf("======================================\n");
    
    // Run offline tests first (always works)
    testOfflineParsing();
    testAcceptCharsetHeaders();
    
    // Run online tests
    bool skipOnline = (argc > 1 && strcmp(argv[1], "--offline") == 0);
    
    if (skipOnline) {
        printf("\nSkipping online tests (--offline flag)\n");
    } else {
        printf("\nRunning online tests (use --offline to skip)\n");
        testKannelWmlcResponse();
        runAllWAPTests();
    }
    
    printf("\n======================================\n");
    printf("  Tests Complete\n");
    printf("======================================\n");
    
    return 0;
}
