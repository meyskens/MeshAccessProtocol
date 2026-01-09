/**
 * test_base91.cpp - Unit tests for Base91 encoding/decoding
 * 
 * Compile and run with:
 *   g++ -std=c++11 -I lib/base91 test/test_base91.cpp lib/base91/base91.cpp -o test_base91 && ./test_base91
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

#include "base91.h"

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

void hexDump(const uint8_t* data, size_t len, const char* label) {
    printf("  %s (%zu bytes): ", label, len);
    for (size_t i = 0; i < len && i < 32; i++) {
        printf("%02X ", data[i]);
    }
    if (len > 32) printf("...");
    printf("\n");
}

void testBasicEncodeDecode() {
    printf("\n=== Test: Basic Encode/Decode ===\n");
    
    // Test simple data
    uint8_t input[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05};
    char encoded[32];
    uint8_t decoded[32];
    
    size_t encodedLen = Base91::encode(input, sizeof(input), encoded, sizeof(encoded));
    TEST_ASSERT(encodedLen > 0, "Encoding succeeded");
    printf("  Input:   "); for (size_t i = 0; i < sizeof(input); i++) printf("%02X ", input[i]); printf("\n");
    printf("  Encoded: %s (len=%zu)\n", encoded, encodedLen);
    
    // Verify no null bytes in encoded output
    bool hasNull = false;
    for (size_t i = 0; i < encodedLen; i++) {
        if (encoded[i] == '\0') hasNull = true;
    }
    TEST_ASSERT(!hasNull, "Encoded string has no embedded null bytes");
    
    size_t decodedLen = Base91::decode(encoded, decoded, sizeof(decoded));
    TEST_ASSERT(decodedLen == sizeof(input), "Decoded length matches");
    TEST_ASSERT(memcmp(input, decoded, sizeof(input)) == 0, "Decoded data matches");
}

void testNullBytesInInput() {
    printf("\n=== Test: Null Bytes in Input ===\n");
    
    // This is the critical test - ensure null bytes don't cause issues
    uint8_t input[] = {0x96, 0x77, 0x61, 0x70, 0x00, 0xA9, 0x4D, 0x41, 0x50, 0x00, 0x80};
    char encoded[64];
    uint8_t decoded[64];
    
    size_t encodedLen = Base91::encode(input, sizeof(input), encoded, sizeof(encoded));
    TEST_ASSERT(encodedLen > 0, "Encoding with null bytes succeeded");
    printf("  Input with nulls: ");
    for (size_t i = 0; i < sizeof(input); i++) printf("%02X ", input[i]);
    printf("\n");
    printf("  Encoded: %s (len=%zu)\n", encoded, encodedLen);
    
    // strlen should equal encodedLen (no embedded nulls)
    TEST_ASSERT(strlen(encoded) == encodedLen, "strlen equals encodedLen (no truncation)");
    
    size_t decodedLen = Base91::decode(encoded, decoded, sizeof(decoded));
    TEST_ASSERT(decodedLen == sizeof(input), "Decoded length matches original");
    TEST_ASSERT(memcmp(input, decoded, sizeof(input)) == 0, "Decoded data matches original");
}

void testWDPMessage() {
    printf("\n=== Test: Real WDP Message ===\n");
    
    // Simulated WDP message with UDH and WSP data
    uint8_t wdpMsg[] = {
        // UDH (7 bytes)
        0x06, 0x05, 0x04, 0x23, 0xF0, 0x1E, 0xAC,
        // WSP GET request payload
        0x04, 0x40, 0x19, 0x68, 0x74, 0x74, 0x70, 0x3A,
        0x2F, 0x2F, 0x77, 0x61, 0x70, 0x2E, 0x62, 0x65,
        0x76, 0x65, 0x6C, 0x67, 0x61, 0x63, 0x6F, 0x6D,
        0x2E, 0x62, 0x65, 0x2F, 0x96, 0x77, 0x61, 0x70,
        0x2E, 0x62, 0x65, 0x76, 0x65, 0x6C, 0x67, 0x61,
        0x63, 0x6F, 0x6D, 0x2E, 0x62, 0x65, 0x00,  // Host header with null terminator!
        0xA9, 0x4D, 0x41, 0x50, 0x2F, 0x31, 0x2E, 0x30, 0x00,  // User-Agent with null!
        0x80, 0x80, 0x80, 0x94, 0x80, 0x88, 0x80, 0xA1, 0x81, 0xEA, 0x81, 0x84  // Accept headers
    };
    
    char encoded[256];
    uint8_t decoded[256];
    
    printf("  Original WDP message: %zu bytes\n", sizeof(wdpMsg));
    hexDump(wdpMsg, sizeof(wdpMsg), "WDP");
    
    size_t encodedLen = Base91::encode(wdpMsg, sizeof(wdpMsg), encoded, sizeof(encoded));
    TEST_ASSERT(encodedLen > 0, "WDP encoding succeeded");
    printf("  Base91 encoded: %zu chars\n", encodedLen);
    printf("  Encoded string: %.60s...\n", encoded);
    
    // Calculate efficiency
    float expansion = (float)encodedLen / sizeof(wdpMsg);
    printf("  Expansion ratio: %.2fx (vs 2.0x for hex)\n", expansion);
    TEST_ASSERT(expansion < 1.5, "Expansion ratio is better than 1.5x");
    
    size_t decodedLen = Base91::decode(encoded, decoded, sizeof(decoded));
    TEST_ASSERT(decodedLen == sizeof(wdpMsg), "Decoded length matches");
    TEST_ASSERT(memcmp(wdpMsg, decoded, sizeof(wdpMsg)) == 0, "Decoded WDP matches original");
}

void testEdgeCases() {
    printf("\n=== Test: Edge Cases ===\n");
    
    char encoded[256];
    uint8_t decoded[256];
    
    // Empty input
    size_t len = Base91::encode(nullptr, 0, encoded, sizeof(encoded));
    TEST_ASSERT(len == 0, "Null input returns 0");
    
    // Single byte
    uint8_t single[] = {0x42};
    len = Base91::encode(single, 1, encoded, sizeof(encoded));
    TEST_ASSERT(len > 0, "Single byte encodes");
    size_t dlen = Base91::decode(encoded, decoded, sizeof(decoded));
    TEST_ASSERT(dlen == 1 && decoded[0] == 0x42, "Single byte roundtrips");
    
    // All zeros
    uint8_t zeros[16] = {0};
    len = Base91::encode(zeros, sizeof(zeros), encoded, sizeof(encoded));
    TEST_ASSERT(len > 0, "All zeros encodes");
    dlen = Base91::decode(encoded, decoded, sizeof(decoded));
    TEST_ASSERT(dlen == sizeof(zeros), "All zeros decoded length correct");
    TEST_ASSERT(memcmp(zeros, decoded, sizeof(zeros)) == 0, "All zeros roundtrips");
    
    // All 0xFF
    uint8_t ffs[16];
    memset(ffs, 0xFF, sizeof(ffs));
    len = Base91::encode(ffs, sizeof(ffs), encoded, sizeof(encoded));
    TEST_ASSERT(len > 0, "All 0xFF encodes");
    dlen = Base91::decode(encoded, decoded, sizeof(decoded));
    TEST_ASSERT(dlen == sizeof(ffs) && memcmp(ffs, decoded, sizeof(ffs)) == 0, "All 0xFF roundtrips");
}

void testMeshCoreCompatibility() {
    printf("\n=== Test: MeshCore Compatibility ===\n");
    
    // Simulate what happens when MeshCore receives the Base91 string
    // MeshCore uses strlen() which should work fine since Base91 has no nulls
    
    uint8_t original[] = {0x00, 0x01, 0x02, 0x00, 0xFF, 0xFE, 0x00, 0x42};
    char encoded[64];
    
    size_t encodedLen = Base91::encode(original, sizeof(original), encoded, sizeof(encoded));
    
    // Simulate MeshCore's strlen()
    size_t strlenResult = strlen(encoded);
    TEST_ASSERT(strlenResult == encodedLen, "strlen matches encodedLen (MeshCore compatible)");
    
    // Simulate receiving and decoding
    uint8_t decoded[64];
    size_t decodedLen = Base91::decode(encoded, decoded, sizeof(decoded));
    TEST_ASSERT(decodedLen == sizeof(original), "Full message decoded");
    TEST_ASSERT(memcmp(original, decoded, sizeof(original)) == 0, "Message intact after MeshCore simulation");
    
    printf("  Original with nulls: ");
    for (size_t i = 0; i < sizeof(original); i++) printf("%02X ", original[i]);
    printf("\n");
    printf("  After Base91 roundtrip: ");
    for (size_t i = 0; i < decodedLen; i++) printf("%02X ", decoded[i]);
    printf("\n");
}

int main() {
    printf("======================================\n");
    printf("  Base91 Encoding Test Suite\n");
    printf("======================================\n");
    
    testBasicEncodeDecode();
    testNullBytesInInput();
    testWDPMessage();
    testEdgeCases();
    testMeshCoreCompatibility();
    
    printf("\n======================================\n");
    printf("  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("======================================\n");
    
    return tests_failed > 0 ? 1 : 0;
}
