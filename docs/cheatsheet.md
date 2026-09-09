---
status: Up to Date
last_updated: 2026-09-07
refactor_on: "N/A"
ai_breadcrumbs: []
---

# 🛠️ Crimson Kestrel: Technical Cheatsheet & Quick Reference

This cheatsheet compiles critical terminal commands, physical pinouts, electrical constraints, and container management instructions into a single rapid-lookup reference guide.

---

## 🔌 1. Hardware Pinout & Wiring Guides

### A. AM2302 / DHT22 Sensor
*   **Protocol:** Proprietary Single-Bus Serial (timing-based signaling, *not* standard 1-Wire).
*   **Logic Level:** Match supply voltage ($V_{CC}$). For $3.3	ext{V}$ microcontrollers (ESP32-C6 / ESP8266), power the sensor with **$3.3	ext{V}$** directly to ensure native $3.3	ext{V}$ logic levels.

#### Standalone 4-Pin Package
Looking directly at the front grid face with pins pointing downwards:
| Pin Number | Name | Connection Target | Purpose |
| :--- | :--- | :--- | :--- |
| **Pin 1 (Left)** | $V_{CC}$ | $3.3	ext{V}$ Bus Rail | Main supply voltage. |
| **Pin 2** | DATA | ESP32-C6 GPIO15 | Single-wire data line. **Requires a physical $4.7	ext{k}\Omega$ to $10	ext{k}\Omega$ pull-up resistor to $V_{CC}$.** |
| **Pin 3** | NC | *Floating* | Not Connected. |
| **Pin 4 (Right)** | GND | GND Bus Rail | Common star ground plane connection. |

*Note: If utilizing a pre-mounted 3-pin breakout module, the pull-up resistor is typically already integrated on the breakout PCB.*

---

### B. Ebyte ESPC6-WROOM-32 Pin Mapping Reference
High-density mapping of active testing pins utilized across the prototype:
| Module Pin | Pin Name | HW Function | Internal Role & Usage |
| :--- | :--- | :--- | :--- |
| **Pin 1** | GND | Ground | Connected to common star ground plane. |
| **Pin 2** | 3V3 | Power In | Rock-solid $3.0	ext{V}$ to $3.6	ext{V}$ supply. |
| **Pin 3** | EN | Chip Enable | Active HIGH (pulls up internally). Pull LOW to turn off. |
| **Pin 15** | IO9 | Boot Strap | **Strapping Pin:** Must be HIGH at reset for normal Flash Boot. Pull LOW at reset to force UART Download Mode. |
| **Pin 17** | IO19 | GPIO19 / SDA | Shared hardware I2C Data line. |
| **Pin 18** | IO20 | GPIO20 / SCL | Shared hardware I2C Clock line. |
| **Pin 23** | IO15 | GPIO15 | DHT22 single-bus serial data input line. |
| **Pin 24** | RXD0 | U0RXD | USB/JTAG Virtual COM port RX (serial console debug). |
| **Pin 25** | TXD0 | U0TXD | USB/JTAG Virtual COM port TX (serial console debug). |

---

## 💻 2. Remote File Transfer & Networking (`scp`)

Always run these commands from your **local terminal** (not from inside an active SSH session):

### A. Push Files / Folders (Local to Gateway)
*   **Recursively Copy Enclosure STL Directory:**
    ```bash
    scp -r ./hardware/enclosure/ marshall@192.168.0.245:~/wrkspc/weather_station_staging/hardware/
    ```
*   **Push Single Configuration File:**
    ```bash
    scp ./telegraf.conf marshall@192.168.0.245:~/wrkspc/weather_station_staging/telegraf/
    ```

### B. Pull Files / Folders (Gateway to Local)
*   **Download Completed Telemetry CSV Logs:**
    ```bash
    scp -r marshall@192.168.0.245:~/wrkspc/weather_station_staging/data/ ~/Downloads/
    ```
*   **Pull Single Gateway Daemon Script:**
    ```bash
    scp marshall@192.168.0.245:~/wrkspc/weather_station_staging/gateway/crimson_kestrel_daemon.py ~/Downloads/
    ```

### C. SSH Command-Line Escape Sequence (Transfer on the Fly)
If you are already in an active SSH session and want to issue an inline recursive transfer back to your local computer without opening a new tab:
1. Hit `Enter`.
2. Type **`~C`** (Tilde followed by capital C). The prompt will change to `ssh>`.
3. Issue the local execution escape:
   ```text
   ssh> ! scp -r marshall@192.168.0.245:/home/marshall/wrkspc/weather_station_staging/ ~/Downloads/
   ```
4. Hit `Enter` to return seamlessly to your active remote shell.

---

## 🤖 3. Docker & Stack Observability Reference

### A. Quick Diagnostics & Status
*   **List Running Containers & Exposed Ports:**
    ```bash
    docker ps
    ```
*   **Show All Containers (Including Stopped/Crashed Services):**
    ```bash
    docker ps -a
    ```
*   **Tail Live Processing Logs (Last 50 Lines):**
    ```bash
    docker logs telegraf --tail 50
    docker logs mosquitto --tail 50
    docker logs influxdb --tail 50
    ```

### B. Host & Stack Permission Maintenance
*   **Resolve Docker API "Permission Denied" Errors (Add User to Group):**
    ```bash
    sudo usermod -aG docker $USER
    newgrp docker
    ```
*   **Resolve Grafana "Permission Denied" Directory Write Loops:**
    ```bash
    sudo chown -R 472:472 ./grafana/data
    ```

### C. Resetting and Wiping Stack Cache (The Data Wipe Options)
*   **Nuclear Option: Purge Entire Host Docker Cash (All Containers, Volumes, & Networks):**
    ```bash
    docker system prune -a --volumes -f
    ```
*   **Reset Weather Station Stack Only (Clear Persistent Data Volumes):**
    ```bash
    docker compose down -v
    docker compose up -d
    ```

---

## 📡 4. MQTT Broker & Ingestion Diagnostics

### A. Monitoring Live Subnet Traffic (MQTT Sniffing)
*   **Listen to All Subscribed Topics (Great for confirming packet ingress):**
    ```bash
    docker exec -it mosquitto mosquitto_sub -h localhost -p 1883 -t "#" -v
    ```
*   **Subscribe Specifically to Aligned Environmental Topics:**
    ```bash
    docker exec -it mosquitto mosquitto_sub -h localhost -p 1883 -t "telemetry/+/weather" -v
    ```

### B. Simulating Telemetry Ingress (Manual Publishing)
*   **Post Environmental Payload to Broker:**
    ```bash
    docker exec -it mosquitto mosquitto_pub -h localhost -p 1883 -t "telemetry/weather_node_01/weather" -m '{"client_id":"weather_node_01","environment":{"temperature_c":22.3,"humidity_pct":38.5}}'
    ```
*   **Post Power & Diagnostics Payload to Broker:**
    ```bash
    docker exec -it mosquitto mosquitto_pub -h localhost -p 1883 -t "telemetry/weather_node_01/system" -m '{"client_id":"weather_node_01","system":{"version":"1.15.4","uptime_ms":3585},"power":{"bus_voltage_v":3.82,"load_current_ma":36.9,"shunt_voltage_mv":3.69}}'
    ```

---

## 📦 5. Firmware Build & Erase Tools (CLI Flash Option)

If pushing firmware directly over USB or utilizing emergency recovery CLI tools rather than the Arduino IDE:

### A. Arduino CLI Library Ingress
*   **Install Required Library Dependencies:**
    ```bash
    arduino-cli lib install "PubSubClient"
    ```

### B. Direct Python-Based Over-The-Air (OTA) Flash
If local mDNS network resolution fails in the Arduino IDE port selector list, push the compiled application binary directly via IP:
```bash
python3 ~/.arduino15/packages/esp32/hardware/esp32/3.0.0/tools/espota.py -i 192.168.0.114 -p 8266 -a BLACKraptor -f /tmp/arduino/build/CrimsonKestrel.ino.bin
```
