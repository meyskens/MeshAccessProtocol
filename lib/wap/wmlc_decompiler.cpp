/**
 * wmlc_decompiler.cpp - WMLC (Compiled WML) Decompiler Implementation
 * 
 * Decompiles WBXML-encoded WMLC back to WML/XML text.
 * Token tables derived from Kannel's wml_definitions.h
 */

#include "wmlc_decompiler.h"
#include <cstring>
#include <cstdio>

// WML Element tokens (tag code page 0)
struct WMLElement {
    uint8_t token;
    const char* name;
};

static const WMLElement wml_elements[] = {
    { 0x1C, "a" },
    { 0x1D, "td" },
    { 0x1E, "tr" },
    { 0x1F, "table" },
    { 0x20, "p" },
    { 0x21, "postfield" },
    { 0x22, "anchor" },
    { 0x23, "access" },
    { 0x24, "b" },
    { 0x25, "big" },
    { 0x26, "br" },
    { 0x27, "card" },
    { 0x28, "do" },
    { 0x29, "em" },
    { 0x2A, "fieldset" },
    { 0x2B, "go" },
    { 0x2C, "head" },
    { 0x2D, "i" },
    { 0x2E, "img" },
    { 0x2F, "input" },
    { 0x30, "meta" },
    { 0x31, "noop" },
    { 0x32, "prev" },
    { 0x33, "onevent" },
    { 0x34, "optgroup" },
    { 0x35, "option" },
    { 0x36, "refresh" },
    { 0x37, "select" },
    { 0x38, "small" },
    { 0x39, "strong" },
    { 0x3B, "template" },
    { 0x3C, "timer" },
    { 0x3D, "u" },
    { 0x3E, "setvar" },
    { 0x3F, "wml" },
    { 0, nullptr }
};

// WML Attribute start tokens
struct WMLAttribute {
    uint8_t token;
    const char* name;
    const char* value;  // nullptr if no value prefix
};

static const WMLAttribute wml_attributes[] = {
    { 0x05, "accept-charset", nullptr },
    { 0x06, "align", "bottom" },
    { 0x07, "align", "center" },
    { 0x08, "align", "left" },
    { 0x09, "align", "middle" },
    { 0x0A, "align", "right" },
    { 0x0B, "align", "top" },
    { 0x0C, "alt", nullptr },
    { 0x0D, "content", nullptr },
    { 0x0F, "domain", nullptr },
    { 0x10, "emptyok", "false" },
    { 0x11, "emptyok", "true" },
    { 0x12, "format", nullptr },
    { 0x13, "height", nullptr },
    { 0x14, "hspace", nullptr },
    { 0x15, "ivalue", nullptr },
    { 0x16, "iname", nullptr },
    { 0x18, "label", nullptr },
    { 0x19, "localsrc", nullptr },
    { 0x1A, "maxlength", nullptr },
    { 0x1B, "method", "get" },
    { 0x1C, "method", "post" },
    { 0x1D, "mode", "nowrap" },
    { 0x1E, "mode", "wrap" },
    { 0x1F, "multiple", "false" },
    { 0x20, "multiple", "true" },
    { 0x21, "name", nullptr },
    { 0x22, "newcontext", "false" },
    { 0x23, "newcontext", "true" },
    { 0x24, "onpick", nullptr },
    { 0x25, "onenterbackward", nullptr },
    { 0x26, "onenterforward", nullptr },
    { 0x27, "ontimer", nullptr },
    { 0x28, "optional", "false" },
    { 0x29, "optional", "true" },
    { 0x2A, "path", nullptr },
    { 0x2E, "scheme", nullptr },
    { 0x2F, "sendreferer", "false" },
    { 0x30, "sendreferer", "true" },
    { 0x31, "size", nullptr },
    { 0x32, "src", nullptr },
    { 0x33, "ordered", "true" },
    { 0x34, "ordered", "false" },
    { 0x35, "tabindex", nullptr },
    { 0x36, "title", nullptr },
    { 0x37, "type", nullptr },
    { 0x38, "type", "accept" },
    { 0x39, "type", "delete" },
    { 0x3A, "type", "help" },
    { 0x3B, "type", "password" },
    { 0x3C, "type", "onpick" },
    { 0x3D, "type", "onenterbackward" },
    { 0x3E, "type", "onenterforward" },
    { 0x3F, "type", "ontimer" },
    { 0x45, "type", "options" },
    { 0x46, "type", "prev" },
    { 0x47, "type", "reset" },
    { 0x48, "type", "text" },
    { 0x49, "type", "vnd." },
    { 0x4A, "href", nullptr },
    { 0x4B, "href", "http://" },
    { 0x4C, "href", "https://" },
    { 0x4D, "value", nullptr },
    { 0x4E, "vspace", nullptr },
    { 0x4F, "width", nullptr },
    { 0x50, "xml:lang", nullptr },
    { 0x52, "align", nullptr },
    { 0x53, "columns", nullptr },
    { 0x54, "class", nullptr },
    { 0x55, "id", nullptr },
    { 0x56, "forua", "false" },
    { 0x57, "forua", "true" },
    { 0x58, "src", "http://" },
    { 0x59, "src", "https://" },
    { 0x5A, "http-equiv", nullptr },
    { 0x5B, "http-equiv", "Content-Type" },
    { 0x5C, "content", "application/vnd.wap.wmlc;charset=" },
    { 0x5D, "http-equiv", "Expires" },
    { 0x5E, "accesskey", nullptr },
    { 0x5F, "enctype", nullptr },
    { 0x60, "enctype", "application/x-www-form-urlencoded" },
    { 0x61, "enctype", "multipart/form-data" },
    { 0x62, "xml:space", "preserve" },
    { 0x63, "xml:space", "default" },
    { 0x64, "cache-control", "no-cache" },
    { 0, nullptr, nullptr }
};

// WML Attribute value tokens
struct WMLAttrValue {
    uint8_t token;
    const char* value;
};

static const WMLAttrValue wml_attr_values[] = {
    { 0x85, ".com/" },
    { 0x86, ".edu/" },
    { 0x87, ".net/" },
    { 0x88, ".org/" },
    { 0x89, "accept" },
    { 0x8A, "bottom" },
    { 0x8B, "clear" },
    { 0x8C, "delete" },
    { 0x8D, "help" },
    { 0x8E, "http://" },
    { 0x8F, "http://www." },
    { 0x90, "https://" },
    { 0x91, "https://www." },
    { 0x93, "middle" },
    { 0x94, "nowrap" },
    { 0x95, "onpick" },
    { 0x96, "onenterbackward" },
    { 0x97, "onenterforward" },
    { 0x98, "ontimer" },
    { 0x99, "options" },
    { 0x9A, "password" },
    { 0x9B, "reset" },
    { 0x9D, "text" },
    { 0x9E, "top" },
    { 0x9F, "unknown" },
    { 0xA0, "wrap" },
    { 0xA1, "www." },
    { 0, nullptr }
};

const char* WMLCDecompiler::getElementName(uint8_t token) {
    // Strip content and attribute bits
    uint8_t baseToken = token & 0x3F;
    
    for (int i = 0; wml_elements[i].name != nullptr; i++) {
        if (wml_elements[i].token == baseToken) {
            return wml_elements[i].name;
        }
    }
    return nullptr;
}

const char* WMLCDecompiler::getAttributeName(uint8_t token, const char** value) {
    for (int i = 0; wml_attributes[i].name != nullptr; i++) {
        if (wml_attributes[i].token == token) {
            if (value) *value = wml_attributes[i].value;
            return wml_attributes[i].name;
        }
    }
    if (value) *value = nullptr;
    return nullptr;
}

const char* WMLCDecompiler::getAttributeValue(uint8_t token) {
    for (int i = 0; wml_attr_values[i].value != nullptr; i++) {
        if (wml_attr_values[i].token == token) {
            return wml_attr_values[i].value;
        }
    }
    return nullptr;
}

size_t WMLCDecompiler::decodeMbUint(const uint8_t* data, size_t len, unsigned long* value) {
    if (data == nullptr || len == 0 || value == nullptr) {
        return 0;
    }
    
    *value = 0;
    size_t i = 0;
    
    do {
        if (i >= len) return 0;
        *value = (*value << 7) | (data[i] & 0x7F);
    } while (data[i++] & 0x80);
    
    return i;
}

int WMLCDecompiler::getVersion(const uint8_t* wmlc, size_t wmlcLen) {
    if (wmlc == nullptr || wmlcLen < 1) {
        return 0;
    }
    return wmlc[0] + 1;  // Version 1.0 = 0x00, 1.1 = 0x01, etc.
}

unsigned long WMLCDecompiler::getPublicId(const uint8_t* wmlc, size_t wmlcLen) {
    if (wmlc == nullptr || wmlcLen < 2) {
        return 0;
    }
    
    unsigned long publicId;
    decodeMbUint(&wmlc[1], wmlcLen - 1, &publicId);
    return publicId;
}

size_t WMLCDecompiler::decompileBody(const uint8_t* data, size_t len,
                                      const char* stringTable, size_t stringTableLen,
                                      char* output, size_t outputSize, int indent) {
    if (data == nullptr || output == nullptr || len == 0) {
        return 0;
    }
    
    size_t pos = 0;      // Input position
    size_t outPos = 0;   // Output position
    
    // Helper to append string safely
    auto appendStr = [&](const char* str) {
        size_t slen = strlen(str);
        if (outPos + slen < outputSize) {
            memcpy(&output[outPos], str, slen);
            outPos += slen;
        }
    };
    
    // Helper to append char safely
    auto appendChar = [&](char c) {
        if (outPos + 1 < outputSize) {
            output[outPos++] = c;
        }
    };
    
    // Helper for indentation
    auto appendIndent = [&](int level) {
        for (int i = 0; i < level && outPos + 1 < outputSize; i++) {
            output[outPos++] = ' ';
        }
    };
    
    // Stack for tracking open elements
    const char* elementStack[32];
    int stackDepth = 0;
    
    while (pos < len) {
        uint8_t token = data[pos++];
        
        // Global tokens
        switch (token) {
            case WBXML_SWITCH_PAGE:
                // Code page switch - skip the page number
                if (pos < len) pos++;
                continue;
                
            case WBXML_END:
                // End of element
                if (stackDepth > 0) {
                    stackDepth--;
                    appendStr("</");
                    appendStr(elementStack[stackDepth]);
                    appendChar('>');
                }
                continue;
                
            case WBXML_ENTITY:
                // Character entity
                if (pos < len) {
                    unsigned long entity;
                    size_t consumed = decodeMbUint(&data[pos], len - pos, &entity);
                    if (consumed > 0) {
                        pos += consumed;
                        char entityBuf[16];
                        snprintf(entityBuf, sizeof(entityBuf), "&#%lu;", entity);
                        appendStr(entityBuf);
                    }
                }
                continue;
                
            case WBXML_STR_I:
                // Inline string (null-terminated)
                while (pos < len && data[pos] != 0) {
                    appendChar((char)data[pos++]);
                }
                if (pos < len) pos++;  // Skip null terminator
                continue;
                
            case WBXML_STR_T:
                // String table reference
                if (pos < len) {
                    unsigned long offset;
                    size_t consumed = decodeMbUint(&data[pos], len - pos, &offset);
                    if (consumed > 0) {
                        pos += consumed;
                        if (stringTable && offset < stringTableLen) {
                            appendStr(&stringTable[offset]);
                        }
                    }
                }
                continue;
                
            case WBXML_EXT_I_0:
            case WBXML_EXT_I_1:
            case WBXML_EXT_I_2:
                // Extension with inline string - output as variable
                {
                    appendStr("$(");
                    while (pos < len && data[pos] != 0) {
                        appendChar((char)data[pos++]);
                    }
                    if (pos < len) pos++;
                    // Add escape suffix based on extension type
                    if (token == WBXML_EXT_I_1) appendStr(":e");
                    else if (token == WBXML_EXT_I_2) appendStr(":u");
                    appendChar(')');
                }
                continue;
                
            case WBXML_EXT_T_0:
            case WBXML_EXT_T_1:
            case WBXML_EXT_T_2:
                // Extension with string table reference - output as variable
                if (pos < len) {
                    unsigned long offset;
                    size_t consumed = decodeMbUint(&data[pos], len - pos, &offset);
                    if (consumed > 0) {
                        pos += consumed;
                        appendStr("$(");
                        if (stringTable && offset < stringTableLen) {
                            appendStr(&stringTable[offset]);
                        }
                        if (token == WBXML_EXT_T_1) appendStr(":e");
                        else if (token == WBXML_EXT_T_2) appendStr(":u");
                        appendChar(')');
                    }
                }
                continue;
                
            case WBXML_OPAQUE:
                // Opaque data
                if (pos < len) {
                    unsigned long opaqueLen;
                    size_t consumed = decodeMbUint(&data[pos], len - pos, &opaqueLen);
                    if (consumed > 0) {
                        pos += consumed;
                        // Skip opaque data for now (could be image data, etc.)
                        pos += opaqueLen;
                    }
                }
                continue;
                
            case WBXML_PI:
                // Processing instruction - skip
                continue;
                
            case WBXML_LITERAL:
            case WBXML_LITERAL_A:
            case WBXML_LITERAL_C:
            case WBXML_LITERAL_AC:
                // Literal element from string table
                if (pos < len) {
                    unsigned long offset;
                    size_t consumed = decodeMbUint(&data[pos], len - pos, &offset);
                    if (consumed > 0) {
                        pos += consumed;
                        const char* elemName = (stringTable && offset < stringTableLen) 
                                               ? &stringTable[offset] : "unknown";
                        
                        bool hasAttrs = (token == WBXML_LITERAL_A || token == WBXML_LITERAL_AC);
                        bool hasContent = (token == WBXML_LITERAL_C || token == WBXML_LITERAL_AC);
                        
                        appendChar('<');
                        appendStr(elemName);
                        
                        // Parse attributes if present
                        if (hasAttrs) {
                            while (pos < len && data[pos] != WBXML_END) {
                                // Similar attribute parsing as below
                                pos++;
                            }
                            if (pos < len) pos++;  // Skip END
                        }
                        
                        if (hasContent) {
                            appendChar('>');
                            if (stackDepth < 32) {
                                elementStack[stackDepth++] = elemName;
                            }
                        } else {
                            appendStr("/>");
                        }
                    }
                }
                continue;
        }
        
        // Check for element token (0x05-0x3F range, with possible bits set)
        if (token >= 0x05) {
            bool hasAttrs = (token & TAG_HAS_ATTRS) != 0;
            bool hasContent = (token & TAG_HAS_CONTENT) != 0;
            uint8_t baseToken = token & 0x3F;
            
            const char* elemName = getElementName(baseToken);
            if (elemName == nullptr) {
                // Unknown token - skip
                continue;
            }
            
            appendChar('<');
            appendStr(elemName);
            
            // Parse attributes
            if (hasAttrs) {
                while (pos < len) {
                    uint8_t attrToken = data[pos];
                    
                    if (attrToken == WBXML_END) {
                        pos++;
                        break;
                    }
                    
                    if (attrToken == WBXML_STR_I) {
                        // Inline string in attributes
                        pos++;
                        while (pos < len && data[pos] != 0) {
                            appendChar((char)data[pos++]);
                        }
                        if (pos < len) pos++;
                        continue;
                    }
                    
                    if (attrToken == WBXML_STR_T) {
                        // String table reference
                        pos++;
                        unsigned long offset;
                        size_t consumed = decodeMbUint(&data[pos], len - pos, &offset);
                        if (consumed > 0) {
                            pos += consumed;
                            if (stringTable && offset < stringTableLen) {
                                appendStr(&stringTable[offset]);
                            }
                        }
                        continue;
                    }
                    
                    // Check for attribute value token (0x80+)
                    if (attrToken >= 0x80) {
                        const char* attrVal = getAttributeValue(attrToken);
                        if (attrVal) {
                            appendStr(attrVal);
                        }
                        pos++;
                        continue;
                    }
                    
                    // Attribute start token
                    const char* attrValuePrefix = nullptr;
                    const char* attrName = getAttributeName(attrToken, &attrValuePrefix);
                    
                    if (attrName) {
                        appendChar(' ');
                        appendStr(attrName);
                        appendStr("=\"");
                        if (attrValuePrefix) {
                            appendStr(attrValuePrefix);
                        }
                        pos++;
                        
                        // Read attribute value continuation
                        while (pos < len) {
                            uint8_t valToken = data[pos];
                            
                            // End of attributes or next attribute
                            if (valToken == WBXML_END || (valToken < 0x80 && valToken >= 0x05)) {
                                break;
                            }
                            
                            if (valToken == WBXML_STR_I) {
                                pos++;
                                while (pos < len && data[pos] != 0) {
                                    appendChar((char)data[pos++]);
                                }
                                if (pos < len) pos++;
                            } else if (valToken == WBXML_STR_T) {
                                pos++;
                                unsigned long offset;
                                size_t consumed = decodeMbUint(&data[pos], len - pos, &offset);
                                if (consumed > 0) {
                                    pos += consumed;
                                    if (stringTable && offset < stringTableLen) {
                                        appendStr(&stringTable[offset]);
                                    }
                                }
                            } else if (valToken >= WBXML_EXT_I_0 && valToken <= WBXML_EXT_I_2) {
                                // Variable in attribute (inline string)
                                pos++;
                                appendStr("$(");
                                while (pos < len && data[pos] != 0) {
                                    appendChar((char)data[pos++]);
                                }
                                if (pos < len) pos++;
                                if (valToken == WBXML_EXT_I_1) appendStr(":e");
                                else if (valToken == WBXML_EXT_I_2) appendStr(":u");
                                appendChar(')');
                            } else if (valToken >= WBXML_EXT_T_0 && valToken <= WBXML_EXT_T_2) {
                                // Variable in attribute (string table reference)
                                pos++;
                                unsigned long offset;
                                size_t consumed = decodeMbUint(&data[pos], len - pos, &offset);
                                if (consumed > 0) {
                                    pos += consumed;
                                    appendStr("$(");
                                    if (stringTable && offset < stringTableLen) {
                                        appendStr(&stringTable[offset]);
                                    }
                                    if (valToken == WBXML_EXT_T_1) appendStr(":e");
                                    else if (valToken == WBXML_EXT_T_2) appendStr(":u");
                                    appendChar(')');
                                }
                            } else if (valToken >= 0x80) {
                                const char* attrVal = getAttributeValue(valToken);
                                if (attrVal) {
                                    appendStr(attrVal);
                                }
                                pos++;
                            } else {
                                pos++;
                            }
                        }
                        
                        appendChar('"');
                    } else {
                        pos++;
                    }
                }
            }
            
            if (hasContent) {
                appendChar('>');
                if (stackDepth < 32) {
                    elementStack[stackDepth++] = elemName;
                }
            } else {
                appendStr("/>");
            }
        }
    }
    
    // Close any remaining open elements
    while (stackDepth > 0) {
        stackDepth--;
        appendStr("</");
        appendStr(elementStack[stackDepth]);
        appendChar('>');
    }
    
    // Null terminate
    if (outPos < outputSize) {
        output[outPos] = '\0';
    } else if (outputSize > 0) {
        output[outputSize - 1] = '\0';
    }
    
    return outPos;
}

size_t WMLCDecompiler::decompile(const uint8_t* wmlc, size_t wmlcLen,
                                  char* output, size_t outputSize) {
    if (wmlc == nullptr || output == nullptr || wmlcLen < 4 || outputSize < 100) {
        if (output && outputSize > 0) output[0] = '\0';
        return 0;
    }
    
    size_t pos = 0;
    size_t outPos = 0;
    
    // Helper to append
    auto append = [&](const char* str) {
        size_t slen = strlen(str);
        if (outPos + slen < outputSize) {
            memcpy(&output[outPos], str, slen);
            outPos += slen;
        }
    };
    
    // 1. Version byte
    uint8_t version = wmlc[pos++];
    (void)version;  // WBXML version, not used in output
    
    // 2. Public ID (mb_u_int32)
    unsigned long publicId;
    size_t consumed = decodeMbUint(&wmlc[pos], wmlcLen - pos, &publicId);
    if (consumed == 0) {
        output[0] = '\0';
        return 0;
    }
    pos += consumed;
    
    // If publicId is 0, there's a string table index for DTD
    if (publicId == 0 && pos < wmlcLen) {
        // Skip string table index for public ID
        consumed = decodeMbUint(&wmlc[pos], wmlcLen - pos, &publicId);
        if (consumed > 0) pos += consumed;
    }
    
    // 3. Charset (mb_u_int32)
    unsigned long charset = 0;
    consumed = decodeMbUint(&wmlc[pos], wmlcLen - pos, &charset);
    if (consumed == 0) {
        output[0] = '\0';
        return 0;
    }
    pos += consumed;
    
    // 4. String table length (mb_u_int32)
    unsigned long stringTableLen = 0;
    consumed = decodeMbUint(&wmlc[pos], wmlcLen - pos, &stringTableLen);
    if (consumed == 0) {
        output[0] = '\0';
        return 0;
    }
    pos += consumed;
    
    // 5. String table
    const char* stringTable = nullptr;
    if (stringTableLen > 0) {
        if (pos + stringTableLen > wmlcLen) {
            output[0] = '\0';
            return 0;
        }
        stringTable = (const char*)&wmlc[pos];
        pos += stringTableLen;
    }
    
    // Write XML declaration
    append("<?xml version=\"1.0\"?>\n");
    
    // Write DOCTYPE based on public ID
    // WML 1.1 = 0x04, WML 1.2 = 0x09, WML 1.3 = 0x0A
    if (publicId == 0x04) {
        append("<!DOCTYPE wml PUBLIC \"-//WAPFORUM//DTD WML 1.1//EN\" \"http://www.wapforum.org/DTD/wml_1.1.xml\">\n");
    } else if (publicId == 0x09) {
        append("<!DOCTYPE wml PUBLIC \"-//WAPFORUM//DTD WML 1.2//EN\" \"http://www.wapforum.org/DTD/wml12.dtd\">\n");
    } else if (publicId == 0x0A) {
        append("<!DOCTYPE wml PUBLIC \"-//WAPFORUM//DTD WML 1.3//EN\" \"http://www.wapforum.org/DTD/wml13.dtd\">\n");
    }
    
    // 6. Body - decompile the rest
    if (pos < wmlcLen) {
        outPos += decompileBody(&wmlc[pos], wmlcLen - pos, 
                                stringTable, stringTableLen,
                                &output[outPos], outputSize - outPos, 0);
    }
    
    // Ensure null termination
    if (outPos < outputSize) {
        output[outPos] = '\0';
    } else if (outputSize > 0) {
        output[outputSize - 1] = '\0';
    }
    
    return outPos;
}
