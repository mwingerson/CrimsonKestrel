# ADR-0003: Decoupled Multi-Topic Telemetry Schemas

## Status
Accepted

## Context
In early prototyping phases, environmental weather stations typically bundle all sensor outputs (temperature, humidity, battery voltage, up-time, etc.) into a single, monolithic JSON payload published to a single MQTT topic.

While simple to write, this monolithic approach presents severe architectural drawbacks for production-scale deployments:
* **Database Hygiene:** Systems tracking ambient environmental metrics (e.g., InfluxDB) are forced to continuously ingest and store static system metadata (such as firmware version and boot up-time).
* **Payload Bloat:** Network bandwidth is wasted sending slow-changing system diagnostics at the same frequency as rapid environmental changes.
* **Automation Coupling:** Home automation systems (like Home Assistant) must run complex parsing scripts on a single payload to extract independent entities.

## Decision
We decided to **decouple the telemetry payloads** into two distinct, structured JSON schemas published to independent MQTT topics:

1. **Environmental Weather Telemetry (`telemetry/<CLIENT_ID>/weather`):**
   Contains strictly ambient environmental metrics.
   ```json
   {
     "client_id": "<CLIENT_ID>",
     "environment": {
       "temperature_c": 19.6,
       "humidity_pct": 38.7
     }
   }
   ```

2. **System & Power Observability (`telemetry/<CLIENT_ID>/system`):**
   Contains system metadata, diagnostic metrics, and battery health telemetry.
   ```json
   {
     "client_id": "<CLIENT_ID>",
     "system": {
       "version": "1.15.2",
       "uptime_ms": 3594
     },
     "power": {
       "bus_voltage_v": 3.93,
       "load_current_ma": 30.4,
       "shunt_voltage_mv": 3.04
     }
   }
   ```

## Consequences
* **Decoupled Data Ingestion:** Database parsers and time-series databases can subscribe strictly to the `/weather` topic, preserving database hygiene.
* **Payload Efficiency:** System diagnostic payloads can eventually be duty-cycled at a lower rate than environmental payloads to conserve battery power.
* **Cleaner Home Automation:** Home Assistant can consume the two topics independently, mapping entities directly to their logical controls and graphs without heavy JSON post-processing.
