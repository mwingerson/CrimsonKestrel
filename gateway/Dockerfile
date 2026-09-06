FROM python:3.11-slim

WORKDIR /app

RUN pip install paho-mqtt influxdb-client

COPY . .

CMD ["python", "crimson_kestrel_daemon.py"]