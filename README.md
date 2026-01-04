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
