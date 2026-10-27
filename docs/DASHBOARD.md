# UOSM Dashboard (Raspberry Pi Zero 2 W)

Linux version of the dashboard. It subscribes to the car's MQTT topics, decodes the telemetry, keeps the latest known state of the vehicle, and prints it to the terminal.

Current status: **working end to end on the Pi**, tested with `mosquitto_pub` in place of the telemetry board.

---

## 1. Data flow

```
broker ──> on_message (main.c)
       ──> mqtt_topics_dispatch (mqtt_topics.c)   topic table
       ──> handle_telemetry -> telemetry_decode (mqtt_decode.c + cJSON)
       ──> on_telemetry (main.c)                  registered handler
       ──> vehicle_state_update (vehicle_state.c) merge + timestamps
       ──> display_print (display.c)              one line in the terminal
```

## 2. Files

| File | Role | Platform-specific? |
|---|---|---|
| `src/main.c` | Connects to the broker (libmosquitto), reads the clock, wires the modules together | Yes (Linux) |
| `src/mqtt_topics.c/.h` | Topic table: topic → handler. Calls the telemetry handler registered by `main` | No |
| `src/mqtt_decode.c/.h` | Decodes the JSON payload into a `telemetry_t` (one `has_` flag per field) | No |
| `lib/cjson/cJSON.c/.h` | JSON parser (third-party) | No |
| `src/vehicle_state.c/.h` | Latest value of every field + when it was received | No |
| `src/display.c/.h` |
| `src/ui.h` | Contract between the core and the LVGL screen (`dashboard_state_t`, `ui_init`, `ui_update`) | No |
| `tests/test_decode.c` | Decoder tests, run on any PC | No | Prints the state on one terminal line | No (uses `printf`) |

Only `main.c` depends on Linux. Everything else can compile on the STM32 unchanged.

---

## 3. Decisions and why

### main.c

- **Subscribe in `on_connect`, not in `main`.** The client uses a clean session, so the broker forgets subscriptions on disconnect. `on_connect` runs on every (re)connection, so the subscription is restored automatically.
- **Broker address is an argument, default `localhost`.** `./uosm_dashboard <host> [port]`. Changing broker never requires a code change.
- **`mosquitto_loop_forever` is kept.** The display reacts to messages, so `main` never needs control back. (See section 5 for when this changes.)
- **Clock: `CLOCK_MONOTONIC`.** We only measure durations (ages). The Pi has no battery-backed clock, so wall-clock time can jump at boot when it syncs; the monotonic clock never jumps.
- **The clock is read once per message** and the same `now` is passed to both `vehicle_state_update` and `display_print`, so fields that just arrived have an age of exactly 0.
- **Setup before connecting.** `vehicle_state_init()` and `mqtt_topics_set_telemetry_handler()` are called before `mosquitto_connect`, so no early message is lost.

### mqtt_topics

- **Handler registration** (`mqtt_topics_set_telemetry_handler`). `mqtt_topics` decodes but doesn't decide what to do with the result; the user of the module registers a function. Same module on Pi (print) and STM32 (screen).
- If no handler is registered, messages are decoded and silently dropped.

### vehicle_state

1. **What is stored: each field separately, merged.** Messages can be partial (`has_` flags). A field is only overwritten when the message carries it; the others keep their previous value. In the state, `has_x` means "received at least once".
2. **How staleness is known: one timestamp per field.** A single timestamp for the whole state would be refreshed by any message, hiding a field whose source died. The *arrival time* is stored, not the age; the age is computed when read (`age = now − arrival`).
3. **Who reads the clock: the caller.** `vehicle_state` takes `now_s` as a parameter. It stays free of any OS/board dependency and can be tested with fake times.
4. **How the display reads: a snapshot.** `vehicle_state_snapshot()` copies the data out and computes every age with the same `now`. The display can't modify the real state, all values come from the same instant, and a lock (if ever needed) only covers the copy.

The state does **not** decide what "too old" means. That is the display's job.

### display

- **Printed on every message**, from `on_telemetry`. Simplest option, no loop change.
- **One new line per message** (endless scrolling). Gives a history that can be scrolled or redirected to a log.
- **Every field is shown.** Three looks, checked in this order:

  | Case | Shown as |
  |---|---|
  | never received | `v=---` |
  | older than 2 s | `v=48.20(5s)` |
  | fresh | `v=48.20` |

- **Stale threshold: 2 s for every field** (`STALE_S` in `display.c`). Rule used: about 2–3× the expected publish period. Assumes ~1 Hz publishing; not confirmed yet.
- `has_` is always checked before the age: the age of a never-received field is meaningless.

---

## 4. Build, run, test

### Build (CLion, remote toolchain on the Pi)

The root `CMakeLists.txt` builds two executables:

- `uosm_dashboard`: the dashboard (`src/` + `lib/cjson/`, linked with `mosquitto`)
- `test_decode`: the decoder tests (no broker needed)

- The project is **C, not C++**: `main.c`, not `main.cpp` (C++ name mangling breaks linking with the C files).
- Every new `.c` file must be added to `CORE_SOURCES` in `CMakeLists.txt`, otherwise the link fails with `undefined reference`.
- Pi packages: `sudo apt install libmosquitto-dev mosquitto-clients`

### Run

```bash
./uosm_dashboard              # broker on the Pi (localhost)
./uosm_dashboard <broker_ip>  # another broker
./test_decode                 # decoder tests
```

### Test without the telemetry board

Two SSH terminals on the Pi.

Terminal 1: run the dashboard.

Terminal 2:

```bash
mosquitto_pub -t uosm/car1/tlm -m '{"spd":12.5,"v":48.2,"i":3.1,"lts":"10100"}'
# wait 3–4 s
mosquitto_pub -t uosm/car1/tlm -m '{"spd":13.0}'
```

Expected: the second line shows `spd=13.0` fresh and `v`, `i`, `lts` with their age.

If nothing prints: check the topic is exactly `uosm/car1/tlm` (other topics are ignored), check the JSON is valid, and check the broker receives it with `mosquitto_sub -t "uosm/#" -v`.

---

## 5. What will need to change, and what triggers it

| Change | Trigger | What to do |
|---|---|---|
| **Detect total silence** | Before running in the car, or as soon as real telemetry is sent. Today, if telemetry stops completely, nothing prints and the last line looks normal. | Replace `mosquitto_loop_forever` with our own loop (`mosquitto_loop(mosq, 100, 1)` + `mosquitto_reconnect` on error) that also prints a warning when no message has arrived for N seconds. |
| **Redraw at a fixed rate** instead of per message | Message rate above ~5 Hz (terminal unreadable), or moving to a real screen. | Same own loop: redraw every ~0.5 s from a snapshot, independently of messages. |
| **Per-field stale thresholds** | Real publish rates of each field are known. | Replace `STALE_S` with one threshold per field (≈ 2–3× its period). |
| **Mutex in `vehicle_state`** | MQTT moved to its own thread (`mosquitto_loop_start`), e.g. for a GUI. | Lock around `update` and around the copy in `snapshot`. Not needed while everything runs on one thread. |
| **Start at boot / retry if broker is down** | The Pi runs headless in the car. Today, if the broker isn't reachable at startup, the program exits. | systemd service with `After=network-online.target`, `Restart=on-failure`, `RestartSec=2`. No code change. |
| **New topics** | A new message type is defined. | Add it to `docs/MQTT_TOPICS.md` first, then one line in the topic table, then the matching fields in `vehicle_state` and `display`. |
| **Broker location** | Team decision on the car's network (CAN / MQTT / mix). | Only the launch argument changes. If the broker must accept other machines: `listener 1883` + `allow_anonymous true` (or credentials) in its config. Note: the Pi Zero 2 W has no Ethernet port (USB adapter or HAT needed for wired MQTT). |
| **Unique client ID** | Two dashboards (or two instances) on the same broker. | `"uosm-dashboard"` is fixed; two clients with the same ID kick each other off. Make it an argument or add a suffix. |
| **Robustness fixes** | Before the car, or when chasing a silent failure. | Check return values of `mosquitto_subscribe` / `mosquitto_loop_forever`; handle Ctrl+C cleanly; validate the port instead of `atoi`. |
| **Display decimals** | Units and precision of each field are confirmed. | Adjust the `decimals` argument of each `print_num` call in `display.c`. |
| **Build output location** | Now. CLion builds in `/tmp`, which is wiped at every reboot of the Pi. | Change the deployment mapping in CLion to a permanent folder (e.g. `/home/user/uosm/`), or copy the executable there. |
| **Header comment of `main.c`** | Now. It still says "Debian VM". | Update to Raspberry Pi Zero 2 W. |

---

## 6. Next steps

1. **Silence detection** (own loop instead of `mosquitto_loop_forever`).
2. **systemd service** so the dashboard starts at boot and retries until the broker is up.
3. **LVGL screen** (`src/ui.c`, contract in `src/ui.h`), on the Pi's 1600x900 HDMI screen.

Open questions:
- How often does the car publish on `uosm/car1/tlm`? (confirms or adjusts the 2 s threshold)
- Where does the broker live in the final car?
