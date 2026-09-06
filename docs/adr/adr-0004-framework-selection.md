# ADR-0004: Development Framework Selection

## Status
Accepted

## Context
We evaluated the optimal software platform and development framework for writing high-reliability, low-power edge firmware on the ESP32-C6 RISC-V microcontroller.

Three primary options were considered:
1. **Espressif IoT Development Framework (ESP-IDF):** The native, bare-metal C-based SDK provided by Espressif.
2. **Zephyr RTOS:** A modular, secure, open-source real-time operating system optimized for resource-constrained resource devices.
3. **Arduino ESP32 Core (v3.0.0+):** A highly accessible C++ framework built on top of FreeRTOS tasks and the underlying ESP-IDF port.

### Trade-Off Analysis

| Criteria | Native ESP-IDF | Zephyr RTOS | Arduino ESP32 Core |
| :--- | :--- | :--- | :--- |
| **Low-Power Control** | Absolute, registers-level deep-sleep configurations | Excellent, native power-management subsystem | Fully accessible (wraps native ESP-IDF system APIs) |
| **Development Velocity** | Slow (verbose APIs, steep learning curve) | Slow (complex device-tree configurations, tooling setup) | **Extremely Fast** (large library ecosystem, clean syntax) |
| **Portability** | Locked to Espressif silicon | Excellent cross-architecture abstraction | Portable across major Maker/Hobbyist hardware |
| **Library Availability** | Moderate (requires porting custom drivers) | Low (requires custom Zephyr-compliant device-tree drivers) | **Excellent** (thousands of community-maintained drivers) |
| **Watchdog & OTA** | Robust, requires custom partition logic | Native, robust | **Native & Simplified** (uses mDNS, ArduinoOTA, and WDT libraries) |

## Decision
We decided to standardize on the **Arduino ESP32 Core (v3.0.0+)** running on top of underlying **FreeRTOS** APIs.

While Zephyr RTOS and native ESP-IDF are outstanding for enterprise-only products, the Arduino ESP32 Core offers the perfect balance of development velocity and hardware accessibility for a robust, open-source research and development platform. It allows us to utilize the extensive Arduino library ecosystem for sensor bring-up while still leaving low-level ESP-IDF system APIs (like deep-sleep and watchdog timers) fully accessible.

## Consequences
* **Rapid Prototyping:** We can easily swap out complex sensors (such as moving from a basic DHT22 to a Sensirion SPS30 or AS3935 lightning sensor) using verified, pre-existing libraries.
* **Production-Grade Safety:** We still maintain enterprise-level safety nets (including the 15-second Main System Watchdog and OTA rollback partitions) by calling direct low-level ESP-IDF functions when required.
* **Accessibility:** Other developers can easily clone, audit, and compile the firmware in their local environments without needing to configure complex command-line toolchains or device-trees.
