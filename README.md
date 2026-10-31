# UOSM Dashboard (Raspberry Pi)

Dashboard of the uOttawa Supermileage car. Runs on the Raspberry Pi, receives the
telemetry over MQTT (`uosm/car1/tlm`, Telemetry V2 JSON) and shows it.

## Layout

```
src/         dashboard code (main, MQTT topics, decoder, vehicle state, display)
             + the screen: ui.h (contract), ui.c (LVGL layout), ui_assets.c (generated), lv_conf.h
lib/cjson/   cJSON v1.7.19 (third-party JSON parser)
lib/lvgl/    LVGL v9.2.2 (git submodule)
sim/         PC simulator of the screen (SDL2 window, fake values)
assets/      font (Barlow Condensed, SIL OFL)
tools/       make_assets.py: regenerates src/ui_assets.c (font + icons)
tests/       decoder tests (run on any PC)
docs/        DASHBOARD.md (design and decisions), MQTT_TOPICS.md (what the dashboard reads)
```

After cloning or pulling, fetch LVGL once:

```bash
git submodule update --init
```

## Build and run (on the Pi)

```bash
sudo apt install libmosquitto-dev mosquitto-clients
cmake -B build && cmake --build build
./build/uosm_dashboard            # broker on localhost
./build/uosm_dashboard <broker>   # another broker
./build/test_decode               # decoder tests
```

## Screen simulator (PC, Windows + MinGW)

Needs the SDL2 MinGW development package in `../deps/SDL2-2.32.10/` (or pass `-DSDL2_ROOT=...`).

```bash
cmake -B build-sim -G Ninja -DUOSM_SIM=ON && cmake --build build-sim --target ui_sim
./build-sim/ui_sim                          # window: 30 s normal, 10 s "MQTT lost", repeated
./build-sim/ui_sim --shot normal.ppm lost.ppm   # no window: saves both screens
./build-sim/ui_sim --size 1024x600              # another screen size (the layout scales)
```

In CLion: a CMake profile with `-DUOSM_SIM=ON`, target `ui_sim`.

Details: [docs/DASHBOARD.md](docs/DASHBOARD.md).
