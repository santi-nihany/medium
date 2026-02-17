#!/usr/bin/env python3
"""Simple UART-USB terminal for MCU communication."""

from __future__ import annotations

import argparse
import re
import sys
import threading

try:
    import serial
    from serial.tools import list_ports
except ImportError as exc:
    raise SystemExit(
        "pyserial is required. Install it with: pip install pyserial"
    ) from exc

FTDI_VID = 0x0403
FT2232H_PID = 0x6010


def reader_thread(port: serial.Serial) -> None:
    """Read bytes from UART and print to stdout."""
    while port.is_open:
        try:
            data = port.read(port.in_waiting or 1)
        except serial.SerialException:
            break

        if not data:
            continue

        try:
            text = data.decode("utf-8", errors="replace")
            print(text, end="", flush=True)
        except Exception:
            print(repr(data), flush=True)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Communicate with an MCU over UART-USB"
    )
    parser.add_argument(
        "port",
        nargs="?",
        help=(
            "Serial port, e.g. /dev/ttyUSB0, /dev/ttyACM0, or COM3. "
            "If omitted, auto-detect FT2232H."
        ),
    )
    parser.add_argument(
        "-b",
        "--baudrate",
        type=int,
        default=115200,
        help="UART baudrate (default: 115200)",
    )
    parser.add_argument(
        "-t",
        "--timeout",
        type=float,
        default=0.1,
        help="Read timeout in seconds (default: 0.1)",
    )
    parser.add_argument(
        "--list-ports",
        action="store_true",
        help="List available serial ports and exit",
    )
    return parser.parse_args()


def _device_sort_key(device: str) -> tuple[str, int]:
    match = re.match(r"^(.*?)(\d+)$", device)
    if not match:
        return (device, -1)
    return (match.group(1), int(match.group(2)))


def _is_interface_1(port_info: list_ports.ListPortInfo) -> bool:
    interface = (port_info.interface or "").lower()
    description = (port_info.description or "").lower()
    return "interface 1" in interface or "interface 1" in description


def list_available_ports() -> None:
    ports = list(list_ports.comports())
    if not ports:
        print("No serial ports found.")
        return

    for p in sorted(ports, key=lambda item: _device_sort_key(item.device)):
        vid = f"0x{p.vid:04x}" if p.vid is not None else "n/a"
        pid = f"0x{p.pid:04x}" if p.pid is not None else "n/a"
        print(
            f"{p.device}: VID={vid} PID={pid} "
            f"SN={p.serial_number or 'n/a'} "
            f"MFG={p.manufacturer or 'n/a'} "
            f"DESC={p.description or 'n/a'}"
        )


def autodetect_ft2232h_port() -> str:
    matches = [
        p for p in list_ports.comports()
        if p.vid == FTDI_VID and p.pid == FT2232H_PID
    ]

    if not matches:
        raise RuntimeError(
            "No FT2232H ports found (VID:PID 0403:6010). "
            "Use --port to set it manually."
        )

    if len(matches) == 1:
        return matches[0].device

    matches_sorted = sorted(matches, key=lambda item: _device_sort_key(item.device))
    interface_1 = [p for p in matches_sorted if _is_interface_1(p)]
    if len(interface_1) == 1:
        return interface_1[0].device

    # Common Linux case on FT2232H boards: ttyUSB1 is UART and ttyUSB0 is debug/JTAG.
    return matches_sorted[-1].device


def main() -> int:
    args = parse_args()
    if args.list_ports:
        list_available_ports()
        return 0

    if args.port:
        selected_port = args.port
    else:
        try:
            selected_port = autodetect_ft2232h_port()
        except RuntimeError as exc:
            print(str(exc), file=sys.stderr)
            return 1

    try:
        port = serial.Serial(
            port=selected_port,
            baudrate=args.baudrate,
            timeout=args.timeout,
            write_timeout=1,
        )
    except serial.SerialException as exc:
        print(f"Could not open {selected_port}: {exc}", file=sys.stderr)
        return 1

    print(
        f"Connected to {selected_port} at {args.baudrate} baud. "
        "Type and press Enter to send. Ctrl+C to exit."
    )

    t = threading.Thread(target=reader_thread, args=(port,), daemon=True)
    t.start()

    try:
        for line in sys.stdin:
            if not port.is_open:
                break
            payload = line.encode("utf-8")
            port.write(payload)
            port.flush()
    except KeyboardInterrupt:
        pass
    finally:
        if port.is_open:
            port.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
