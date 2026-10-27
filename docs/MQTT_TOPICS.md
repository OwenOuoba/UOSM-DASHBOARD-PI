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
| QoS / retain | QoS 1, retained: the dashboard gets the last sample as soon as it connects |
| Broker | `MQTT_HOST` environment variable (default `localhost`) |

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
- **No message for 3 s** (3 missed periods): every value is shown as `--`, since the
  displayed data can no longer be trusted.

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
