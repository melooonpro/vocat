"""VoCat serial monitor that tolerates the expected USB mode switch.

The ESP32-S3 internal USB PHY is shared by USB Serial/JTAG and USB-OTG.
Entering the Mic/Mouse pages therefore removes the COM port temporarily.
Unlike a regular serial monitor, this monitor treats that event as a pause,
waits for the same port to return, and reconnects without printing a traceback.
"""

from __future__ import annotations

import argparse
import codecs
import sys
import time

import serial

try:
    import msvcrt
except ImportError:  # pragma: no cover - this helper is currently Windows-only
    msvcrt = None


def open_port(port: str, baud: int) -> serial.Serial:
    connection = serial.Serial()
    connection.port = port
    connection.baudrate = baud
    connection.timeout = 0.10
    connection.write_timeout = 0.25
    # Do not reset the ESP32-S3 whenever its serial interface reappears.
    connection.dtr = False
    connection.rts = False
    connection.open()
    return connection


def read_keyboard(connection: serial.Serial | None) -> bool:
    if msvcrt is None or not msvcrt.kbhit():
        return True

    key = msvcrt.getwch()
    if key == "\x1d":  # Ctrl+]
        return False
    if key in ("\x00", "\xe0"):
        if msvcrt.kbhit():
            msvcrt.getwch()
        return True

    if connection is not None:
        try:
            connection.write(key.encode("utf-8"))
        except (OSError, serial.SerialException, serial.SerialTimeoutException):
            pass
    return True


def monitor(port: str, baud: int) -> int:
    connection: serial.Serial | None = None
    decoder = codecs.getincrementaldecoder("utf-8")(errors="replace")
    waiting_message_shown = False

    print(f"[VoCat monitor] Watching {port} at {baud} baud. Press Ctrl+] to exit.")
    try:
        while True:
            if connection is None:
                try:
                    connection = open_port(port, baud)
                    decoder.reset()
                    if waiting_message_shown:
                        print(f"\n[VoCat monitor] {port} restored; log resumed.")
                    waiting_message_shown = False
                except (OSError, serial.SerialException):
                    if not waiting_message_shown:
                        print(
                            f"[VoCat monitor] {port} is in USB Mic/Mouse mode; "
                            "waiting for the log interface..."
                        )
                        waiting_message_shown = True
                    if not read_keyboard(None):
                        return 0
                    time.sleep(0.25)
                    continue

            try:
                data = connection.read(connection.in_waiting or 1)
                if data:
                    sys.stdout.write(decoder.decode(data))
                    sys.stdout.flush()
                if not read_keyboard(connection):
                    return 0
            except (OSError, serial.SerialException):
                try:
                    connection.close()
                except (OSError, serial.SerialException):
                    pass
                connection = None
                if not waiting_message_shown:
                    print(
                        f"\n[VoCat monitor] USB log paused; waiting for {port} to return..."
                    )
                    waiting_message_shown = True
    except KeyboardInterrupt:
        return 0
    finally:
        if connection is not None:
            try:
                connection.close()
            except (OSError, serial.SerialException):
                pass


def main() -> int:
    parser = argparse.ArgumentParser(description="Reconnectable VoCat serial monitor")
    parser.add_argument("port")
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()
    return monitor(args.port, args.baud)


if __name__ == "__main__":
    raise SystemExit(main())
