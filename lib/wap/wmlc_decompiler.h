/**
 * wmlc_decompiler.h - WMLC (Compiled WML) Decompiler
 * 
 * Decompiles WBXML-encoded WML binary back to WML/XML text.
 * Uses token tables from Kannel's wml_definitions.h
 */

#ifndef WMLC_DECOMPILER_H
#define WMLC_DECOMPILER_H

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

/**
 * WMLC (Compiled WML) Decompiler
 * 
 * Decompiles WBXML-encoded WML binary back to WML/XML text.
 * Uses token tables from Kannel's wml_definitions.h
 */
class WMLCDecompiler {
public:
    /**
     * Decompile WMLC binary to WML text.
     * 
     * @param wmlc Input WMLC binary data
     * @param wmlcLen Length of WMLC data
     * @param output Output buffer for WML text
     * @param outputSize Size of output buffer
     * @return Number of bytes written to output, or 0 on error
     */
    static size_t decompile(const uint8_t* wmlc, size_t wmlcLen, 
                            char* output, size_t outputSize);
    
    /**
     * Get the WBXML version from WMLC header.
     * 
     * @param wmlc Input WMLC data
     * @param wmlcLen Length of WMLC data
     * @return WBXML version (1-5) or 0 on error
     */
    static int getVersion(const uint8_t* wmlc, size_t wmlcLen);
    
    /**
     * Get the public ID from WMLC header.
     * 
     * @param wmlc Input WMLC data
     * @param wmlcLen Length of WMLC data
     * @return Public ID or 0 on error
     */
    static unsigned long getPublicId(const uint8_t* wmlc, size_t wmlcLen);

private:
    // WBXML global tokens
    static const uint8_t WBXML_SWITCH_PAGE = 0x00;
    static const uint8_t WBXML_END = 0x01;
    static const uint8_t WBXML_ENTITY = 0x02;
    static const uint8_t WBXML_STR_I = 0x03;
    static const uint8_t WBXML_LITERAL = 0x04;
    static const uint8_t WBXML_EXT_I_0 = 0x40;
    static const uint8_t WBXML_EXT_I_1 = 0x41;
    static const uint8_t WBXML_EXT_I_2 = 0x42;
    static const uint8_t WBXML_PI = 0x43;
    static const uint8_t WBXML_LITERAL_C = 0x44;
    static const uint8_t WBXML_EXT_T_0 = 0x80;
    static const uint8_t WBXML_EXT_T_1 = 0x81;
    static const uint8_t WBXML_EXT_T_2 = 0x82;
    static const uint8_t WBXML_STR_T = 0x83;
    static const uint8_t WBXML_LITERAL_A = 0x84;
    static const uint8_t WBXML_EXT_0 = 0xC0;
    static const uint8_t WBXML_EXT_1 = 0xC1;
    static const uint8_t WBXML_EXT_2 = 0xC2;
    static const uint8_t WBXML_OPAQUE = 0xC3;
    static const uint8_t WBXML_LITERAL_AC = 0xC4;
    
    // Tag bits
    static const uint8_t TAG_HAS_CONTENT = 0x40;
    static const uint8_t TAG_HAS_ATTRS = 0x80;
    
    // Helper to decode multibyte integer (like uintvar)
    static size_t decodeMbUint(const uint8_t* data, size_t len, unsigned long* value);
    
    // Get element name from token
    static const char* getElementName(uint8_t token);
    
    // Get attribute name from token
    static const char* getAttributeName(uint8_t token, const char** value);
    
    // Get attribute value token string
    static const char* getAttributeValue(uint8_t token);
    
    // Decompile body content
    static size_t decompileBody(const uint8_t* data, size_t len, 
                                const char* stringTable, size_t stringTableLen,
                                char* output, size_t outputSize, int indent);
};

#endif // WMLC_DECOMPILER_H
