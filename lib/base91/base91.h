/**
 * base91.h - Base91 Encoding/Decoding for MeshCore
 * 
 * Base91 encodes binary data using 91 printable ASCII characters,
 * achieving ~23% better efficiency than Base64 (~1.23 bytes per input byte
 * vs ~1.33 for Base64).
 * 
 * Importantly, the character set excludes:
 * - Null byte (0x00) - would truncate MeshCore strlen() messages
 * - Backslash, single/double quotes - problematic in many contexts
 * 
 */

#ifndef BASE91_H
#define BASE91_H

#include <cstdint>
#include <cstddef>

class Base91 {
public:
    /**
     * Encode binary data to Base91 string
     * 
     * @param input Binary data to encode
     * @param inputLen Length of input data
     * @param output Output buffer for Base91 string (will be null-terminated)
     * @param outputMaxLen Maximum size of output buffer
     * @return Length of encoded string (not including null terminator), or 0 on error
     */
    static size_t encode(const uint8_t* input, size_t inputLen, 
                         char* output, size_t outputMaxLen);
    
    /**
     * Decode Base91 string to binary data
     * 
     * @param input Base91 encoded string (null-terminated)
     * @param output Output buffer for decoded binary data
     * @param outputMaxLen Maximum size of output buffer
     * @return Length of decoded data, or 0 on error
     */
    static size_t decode(const char* input, uint8_t* output, size_t outputMaxLen);
    
    /**
     * Calculate maximum encoded size for given input length
     * Base91 worst case is ceil(inputLen * 16 / 13) + 1
     */
    static size_t encodedSize(size_t inputLen) {
        return ((inputLen * 16 + 12) / 13) + 1;  // +1 for safety
    }
    
    /**
     * Calculate maximum decoded size for given encoded length
     * Base91 decodes to at most ceil(encodedLen * 13 / 16)
     */
    static size_t decodedSize(size_t encodedLen) {
        return ((encodedLen * 13 + 15) / 16) + 1;  // +1 for safety
    }

private:
    // 91 printable ASCII characters (excludes NUL, ", ', \, and some others)
    static const char ALPHABET[91];
    static const int8_t DECODE_TABLE[256];
};

#endif // BASE91_H
