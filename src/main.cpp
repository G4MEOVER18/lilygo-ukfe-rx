// G4MEOVER LilyGo T-Dongle S3 — ESP-NOW ukfe_rf-Empfaenger + USB-HID-Penetrator
// ---------------------------------------------------------------------------
// Satelliten-Ebene des G4MEOVER-Oekosystems. Empfaengt die signierten ukfe_rf-
// Befehle vom WROOM-Relay per ESP-NOW (2.4 GHz), verifiziert sie (keyed MAC +
// CRC16 + Rolling-Counter) und fuehrt sie aus. Die S3-Staerke: **natives USB-HID**
// als BadUSB-Tastatur am Zielrechner. Gemeinsames ukfe_rf.c mit Flipper/WROOM/Heltec.
//
// Nur fuer autorisierte Sicherheitstests auf eigenen Geraeten.
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "USB.h"
#include "USBHIDKeyboard.h"
#include <TFT_eSPI.h>      // ST7735 80x160 des T-Dongle S3 (Config in platformio.ini)

extern "C" {
#include "ukfe_rf.h"
}
#include "ble_spam.h"   // BLE-Spam (Apple/Windows/Android) + BLE-Scan via NimBLE

USBHIDKeyboard Keyboard;   // natives S3-USB als HID-Tastatur
TFT_eSPI tft = TFT_eSPI(); // Status-Display (visuelle Bestaetigung ohne Serial)

// Kopfzeile + Status aufs TFT. col: TFT_GREEN/TFT_YELLOW/...
static void tft_show(const char* l1, const char* l2, const char* l3, uint16_t col) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setTextFont(2);
    tft.setCursor(4, 2);  tft.print("G4MEOVER LilyGo");
    tft.setTextColor(col, TFT_BLACK);
    tft.setCursor(4, 24); tft.print(l1);
    tft.setCursor(4, 44); tft.print(l2);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.setCursor(4, 66); tft.print(l3);
}

// Gemeinsames Geheimnis — IDENTISCH mit RF_SECRET/UKFE_SECRET (out-of-band pairen!).
static const uint8_t UKFE_SECRET[UKFE_RF_SECRET_LEN] = {
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
    0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,
};

// ---- ESP-NOW-Empfang (Callback setzt nur Flag; HID/Delay laufen in loop) ----
static volatile bool enowFlag = false;
static uint8_t  enowBuf[UKFE_RF_MAX_FRAME];
static volatile int enowLen = 0;
static uint32_t enowCounter = 0;     // Anti-Replay-Fenster (WiFi)
static uint32_t rxCount = 0, okCount = 0;
static uint8_t  enowSenderMac[6] = {0};   // MAC des WROOM (fuer ACK-Rueckweg)
static uint32_t respCounter = 0;

void onEspNowRecv(const uint8_t* mac, const uint8_t* data, int len) {
    if(enowFlag) return;
    if(len <= 0 || len > (int)UKFE_RF_MAX_FRAME) return;
    memcpy(enowSenderMac, mac, 6);
    memcpy(enowBuf, data, len);
    enowLen = len;
    enowFlag = true;
}

// Signierter ACK per ESP-NOW zurueck an den WROOM -> Deck sieht das Ergebnis
// (auch wenn der S3-USB-CDC still ist).
void send_espnow_ack(const uint8_t* mac, uint8_t orig_cmd, uint8_t result) {
    UkfeRfMessage m;
    m.cmd = UkfeRfRespAck; m.arg_len = 2;
    m.args[0] = orig_cmd; m.args[1] = result;
    m.counter = ++respCounter;
    uint8_t frame[UKFE_RF_MAX_FRAME];
    size_t n = ukfe_rf_build_frame(UKFE_SECRET, &m, frame, sizeof(frame));
    if(!n) return;
    if(!esp_now_is_peer_exist(mac)) {
        esp_now_peer_info_t p = {};
        memcpy(p.peer_addr, mac, 6);
        p.channel = ESPNOW_CHANNEL; p.encrypt = false;
        esp_now_add_peer(&p);
    }
    esp_now_send(mac, frame, n);
}

// ---- HID-Payload-Bibliothek (benannt, erweiterbar). Nur autorisierte Tests. ----
static void win_r(const char* cmd) {
    Keyboard.press(KEY_LEFT_GUI); Keyboard.press('r');
    delay(120); Keyboard.releaseAll(); delay(400);
    Keyboard.println(cmd);
}
static void pl_marker()     { Keyboard.println("G4MEOVER-LilyGo HID online"); }
static void pl_notepad()    { win_r("notepad"); }
static void pl_powershell() { win_r("powershell"); }
static void pl_cmd_marker() { win_r("cmd /k echo G4MEOVER pentest marker"); }
static void pl_lock()       { Keyboard.press(KEY_LEFT_GUI); Keyboard.press('l');
                              delay(80); Keyboard.releaseAll(); }

typedef struct { const char* name; void (*run)(); } HidPayload;
static const HidPayload PAYLOADS[] = {
    {"Marker", pl_marker}, {"Notepad", pl_notepad}, {"PowerShell", pl_powershell},
    {"CMD Marker", pl_cmd_marker}, {"Lock", pl_lock},
};
#define PAYLOAD_COUNT (sizeof(PAYLOADS) / sizeof(PAYLOADS[0]))

static const char* hid_payload(uint8_t idx) {
    delay(300);  // Host-Enumeration abwarten
    if(idx >= PAYLOAD_COUNT) return "?";
    PAYLOADS[idx].run();
    return PAYLOADS[idx].name;
}

// ---- Befehls-Dispatcher (gleiche Handler-Semantik wie Heltec) ----
void act(const UkfeRfMessage* m) {
    switch(m->cmd) {
    case UkfeRfCmdTrigger:
    case UkfeRfCmdPayloadRun: {
        uint8_t idx = m->arg_len ? m->args[0] : 0;
        const char* pn = hid_payload(idx);
        Serial.printf("HID getippt: idx=%u (%s)\n", idx, pn);
        break;
    }
    case UkfeRfCmdWifiDeauth:  Serial.println("CMD WIFI DEAUTH (TODO: Marauder-Bridge)"); break;
    case UkfeRfCmdEvilPortal:  Serial.println("CMD EVIL PORTAL (TODO)"); break;
    case UkfeRfCmdBeaconSpam:  Serial.println("CMD BEACON SPAM (TODO)"); break;
    case UkfeRfCmdBleScan: {
        uint8_t n = ble_scan(m->arg_len ? (uint32_t)m->args[0] * 1000 : 3000);
        Serial.printf("BLE SCAN: %u Geraete\n", n);
        char l2[24]; snprintf(l2, sizeof(l2), "%u Geraete", n);
        tft_show("BLE SCAN", l2, "siehe Serial", TFT_CYAN);
        break;
    }
    case UkfeRfCmdBleSpam: {
        uint8_t mode = m->arg_len ? m->args[0] : 0;
        ble_spam_start(mode);                     // BLE koexistiert mit ESP-NOW -> Abort erreicht uns
        Serial.println("BLE SPAM laeuft");
        tft_show("BLE SPAM", "laeuft", "Abort=stop", TFT_YELLOW);
        break;
    }
    case UkfeRfCmdSourApple:
        ble_spam_start(1);                        // Apple-only Proximity-Spam
        tft_show("SOUR APPLE", "Apple-Spam", "Abort=stop", TFT_YELLOW);
        break;
    case UkfeRfCmdBleSniff:  Serial.println("BLE SNIFF (noch nicht impl.)"); break;
    case UkfeRfCmdAbort:
        ble_spam_stop();
        tft_show("ABORT", "BLE-Spam gestoppt", "", TFT_GREEN);
        break;
    default: Serial.printf("CMD 0x%02X alen=%u\n", m->cmd, m->arg_len); break;
    }
}

void setup() {
    Serial.begin(115200);
    Keyboard.begin();
    USB.begin();

    // TFT zuerst — sichtbarer Lebensbeweis, auch wenn USB-CDC still bleibt.
    tft.init();
    tft.setRotation(1);           // Querformat 160x80
    tft_show("Init...", "", "ESP-NOW Kanal 1", TFT_YELLOW);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    bool ok = (esp_now_init() == ESP_OK);
    if(ok) esp_now_register_recv_cb(onEspNowRecv);
    // Hinweis: esp_wifi_set_ps() NICHT vor esp_now_init aufrufen -> crasht S3
    // (USB-Drop/schwarzes TFT). Power-Save-Handling später sicher nachrüsten.

    Serial.printf("\nG4MEOVER LilyGo UKFE-RX bereit. ESP-NOW(Kanal %d, %s) + USB-HID.\n",
                  ESPNOW_CHANNEL, ok ? "an" : "AUS");
    Serial.printf("STA-MAC %s\n", WiFi.macAddress().c_str());

    char mac[20]; snprintf(mac, sizeof(mac), "%s", WiFi.macAddress().c_str());
    tft_show(ok ? "BEREIT" : "ESP-NOW AUS", "warte auf Frame", mac,
             ok ? TFT_GREEN : TFT_RED);
}

void loop() {
    ble_spam_tick();   // falls BLE-Spam aktiv: naechstes Advertisement senden
    if(!enowFlag) { if(!ble_spam_active()) delay(2); return; }
    int len = enowLen;
    uint8_t frame[UKFE_RF_MAX_FRAME];
    memcpy(frame, enowBuf, len);
    enowFlag = false;
    rxCount++;

    size_t real_len = (size_t)frame[0] + 1;
    if(real_len > (size_t)len) real_len = len;
    UkfeRfMessage msg;
    if(ukfe_rf_parse_frame(UKFE_SECRET, frame, real_len, &msg, &enowCounter)) {
        okCount++;
        Serial.printf("ESPNOW OK cmd=0x%02X counter=%lu (rx:%lu ok:%lu)\n",
                      msg.cmd, (unsigned long)msg.counter,
                      (unsigned long)rxCount, (unsigned long)okCount);
        char l1[24], l3[24];
        snprintf(l1, sizeof(l1), "OK cmd=0x%02X", msg.cmd);
        snprintf(l3, sizeof(l3), "ctr=%lu ok=%lu", (unsigned long)msg.counter,
                 (unsigned long)okCount);
        tft_show(l1, "ESPNOW +ACK", l3, TFT_GREEN);
        act(&msg);
        send_espnow_ack(enowSenderMac, msg.cmd, 0);   // ACK zurueck an WROOM/Deck
    } else {
        Serial.println("ESPNOW PARSE FAIL (MAC/CRC/Counter)");
    }
}
