/*
 * Mini WiFi Firmware
 * A alternative firmware for ESP8266 devices
 * Instead of using an AT command based firmware, you can use this one which
 * uses a binary protocol and does not require any parsing.
 *
 * Fotis Loukos <me@fotisl.com>
 */
#include <Arduino.h>

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266SSDP.h>
#include <WiFiUdp.h>

/*
 * Change the following if you want to support a biggest number of concurrent
 * clients.
 */
#define MAX_CLIENTS 5

ADC_MODE(ADC_VCC);

enum {
  WIFI_SSDP_DEVICE_TYPE,
  WIFI_SSDP_NAME,
  WIFI_SSDP_URL,
  WIFI_SSDP_SCHEMA_URL,
  WIFI_SSDP_SN,
  WIFI_SSDP_MODEL_NAME,
  WIFI_SSDP_MODEL_NUMBER,
  WIFI_SSDP_MODEL_URL,
  WIFI_SSDP_MANUFACTURER,
  WIFI_SSDP_MANUFACTURER_URL
};

WiFiClient *clients[MAX_CLIENTS], *httpstream;
WiFiClientSecure *sslclients[MAX_CLIENTS];
WiFiUDP Udp;
WiFiServer *server;
ESP8266WebServer *ssdpwebserver;
HTTPClient http;
boolean sockets[MAX_CLIENTS], sslsockets[MAX_CLIENTS];
boolean servessdp;
char ssdpschemaurl[32];

char syncread();
uint8_t readbuf(char *buf);
void readstr(char *buf);
void writebuf(char *buf, uint8_t len);
void writestr(char *buf);
int8_t getsock(boolean *sockets);
boolean verifysock(int8_t s, boolean *slist);

void setup() {
  int i;

  Serial.begin(9600);

  WiFi.mode(WIFI_STA);

  for(i = 0; i < MAX_CLIENTS; i++)
    sockets[i] = sslsockets[i] = false;
  server = NULL;
  ssdpwebserver = NULL;
  servessdp = false;
}

void loop() {
  char buf1[256], buf2[256], buf3[16], buf4[16];
  int i;
  uint32_t u32;
  uint16_t u16;
  uint8_t u8;
  int8_t i8, i8b;
  uint8_t mac[6];
  WiFiClient *tcpclient, sclient;
  WiFiClientSecure *sslclient;
  IPAddress IP1, IP2, IP3, IP4, IP5;

  if(servessdp)
    ssdpwebserver->handleClient();

  if(Serial.available()) {
    switch(Serial.read()) {
      /* Function 0x00: Ping */
      case 0x00:
        readbuf((char *) &i8);
        writebuf((char *) &i8, 1);
        break;
      /* Function 0x01: Reset */
      case 0x01:
        ESP.restart();
        break;
      /* Function 0x02: Set baud rate */
      case 0x02:
        readbuf((char *) &u32);
        Serial.begin(u32);
        break;
      /* Function 0x03: Get Chip ID */
      case 0x03:
        u32 = ESP.getChipId();
        writebuf((char *) &u32, 4);
        break;
      /* Function 0x04: Get VCC */
      case 0x04:
        u16 = ESP.getVcc();
        writebuf((char *) &u16, 2);
        break;
      /* Function 0x10: Connect to WiFi */
      case 0x10:
        readstr(buf1);
        readstr(buf2);
        WiFi.begin(buf1, buf2);
        WiFi.setAutoReconnect(true);
        WiFi.setAutoConnect(false);
        break;
      /* Function 0x11: Disconnect from wifi */
      case 0x11:
        WiFi.disconnect();
        break;
      /* Function 0x12: Get status */
      case 0x12:
        i8 = WiFi.status();
        writebuf((char *) &i8, 1);
        break;
      /* Function 0x13: Get IP config */
      case 0x13:
        readbuf((char *) &i8);
        switch(i8) {
          case 0:
            u32 = (uint32_t) WiFi.localIP();
            break;
          case 1:
            u32 = (uint32_t) WiFi.gatewayIP();
            break;
          case 2:
            u32 = (uint32_t) WiFi.subnetMask();
            break;
          case 3:
            u32 = (uint32_t) WiFi.dnsIP(0);
            break;
          case 4:
            u32 = (uint32_t) WiFi.dnsIP(1);
            break;
          default:
            u32 = 0xffffffff;
            break;
        }
        writebuf((char *) &u32, 4);
        break;
      /* Function 0x14: Set IP config */
      case 0x14:
        readbuf((char *) u32);
        IP1 = (uint32_t) u32;
        readbuf((char *) u32);
        IP2 = (uint32_t) u32;
        readbuf((char *) u32);
        IP3 = (uint32_t) u32;
        readbuf((char *) u32);
        IP4 = (uint32_t) u32;
        readbuf((char *) u32);
        IP5 = (uint32_t) u32;
        if(WiFi.config(IP1, IP2, IP3, IP4, IP5))
          i8 = 0;
        else
          i8 = -1;
        writebuf((char *) &i8, 1);
        break;
      /* Function 0x15: Get hostname */
      case 0x15:
        writestr((char *) WiFi.hostname().c_str());
        break;
      /* Function 0x16: Set hostname */
      case 0x16:
        readstr(buf1);
        if(WiFi.hostname(buf1))
          i8 = 0;
        else
          i8 = -1;
        writebuf((char *) &i8, 1);
        break;
      /* Function 0x17: Get MAC */
      case 0x17:
        strcpy(buf1, WiFi.macAddress().c_str());
        writestr(buf1);
        break;
      /* Function 0x18: Set MAC */
      case 0x18:
        readbuf((char *) mac);
        WiFi.macAddress(mac);
        break;
      /* Function 0x20: TCP connect */
      case 0x20:
        readstr(buf1);
        readbuf((char *) &u16);
        i8 = getsock(sockets);
        if(i8 == -1) {
          i8--;
          writebuf((char *) &i8, 1);
          break;
        }
        tcpclient = new WiFiClient();
        if(tcpclient->connect(buf1, u16)) {
          sockets[i8] = true;
          clients[i8] = tcpclient;
        } else {
          i8 = -1;
        }
        writebuf((char *) &i8, 1);
        break;
      /* Function 0x21: Check if client is connected */
      case 0x21:
        readbuf((char *) &i8);
        if(verifysock(i8, sockets) == false) {
          i8 = -1;
          writebuf((char *) &i8, 1);
          break;
        }
        if(clients[i8]->connected())
          i8 = 1;
        else
          i8 = 0;
        writebuf((char *) &i8, 1);
        break;
      /* Function 0x22: Close */
      case 0x22:
        readbuf((char *) &i8);
        if(verifysock(i8, sockets) == false) {
          i8 = -1;
          writebuf((char *) &i8, 1);
          break;
        }
        clients[i8]->stop();
        delete clients[i8];
        sockets[i8] = false;
        i8 = 0;
        writebuf((char *) &i8, 1);
        break;
      /* Function 0x23: Available */
      case 0x23:
        readbuf((char *) &i8);
        if(verifysock(i8, sockets) == false)
          i8 = -1;
        else
          i8 = (int8_t) clients[i8]->available();
        writebuf((char *) &i8, 1);
        break;
      /* Function 0x24: Write */
      case 0x24:
        readbuf((char *) &i8);
        i = readbuf(buf1);
        if(verifysock(i8, sockets) == false)
          i8 = -1;
        else
          i8 = (int8_t) clients[i8]->write((uint8_t *) buf1, (size_t) i);
        writebuf((char *) &i8, 1);
        break;
      /* Function 0x25: Read one */
      case 0x25:
        readbuf((char *) &i8);
        if(verifysock(i8, sockets) == false)
          i8 = -1;
        else
          i8 = (int8_t) clients[i8]->read();
        writebuf((char *) &i8, 1);
        break;
      /* Function 0x26: Read many */
      case 0x26:
        readbuf((char *) &i8);
        readbuf((char *) &i8b);
        if(verifysock(i8, sockets) == false) {
          i8 = -1;
          writebuf((char *) &i8, 1);
          break;
        }
        i8 = clients[i8]->read((uint8_t *) buf1, (size_t) i8b);
        writebuf((char *) &i8, 1);
        writebuf(buf1, i8);
        break;
      /* Function 0x30: SSL connect */
      case 0x30:
        readstr(buf1);
        readbuf((char *) &u16);
        i8 = getsock(sslsockets);
        if(i8 == -1) {
          i8--;
          writebuf((char *) &i8, 1);
          break;
        }
        sslclient = new WiFiClientSecure();
        if(sslclient->connect(buf1, u16)) {
          sslsockets[i8] = true;
          sslclients[i8] = sslclient;
        } else {
          i8 = -1;
        }
        writebuf((char *) &i8, 1);
        break;
      /* Function 0x31: Check if client is connected */
      case 0x31:
        readbuf((char *) &i8);
        if(verifysock(i8, sslsockets) == false) {
          i8 = -1;
          writebuf((char *) &i8, 1);
          break;
        }
        if(sslclients[i8]->connected())
          i8 = 1;
        else
          i8 = 0;
        writebuf((char *) &i8, 1);
        break;
      /* Function 0x32: Close */
      case 0x32:
        readbuf((char *) &i8);
        if(verifysock(i8, sslsockets) == false) {
          i8 = -1;
          writebuf((char *) &i8, 1);
          break;
        }
        sslclients[i8]->stop();
        delete sslclients[i8];
        sslsockets[i8] = false;
        i8 = 0;
        writebuf((char *) &i8, 1);
        break;
      /* Function 0x33: Available */
      case 0x33:
        readbuf((char *) &i8);
        if(verifysock(i8, sslsockets) == false)
          i8 = -1;
        else
          i8 = (int8_t) sslclients[i8]->available();
        writebuf((char *) &i8, 1);
        break;
      /* Function 0x34: Write */
      case 0x34:
        readbuf((char *) &i8);
        i = readbuf(buf1);
        if(verifysock(i8, sslsockets) == false)
          i8 = -1;
        else
          i8 = (int8_t) sslclients[i8]->write((uint8_t *) buf1, (size_t) i);
        writebuf((char *) &i8, 1);
        break;
      /* Function 0x35: Read one */
      case 0x35:
        readbuf((char *) &i8);
        if(verifysock(i8, sslsockets) == false)
          i8 = -1;
        else
          i8 = (int8_t) sslclients[i8]->read();
        writebuf((char *) &i8, 1);
        break;
      /* Function 0x36: Read many */
      case 0x36:
        readbuf((char *) &i8);
        readbuf((char *) &i8b);
        if(verifysock(i8, sslsockets) == false) {
          i8 = -1;
          writebuf((char *) &i8, 1);
          writebuf(NULL, 0);
          break;
        }
        i8 = sslclients[i8]->read((uint8_t *) buf1, (size_t) i8b);
        writebuf((char *) &i8, 1);
        writebuf(buf1, i8);
        break;
      /* Function 0x40: UDP send */
      case 0x40:
        readstr(buf1);
        readbuf((char *) &u16);
        i = readbuf(buf2);
        Udp.beginPacket(buf1, u16);
        Udp.write((uint8_t *) buf2, (size_t) i);
        Udp.endPacket();
        break;
      /* Function 0x41: Listen at port */
      case 0x41:
        readbuf((char *) &u16);
        i8 = (int8_t) Udp.begin(u16);
        writebuf((char *) &i8, 1);
        break;
      /* Function 0x42: Stop listening */
      case 0x42:
        Udp.stop();
        break;
      /* Function 0x43: Read packet */
      case 0x43:
        i = Udp.parsePacket();
        i = Udp.read(buf1, i);
        writebuf(buf1, i);
        break;
      /* Function 0x50: Start TCP server */
      case 0x50:
        readbuf((char *) &u16);
        if(server)
          delete server;
        server = new WiFiServer(u16);
        if(server) {
          i8 = 0;
          server->begin();
        } else {
          i8 = -1;
        }
        writebuf((char *) &i8, 1);
        break;
      /* Function 0x51: Stop listening */
      case 0x51:
        if(server) {
          server->stop();
          delete server;
          server = NULL;
          i8 = 0;
        } else {
          i8 = -1;
        }
        writebuf((char *) &i8, 1);
        break;
      /* Function 0x52: Receive connection */
      case 0x52:
        i8 = getsock(sockets);
        if(i8 == -1) {
          writebuf((char *) &i8, 1);
          break;
        }
        sclient = server->available();
        if(sclient) {
          sockets[i8] = true;
          clients[i8] = new WiFiClient(sclient);
        } else {
          i8 = -1;
        }
        writebuf((char *) &i8, 1);
        break;
      /* Function 0x60: Open HTTP connection and GET URL*/
      case 0x60:
        readstr(buf1);
        http.begin(buf1);
        i8 = http.GET();
        httpstream = http.getStreamPtr();
        writebuf((char *) &i8, 1);
        break;
      /* Function 0x61: Open HTTPS connection with fingerprint and GET URL */
      case 0x61:
        readstr(buf1);
        readstr(buf2);
        http.begin(buf1, buf2);
        i8 = http.GET();
        httpstream = http.getStreamPtr();
        writebuf((char *) &i8, 1);
        break;
      /* Function 0x62: Get length of reply */
      case 0x62:
        u32 = http.getSize();
        writebuf((char *) &u32, 4);
        break;
      /* Function 0x63: Get number of available bytes to read */
      case 0x63:
        u32 = httpstream->available();
        writebuf((char *) &u32, 4);
        break;
      /* Function 0x64: Read bytes */
      case 0x64:
        readbuf((char *) &u8);
        u8 = httpstream->readBytes(buf1, (u8 > sizeof(buf1)) ? sizeof(buf1) : u8);
        writebuf((char *) buf1, u8);
        break;
      /* Function 0x65: Close HTTP/HTTPS connection */
      case 0x65:
        http.end();
        break;
      /* Function 0x70: mDNS responder begin */
      case 0x70:
        readstr(buf1);
        if(MDNS.begin(buf1))
          i8 = 0;
        else
          i8 = -1;
        writebuf((char *) &i8, 1);
        break;
      /* Function 0x71: Add service */
      case 0x71:
        readstr(buf1);
        readstr(buf2);
        readbuf((char *) &u16);
        MDNS.addService(buf1, buf2, u16);
        break;
      /* Function 0x72: Add service TXT */
      case 0x72:
        readstr(buf3);
        readstr(buf4);
        readstr(buf1);
        readstr(buf2);
        MDNS.addServiceTxt(buf3, buf4, buf1, buf2);
        break;
      /* Function 0x73: Query for service */
      case 0x73:
        readstr(buf1);
        readstr(buf2);
        i8 = MDNS.queryService(buf1, buf2);
        writebuf((char *) &i8, 1);
        break;
      /* Function 0x74: Get query result */
      case 0x74:
        readbuf((char *) &u8);
        writestr((char *) MDNS.hostname(u8).c_str());
        u32 = (uint32_t) MDNS.IP(u8);
        writebuf((char *) &u32, 4);
        u16 = MDNS.port(u8);
        writebuf((char *) &u16, 2);
        break;
      /* Function 0x80: Enable SSDP */
      case 0x80:
        readbuf((char *) &u16);
        readbuf((char *) &u8);
        SSDP.setHTTPPort(u16);
        if(u8 != 0) {
          if(ssdpwebserver)
            delete ssdpwebserver;
          ssdpwebserver = new ESP8266WebServer(u16);
          ssdpwebserver->on(ssdpschemaurl, HTTP_GET, []() {
            SSDP.schema(ssdpwebserver->client());
          });
          ssdpwebserver->begin();
          servessdp = true;
        }
        SSDP.begin();
        break;
      /* Function 0x81: Stop SSDP Webserver */
      case 0x81:
        if(ssdpwebserver) {
          ssdpwebserver->stop();
          delete ssdpwebserver;
          servessdp = false;
        }
        break;
      /* Function 0x82: Set SSDP option */
      case 0x82:
        readbuf((char *) &u8);
        readstr(buf1);
        switch(u8) {
          case WIFI_SSDP_DEVICE_TYPE:
            SSDP.setDeviceType(buf1);
            break;
          case WIFI_SSDP_NAME:
            SSDP.setName(buf1);
            break;
          case WIFI_SSDP_URL:
            SSDP.setURL(buf1);
            break;
          case WIFI_SSDP_SCHEMA_URL:
            SSDP.setSchemaURL(buf1);
            if(buf1[0] != '/')
              snprintf(ssdpschemaurl, sizeof(ssdpschemaurl), "/%s", buf1);
            else
              strncpy(ssdpschemaurl, buf1, sizeof(ssdpschemaurl));
            break;
          case WIFI_SSDP_SN:
            SSDP.setSerialNumber(buf1);
            break;
          case WIFI_SSDP_MODEL_NAME:
            SSDP.setModelName(buf1);
            break;
          case WIFI_SSDP_MODEL_NUMBER:
            SSDP.setModelNumber(buf1);
            break;
          case WIFI_SSDP_MODEL_URL:
            SSDP.setModelURL(buf1);
            break;
          case WIFI_SSDP_MANUFACTURER:
            SSDP.setManufacturer(buf1);
            break;
          case WIFI_SSDP_MANUFACTURER_URL:
            SSDP.setManufacturerURL(buf1);
            break;
        }
        break;
    }
  }
  delay(1);
}

char syncread()
{
  while(!Serial.available())
    ;
  return (char) Serial.read();
}

uint8_t readbuf(char *buf)
{
  uint8_t i, len;

  len = (uint8_t) syncread();
  for(i = 0; i < len; i++)
    buf[i] = syncread();

  return len;
}

void readstr(char *buf)
{
  int len;

  len = readbuf(buf);
  buf[len] = '\0';
}

void writebuf(char *buf, uint8_t len)
{
  uint8_t i;

  Serial.write(len);
  for(i = 0; i < len; i++)
    Serial.write(buf[i]);
}

void writestr(char *buf)
{
  writebuf(buf, strlen(buf));
}

int8_t getsock(boolean *sockets)
{
  int8_t s;

  for(s = 0; s < MAX_CLIENTS; s++)
    if(sockets[s] == false)
      return s;

  return -1;
}

boolean verifysock(int8_t s, boolean *slist)
{
  if((s < 0) || (s >= MAX_CLIENTS) || (slist[s] == false))
    return false;
  return true;
}
