# MQTT topics — UOSM dashboard

What the dashboard listens to and publishes on MQTT. The car's data format is
defined by the **Telemetry V2 MQTT Interface Specification v1.0**; this page does not
redefine it, it only states what the dashboard uses from it.

Rule: a topic or field is added **here first**, then in the code.

## 1. Subscribed: `uosm/car1/tlm` (Telemetry V2)

| Setting | Value |
|---|---|
| Topic | `uosm/car1/tlm` |
| Rate | 1 Hz (one message per second) |
| Payload | one flat JSON object, units fixed by the specification, never in the payload |
| QoS / retain | QoS 1 (the dashboard subscribes in QoS 1). The message is retained by the broker, but the dashboard **ignores the retained copy** (see behaviour below) |
| Broker | Command-line argument: `./uosm_dashboard <broker> [port]` (default `localhost 1883`) |

Example:

    {"ts":1761170325123,"spd":24.5,"dist":1830,"v":47.02,"i":3.15,"e":148,"thr":62,"lts":"11000"}

### Fields used by the dashboard

| Field | Unit | Shown as | Screen |
|---|---|---|---|
| `spd` | km/h | Speed (Hall-effect sensor) | Main, large |
| `v` | V | Battery voltage | Main |
| `i` | A | Battery current | Main |
| `e` | J per second | Power, in W (energy over one second = average power) | Main |
| `thr` | % | Throttle | Main |
| `lts` | 5 characters `0`/`1` | Indicators: headlight, running lights, left turn, right turn, wiper | Main |
| `dist` | m | Distance traveled | Main |
| `ts` | ms, UTC epoch (GPS) | Age of the last message | Debug |
| `gs`, `fix` | km/h, quality code | GPS speed and fix quality | Debug |

Not shown (kept for the server and analysis): `lat`, `lon`, `alt`, `ax`, `ay`, `az`,
`ap`, `l1`, `l2`.

### Dashboard behaviour

- Fields are read **by name**, never by position. Unknown fields are ignored.
- An invalid payload is ignored; the dashboard never crashes because of it.
- A **missing field** means the sensor had no valid value that second: that value is
  shown as `--`.
- **Stale field:** a field not updated for more than `STALE_S` (3 s, `src/vehicle_state.h`)
  is stale. At 1 Hz, one lost message is tolerated and the second one is reported.
  The terminal shows the value with its age (`v=48.20(5s)`, useful for debugging);
  the screen shows `--` (`valid = false` in `src/ui.h`).
- **No message at all:** with `STALE_S`, every field becomes stale after 3 s. Not yet
  reported on the terminal, which only prints when a message arrives (see
  `DASHBOARD.md`, section 5, "Detect total silence").
- **Retained message ignored:** right after subscribing, the broker sends the last
  sample it kept. It can be hours old and would look fresh (its age is measured from
  its arrival), so it is dropped. Cost: `--` for at most 1 s at startup.
- **Duplicates and late messages ignored:** QoS 1 may deliver a message twice. A message
  whose `ts` is not newer than the last accepted one is dropped, so a late message never
  replaces a newer value. Messages without `ts` are accepted.

## 2. Published by the dashboard

Nothing yet.

## 3. Other topics

None yet. Data not covered by the Telemetry V2 specification (for example lap
number or motor controller data) must be added to this page, agreed with the
team, before any code uses it.

## Open points

- **Update rate:** 1 Hz is enough for the pit server; the driver's speed display may
  need more. Kept at 1 Hz for now.
- **Broker location:** the dashboard needs a broker inside the car, so that the
  screen keeps working when the cellular link drops.
