# ENV-GUCK
Environmental sensor monitoring via Web and ESP-NOW.

## C6-ENV-III-WEBSERVER
- ESP32-C6 + Env III Sensor 
- Connected to WiFi, works as Webserver
- Installed in workshop space
- Used for monitoring remotely

## C6-ENV-PRO-ESP-NOW-AP
- ESP32-C6 + Env Pro Sensor
- Works as an Access Point and web server
- Transmits sensor values via ESP-NOW 
- Installed in Automobile
- Used for monitoring the environment during car living

## CARDPUTER-ENV-PRO-STANDALONE
- M5Stack Cardputer ADV + ENV PRO Sensor
- Standalone operation without ESP32-C6

## CARDPUTER-ESP-NOW
NanoC6 measures and calculates the 24-hour MIN/MAX; Cardputer receives that data, maintains its own 24-hour history, displays everything, handles keyboard control, and saves screenshots to microSD.

- Cardputer ADV
- Receves sensor data from ESP32-C6 via ESP-NOW
- Using in the automobile

### Keys
| Key | Function |
|---|---|
| `1` | ENV PRO — current sensor values |
| `2` | 24HR MIN/MAX |
| `3` | HISTORY — press again to cycle T/H/P/G/IAQ |
| `4` | CONTROL |
| `N` | Next page |
| `B` | Cycle display brightness |
| `T` | History: Temperature |
| `H` | History: Humidity |
| `P` | History: Pressure |
| `G` | History: Gas Resistance |
| `I` | History: IAQ |
| `R` | Cycle history interval: 10s / 30s / 60s |
| `Fn + S` | Save screenshot to microSD |

### History

- `10s` — approximately 4 hours
- `30s` — approximately 12 hours
- `60s` — approximately 24 hours
- The first ESP-NOW packet immediately creates the first history point.
- History is stored locally on the Cardputer.
- 24HR MIN/MAX values are received directly from the NanoC6.

### System
## System Overview

```text
                         ENV-GUCK SYSTEM
              Environmental Monitoring via ESP-NOW

    ┌──────────────────────┐
    │       NanoC6         │
    │   ESP32-C6 + ENV PRO │
    │                      │
    │  Temperature         │
    │  Humidity            │
    │  Pressure            │
    │  Gas Resistance      │
    │  IAQ                 │
    │                      │
    │  24H MIN / MAX       │
    └──────────┬───────────┘
               │
               │ ESP-NOW
               │ sensorPayload
               ▼
    ┌──────────────────────────────┐
    │       CARDPUTER ADV          │
    │                              │
    │      ESP-NOW RECEIVER        │
    │              │               │
    │              ▼               │
    │     ┌──────────────────┐     │
    │     │ Latest Data      │     │
    │     │                  │     │
    │     │ T / H / P / GAS  │     │
    │     │ IAQ              │     │
    │     └────────┬─────────┘     │
    │              │               │
    │       ┌──────┴──────┐        │
    │       ▼             ▼        │
    │   MIN / MAX      HISTORY     │
    │   from NanoC6    local       │
    │                     │        │
    │                     ▼        │
    │                 1440 points  │
    │                 ≈ 24 hours   │
    │                              │
    │     ┌──────────────────┐     │
    │     │     DISPLAY      │     │
    │     │                  │     │
    │     │  1 ENV PRO       │     │
    │     │  2 MIN/MAX       │     │
    │     │  3 HISTORY       │     │
    │     │  4 CONTROL       │     │
    │     └────────┬─────────┘     │
    │              │               │
    │              ▼               │
    │          KEYBOARD            │
    │                              │
    │  1  ENV PRO                  │
    │  2  MIN/MAX                  │
    │  3  HISTORY                  │
    │  4  CONTROL                  │
    │  N  Next page                │
    │  B  Brightness               │
    │  T  Temperature              │
    │  H  Humidity                 │
    │  P  Pressure                 │
    │  G  Gas                      │
    │  I  IAQ                      │
    │  R  History interval         │
    │  Fn+S  Screenshot            │
    │                              │
    │              │               │
    │              ▼               │
    │          microSD             │
    │          shotXXX.bmp         │
    └──────────────────────────────┘
```

