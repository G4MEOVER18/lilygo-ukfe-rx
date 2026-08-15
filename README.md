# G4MEOVER LilyGo T-Dongle S3 — ESP-NOW ukfe_rf-Empfänger

WiFi-Satellit des G4MEOVER-Ökosystems. Empfängt die signierten `ukfe_rf`-Befehle
vom **WROOM-Relay** per **ESP-NOW** (2,4 GHz), verifiziert sie (keyed MAC + CRC16 +
Rolling-Counter) und führt sie aus. Die Stärke des ESP32-S3: **natives USB-HID** —
der Dongle tippt Payloads als BadUSB-Tastatur am Zielrechner.

```
WROOM-Relay ──ESP-NOW(Kanal 1)──► LilyGo T-Dongle S3 ──USB-HID──► Ziel-PC
                 ukfe_rf-Frame           validiert          DuckyScript-artig
```

Gemeinsames `ukfe_rf.c` mit Flipper/WROOM/Heltec — **ein Vokabular über alle
Transporte**. Kein 868/LoRa (das ist Heltecs Rolle); der LilyGo ist reiner
2,4-GHz-Empfänger + USB-Penetrator.

## Konfiguration
| Parameter | Wert |
|---|---|
| ESP-NOW-Kanal | 1 (`ESPNOW_CHANNEL`, = WROOM-Relay) |
| Secret | 16 B, identisch mit RF_SECRET/UKFE_SECRET |
| USB | natives S3-USB als HID-Keyboard (`ARDUINO_USB_MODE=0`) |

## HID-Payloads (`hid_payload()`)
Marker · Notepad · PowerShell · CMD-Marker · Lock — per `Trigger`/`PayloadRun` mit idx.
Eigene autorisierte Payloads dort ergänzen.

## Bauen & Flashen
```
pio run
pio run -t upload            # LilyGo per USB (Download-Modus falls Auto-Reset zickt)
pio device monitor -b 115200
```
Test: WROOM-CLI `ping` → hier `ESPNOW OK cmd=0x01` (wie beim Heltec).

## Offen
- TFT-Statuskopf (ST7735, T-Dongle) — aktuell Serial/LED.
- WiFi-Angriffe (Marauder-Bridge) für `WifiDeauth`/`EvilPortal`/`BeaconSpam`.

> Nur für autorisierte Sicherheitstests auf eigenen Geräten.
