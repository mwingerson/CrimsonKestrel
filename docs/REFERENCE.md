# Reference: Firmware Safeguards, LED Codes, Credentials Pattern & Telemetry Schemas

Lookup material for the firmware's internals — safety nets, status LED meanings, the credentials
pattern, and the JSON payloads it publishes. For flashing/gateway setup steps, see
[SETUP.md](SETUP.md).

---

#### 🛡 Core Firmware Safeguards (The "Bulletproofing")
Let's be honest: nobody wants to drag a 20-foot ladder out to their roof in the middle of a freezing downpour just to press a tiny physical reset button because the Wi-Fi hiccuped or an I2C sensor locked up. To prevent those unnecessary roof-climbing adventures and keep your station running 24/7, the firmware includes several robust, built-in safety nets:

##### 1. Hardware Watchdog Timer (WDT)
If an I2C transaction hangs due to electrical noise, or a Wi-Fi handshake locks up, the hardware watchdog automatically triggers a full system reset.
*   **Implementation:** The ESP32-C6's internal Main System Watchdog Timer (MWDT) is initialized at boot with a **15-second timeout**. The watchdog is fed continuously through the execution path; if any task hangs, the hardware reboots the processor to recover.

##### 2. Connection-Timeout & Graceful Fallbacks
A major failure mode in field deployments is a node getting stuck trying to connect to an offline router, draining its battery in minutes.
*   **Implementation:** The Wi-Fi connection loop uses a non-blocking timeout. If connection is not established within **10 seconds**, the code gracefully halts, aborts transmission, configures low-power states, and goes to sleep.

##### 3. Data Sanitization & Range Validation
Physical sensors can report NaN (Not a Number) or severe spikes during startup transients or low-voltage states.
*   **Implementation:** Outlier range validation is run on all telemetry before packaging:
    *   **DHT22:** Rejects relative humidity readings outside 0.0 to 100.0 and NaN states.
    *   **INA219:** Rejects bus voltage readings outside 0.0 V to 26.0 V (max chip limit) or NaN states.
    *   If a reading is invalid, the node logs the error, flags the payload as invalid, and bypasses transmission to protect database hygiene.

##### 4. Cloud-Driven Debug Sleep Override State
During active workspace debugging, waiting 5 minutes between iterations slows down development. The node supports dynamic state shifting:
*   On boot, the node subscribes to a command topic. If it reads a retained "ON" debug command, it overwrites its standard 5-minute deep sleep timer with a rapid **5-second cycle** and pulses its RGB LED to signal active debug mode. This allows you to observe full boot-up sequences, Wi-Fi negotiations, and sleep routines rapidly at your desk.

---

#### 🚦 Diagnostic Device Observability (WS2812 LED)
##### 🎨 WS2812 Visual Status LED Color Codes
To assist in local debugging and bring-up without having a serial monitor attached, the onboard WS2812 LED flashes standard, high-density color-coded states. Because these color codes are documented directly using GitHub-compatible hex swatches, they will render as beautifully colored visual dots directly on your GitHub landing page:

| LED State | Visual Swatch | Hex Code | R, G, B | System State & Meaning |
| ------ | ------ | ------ | ------ | ------ |
| **Solid Orange** | 🟧 #964B00 | #964B00 | 150, 75, 0 | **System Booting:** Microcontroller powering up, establishing internal clock registers, and configuring hardware peripherals. |
| **Pulsing Yellow** | 🟨 #964B00 | #964B00 | 150, 75, 0 (pulse) | **Wi-Fi Negotiating:** Active, non-blocking connection attempt to your local Wi-Fi gateway router. |
| **Flash Green** | 🟩 #009600 | #009600 | 0, 150, 0 | **Publish Successful:** Environmental and system power telemetry successfully packaged and published to your MQTT broker. |
| **Solid Purple** | 🟪 #640064 | #640064 | 100, 0, 100 | **Maintenance Window:** Active 2-second idle window listening on command topics for OTA override packages. |
| **Pulsing Cyan** | ⬜ #009696 | #009696 | Variable | **Debug Loop Active:** Dynamic MQTT debug mode triggered; deep sleep is overridden to a rapid 5-second sampling loop. |
| **Solid Blue** | 🟦 #000096 | #000096 | 0, 0, 150 | **OTA Mode Engaged:** Maintenance Mode active; the web-server is open and awaiting wireless firmware binaries. |
| **Pulsing Blue** | 🟦 #000096 | #000096 | 0, 0, pulse | **Active OTA Download:** High-speed wireless download of new compiled .bin firmware packets. |
| **Solid Cyan** | ⬜ #009696 | #009696 | 0, 150, 150 | **Flash Write:** Actively committing new firmware blocks to local partition slots. |
| **Solid Green** | 🟩 #00FF00 | #00FF00 | 0, 255, 0 | **Flash Success:** Firmware update completely written and verified; system is rebooting into the new software layer. |
| **Solid Red** | 🟥 #960000 | #960000 | 150, 0, 0 | **System Error:** Hardware initialization failure (DHT22 missing, INA219 offline), or data sanitization outlier rejected. |
| **Fast Red Flash** | 🟥 #960000 | #960000 | 150, 0, 0 (fast) | **Network Timeout:** Wi-Fi connection timed out or MQTT broker unreachable. Pipeline gracefully aborted to protect battery. |

---

#### 🔒 Safe Credentials Abstraction Pattern
To keep your private home router credentials and server IP addresses completely safe from leakages on public repositories, **CrimsonKestrel** uses a professional **Safe Include Pattern**.
Inside the main firmware sketch, the C++ preprocessor dynamically checks for the presence of a local, untracked secrets.h file:

```cpp
#if __has_include("secrets.h")
  #include "secrets.h"         // Pulls actual credentials locally
#else
  #include "secrets.example.h" // Fallback template so repo compiles out-of-the-box
#endif
```

By adding `secrets.h` and `*secrets.h` to your `.gitignore` file, your private information remains strictly on your local PC (`DIBShip`) while others can easily clone the repo and compile it using the provided `secrets.example.h` blueprint!

---

#### 📊 Decoupled JSON Telemetry Schemas (v1.15.4)
To maintain a clean database structure and separate transient environmental states from system diagnostics, the node publishes two distinct JSON payloads during its single-shot boot routine:

##### 1. Ambient Weather Telemetry
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

##### 2. Device System & Power Observability
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
