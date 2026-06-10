# NeuraFlow RC Car Remote Firmware

ESP32 firmware that drives an RC car receiver over **analog throttle/steer (DAC)** and a **lights mode button pulse (GPIO)**. Hosts connect over **USB serial** using line-delimited **JSON + CRC16**.

## Requirements

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/) (tested with ESP32 target)
- ESP32 dev board with USB-UART bridge (CP210x, CH340, etc.)
- RC car receiver wired to DAC outputs and the lights input

## Hardware

| Signal   | ESP32 pin | Driver                                                                  |
| -------- | --------- | ----------------------------------------------------------------------- |
| Throttle | GPIO 25   | `DAC_CHAN_0`                                                            |
| Steer    | GPIO 26   | `DAC_CHAN_1`                                                            |
| Lights   | GPIO 4    | Digital out via BC547 (idle LOW, ~100 ms HIGH pulse per `cycle_lights`) |

Level ranges and DAC mapping live in `main/config.h`. Forward throttle uses voltage **below** the center point; brake/reverse uses **above** center (host owns timing and reverse sequencing).

## Build and flash

```bash
cd neuraflow-rc-car-remote-firmware
idf.py set-target esp32   # first-time only
idf.py build
idf.py -p /dev/ttyUSB0 flash
```

Replace `/dev/ttyUSB0` with your port (`/dev/ttyACM*` on Linux, `COM*` on Windows).

UART0 is dedicated to the protocol (`CONFIG_ESP_CONSOLE_NONE` in `sdkconfig.defaults`). There is no serial console log on the USB port — use the host tools below to talk to the device.

On boot the firmware emits a `ready` event, then accepts commands.

## Serial protocol

Full command reference, CRC rules, level semantics, and brake/reverse guidance:

**[`docs/serial-protocol.md`](docs/serial-protocol.md)**

Quick summary:

| Command           | Purpose                                       |
| ----------------- | --------------------------------------------- |
| `get_device_info` | Firmware version, protocol, level bounds      |
| `get_state`       | Last applied `throttle_level` / `steer_level` |
| `set_controls`    | Set throttle and/or steer (signed integers)   |
| `cycle_lights`    | One pulse on the lights GPIO                  |
| `heartbeat`       | Connectivity check                            |

Example (CRC omitted; use host tooling to compute):

```json
{"cmd":"set_controls","data":{"throttle_level":3,"steer_level":-5},"crc":0}
```

## Host software

### GUI bridge (recommended)

For interactive driving, keyboard/gamepad input, and WebSocket access:

```bash
cd ../neuraflow-rc-car-bridge
python -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
python main.py
```

See the [bridge README](../neuraflow-rc-car-bridge/README.md) for configuration, controls, and WebSocket messages.

### CLI (scripting / smoke tests)

```bash
cd test-scripts
pip install -e .
python rc_car.py --port /dev/ttyUSB0 info
python rc_car.py --port /dev/ttyUSB0 set --throttle-level 5
python rc_car.py --port /dev/ttyUSB0 set --steer-level -10
python rc_car.py --port /dev/ttyUSB0 state
python rc_car.py --port /dev/ttyUSB0 cycle-lights
```

## Project layout

```
main/
  main.c   # app entry
  actuators.c                  # DAC + lights GPIO
  protocol.c                   # JSON command dispatch
  uart_transport.c             # line-framed UART I/O
  crc16.c                      # CRC16-CCITT validation
  config.h                     # pins, level limits, firmware version
docs/
  serial-protocol.md           # protocol specification
test-scripts/
  rc_car.py                    # host CLI
  cmd.py                       # CRC + serial helpers
```

## Related repos

- **`neuraflow-rc-car-bridge`** — local Python bridge (GUI, gamepad, WebSocket)
- **`neuraflow-web`** — future web UI integration via the bridge
