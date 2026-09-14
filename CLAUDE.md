# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Working style (Rules of Engagement)

* **Context economy:** Don't request or dump whole files/modules unless the task needs them — ask for the
  specific file or function relevant to the task.
* **Step-by-step execution:** For non-trivial changes, propose the architecture or function signature first
  and wait for confirmation before writing the full implementation.
* **Hardware grounding:** For firmware/electrical/mechanical tasks, verify pinouts and electrical/mechanical
  constraints against [README.md](README.md) / [BOM.md](BOM.md) before proposing changes.
* **No auto-pilot:** Keep changes modular. Don't rewrite entire files unless explicitly requested.

## PROJECT REGISTER

Running ledger of open items for this project — not just bugs, not just features: anything that needs
attention before it's forgotten. Update statuses as items move; don't delete closed items, mark them Done
so the history of what's been dealt with stays visible.

| # | Item | Status | Notes |
| --- | --- | --- | --- |
| 1 | `influxDBAPIKey` at repo root contained a real InfluxDB token in plaintext and was **not** covered by `.gitignore`. | Done | Consolidated the three divergent token values (influxdb init token, telegraf/gateway hardcoded token, and the standalone file) onto one canonical token, moved it plus org/bucket/admin creds into a gitignored `.env` (see `.env.example` for the template) consumed by `docker-compose.yml` and `telegraf/telegraf.conf` via `${VAR}` interpolation. Deleted the stray `influxDBAPIKey` file and added `.env`/`influxDBAPIKey` to `.gitignore`. Also added `influxdb-client` to `gateway/Dockerfile`'s pip install — it was missing, so `InfluxDBStreamer` was silently a no-op in the container regardless of token correctness. |
| 2 | MQTT → InfluxDB → Grafana field/label consistency. Reported: labels aren't lining up correctly across the pipeline. | Done | Traced firmware → daemon → Grafana end-to-end: every field/measurement/tag name (`temperature_c`, `humidity_pct`, `bus_voltage_v`, `load_current_ma`, `shunt_voltage_mv`, `client_id`, measurements `weather`/`system`) matches exactly across all three layers — not a naming-convention bug. Actual bug was in `gateway/grafana_dashboard.json` panel 1 ("Ambient Environmental Telemetry"): the Flux queries `yield(name: "temperature")` / `yield(name: "humidity")`, but the panel's `fieldConfig.overrides` used `byName` matchers on those yield names (`"temperature"`/`"humidity"`) instead of the actual Grafana series names, which InfluxDB's Flux datasource derives from `_field` (`"temperature_c"`/`"humidity_pct"`). The overrides silently never matched, so temperature/humidity always rendered with default color/unit instead of the intended red/°C and blue/%. Fixed by changing the `byName` `options` to `"temperature_c"`/`"humidity_pct"`. |
| 3 | Firmware "appears stable" on the bench but lacks real validation tooling to confirm it (deep-sleep cycle correctness, watchdog behavior, OTA rollback path). | Open, deprioritized for MVP | Not required to demo a working end-to-end pipeline; the existing safety nets (watchdog, connection timeouts, OTA rollback guard) plus bench observation are enough to claim for a resume-facing MVP. Revisit if the firmware actually shows flaky behavior in the field, or once the MVP is out and there's time to harden further. |
| 4 | CLAUDE.md was stale relative to the repo (only described the firmware, not the gateway/Docker stack). | Done | Fixed by a rewrite — see architecture sections below. |
| 5 | Grafana showed no data end-to-end even after the Register #2 fix. | Done | Three independent bugs stacked: (1) `gateway_daemon`'s built image predated the Register #1 `influxdb-client` fix, so it was silently CSV-only — fixed by rebuilding. (2) The Grafana InfluxDB data source, when added by hand per the README's old instructions, gets a random auto-generated UID, but `gateway/grafana_dashboard.json` hardcodes `datasource.uid: "influxdb_telemetry"` on every panel — mismatch means every panel silently fails to resolve. Fixed permanently via Grafana provisioning (see Register #6). (3) The daemon's `load_current_ma` sanity floor was `-100.0`, rejecting 100% of system-telemetry readings — this circuit tracks a solar charge controller, which legitimately swings to roughly ±1000mA (charging vs. discharging), not just positive load draw. Widened the floor to `-1000.0` in `VALIDATION_RANGES`. |
| 6 | Grafana required manual "Add data source" + "Import dashboard" UI steps every time, which is exactly what caused the datasource-UID mismatch in Register #5. | Done | Added Grafana provisioning: `grafana/provisioning/datasources/influxdb.yml` (data source pinned to `uid: influxdb_telemetry`, org/bucket/token pulled from `.env` via Grafana's native `${VAR}` provisioning-file interpolation) and `grafana/provisioning/dashboards/dashboards.yml` (file provider pointing at a mounted copy of `gateway/grafana_dashboard.json`). Both directories are mounted read-only into the `grafana` service in `docker-compose.yml`, along with the three new `INFLUX_*` env vars the data source YAML needs. README's dashboard section rewritten — it's now zero manual steps, log in and the dashboard is already there. |
| 7 | `telegraf` service was crash-looping (`mqtt_consumer` configured with `data_format = "json_v2"` but no field-mapping sub-block) and, even fixed, would have written to a different measurement (`weather_metrics`) than what the dashboard queries (`weather`/`system`) — fully redundant with `gateway_daemon`'s own direct InfluxDB streaming. | Done | Removed entirely: the `telegraf` service from `docker-compose.yml`, the `telegraf/` directory, and references in this file. `gateway_daemon` is now the sole MQTT→InfluxDB path. |
| 8 | A round of edits from another AI assistant (Gemini) left `docker-compose_v9.yml` and friends with two more of the same "labels don't line up" bugs as Register #5/#6, plus a permissions regression introduced while fixing one of them. | Done | (1) `grafana/provisioning/dashboards/dashboards.yml` pointed its `path` at `dashboards/grafana_dashboard.json`, but the actual file had been placed one level deeper at `dashboards/files/grafana_dashboard.json` — silently broke dashboard provisioning again. Fixed by moving the file up to match the declared path. (2) `docker-compose.yml`/`grafana/provisioning/datasources/datasources.yml` had been migrated to `INFLUXDB_*` env var names, but the real `.env` and `.env.example` still had the old `INFLUX_*` names — every `${INFLUXDB_*:-default}` was silently falling back to hardcoded defaults instead of the real token/org/bucket. Fixed by renaming the vars in `.env`/`.env.example`. (3) While switching Grafana's data dir from a Docker-managed named volume (`grafana_data`) to a host bind mount (`./grafana/data`, so state is visible/backup-able in-repo — see README's "Grafana Data Persistence" section), the bind-mounted directory was owned by the host user, not UID 472 that the `grafana` service runs as (`user: "472:472"`), so Grafana couldn't open its own SQLite db on startup. Fixed with `sudo chown -R 472:472 grafana/data` — chosen over loosening the directory's permission bits (`chmod -R o+rwX`) because it matches ownership to the actual running user instead of opening the data up to every user on the host. |

## FOR NEXT SESSION

* **Read the PROJECT REGISTER above first and pick up from there.**
* **Last completed:** Register #8 closed — a round of edits from another AI assistant (Gemini) reintroduced
  the same class of bug as Register #5/#6 (dashboard provisioning `path` pointing one directory level off
  from where the JSON actually lived) plus a fresh `.env`/`docker-compose.yml` env-var-name mismatch
  (`INFLUX_*` vs `INFLUXDB_*`) that was silently defeating token/org/bucket overrides. Also moved Grafana's
  data directory from a Docker-managed named volume to a host bind mount (`./grafana/data`) for in-repo
  visibility/backup, which surfaced a UID-472-vs-host-user permission mismatch on startup — fixed via
  `chown`, documented in README's new "Grafana Data Persistence" section. Left several versioned scratch
  files behind from that Gemini session (`docker-compose_v7/v8/v9.yml`, `gateway/Dockerfile_v8/v9`,
  `grafana/provisioning/datasources/influxdb.yml_del_me`) — not yet cleaned up.
* **MVP scoping decision:** Register #3 (firmware soak-test tooling) is intentionally deprioritized — not
  required to demo a working pipeline for a resume-facing MVP. Only revisit if real field flakiness shows up.
* **Current blockers / state:** Repo is still pre-first-commit. Gateway stack fully functional and
  runtime-verified (not just statically reviewed) as of this session. Remember: `docker compose up -d
  --build` after any change under `gateway/`, or the container will keep running a stale image.
* **Immediate next step:** Delete the leftover scratch/versioned files listed above once confirmed the
  current `docker-compose.yml`/`gateway/Dockerfile`/`grafana/provisioning/datasources/datasources.yml` are
  the ones to keep — they're a recurring source of "which file is real" confusion. Other good next items:
  (1) visually eyeball the dashboard in a browser rather than just via the Grafana query API, (2) double
  check the `load_current_ma` ±1000mA range is actually the right bound for the specific solar charge
  controller in use, (3) Register #3 if you decide it's worth it after all.

## What this is

CrimsonKestrel is a solar-powered weather station edge node built on the ESP32-C6 (RISC-V), paired with an
indoor Linux gateway. It's a **hybrid edge architecture** (see [docs/adr/adr-0001-hybrid-edge.md](docs/adr/adr-0001-hybrid-edge.md)):

* **Edge node** (`firmware/CrimsonKestrel/CrimsonKestrel.ino`) — single-sketch Arduino/ESP32 firmware,
  flashed via the Arduino IDE. No build system, package manifest, or test suite for this half of the repo.
  See [README.md](README.md) for hardware BOM, wiring map, and Arduino IDE board/partition configuration.
* **Gateway** (`gateway/`, orchestrated by `docker-compose.yml`) — a permanently-powered indoor stack that
  receives the edge node's MQTT telemetry and turns it into stored, visualized data:
  * `mosquitto` — MQTT broker
  * `gateway_daemon` (`gateway/crimson_kestrel_daemon.py`) — subscribes to MQTT, validates payloads against
    sanity ranges mirroring the firmware's own validation, writes rolling CSV logs, and optionally streams
    to InfluxDB
  * `influxdb` — time-series storage
  * `grafana` — dashboards. The InfluxDB data source and `gateway/grafana_dashboard.json` are both
    auto-provisioned on startup from `grafana/provisioning/` (mounted read-only into the container); the
    data source's `uid` is pinned to `influxdb_telemetry` there to match what the dashboard JSON's panels
    hardcode — if that data source is ever recreated by hand instead of through provisioning, Grafana
    assigns it a random UID and every panel silently fails to resolve data

Architecture rationale (framework choice, gateway split, observability approach, schema decoupling) is
recorded in `docs/adr/` — check there before re-litigating a design decision.

## Build / flash (firmware)

This project is compiled and flashed through the **Arduino IDE**, not a CLI toolchain. There is no
`arduino-cli`/`platformio` config checked into the repo. Required settings (from the README):

- Board package: ESP32 Core by Espressif Systems (v3.0.0+)
- Flash Size: 16MB (128Mb)
- Partition Scheme: `16M Flash (3MB APP/9.9MB FATFS)` (default "RainMaker 4MB" will not work — app needs
  the larger APP partition and rollback needs a valid OTA partition table)
- USB CDC On Boot: Enabled (for `Serial.print()` over the native USB/JTAG controller)

Required Arduino libraries (installed via Library Manager, not vendored): `Adafruit_Sensor`, `DHT sensor
library`, `Adafruit_INA219`, `ArduinoJson`, `PubSubClient`, `ArduinoOTA`, `Adafruit_NeoPixel`. There is no
automated lint/test command — verification is done by compiling in the Arduino IDE and observing serial
output / MQTT traffic on a real device.

## Running the gateway stack

`docker-compose.yml` brings up `mosquitto`, `influxdb`, `grafana`, and `gateway_daemon` together
on a shared `weather_net` network. `gateway_daemon` depends on `paho-mqtt` (required) and `influxdb-client`
(optional — the daemon falls back to CSV-only logging if it's absent, per
`gateway/crimson_kestrel_daemon.py`'s `InfluxDBStreamer`). There is no automated test suite for this half of
the repo either — verification is by watching daemon logs, the CSV output in `data/`, and the Grafana
dashboard.

## Architecture: single-shot boot, not a loop-driven sketch

Despite being an `.ino`, this firmware deliberately avoids the classic Arduino `loop()` pattern for its
main behavior. Almost everything happens once inside `setup()`:

1. Init watchdog, sensors (DHT22 + INA219), read+validate one sample (`readSensors()`).
2. Power down the INA219 over I2C to cut idle current.
3. If sensors were valid: connect Wi-Fi → connect MQTT → publish two JSON payloads (weather, system) →
   hold open a short (`OTA_LISTEN_MS`) command-listening window.
4. Decide the exit path: either drop into `ArduinoOTA` maintenance mode, or call
   `esp_ota_mark_app_valid_cancel_rollback()` and go to deep sleep (`esp_deep_sleep_start()`), which
   restarts execution from `setup()` on wake — there is no persistent RAM state across cycles except what
   survives via MQTT retained messages.

`loop()` is only ever reached when OTA mode was toggled on; its sole job is running `ArduinoOTA.handle()` /
`mqttClient.loop()` until a 5-minute safety timeout (`OTA_WATCHDOG_MS`) or an external "OFF" command
returns the node to the deep-sleep exit path. Any change to boot behavior almost always belongs in
`setup()`, not `loop()`.

### External state via MQTT retained messages

The node has no flash-persisted config. Runtime behavior is steered entirely by retained MQTT messages
picked up during the brief listening window each boot cycle (handled in `mqttCallback()`):

| Topic | Effect |
| --- | --- |
| `cmd/weather_node_01/ota` | `ON`/`true`/`1` sets `otaModeActive`, keeping the node awake in the OTA loop instead of sleeping |
| `cmd/weather_node_01/debug` | `ON`/`true`/`1` collapses `activeSleepDurationSec` to 5s for rapid dev iteration; anything else restores the default |
| `cmd/weather_node_01/sleep` | Sets `activeSleepDurationSec` dynamically (1–86400s bounds-checked) |

When touching this logic, remember every cycle is a fresh boot: `activeSleepDurationSec` and the mode flags
reset to defaults each time and are only re-derived from whatever retained messages arrive during that
cycle's listen window.

### Safety nets that gate the network/publish path

- **Watchdog**: `initWatchdog()`/`feedWatchdog()` wrap every blocking section (sensor init, Wi-Fi connect
  loop, MQTT connect loop, OTA loop) with a 15s hardware WDT. Any new blocking call added to `setup()` or
  `loop()` needs a `feedWatchdog()` in its wait loop or the chip will hard-reset.
- **Connection timeouts**: `connectWiFi()` and `connectMQTT()` are both non-blocking-with-timeout
  (`NET_TIMEOUT_MS` = 10s) and fail closed — timing out short-circuits the rest of the publish pipeline and
  goes straight to sleep, rather than retrying indefinitely and draining the battery.
- **Sensor validation**: `readSensors()` rejects NaN/out-of-range DHT22 (0–100% RH) and INA219 (0–26V bus)
  readings and sets `sensors_valid = false`, which aborts the entire Wi-Fi/MQTT/publish path for that cycle
  (see the `if (data.sensors_valid)` branch in `setup()`).
- **OTA rollback guard**: `esp_ota_mark_app_valid_cancel_rollback()` is only called on the successful,
  non-OTA exit paths (end of `setup()` and end of `loop()`). A new failure path that bypasses both of these
  before sleeping will leave the app unmarked, so the bootloader will treat it as a bad update and roll
  back to the previous OTA partition on next boot — this is intentional and should not be "fixed" by
  calling it earlier.
- **Status LED** (`setLEDColor`) encodes state for bench debugging without a serial connection: orange =
  booting, red = any hardware/network/validation failure, purple = idle listen window, pulsing white/cyan =
  debug mode, cyan = OTA write in progress, green = success. Keep new states consistent with this palette
  rather than introducing new colors ad hoc.

## Hardware/pin map

GPIO assignments (`DHTPIN`=1, `I2C_SDA`=19, `I2C_SCL`=20, `ONBOARD_LED`=8) and the full wiring rationale
(including why the INA219 breakout's onboard pull-ups double as the I2C bus pull-ups) are documented in
[README.md](README.md) — check there before changing pin assignments, since they're chosen to avoid
ESP32-C6 strapping-pin conflicts.

## Config block

Firmware tunables (Wi-Fi credentials, MQTT broker/topics, timeouts, pins) live in `firmware/CrimsonKestrel/secrets.h`
(gitignored — see `secrets.example.h` for the template) plus the `USER CONFIGURATION BLOCK` in
[CrimsonKestrel.ino](firmware/CrimsonKestrel/CrimsonKestrel.ino). Treat any change to WiFi/MQTT/OTA
credentials as touching a credential, not just a constant. Gateway-stack credentials (InfluxDB admin
user/password/token, org, bucket, Grafana admin user/password) live in a gitignored `.env` at the repo
root — see `.env.example` for the required keys — and are consumed by `docker-compose.yml` via `${VAR}`
interpolation; do not hardcode them back into that file.
