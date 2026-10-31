# UOSM Dashboard (Raspberry Pi Zero 2 W)

Linux version of the dashboard. It subscribes to the car's MQTT topics, decodes the telemetry, keeps the latest known state of the vehicle, and prints it to the terminal.

Current status: **working end to end on the Pi**, tested with `mosquitto_pub` in place of the telemetry board.

---

## 1. Data flow

Everything runs in **one thread**, in the main loop of `main.c` (option A, section 3):

```
main loop, every turn:
  mosquitto_loop (≤ 5 ms) ──> on_message (main.c)
                          ──> mqtt_topics_dispatch (mqtt_topics.c)   topic table
                          ──> handle_telemetry -> telemetry_decode (mqtt_decode.c + cJSON)
                          ──> on_telemetry (main.c)                  registered handler
                          ──> vehicle_state_update (vehicle_state.c) merge + timestamps
main loop, every second:
  display_print (display.c) ──> vehicle_state_snapshot ──> one line in the terminal
```

## 2. Files

| File | Role | Platform-specific? |
|---|---|---|
| `src/main.c` | Runs the main loop: connects and reconnects to the broker (libmosquitto), reads the clock, prints at a fixed rate, wires the modules together | Yes (Linux) |
| `src/mqtt_topics.c/.h` | Topic table: topic → handler. Calls the telemetry handler registered by `main` | No |
| `src/mqtt_decode.c/.h` | Decodes the JSON payload into a `telemetry_t` (one `has_` flag per field) | No |
| `lib/cjson/cJSON.c/.h` | JSON parser (third-party) | No |
| `src/vehicle_state.c/.h` | Latest value of every field + when it was received; `STALE_S` threshold | No |
| `src/display.c/.h` | Prints the state on one terminal line | No (uses `printf`) |
| `src/ui.h` | Contract between the core and the LVGL screen (`dashboard_state_t`, `ui_init`, `ui_update`) | No |
| `tests/test_decode.c` | Decoder tests, run on any PC | No |

Only `main.c` depends on Linux. Everything else can compile on the STM32 unchanged.

---

## 3. Decisions and why

### main.c

- **Subscribe in `on_connect`, not in `main`.** The client uses a clean session, so the broker forgets subscriptions on disconnect. `on_connect` runs on every (re)connection, so the subscription is restored automatically.
- **Broker address is an argument, default `localhost`.** `./uosm_dashboard <host> [port]`. Changing broker never requires a code change.
- **Option A: our own main loop, one thread** (instead of `mosquitto_loop_forever`, which never gave control back). Each turn: `mosquitto_loop(mosq, 5, 1)` handles the network for at most 5 ms and runs the callbacks; every second the terminal line is printed; later the screen goes in the same loop (`lv_timer_handler()`, `ui_update()` every 100 ms).
  - **Why A and not B** (B = `mosquitto_loop_start()`: MQTT in its own thread + a mutex in `vehicle_state`): with one thread, nothing is read while it is being written, so **no mutex and no concurrency bug** is possible, and the order of events can be read line by line. Chosen by the team. B only becomes useful if the loop ever has to do something long.
  - **The one rule of A: nothing in the loop may block for long**, or the network and the screen freeze together. Hence `mosquitto_connect_async()` / `mosquitto_reconnect_async()` (they start the connection and return at once; `mosquitto_loop()` finishes it), no `sleep` except a 10 ms pause when there is no connection (otherwise `mosquitto_loop()` returns at once and the loop spins at 100 % CPU), and the broker given as an **IP address** (resolving a host name can block).
  - `mosquitto_loop()` must run often: it also sends the keep-alive. A loop that stalls makes the broker drop the dashboard.
- **Reconnection is ours** (`loop_forever` used to do it): when `mosquitto_loop()` fails, `mosquitto_reconnect_async()` at most every 2 s (`RECONNECT_S`). The subscription comes back by itself, since it is made in `on_connect`. A broker that is down at startup is **no longer fatal**: the program keeps retrying instead of exiting. Until a first `mosquitto_connect_async()` succeeds, the retry calls `connect_async` again, since `reconnect_async` reuses the host and port of a successful one.
- **`mqtt_connected` instead of a ping** (`s_mqtt_connected`, set in `on_connect`, cleared in `on_disconnect`). Printed as `[no broker]` on the terminal, later on the screen. What each one tells:

  | Situation | Ping to the router | `mqtt_connected` | Data age |
  |---|---|---|---|
  | All good | yes | yes | fresh |
  | Telemetry stopped sending (down, or unable to send) | yes | yes | **old** |
  | Broker stopped | yes | no | old |
  | Dashboard network down | no | no | old |

  `mqtt_connected` plus the data age already separate the first three cases, for free (libmosquitto's keep-alive). A ping only separates the last two, and only when the broker is not on the pinged machine. `mqtt_connected` true implies the network path to the broker works; a ping that answers does not imply MQTT works. Neither says whether the telemetry is publishing: only the data age does. A real ping (ICMP) also needs root or a Linux capability, or calling the `ping` program, plus its own timer. So: no ping for the driver; possible later on a debug screen. (`src/ui.c` still shows `NET … ms` from `network_ping_ms`: to align.)
- **Clean stop.** Ctrl+C (SIGINT) or `systemctl stop` (SIGTERM) only sets a flag; the loop ends, then `mosquitto_disconnect()` and `mosquitto_destroy()`.
- **Clock: `CLOCK_MONOTONIC`.** We only measure durations (ages). The Pi has no battery-backed clock, so wall-clock time can jump at boot when it syncs; the monotonic clock never jumps.
- **Two clocks, two uses.** Staleness uses the Pi's monotonic clock (durations only). The time of day shown to the driver (`clock_ms` in `ui.h`) comes from the GPS `ts` when valid, else `--`: the Pi has no battery-backed clock and no Internet time in the car.
- **The clock is read when a message is stored** (`on_telemetry`) and again when the line is printed, so an age is the time between the two.
- **Setup before connecting.** `vehicle_state_init()` and `mqtt_topics_set_telemetry_handler()` are called before `mosquitto_connect_async`, so no early message is lost.
- **Subscribe to `TOPIC_TELEMETRY` in QoS 1.** The topic name is defined once, in `mqtt_topics.h`, and used by both the topic table and the subscription. QoS 1 is what the Telemetry V2 specification requires. The return value of `mosquitto_subscribe` is checked.
- **Retained message dropped** (`on_message`, `msg->retain`). Right after subscribing, the broker sends the last sample it kept. It can be hours old, but its age is measured from its arrival, so it would look fresh. Dropping it costs `--` for at most 1 s at startup.
- **Duplicates and late messages dropped** (`on_telemetry`, `s_last_ts`). QoS 1 may deliver a message twice. A message whose `ts` is not newer than the last accepted one is ignored, so a late message never replaces a newer value. Messages without `ts` are accepted (nothing to compare).

### mqtt_topics

- **Handler registration** (`mqtt_topics_set_telemetry_handler`). `mqtt_topics` decodes but doesn't decide what to do with the result; the user of the module registers a function. Same module on Pi (print) and STM32 (screen).
- If no handler is registered, messages are decoded and silently dropped.

### vehicle_state

1. **What is stored: each field separately, merged.** Messages can be partial (`has_` flags). A field is only overwritten when the message carries it; the others keep their previous value. In the state, `has_x` means "received at least once".
2. **How staleness is known: one timestamp per field.** A single timestamp for the whole state would be refreshed by any message, hiding a field whose source died. The *arrival time* is stored, not the age; the age is computed when read (`age = now − arrival`).
3. **Who reads the clock: the caller.** `vehicle_state` takes `now_s` as a parameter. It stays free of any OS/board dependency and can be tested with fake times.
4. **How the display reads: a snapshot.** `vehicle_state_snapshot()` copies the data out and computes every age with the same `now`. The display can't modify the real state, all values come from the same instant, and a lock (if ever needed) only covers the copy.

The state does **not** apply the "too old" rule: it only stores times. The threshold `STALE_S` is defined in `vehicle_state.h` only so that every display (terminal, screen) uses the same value.

### display

- **Printed every second by the main loop** (`PRINT_PERIOD_S`), no longer on every message. A line still comes when nothing arrives, so a silent telemetry shows as ages growing past `STALE_S`: this is the total-silence detection. Prefixed `[no broker]` while disconnected.
- **One new line per second** (endless scrolling). Gives a history that can be scrolled or redirected to a log.
- **Every field is shown.** Three looks, checked in this order:

  | Case | Shown as |
  |---|---|
  | never received | `v=---` |
  | older than `STALE_S` | `v=48.20(5s)` |
  | fresh | `v=48.20` |

- **Stale threshold: 3 s for every field** (`STALE_S` in `vehicle_state.h`). Rule used: about 2–3× the expected publish period. At 1 Hz, one lost message is tolerated and the second one is reported (with 2 s, a single lost message would already flag the value). Assumes ~1 Hz publishing; not confirmed yet.
- **Terminal and screen differ on purpose:** the terminal shows a stale value with its age (useful for debugging), the future screen shows `--` (`valid = false` in `ui.h`), because the driver only needs to know whether to trust it.
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
# wait 4–5 s (more than STALE_S = 3 s)
mosquitto_pub -t uosm/car1/tlm -m '{"spd":13.0}'
```

Expected: the second line shows `spd=13.0` fresh and `v`, `i`, `lts` with their age.

Notes: do not add `-r` (retain) to these commands, retained messages are ignored by design. These test messages have no `ts`, so the duplicate filter accepts them all; to test it, send the same message with a `ts` twice: the second one is ignored.

If the values never change: check the topic is exactly `uosm/car1/tlm` (other topics are ignored), check the JSON is valid, and check the broker receives it with `mosquitto_sub -t "uosm/#" -v`.

### Test the main loop (option A)

1. Run the dashboard: one line per second, even without any message.
2. Stop the broker: `sudo systemctl stop mosquitto`. The dashboard keeps running, prints `Disconnected from broker…`, then `[no broker]` lines, and retries every 2 s.
3. Start the broker again: `sudo systemctl start mosquitto`. `Connected to broker, subscribing…` comes back by itself, and published messages are received again.
4. Publish one message, then nothing: the ages grow every second and pass `STALE_S` (3 s).
5. Start the dashboard while the broker is stopped: it does not exit, it waits and connects once the broker is up.
6. Ctrl+C: `Stopping`, then the program exits.

---

## 5. What will need to change, and what triggers it

| Change | Trigger | What to do |
|---|---|---|
| **Screen in the main loop** | The LVGL screen (`src/ui.c`) is wired to the dashboard. | In the loop of `main.c`: `lv_timer_handler()` every turn, and every 100 ms `vehicle_state_snapshot()` → `dashboard_state_t` (`valid` = received and younger than `STALE_S`) → `ui_update()`. Callbacks never touch LVGL (`ui.h`, rule 3). |
| **Per-field stale thresholds** | Real publish rates of each field are known. | Replace `STALE_S` with one threshold per field (≈ 2–3× its period). |
| **Option B: MQTT in its own thread + mutex** | Only if the loop ever has to do something long (blocking), which a dashboard should not. | `mosquitto_loop_start()` instead of the loop's `mosquitto_loop()`; lock around `update` and around the copy in `snapshot`; `s_mqtt_connected` under the lock or atomic. Not needed with option A. |
| **Start at boot** | The Pi runs headless in the car. | systemd service with `After=network-online.target`, `Restart=on-failure`, `RestartSec=2`. The program already retries the broker by itself; systemd covers the boot and a crash. No code change. |
| **New topics** | A new message type is defined. | Add it to `docs/MQTT_TOPICS.md` first, then one line in the topic table, then the matching fields in `vehicle_state` and `display`. |
| **Broker location** | Team decision on the car's network (CAN / MQTT / mix). | Only the launch argument changes. If the broker must accept other machines: `listener 1883` + `allow_anonymous true` (or credentials) in its config. Note: the Pi Zero 2 W has no Ethernet port (USB adapter or HAT needed for wired MQTT). |
| **Unique client ID** | Two dashboards (or two instances) on the same broker. | `"uosm-dashboard"` is fixed; two clients with the same ID kick each other off. Make it an argument or add a suffix. |
| **Real age from `ts`** | The dashboard's broker is outside the car (e.g. the server), so it can receive the burst of buffered messages the telemetry sends after a link loss (specification rule 5). Today these late messages are dropped only if older than the last one received; a burst arriving after the link comes back would still be shown as fresh. | Estimate the offset between GPS time and the monotonic clock (largest `ts − arrival` seen = live messages), store each field with its measurement time instead of its arrival time, and only overwrite a field with a newer measurement. Not needed if the broker is inside the car. |
| **Robustness fixes** | Before the car, or when chasing a silent failure. | Validate the port instead of `atoi`. (Done: `mosquitto_subscribe` checked, clean stop on Ctrl+C / SIGTERM, reconnection.) |
| **Display decimals** | Units and precision of each field are confirmed. | Adjust the `decimals` argument of each `print_num` call in `display.c`. |
| **Build output location** | Now. CLion builds in `/tmp`, which is wiped at every reboot of the Pi. | Change the deployment mapping in CLion to a permanent folder (e.g. `/home/user/uosm/`), or copy the executable there. |
---

## 6. Other decisions agreed with the team

- **No logging on the dashboard.** The server already receives and stores all the telemetry (Telemetry V2), so the dashboard records nothing itself.
- **Battery voltage, current, power and throttle** (`v`, `i`, `e`, `thr`) are **not on the main screen** for now: maybe a second view later. They stay in `vehicle_state` and on the terminal.
- **Run button:** the feature does not exist on the car yet. `run_time_s` stays `valid = false` until it does.

---

## 7. Next steps

1. **Test option A on the Pi** (section 4, "Test the main loop").
2. **systemd service** so the dashboard starts at boot.
3. **Wire the LVGL screen** (`src/ui.c`, contract in `src/ui.h`) into the main loop (section 5, "Screen in the main loop").

Open questions:
- How often does the car publish on `uosm/car1/tlm`? (confirms or adjusts the 3 s threshold)
- Where does the broker live in the final car?
