# Shelly_Heating_Control

Simple C++ HTTP server for receiving and serving sensor data.

First of all, this was my first try with "vibe coding". Github copilot wrote the first dozen commits. Honestly it did quite well. It wasn't very tidy (see yourself in commit history) and it misinterpreted it's own suggestions (e.g. it suggested using library to parse JSON and after I gave it a go it decided to move from string manipulation to regex) but it still produced a product I could use if I didn't have more requirements I decided to implement myself. Nonetheless, this server (with modifications visible in my later commits) is now my sole back-end for my heating automation at home. I have 6 rooms with Shelly H&T Gen 3 sensors and all rooms have a separate circuit to trigger.

Why did I make it? Because I had a bunch of shelly relays and the H&T Gen 3 sensors I decided to put into use. Feel free to use and modify. If there is something more people would benefit, please add send me a pull request.

Future plans:
1. evaluate how fast the temperature per room is changing after a trigger was fired
2. improve testing

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

## Endpoints (with examples)

### POST (modify state):

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

### Trigger control (new)

- Trigger all configured *high* URLs immediately:

```bash
curl -X POST http://localhost:8080/triggerAllHigh
```

- Trigger all configured *low* URLs immediately:

```bash
curl -X POST http://localhost:8080/triggerAllLow
```

- Disable automatic trigger execution:

```bash
curl -X POST http://localhost:8080/disableTriggers
```

- Enable automatic trigger execution:

```bash
curl -X POST http://localhost:8080/enableTriggers
```

- Check whether trigger execution is enabled (returns JSON):

```bash
curl http://localhost:8080/triggersEnabled
```

### GET (read-only):

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

### DELETE:
- Settings for a specific room (`<room>`)

```bash
curl -X DELETE http://localhost:8080/settings/<room>
```

- All logged triggers

```bash
curl -X DELETE http://localhost:8080/triggerLog
```

## Storage details

- Sensor data: stored per-sensor under the `data/` directory (default). Each sensor has a single file with the last JSON payload.
- Settings: stored in `settings.json` (repository root by default). This is the single canonical source for room settings.
- Triggers: stored in `triggers.log` (repository root by default). This is the single source for log of triggers.
- Triggers execution: performed in-process using `libcurl`; no external `curl` binary is required on the host.

## Behavior note

- **Expect: 100-continue**: The server replies with an interim `HTTP/1.1 100 Continue` response when a client sends the `Expect: 100-continue` header. This prevents clients such as Postman from appearing to stall while waiting to send the request body.

## License

This project is licensed under the GNU General Public License v3 or later. See the `COPYING` file for full license text.

Author: Jakub
```bash
