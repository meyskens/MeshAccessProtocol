# MeshAccessProtocol (MAP) 

**Browsing over the mesh like it is 1999**


MeshAccessProtocol bridges WAP (Wireless Application Protocol) and MeshCore LoRa mesh networks, enabling low-bandwidth internet access over long-range decentralised radio links.

## OMG WHY?

[MeshCore](https://meshcore.co.uk) is a very neat and well scaling LoRa based mesh network. However as is the case with
many of these is focussed on messaging. Not a bad thing for sure, but what if we can enable off-grid web browsing?

MeshCore is designed to handle small messages (up to 150 bytes). Thinking about this is that it is very similar to SMS.
When the Wireless Application Protocol (WAP) was designed in the late 1990s it need a carrier, CSD (Circuit Switched Data) was not yet widely available and needed network changes. There was a rare short lived WAP over SMS standard (this all before GPRS was even a thing). Already running a [WAP over SMS gateway](https://github.com/bevelgacom/wap-over-sms/) I figured it would be fun to try and adapt WAP to run over MeshCore.

Unlike WAP 2.x which was more widely available, devices before 2002 uses UDP/WDP (over some barrier) to connect to a proxy server (like Kannel is the only open source of) which translated the WSP/WTP ultra tiny requests into the bulky HTTP.
Same was true for the WML (Wireless Markup Language) content, which was compiled into a binary format (WMLC) to save bandwidth.

Why reinvent the wheel for internet over mesh when somebody already did the hard work 25 years ago, and were right!

## Demo!!

<blockquote class="mastodon-embed" data-embed-url="https://blahaj.social/@maartje/115867225088740197/embed" style="background: #FCF8FF; border-radius: 8px; border: 1px solid #C9C4DA; margin: 0; max-width: 540px; min-width: 270px; overflow: hidden; padding: 0;"> <a href="https://blahaj.social/@maartje/115867225088740197" target="_blank" style="align-items: center; color: #1C1A25; display: flex; flex-direction: column; font-family: system-ui, -apple-system, BlinkMacSystemFont, 'Segoe UI', Oxygen, Ubuntu, Cantarell, 'Fira Sans', 'Droid Sans', 'Helvetica Neue', Roboto, sans-serif; font-size: 14px; justify-content: center; letter-spacing: 0.25px; line-height: 20px; padding: 24px; text-decoration: none;"> <svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="32" height="32" viewBox="0 0 79 75"><path d="M63 45.3v-20c0-4.1-1-7.3-3.2-9.7-2.1-2.4-5-3.7-8.5-3.7-4.1 0-7.2 1.6-9.3 4.7l-2 3.3-2-3.3c-2-3.1-5.1-4.7-9.2-4.7-3.5 0-6.4 1.3-8.6 3.7-2.1 2.4-3.1 5.6-3.1 9.7v20h8V25.9c0-4.1 1.7-6.2 5.2-6.2 3.8 0 5.8 2.5 5.8 7.4V37.7H44V27.1c0-4.9 1.9-7.4 5.8-7.4 3.5 0 5.2 2.1 5.2 6.2V45.3h8ZM74.7 16.6c.6 6 .1 15.7.1 17.3 0 .5-.1 4.8-.1 5.3-.7 11.5-8 16-15.6 17.5-.1 0-.2 0-.3 0-4.9 1-10 1.2-14.9 1.4-1.2 0-2.4 0-3.6 0-4.8 0-9.7-.6-14.4-1.7-.1 0-.1 0-.1 0s-.1 0-.1 0 0 .1 0 .1 0 0 0 0c.1 1.6.4 3.1 1 4.5.6 1.7 2.9 5.7 11.4 5.7 5 0 9.9-.6 14.8-1.7 0 0 0 0 0 0 .1 0 .1 0 .1 0 0 .1 0 .1 0 .1.1 0 .1 0 .1.1v5.6s0 .1-.1.1c0 0 0 0 0 .1-1.6 1.1-3.7 1.7-5.6 2.3-.8.3-1.6.5-2.4.7-7.5 1.7-15.4 1.3-22.7-1.2-6.8-2.4-13.8-8.2-15.5-15.2-.9-3.8-1.6-7.6-1.9-11.5-.6-5.8-.6-11.7-.8-17.5C3.9 24.5 4 20 4.9 16 6.7 7.9 14.1 2.2 22.3 1c1.4-.2 4.1-1 16.5-1h.1C51.4 0 56.7.8 58.1 1c8.4 1.2 15.5 7.5 16.6 15.6Z" fill="currentColor"/></svg> <div style="color: #787588; margin-top: 16px;">Post by @maartje@blahaj.social</div> <div style="font-weight: 500;">View on Mastodon</div> </a> </blockquote> <script data-allowed-prefixes="https://blahaj.social/" async src="https://blahaj.social/embed.js"></script>

  
  
![Demo of WAP browsing over MeshCore](https://cdn.blahaj.social/media_attachments/files/115/867/457/642/397/936/original/23d46f1aa937079e.jpeg)

## Overview

MAP provides needs two types of nodes:

- **Proxy Mode**: ESP32 connects to WiFi and acts as a WAP gateway, forwarding requests from the LoRa mesh to a WAPBox server (Kannel).
- **AP Mode**: ESP32 creates a WiFi access point where clients can make HTTP requests that get routed through a Proxy node on the mesh

```
┌─────────────┐              ┌─────────────┐         UDP    ┌─────────────┐
│  AP Node    │◄────────────►│ Proxy Node  │◄──────────────►│   WAPBox    │
│  (WiFi AP)  │   MeshCore   │(WiFi Client)│                │  (Kannel)   │
└─────────────┘              └─────────────┘                └─────────────┘
       ▲                                                           ▲ 
       │ WiFi                                                      │
       ▼                                                           ▼
┌─────────────┐                                            ┌─────────────┐
│ WAP Client  │                                            │ WAP Content │
│  (Browser)  │                                            │   Server    │
└─────────────┘                                            └─────────────┘
```


## Up and running

### Hardware

For now this only works with the **Heltec V3** board (ESP32 with OLED and LoRa). Other boards may work with some modifications, I simply don't have them to test with.

### WAP browsers

Due to the nature of most WAP devices capable of WML browsing having no WiFi and ESP32 not having GSM cell capabilities (oh hi radio band licensing), you will need a WAP browser that can connect over WiFi.

There are a few options:
- Nokia 6300i (not the classic 6300!) with WiFi and built-in WAP browser (to be tested, probably a rare unicorn in phones)
- Smartphone with J2ME emulator and WAP browser app (more below), they are noisy in requests! I tried to block most of them.
- Symbian phone with WiFi (like Nokia E65) and a J2ME WAP browser (older Symbian might have a built-in WML capable browser)
- Laptop with WAP emulator
- Why not contribute to our efforts on a modern WAP browser? (contact me on Mastodon) (did anyone say 3310 LoRa mod?)
- Any phone running MediaTek MAUI Mocor RTOS (HMD sells it as S30+) that might have WiFi. It has a full WML browser hidden, even if not available it can be triggered with a WAP Push message. (report if you find one!)

#### J2ME WAP browsers

*Be very upset here, for no good reason all J2ME browsers use proprietary proxies, shocker! Even before the days of data mining. Forget your Opera Mini.*

- **UC Browser 7.7** [JAR](./jar/UCBrowser.jar) - requires going to settings and disable `WAP Access via Server` to use direct WAP. Company and all servers are dead, all checks will fail, MAP blocks them anyway.

### What websites to visit?

We are working at [Bevelgacom](https://github.com/bevelgacom/) to create lots of WML content compatible with the earliest WAP devices and building the mordern WAP portal! Check it out on [http://wap.bevelgacom.be/](http://wap.bevelgacom.be/) (works with MAP too!).

### Prerequisites

- [PlatformIO](https://platformio.org/)
- [just](https://just.systems/) command runner (optional, for convenience)

### Configuration

1. Copy the secrets template:
   ```bash
   cp src/secrets.h.example src/secrets.h
   ```

2. Edit `src/secrets.h` with your configuration:
   ```cpp
   // For Proxy Mode - WiFi credentials
   #define WIFI_SSID     "YourWiFiSSID"
   #define WIFI_PASSWORD "YourWiFiPassword"
   
   // For AP Mode - Public key of your Proxy node
   // Flash your proxy node first, connect to serial press enter to generate and show pubkey
   #define PROXY_NODE_PUBKEY "21BDD77007F54EF3..."
   ```

3. Select operation mode for the device you will be flashing in `src/main.cpp`:
   ```cpp
   #define OPERATION_MODE MODE_PROXY  // or MODE_AP
   ```

4. Build and upload the firmware.



### LoRa Configuration

Default settings optimized for EU868 narrow:

| Parameter | Value |
|-----------|-------|
| Frequency | 869.617 MHz |
| Bandwidth | 62.50 kHz |
| Spreading Factor | SF8 |
| Coding Rate | 4/8 |
| TX Power | 22 dBm |

## Testing

Run the WAP library tests (requires g++):

```bash
# Unit tests
just test

# End-to-end test (requires network)
just test-e2e

# All tests
just test-all
```


## 🌍 WAPBox Configuration

MAP is designed to work with [Kannel](https://www.kannel.org/) as the WAP gateway. The default configuration points [Bevelgacom](https://bevelgacom.be) to:

- **Host**: `206.83.40.166` (bevelgacom public gateway)
- **Port**: `9200` (WAP sessionless mode)

To use your own WAPBox, modify `WAPBOX_HOST` and `WAPBOX_PORT` in `src/main.cpp`.
 
