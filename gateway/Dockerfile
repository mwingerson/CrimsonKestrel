# gateway/Dockerfile
FROM python:3.11-slim

WORKDIR /app

# Install required telemetry dependencies
RUN pip install --no-cache-dir paho-mqtt influxdb-client

# Copy repository files into container
COPY . /app/

# Default execution entrypoint (can be overridden by docker-compose)
CMD ["python3", "gateway/crimson_kestrel_daemon.py", "--host", "mosquitto", "--port", "1883"]
