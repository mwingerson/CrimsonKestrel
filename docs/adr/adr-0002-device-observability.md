# ADR-0002: Hardware-Based Device Observability

## Status
Accepted

## Context
Deploying an outdoor, solar-boosted telemetry node requires real-time monitoring of battery state-of-charge (SoC) and system current draw to predict run-time limits and detect hardware anomalies. 

We evaluated two primary power-sensing methods:
1. **Resistive Voltage Divider (Analog ADC Pin):** A standard two-resistor network stepping down battery voltage to fit within the microcontroller's ADC limits.
2. **Dedicated I2C Power Monitor (INA219 / INA226):** An external integrated circuit measuring both bus voltage and differential voltage drop across a low-resistance inline shunt.

### Trade-Off Analysis

| Criteria | Resistive Voltage Divider (ADC) | Dedicated I2C Power Monitor (INA219) |
| :--- | :--- | :--- |
| **Metrics Captured** | Voltage only (non-linear battery estimation) | Voltage, Shunt Voltage, Current (mA), and calculated Power (mW) |
| **Parasitic Current** | Constant leakage to ground (mitigated only via complex high-side MOSFET gating) | Native, register-driven software shutdown modes reducing current draw to < 15 µA |
| **Noise Immunity** | Highly susceptible to RF transients during active Wi-Fi transmission bursts | Isolated digital I2C telemetry with internal averaging and high-frequency filtering |
| **Component Count** | 2 resistors, 1 P-channel MOSFET, 1 NPN transistor, biasing passives | 1 integrated circuit breakout, decoupling capacitor |
| **PCB Footprint** | Large discrete footprint, complex trace routing | Small, modular footprint sharing the existing digital I2C bus |

## Decision
We decided to standardize on the **INA219 high-side I2C current and power monitor** for hardware-based device observability. 

The sensor will be placed upstream of the main voltage regulation and distribution rails (high-side sensing) to ensure an unbroken ground plane across the microcontroller and other peripherals, preventing ground loop offsets.

## Consequences
* **Precision Telemetry:** We gain real-time, microamp-resolution insights into the active transmission spikes and deep-sleep baselines of the microcontroller.
* **Firmware Complexity:** We must initialize and configure the INA219 via standard I2C wire transactions at boot.
* **Zero Parasitic Bleed:** Prior to triggering the microcontroller's deep sleep state, the firmware must write to the INA219 Configuration Register to put the IC into I2C power-down mode (~15 µA draw).
* **Shared Bus Architecture:** The INA219 shares physical I2C pins with any future expansion sensors, eliminating GPIO pin-budget bottlenecks.
