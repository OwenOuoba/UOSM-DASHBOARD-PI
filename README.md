# UOSM Dashboard (Raspberry Pi)

Dashboard of the uOttawa Supermileage car. Runs on the Raspberry Pi, receives the
telemetry over MQTT (`uosm/car1/tlm`, Telemetry V2 JSON) and shows it.

## Layout

```
src/         dashboard code (main, MQTT topics, decoder, vehicle state, display, ui.h)
lib/cjson/   cJSON v1.7.19 (third-party JSON parser)
tests/       decoder tests (run on any PC)
docs/        DASHBOARD.md (design and decisions), MQTT_TOPICS.md (what the dashboard reads)
```

## Build and run (on the Pi)

```bash
sudo apt install libmosquitto-dev mosquitto-clients
cmake -B build && cmake --build build
./build/uosm_dashboard            # broker on localhost
./build/uosm_dashboard <broker>   # another broker
./build/test_decode               # decoder tests
```

Details: [docs/DASHBOARD.md](docs/DASHBOARD.md).
