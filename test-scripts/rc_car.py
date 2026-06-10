#!/usr/bin/env python3
"""CLI for neuraflow-rc-car-remote-firmware serial protocol."""

from __future__ import annotations

import argparse
import sys

import serial

from cmd import open_serial, send_command


def build_set_data(args: argparse.Namespace) -> dict[str, int]:
    data: dict[str, int] = {}
    if args.throttle_level is not None:
        data["throttle_level"] = args.throttle_level
    if args.steer_level is not None:
        data["steer_level"] = args.steer_level
    return data


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Control neuraflow-rc-car-remote-firmware over USB serial")
    parser.add_argument("--port", "-p", required=True, help="Serial port (e.g. /dev/ttyUSB0)")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate (default 115200)")
    parser.add_argument(
        "--no-ready",
        action="store_true",
        help="Skip waiting for ready event (port already open)",
    )

    subparsers = parser.add_subparsers(dest="command", required=True)

    subparsers.add_parser("heartbeat", help="Send heartbeat")
    subparsers.add_parser("info", help="Get device info")
    subparsers.add_parser("state", help="Get current throttle/steer levels")
    subparsers.add_parser("cycle-lights", help="Pulse lights mode button once")

    set_parser = subparsers.add_parser("set", help="Set throttle and/or steer levels")
    set_parser.add_argument(
        "--throttle-level",
        type=int,
        help="Throttle level -65..+66 (0=neutral, +k=forward, -k=above CP)",
    )
    set_parser.add_argument(
        "--steer-level",
        type=int,
        help="Steer level -98..+143 (0=center)",
    )

    args = parser.parse_args(argv)

    try:
        if args.no_ready:
            ser = serial.Serial(args.port, args.baud, timeout=0.1)
        else:
            ser = open_serial(args.port, args.baud)

        if args.command == "heartbeat":
            print(send_command(ser, "heartbeat", {}))
        elif args.command == "info":
            print(send_command(ser, "get_device_info", {}))
        elif args.command == "state":
            print(send_command(ser, "get_state", {}))
        elif args.command == "cycle-lights":
            print(send_command(ser, "cycle_lights", {}))
        elif args.command == "set":
            data = build_set_data(args)
            if not data:
                print("Provide --throttle-level and/or --steer-level", file=sys.stderr)
                return 2
            print(send_command(ser, "set_controls", data))

        ser.close()
    except serial.SerialException as error:
        print(f"Serial error: {error}", file=sys.stderr)
        return 1
    except TimeoutError as error:
        print(f"Timeout: {error}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
