# RC Car USB Serial Protocol

Line-delimited JSON over USB UART0 (115200 8N1). CRC rules match host tooling (`test-scripts/cmd.py`).

## Transport

| Parameter | Value                                                                 |
| --------- | --------------------------------------------------------------------- |
| Interface | USB UART bridge on ESP32 dev board (`/dev/ttyUSB*` or `/dev/ttyACM*`) |
| UART      | UART0 (protocol only; console disabled)                               |
| Baud      | 115200 8N1                                                            |
| Framing   | One JSON object per line, terminated with `\n`                        |
| Max line  | 256 bytes                                                             |

## Host connect

Open the serial port, then send `get_device_info` to handshake. Optionally follow with `get_state` to sync current levels. The host may retry `get_device_info` briefly if the port was just opened and the ESP32 is still booting (e.g. after DTR reset).

## CRC16-CCITT

- Polynomial `0x1021`, initial value `0xFFFF`
- Host commands: CRC over sorted compact `cmd`+`data` JSON without the root closing `}` (see `command_crc_body` in `test-scripts/cmd.py`)
- Device validates by hashing the raw line prefix before the last `,"crc":`
- Device responses/events: canonical JSON body + appended `,"crc":N}`

## Throttle levels (signed)

This car moves **forward** when throttle voltage is **below** the center point. Brake and reverse use voltage **above** the center point (host owns timing and reverse sequencing).

| `throttle_level` | Meaning                                     |
| ---------------- | ------------------------------------------- |
| `0`              | Neutral                                     |
| `+1` … `+66`     | Forward; higher = faster (66 = max forward) |
| `-1` … `-65`     | Above center point (brake / reverse pose)   |

Firmware maps levels 1:1 to DAC steps internally. Host never sees raw codes.

## Steer levels (signed)

| `steer_level` | Meaning |
| ------------- | ------- |
| `0`           | Center  |
| `-1` … `-98`  | Left    |
| `+1` … `+143` | Right   |

## Commands

### `heartbeat`

```json
{"cmd":"heartbeat","data":{},"crc":0}
```

Response: `{"message":"Heartbeat","status":"ok","crc":...}`

### `get_device_info`

```json
{"cmd":"get_device_info","data":{},"crc":0}
```

Response:

```json
{
  "device": "rc-car",
  "firmware": "1.0.0",
  "lights_assumed_mode": "steady",
  "protocol": 1,
  "status": "ok",
  "steer_level_max": 143,
  "steer_level_min": -98,
  "steer_neutral_level": 0,
  "throttle_above_level_max": 65,
  "throttle_forward_level_max": 66,
  "throttle_neutral_level": 0,
  "crc": 0
}
```

### `get_state`

```json
{"cmd":"get_state","data":{},"crc":0}
```

Response:

```json
{
  "status": "ok",
  "steer_level": 0,
  "throttle_level": 0,
  "crc": 0
}
```

Returns last applied levels (not sensor feedback). Useful after USB reconnect.

### `set_controls`

At least one field required. Integer fields only (no floats).

```json
{"cmd":"set_controls","data":{"throttle_level":3,"steer_level":-5},"crc":0}
```

| Field            | Type | Range          |
| ---------------- | ---- | -------------- |
| `throttle_level` | int  | `-65` … `+66`  |
| `steer_level`    | int  | `-98` … `+143` |

Legacy v1 fields `throttle` / `steer` (float) are rejected.

Response: `{"status":"ok","crc":...}`

### `cycle_lights`

One BC547 pulse on GPIO 4 (LOW → HIGH ~100 ms → LOW). Each call advances the car's light mode on the receiver.

```json
{"cmd":"cycle_lights","data":{},"crc":0}
```

Suggested host cycle: `off → steady → slow_blink → fast_blink → off`

## Host brake and reverse (not in firmware)

Brake and reverse are **not** separate commands. The host sets negative `throttle_level` and owns hold timing.

Suggested reverse sequence:

1. Forward: `throttle_level` `+k`
2. Brake: `throttle_level` `-1` (or any negative); host holds ~1 s
3. Neutral: `throttle_level` `0`
4. Reverse: `throttle_level` `-k` (above center point; receiver state after sequence)

## Safe BCI forward creep

- Start at `throttle_level: 0`
- Each “faster” intent: increment by `+1` (one DAC step)
- Stop from forward: `throttle_level: -1`, hold ~1 s, then `0`
- Cap max forward in host config (e.g. never exceed `+10` until calibrated)

## Host bridge (GUI + WebSocket)

For interactive driving and future NeuraFlow web integration, use **`neuraflow-rc-car-bridge`**:

```bash
cd neuraflow-rc-car-bridge
python -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
python main.py
```

- Desktop GUI: serial port picker, live levels, system log
- Keyboard: ↑↓ throttle, ←→ steer, **B** brake, **L** cycle lights, **Space** neutral
- Gamepad (Forza-style): left stick X steer, **RT/LT** net throttle (RT − LT)
- WebSocket: `ws://127.0.0.1:8801` (see bridge `README.md` for JSON protocol)

## Host CLI

One-shot scripting (no GUI):

```bash
cd test-scripts
pip install -e .
python rc_car.py --port /dev/ttyUSB0 info
python rc_car.py --port /dev/ttyUSB0 set --throttle-level 5
python rc_car.py --port /dev/ttyUSB0 set --throttle-level -1
python rc_car.py --port /dev/ttyUSB0 set --steer-level -10
python rc_car.py --port /dev/ttyUSB0 state
python rc_car.py --port /dev/ttyUSB0 cycle-lights
```

## Hardware notes

- Throttle: `DAC_CHAN_0` / GPIO 25
- Steer: `DAC_CHAN_1` / GPIO 26
- Lights: GPIO 4 via BC547 (idle LOW)
- Pulse duration: `LIGHTS_PULSE_HIGH_MS` in `main/config.h` (default 100 ms)
