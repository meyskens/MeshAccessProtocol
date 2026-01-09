#include <Arduino.h>
#include <Mesh.h>

#if defined(NRF52_PLATFORM)
  #include <InternalFileSystem.h>
#elif defined(RP2040_PLATFORM)
  #include <LittleFS.h>
#elif defined(ESP32)
  #include <SPIFFS.h>
#endif

#include <helpers/ArduinoHelpers.h>
#include <helpers/StaticPoolPacketManager.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/IdentityStore.h>
#include <RTClib.h>
#include <target.h>

// OLED Display for Heltec V3
#include <U8g2lib.h>
#include <Wire.h>

#include "base91.h"

// WiFi and UDP for ESP32 (WDP Gateway)
#ifdef ESP32
  #include <WiFi.h>
  #include <WiFiUdp.h>
  #include "secrets.h"  // WiFi credentials (gitignored)
  
  // Operation Mode Configuration
  // MODE_PROXY (1): Connects to existing WiFi, forwards WDP packets to WAPBox
  // MODE_AP (2): Creates WiFi Access Point for direct device connections
  #define MODE_PROXY 1
  #define MODE_AP    2
  
  #ifndef OPERATION_MODE
    #define OPERATION_MODE MODE_AP
  #endif
  
  #include "mode_proxy.h"
  #include "mode_ap.h"
#endif

// Heltec V3 OLED pins: SDA=17, SCL=18, RST=21
// Heltec V3 LED pin: 35
#define LED_PIN 35
U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, /* reset=*/ 21, /* clock=*/ 18, /* data=*/ 17);

// User Button Configuration (GPIO0 on Heltec V3)
#ifndef PIN_USER_BTN
  #define PIN_USER_BTN 0
#endif
#define LONG_PRESS_DURATION_MS 2000  // 2 seconds for long press to trigger deep sleep

/* ---------------------------------- CONFIGURATION ------------------------------------- */

#define FIRMWARE_VER_TEXT   "MAPv1 (build: 10 Jan 2026)"

// WDP Gateway Configuration
#define WAPBOX_HOST         "206.83.40.166" // bevelgacom public WAP gateway
#define WAPBOX_PORT         9200            // Default WAP sessionless port
#define MESHCORE_MAX_BYTES  150             // MeshCore message limit in bytes
// With Base91 encoding: max binary = (MESHCORE_MAX_BYTES - 1) * 13 / 16 ≈ 121 bytes, 120 to be sure
#define MESHCORE_MAX_BINARY_PAYLOAD  120    // Max binary bytes per message (after Base91 encoding)

// EU868 Long Range Settings
#ifndef LORA_FREQ
  #define LORA_FREQ   869.617   // EU868 band
#endif
#ifndef LORA_BW
  #define LORA_BW     62.50       // 62.50  kHz bandwidth for narrow
#endif
#ifndef LORA_SF
  #define LORA_SF     8        // SF8 for maximum range
#endif
#ifndef LORA_CRa
  #define LORA_CR      8        // coding rate
#endif
#ifndef LORA_TX_POWER
  #define LORA_TX_POWER  22     // Max power for EU 869.617 MHz (up to 27 dBm allowed)
#endif

#ifndef MAX_CONTACTS
  #define MAX_CONTACTS         100
#endif

#include <helpers/BaseChatMesh.h>

#define SEND_TIMEOUT_BASE_MILLIS          500
#define FLOOD_SEND_TIMEOUT_FACTOR         16.0f
#define DIRECT_SEND_PERHOP_FACTOR         6.0f
#define DIRECT_SEND_PERHOP_EXTRA_MILLIS   250

#define  PUBLIC_GROUP_PSK  "izOH6cXN6mrJ5e26oRXNcg=="

/* --------------------------------End of configuration------------------------------------ */

// Believe it or not, this std C function is busted on some platforms!
static uint32_t _atoi(const char* sp) {
  uint32_t n = 0;
  while (*sp && *sp >= '0' && *sp <= '9') {
    n *= 10;
    n += (*sp++ - '0');
  }
  return n;
}


// Forward declaration of display function
void displayStatus(const char* line1, const char* line2 = nullptr, const char* line3 = nullptr, const char* line4 = nullptr);


struct NodePrefs {  // persisted to file
  float airtime_factor;
  char node_name[32];
  double node_lat, node_lon;
  float freq;
  uint8_t tx_power_dbm;
  uint8_t unused[3];
};

class MyMesh : public BaseChatMesh, ContactVisitor {
  FILESYSTEM* _fs;
  NodePrefs _prefs;
  uint32_t expected_ack_crc;
  ChannelDetails* _public;
  unsigned long last_msg_sent;
  ContactInfo* curr_recipient;
  char command[512+10];
  uint8_t tmp_buf[256];
  char hex_buf[512];

  // we store pending messages here to not lock up the threads of the MeshCore (ACK) logic
  static const int MAX_PENDING_INBOX = 16;
  struct PendingInbox {
    bool active;
    unsigned long time;
    char senderIdStr[20];    // pub_key prefix as hex string
    uint8_t wdpData[256];    // WDP binary data
    size_t wdpLen;
  };
  PendingInbox pending_inbox[MAX_PENDING_INBOX];

  // Clear/reset a pending inbox slot
  void clearPendingInbox(PendingInbox* msg) {
    msg->active = false;
    msg->time = 0;
    memset(msg->senderIdStr, 0, sizeof(msg->senderIdStr));
    memset(msg->wdpData, 0, sizeof(msg->wdpData));
    msg->wdpLen = 0;
  }

  static const int MAX_PENDING_REPLIES = 16;
  struct PendingReply {
    bool active;
    unsigned long time;
    uint8_t senderPubKey[PUB_KEY_SIZE];
    char replyText[256];
  };
  PendingReply pending_replies[MAX_PENDING_REPLIES];

  // Clear/reset a pending reply slot
  void clearPendingReply(PendingReply* msg) {
    msg->active = false;
    msg->time = 0;
    memset(msg->senderPubKey, 0, sizeof(msg->senderPubKey));
    memset(msg->replyText, 0, sizeof(msg->replyText));
  }

  // Message counter for display
  uint32_t messages_handled;

  const char* getTypeName(uint8_t type) const {
    if (type == ADV_TYPE_CHAT) return "Chat";
    if (type == ADV_TYPE_REPEATER) return "Repeater";
    if (type == ADV_TYPE_ROOM) return "Room";
    return "??";  // unknown
  }

  void loadContacts() {
    if (_fs->exists("/contacts")) {
    #if defined(RP2040_PLATFORM)
      File file = _fs->open("/contacts", "r");
    #else
      File file = _fs->open("/contacts");
    #endif
      if (file) {
        bool full = false;
        while (!full) {
          ContactInfo c;
          uint8_t pub_key[32];
          uint8_t unused;
          uint32_t reserved;

          bool success = (file.read(pub_key, 32) == 32);
          success = success && (file.read((uint8_t *) &c.name, 32) == 32);
          success = success && (file.read(&c.type, 1) == 1);
          success = success && (file.read(&c.flags, 1) == 1);
          success = success && (file.read(&unused, 1) == 1);
          success = success && (file.read((uint8_t *) &reserved, 4) == 4);
          success = success && (file.read((uint8_t *) &c.out_path_len, 1) == 1);
          success = success && (file.read((uint8_t *) &c.last_advert_timestamp, 4) == 4);
          success = success && (file.read(c.out_path, 64) == 64);
          c.gps_lat = c.gps_lon = 0;   // not yet supported

          if (!success) break;  // EOF

          c.id = mesh::Identity(pub_key);
          c.lastmod = 0;
          if (!addContact(c)) full = true;
        }
        file.close();
      }
    }
  }

  void saveContacts() {
#if defined(NRF52_PLATFORM)
    _fs->remove("/contacts");
    File file = _fs->open("/contacts", FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
    File file = _fs->open("/contacts", "w");
#else
    File file = _fs->open("/contacts", "w", true);
#endif
    if (file) {
      ContactsIterator iter;
      ContactInfo c;
      uint8_t unused = 0;
      uint32_t reserved = 0;

      while (iter.hasNext(this, c)) {
        bool success = (file.write(c.id.pub_key, 32) == 32);
        success = success && (file.write((uint8_t *) &c.name, 32) == 32);
        success = success && (file.write(&c.type, 1) == 1);
        success = success && (file.write(&c.flags, 1) == 1);
        success = success && (file.write(&unused, 1) == 1);
        success = success && (file.write((uint8_t *) &reserved, 4) == 4);
        success = success && (file.write((uint8_t *) &c.out_path_len, 1) == 1);
        success = success && (file.write((uint8_t *) &c.last_advert_timestamp, 4) == 4);
        success = success && (file.write(c.out_path, 64) == 64);

        if (!success) break;  // write failed
      }
      file.close();
    }
  }

  void setClock(uint32_t timestamp) {
    uint32_t curr = getRTCClock()->getCurrentTime();
    if (timestamp > curr) {
      getRTCClock()->setCurrentTime(timestamp);
      Serial.println("   (OK - clock set!)");
    } else {
      Serial.println("   (ERR: clock cannot go backwards)");
    }
  }

  void importCard(const char* command) {
    while (*command == ' ') command++;   // skip leading spaces
    if (memcmp(command, "meshcore://", 11) == 0) {
      command += 11;  // skip the prefix
      char *ep = strchr(command, 0);  // find end of string
      while (ep > command) {
        ep--;
        if (mesh::Utils::isHexChar(*ep)) break;  // found tail end of card
        *ep = 0;  // remove trailing spaces and other junk
      }
      int len = strlen(command);
      if (len % 2 == 0) {
        len >>= 1;  // halve, for num bytes
        if (mesh::Utils::fromHex(tmp_buf, len, command)) {
          importContact(tmp_buf, len);
          return;
        }
      }
    }
    Serial.println("   error: invalid format");
  }

protected:
  float getAirtimeBudgetFactor() const override {
    return _prefs.airtime_factor;
  }

  int calcRxDelay(float score, uint32_t air_time) const override {
    return 0;  // disable rxdelay
  }

  bool allowPacketForward(const mesh::Packet* packet) override {
    return true;
  }

  void onDiscoveredContact(ContactInfo& contact, bool is_new, uint8_t path_len, const uint8_t* path) override {
    Serial.printf("ADVERT from -> %s\n", contact.name);
    Serial.printf("  type: %s\n", getTypeName(contact.type));
    Serial.print("   public key: "); mesh::Utils::printHex(Serial, contact.id.pub_key, PUB_KEY_SIZE); Serial.println();

    saveContacts();
  }

  void onContactPathUpdated(const ContactInfo& contact) override {
    Serial.printf("PATH to: %s, path_len=%d\n", contact.name, (int32_t) contact.out_path_len);
    saveContacts();
  }

  ContactInfo* processAck(const uint8_t *data) override {
    if (memcmp(data, &expected_ack_crc, 4) == 0) {     // got an ACK from recipient
      Serial.printf("   Got ACK! (round trip: %d millis)\n", _ms->getMillis() - last_msg_sent);
      // NOTE: the same ACK can be received multiple times!
      expected_ack_crc = 0;  // reset our expected hash, now that we have received ACK
      return NULL;  // TODO: really should return ContactInfo pointer 
    }

    //uint32_t crc;
    //memcpy(&crc, data, 4);
    //MESH_DEBUG_PRINTLN("unknown ACK received: %08X (expected: %08X)", crc, expected_ack_crc);
    return NULL;
  }

  // Helper: Check if string is valid hex
  bool isValidHex(const char* str, size_t len) {
    for (size_t i = 0; i < len; i++) {
      char c = str[i];
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
        return false;
      }
    }
    return true;
  }

  // Helper: Hex char to nibble
  uint8_t hexToNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
  }

  // Helper: Decode hex string to bytes
  size_t hexDecode(const char* hex, size_t hexLen, uint8_t* out, size_t outMaxLen) {
    size_t byteLen = hexLen / 2;
    if (byteLen > outMaxLen) byteLen = outMaxLen;
    for (size_t i = 0; i < byteLen; i++) {
      out[i] = (hexToNibble(hex[i*2]) << 4) | hexToNibble(hex[i*2 + 1]);
    }
    return byteLen;
  }

  // Helper: Encode bytes to hex string
  void hexEncode(const uint8_t* data, size_t len, char* out) {
    static const char hexChars[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
      out[i*2] = hexChars[(data[i] >> 4) & 0x0F];
      out[i*2 + 1] = hexChars[data[i] & 0x0F];
    }
    out[len*2] = '\0';
  }

  // Helper: Validate sender node ID - checks if pub_key prefix matches a known contact
  // Returns true if sender is a known contact, false otherwise
  bool isValidSenderNodeId(const char* senderIdStr) {
    size_t idLen = strlen(senderIdStr);
    if (idLen < 8 || !isValidHex(senderIdStr, idLen)) {
      Serial.printf("   Invalid sender ID format: %s\n", senderIdStr);
      return false;
    }
    
    // Decode the hex node ID to bytes
    uint8_t senderPrefix[4];
    hexDecode(senderIdStr, 8, senderPrefix, 4);
    
    // Look up contact by pub_key prefix
    ContactInfo* contact = lookupContactByPubKey(senderPrefix, 4);
    if (!contact) {
      Serial.printf("   Unknown sender node: %s (not in contacts)\n", senderIdStr);
      return false;
    }
    
    Serial.printf("   Sender verified: %s (%s)\n", senderIdStr, contact->name);
    return true;
  }

  // Helper: Validate WDP message format
  // Checks basic UDH structure and minimum length requirements
  // Returns true if message appears to be valid WDP data
  bool isValidWDPMessage(const uint8_t* data, size_t len) {
    // Minimum UDH length: 7 bytes for simple UDH (headerLen + EI + eiLen + 2x port)
    // Or 12 bytes for concatenated UDH
    if (len < 7) {
      Serial.printf("   Invalid WDP: message too short (%zu bytes, min 7)\n", len);
      return false;
    }
    
    uint8_t headerLen = data[0];
    
    // Simple UDH: headerLen should be 0x06 (6 bytes following)
    // Concatenated UDH: headerLen should be 0x0B (11 bytes following)
    if (headerLen != 0x06 && headerLen != 0x0B) {
      Serial.printf("   Invalid WDP: unexpected UDH header length 0x%02X (expected 0x06 or 0x0B)\n", headerLen);
      return false;
    }
    
    // Verify message is long enough for the declared UDH
    if (len < (size_t)(headerLen + 1)) {
      Serial.printf("   Invalid WDP: message too short for UDH (%zu bytes, need %d)\n", len, headerLen + 1);
      return false;
    }
    
    // Simple UDH validation (0x06)
    if (headerLen == 0x06) {
      // Expected format: [0x06] [0x05] [0x04] [dest_hi] [dest_lo] [src_hi] [src_lo]
      uint8_t ei = data[1];      // Element Identifier
      uint8_t eiLen = data[2];   // Element length
      
      if (ei != 0x05) {
        Serial.printf("   Invalid WDP: unexpected element ID 0x%02X (expected 0x05 for port addressing)\n", ei);
        return false;
      }
      
      if (eiLen != 0x04) {
        Serial.printf("   Invalid WDP: unexpected element length 0x%02X (expected 0x04)\n", eiLen);
        return false;
      }
      
      // Extract and validate ports (should be non-zero)
      uint16_t destPort = (data[3] << 8) | data[4];
      uint16_t srcPort = (data[5] << 8) | data[6];
      
      if (destPort == 0 || srcPort == 0) {
        Serial.printf("   Invalid WDP: zero port number (dest=%d, src=%d)\n", destPort, srcPort);
        return false;
      }
    }
    
    // Concatenated UDH validation (0x0B)
    if (headerLen == 0x0B) {
      // Expected format: [0x0B] [0x00] [0x03] [ref] [total] [current] [0x05] [0x04] [dest_hi] [dest_lo] [src_hi] [src_lo]
      if (data[1] != 0x00 || data[2] != 0x03) {
        Serial.printf("   Invalid WDP: unexpected concat header (0x%02X 0x%02X)\n", data[1], data[2]);
        return false;
      }
      
      uint8_t totalParts = data[4];
      uint8_t currentPart = data[5];
      
      if (totalParts == 0 || currentPart == 0 || currentPart > totalParts) {
        Serial.printf("   Invalid WDP: invalid concat part info (part %d/%d)\n", currentPart, totalParts);
        return false;
      }
      
      if (data[6] != 0x05 || data[7] != 0x04) {
        Serial.printf("   Invalid WDP: unexpected port addressing header (0x%02X 0x%02X)\n", data[6], data[7]);
        return false;
      }
      
      uint16_t destPort = (data[8] << 8) | data[9];
      uint16_t srcPort = (data[10] << 8) | data[11];
      
      if (destPort == 0 || srcPort == 0) {
        Serial.printf("   Invalid WDP: zero port number (dest=%d, src=%d)\n", destPort, srcPort);
        return false;
      }
    }
    
    return true;
  }

  void onMessageRecv(const ContactInfo& from, mesh::Packet* pkt, uint32_t sender_timestamp, const char *text) override {
    Serial.printf("(%s) MSG -> from %s\n", pkt->isRouteDirect() ? "DIRECT" : "FLOOD", from.name);
    
#ifdef ESP32
    size_t textLen = strlen(text);
    
    // Check for "ping" command BEFORE base91 decoding (ping is sent as raw text, not base91)
    if (textLen == 4 && memcmp(text, "ping", 4) == 0) {
      char senderIdStr[16];
      snprintf(senderIdStr, sizeof(senderIdStr), "%02x%02x%02x%02x", 
               from.id.pub_key[0], from.id.pub_key[1], from.id.pub_key[2], from.id.pub_key[3]);
      Serial.printf("   Ping received from %s, queuing reply\n", senderIdStr);
      
      // Queue ping reply for sending after ACK
      for (int i = 0; i < MAX_PENDING_REPLIES; i++) {
        if (!pending_replies[i].active) {
          pending_replies[i].active = true;
          pending_replies[i].time = _ms->getMillis();
          memcpy(pending_replies[i].senderPubKey, from.id.pub_key, PUB_KEY_SIZE);
          snprintf(pending_replies[i].replyText, sizeof(pending_replies[i].replyText), "ping ok");
          Serial.printf("   (queued ping reply #%d for sending after ACK)\n", i);
          break;
        }
      }
      messages_handled++;
      updateDisplay();
      return;
    }
    
    // Check if this looks like Base91-encoded WDP data
    if (textLen > 0) {
      // Queue the message for Base91 decoding and processing
      for (int i = 0; i < MAX_PENDING_INBOX; i++) {
        if (!pending_inbox[i].active) {
          pending_inbox[i].active = true;
          pending_inbox[i].time = _ms->getMillis();
          snprintf(pending_inbox[i].senderIdStr, sizeof(pending_inbox[i].senderIdStr), "%02x%02x%02x%02x", 
                   from.id.pub_key[0], from.id.pub_key[1], from.id.pub_key[2], from.id.pub_key[3]);
          // Leave room for null terminator - Base91::decode expects null-terminated string!
          pending_inbox[i].wdpLen = (textLen < sizeof(pending_inbox[i].wdpData) - 1) ? textLen : sizeof(pending_inbox[i].wdpData) - 1;
          memcpy(pending_inbox[i].wdpData, text, pending_inbox[i].wdpLen);
          pending_inbox[i].wdpData[pending_inbox[i].wdpLen] = '\0';  // Null-terminate for Base91::decode
          Serial.printf("   (queued message #%d for Base91 decode, %zu chars)\n", i, textLen);
          messages_handled++;
          updateDisplay();
          return;
        }
      }
      Serial.println("   WARNING: Pending inbox full, dropping message");
      return;
    }
    
    // Empty message - queue welcome/error reply (sent after ACK)
    Serial.printf("   Empty message received\n");
#else
    Serial.printf("   %s\n", text);

    if (strcmp(text, "clock sync") == 0) {  // special text command
      setClock(sender_timestamp + 1);
    }
#endif

    // Increment message counter and update display
    messages_handled++;
    updateDisplay();
  }

  void onCommandDataRecv(const ContactInfo& from, mesh::Packet* pkt, uint32_t sender_timestamp, const char *text) override {
  }
  void onSignedMessageRecv(const ContactInfo& from, mesh::Packet* pkt, uint32_t sender_timestamp, const uint8_t *sender_prefix, const char *text) override {
  }

  void onChannelMessageRecv(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t timestamp, const char *text) override {
    if (pkt->isRouteDirect()) {
      Serial.printf("PUBLIC CHANNEL MSG -> (Direct!)\n");
    } else {
      Serial.printf("PUBLIC CHANNEL MSG -> (Flood) hops %d\n", pkt->path_len);
    }
    Serial.printf("   %s\n", text);
  }

  uint8_t onContactRequest(const ContactInfo& contact, uint32_t sender_timestamp, const uint8_t* data, uint8_t len, uint8_t* reply) override {
    return 0;  // unknown
  }

  void onContactResponse(const ContactInfo& contact, const uint8_t* data, uint8_t len) override {
    // not supported
  }

  uint32_t calcFloodTimeoutMillisFor(uint32_t pkt_airtime_millis) const override {
    return SEND_TIMEOUT_BASE_MILLIS + (FLOOD_SEND_TIMEOUT_FACTOR * pkt_airtime_millis);
  }
  uint32_t calcDirectTimeoutMillisFor(uint32_t pkt_airtime_millis, uint8_t path_len) const override {
    return SEND_TIMEOUT_BASE_MILLIS + 
         ( (pkt_airtime_millis*DIRECT_SEND_PERHOP_FACTOR + DIRECT_SEND_PERHOP_EXTRA_MILLIS) * (path_len + 1));
  }

  void onSendTimeout() override {
    if (expected_ack_crc != 0) {  // Only show error if we're still waiting for ACK
      Serial.println("   ERROR: timed out, no ACK.");
    }
  }

public:
  MyMesh(mesh::Radio& radio, StdRNG& rng, mesh::RTCClock& rtc, SimpleMeshTables& tables)
     : BaseChatMesh(radio, *new ArduinoMillis(), rng, rtc, *new StaticPoolPacketManager(16), tables)
  {
    // defaults
    memset(&_prefs, 0, sizeof(_prefs));
    _prefs.airtime_factor = 2.0;    // one third
    #if (OPERATION_MODE == MODE_AP)
      strcpy(_prefs.node_name, "MAP-AP");
    #else
      strcpy(_prefs.node_name, "MAP-Proxy");
    #endif
    _prefs.freq = LORA_FREQ;
    _prefs.tx_power_dbm = LORA_TX_POWER;

    command[0] = 0;
    curr_recipient = NULL;
    // Initialize pending inbox queue
    for (int i = 0; i < MAX_PENDING_INBOX; i++) {
      pending_inbox[i].active = false;
    }
    // Initialize pending replies queue
    for (int i = 0; i < MAX_PENDING_REPLIES; i++) {
      pending_replies[i].active = false;
    }
    messages_handled = 0;
  }

  float getFreqPref() const { return _prefs.freq; }
  uint8_t getTxPowerPref() const { return _prefs.tx_power_dbm; }
  uint32_t getMessagesHandled() const { return messages_handled; }

#ifdef ESP32
#if (OPERATION_MODE == MODE_AP)
  // Proxy path discovery state (for AP mode only)
  // On startup the AP will ping the proxy node to discover a path
  bool proxy_ping_pending = false;
  unsigned long proxy_ping_sent_time = 0;
  static const unsigned long PROXY_PING_TIMEOUT_MS = 8000;  // 8 second timeout per attempt

  // Get proxy contact by public key
  ContactInfo* getProxyContact() {
    // Convert PROXY_NODE_PUBKEY hex string to bytes
    uint8_t proxyPubKey[PUB_KEY_SIZE];
    size_t pkLen = strlen(PROXY_NODE_PUBKEY);
    if (pkLen >= PUB_KEY_SIZE * 2) {
      hexDecode(PROXY_NODE_PUBKEY, PUB_KEY_SIZE * 2, proxyPubKey, PUB_KEY_SIZE);
      return lookupContactByPubKey(proxyPubKey, PUB_KEY_SIZE);
    }
    return nullptr;
  }

  // Reset path to proxy node (forces flood routing for next message)
  bool resetProxyPath() {
    ContactInfo* proxy = getProxyContact();
    if (!proxy) {
      Serial.println("AP-Discovery: Proxy contact not found!");
      return false;
    }
    resetPathTo(*proxy);
    saveContacts();
    Serial.printf("AP-Discovery: Reset path to proxy %s\n", proxy->name);
    return true;
  }

  // Send ping to proxy using flood routing
  bool sendProxyPing() {
    ContactInfo* proxy = getProxyContact();
    if (!proxy) {
      Serial.println("AP-Discovery: Cannot ping - proxy contact not found!");
      return false;
    }
    
    uint32_t est_timeout;
    int result = sendMessage(*proxy, getRTCClock()->getCurrentTime(), 0, "ping", expected_ack_crc, est_timeout);
    if (result == MSG_SEND_FAILED) {
      Serial.println("AP-Discovery: Ping send failed");
      return false;
    }
    
    proxy_ping_sent_time = _ms->getMillis();
    proxy_ping_pending = true;
    Serial.printf("AP-Discovery: Ping sent via %s\n", result == MSG_SEND_SENT_FLOOD ? "FLOOD" : "DIRECT");
    return true;
  }

  // Check if ping was acknowledged (call after processAck)
  bool isProxyPingPending() const {
    return proxy_ping_pending;
  }

  // Check if ping timed out
  bool isProxyPingTimedOut() const {
    if (!proxy_ping_pending) return false;
    return (_ms->getMillis() - proxy_ping_sent_time) > PROXY_PING_TIMEOUT_MS;
  }

  // Called when we receive an ACK - check if it's from proxy
  void checkProxyPingAck() {
    if (proxy_ping_pending && expected_ack_crc == 0) {
      // ACK was received (expected_ack_crc is cleared in processAck)
      proxy_ping_pending = false;
      
      // Get updated path length
      ContactInfo* proxy = getProxyContact();
      if (proxy) {
        Serial.printf("AP-Discovery: Proxy path discovered! path_len=%d\n", proxy->out_path_len);
        ap_setProxyPathDiscovered(proxy->out_path_len);
        updateDisplay();
      }
    }
  }

  // Get current proxy path length (or -1 if unknown)
  int getProxyPathLen() {
    ContactInfo* proxy = getProxyContact();
    if (proxy && ap_isProxyPathDiscovered()) {
      return proxy->out_path_len;
    }
    return -1;
  }

  // Start proxy discovery process - resets path and sends ping via flood
  void startProxyDiscovery() {
    Serial.println("AP-Discovery: Starting proxy path discovery...");
    
    // Check if proxy contact exists
    ContactInfo* proxy = getProxyContact();
    if (!proxy) {
      Serial.println("AP-Discovery: Proxy contact not found, adding from PROXY_NODE_PUBKEY...");
      
      // Send advertisement first so proxy can discover us
      Serial.println("AP-Discovery: Sending advertisement...");
      sendSelfAdvert(0);
      
      // Manually add proxy contact from PROXY_NODE_PUBKEY
      size_t pkLen = strlen(PROXY_NODE_PUBKEY);
      if (pkLen >= PUB_KEY_SIZE * 2) {
        ContactInfo newContact;
        memset(&newContact, 0, sizeof(newContact));
        
        // Decode public key from hex
        uint8_t proxyPubKey[PUB_KEY_SIZE];
        hexDecode(PROXY_NODE_PUBKEY, PUB_KEY_SIZE * 2, proxyPubKey, PUB_KEY_SIZE);
        newContact.id = mesh::Identity(proxyPubKey);
        
        // Set contact properties
        strncpy(newContact.name, "Proxy", sizeof(newContact.name) - 1);
        newContact.type = ADV_TYPE_CHAT;
        newContact.flags = 0;
        newContact.out_path_len = 0;  // Unknown path, will use flood
        newContact.last_advert_timestamp = getRTCClock()->getCurrentTime();
        newContact.gps_lat = 0;
        newContact.gps_lon = 0;
        
        if (addContact(newContact)) {
          saveContacts();
          Serial.println("AP-Discovery: Proxy contact added successfully");
          proxy = getProxyContact();  // Re-fetch the contact
        } else {
          Serial.println("AP-Discovery: Failed to add proxy contact!");
          displayStatus("ERROR!", "Failed to add", "proxy contact!", "");
          return;
        }
      } else {
        Serial.println("AP-Discovery: Invalid PROXY_NODE_PUBKEY!");
        displayStatus("ERROR!", "Invalid", "PROXY_NODE_PUBKEY!", "");
        return;
      }
    }
    
    if (!proxy) {
      Serial.println("AP-Discovery: ERROR - Still no proxy contact!");
      displayStatus("ERROR!", "Proxy contact", "not found!", "");
      return;
    }
    
    // Start discovery process
    ap_startProxyDiscovery();
    
    // First attempt
    if (ap_incrementProxyDiscoveryAttempt()) {
      updateDisplay();
      
      // Reset path to force flood routing
      resetProxyPath();
      
      // Send ping
      sendProxyPing();
    }
  }
#endif // OPERATION_MODE == MODE_AP

  // Send WDP data to a MeshCore recipient (for WDP Gateway responses)
  // Recipient is identified by pub_key prefix hex string
  // NOTE: MeshCore sendMessage uses strlen() and WDP contains a lot of 0x00
  // so we must Base91-encode binary data to avoid null bytes truncating the message!
  // Base91 uses all ASCII characters not causing issues, much more efficient than hex or base64.
  void sendWDPToMesh(const String& recipientId, const uint8_t* data, size_t len) {
    Serial.printf("WDP->Mesh: Sending %d bytes to %s\n", len, recipientId.c_str());
    
    // Find contact by pub_key prefix
    uint8_t targetPrefix[4];
    for (int i = 0; i < 4 && i*2 < (int)recipientId.length(); i++) {
      targetPrefix[i] = (hexToNibble(recipientId.charAt(i*2)) << 4) | 
                        hexToNibble(recipientId.charAt(i*2 + 1));
    }
    
    ContactInfo* contact = lookupContactByPubKey(targetPrefix, 4);
    if (!contact) {
      Serial.printf("WDP->Mesh: Contact not found for %s\n", recipientId.c_str());
      return;
    }
    
    const size_t maxBinaryLen = ((MESHCORE_MAX_BYTES - 1) * 13) / 16;
    if (len > maxBinaryLen) {
      Serial.printf("WDP->Mesh: Data too large (%d bytes), truncating to %d\n", len, maxBinaryLen);
      len = maxBinaryLen;
    }
    
    // Base91-encode
    char encodedMsg[MESHCORE_MAX_BYTES + 1];
    size_t encodedLen = Base91::encode(data, len, encodedMsg, sizeof(encodedMsg));
    if (encodedLen == 0) {
      Serial.println("WDP->Mesh: Base91 encoding failed");
      return;
    }
    
    // Send as regular message
    uint32_t est_timeout;
    int result = sendMessage(*contact, getRTCClock()->getCurrentTime(), 0, encodedMsg, expected_ack_crc, est_timeout);
    if (result == MSG_SEND_FAILED) {
      Serial.println("WDP->Mesh: Send failed");
    } else {
      last_msg_sent = _ms->getMillis();
      Serial.printf("WDP->Mesh: Sent %s (%d bytes Base91-encoded as %d chars)\n", 
                    result == MSG_SEND_SENT_FLOOD ? "FLOOD" : "DIRECT", len, encodedLen);
    }
  }
#endif

  void updateDisplay() {
    char line2[32];
    char line3[32];
    char line4[48];
    
    #if defined(ESP32)
      if (OPERATION_MODE == MODE_PROXY) {
        snprintf(line2, sizeof(line2), "Proxy ready!");
        snprintf(line3, sizeof(line3), "Freq: %.1f MHz", _prefs.freq);
        snprintf(line4, sizeof(line4), "Messages: %lu", (unsigned long)messages_handled);
        displayStatus("MeshAccessProtocol", line2, line3, line4);
      } else if (OPERATION_MODE == MODE_AP) {
        // Skip display update if WDP session is active (it manages its own display)
        if (ap_isWdpSessionActive()) {
          return;
        }
        
        // Check proxy discovery status
        if (ap_isProxyDiscoveryInProgress()) {
          int attempt = ap_getProxyDiscoveryAttempt();
          snprintf(line2, sizeof(line2), "Finding proxy...");
          snprintf(line3, sizeof(line3), "Attempt %d/%d", attempt, AP_PROXY_DISCOVERY_MAX_RETRIES);
          snprintf(line4, sizeof(line4), "Pinging via flood");
          displayStatus("MeshAccessProtocol", line2, line3, line4);
        } else if (ap_isProxyPathDiscovered()) {
          int pathLen = ap_getProxyPathLen();
          int clientCount = ap_getClientCount();
          if (pathLen == 0) {
            snprintf(line2, sizeof(line2), "Path: direct");
          } else {
            snprintf(line2, sizeof(line2), "Path: %d hop%s", pathLen, pathLen == 1 ? "" : "s");
          }
          snprintf(line3, sizeof(line3), "WiFi Clients: %d", clientCount);
          snprintf(line4, sizeof(line4), "Msgs: %lu", (unsigned long)messages_handled);
          displayStatus("MeshAccessProtocol", line2, line3, line4);
        } else {
          // Proxy not found after all attempts
          int clientCount = ap_getClientCount();
          snprintf(line2, sizeof(line2), "Proxy: NOT FOUND!");
          snprintf(line3, sizeof(line3), "WiFi Clients: %d", clientCount);
          snprintf(line4, sizeof(line4), "Msgs: %lu", (unsigned long)messages_handled);
          displayStatus("MeshAccessProtocol", line2, line3, line4);
        }
      }
    #endif
  }

  void begin(FILESYSTEM& fs) {
    _fs = &fs;

    BaseChatMesh::begin();

  #if defined(NRF52_PLATFORM)
    IdentityStore store(fs, "");
  #elif defined(RP2040_PLATFORM)
    IdentityStore store(fs, "/identity");
    store.begin();
  #else
    IdentityStore store(fs, "/identity");
  #endif
    if (!store.load("_main", self_id, _prefs.node_name, sizeof(_prefs.node_name))) {
      // Need way to get some entropy to seed RNG
      Serial.println("Press ENTER to generate key:");
      char c = 0;
      while (c != '\n') {   // wait for ENTER to be pressed
        if (Serial.available()) c = Serial.read();
      }
      ((StdRNG *)getRNG())->begin(millis());

      self_id = mesh::LocalIdentity(getRNG());  // create new random identity
      int count = 0;
      while (count < 10 && (self_id.pub_key[0] == 0x00 || self_id.pub_key[0] == 0xFF)) {  // reserved id hashes
        self_id = mesh::LocalIdentity(getRNG()); count++;
      }
      store.save("_main", self_id);
    }

    // load persisted prefs
    if (_fs->exists("/node_prefs")) {
    #if defined(RP2040_PLATFORM)
      File file = _fs->open("/node_prefs", "r");
    #else
      File file = _fs->open("/node_prefs");
    #endif
      if (file) {
        file.read((uint8_t *) &_prefs, sizeof(_prefs));
        file.close();
      }
    }

    loadContacts();
    _public = addChannel("Public", PUBLIC_GROUP_PSK);
  }

  void savePrefs() {
#if defined(NRF52_PLATFORM)
    _fs->remove("/node_prefs");
    File file = _fs->open("/node_prefs", FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
    File file = _fs->open("/node_prefs", "w");
#else
    File file = _fs->open("/node_prefs", "w", true);
#endif
    if (file) {
      file.write((const uint8_t *)&_prefs, sizeof(_prefs));
      file.close();
    }
  }

  void showWelcome() {
    Serial.println("===== MeshAccessProtocol Terminal =====");
    Serial.println();
    Serial.printf("WELCOME TO %s\n", _prefs.node_name);
    mesh::Utils::printHex(Serial, self_id.pub_key, PUB_KEY_SIZE);
    Serial.println();
    Serial.println("   (enter 'help' for basic commands)");
    Serial.println();
  }

  void sendSelfAdvert(int delay_millis) {
    auto pkt = createSelfAdvert(_prefs.node_name, _prefs.node_lat, _prefs.node_lon);
    if (pkt) {
      sendFlood(pkt, delay_millis);
    }
  }

  void onContactVisit(const ContactInfo& contact) override {
    Serial.printf("   %s - ", contact.name);
    char tmp[40];
    int32_t secs = contact.last_advert_timestamp - getRTCClock()->getCurrentTime();
    AdvertTimeHelper::formatRelativeTimeDiff(tmp, secs, false);
    Serial.println(tmp);
  }

  void handleCommand(const char* command) {
    while (*command == ' ') command++;  // skip leading spaces

    if (memcmp(command, "send ", 5) == 0) {
      if (curr_recipient) {
        const char *text = &command[5];
        uint32_t est_timeout;

        int result = sendMessage(*curr_recipient, getRTCClock()->getCurrentTime(), 0, text, expected_ack_crc, est_timeout);
        if (result == MSG_SEND_FAILED) {
          Serial.println("   ERROR: unable to send.");
        } else {
          last_msg_sent = _ms->getMillis();
          Serial.printf("   (message sent - %s)\n", result == MSG_SEND_SENT_FLOOD ? "FLOOD" : "DIRECT");
        }
      } else {
        Serial.println("   ERROR: no recipient selected (use 'to' cmd).");
      }
    } else if (memcmp(command, "public ", 7) == 0) {  // send GroupChannel msg
      uint8_t temp[5+MAX_TEXT_LEN+32];
      uint32_t timestamp = getRTCClock()->getCurrentTime();
      memcpy(temp, &timestamp, 4);   // mostly an extra blob to help make packet_hash unique
      temp[4] = 0;  // attempt and flags

      sprintf((char *) &temp[5], "%s: %s", _prefs.node_name, &command[7]);  // <sender>: <msg>
      temp[5 + MAX_TEXT_LEN] = 0;  // truncate if too long

      int len = strlen((char *) &temp[5]);
      auto pkt = createGroupDatagram(PAYLOAD_TYPE_GRP_TXT, _public->channel, temp, 5 + len);
      if (pkt) {
        sendFlood(pkt);
        Serial.println("   Sent.");
      } else {
        Serial.println("   ERROR: unable to send");
      }
    } else if (memcmp(command, "list", 4) == 0) {  // show Contact list, by most recent
      int n = 0;
      if (command[4] == ' ') {  // optional param, last 'N'
        n = atoi(&command[5]);
      }
      scanRecentContacts(n, this);
    } else if (strcmp(command, "clock") == 0) {    // show current time
      uint32_t now = getRTCClock()->getCurrentTime();
      DateTime dt = DateTime(now);
      Serial.printf(   "%02d:%02d - %d/%d/%d UTC\n", dt.hour(), dt.minute(), dt.day(), dt.month(), dt.year());
    } else if (memcmp(command, "time ", 5) == 0) {  // set time (to epoch seconds)
      uint32_t secs = _atoi(&command[5]);
      setClock(secs);
    } else if (memcmp(command, "to ", 3) == 0) {  // set current recipient
      curr_recipient = searchContactsByPrefix(&command[3]);
      if (curr_recipient) {
        Serial.printf("   Recipient %s now selected.\n", curr_recipient->name);
      } else {
        Serial.println("   Error: Name prefix not found.");
      }
    } else if (strcmp(command, "to") == 0) {    // show current recipient
      if (curr_recipient) {
         Serial.printf("   Current: %s\n", curr_recipient->name);
      } else {
         Serial.println("   Err: no recipient selected");
      }
    } else if (strcmp(command, "advert") == 0) {
      auto pkt = createSelfAdvert(_prefs.node_name, _prefs.node_lat, _prefs.node_lon);
      if (pkt) {
        sendZeroHop(pkt);
        Serial.println("   (advert sent, zero hop).");
      } else {
        Serial.println("   ERR: unable to send");
      }
    } else if (strcmp(command, "reset path") == 0) {
      if (curr_recipient) {
        resetPathTo(*curr_recipient);
        saveContacts();
        Serial.println("   Done.");
      }
    } else if (memcmp(command, "card", 4) == 0) {
      Serial.printf("Hello %s\n", _prefs.node_name);
      auto pkt = createSelfAdvert(_prefs.node_name, _prefs.node_lat, _prefs.node_lon);
      if (pkt) {
        uint8_t len =  pkt->writeTo(tmp_buf);
        releasePacket(pkt);  // undo the obtainNewPacket()

        mesh::Utils::toHex(hex_buf, tmp_buf, len);
        Serial.println("Your MeshCore biz card:");
        Serial.print("meshcore://"); Serial.println(hex_buf);
        Serial.println();
      } else {
        Serial.println("  Error");
      }
    } else if (memcmp(command, "import ", 7) == 0) {
      importCard(&command[7]);
    } else if (memcmp(command, "set ", 4) == 0) {
      const char* config = &command[4];
      if (memcmp(config, "af ", 3) == 0) {
        _prefs.airtime_factor = atof(&config[3]);
        savePrefs();
        Serial.println("  OK");
      } else if (memcmp(config, "name ", 5) == 0) {
        StrHelper::strncpy(_prefs.node_name, &config[5], sizeof(_prefs.node_name));
        savePrefs();
        Serial.println("  OK");
      } else if (memcmp(config, "lat ", 4) == 0) {
        _prefs.node_lat = atof(&config[4]);
        savePrefs();
        Serial.println("  OK");
      } else if (memcmp(config, "lon ", 4) == 0) {
        _prefs.node_lon = atof(&config[4]);
        savePrefs();
        Serial.println("  OK");
      } else if (memcmp(config, "tx ", 3) == 0) {
        _prefs.tx_power_dbm = atoi(&config[3]);
        savePrefs();
        Serial.println("  OK - reboot to apply");
      } else if (memcmp(config, "freq ", 5) == 0) {
        _prefs.freq = atof(&config[5]);
        savePrefs();
        Serial.println("  OK - reboot to apply");
      } else {
        Serial.printf("  ERROR: unknown config: %s\n", config);
      }
    } else if (memcmp(command, "ver", 3) == 0) {
      Serial.println(FIRMWARE_VER_TEXT);
    } else if (memcmp(command, "help", 4) == 0) {
      Serial.println("Commands:");
      Serial.println("   set {name|lat|lon|freq|tx|af} {value}");
      Serial.println("   card");
      Serial.println("   import {biz card}");
      Serial.println("   clock");
      Serial.println("   time <epoch-seconds>");
      Serial.println("   list {n}");
      Serial.println("   to <recipient name or prefix>");
      Serial.println("   to");
      Serial.println("   send <text>");
      Serial.println("   advert");
      Serial.println("   reset path");
      Serial.println("   public <text>");
    } else {
      Serial.print("   ERROR: unknown command: "); Serial.println(command);
    }
  }

  void loop() {
    BaseChatMesh::loop();

#ifdef ESP32
  #if (OPERATION_MODE == MODE_PROXY)
    // Check for UDP responses from WAPBOX 
    proxy_loop();
    
    // Process pending WDP messages (after 100ms to allow ACK to be sent first)
    for (int i = 0; i < MAX_PENDING_INBOX; i++) {
      if (pending_inbox[i].active && (_ms->getMillis() - pending_inbox[i].time > 100)) {
        // Copy data before clearing
        char senderIdStr[20];
        uint8_t wdpData[256];
        size_t wdpLen = pending_inbox[i].wdpLen;
        strncpy(senderIdStr, pending_inbox[i].senderIdStr, sizeof(senderIdStr));
        memcpy(wdpData, pending_inbox[i].wdpData, sizeof(wdpData));
        
        // Clear the slot
        clearPendingInbox(&pending_inbox[i]);
        
        Serial.printf("   Processing queued WDP message from %s\n", senderIdStr);
        
        // Validate sender node ID before processing
        if (!isValidSenderNodeId(senderIdStr)) {
          Serial.println("   REJECTED: Message from unknown/invalid node ID");
          break;
        }
        
        // Base91-decode the message
        uint8_t decodedData[256];
        size_t decodedLen = Base91::decode((const char*)wdpData, 
                                            decodedData, sizeof(decodedData));
        
        if (decodedLen > 0) {
          Serial.printf("   Base91-decoded: %zu chars -> %zu bytes\n", 
                        strlen((const char*)wdpData), decodedLen);
          
          // Validate WDP message format before forwarding
          if (!isValidWDPMessage(decodedData, decodedLen)) {
            Serial.println("   REJECTED: Invalid WDP message format");
            break;
          }
          
          // Forward decoded binary to WDP gateway
          proxy_handleIncomingMesh(String(senderIdStr), 
                                   decodedData, 
                                   decodedLen);
        } else {
          Serial.println("   Base91 decode failed, trying as raw binary");
          
          // Validate WDP message format before forwarding (raw binary)
          if (!isValidWDPMessage(wdpData, wdpLen)) {
            Serial.println("   REJECTED: Invalid WDP message format (raw binary)");
            break;
          }
          
          // Fallback: try as raw binary (for backward compatibility)
          proxy_handleIncomingMesh(String(senderIdStr), 
                                   wdpData, 
                                   wdpLen);
        }
        
        break;  // Only process one per loop iteration
      }
    }
  #elif (OPERATION_MODE == MODE_AP)
    // Handle HTTP clients and response timeouts
    ap_loop();
    
    // Check if display needs update (e.g., WiFi client count changed)
    if (ap_needsDisplayUpdate()) {
      updateDisplay();
    }
    
    // Check if proxy ping was acknowledged
    checkProxyPingAck();
    
    // Handle proxy discovery timeout and retries
    if (ap_isProxyDiscoveryInProgress() && !ap_isProxyPathDiscovered()) {
      if (isProxyPingTimedOut()) {
        Serial.println("AP-Discovery: Ping timed out");
        proxy_ping_pending = false;
        
        // Try again if we have retries left
        if (ap_incrementProxyDiscoveryAttempt()) {
          int attempt = ap_getProxyDiscoveryAttempt();
          Serial.printf("AP-Discovery: Retry %d/%d\n", attempt, AP_PROXY_DISCOVERY_MAX_RETRIES);
          updateDisplay();
          
          // Reset path and send new ping
          resetProxyPath();
          sendProxyPing();
        } else {
          // No more retries
          Serial.println("AP-Discovery: Failed to discover proxy after all retries");
          updateDisplay();
        }
      }
    }
    
    // Process pending WDP messages for AP mode
    for (int i = 0; i < MAX_PENDING_INBOX; i++) {
      if (pending_inbox[i].active && (_ms->getMillis() - pending_inbox[i].time > 100)) {
        // Copy data before clearing
        char senderIdStr[20];
        uint8_t wdpData[256];
        size_t wdpLen = pending_inbox[i].wdpLen;
        strncpy(senderIdStr, pending_inbox[i].senderIdStr, sizeof(senderIdStr));
        memcpy(wdpData, pending_inbox[i].wdpData, sizeof(wdpData));
        
        // Clear the slot
        clearPendingInbox(&pending_inbox[i]);
        
        Serial.printf("   Processing queued message from %s (AP mode)\n", senderIdStr);
        
        // Validate sender node ID before processing
        if (!isValidSenderNodeId(senderIdStr)) {
          Serial.println("   REJECTED: Message from unknown/invalid node ID (AP mode)");
          break;
        }
        
        // Base91-decode the message
        uint8_t decodedData[256];
        size_t decodedLen = Base91::decode((const char*)wdpData, 
                                            decodedData, sizeof(decodedData));
        
        if (decodedLen > 0) {
          Serial.printf("   Base91-decoded: %zu chars -> %zu bytes\n", 
                        strlen((const char*)wdpData), decodedLen);
          
          // Validate WDP message format before forwarding
          if (!isValidWDPMessage(decodedData, decodedLen)) {
            Serial.println("   REJECTED: Invalid WDP message format (AP mode)");
            break;
          }
          
          // Forward decoded binary to AP mode handler
          ap_handleIncomingMesh(String(senderIdStr), 
                                decodedData, 
                                decodedLen);
        } else {
          Serial.println("   Base91 decode failed, trying as raw binary");
          
          // Validate WDP message format before forwarding (raw binary)
          if (!isValidWDPMessage(wdpData, wdpLen)) {
            Serial.println("   REJECTED: Invalid WDP message format (raw binary, AP mode)");
            break;
          }
          
          // Fallback: try as raw binary (for backward compatibility)
          ap_handleIncomingMesh(String(senderIdStr), 
                                wdpData, 
                                wdpLen);
        }
        
        break;  // Only process one per loop iteration
      }
    }
  #endif
    
    // Process pending replies (after 100ms to allow ACK to be sent first)
    for (int i = 0; i < MAX_PENDING_REPLIES; i++) {
      if (pending_replies[i].active && (_ms->getMillis() - pending_replies[i].time > 100)) {
        // Copy data before clearing
        uint8_t senderPubKey[PUB_KEY_SIZE];
        char replyText[256];
        memcpy(senderPubKey, pending_replies[i].senderPubKey, sizeof(senderPubKey));
        strncpy(replyText, pending_replies[i].replyText, sizeof(replyText));
        
        // Clear the slot
        clearPendingReply(&pending_replies[i]);
        
        Serial.println("   Processing queued welcome reply");
        
        ContactInfo* sender = lookupContactByPubKey(senderPubKey, PUB_KEY_SIZE);
        if (sender) {
          uint32_t est_timeout;
          int result = sendMessage(*sender, getRTCClock()->getCurrentTime(), 0, 
                                   replyText, expected_ack_crc, est_timeout);
          if (result != MSG_SEND_FAILED) {
            last_msg_sent = _ms->getMillis();
            Serial.printf("   Sent welcome reply (%s)\n", result == MSG_SEND_SENT_FLOOD ? "FLOOD" : "DIRECT");
          }
        }
        
        break;  // Only process one per loop iteration
      }
    }
#endif

    int len = strlen(command);
    while (Serial.available() && len < sizeof(command)-1) {
      char c = Serial.read();
      if (c != '\n') { 
        command[len++] = c;
        command[len] = 0;
      }
      Serial.print(c);
    }
    if (len == sizeof(command)-1) {  // command buffer full
      command[sizeof(command)-1] = '\r';
    }

    if (len > 0 && command[len - 1] == '\r') {  // received complete line
      command[len - 1] = 0;  // replace newline with C string null terminator

      handleCommand(command);
      command[0] = 0;  // reset command buffer
    }
  }
};

StdRNG fast_rng;
SimpleMeshTables tables;
MyMesh the_mesh(radio_driver, fast_rng, rtc_clock, tables);

void halt() {
  // Fast blink on error
  while (1) {
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
    delay(100);
  }
}

void blinkLED(int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(200);
    digitalWrite(LED_PIN, LOW);
    delay(200);
  }
}

void displayInit() {
  display.begin();
  display.setFont(u8g2_font_6x10_tf);
  display.clearBuffer();
  display.drawStr(0, 12, "MAP Starting...");
  display.sendBuffer();
}

void displayStatus(const char* line1, const char* line2, const char* line3, const char* line4) {
  display.clearBuffer();
  display.setFont(u8g2_font_6x10_tf);
  if (line1) display.drawStr(0, 12, line1);
  if (line2) display.drawStr(0, 24, line2);
  if (line3) display.drawStr(0, 36, line3);
  if (line4) display.drawStr(0, 48, line4);
  display.sendBuffer();
}

// Deep sleep functions
void enterDeepSleep() {
  Serial.println("Entering deep sleep...");
  displayStatus("Deep Sleep", "Press BTN to wake");
  delay(500);  // Let user see the message
  
  // Turn off display
  display.clearBuffer();
  display.sendBuffer();
  display.setPowerSave(1);
  
  // Turn off LED
  digitalWrite(LED_PIN, LOW);
  
  // Configure GPIO0 (user button) as wakeup source
  // Button is active LOW, so wake on LOW level
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_USER_BTN, 0);
  
  // Enter deep sleep
  esp_deep_sleep_start();
}

// Button state tracking for long press detection
unsigned long buttonPressStart = 0;
bool buttonWasPressed = false;

void checkUserButton() {
  bool buttonPressed = (digitalRead(PIN_USER_BTN) == LOW);
  
  if (buttonPressed && !buttonWasPressed) {
    // Button just pressed - start timing
    buttonPressStart = millis();
    buttonWasPressed = true;
  } else if (buttonPressed && buttonWasPressed) {
    // Button held - check for long press
    if (millis() - buttonPressStart >= LONG_PRESS_DURATION_MS) {
      enterDeepSleep();
    }
  } else if (!buttonPressed && buttonWasPressed) {
    // Button released
    buttonWasPressed = false;
  }
}

#ifdef ESP32
void initWiFiMode() {
  #if (OPERATION_MODE == MODE_PROXY)
    Serial.println("DEBUG: Operation Mode = PROXY");
    proxy_connectToWiFi(WIFI_SSID, WIFI_PASSWORD);
  #elif (OPERATION_MODE == MODE_AP)
    Serial.println("DEBUG: Operation Mode = AP");
    ap_init();
  #else
    #error "Unknown OPERATION_MODE - must be MODE_PROXY or MODE_AP"
  #endif
}
#endif

void setup() {
  // Setup LED first - this should work immediately
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);  // LED on = we're alive
  
  // Setup user button for deep sleep detection
  pinMode(PIN_USER_BTN, INPUT_PULLUP);
  
  // Check if waking from deep sleep (button press)
  #ifdef ESP32
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
    // Woke from button press - wait for button release before continuing
    Serial.begin(115200);
    delay(100);
    Serial.println("Waking from deep sleep (button press)...");
    
    // Wait for button release to avoid immediate re-sleep
    while (digitalRead(PIN_USER_BTN) == LOW) {
      delay(10);
    }
    delay(100);  // Debounce
  }
  #endif
  
  Serial.begin(115200);
  delay(500);
  Serial.println();
    Serial.println("=== DEBUG: Starting setup ===");
  
  // Blink twice to show we're running
  blinkLED(2);
  
  // Initialize display
  displayInit();
  blinkLED(1);  // Blink once after display init
  
  // Wait for USB CDC serial to be ready (ESP32-S3 native USB)
  delay(2000);
  
  // Initialize WiFi based on operation mode
#ifdef ESP32
  initWiFiMode();
#endif
  
  displayStatus("MeshAccessProtocol", "Initializing...");

  Serial.println("DEBUG: Calling board.begin()...");
  board.begin();
  Serial.println("DEBUG: board.begin() done");

  displayStatus("MeshAccessProtocol", "Radio init...");

  Serial.println("DEBUG: Calling radio_init()...");
  if (!radio_init()) { 
    Serial.println("DEBUG: radio_init() FAILED!");
    displayStatus("ERROR!", "Radio init failed!");
    halt(); 
  }
  Serial.println("DEBUG: radio_init() done");

  displayStatus("MeshAccessProtocol", "Radio OK", "Seeding RNG...");

  Serial.println("DEBUG: Seeding RNG...");
  fast_rng.begin(radio_get_rng_seed());
  Serial.println("DEBUG: RNG seeded");

#if defined(NRF52_PLATFORM)
  Serial.println("DEBUG: Starting InternalFS...");
  InternalFS.begin();
  the_mesh.begin(InternalFS);
#elif defined(RP2040_PLATFORM)
  Serial.println("DEBUG: Starting LittleFS...");
  LittleFS.begin();
  the_mesh.begin(LittleFS);
#elif defined(ESP32)
  displayStatus("MeshAccessProtocol", "Radio OK", "Starting SPIFFS...");
  Serial.println("DEBUG: Starting SPIFFS...");
  SPIFFS.begin(true);
  displayStatus("MeshAccessProtocol", "Radio OK", "SPIFFS OK", "the_mesh.begin()");
  Serial.println("DEBUG: SPIFFS started, calling the_mesh.begin()...");
  the_mesh.begin(SPIFFS);
  Serial.println("DEBUG: the_mesh.begin() done");
#else
  #error "need to define filesystem"
#endif

  displayStatus("MeshAccessProtocol", "Radio OK", "Configuring...");

  Serial.println("DEBUG: Setting radio params...");
  radio_set_params(the_mesh.getFreqPref(), LORA_BW, LORA_SF, LORA_CR);
  Serial.println("DEBUG: Setting TX power...");
  radio_set_tx_power(the_mesh.getTxPowerPref());
  Serial.println("DEBUG: Radio configured");

  the_mesh.showWelcome();

  // Show ready status on display
  the_mesh.updateDisplay();

#ifdef ESP32
  // Initialize mode-specific functionality
  #if (OPERATION_MODE == MODE_PROXY)
    if (proxy_isWiFiConnected()) {
      Serial.println("DEBUG: Initializing WDP Gateway (Proxy Mode)...");
      proxy_init(WAPBOX_HOST, WAPBOX_PORT);
      proxy_begin([](const String& to, const uint8_t* data, size_t len) {
        the_mesh.sendWDPToMesh(to, data, len);
      });
      Serial.printf("DEBUG: WDP Gateway ready, forwarding to %s\n", WAPBOX_HOST);
      displayStatus("MeshAccessProtocol", "Proxy Mode Ready", WAPBOX_HOST);
      delay(1000);
    } else {
      Serial.println("DEBUG: WiFi not connected, WDP Gateway disabled");
    }
  #elif (OPERATION_MODE == MODE_AP)
    if (ap_isInitialized()) {
      Serial.println("DEBUG: AP Mode active, setting up mesh callbacks...");
      // Set mesh callback so AP mode can send requests via mesh to proxy node
      ap_setMeshCallback([](const String& to, const uint8_t* data, size_t len) {
        the_mesh.sendWDPToMesh(to, data, len);
      });
      // Set mesh loop callback so AP mode can process mesh during blocking HTTP waits
      // This is CRITICAL - without it, the AP cannot receive responses or send ACKs!
      ap_setMeshLoopCallback([]() {
        the_mesh.loop();
      });
      Serial.println("DEBUG: AP Mode mesh callbacks configured");
      
      // Start proxy path discovery - resets stored path and pings via flood
      // This ensures we always have a fresh path after boot
      Serial.println("DEBUG: Starting proxy path discovery...");
      the_mesh.startProxyDiscovery();
      
      // AP mode display is handled in startProxyDiscovery/updateDisplay
    } else {
      Serial.println("DEBUG: AP Mode failed to initialize");
    }
  #endif
#endif

  // send out initial Advertisement to the mesh
  Serial.println("DEBUG: Sending initial advert...");
  the_mesh.sendSelfAdvert(1200);   // add slight delay
  Serial.println("DEBUG: Setup complete!");
}

void loop() {
  the_mesh.loop();
  rtc_clock.tick();
  
  // Check for long press to enter deep sleep
  checkUserButton();
}
