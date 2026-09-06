<p align="center">
  <img src="docs/images/crimson_kestrel_banner.png" alt="CrimsonKestrel Banner" width="100%">
</p>

# CrimsonKestrel: Open-Source Weather & Solar Edge Node (v1.15.4)


<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-crimson.svg" alt="License"></a>
  <a href="firmware/CrimsonKestrel/CrimsonKestrel.ino"><img src="https://img.shields.io/badge/firmware-v1.15.4-crimson.svg" alt="Firmware Version"></a>
  <a href="BOM.md"><img src="https://img.shields.io/badge/hardware-ESP32--C6-crimson.svg" alt="Hardware"></a>
  <a href="https://github.com/espressif/arduino-esp32"><img src="https://img.shields.io/badge/platform-Arduino%20ESP32-crimson.svg" alt="Platform"></a>
</p>

<p align="left">
  <img src="docs/images/crimson_kestrel_logo.png" alt="CrimsonKestrel Logo" width="130" align="right">
</p>

This repository contains the production-grade reference architecture for a solar-boosted, low-power environmental monitoring station built around the **ESP32-C6 RISC-V SoC**. 

Rather than a fragile "maker-grade" loop sketch, this firmware utilizes a high-reliability **Single-Shot Boot Execution Pattern** paired with robust hardware and software safety nets designed to create an incredibly reliable, "set-it-and-forget-it" testing platform.

---

## 🏗️ System Architecture: The Hybrid Edge Pattern

To show quality design decisions while keeping the build highly accessible for makers, the system is architected as an overly robust, modular R&D platform. Instead of a single rigid design, this approach supports a wide range of interchangeable sensors for testing, availability, and budget. This allows anyone from any environment to build a simple, low-cost weather station using parts they already have or can easily afford, while separating physical environmental data collection from heavy data processing:

```
[ Outdoor ESP32-C6 Node ] 
       │ 
       │  (1. Single-Shot Boot: Sample -> Connect -> Decoupled JSON Payloads -> Deep Sleep)
       ▼ 
[ Indoor Gateway (Raspberry Pi @ 192.168.0.245) ]
       │
       ├──► [ MQTT Broker (Mosquitto) ]
       ├──► [ Time-Series Database (InfluxDB) ]
       └──► [ Visualization Dashboard (Grafana) ]
```

* **The Edge Node:** Wakes up from hardware deep sleep, takes rapid, averaged sensor samples, negotiates Wi-Fi/MQTT sockets, fires decoupled, nested JSON payloads, and drops back into deep sleep.
* **The Gateway:** Lives indoors on permanent wall power. It handles heavy networking, data storage, third-party weather aggregator API pushing (such as Weather Underground, CWOP, or Home Assistant), and visualization dashboards.

---

## 📂 Repository Directory Tree

A clean, decoupled directory structure immediately communicates professional maturity:

```text
CrimsonKestrel-edge/
├── .gitignore                      <-- Publicly ignores secrets.h
├── LICENSE                         <-- MIT License
├── README.md                       <-- This document
├── BOM.md                          <-- Standalone Bill of Materials & Sourcing Guide
├── [BRINGUP.md](BRINGUP.md)                   <-- Prototype Assembly & First-Time Bring-Up Guide
├── [docs/](docs/)
│   ├── [images/](docs/images/)
│   │   ├── crimson_kestrel_banner.png
│   │   ├── crimson_kestrel_logo.png
│   │   └── [prototype.jpg](docs/images/prototype.jpg)    <-- Real-world cardboard-mounted prototype photo
│   └── [adr/](docs/adr/)
│       ├── [0001-hybrid-edge.md](docs/adr/adr-0001-hybrid-edge.md)
│       ├── [0002-device-observability.md](docs/adr/adr-0002-device-observability.md)
│       ├── [0003-decoupled-schemas.md](docs/adr/adr-0003-decoupled-schemas.md)
│       └── [0004-framework-selection.md](docs/adr/adr-0004-framework-selection.md)
└── firmware/
    └── CrimsonKestrel/
        ├── CrimsonKestrel.ino      <-- Rebranded and updated v1.15.4 Arduino source code
        ├── secrets.h               <-- Private local Wi-Fi and MQTT credentials
        └── secrets.example.h       <-- Public configuration template
└── [gateway/](gateway/)
    └── [crimson_kestrel_daemon.py](gateway/crimson_kestrel_daemon.py) <-- Paho-MQTT gateway subscriber & log rotator
```

---

## 🛡️ Core Firmware Safeguards (The "Bulletproofing")

Let's be honest: nobody wants to drag a 20-foot ladder out to their roof in the middle of a freezing downpour just to press a tiny physical reset button because the Wi-Fi hiccuped or an I2C sensor locked up. To prevent those unnecessary roof-climbing adventures and keep your station running 24/7, the firmware includes several robust, built-in safety nets:

### 1. Hardware Watchdog Timer (WDT)
If an I2C transaction hangs due to electrical noise, or a Wi-Fi handshake locks up, the hardware watchdog automatically triggers a full system reset.
* **Implementation:** The ESP32-C6's internal Main System Watchdog Timer (MWDT) is initialized at boot with a **15-second timeout**. The watchdog is fed continuously through the execution path; if any task hangs, the hardware reboots the processor to recover.

### 2. Connection-Timeout & Graceful Fallbacks
A major failure mode in field deployments is a node getting stuck trying to connect to an offline router, draining its battery in minutes.
* **Implementation:** The Wi-Fi connection loop uses a non-blocking timeout. If connection is not established within **10 seconds**, the code gracefully halts, aborts transmission, configures low-power states, and goes to sleep.

### 3. Data Sanitization & Range Validation
Physical sensors can report NaN (Not a Number) or severe spikes during startup transients or low-voltage states.
* **Implementation:** Outlier range validation is run on all telemetry before packaging:
  * **DHT22:** Rejects relative humidity readings outside $0.0\%$ to $100.0\%$ and NaN states.
  * **INA219:** Rejects bus voltage readings outside $0.0\text{V}$ to $26.0\text{V}$ (max chip limit) or NaN states.
  * If a reading is invalid, the node logs the error, flags the payload as invalid, and bypasses transmission to protect database hygiene.

### 4. Cloud-Driven Debug Sleep Override State
During active workspace debugging, waiting 5 minutes between iterations slows down development. The node supports dynamic state shifting:
* On boot, the node subscribes to a command topic. If it reads a retained `"ON"` debug command, it overwrites its standard 5-minute deep sleep timer with a rapid **5-second cycle** and pulses its RGB LED to signal active debug mode. This allows you to observe full boot-up sequences, Wi-Fi negotiations, and sleep routines rapidly at your desk.

---

## 🚦 Diagnostic Device Observability (WS2812 LED)


### 🎨 WS2812 Visual Status LED Color Codes

To assist in local debugging and bring-up without having a serial monitor attached, the onboard WS2812 LED flashes standard, high-density color-coded states. Because these color codes are documented directly using GitHub-compatible hex swatches, they will render as beautifully colored visual dots directly on your GitHub landing page:

| LED State | Visual Swatch | Hex Code | R, G, B | System State & Meaning |
| :--- | :--- | :--- | :--- | :--- |
| **Solid Orange** | 🟧 ` #964B00 ` | `#964B00` | `150, 75, 0` | **System Booting:** Microcontroller powering up, establishing internal clock registers, and configuring hardware peripherals. |
| **Pulsing Yellow** | 🟨 ` #964B00 ` | `#964B00` | `150, 75, 0` (pulse) | **Wi-Fi Negotiating:** Active, non-blocking connection attempt to your local Wi-Fi gateway router. |
| **Flash Green** | 🟩 ` #009600 ` | `#009600` | `0, 150, 0` | **Publish Successful:** Environmental and system power telemetry successfully packaged and published to your MQTT broker. |
| **Solid Purple** | 🟪 ` #640064 ` | `#640064` | `100, 0, 100` | **Maintenance Window:** Active 2-second idle window listening on command topics for OTA override packages. |
| **Pulsing Cyan** | ⬜ ` #009696 ` | `#009696` | Variable | **Debug Loop Active:** Dynamic MQTT debug mode triggered; deep sleep is overridden to a rapid 5-second sampling loop. |
| **Solid Blue** | 🟦 ` #000096 ` | `#000096` | `0, 0, 150` | **OTA Mode Engaged:** Maintenance Mode active; the web-server is open and awaiting wireless firmware binaries. |
| **Pulsing Blue** | 🟦 ` #000096 ` | `#000096` | `0, 0, pulse` | **Active OTA Download:** High-speed wireless download of new compiled `.bin` firmware packets. |
| **Solid Cyan** | ⬜ ` #009696 ` | `#009696` | `0, 150, 150` | **Flash Write:** Actively committing new firmware blocks to local partition slots. |
| **Solid Green** | 🟩 ` #00FF00 ` | `#00FF00` | `0, 255, 0` | **Flash Success:** Firmware update completely written and verified; system is rebooting into the new software layer. |
| **Solid Red** | 🟥 ` #960000 ` | `#960000` | `150, 0, 0` | **System Error:** Hardware initialization failure (DHT22 missing, INA219 offline), or data sanitization outlier rejected. |
| **Fast Red Flash** | 🟥 ` #960000 ` | `#960000` | `150, 0, 0` (fast) | **Network Timeout:** Wi-Fi connection timed out or MQTT broker unreachable. Pipeline gracefully aborted to protect battery. |


## 🔒 Safe Credentials Abstraction Pattern

To keep your private home router credentials and server IP addresses completely safe from leakages on public repositories, **CrimsonKestrel** uses a professional **Safe Include Pattern**. 

Inside the main firmware sketch, the C++ preprocessor dynamically checks for the presence of a local, untracked `secrets.h` file:

```cpp
#if __has_include("secrets.h")
  #include "secrets.h"         // Pulls actual credentials locally
#else
  #include "secrets.example.h" // Fallback template so repo compiles out-of-the-box
#endif
```

By adding `secrets.h` and `*secrets.h` to your `.gitignore` file, your private information remains strictly on your local PC (`DIBShip`) while others can easily clone the repo and compile it using the provided `secrets.example.h` blueprint!

---

## 📊 Decoupled JSON Telemetry Schemas (v1.15.4)

To maintain a clean database structure and separate transient environmental states from system diagnostics, the node publishes two distinct JSON payloads during its single-shot boot routine:

### 1. Ambient Weather Telemetry
Pushed directly to your database parser to record ambient environmental updates:

**Topic:** `telemetry/weather_node_01/weather`
```json
{
  "client_id": "weather_node_01",
  "environment": {
    "temperature_c": 19.6,
    "humidity_pct": 38.7
  }
}
```

### 2. Device System & Power Observability
Pushed to your system health logger to track battery charge levels and hardware status:

**Topic:** `telemetry/weather_node_01/system`
```json
{
  "client_id": "weather_node_01",
  "system": {
    "version": "1.15.3",
    "uptime_ms": 3594
  },
  "power": {
    "bus_voltage_v": 3.93,
    "load_current_ma": 30.4,
    "shunt_voltage_mv": 3.04
  }
}
```

---

## 🔧 Arduino IDE & Hardware Configurations

To flash this firmware cleanly and unlock full hardware capabilities:

1. **Board Package:** Install **ESP32 Core by Espressif Systems (v3.0.0+)** via the Boards Manager.
2. **Flash Size:** Select **16MB (128Mb)** under *Tools > Flash Size*.
3. **Partition Scheme (CRITICAL):** Change this from the default "RainMaker 4MB" to **`16M Flash (3MB APP/9.9MB FATFS)`**.
   * *Why:* This leaves 3MB of space for heavy application binaries (allowing room for future sensor libraries) and carves out **9.9 Megabytes of local File System space (FATFS)** for offline caching.
4. **USB CDC On Boot:** Set to **Enabled** to allow direct `Serial.print()` routing over the built-in USB/JTAG controller without an external programmer.
5. **Erase All Flash Before Sketch Upload:** Set to **Disabled** to prevent erasing your local Wi-Fi profiles or FATFS files across programming cycles.

---

## ⚙️ Gateway & Data Ingestion Pipeline

To show a complete end-to-end proof of concept, this repository includes a dedicated Python daemon to run on your indoor gateway server. The **[crimson_kestrel_daemon.py](gateway/crimson_kestrel_daemon.py)** acts as the database-ingestion tier. It subscribes to both weather and system topics, performs strict schema validation and safety checks in real-time, and handles rotative CSV logging natively.

---

## 🛠️ Standalone Sourcing and Documentation Assets

To keep the repository clean and avoid support overhead, we maintain detailed, standalone documents for hardware assembly and components:

*   **[BOM.md](BOM.md)**: Complete, itemized Bill of Materials with pricing, manufacturer part numbers, sourcing links, and direct-solder wiring maps.
*   **[BRINGUP.md](BRINGUP.md)**: Prototype Assembly & First-Time Flashing Guide detailing physical breadboard layout, star grounding, sensor pull-ups, and step-by-step firmware installation.
*   **[gateway/crimson_kestrel_daemon.py](gateway/crimson_kestrel_daemon.py)**: Production-grade Python ingestion daemon that subscribes to raw MQTT streams, validates payload metrics, and commits structured outputs to rolling daily logs. detailing physical breadboard layout, star grounding, sensor pull-ups, and step-by-step firmware installation.
*   **[docs/images/prototype.jpg](docs/images/prototype.jpg)**: High-resolution photo of our card-mounted rapid prototyping bench used to validate logic states and bus connections during development.
*   **[docs/adr/](docs/adr/)**: Formal Architectural Decision Records (ADRs) documenting core design decisions:
    *   **[ADR-0001: The Hybrid Edge Architecture](docs/adr/adr-0001-hybrid-edge.md)**: Moving heavy processing indoors to achieve a microamp sleep baseline outdoors.
    *   **[ADR-0002: Hardware-Based Device Observability](docs/adr/adr-0002-device-observability.md)**: Opting for register-gated I2C power monitoring over power-bleeding resistive dividers.
    *   **[ADR-0003: Decoupled Multi-Topic Telemetry Schemas](docs/adr/adr-0003-decoupled-schemas.md)**: Separating ambient environmental metrics from transient system diagnostics.
    *   **[ADR-0004: Development Framework Selection](docs/adr/adr-0004-framework-selection.md)**: Selecting the Arduino ESP32 Core over ESP-IDF or Zephyr RTOS for rapid prototyping and accessibility.


## 📊 Gateway & Data Ingestion Pipeline

To provide complete, Staff-level end-to-end system validation, **CrimsonKestrel** includes a decoupled ingestion and database tier that resides on your indoor gateway server (like a permanently powered Raspberry Pi):

1.  **`crimson_kestrel_daemon.py`**: A robust, class-based Python background daemon that subscribes to all node topics via wildcards (`telemetry/+/weather` and `telemetry/+/system`), validates payloads against strict sanity ranges, logs data to daily rolling CSV files, and streams metrics directly to InfluxDB.
2.  **`docker-compose.yml`**: A single-command orchestration stack that spins up **Mosquitto** (MQTT), **InfluxDB 2.7** (Time-Series Database), and **Grafana** (Visualization UI).
3.  **`grafana_dashboard.json`**: A pre-configured, beautiful Grafana Dashboard template that you can import with a single click to instantly chart live temperature, humidity, battery charge levels, and system health metrics.

---

### 🚀 1. Spin Up the Ingestion Stack

On your gateway server, from the repo root, copy `.env.example` to `.env` and fill in real credentials (InfluxDB admin user/password/org/bucket/token, Grafana admin user/password), then spin up the Docker containers:

```bash
cp .env.example .env
# edit .env with your own credentials
docker compose up -d
```

This will instantly initialize:
*   **Mosquitto MQTT Broker** listening on port `1883`.
*   **InfluxDB 2.7** on port `8086` (organization/bucket as configured in `.env`).
*   **Grafana** on port `3000` (credentials as configured in `.env`).

---

### 🐍 2. Run the Telemetry Daemon

Ensure the `influxdb-client` and `paho-mqtt` packages are installed on your gateway:
```bash
pip install paho-mqtt influxdb-client
```

Now execute the daemon, pointing it to your local Mosquitto broker and your newly spun-up InfluxDB instance, using the same values you set in `.env`:
```bash
python3 gateway/crimson_kestrel_daemon.py   --host localhost   --port 1883   --outdir ./data   --influx-url http://localhost:8086   --influx-token "<INFLUX_TOKEN from .env>"   --influx-org "<INFLUX_ORG from .env>"   --influx-bucket "<INFLUX_BUCKET from .env>"
```

The daemon will now intercept, validate, and write every incoming single-shot MQTT transmission to BOTH your daily local CSV log files and InfluxDB simultaneously!

---

### 🎨 3. View Your Grafana Dashboard

The InfluxDB data source and the `gateway/grafana_dashboard.json` dashboard are both auto-provisioned on
startup — no manual "Add data source" or "Import" steps needed. Grafana reads them from
`grafana/provisioning/` (mounted into the container by `docker-compose.yml`), with the data source's
organization/token/bucket pulled straight from your `.env` file.

1.  Open your browser and navigate to `http://<GATEWAY_IP>:3000` (log in using the `GRAFANA_ADMIN_USER` / `GRAFANA_ADMIN_PASSWORD` you set in `.env`).
2.  Go to **Dashboards** and open **CrimsonKestrel Weather Station Dashboard** — it's already there.

You will instantly be presented with a responsive, professional dashboard displaying your ambient environmental indices and live battery curves side-by-side!


## 🧠 Technical Learnings & Post-Mortem: The mDNS Underscore Bug

During initial integration testing of the wireless update (OTA) path on the **ESP32-C6**, the edge node successfully received MQTT command triggers, printed the callback receipt, and immediately crashed/went offline (`status offline`) in a permanent bootloop.

### 🔍 Root Cause Analysis
This failure was traced back to a strict compliance check inside the modern **ESP-IDF v5.1+ / Arduino ESP32 Core v3.0+** network stack:
1. **RFC 1035 Hostname Spec Violation:** Under RFC 1035 (the domain name specification), hostnames are strictly restricted to alphanumeric characters and hyphens (`-`). **Underscores (`_`) are illegal** in network hostnames.
2. **The Underscore Crash:** The codebase configured the mDNS hostname using the main MQTT client ID: `ArduinoOTA.setHostname(MQTT_CLIENT_ID);`, which resolved to `"weather_node_01"`. 
3. **Internal Assertion Panic:** Upon calling `ArduinoOTA.begin()`, the ESP32-C6 attempted to register the mDNS service. The strict underlying ESP-IDF stack encountered the illegal underscore, resulting in a silent assertion failure, memory panic, and an immediate hardware watchdog or register-level CPU reset.
4. **Retained Message Loop:** Because the OTA trigger command was sent to the MQTT broker with the **retained** flag, the broker automatically re-published the `"ON"` state to the board as soon as it booted back up and reconnected, trapping the firmware in an infinite crash-on-boot cycle.

### 🛡️ Core Remedies Implemented in v1.15.4
* **mDNS Sanitization:** The mDNS setup task has been updated to dynamically replace illegal underscores with RFC 1035-compliant hyphens:
  ```cpp
  String dnsName = String(MQTT_CLIENT_ID);
  dnsName.replace("_", "-");
  ArduinoOTA.setHostname(dnsName.c_str()); // Resolves to "weather-node-01" (RFC-compliant)
  ```
* **Explicit Port & Password Declarations:** Restored missing explicit initializers for `ArduinoOTA.setPort(OTA_PORT)` and `ArduinoOTA.setPassword(OTA_PASSWORD)` to secure the wireless flash pipeline.
* **Safety Operational Guidelines:** Wiped retained state flags on the Mosquitto broker by publishing null payloads with the `-r` flag to clear the bootloop.
