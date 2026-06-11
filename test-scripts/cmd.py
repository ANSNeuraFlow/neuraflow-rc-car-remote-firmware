"""Host-side serial JSON protocol helpers for neuraflow-rc-car-remote-firmware."""

from __future__ import annotations

import json
import time
from typing import Any

import serial

RECV_BUFFER = ""


def crc16_ccitt(data: str, poly: int = 0x1021, init: int = 0xFFFF) -> int:
    crc = init
    for ch in data.encode("utf-8"):
        crc ^= ch << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ poly
            else:
                crc <<= 1
            crc &= 0xFFFF
    return crc


def validate_crc(payload: dict[str, Any]) -> bool:
    if "crc" not in payload:
        print("No CRC field found in payload")
        return False

    crc_received = payload.pop("crc")
    json_str = json.dumps(payload, separators=(",", ":"), sort_keys=True)
    crc_calculated = crc16_ccitt(json_str)
    payload["crc"] = crc_received

    if crc_received == crc_calculated:
        return True

    print(f"CRC mismatch! Received: {crc_received}, Calculated: {crc_calculated}")
    print(f"  Canonical: {json_str}")
    return False


def command_crc_body(cmd: str, data: dict[str, Any]) -> str:
    payload = {"cmd": cmd, "data": data}
    canonical = json.dumps(payload, separators=(",", ":"), sort_keys=True)
    return canonical[:-1]


def build_command(cmd: str, data: dict[str, Any]) -> str:
    body = command_crc_body(cmd, data)
    crc = crc16_ccitt(body)
    return body + f',"crc":{crc}' + "}"


def reset_recv_buffer() -> None:
    global RECV_BUFFER
    RECV_BUFFER = ""


def read_lines(
    ser: serial.Serial,
    timeout_s: float = 0.5,
    *,
    accept_events: bool = True,
    accept_responses: bool = True,
) -> list[dict[str, Any]]:
    global RECV_BUFFER

    deadline = time.time() + timeout_s
    messages: list[dict[str, Any]] = []

    while time.time() < deadline:
        chunk = ser.read(ser.in_waiting or 1)
        if not chunk:
            time.sleep(0.01)
            continue

        RECV_BUFFER += chunk.decode(errors="replace")

        while "\n" in RECV_BUFFER:
            line, RECV_BUFFER = RECV_BUFFER.split("\n", 1)
            line = line.strip()
            if not line:
                continue

            print("Received:", line)

            try:
                payload: dict[str, Any] = json.loads(line)
            except json.JSONDecodeError as error:
                print("JSON error:", error)
                continue

            if not validate_crc(payload):
                continue

            if payload.get("event") and accept_events:
                messages.append(payload)
            elif payload.get("status") and accept_responses:
                messages.append(payload)
            elif accept_events or accept_responses:
                messages.append(payload)

    return messages


def read_events(ser: serial.Serial, timeout_s: float = 0.5) -> list[dict[str, Any]]:
    return [
        message
        for message in read_lines(
            ser, timeout_s=timeout_s, accept_events=True, accept_responses=False
        )
        if message.get("event")
    ]


def open_serial(port: str, baud: int = 115200) -> serial.Serial:
    reset_recv_buffer()
    ser = serial.Serial(port, baud, timeout=0.1)
    ser.dtr = True
    ser.rts = False
    return ser


def send_command(
    ser: serial.Serial, cmd: str, data: dict[str, Any], timeout_s: float = 5.0
) -> dict[str, Any]:
    global RECV_BUFFER

    msg = build_command(cmd, data)
    ser.write((msg + "\n").encode())
    ser.flush()
    print("Sent:", msg)

    deadline = time.time() + timeout_s
    last_line: str | None = None

    while time.time() < deadline:
        chunk = ser.read(ser.in_waiting or 1)
        if not chunk:
            time.sleep(0.01)
            continue

        RECV_BUFFER += chunk.decode(errors="replace")

        while "\n" in RECV_BUFFER:
            line, RECV_BUFFER = RECV_BUFFER.split("\n", 1)
            line = line.strip()
            if not line:
                continue

            last_line = line
            print("Received:", line)

            try:
                payload: dict[str, Any] = json.loads(line)
            except json.JSONDecodeError as error:
                print("JSON error:", error)
                continue

            if payload.get("event"):
                print("(ignored event while waiting for command response)")
                continue

            if not validate_crc(payload):
                continue

            if payload.get("status"):
                return payload

    hint = f" Last line: {last_line!r}" if last_line else " No lines received."
    raise TimeoutError(f"No valid response for command {cmd}.{hint}")
