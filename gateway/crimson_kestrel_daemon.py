#!/usr/bin/env python3
"""
CrimsonKestrel Telemetry & Logging Daemon (v1.1.1)
Path: gateway/crimson_kestrel_daemon.py

This production-grade logging daemon runs on an indoor gateway server (such as a 
Raspberry Pi) and acts as the consumption tier for the CrimsonKestrel edge node.

It subscribes to decoupled weather and system MQTT topics, validates incoming JSON
payloads against strict sanity ranges, commits normalized records to rolling, 
comma-separated (CSV) log sheets, and optionally streams data directly to InfluxDB 
if the 'influxdb-client' package is installed.

Dependencies:
    pip install paho-mqtt
    pip install influxdb-client  (Optional, for InfluxDB streaming)
"""

import argparse
import csv
import json
import logging
import os
import signal
import sys
from datetime import datetime

# ==========================================
# --- CONSTANTS & CONFIGURATION -----------
# ==========================================
VERSION = "1.1.1"

# Strict firmware sanity ranges (matching edge safeguards)
VALIDATION_RANGES = {
    "temperature_c": {"min": -40.0, "max": 80.0},
    "humidity_pct": {"min": 0.0, "max": 100.0},
    "bus_voltage_v": {"min": 0.0, "max": 26.0},
    "load_current_ma": {"min": -1000.0, "max": 3200.0},
    "shunt_voltage_mv": {"min": -320.0, "max": 320.0}
}

# Logger setup
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    handlers=[
        logging.StreamHandler(sys.stdout)
    ]
)
logger = logging.getLogger("CrimsonKestrelDaemon")


# ==========================================
# --- INFLUXDB WRITER CLASS ---------------
# ==========================================
class InfluxDBStreamer:
    """Manages live streaming to InfluxDB v2 with graceful fallback if disabled."""
    
    def __init__(self, url, token, org, bucket):
        self.enabled = False
        self.client = None
        self.write_api = None
        self.bucket = bucket
        self.org = org
        
        if not url or not token:
            logger.info("InfluxDB streaming is disabled (credentials not provided). Only CSV logging active.")
            return

        try:
            from influxdb_client import InfluxDBClient
            from influxdb_client.client.write_api import SYNCHRONOUS
            self.client = InfluxDBClient(url=url, token=token, org=org)
            self.write_api = self.client.write_api(write_options=SYNCHRONOUS)
            self.enabled = True
            logger.info(f"InfluxDB client initialized successfully. Target: {url} -> Bucket: {bucket}")
        except ImportError:
            logger.warning("Optional 'influxdb-client' library is missing. Skipping direct database streaming.")
        except Exception as e:
            logger.error(f"Failed to initialize InfluxDB client: {e}")

    def write_point(self, measurement, tags, fields):
        """Writes a single data point to InfluxDB."""
        if not self.enabled:
            return
            
        try:
            from influxdb_client import Point
            point = Point(measurement)
            for tag_key, tag_val in tags.items():
                point = point.tag(tag_key, tag_val)
            for field_key, field_val in fields.items():
                point = point.field(field_key, field_val)
                
            self.write_api.write(bucket=self.bucket, org=self.org, record=point)
        except Exception as e:
            logger.error(f"InfluxDB write failure: {e}")

    def close(self):
        """Closes open database connections."""
        if self.client:
            self.client.close()


# ==========================================
# --- DATA WRITER CLASS --------------------
# ==========================================
class TelemetryLogger:
    """Manages rolling CSV files for telemetry storage."""
    
    def __init__(self, output_dir):
        self.output_dir = output_dir
        os.makedirs(output_dir, exist_ok=True)
        
        # Define field headers
        self.weather_fields = ["timestamp", "client_id", "temperature_c", "humidity_pct"]
        self.system_fields = ["timestamp", "client_id", "uptime_ms", "bus_voltage_v", "load_current_ma", "shunt_voltage_mv"]
        
    def _get_file_path(self, prefix):
        """Generates rolling daily file paths (e.g., prefix_2026-08-30.csv)."""
        date_str = datetime.now().strftime("%Y-%m-%d")
        return os.path.join(self.output_dir, f"{prefix}_{date_str}.csv")

    def _write_row(self, file_prefix, fields, row_data):
        """Writes a structured row of data to a daily rolling CSV file."""
        file_path = self._get_file_path(file_prefix)
        file_exists = os.path.exists(file_path)
        
        try:
            with open(file_path, mode="a", newline="", encoding="utf-8") as f:
                writer = csv.DictWriter(f, fieldnames=fields)
                if not file_exists:
                    writer.writeheader()
                    logger.info(f"Created new log file: {file_path}")
                writer.writerow(row_data)
        except Exception as e:
            logger.error(f"Failed writing to {file_path}: {e}")

    def log_weather(self, client_id, temp, hum):
        """Logs valid weather variables."""
        row = {
            "timestamp": datetime.now().isoformat(),
            "client_id": client_id,
            "temperature_c": round(temp, 2),
            "humidity_pct": round(hum, 2)
        }
        self._write_row("weather", self.weather_fields, row)
        logger.info(f"[WEATHER] Saved record from '{client_id}': Temp={temp}°C | Hum={hum}%")

    def log_system(self, client_id, uptime_ms, bus_v, load_ma, shunt_mv):
        """Logs valid system diagnostics."""
        row = {
            "timestamp": datetime.now().isoformat(),
            "client_id": client_id,
            "uptime_ms": uptime_ms,
            "bus_voltage_v": round(bus_v, 3),
            "load_current_ma": round(load_ma, 2),
            "shunt_voltage_mv": round(shunt_mv, 3)
        }
        self._write_row("system", self.system_fields, row)
        logger.info(f"[SYSTEM] Saved diagnostic from '{client_id}': V_bus={bus_v}V | I_load={load_ma}mA")


# ==========================================
# --- MQTT HANDLER FUNCTIONS ---------------
# ==========================================
class TelemetryDaemon:
    """Subscribes to MQTT broker and coordinates payload parsing."""

    def __init__(self, broker_host, broker_port, output_dir, username=None, password=None, db_streamer=None):
        self.broker_host = broker_host
        self.broker_port = broker_port
        self.username = username
        self.password = password
        self.logger_db = TelemetryLogger(output_dir)
        self.db_streamer = db_streamer
        self.client = None

    def start(self):
        """Configures client and initiates asynchronous listener thread."""
        try:
            import paho.mqtt.client as mqtt
        except ImportError:
            logger.error("Required library 'paho-mqtt' is missing. Run 'pip install paho-mqtt' on gateway.")
            sys.exit(1)

        try:
            self.client = mqtt.Client(client_id="CrimsonKestrelDaemon", callback_api_version=mqtt.CallbackAPIVersion.VERSION2)
        except AttributeError:
            self.client = mqtt.Client(client_id="CrimsonKestrelDaemon")

        if self.username and self.password:
            self.client.username_pw_set(self.username, self.password)

        self.client.on_connect = self.on_connect
        self.client.on_message = self.on_message
        self.client.on_disconnect = self.on_connect

        logger.info(f"Connecting to MQTT Broker on {self.broker_host}:{self.broker_port}...")
        try:
            self.client.connect(self.broker_host, self.broker_port, keepalive=60)
            self.client.loop_forever()
        except Exception as e:
            logger.error(f"Failed to connect to MQTT broker: {e}")
            sys.exit(1)

    def on_connect(self, client, userdata, flags, rc, properties=None):
        """Subscribes to decoupled topics upon establishing connection."""
        code = rc.value if hasattr(rc, 'value') else rc
        if code == 0:
            logger.info("Successfully authenticated and connected to MQTT broker.")
            
            weather_topic = "telemetry/+/weather"
            system_topic = "telemetry/+/system"
            
            self.client.subscribe(weather_topic)
            self.client.subscribe(system_topic)
            logger.info(f"Subscribed to topic patterns: '{weather_topic}' and '{system_topic}'")
        else:
            logger.error(f"Broker connection refused. Return code: {code}")

    def on_message(self, client, userdata, msg):
        """Processes incoming messages on subscribed topics."""
        topic = msg.topic
        payload_str = msg.payload.decode("utf-8")
        
        try:
            payload = json.loads(payload_str)
        except json.JSONDecodeError:
            logger.warning(f"Discarded non-JSON packet received on '{topic}': {payload_str}")
            return

        client_id = payload.get("client_id", "unknown_node")

        if topic.endswith("/weather"):
            self.parse_weather_payload(client_id, payload)
        elif topic.endswith("/system"):
            self.parse_system_payload(client_id, payload)
        else:
            logger.warning(f"Message received on unhandled topic '{topic}': {payload_str}")

    def parse_weather_payload(self, client_id, payload):
        """Parses and validates weather sub-structure."""
        env = payload.get("environment")
        if not env:
            logger.warning(f"Discarded malformed weather payload (missing 'environment' block): {payload}")
            return
            
        temp = env.get("temperature_c")
        hum = env.get("humidity_pct")
        
        if temp is None or hum is None:
            logger.warning(f"Discarded incomplete weather variables: Temp={temp}, Hum={hum}")
            return

        # Sanity Checks
        limits = VALIDATION_RANGES
        if not (limits["temperature_c"]["min"] <= temp <= limits["temperature_c"]["max"]):
            logger.error(f"[SANITY FAILURE] Out-of-bounds temperature rejected: {temp}°C (Client: {client_id})")
            return
        if not (limits["humidity_pct"]["min"] <= hum <= limits["humidity_pct"]["max"]):
            logger.error(f"[SANITY FAILURE] Out-of-bounds humidity rejected: {hum}% (Client: {client_id})")
            return

        # Commit to daily CSV
        self.logger_db.log_weather(client_id, temp, hum)
        
        # Stream to InfluxDB (if enabled)
        if self.db_streamer:
            self.db_streamer.write_point(
                measurement="weather",
                tags={"client_id": client_id},
                fields={"temperature_c": float(temp), "humidity_pct": float(hum)}
            )

    def parse_system_payload(self, client_id, payload):
        """Parses and validates system diagnostics sub-structure."""
        sys_info = payload.get("system")
        power = payload.get("power")
        
        if not sys_info or not power:
            logger.warning(f"Discarded malformed system payload (missing 'system' or 'power' blocks): {payload}")
            return
            
        uptime = sys_info.get("uptime_ms")
        bus_v = power.get("bus_voltage_v")
        load_ma = power.get("load_current_ma")
        shunt_mv = power.get("shunt_voltage_mv")
        
        if any(v is None for v in [uptime, bus_v, load_ma, shunt_mv]):
            logger.warning(f"Discarded incomplete system metrics: {payload}")
            return

        # Sanity Checks
        limits = VALIDATION_RANGES
        if not (limits["bus_voltage_v"]["min"] <= bus_v <= limits["bus_voltage_v"]["max"]):
            logger.error(f"[SANITY FAILURE] Out-of-bounds bus voltage rejected: {bus_v}V (Client: {client_id})")
            return
        if not (limits["load_current_ma"]["min"] <= load_ma <= limits["load_current_ma"]["max"]):
            logger.error(f"[SANITY FAILURE] Out-of-bounds load current rejected: {load_ma}mA (Client: {client_id})")
            return
        if not (limits["shunt_voltage_mv"]["min"] <= shunt_mv <= limits["shunt_voltage_mv"]["max"]):
            logger.error(f"[SANITY FAILURE] Out-of-bounds shunt voltage rejected: {shunt_mv}mV (Client: {client_id})")
            return

        # Commit to daily CSV
        self.logger_db.log_system(client_id, uptime, bus_v, load_ma, shunt_mv)
        
        # Stream to InfluxDB (if enabled)
        if self.db_streamer:
            self.db_streamer.write_point(
                measurement="system",
                tags={"client_id": client_id},
                fields={
                    "uptime_ms": int(uptime),
                    "bus_voltage_v": float(bus_v),
                    "load_current_ma": float(load_ma),
                    "shunt_voltage_mv": float(shunt_mv)
                }
            )


# ==========================================
# --- CLI ENTRYPOINT ----------------------
# ==========================================
if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="CrimsonKestrel Telemetry & Logging Daemon",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter
    )
    parser.add_argument("--host", default="localhost", help="MQTT broker IP address or hostname")
    parser.add_argument("--port", type=int, default=1883, help="MQTT broker TCP port")
    parser.add_argument("--outdir", default="./data", help="Directory for rolling CSV logs")
    parser.add_argument("--user", default=None, help="MQTT Broker username")
    parser.add_argument("--passw", default=None, help="MQTT Broker password")
    
    # InfluxDB arguments
    parser.add_argument("--influx-url", default=None, help="InfluxDB Server URL (e.g. http://localhost:8086)")
    parser.add_argument("--influx-token", default=None, help="InfluxDB Admin API Token")
    parser.add_argument("--influx-org", default="crimson_kestrel_org", help="InfluxDB Organization name")
    parser.add_argument("--influx-bucket", default="telemetry", help="InfluxDB Bucket name")
    
    args = parser.parse_args()

    # Initialize InfluxDB Streamer if configurations supplied
    db_streamer = None
    if args.influx_url and args.influx_token:
        db_streamer = InfluxDBStreamer(
            url=args.influx_url,
            token=args.influx_token,
            org=args.influx_org,
            bucket=args.influx_bucket
        )

    daemon = TelemetryDaemon(
        broker_host=args.host,
        broker_port=args.port,
        output_dir=args.outdir,
        username=args.user,
        password=args.passw,
        db_streamer=db_streamer
    )

    # Setup termination signal handlers
    def graceful_exit(signum, frame):
        logger.info("Signal received. Halting telemetry listener...")
        if daemon.client:
            daemon.client.disconnect()
        if db_streamer:
            db_streamer.close()
        logger.info("Daemon cleanly shutdown.")
        sys.exit(0)

    signal.signal(signal.SIGINT, graceful_exit)
    signal.signal(signal.SIGTERM, graceful_exit)

    logger.info(f"Starting CrimsonKestrel Telemetry Daemon (v{VERSION})...")
    daemon.start()
