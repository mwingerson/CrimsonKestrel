# Setup: Firmware Flashing & Gateway Stack

Step-by-step instructions for getting a CrimsonKestrel node flashed and its gateway stack running.
For hardware assembly and wiring, see [BRINGUP.md](../BRINGUP.md). For sensor/schema/LED reference
material, see [REFERENCE.md](REFERENCE.md).

---

#### 🔧 Arduino IDE & Hardware Configurations
To flash this firmware cleanly and unlock full hardware capabilities:
1. **Board Package:** Install **ESP32 Core by Espressif Systems (v3.0.0+)** via the Boards Manager.
2. **Flash Size:** Select **16MB (128Mb)** under *Tools > Flash Size*.
3. **Partition Scheme (CRITICAL):** Change this from the default "RainMaker 4MB" to **16M Flash (3MB APP/9.9MB FATFS)**.
   * *Why:* This leaves 3MB of space for heavy application binaries (allowing room for future sensor libraries) and carves out **9.9 Megabytes of local File System space (FATFS)** for offline caching.
4. **USB CDC On Boot:** Set to **Enabled** to allow direct `Serial.print()` routing over the built-in USB/JTAG controller without an external programmer.
5. **Erase All Flash Before Sketch Upload:** Set to **Disabled** to prevent erasing your local Wi-Fi profiles or FATFS files across programming cycles.

---

#### ⚙️ Gateway & Data Ingestion Pipeline
To show a complete end-to-end proof of concept, this repository includes a dedicated Python daemon to run on your indoor gateway server. The **crimson_kestrel_daemon.py** acts as the database-ingestion tier. It subscribes to both weather and system topics, performs strict schema validation and safety checks in real-time, and handles rotative CSV logging natively.

To provide complete, Staff-level end-to-end system validation, **CrimsonKestrel** includes a decoupled ingestion and database tier that resides on your indoor gateway server (like a permanently powered Raspberry Pi):
1. **crimson_kestrel_daemon.py**: A robust, class-based Python background daemon that subscribes to all node topics via wildcards (`telemetry/+/weather` and `telemetry/+/system`), validates payloads against strict sanity ranges, logs data to daily rolling CSV files, and streams metrics directly to InfluxDB.
2. **docker-compose.yml**: A single-command orchestration stack that spins up **Mosquitto** (MQTT), **InfluxDB 2.7** (Time-Series Database), and **Grafana** (Visualization UI).
3. **grafana_dashboard.json**: A pre-configured, beautiful Grafana Dashboard template that you can import with a single click to instantly chart live temperature, humidity, battery charge levels, and system health metrics.

##### 🚀 1. Spin Up the Ingestion Stack
On your gateway server, from the repo root, copy `.env.example` to `.env` and fill in real credentials (InfluxDB admin user/password/org/bucket/token, Grafana admin user/password), then spin up the Docker containers:
```bash
cp .env.example .env
# edit .env with your own credentials
docker compose up -d
```
This will instantly initialize:
*   **Mosquitto MQTT Broker** listening on port 1883.
*   **InfluxDB 2.7** on port 8086 (organization/bucket as configured in `.env`).
*   **Grafana** on port 3000 (credentials as configured in `.env`).

##### 🐍 2. Run the Telemetry Daemon
Ensure the `influxdb-client` and `paho-mqtt` packages are installed on your gateway:
```bash
pip install paho-mqtt influxdb-client
```
Now execute the daemon, pointing it to your local Mosquitto broker and your newly spun-up InfluxDB instance, using the same values you set in `.env`:
```bash
python3 gateway/crimson_kestrel_daemon.py \
  --host localhost \
  --port 1883 \
  --outdir ./data \
  --influx-url http://localhost:8086 \
  --influx-token "<INFLUX_TOKEN from .env>" \
  --influx-org "<INFLUX_ORG from .env>" \
  --influx-bucket "<INFLUX_BUCKET from .env>"
```
The daemon will now intercept, validate, and write every incoming single-shot MQTT transmission to BOTH your daily local CSV log files and InfluxDB simultaneously!

##### 🎨 3. View Your Grafana Dashboard
The InfluxDB data source and the `gateway/grafana_dashboard.json` dashboard are both auto-provisioned on startup — no manual "Add data source" or "Import" steps needed. Grafana reads them from `grafana/provisioning/` (mounted into the container by `docker-compose.yml`), with the data source's organization/token/bucket pulled straight from your `.env` file.

1. Open your browser and navigate to `http://<GATEWAY_IP>:3000` (log in using the `GRAFANA_ADMIN_USER` / `GRAFANA_ADMIN_PASSWORD` you set in `.env`).
2. Go to **Dashboards** and open **CrimsonKestrel Weather Station Dashboard** — it's already there.

You will instantly be presented with a responsive, professional dashboard displaying your ambient environmental indices and live battery curves side-by-side!

##### 💾 Grafana Data Persistence

Grafana's own state (its SQLite database, alerting history, uploaded plugins) is bind-mounted to `./grafana/data` on the host rather than a Docker-managed named volume. This is a deliberate choice: keeping it inside the repo directory means the data is visible and easy to back up or inspect alongside everything else, instead of being tucked away under Docker's internal volume storage.

The trade-off is a permissions gotcha. The `grafana` service runs as `user: "472:472"` in `docker-compose.yml` (matching the `grafana` user baked into the official image), but `./grafana/data` is created and owned by whatever host user first ran `docker compose up`. If that host UID isn't 472, Grafana fails on startup with:
```
Error: ✗ failed to check table existence: unable to open database file: permission denied
GF_PATHS_DATA='/var/lib/grafana' is not writable.
```
Fix this by giving UID 472 real ownership of the directory:
```bash
sudo chown -R 472:472 grafana/data
```
This is the correct fix — it makes the on-disk owner match the user the container actually runs as, rather than loosening the directory's permission bits for every user on the host (e.g. `chmod -R o+rwX`), which "works" but leaves Grafana's session/db data world-writable for no reason beyond convenience.
