# Shelly_Heating_Control

Simple C++ HTTP server for receiving and serving sensor data.

## Build

Requires a C++17 compiler (g++). From the project root:

```bash
make
```

## Run

Default:

```bash
./server
```

Options:

# Shelly_Heating_Control

Simple C++ HTTP server for receiving and serving sensor data.

## Overview

- Receives sensor readings via HTTP and stores the latest payload per sensor.
- Exposes read-only endpoints to fetch sensor data and settings.
- Supports per-room desired temperature and trigger URLs (high/low) executed in background.

## Build

Requires a C++17 compiler (g++). From the project root:

```bash
make
```

## Run

Default (port 8080):

```bash
./server
```

Options:

- `./server 8000` — run on port 8000
- `./server -v` — enable verbose request logging
- `./server -i 3600` — set flush interval (seconds), default 3600

Examples:

- Run on port 8000 and flush every 10 minutes:

```bash
./server 8000 -i 600
```

## Tests

Run unit tests:

```bash
make test
```

## Endpoints (examples)

POST (modify state):

- Set desired temperature for a room (form-encoded):

```bash
curl -X POST -d "room=living-room&desired=21.5" http://localhost:8080/setDesiredTemperature
```

- Set trigger URL when temperature is higher than desired:

```bash
curl -X POST -d "room=living-room&url=https://example.com/high" http://localhost:8080/setHighTrigger
```

- Set trigger URL when temperature is lower than desired:

```bash
curl -X POST -d "room=living-room&url=https://example.com/low" http://localhost:8080/setLowTrigger
```

GET (read-only):

- Web UI / index:

```bash
curl http://localhost:8080/
```

- Single sensor last payload (`<id>`):

```bash
curl http://localhost:8080/sensor/<id>
```

- All sensors as JSON:

```bash
curl http://localhost:8080/sensors
```

- All settings as JSON:

```bash
curl http://localhost:8080/settings
```

- Settings for a specific room (`<room>`):

```bash
curl http://localhost:8080/settings/<room>
```

- Send sensor reading (evaluates triggers after saving):

```bash
curl "http://localhost:8080/saveSensorInformation?sensor=living-room&temp=22.0&hum=45&batt=3.7"
```

## Storage details

- Sensor data: stored per-sensor under the `data/` directory (default). Each sensor has a single file with the last JSON payload.
- Settings: stored in `settings.json` (repository root by default). This is the single canonical source for room settings.
- Trigger execution: uses `curl` in a background child process; ensure `curl` is installed on the host.

## License

This project is licensed under the GNU General Public License v3 or later. See the `COPYING` file for full license text.

Author: Jakub
```bash
