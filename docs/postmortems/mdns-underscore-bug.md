# Post-Mortem: The mDNS Underscore Bug

During initial integration testing of the wireless update (OTA) path on the **ESP32-C6**, the edge node successfully received MQTT command triggers, printed the callback receipt, and immediately crashed/went offline (`status offline`) in a permanent bootloop.

#### 🔍 Root Cause Analysis
This failure was traced back to a strict compliance check inside the modern **ESP-IDF v5.1+ / Arduino ESP32 Core v3.0+** network stack:
1. **RFC 1035 Hostname Spec Violation:** Under RFC 1035 (the domain name specification), hostnames are strictly restricted to alphanumeric characters and hyphens ( - ). **Underscores ( _ ) are illegal** in network hostnames.
2. **The Underscore Crash:** The codebase configured the mDNS hostname using the main MQTT client ID: `ArduinoOTA.setHostname(MQTT_CLIENT_ID);`, which resolved to `"weather_node_01"`.
3. **Internal Assertion Panic:** Upon calling `ArduinoOTA.begin()`, the ESP32-C6 attempted to register the mDNS service. The strict underlying ESP-IDF stack encountered the illegal underscore, resulting in a silent assertion failure, memory panic, and an immediate hardware watchdog or register-level CPU reset.
4. **Retained Message Loop:** Because the OTA trigger command was sent to the MQTT broker with the **retained** flag, the broker automatically re-published the "ON" state to the board as soon as it booted back up and reconnected, trapping the firmware in an infinite crash-on-boot cycle.

#### 🛡️ Core Remedies Implemented in v1.15.4
*   **mDNS Sanitization:** The mDNS setup task has been updated to dynamically replace illegal underscores with RFC 1035-compliant hyphens:
    ```cpp
    String dnsName = String(MQTT_CLIENT_ID);
    dnsName.replace("_", "-");
    ArduinoOTA.setHostname(dnsName.c_str()); // Resolves to "weather-node-01" (RFC-compliant)
    ```
*   **Explicit Port & Password Declarations:** Restored missing explicit initializers for `ArduinoOTA.setPort(OTA_PORT)` and `ArduinoOTA.setPassword(OTA_PASSWORD)` to secure the wireless flash pipeline.
*   **Safety Operational Guidelines:** Wiped retained state flags on the Mosquitto broker by publishing null payloads with the `-r` flag to clear the bootloop.
