# Bill of Materials (BOM) — CrimsonKestrel

This document serves as the official, production-grade Bill of Materials (BOM) and Sourcing Guide for **CrimsonKestrel**, a highly optimized, solar-powered edge telemetry node built around the ESP32-C6 RISC-V SoC. 

This reference architecture serves as an overly robust, low-power R&D platform designed to be highly accessible and affordable for hobbyists. By utilizing a single 18650 Lithium-Ion cell, a small solar panel, and modular breakout boards, anyone can build a reliable 24/7 weather station using the parts they have available or can afford, while maintaining an ultra-low sleep current baseline (~15–30 µA).

---

## 📊 Phase 1: MVP Core Bill of Materials

These components form the baseline telemetry node designed in Phase 1. All connections are designed for standard **2.54mm (0.1") pitch male/female headers** or direct-solder prototyping boards.

| Ref | Component Name | Manufacturer / MPN | Key Technical Specifications | Qty | Est. Cost (USD) | Primary Sourcing | Design & Integration Notes |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **U1** | **ESP32-C6 Dev Board** | Meshnology / [QS-ESP32 C6 N16](https://www.amazon.com/dp/B0FKH69BP7) | Core board with 16MB Flash, RISC-V Single-Core CPU, 2.4GHz Wi-Fi 6, BLE 5, Zigbee 3.0, and Thread. Compatible with ESP32-WROOM series boards, Type-C connector. | 1 | $4.50 – $6.00 | [Amazon](https://www.amazon.com/dp/B0FKH69BP7) | Provides the core processing, network socket negotiation, and deep sleep routines. Features USB CDC for direct debug monitoring over Type-C. |
| **U2** | **INA219 Power Monitor Breakout** | Texas Instruments / INA219AIDR (Adafruit PID 904 or Generic) | I2C high-side current and voltage monitor, ±26V common-mode input range, 12-bit ADC, onboard 0.1Ω 1% shunt resistor. | 1 | $2.00 – $4.50 | Mouser, Adafruit, Amazon | Monitors battery health and real-time current draw. Breakout includes physical 10kΩ pull-up resistors on SDA/SCL lines. |
| **SEN1** | **DHT22 Humidity & Temp Sensor** | Aosong / AM2302 | Ambient temperature range: -40°C to 80°C (±0.5°C), relative humidity: 0–100% (±2–5% RH). Single-bus serial protocol. | 1 | $3.00 – $4.00 | Adafruit, SparkFun, Amazon | Measures primary environmental metrics. Can use standard 3-pin PCB module with pre-installed pull-up, or bare 4-pin sensor with external resistor. |
| **D1** | **WS2812 RGB LED** | Worldsemi / WS2812B (Onboard GPIO8) | Addressable RGB LED, single-wire control protocol. Integrated directly on Dev Board. | 1 | Included | Built-in on Node Board | Configured in firmware as a multi-state visual indicator for booting, network socket errors, active debug mode, and OTA progress. |
| **BAT1** | **18650 Li-Ion Cell** | Samsung / INR18650-30Q or Panasonic / NCR18650B | 3.7V Nominal, 3000mAh capacity, chemistry optimized for high reliability and steady discharge. | 1 | $5.00 – $7.00 | 18650BatteryStore, LiionWholesale | Main system power reservoir. Size is highly optimized for multi-week operation even under extended solar blackout. |
| **U3** | **Solar Charger & Board** | WatangTech / [5V Solar Charge Controller](https://www.amazon.com/dp/B0FQP915R9) | JEITA Li-ion Battery Charger with dual USB output, 5V and 3.3V power rails, and built-in 18650 battery holder. Compatible with Arduino, RPi, STM32. | 1 | $5.00 – $7.00 | [Amazon](https://www.amazon.com/dp/B0FQP915R9) | Automatically manages solar charging cycles for the 18650 cell, and exposes highly convenient 5V and 3.3V supply lines. |
| **PV1** | **Monocrystalline Solar Panel** | Voltaic Systems / [5.5 Watt 6 Volt Panel](https://www.amazon.com/dp/B085W9KG6V) | 5.5 Watt, 6 Volt high-performance monocrystalline cells. Waterproof, UV-resistant, scratch-resistant ETFE laminate casing. | 1 | $35.00 – $40.00 | [Amazon](https://www.amazon.com/dp/B085W9KG6V) | High-durability charging source designed to gather generous power even under partial shade or low daylight winter months. |
| **R1** | **DHT22 Pull-Up Resistor** | Generic / Metal Film 1/4W | 4.7kΩ to 10kΩ resistor, 1% tolerance. | 1 | $0.10 | Mouser, DigiKey, Parts Bin | Placed between VCC and DATA pins of bare 4-pin DHT22 to keep single-bus line from floating. Skip if using 3-pin breakout board. |

---



### 🚦 The WS2812 "Device Observability" Pivot
A common question when designing low-power remote nodes is: *Why spend precious power on an addressable WS2812 RGB LED (which can draw up to 50mA when fully bright) instead of a simple single-color indicator LED or no LED at all?*

The decision is driven by **strict engineering trade-offs** and **field survivability**:
1. **Single-Pin Multi-State Diagnostics:** A traditional single-color status LED only offers two states (On or Off). To represent the multiple critical stages of our single-shot boot pipeline (Booting -> Sensor Init -> Wi-Fi Connect -> MQTT Handshake -> Payload Validation -> Active Listening -> Sleep), we would need a bank of 5+ separate LEDs, which would hijack precious GPIO pins, increase BOM costs, and clutter the physical board. The WS2812 uses **exactly one physical GPIO pin (GPIO8)** to output a high-density, multi-color diagnostic spectrum.
2. **Zero-Power Deep Sleep Isolation:** The WS2812 is only powered and active during the brief **2-to-3 second active boot and transmission window**. The instant the transmission completes and the node prepares to sleep, the firmware executes `statusLED.clear()` and disables the LED completely. During deep sleep, the LED's power draw is non-existent, making its net daily energy consumption negligible (under 0.05% of our daily battery capacity).
3. **The "No-Ladder" Diagnostics Rule:** Let's be honest: nobody wants to drag a 20-foot ladder out to their roof in a freezing downpour just to attach a physical USB serial debugging cable because the station stopped updating. Having a high-density RGB status indicator allows you to perform a simple "ground-level binocular test" or check it from a window. If the node is struggling with your router, it pulses yellow; if the sensor is disconnected, it glows solid red; if it is ready for a firmware update, it turns solid blue.


## 🔌 Core Solder-Direct Master Wiring Map (PTH Spacing)

By designing for direct pin-to-pin soldering using standard **2.54mm (0.1") pitch pin headers**, we eliminate expensive proprietary connectors, lowering BOM costs and simplifying assembly.

All breakout modules share the physical I2C bus lines in parallel:

| Breakout Board Pin | ESP32-C6 Pin | Function | Electrical / Configuration Notes |
| :--- | :--- | :--- | :--- |
| **VCC / VIN (All)** | **3.3V** | Power Rail | Main system power (Common 3.3V supply). |
| **GND (All)** | **GND** | Ground Plane | Star ground configuration to prevent loops and signal noise. |
| **SDA (INA219)** | **GPIO19** | I2C Data | Communicates battery diagnostics. Shared hardware I2C bus. |
| **SCL (INA219)** | **GPIO20** | I2C Clock | Shared hardware I2C bus lines. |
| **DATA (DHT22)** | **GPIO15** | Single-Bus Serial | Uses an internal or external 10kΩ pull-up resistor to 3.3V. |

---

## 💡 Sourcing & Assembly Best Practices

To ensure "right-first-time" assembly and protect against field failures, follow these professional prototyping rules:

### 1. The I2C Pull-Up "BOM Hack"
Standard I2C buses require physical pull-up resistors (typically $2\text{k}\Omega$ to $5\text{k}\Omega$) on SCL and SDA to ensure reliable high-speed communication. Relying strictly on the ESP32-C6's internal weak software pull-ups (typically $45\text{k}\Omega$) causes signal degradation and eventual system hangs.
*   **The Hack:** Standard off-the-shelf INA219 breakout modules already feature physical $10\text{k}\Omega$ pull-up resistors pre-soldered on-board. Wiring the breakout module directly to the I2C lines provides the necessary physical pull-up network for the entire bus, eliminating the need to solder discrete resistors on your proto-board.

### 2. Ground Plane Integrity & Star Grounding
High-speed wireless transmissions (ESP32-C6 Wi-Fi 6 active bursts up to 350mA) introduce significant electrical noise into physical power rails.
*   **Best Practice:** Implement a strict **Star Ground Configuration** where the ground pins from the ESP32-C6, INA219, and DHT22 are soldered to a single physical point on your perfboard. This prevents ground loops and stabilizes readings.
*   **Decoupling:** Place a $0.1\mu\text{F}$ ceramic capacitor close to the power pins of each peripheral to filter high-frequency switching noise.

---

## 🛣️ Phase 2: Extensible Platform Sourcing Guide

Because your battery capacity is large (3000mAh) and the ESP32-C6's single-shot sleep routine draws minimal power, you have abundant energy reserves. This allows you to scale up the weather station dynamically in Phase 2 by plugging these professional modules straight into your physical I2C bus and GPIO lines:

### 1. Wind Speed: Hobbyist Cup Anemometer
*   **Estimated Cost:** ~$15.00 – $25.00 (Standard replacement cup assemblies).
*   **Interface:** Digital GPIO Pin (Magnetic reed switch output).
*   **Integration:** Connect to **GPIO3**. Configured in firmware using the ESP32-C6's hardware **Pulse Count Controller (PCNT)** peripheral to count wind speed pulses in the background without stealing CPU cycles.

### 2. Storm Tracking: AS3935 Franklin Lightning Sensor
*   **Estimated Cost:** ~$22.00 – $35.00 (DFRobot Gravity or SparkFun Qwiic breakouts).
*   **Interface:** I2C (shares GPIO19/SDA and GPIO20/SCL) + 1 Hardware Interrupt Pin.
*   **Integration:** Connect the interrupt line to **GPIO2**. It will automatically wake the ESP32-C6 from deep sleep when electromagnetic storm pulses are detected up to 40 km away.

### 3. Air Quality: Sensirion SPS30 Particulate Matter Sensor
*   **Estimated Cost:** ~$35.00 – $45.00.
*   **Interface:** I2C (shares GPIO19/SDA and GPIO20/SCL).
*   **Integration:** Features an automated high-RPM self-cleaning fan routine to prevent long-term optical drift. Requires a gated load switch (such as a P-channel MOSFET) controlled by **GPIO1** to completely isolate and power-down the sensor's fan during deep-sleep states.
