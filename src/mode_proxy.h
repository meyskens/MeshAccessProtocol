#ifndef MODE_PROXY_H
#define MODE_PROXY_H

#ifdef ESP32

#include <WiFi.h>
#include <WiFiUdp.h>
#include <functional>

// Default values if not defined in main
#ifndef MESHCORE_MAX_BINARY_PAYLOAD
  #define MESHCORE_MAX_BINARY_PAYLOAD 120  // Max binary bytes after Base91 encoding
#endif

// Forward declaration - defined in main.cpp
extern void displayStatus(const char* line1, const char* line2, const char* line3, const char* line4);

// UDH (User Data Header) structure for WAP over bearer
struct UDH {
  uint8_t headerLen;
  uint8_t ei;         // Element Identifier (0x05 = Application Port Addressing 16-bit)
  uint8_t eiLength;   // Length of element data (0x04 for 16-bit ports)
  uint16_t source;    // Source port
  uint16_t dest;      // Destination port
};

// Concatenated message tracking for reassembly
struct ConcatMessage {
  bool active;
  uint8_t refNum;           // Reference number
  uint8_t totalParts;
  uint8_t receivedParts;
  uint8_t partReceived[16]; // Bitmask for which parts received (max 16 parts)
  uint8_t data[2048];       // Reassembled data buffer
  uint16_t partSizes[16];   // Size of each part
  uint16_t sourcePort;
  uint16_t destPort;
  String senderMeshId;
  unsigned long lastUpdate;
};

class WDPGateway {
private:
  String wapBoxHost;
  uint16_t wapBoxPort;
  
  // Pending UDP connections for response routing (keyed by source port)
  static const int MAX_PENDING_CONNECTIONS = 8;
  struct PendingConnection {
    bool active;
    uint16_t clientSourcePort;  // Source port from the mesh client (used for response routing)
    uint16_t wapboxPort;        // WAPBOX port we sent to
    String meshRecipient;
    unsigned long timestamp;
    WiFiUDP udpSocket;          // Per-connection UDP socket bound to clientSourcePort
  };
  PendingConnection pendingConnections[MAX_PENDING_CONNECTIONS];
  
  // Clear/reset a pending connection slot
  void clearPendingConnection(PendingConnection* conn) {
    conn->udpSocket.stop();
    conn->udpSocket = WiFiUDP(); // Reinitialize
    conn->active = false;
    conn->clientSourcePort = 0;
    conn->wapboxPort = 0;
    conn->meshRecipient = "";
    conn->timestamp = 0;
  }
  
  // Concatenated message reassembly
  static const int MAX_CONCAT_MESSAGES = 4;
  ConcatMessage concatMessages[MAX_CONCAT_MESSAGES];
  
  // Clear/reset a concat message slot
  void clearConcatMessage(ConcatMessage* msg) {
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
  
  // Callback for sending MeshCore messages
  std::function<void(const String&, const uint8_t*, size_t)> sendMeshCallback;

public:
  WDPGateway(const char* host, uint16_t port) : wapBoxHost(host), wapBoxPort(port) {
    for (int i = 0; i < MAX_PENDING_CONNECTIONS; i++) {
      pendingConnections[i].active = false;
    }
    for (int i = 0; i < MAX_CONCAT_MESSAGES; i++) {
      concatMessages[i].active = false;
    }
  }
  
  void begin(std::function<void(const String&, const uint8_t*, size_t)> callback) {
    sendMeshCallback = callback;
    Serial.println("WDP Gateway initialized (per-connection UDP sockets)");
  }
  
  // Parse UDH from incoming MeshCore message
  bool parseUDH(const uint8_t* data, size_t len, UDH& udh) {
    if (len < 7) {
      Serial.println("WDP: Invalid UDH - too short");
      return false;
    }
    
    udh.headerLen = data[0];
    udh.ei = data[1];
    udh.eiLength = data[2];
    udh.dest = (data[3] << 8) | data[4];    // Destination port first
    udh.source = (data[5] << 8) | data[6];  // Source port second
    
    // Validate: EI should be 0x05 (Application Port Addressing, 16-bit)
    if (udh.ei != 0x05) {
      Serial.printf("WDP: Unexpected EI: 0x%02X\n", udh.ei);
      // Still allow processing - might be concatenated message
    }
    
    return true;
  }
  
  // Parse concatenated message UDH (0x0B header length indicates concat)
  bool parseConcatUDH(const uint8_t* data, size_t len, uint8_t& refNum, uint8_t& totalParts, 
                      uint8_t& currentPart, UDH& udh) {
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
  
  // Handle incoming MeshCore message containing WDP data
  void handleIncomingMesh(const String& from, const uint8_t* data, size_t len) {
    Serial.printf("WDP: Received %d bytes from %s\n", len, from.c_str());
    
    // Display status: package received
    char fromLine[32];
    snprintf(fromLine, sizeof(fromLine), "From: %.20s", from.c_str());
    char sizeLine[32];
    snprintf(sizeLine, sizeof(sizeLine), "Size: %d bytes", (int)len);
    displayStatus("WDP Received", fromLine, sizeLine, "Processing...");
    
    if (len < 7) {
      Serial.println("WDP: Message too short for UDH");
      displayStatus("WDP too short", fromLine, (const char*)data, "Ignoring...");
      return;
    }
    
    UDH udh;
    const uint8_t* payload;
    size_t payloadLen;
    
    // Check if this is a concatenated message
    uint8_t refNum, totalParts, currentPart;
    if (parseConcatUDH(data, len, refNum, totalParts, currentPart, udh)) {
      Serial.printf("WDP: Concatenated message part %d/%d (ref: %d)\n", currentPart, totalParts, refNum);
      
      // Display status: multi-part message receiving
      char partLine[32];
      snprintf(partLine, sizeof(partLine), "Part %d/%d (%dB)", currentPart, totalParts, (int)(len - 12));
      displayStatus("WDP Multi-Recv", fromLine, partLine, sizeLine);
      
      // Find or create concat message entry
      ConcatMessage* concat = nullptr;
      for (int i = 0; i < MAX_CONCAT_MESSAGES; i++) {
        if (concatMessages[i].active && 
            concatMessages[i].refNum == refNum &&
            concatMessages[i].senderMeshId == from) {
          concat = &concatMessages[i];
          break;
        }
      }
      
      if (!concat) {
        // Create new concat entry
        for (int i = 0; i < MAX_CONCAT_MESSAGES; i++) {
          if (!concatMessages[i].active) {
            concat = &concatMessages[i];
            concat->active = true;
            concat->refNum = refNum;
            concat->totalParts = totalParts;
            concat->receivedParts = 0;
            concat->sourcePort = udh.source;
            concat->destPort = udh.dest;
            concat->senderMeshId = from;
            memset(concat->partReceived, 0, sizeof(concat->partReceived));
            memset(concat->data, 0, sizeof(concat->data));
            break;
          }
        }
      }
      
      if (!concat) {
        Serial.println("WDP: No free concat message slots");
        return;
      }
      
      // Store this part (using hex-decoded binary payload size)
      if (currentPart > 0 && currentPart <= 16 && !(concat->partReceived[currentPart - 1])) {
        size_t partPayloadLen = len - 12;  // Subtract concat UDH size
        size_t offset = (currentPart - 1) * (MESHCORE_MAX_BINARY_PAYLOAD - 12);
        
        if (offset + partPayloadLen < sizeof(concat->data)) {
          memcpy(&concat->data[offset], &data[12], partPayloadLen);
          concat->partSizes[currentPart - 1] = partPayloadLen;
          concat->partReceived[currentPart - 1] = 1;
          concat->receivedParts++;
          concat->lastUpdate = millis();
        }
      }
      
      // Check if complete
      if (concat->receivedParts == concat->totalParts) {
        Serial.printf("WDP: Concat message complete, forwarding to UDP\n");
        
        // Calculate total size
        size_t totalSize = 0;
        for (int i = 0; i < concat->totalParts; i++) {
          totalSize += concat->partSizes[i];
        }
        
        char completeLine[32];
        snprintf(completeLine, sizeof(completeLine), "Complete: %dB", (int)totalSize);
        char partsInfo[32];
        snprintf(partsInfo, sizeof(partsInfo), "%d parts received", concat->totalParts);
        displayStatus("WDP Multi-Recv", fromLine, completeLine, partsInfo);
        
        forwardToWAPBox(concat->senderMeshId, concat->sourcePort, concat->destPort, 
                        concat->data, totalSize);
        clearConcatMessage(concat);
      }
      return;
    }
    
    // Simple (non-concatenated) message
    if (!parseUDH(data, len, udh)) {
      return;
    }
    
    payload = data + 7;  // Skip UDH
    payloadLen = len - 7;
    
    displayStatus("WDP Received", fromLine, sizeLine, "Forwarding...");
    
    forwardToWAPBox(from, udh.source, udh.dest, payload, payloadLen);
  }
  
  // Forward WDP payload to WAPBox via UDP
  void forwardToWAPBox(const String& from, uint16_t srcPort, uint16_t dstPort, 
                       const uint8_t* payload, size_t len) {
    Serial.printf("WDP: Forwarding %d bytes to %s:%d (client src port: %d)\n", 
                  len, wapBoxHost.c_str(), dstPort, srcPort);
    
    // Check for duplicate request (same sender + source port) - prevents mesh retransmit flooding
    for (int i = 0; i < MAX_PENDING_CONNECTIONS; i++) {
      if (pendingConnections[i].active && 
          pendingConnections[i].clientSourcePort == srcPort &&
          pendingConnections[i].meshRecipient == from) {
        Serial.printf("WDP: Ignoring duplicate request from %s (port %d already pending in slot %d)\n", 
                      from.c_str(), srcPort, i);
        // Update timestamp to extend timeout for active transaction
        pendingConnections[i].timestamp = millis();
        return;
      }
    }
    
    // Store pending connection for response routing (keyed by client source port)
    int slot = -1;
    for (int i = 0; i < MAX_PENDING_CONNECTIONS; i++) {
      if (!pendingConnections[i].active) {
        slot = i;
        break;
      }
      // Reuse oldest expired connection (>60s)
      if (millis() - pendingConnections[i].timestamp > 60000) {
        slot = i;
        break;
      }
    }
    
    if (slot >= 0) {
      pendingConnections[slot].active = true;
      pendingConnections[slot].clientSourcePort = srcPort;
      pendingConnections[slot].wapboxPort = dstPort;
      pendingConnections[slot].meshRecipient = from;
      pendingConnections[slot].timestamp = millis();
      
      // Bind UDP socket to clientSourcePort so responses come back to this port
      pendingConnections[slot].udpSocket.stop();  // Ensure clean state
      if (!pendingConnections[slot].udpSocket.begin(srcPort)) {
        Serial.printf("WDP: WARNING - Failed to bind UDP socket to port %d\n", srcPort);
      }
      
      // Count how many pending connections exist for this WAPBOX port
      int pendingCount = 0;
      for (int i = 0; i < MAX_PENDING_CONNECTIONS; i++) {
        if (pendingConnections[i].active && pendingConnections[i].wapboxPort == dstPort) {
          pendingCount++;
        }
      }
      Serial.printf("WDP: Stored pending connection in slot %d (client port: %d -> mesh: %s, %d pending for port %d)\n", 
                    slot, srcPort, from.c_str(), pendingCount, dstPort);
      
      // Send UDP packet to WAPBox from clientSourcePort
      IPAddress wapIP;
      if (wapIP.fromString(wapBoxHost)) {
        pendingConnections[slot].udpSocket.beginPacket(wapIP, dstPort);
        pendingConnections[slot].udpSocket.write(payload, len);
        pendingConnections[slot].udpSocket.endPacket();
        Serial.printf("WDP: Sent UDP packet to %s:%d from source port %d\n", wapBoxHost.c_str(), dstPort, srcPort);
      } else {
        Serial.printf("WDP: Invalid WAPBox IP: %s\n", wapBoxHost.c_str());
      }
    } else {
      Serial.println("WDP: WARNING - No free slots for pending connection!");
    }
  }
  
  // Generate UDH and fragment data for MeshCore transmission
  // Note: Data will be Base91-encoded when sent, limiting binary payload to 120 bytes
  void sendWDPViaMesh(const String& to, uint16_t srcPort, uint16_t dstPort, 
                      const uint8_t* data, size_t len) {
    const size_t maxPayloadSimple = MESHCORE_MAX_BINARY_PAYLOAD - 7;   // Simple UDH is 7 bytes = 114 bytes payload
    const size_t maxPayloadConcat = MESHCORE_MAX_BINARY_PAYLOAD - 12;  // Concat UDH is 12 bytes = 109 bytes payload
    
    // Display status: sending reply
    char toLine[32];
    snprintf(toLine, sizeof(toLine), "To: %.20s", to.c_str());
    
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
      
      char sizeLine[32];
      snprintf(sizeLine, sizeof(sizeLine), "Size: %d bytes", (int)(7 + len));
      displayStatus("WDP Sending", toLine, sizeLine, "Single packet");
      
      Serial.printf("WDP: Sending simple message (%d bytes) to %s\n", 7 + len, to.c_str());
      if (sendMeshCallback) {
        sendMeshCallback(to, msg, 7 + len);
      }
      
      displayStatus("WDP Sent", toLine, sizeLine, "Complete!");
    } else {
      // Concatenated message (fragmentation needed)
      int totalParts = (len + maxPayloadConcat - 1) / maxPayloadConcat;
      uint8_t refNum = (millis() & 0xFF);  // Simple reference number
      
      char sizeLine[32];
      snprintf(sizeLine, sizeof(sizeLine), "Size: %d bytes", (int)len);
      char partsLine[32];
      snprintf(partsLine, sizeof(partsLine), "Parts: %d total", totalParts);
      displayStatus("WDP Multi-Send", toLine, sizeLine, partsLine);
      
      Serial.printf("WDP: Fragmenting %d bytes into %d parts\n", len, totalParts);
      
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
        
        // Update display with current part progress
        char progressLine[32];
        snprintf(progressLine, sizeof(progressLine), "Part %d/%d (%dB)", part, totalParts, (int)(12 + partLen));
        displayStatus("WDP Multi-Send", toLine, progressLine, sizeLine);
        
        Serial.printf("WDP: Sending part %d/%d (%d bytes)\n", part, totalParts, 12 + partLen);
        if (sendMeshCallback) {
          sendMeshCallback(to, msg, 12 + partLen);
        }
      }
      
      // Show completion status
      char doneMsg[32];
      snprintf(doneMsg, sizeof(doneMsg), "Sent %dB in %d parts", (int)len, totalParts);
      displayStatus("WDP Multi-Send", toLine, "Complete!", doneMsg);
    }
  }
  
  // Check for incoming UDP packets and cleanup expired concat messages
  void loop() {
    // Check each pending connection's UDP socket for responses
    for (int i = 0; i < MAX_PENDING_CONNECTIONS; i++) {
      if (!pendingConnections[i].active) continue;
      
      int packetSize = pendingConnections[i].udpSocket.parsePacket();
      if (packetSize > 0) {
        uint8_t buffer[1500];
        int len = pendingConnections[i].udpSocket.read(buffer, sizeof(buffer));
        
        if (len > 0) {
          IPAddress remoteIP = pendingConnections[i].udpSocket.remoteIP();
          uint16_t remotePort = pendingConnections[i].udpSocket.remotePort();
          
          Serial.printf("WDP: UDP response from %s:%d on local port %d (%d bytes)\n", 
                        remoteIP.toString().c_str(), remotePort, 
                        pendingConnections[i].clientSourcePort, len);
          
          // Log hex of received UDP reply
          Serial.print("WDP: UDP reply hex: ");
          for (int j = 0; j < len; j++) {
            Serial.printf("%02X ", buffer[j]);
          }
          Serial.println();
          
          // Response received on this connection's socket - we know exactly which client it's for
          String meshRecipient = pendingConnections[i].meshRecipient;
          uint16_t srcPort = remotePort;
          uint16_t dstPort = pendingConnections[i].clientSourcePort;
          
          Serial.printf("WDP: Matched pending connection slot %d (client port: %d, mesh: %s)\n",
                        i, dstPort, meshRecipient.c_str());
          
          // Display status: UDP response received from WAPBox
          char wapLine[32];
          snprintf(wapLine, sizeof(wapLine), "WAPBox: %d bytes", len);
          char recipLine[32];
          snprintf(recipLine, sizeof(recipLine), "To: %.20s", meshRecipient.c_str());
          displayStatus("WDP Response", wapLine, recipLine, "Relaying...");
          
          // Generate WDP messages and send via MeshCore
          sendWDPViaMesh(meshRecipient, srcPort, dstPort, buffer, len);
          
          // Deactivate connection after sending response
          clearPendingConnection(&pendingConnections[i]);
        }
      }
    }
    
    // Cleanup expired pending connections (>60s)
    unsigned long now = millis();
    for (int i = 0; i < MAX_PENDING_CONNECTIONS; i++) {
      if (pendingConnections[i].active && (now - pendingConnections[i].timestamp > 60000)) {
        Serial.printf("WDP: Pending connection slot %d timed out (client port: %d)\n", 
                      i, pendingConnections[i].clientSourcePort);
        clearPendingConnection(&pendingConnections[i]);
      }
    }
    
    // Cleanup expired concat messages
    for (int i = 0; i < MAX_CONCAT_MESSAGES; i++) {
      if (concatMessages[i].active && (now - concatMessages[i].lastUpdate > 30000)) {
        Serial.printf("WDP: Concat message %d timed out\n", concatMessages[i].refNum);
        clearConcatMessage(&concatMessages[i]);
      }
    }
  }
};

// Global WDP Gateway instance
WDPGateway* wdpGateway = nullptr;

void proxy_init(const char* host, uint16_t port) {
  wdpGateway = new WDPGateway(host, port);
}

void proxy_begin(std::function<void(const String&, const uint8_t*, size_t)> callback) {
  if (wdpGateway) {
    wdpGateway->begin(callback);
  }
}

void proxy_loop() {
  if (wdpGateway) {
    wdpGateway->loop();
  }
}

void proxy_handleIncomingMesh(const String& from, const uint8_t* data, size_t len) {
  if (wdpGateway) {
    wdpGateway->handleIncomingMesh(from, data, len);
  }
}

void proxy_connectToWiFi(const char* ssid, const char* password) {
  Serial.println("DEBUG: Starting WiFi connection (Proxy Mode)...");
  displayStatus("WiFi [Proxy]", "Connecting to:", ssid, nullptr);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  const int maxAttempts = 30;  // 15 seconds timeout
  
  while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
    delay(500);
    attempts++;
    
    // Update display with connection progress
    char statusLine[32];
    snprintf(statusLine, sizeof(statusLine), "Attempt %d/%d", attempts, maxAttempts);
    
    // Show animated dots
    char dots[8];
    int numDots = (attempts % 4) + 1;
    for (int i = 0; i < numDots; i++) dots[i] = '.';
    dots[numDots] = '\0';
    
    char connectingLine[32];
    snprintf(connectingLine, sizeof(connectingLine), "Connecting%s", dots);
    
    displayStatus("WiFi [Proxy]", connectingLine, ssid, statusLine);
    Serial.printf("DEBUG: WiFi attempt %d/%d\n", attempts, maxAttempts);
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("DEBUG: WiFi connected!");
    Serial.print("DEBUG: IP address: ");
    Serial.println(WiFi.localIP());
    
    char ipLine[32];
    snprintf(ipLine, sizeof(ipLine), "IP: %s", WiFi.localIP().toString().c_str());
    displayStatus("WiFi Connected!", ssid, ipLine, nullptr);
    delay(2000);  // Show success for 2 seconds
  } else {
    Serial.println("DEBUG: WiFi connection FAILED!");
    displayStatus("WiFi FAILED!", "Could not connect", ssid, "Continuing...");
    delay(2000);  // Show failure for 2 seconds
  }
}

bool proxy_isWiFiConnected() {
  return WiFi.status() == WL_CONNECTED;
}

#endif // ESP32

#endif // MODE_PROXY_H
