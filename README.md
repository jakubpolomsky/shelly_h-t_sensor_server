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

- `./server 8000` — run on port 8000
- `./server -v` — enable verbose request logging

## Tests

Run unit tests:

```bash
make test
```

## License

This project is licensed under the GNU General Public License v3 or
later. See the COPYING file for the full license text.

Author: Jakub

## Examples: Settings and triggers

Set desired temperature for a room (POST, form-encoded):

```bash
curl -X POST -d "room=living-room&desired=21.5" http://localhost:8080/setDesiredTemperature
```

Set the URL to call when measured temperature is higher than desired:

```bash
curl -X POST -d "room=living-room&url=https://example.com/high" http://localhost:8080/setHighTrigger
```

Set the URL to call when measured temperature is lower than desired:

```bash
curl -X POST -d "room=living-room&url=https://example.com/low" http://localhost:8080/setLowTrigger
```

Send sensor reading (existing GET endpoint) — this will evaluate triggers after saving:

```bash
curl "http://localhost:8080/saveSensorInformation?sensor=living-room&temp=22.0&hum=45&batt=3.7"
```

Notes:
- POST bodies are parsed as `application/x-www-form-urlencoded` (key=value pairs joined with `&`).
- By default settings are stored in `settings.txt` and a JSON mirror is written to `settings.json` in the repository root. These paths are configurable via `SETTINGS_FILE` and `SETTINGS_JSON_FILE`.
- Trigger URLs are executed in background using `curl` (ensure `curl` is installed on the host).
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

- `./server 8000` — run on port 8000
- `./server -v` — enable verbose request logging

## Tests

Run unit tests:

```bash
make test
```

## License

This project is licensed under the GNU General Public License v3 or
later. See the COPYING file for the full license text.

Author: Jakub

## Examples: Settings and triggers

Set desired temperature for a room (POST, form-encoded):

```bash
curl -X POST -d "room=living-room&desired=21.5" http://localhost:8080/setDesiredTemperature
```

Set the URL to call when measured temperature is higher than desired:

```bash
curl -X POST -d "room=living-room&url=https://example.com/high" http://localhost:8080/setHighTrigger
```

Set the URL to call when measured temperature is lower than desired:

```bash
curl -X POST -d "room=living-room&url=https://example.com/low" http://localhost:8080/setLowTrigger
```

Send sensor reading (existing GET endpoint) — this will evaluate triggers after saving:

```bash
curl "http://localhost:8080/saveSensorInformation?sensor=living-room&temp=22.0&hum=45&batt=3.7"
```

Notes:
- POST bodies are parsed as `application/x-www-form-urlencoded` (key=value pairs joined with `&`).
- Settings are stored in `data/settings.txt` as simple tab-separated lines: `room\tdesired\thigh_url\tlow_url`.
- Trigger URLs are executed in background using `curl` (ensure `curl` is installed on the host).

GET examples (read-only endpoints):

- Get the web UI / index:

```bash
curl http://localhost:8080/
```

- Get a single sensor's last stored JSON payload (replace `<id>`):

```bash
curl http://localhost:8080/sensor/<id>
```

- Get all sensors as JSON:

```bash
curl http://localhost:8080/sensors
```

- Get all settings as JSON:

```bash
curl http://localhost:8080/settings
```

- Get settings for a specific room (replace `<room>`):

```bash
curl http://localhost:8080/settings/<room>
```
# Shelly_Heating_Control

Simple C++ HTTP server for receiving and serving sensor data.

Build
-----

Requires a C++17 compiler (g++). From the project root:

```bash
make
```

Run
---

Default:

```bash
./server
```

Options:

- `./server 8000` — run on port 8000
- `./server -v` — enable verbose request logging

Tests
-----

Run unit tests:

```bash
make test
```

License
-------

This project is licensed under the GNU General Public License v3 or
later. See the COPYING file for the full license text.
