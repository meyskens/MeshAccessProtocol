#ifndef MODE_AP_H
#define MODE_AP_H

#ifdef ESP32

#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiServer.h>
#include <WiFiUdp.h>
#include <DNSServer.h>
#include <functional>
#include <wap_request.h>
#include <wap_response.h>
#include <wmlc_decompiler.h>

// Forward declaration - defined in main.cpp
extern void displayStatus(const char* line1, const char* line2, const char* line3, const char* line4);

// AP Mode Configuration
#ifndef AP_SSID
  #define AP_SSID "MAP"
#endif
#ifndef AP_CHANNEL
  #define AP_CHANNEL 1
#endif
#ifndef AP_MAX_CONNECTIONS
  #define AP_MAX_CONNECTIONS 4
#endif

// Proxy node public key - the MeshCore node that will forward to WAPBOX
#ifndef PROXY_NODE_PUBKEY
  #define PROXY_NODE_PUBKEY "21BDD77007F54EF3C5FEC28C55A84AE26076928EA2EF1A3F0307711EB4846EE9" // Replace with your proxy node's public key
#endif

// DNS Server Configuration
#define DNS_PORT 53

// HTTP Server Configuration
#define HTTP_PORT 80

#ifndef WAPBOX_PORT
  #define WAPBOX_PORT 9200  // Standard WAP gateway port
#endif

// UDH (User Data Header) structure for datagram
struct AP_UDH {
  uint8_t headerLen;
  uint8_t ei;         // Element Identifier (0x05 = Application Port Addressing 16-bit)
  uint8_t eiLength;   // Length of element data (0x04 for 16-bit ports)
  uint16_t source;    // Source port
  uint16_t dest;      // Destination port
};

// Concatenated message tracking for reassembly (responses from proxy)
struct AP_ConcatMessage {
  bool active;
  uint8_t refNum;           // Reference number
  uint8_t totalParts;
  uint8_t receivedParts;
  uint8_t partReceived[16]; // Bitmask for which parts received (max 16 parts)
  uint8_t data[4096];       // Reassembled data buffer (larger for WAP responses)
  uint16_t partSizes[16];   // Size of each part
  uint16_t sourcePort;
  uint16_t destPort;
  String senderMeshId;
  unsigned long lastUpdate;
};

// Pending WAP request tracking (waiting for mesh response)
struct AP_PendingRequest {
  bool active;
  uint8_t transactionId;
  uint16_t sourcePort;
  uint16_t destPort;
  WiFiClient* client;       // Client waiting for response (nullptr if timed out)
  unsigned long timestamp;
};

// AP Mode state
static bool ap_initialized = false;
static bool sta_connected = false;
static int ap_connected_clients = 0;

// Proxy path discovery state
static bool ap_proxy_path_discovered = false;
static int ap_proxy_path_len = -1;  // -1 = not discovered, 0 = direct, 1+ = hops
static int ap_proxy_discovery_attempts = 0;
static const int AP_PROXY_DISCOVERY_MAX_RETRIES = 5;
static unsigned long ap_proxy_discovery_start_time = 0;
static bool ap_proxy_discovery_in_progress = false;

// DNS Server instance - responds to all queries with AP IP (captive portal style)
static DNSServer dnsServer;

// HTTP Server instance
static WiFiServer httpServer(HTTP_PORT);

// Mesh communication callback
static std::function<void(const String&, const uint8_t*, size_t)> ap_sendMeshCallback = nullptr;

// Mesh loop callback - MUST be set to keep mesh alive during blocking waits
static std::function<void()> ap_meshLoopCallback = nullptr;

// Concatenated message reassembly for incoming mesh responses
static const int AP_MAX_CONCAT_MESSAGES = 4;
static AP_ConcatMessage ap_concatMessages[AP_MAX_CONCAT_MESSAGES];

// Response buffer for completed mesh responses
static uint8_t ap_meshResponseBuffer[4096];
static size_t ap_meshResponseLen = 0;
static bool ap_meshResponseReady = false;
static uint8_t ap_meshResponseTid = 0;

// Early header sending state - send HTTP headers when first packet arrives
static WiFiClient* ap_waitingClient = nullptr;    // Client waiting for mesh response
static bool ap_headersSent = false;               // Have we already sent HTTP headers?
static HTTPResponse ap_earlyResponse;             // Decoded response headers from first packet
static bool ap_isWMLC = false;                    // Is response WMLC that needs decompilation?
static size_t ap_bodyBytesReceived = 0;           // Track body bytes for progress

// Keep-alive interval for HTTP clients waiting for mesh response (ms)
static const unsigned long AP_KEEPALIVE_INTERVAL_MS = 2000;

// Transaction counter for WAP requests
static uint8_t transactionCounter = 0;

// WDP session display state
static bool ap_wdpSessionActive = false;
static size_t ap_wdpBytesSent = 0;
static int ap_wdpTotalParts = 0;
static int ap_wdpReceivedParts = 0;
static unsigned long ap_lastPartReceivedTime = 0;  // Timestamp of last received part (for timeout extension)

// Flag to indicate display needs refresh (WiFi client count changed)
static bool ap_displayNeedsUpdate = false;

// Current request source port for response matching
static uint16_t ap_currentRequestPort = 0;

// Flag to track if a request is currently being processed via mesh
static bool ap_requestInProgress = false;

// Generate random source port (1024-9999)
static uint16_t ap_generateSourcePort() {
  return 1024 + (esp_random() % (9999 - 1024 + 1));
}

/**
 * HTTP Request structure for parsing incoming requests
 * Note: Reduced buffer sizes to save memory
 */
struct HTTPRequest {
  char method[16];      // GET, POST, etc.
  char path[256];       // Request path
  char host[128];       // Host header
  char contentType[64]; // Content-Type (for POST)
  size_t contentLength; // Content-Length
  uint8_t body[512];    // Request body for POST (reduced from 2048)
  size_t bodyLen;
};

// Static buffers to avoid stack overflow
static uint8_t http_wapRequest[512];
static uint8_t http_wapResponse[4096];
static char http_decompiled[8192];
static char http_url[512];
static HTTPRequest http_req;

/**
 * Parse HTTP request from client
 */
bool parseHTTPRequest(WiFiClient& client, HTTPRequest* req) {
  memset(req, 0, sizeof(HTTPRequest));
  
  // Read request line with timeout
  String requestLine = "";
  unsigned long startTime = millis();
  while (client.connected() && (millis() - startTime < 5000)) {
    if (client.available()) {
      char c = client.read();
      if (c == '\n') break;
      if (c != '\r') requestLine += c;
    }
  }
  
  if (requestLine.length() == 0) {
    Serial.println("HTTP: Empty request line");
    return false;
  }
  
  Serial.printf("HTTP Request: %s\n", requestLine.c_str());
  
  // Parse method and path
  int firstSpace = requestLine.indexOf(' ');
  int secondSpace = requestLine.indexOf(' ', firstSpace + 1);
  
  if (firstSpace < 0 || secondSpace < 0) {
    Serial.println("HTTP: Invalid request line format");
    return false;
  }
  
  String method = requestLine.substring(0, firstSpace);
  String path = requestLine.substring(firstSpace + 1, secondSpace);
  
  strncpy(req->method, method.c_str(), sizeof(req->method) - 1);
  strncpy(req->path, path.c_str(), sizeof(req->path) - 1);
  
  // Read headers
  startTime = millis();
  while (client.connected() && (millis() - startTime < 5000)) {
    String line = "";
    while (client.connected() && (millis() - startTime < 5000)) {
      if (client.available()) {
        char c = client.read();
        if (c == '\n') break;
        if (c != '\r') line += c;
      }
    }
    
    if (line.length() == 0) {
      // Empty line = end of headers
      break;
    }
    
    // Parse header
    int colonPos = line.indexOf(':');
    if (colonPos > 0) {
      String headerName = line.substring(0, colonPos);
      String headerValue = line.substring(colonPos + 1);
      headerValue.trim();
      
      headerName.toLowerCase();
      
      if (headerName == "host") {
        strncpy(req->host, headerValue.c_str(), sizeof(req->host) - 1);
      } else if (headerName == "content-type") {
        strncpy(req->contentType, headerValue.c_str(), sizeof(req->contentType) - 1);
      } else if (headerName == "content-length") {
        req->contentLength = headerValue.toInt();
      }
    }
  }
  
  // Read body if present (for POST)
  if (req->contentLength > 0 && req->contentLength < sizeof(req->body)) {
    size_t bytesRead = 0;
    startTime = millis();
    while (bytesRead < req->contentLength && (millis() - startTime < 5000)) {
      if (client.available()) {
        req->body[bytesRead++] = client.read();
      }
    }
    req->bodyLen = bytesRead;
  }
  
  Serial.printf("HTTP: Method=%s Path=%s Host=%s\n", req->method, req->path, req->host);
  
  return true;
}

/**
 * Parse UDH from incoming mesh message
 */
bool ap_parseUDH(const uint8_t* data, size_t len, AP_UDH& udh) {
  if (len < 7) {
    Serial.println("AP-WDP: Invalid UDH - too short");
    return false;
  }
  
  udh.headerLen = data[0];
  udh.ei = data[1];
  udh.eiLength = data[2];
  udh.dest = (data[3] << 8) | data[4];    // Destination port first
  udh.source = (data[5] << 8) | data[6];  // Source port second
  
  return true;
}

/**
 * Parse concatenated message UDH (0x0B header length indicates concat)
 */
bool ap_parseConcatUDH(const uint8_t* data, size_t len, uint8_t& refNum, uint8_t& totalParts, 
                       uint8_t& currentPart, AP_UDH& udh) {
  if (len < 12 || data[0] != 0x0B) {
    return false;
  }
  
  // Concatenated header format:
  // [0] = 0x0B (header length)
  // [1] = 0x00 (concat IE identifier)
  // [2] = 0x03 (concat IE length)
  // [3] = reference number
  // [4] = total parts
  // [5] = current part number
  // [6] = 0x05 (port addressing IE)
  // [7] = 0x04 (port IE length)
  // [8-9] = destination port
  // [10-11] = source port
  
  if (data[1] != 0x00 || data[2] != 0x03) {
    return false;
  }
  
  refNum = data[3];
  totalParts = data[4];
  currentPart = data[5];
  
  udh.headerLen = data[0];
  udh.ei = data[6];
  udh.eiLength = data[7];
  udh.dest = (data[8] << 8) | data[9];
  udh.source = (data[10] << 8) | data[11];
  
  return true;
}

/**
 * Clear/reset a concat message slot
 */
void ap_clearConcatMessage(AP_ConcatMessage* msg) {
  msg->active = false;
  msg->refNum = 0;
  msg->totalParts = 0;
  msg->receivedParts = 0;
  memset(msg->partReceived, 0, sizeof(msg->partReceived));
  memset(msg->data, 0, sizeof(msg->data));
  memset(msg->partSizes, 0, sizeof(msg->partSizes));
  msg->sourcePort = 0;
  msg->destPort = 0;
  msg->senderMeshId = "";
  msg->lastUpdate = 0;
}

/**
 * Update display during WDP session
 */
void ap_updateWDPDisplay() {
  char line2[32];
  char line3[32];
  char line4[32];
  
  snprintf(line2, sizeof(line2), "WDP Session");
  snprintf(line3, sizeof(line3), "Sent: %d bytes", (int)ap_wdpBytesSent);
  
  if (ap_wdpTotalParts > 0) {
    snprintf(line4, sizeof(line4), "Recv: %d/%d parts", ap_wdpReceivedParts, ap_wdpTotalParts);
  } else {
    snprintf(line4, sizeof(line4), "Waiting for reply...");
  }
  
  displayStatus("MeshAccessProtocol", line2, line3, line4);
}

/**
 * Restore normal display after WDP session
 */
void ap_restoreNormalDisplay() {
  ap_wdpSessionActive = false;
  ap_wdpBytesSent = 0;
  ap_wdpTotalParts = 0;
  ap_wdpReceivedParts = 0;
}

/**
 * Send WDP message via mesh with fragmentation if needed
 */
void ap_sendWDPViaMesh(const String& to, uint16_t srcPort, uint16_t dstPort, 
                       const uint8_t* data, size_t len) {
  if (!ap_sendMeshCallback) {
    Serial.println("AP-WDP: No mesh callback configured!");
    return;
  }
  
  // Start WDP session display
  ap_wdpSessionActive = true;
  ap_wdpBytesSent = len;
  ap_wdpTotalParts = 0;
  ap_wdpReceivedParts = 0;
  ap_updateWDPDisplay();
  
  // MeshCore text limit is 150 chars, Base91 expands by ~1.23x
  // So max binary bytes per message is ~121, minus UDH overhead
  const size_t maxPayloadSimple = MESHCORE_MAX_BINARY_PAYLOAD - 7;   // Simple UDH is 7 bytes = 114 bytes payload
  const size_t maxPayloadConcat = MESHCORE_MAX_BINARY_PAYLOAD - 12;  // Concat UDH is 12 bytes = 109 bytes payload
  
  if (len <= maxPayloadSimple) {
    // Simple message (no fragmentation needed)
    uint8_t msg[MESHCORE_MAX_BINARY_PAYLOAD];
    msg[0] = 0x06;  // UDH length
    msg[1] = 0x05;  // Application Port Addressing, 16-bit
    msg[2] = 0x04;  // Length of port data
    msg[3] = (dstPort >> 8) & 0xFF;
    msg[4] = dstPort & 0xFF;
    msg[5] = (srcPort >> 8) & 0xFF;
    msg[6] = srcPort & 0xFF;
    memcpy(&msg[7], data, len);
    
    Serial.printf("AP-WDP: Sending simple message (%d bytes) to %s\n", 7 + len, to.c_str());
    ap_sendMeshCallback(to, msg, 7 + len);
  } else {
    // Concatenated message (fragmentation needed)
    int totalParts = (len + maxPayloadConcat - 1) / maxPayloadConcat;
    uint8_t refNum = (millis() & 0xFF);  // Simple reference number
    
    Serial.printf("AP-WDP: Fragmenting %d bytes into %d parts\n", len, totalParts);
    
    for (int part = 1; part <= totalParts; part++) {
      uint8_t msg[MESHCORE_MAX_BINARY_PAYLOAD];
      
      // Concatenated UDH header
      msg[0] = 0x0B;  // UDH length
      msg[1] = 0x00;  // Concatenation IE identifier
      msg[2] = 0x03;  // Concatenation IE length
      msg[3] = refNum;
      msg[4] = (uint8_t)totalParts;
      msg[5] = (uint8_t)part;
      msg[6] = 0x05;  // Application Port Addressing, 16-bit
      msg[7] = 0x04;  // Length of port data
      msg[8] = (dstPort >> 8) & 0xFF;
      msg[9] = dstPort & 0xFF;
      msg[10] = (srcPort >> 8) & 0xFF;
      msg[11] = srcPort & 0xFF;
      
      // Copy payload fragment
      size_t offset = (part - 1) * maxPayloadConcat;
      size_t partLen = (len - offset < maxPayloadConcat) ? (len - offset) : maxPayloadConcat;
      memcpy(&msg[12], &data[offset], partLen);
      
      Serial.printf("AP-WDP: Sending part %d/%d (%d bytes)\n", part, totalParts, 12 + partLen);
      ap_sendMeshCallback(to, msg, 12 + partLen);
    }
  }
}

/**
 * Send WAP request via mesh to proxy node and wait for response
 * It will deny any futher HTTP requests as browsers will retry because they deem us too slow
 */
bool sendWAPRequestViaMesh(const uint8_t* request, size_t requestLen,
                           uint8_t* response, size_t* responseLen, size_t responseMaxLen,
                           int timeoutMs = 40000, WiFiClient* keepAliveClient = nullptr) {
  
  Serial.printf("AP-HTTP: Sending %zu bytes WAP request via mesh to proxy %s\n", 
                requestLen, PROXY_NODE_PUBKEY);
  
  // Mark request as in progress and stop HTTP server to refuse new SYNs
  ap_requestInProgress = true;
  httpServer.end();
  Serial.println("AP-HTTP: Stopped HTTP server during mesh request");
  
  // Clear any previous response and early header state
  ap_meshResponseReady = false;
  ap_meshResponseLen = 0;
  ap_waitingClient = keepAliveClient;
  ap_headersSent = false;
  ap_isWMLC = false;
  ap_bodyBytesReceived = 0;
  memset(&ap_earlyResponse, 0, sizeof(ap_earlyResponse));
  
  // Generate random source port for this request (used for response routing)
  ap_currentRequestPort = ap_generateSourcePort();
  
  // Send request via mesh with WDP headers
  // Use random source port and WAPBOX_PORT as destination
  Serial.printf("AP-HTTP: Using source port %d for request tracking\n", ap_currentRequestPort);
  ap_sendWDPViaMesh(String(PROXY_NODE_PUBKEY), ap_currentRequestPort, WAPBOX_PORT, request, requestLen);
  
  // Wait for response with timeout
  unsigned long startTime = millis();
  unsigned long lastKeepAlive = startTime;
  ap_lastPartReceivedTime = startTime;  // Initialize last part time
  
  while ((millis() - ap_lastPartReceivedTime) < (unsigned long)timeoutMs) {
    // Call mesh loop to process incoming packets and send ACKs
    if (ap_meshLoopCallback) {
      ap_meshLoopCallback();
    }
    
    // Check if client disconnected while waiting
    if (keepAliveClient && !keepAliveClient->connected()) {
      Serial.println("AP-HTTP: Client disconnected while waiting for mesh response");
      ap_meshResponseReady = false;
      ap_currentRequestPort = 0;
      ap_requestInProgress = false;
      ap_waitingClient = nullptr;
      ap_headersSent = false;
      ap_restoreNormalDisplay();
      httpServer.begin();  // Restart HTTP server
      Serial.println("AP-HTTP: Restarted HTTP server after client disconnect");
      return false;
    }
    
    // Send keep-alive to prevent client timeout (send a space periodically)
    // Note: This works because we haven't sent headers yet, and leading whitespace
    // before HTTP response is tolerated by UC Browser, we should revisit this to see
    // if our SYN no reply is enough as this does upset any proper browser.
    //if (keepAliveClient && (millis() - lastKeepAlive) >= AP_KEEPALIVE_INTERVAL_MS) {
    //  // Send a single space as keep-alive
    //  keepAliveClient->write(' ');
    //  keepAliveClient->flush();
    //  lastKeepAlive = millis();
    //}
    
    // Check if response has arrived via mesh
    if (ap_meshResponseReady) {
      size_t copyLen = (ap_meshResponseLen < responseMaxLen) ? ap_meshResponseLen : responseMaxLen;
      memcpy(response, ap_meshResponseBuffer, copyLen);
      *responseLen = copyLen;
      ap_meshResponseReady = false;
      ap_requestInProgress = false;
      ap_waitingClient = nullptr;  // Clear client reference
      httpServer.begin();  // Restart HTTP server
      Serial.println("AP-HTTP: Restarted HTTP server after mesh response");
      Serial.printf("AP-HTTP: Received %zu bytes response via mesh\n", copyLen);
      return true;
    }
    delay(10);
    yield();  // Allow other tasks to run
  }
  
  ap_requestInProgress = false;
  ap_waitingClient = nullptr;  // Clear client reference
  ap_headersSent = false;
  ap_restoreNormalDisplay();
  httpServer.begin();  // Restart HTTP server
  Serial.println("AP-HTTP: Restarted HTTP server after timeout");
  Serial.println("AP-HTTP: Timeout waiting for mesh response from proxy");
  return false;
}

/**
 * Build full URL from host and path
 */
void buildURL(const HTTPRequest* req, char* url, size_t urlSize) {
  // If path already contains http://, use as-is
  if (strncmp(req->path, "http://", 7) == 0 || strncmp(req->path, "https://", 8) == 0) {
    strncpy(url, req->path, urlSize - 1);
    url[urlSize - 1] = '\0';
    return;
  }
  
  // Build URL from host and path
  if (strlen(req->host) > 0) {
    snprintf(url, urlSize, "http://%s%s", req->host, req->path);
  } else {
    // No host header - use bevelgacom WAP as fallback
    snprintf(url, urlSize, "http://wap.bevelgacom.be%s", req->path);
  }
}

/**
 * Handle HTTP request and proxy to WAP
 * Uses static buffers to avoid stack overflow
 */
void handleHTTPRequest(WiFiClient& client) {
  // Use static buffer for request parsing
  if (!parseHTTPRequest(client, &http_req)) {
    // Send error response
    client.println("HTTP/1.1 400 Bad Request");
    client.println("Content-Type: text/plain");
    client.println("Connection: close");
    client.println();
    client.println("Bad Request");
    return;
  }
  
  // Block connectivity check requests - don't forward to mesh
  if (strstr(http_req.host, "connectivitycheck.gstatic.com") != nullptr ||
      strstr(http_req.host, "connectivitycheck.android.com") != nullptr ||
      strstr(http_req.host, "clients3.google.com") != nullptr ||
      strstr(http_req.host, "captive.apple.com") != nullptr ||
      strstr(http_req.host, "detectportal.firefox.com") != nullptr) {
    Serial.printf("HTTP: Blocking connectivity check to %s\n", http_req.host);
    // we do not send a 204 as that makes Anroid very angry, just close the connection act like we are broken WiFi
    return;
  }
  
  // Build URL for WAP request (using static buffer)
  buildURL(&http_req, http_url, sizeof(http_url));
  
  Serial.printf("HTTP: Proxying to WAP URL: %s\n", http_url);
  
  // Create WAP/WSP request (using static buffer)
  size_t wapRequestLen = 0;
  uint8_t tid = transactionCounter++;
  
  if (strcmp(http_req.method, "GET") == 0 || strcmp(http_req.method, "HEAD") == 0) {
    // Create GET request with host header
    wapRequestLen = WAPRequest::createGetRequest(http_url, tid, http_wapRequest, sizeof(http_wapRequest), true);
    
    // Debug: Print the generated request
    Serial.printf("AP-HTTP: Created WAP request (%zu bytes), TID=%02X\n", wapRequestLen, tid);
    Serial.print("AP-HTTP: Request hex: ");
    for (size_t i = 0; i < wapRequestLen && i < 80; i++) {
      Serial.printf("%02X ", http_wapRequest[i]);
    }
    Serial.println();
  } else {
    // For now, only support GET - send error for other methods
    // Most WAP pages do not use other verbs anyway, we shoud revisit this later
    // This will also kill most UC Broweser/Opera Mini proxy protocols
    client.println("HTTP/1.1 501 Not Implemented");
    client.println("Content-Type: text/plain");
    client.println("Connection: close");
    client.println();
    client.printf("Method %s not implemented for WAP proxy\n", http_req.method);
    return;
  }
  
  if (wapRequestLen == 0) {
    client.println("HTTP/1.1 500 Internal Server Error");
    client.println("Content-Type: text/plain");
    client.println("Connection: close");
    client.println();
    client.println("Failed to create WAP request");
    return;
  }
  
  // Send WAP request via mesh and receive response (using static buffer)
  // Pass client reference to keep connection alive during mesh wait
  size_t wapResponseLen = 0;
  
  if (!sendWAPRequestViaMesh(http_wapRequest, wapRequestLen, http_wapResponse, &wapResponseLen, sizeof(http_wapResponse), 15000, &client)) {
    client.println("HTTP/1.1 504 Gateway Timeout");
    client.println("Content-Type: text/plain");
    client.println("Connection: close");
    client.println();
    client.println("Mesh proxy did not respond");
    return;
  }
  
  // Check if headers were already sent early (when first packet arrived)
  if (ap_headersSent) {
    // Headers already sent - just need to send remaining body
    Serial.println("HTTP: Headers already sent early, sending body now");
    
    // For WMLC, we need to decompile the complete body
    if (ap_isWMLC && ap_meshResponseLen > 0) {
      // Get body from the complete response buffer
      HTTPResponse wapResp;
      if (WAPResponse::decode(http_wapResponse, wapResponseLen, &wapResp) && 
          wapResp.body != nullptr && wapResp.bodyLen > 0) {
        size_t decompiledLen = WMLCDecompiler::decompile(wapResp.body, wapResp.bodyLen, 
                                                          http_decompiled, sizeof(http_decompiled));
        if (decompiledLen > 0) {
          Serial.printf("HTTP: Decompiled %zu bytes WMLC to %zu bytes WML\n", 
                        wapResp.bodyLen, decompiledLen);
          client.write((const uint8_t*)http_decompiled, decompiledLen);
        } else {
          // Decompilation failed, send raw
          client.write(wapResp.body, wapResp.bodyLen);
        }
      }
    } else {
      // Non-WMLC: body was already streamed as packets arrived
      // Nothing more to send
    }
    
    ap_headersSent = false;
    Serial.printf("HTTP: Response complete (headers sent early)\n");
  } else {
    // Normal path - headers not sent yet, decode and send everything
    HTTPResponse wapResp;
    if (!WAPResponse::decode(http_wapResponse, wapResponseLen, &wapResp)) {
      client.println("HTTP/1.1 502 Bad Gateway");
      client.println("Content-Type: text/plain");
      client.println("Connection: close");
      client.println();
      client.println("Failed to decode WAPBOX response");
      return;
    }
    
    Serial.printf("HTTP: WAP response status=%d type=%s bodyLen=%zu\n", 
                  wapResp.statusCode, wapResp.contentType, wapResp.bodyLen);
    
    // Check if response is WMLC and needs decompilation
    bool isWMLC = (strstr(wapResp.contentType, "wmlc") != nullptr);
    
    size_t decompiledLen = 0;
    const uint8_t* responseBody = wapResp.body;
    size_t responseBodyLen = wapResp.bodyLen;
    const char* responseContentType = wapResp.contentType;
    
    if (isWMLC && wapResp.body != nullptr && wapResp.bodyLen > 0) {
      // Decompile WMLC to WML (using static buffer)
      decompiledLen = WMLCDecompiler::decompile(wapResp.body, wapResp.bodyLen, 
                                                 http_decompiled, sizeof(http_decompiled));
      if (decompiledLen > 0) {
        Serial.printf("HTTP: Decompiled %zu bytes WMLC to %zu bytes WML\n", 
                      wapResp.bodyLen, decompiledLen);
        responseBody = (const uint8_t*)http_decompiled;
        responseBodyLen = decompiledLen;
        responseContentType = "text/vnd.wap.wml; charset=utf-8";
      } else {
        Serial.println("HTTP: WMLC decompilation failed, sending raw");
      }
    }
    
    // Send HTTP response to client
    client.printf("HTTP/1.1 %d %s\r\n", wapResp.statusCode, wapResp.statusText);
    client.printf("Content-Type: %s\r\n", responseContentType);
    client.printf("Content-Length: %zu\r\n", responseBodyLen);
    client.println("Connection: close");
    
    // Add original server header if present
    if (strlen(wapResp.server) > 0) {
      client.printf("Server: %s\r\n", wapResp.server);
    }
    
    client.println();  // End of headers
    
    // Send body
    if (responseBody != nullptr && responseBodyLen > 0) {
      client.write(responseBody, responseBodyLen);
    }
    
    Serial.printf("HTTP: Sent response %d with %zu bytes\n", wapResp.statusCode, responseBodyLen);
  }
  
  // Restore normal display after HTTP response is complete
  ap_restoreNormalDisplay();
}

void ap_init() {
  Serial.println("DEBUG: Initializing AP Mode with Mesh Gateway...");
  displayStatus("AP Mode", "Initializing...", nullptr, nullptr);
  
  // Initialize concat message slots
  for (int i = 0; i < AP_MAX_CONCAT_MESSAGES; i++) {
    ap_concatMessages[i].active = false;
  }

  // Configure AP mode only
  WiFi.mode(WIFI_AP);
  
  // Set up the access point
  bool success = WiFi.softAP(AP_SSID, nullptr, AP_CHANNEL, false, AP_MAX_CONNECTIONS);
  
  if (success) {
    ap_initialized = true;
    
    IPAddress apIP = WiFi.softAPIP();
    
    // Start DNS server - responds to ALL A record queries with the AP's IP
    // This will forward all traffic to our AP for sending over the mesh
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(DNS_PORT, "*", apIP);  // "*" matches all domains
    
    // Start HTTP server for WAP proxy
    httpServer.begin();
    Serial.printf("DEBUG: HTTP server started on port %d\n", HTTP_PORT);
    
    Serial.println("DEBUG: AP Mode with Mesh Gateway started successfully!");
    Serial.printf("DEBUG: AP SSID: %s\n", AP_SSID);
    Serial.printf("DEBUG: AP IP: %s\n", apIP.toString().c_str());
    Serial.printf("DEBUG: Channel: %d\n", AP_CHANNEL);
    Serial.printf("DEBUG: DNS Server started on port %d (all queries -> %s)\n", DNS_PORT, apIP.toString().c_str());
    Serial.printf("DEBUG: HTTP/WAP Proxy on port %d via Mesh -> %s\n", HTTP_PORT, PROXY_NODE_PUBKEY);
    
    char ipLine[32];
    snprintf(ipLine, sizeof(ipLine), "AP:%s", apIP.toString().c_str());
    char proxyLine[32];
    snprintf(proxyLine, sizeof(proxyLine), "Proxy:%s", PROXY_NODE_PUBKEY);
    displayStatus("AP Active", AP_SSID, ipLine, proxyLine);
  } else {
    Serial.println("DEBUG: AP Mode FAILED to start!");
    displayStatus("AP FAILED!", "Could not start", "Access Point", nullptr);
  }
}

void ap_loop() {
  if (!ap_initialized) return;
  
  // Process DNS requests
  dnsServer.processNextRequest();
  
  // Handle HTTP clients (server is stopped during mesh requests, so no new clients while busy)
  WiFiClient client = httpServer.available();
  if (client) {
    Serial.println("HTTP: New client connected");
    
    // Wait for client to send data
    unsigned long timeout = millis() + 3000;
    while (client.connected() && !client.available() && millis() < timeout) {
      delay(1);
    }
    
    if (client.available()) {
      handleHTTPRequest(client);
    }
    
    // Give client time to receive data
    delay(10);
    client.stop();
    Serial.println("HTTP: Client disconnected");
  }
  
  // Check for client count changes
  int currentClients = WiFi.softAPgetStationNum();
  if (currentClients != ap_connected_clients) {
    ap_connected_clients = currentClients;
    ap_displayNeedsUpdate = true;  // Signal main loop to update display
    Serial.printf("DEBUG: AP clients changed: %d connected\n", ap_connected_clients);
  }
  
  // Cleanup expired concat messages
  unsigned long now = millis();
  for (int i = 0; i < AP_MAX_CONCAT_MESSAGES; i++) {
    if (ap_concatMessages[i].active && (now - ap_concatMessages[i].lastUpdate > 30000)) {
      Serial.printf("AP-WDP: Concat message %d timed out\n", ap_concatMessages[i].refNum);
      ap_clearConcatMessage(&ap_concatMessages[i]);
    }
  }
}

bool ap_isInitialized() {
  return ap_initialized;
}

int ap_getClientCount() {
  return ap_connected_clients;
}

// Check if WDP session is active (display should not be updated by main loop)
bool ap_isWdpSessionActive() {
  return ap_wdpSessionActive;
}

// Check if display needs update (clears the flag)
bool ap_needsDisplayUpdate() {
  if (ap_displayNeedsUpdate) {
    ap_displayNeedsUpdate = false;
    return true;
  }
  return false;
}

IPAddress ap_getIP() {
  return WiFi.softAPIP();
}

// Set the mesh send callback - must be called before AP mode can send requests
void ap_setMeshCallback(std::function<void(const String&, const uint8_t*, size_t)> callback) {
  ap_sendMeshCallback = callback;
  Serial.println("AP: Mesh send callback configured");
}

// Set the mesh loop callback - MUST be called to keep mesh alive during blocking HTTP waits
void ap_setMeshLoopCallback(std::function<void()> callback) {
  ap_meshLoopCallback = callback;
  Serial.println("AP: Mesh loop callback configured");
}

// Get proxy path discovery status
bool ap_isProxyPathDiscovered() {
  return ap_proxy_path_discovered;
}

int ap_getProxyPathLen() {
  return ap_proxy_path_len;
}

// Set proxy path discovered (called from main when path is updated)
void ap_setProxyPathDiscovered(int pathLen) {
  ap_proxy_path_discovered = true;
  ap_proxy_path_len = pathLen;
  ap_proxy_discovery_in_progress = false;
  Serial.printf("AP: Proxy path discovered, path_len=%d\n", pathLen);
}

// Check if proxy discovery is in progress
bool ap_isProxyDiscoveryInProgress() {
  return ap_proxy_discovery_in_progress;
}

// Start proxy discovery process
void ap_startProxyDiscovery() {
  ap_proxy_discovery_in_progress = true;
  ap_proxy_discovery_attempts = 0;
  ap_proxy_path_discovered = false;
  ap_proxy_path_len = -1;
  ap_proxy_discovery_start_time = millis();
  Serial.println("AP: Starting proxy path discovery...");
}

// Get current discovery attempt number
int ap_getProxyDiscoveryAttempt() {
  return ap_proxy_discovery_attempts;
}

// Increment discovery attempt and check if should continue
bool ap_incrementProxyDiscoveryAttempt() {
  ap_proxy_discovery_attempts++;
  if (ap_proxy_discovery_attempts > AP_PROXY_DISCOVERY_MAX_RETRIES) {
    ap_proxy_discovery_in_progress = false;
    Serial.println("AP: Proxy discovery failed after max retries");
    return false;
  }
  return true;
}

/**
 * Try to decode and send HTTP headers early from the first packet
 * Returns true if headers were successfully sent
 */
bool ap_trySendEarlyHeaders(const uint8_t* wspData, size_t wspLen) {
  if (!ap_waitingClient || ap_headersSent) {
    return false;  // No client waiting or headers already sent
  }
  
  // Try to decode WSP headers from first packet
  if (!WAPResponse::decode(wspData, wspLen, &ap_earlyResponse)) {
    Serial.println("AP-WDP: Could not decode headers from first packet");
    return false;
  }
  
  // Check if this is WMLC that needs decompilation
  ap_isWMLC = (strstr(ap_earlyResponse.contentType, "wmlc") != nullptr);
  
  // Determine content type to send
  const char* responseContentType = ap_earlyResponse.contentType;
  if (ap_isWMLC) {
    // For WMLC, we'll decompile later so advertise WML type
    // But we can't know the final size, so use chunked or don't send Content-Length yet
    // Actually, we need to buffer WMLC for decompilation, so don't stream body
    responseContentType = "text/vnd.wap.wml; charset=utf-8";
  }
  
  Serial.printf("AP-WDP: Sending early headers - status=%d type=%s\n", 
                ap_earlyResponse.statusCode, responseContentType);
  
  // Send HTTP headers immediately
  ap_waitingClient->printf("HTTP/1.1 %d %s\r\n", ap_earlyResponse.statusCode, ap_earlyResponse.statusText);
  ap_waitingClient->printf("Content-Type: %s\r\n", responseContentType);
  
  // For non-WMLC, we can stream the body as it arrives
  // For WMLC, we need to buffer and decompile, so omit Content-Length for now
  // (we'll use Connection: close to signal end)
  if (!ap_isWMLC && ap_earlyResponse.contentLength > 0) {
    ap_waitingClient->printf("Content-Length: %zu\r\n", ap_earlyResponse.contentLength);
  }
  
  ap_waitingClient->println("Connection: close");
  
  // Add original server header if present
  if (strlen(ap_earlyResponse.server) > 0) {
    ap_waitingClient->printf("Server: %s\r\n", ap_earlyResponse.server);
  }
  
  ap_waitingClient->println();  // End of headers
  ap_waitingClient->flush();
  
  // If not WMLC and we have body data in this first packet, send it now
  if (!ap_isWMLC && ap_earlyResponse.body != nullptr && ap_earlyResponse.bodyLen > 0) {
    ap_waitingClient->write(ap_earlyResponse.body, ap_earlyResponse.bodyLen);
    ap_waitingClient->flush();
    ap_bodyBytesReceived = ap_earlyResponse.bodyLen;
    Serial.printf("AP-WDP: Sent %zu body bytes from first packet\n", ap_earlyResponse.bodyLen);
  }
  
  ap_headersSent = true;
  return true;
}

/**
 * Handle incoming mesh message (response from proxy node)
 * This handles both simple and concatenated messages
 */
void ap_handleIncomingMesh(const String& from, const uint8_t* data, size_t len) {
  Serial.printf("AP-WDP: Received %d bytes from %s\n", len, from.c_str());
  
  if (len < 7) {
    Serial.println("AP-WDP: Message too short for UDH");
    return;
  }
  
  AP_UDH udh;
  const uint8_t* payload;
  size_t payloadLen;
  
  // Check if this is a concatenated message
  uint8_t refNum, totalParts, currentPart;
  if (ap_parseConcatUDH(data, len, refNum, totalParts, currentPart, udh)) {
    Serial.printf("AP-WDP: Concatenated message part %d/%d (ref: %d)\n", currentPart, totalParts, refNum);
    
    // Find or create concat message entry
    AP_ConcatMessage* concat = nullptr;
    for (int i = 0; i < AP_MAX_CONCAT_MESSAGES; i++) {
      if (ap_concatMessages[i].active && 
          ap_concatMessages[i].refNum == refNum &&
          ap_concatMessages[i].senderMeshId == from) {
        concat = &ap_concatMessages[i];
        break;
      }
    }
    
    if (!concat) {
      // Create new concat entry
      for (int i = 0; i < AP_MAX_CONCAT_MESSAGES; i++) {
        if (!ap_concatMessages[i].active) {
          concat = &ap_concatMessages[i];
          concat->active = true;
          concat->refNum = refNum;
          concat->totalParts = totalParts;
          concat->receivedParts = 0;
          concat->sourcePort = udh.source;
          concat->destPort = udh.dest;
          concat->senderMeshId = from;
          concat->lastUpdate = millis();
          memset(concat->partReceived, 0, sizeof(concat->partReceived));
          memset(concat->data, 0, sizeof(concat->data));
          memset(concat->partSizes, 0, sizeof(concat->partSizes));
          // Update display with total parts info
          ap_wdpTotalParts = totalParts;
          ap_wdpReceivedParts = 0;
          ap_updateWDPDisplay();
          break;
        }
      }
    }
    
    if (!concat) {
      Serial.println("AP-WDP: No free concat message slots");
      return;
    }
    
    // Store this part
    if (currentPart > 0 && currentPart <= 16 && !(concat->partReceived[currentPart - 1])) {
      size_t partPayloadLen = len - 12;  // Subtract concat UDH size
      size_t offset = (currentPart - 1) * (MESHCORE_MAX_BINARY_PAYLOAD - 12);
      
      if (offset + partPayloadLen < sizeof(concat->data)) {
        memcpy(&concat->data[offset], &data[12], partPayloadLen);
        concat->partSizes[currentPart - 1] = partPayloadLen;
        concat->partReceived[currentPart - 1] = 1;
        concat->receivedParts++;
        concat->lastUpdate = millis();
        // Reset timeout - we're still receiving parts
        ap_lastPartReceivedTime = millis();
        // Update display with receive progress
        ap_wdpReceivedParts = concat->receivedParts;
        ap_updateWDPDisplay();
        
        // On first part, try to decode and send headers early
        // this will stop browsers from timing out
        // since WML headers will almost always fit in first part this is a perfect optimization
        if (currentPart == 1 && !ap_headersSent && ap_waitingClient) {
          // Verify port match first
          if (ap_currentRequestPort == 0 || concat->destPort == ap_currentRequestPort) {
            ap_trySendEarlyHeaders(&data[12], partPayloadLen);
          }
        } else if (!ap_isWMLC && ap_headersSent && ap_waitingClient && ap_waitingClient->connected()) {
          // For non-WMLC responses, stream body data as it arrives
          // Skip the WSP header bytes (they're in the first packet)
          if (currentPart > 1) {
            ap_waitingClient->write(&data[12], partPayloadLen);
            ap_waitingClient->flush();
            ap_bodyBytesReceived += partPayloadLen;
            Serial.printf("AP-WDP: Streamed %zu body bytes (part %d)\n", partPayloadLen, currentPart);
          }
        }
      }
    }
    
    // Check if complete
    if (concat->receivedParts == concat->totalParts) {
      Serial.printf("AP-WDP: Concat message complete\n");
      
      // Verify this response matches our pending request by destination port
      if (ap_currentRequestPort != 0 && concat->destPort != ap_currentRequestPort) {
        Serial.printf("AP-WDP: Concat port mismatch - expected %d, got %d\n", ap_currentRequestPort, concat->destPort);
        ap_clearConcatMessage(concat);
        return;
      }
      
      // Calculate total size
      size_t totalSize = 0;
      for (int i = 0; i < concat->totalParts; i++) {
        totalSize += concat->partSizes[i];
      }
      
      // Copy to response buffer
      if (totalSize < sizeof(ap_meshResponseBuffer)) {
        memcpy(ap_meshResponseBuffer, concat->data, totalSize);
        ap_meshResponseLen = totalSize;
        ap_meshResponseReady = true;
        ap_currentRequestPort = 0;  // Clear port after receiving response
        Serial.printf("AP-WDP: Response ready (%zu bytes)\n", totalSize);
      }
      
      ap_clearConcatMessage(concat);
    }
    return;
  }
  
  // Simple (non-concatenated) message
  if (!ap_parseUDH(data, len, udh)) {
    return;
  }
  
  // Verify this response matches our pending request by destination port
  if (ap_currentRequestPort != 0 && udh.dest != ap_currentRequestPort) {
    Serial.printf("AP-WDP: Port mismatch - expected %d, got %d\n", ap_currentRequestPort, udh.dest);
    return;
  }
  
  payload = data + 7;  // Skip UDH
  payloadLen = len - 7;
  
  // For simple messages, try to send headers early too
  if (!ap_headersSent && ap_waitingClient) {
    ap_trySendEarlyHeaders(payload, payloadLen);
  }
  
  // Copy to response buffer
  if (payloadLen < sizeof(ap_meshResponseBuffer)) {
    memcpy(ap_meshResponseBuffer, payload, payloadLen);
    ap_meshResponseLen = payloadLen;
    ap_meshResponseReady = true;
    ap_currentRequestPort = 0;  // Clear port after receiving response
    // Update display to show single-part response received
    ap_wdpTotalParts = 1;
    ap_wdpReceivedParts = 1;
    ap_updateWDPDisplay();
    Serial.printf("AP-WDP: Simple response ready (%zu bytes)\n", payloadLen);
  }
}

#endif // ESP32

#endif // MODE_AP_H
