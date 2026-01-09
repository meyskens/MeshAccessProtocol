/**
 * base91.cpp - Base91 Encoding/Decoding Implementation
 * 
 */

#include "base91.h"

// 91 printable ASCII characters - excludes NUL (0x00), " (0x22), ' (0x27), \ (0x5C)
// This ensures no null bytes in encoded output (critical for strlen-based messaging)
const char Base91::ALPHABET[91] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
    'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
    'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '!', '#', '$',
    '%', '&', '(', ')', '*', '+', ',', '.', '/', ':', ';', '<', '=',
    '>', '?', '@', '[', ']', '^', '_', '`', '{', '|', '}', '~', '-'
};

// Decode lookup table (-1 = invalid character)
const int8_t Base91::DECODE_TABLE[256] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 0x00-0x0F
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 0x10-0x1F
    -1, 62, -1, 63, 64, 65, 66, -1, 67, 68, 69, 70, 71, 90, 72, 73,  // 0x20-0x2F  ! # $ % & ( ) * + , - . /
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 74, 75, 76, 77, 78, 79,  // 0x30-0x3F  0-9 : ; < = > ?
    80, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14,  // 0x40-0x4F  @ A-O
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 81, -1, 82, 83, 84,  // 0x50-0x5F  P-Z [ \ ] ^ _
    85, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,  // 0x60-0x6F  ` a-o
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 86, 87, 88, 89, -1,  // 0x70-0x7F  p-z { | } ~
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 0x80-0x8F
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 0x90-0x9F
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 0xA0-0xAF
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 0xB0-0xBF
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 0xC0-0xCF
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 0xD0-0xDF
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 0xE0-0xEF
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1   // 0xF0-0xFF
};

size_t Base91::encode(const uint8_t* input, size_t inputLen, 
                      char* output, size_t outputMaxLen) {
    if (input == nullptr || output == nullptr || outputMaxLen == 0) {
        return 0;
    }
    
    size_t outPos = 0;
    uint32_t queue = 0;
    int numBits = 0;
    
    for (size_t i = 0; i < inputLen; i++) {
        queue |= ((uint32_t)input[i]) << numBits;
        numBits += 8;
        
        if (numBits > 13) {
            // Extract 13 bits and encode as 2 characters
            uint32_t val = queue & 8191;  // 8191 = 2^13 - 1
            
            if (val > 88) {
                queue >>= 13;
                numBits -= 13;
            } else {
                // For small values, use 14 bits for better encoding
                val = queue & 16383;  // 16383 = 2^14 - 1
                queue >>= 14;
                numBits -= 14;
            }
            
            if (outPos + 2 > outputMaxLen - 1) {  // -1 for null terminator
                return 0;  // Buffer too small
            }
            
            output[outPos++] = ALPHABET[val % 91];
            output[outPos++] = ALPHABET[val / 91];
        }
    }
    
    // Handle remaining bits
    if (numBits > 0) {
        if (outPos + 1 > outputMaxLen - 1) {
            return 0;
        }
        output[outPos++] = ALPHABET[queue % 91];
        
        if (numBits > 7 || queue > 90) {
            if (outPos + 1 > outputMaxLen - 1) {
                return 0;
            }
            output[outPos++] = ALPHABET[queue / 91];
        }
    }
    
    output[outPos] = '\0';
    return outPos;
}

size_t Base91::decode(const char* input, uint8_t* output, size_t outputMaxLen) {
    if (input == nullptr || output == nullptr || outputMaxLen == 0) {
        return 0;
    }
    
    size_t outPos = 0;
    uint32_t queue = 0;
    int numBits = 0;
    int val = -1;
    
    for (size_t i = 0; input[i] != '\0'; i++) {
        int8_t d = DECODE_TABLE[(uint8_t)input[i]];
        if (d == -1) {
            continue;  // Skip invalid characters
        }
        
        if (val == -1) {
            val = d;
        } else {
            val += d * 91;
            queue |= ((uint32_t)val) << numBits;
            numBits += (val & 8191) > 88 ? 13 : 14;
            
            while (numBits >= 8) {
                if (outPos >= outputMaxLen) {
                    return 0;  // Buffer too small
                }
                output[outPos++] = (uint8_t)(queue & 0xFF);
                queue >>= 8;
                numBits -= 8;
            }
            
            val = -1;
        }
    }
    
    // Handle remaining value
    if (val != -1) {
        if (outPos >= outputMaxLen) {
            return 0;
        }
        output[outPos++] = (uint8_t)((queue | ((uint32_t)val << numBits)) & 0xFF);
    }
    
    return outPos;
}
