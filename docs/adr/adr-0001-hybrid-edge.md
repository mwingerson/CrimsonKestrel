# ADR-0001: Hybrid Edge Architecture

## Status
Approved

## Context
To build a reliable solar-powered weather station, we evaluated two primary architectural patterns for the core processing system:

1. **Outdoor Single-Board Computer (SBC):** Deploying a Linux-based SBC (such as a Raspberry Pi) directly in the outdoor weather station enclosure [cite: 427]. This offers extreme ease of development, high-level programming language support (Python pipelines), and native database/broker execution at the physical edge [cite: 428].
2. **Hybrid Edge Architecture:** Splitting processing and environmental sampling between an ultra-low-power outdoor microcontroller (MCU) edge node and a permanently powered indoor Linux gateway [cite: 429, 436].

### Power & Cost Engineering Analysis
A standard Linux-based SBC (e.g., Raspberry Pi 4) lacks a native hardware-level deep sleep state and draws a continuous active load of roughly 3 Watts (~600mA @ 5V) [cite: 428, 430]. Under extreme off-grid conditions (designing for 4 days of solar blackout/autonomy at mid-latitudes), the energy requirements scale exponentially [cite: 431]:

* **Daily Energy Draw:** 3W * 24h = 72 Watt-hours (Wh) per day [cite: 430].
* **4-Day Autonomy Target:** 72 Wh/day * 4 days = 288 Wh [cite: 431].
* **Battery Sizing (12V nominal LiFePO4):** 288 Wh / 12V = 24 Amp-hours (Ah) [cite: 431].
* **Solar Panel Sizing (4 winter peak sun hours):** 100 Wh target / 4 hours = 25 Watts minimum [cite: 432]. Applying a standard 1.5x to 2x safety factor requires a **50W to 100W monocrystalline solar panel** [cite: 432].

This outdoor SBC approach demands a heavy, expensive system [cite: 431, 435]: a motorcycle-sized 12V LiFePO4 battery [cite: 431], a massive solar panel [cite: 432], a commercial MPPT charge controller [cite: 433], an industrial DC-DC buck converter [cite: 433], and a dedicated low-power hardware supervisor to trigger clean shutdowns to prevent SD card corruption [cite: 433]. This heavily violates our core constraint of building an accessible, highly replicable, and low-cost R&D station [cite: 413, 435].

Conversely, an optimized microcontroller edge node can utilize a **Single-Shot Boot Execution Pattern** [cite: 235, 413]: waking up from hardware deep sleep, taking rapid averaged sensor samples, negotiating Wi-Fi/MQTT sockets, transmitting a lightweight telemetry packet, and immediately returning to deep sleep [cite: 235]. An MCU like the ESP32-C6 has a deep-sleep current draw of approximately 15–30 µA [cite: 235], allowing the entire outdoor setup to run continuously on a single 18650 Lithium-Ion cell and a compact, lightweight 5V solar panel [cite: 235, 435].

## Decision
We reject the outdoor Single-Board Computer design and officially adopt the **Hybrid Edge Architecture** [cite: 429, 436]:

1. **Outdoor Edge Node:** Standardize on an ultra-low-power ESP32-C6 microcontroller node running a non-blocking single-shot boot pattern [cite: 235, 413]. It is powered by a small 5V solar panel and a single 18650 battery cell [cite: 235, 435].
2. **Indoor Gateway:** Deploy a Linux-based gateway (such as a Raspberry Pi) **indoors on permanent wall power** [cite: 429, 436]. This gateway hosts the local MQTT broker (Mosquitto), a time-series database (InfluxDB), and a visualization platform (Grafana) [cite: 429, 436]. The gateway is responsible for subscribing to the node's telemetry, handling data storage, running calibration algorithms, and pushing data to third-party aggregators (such as Weather Underground, CWOP, or Home Assistant) [cite: 423, 429].

## Consequences

### Consequences (Pros)
* **Drastic SWaP-C Reduction:** Eliminates the need for bulky 12V marine/motorcycle batteries [cite: 431] and large 50W+ solar panels [cite: 432]. The system can be safely assembled and deployed on a standard 3D-printed mounting arm using affordable, widely available maker hardware [cite: 235, 435].
* **System Durability:** Minimizes outdoor failure points. Eliminates mechanical wear, moving parts, and complex off-grid power-switching regulators in the weather enclosure [cite: 433, 477].
* **No SD Card Corruption Risk:** The outdoor node operates completely stateless out of raw flash memory, while the database runs on a safe, indoor, line-powered system [cite: 429, 433].
* **Clean Separation of Concerns:** The outdoor node remains purely an environmental telemetry transducer [cite: 235], while the gateway handles heavy networking, processing, and visualization tasks [cite: 429].

### Consequences (Cons)
* **Local Subnet Requirement:** The outdoor node must remain within wireless range of the indoor gateway's local network (Wi-Fi 6 or mDNS range) [cite: 27, 429].
* **Gateway Dependency:** Environmental data cannot be visualized or aggregated if the indoor gateway is powered off or disconnected from the local network.
