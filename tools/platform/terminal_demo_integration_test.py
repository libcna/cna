#!/usr/bin/env python3
"""Run the real 2D demo under a pseudo-terminal and verify its platform contract.

This is deliberately an integration test rather than another presenter unit test: the executable
creates Game/GraphicsDevice/Blend2D/TerminalPlatform, presents real frames, handles a real input
byte, reacts to SIGWINCH, and must restore the caller's terminal before it exits.
"""

import argparse
import errno
import fcntl
import os
import pty
import select
import signal
import struct
import sys
import time
import termios


def set_size(fd: int, columns: int, rows: int) -> None:
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, columns, 0, 0))


def drain(fd: int, output: bytearray, timeout: float) -> None:
    ready, _, _ = select.select([fd], [], [], timeout)
    if not ready:
        return
    try:
        data = os.read(fd, 65536)
    except OSError as error:
        if error.errno == errno.EIO:  # Linux pty master after the child closes its slave.
            return
        raise
    if data:
        output.extend(data)


def wait_for(fd: int, output: bytearray, needle: bytes, deadline: float) -> bool:
    while time.monotonic() < deadline:
        if needle in output:
            return True
        drain(fd, output, 0.05)
    return needle in output


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("demo")
    arguments = parser.parse_args()
    demo = os.path.abspath(arguments.demo)

    master, slave = pty.openpty()
    set_size(slave, 120, 40)
    pid = os.fork()
    if pid == 0:
        os.setsid()
        fcntl.ioctl(slave, termios.TIOCSCTTY, 0)
        for descriptor in (0, 1, 2):
            os.dup2(slave, descriptor)
        os.close(master)
        os.close(slave)
        # The demo's Content directory is staged next to the executable.
        os.chdir(os.path.dirname(demo))
        os.execv(demo, [demo, "--smoke", "75"])

    os.close(slave)
    output = bytearray()
    try:
        startup_deadline = time.monotonic() + 12.0
        if not wait_for(master, output, b"\x1b[?1049h", startup_deadline):
            raise AssertionError("demo never entered the terminal alternate screen")

        # The next present must observe the resized grid.  This checks the actual SIGWINCH path,
        # not merely the presenter's standalone size query.
        set_size(master, 100, 30)
        os.kill(pid, signal.SIGWINCH)
        time.sleep(0.15)

        # Feed a real keyboard byte while the game is polling its platform. The 2D sample is not
        # an input demo, so its bounded smoke mode remains the deterministic exit mechanism;
        # terminal keyboard semantics themselves are covered by TerminalKeyboardTests.
        os.write(master, b"w")

        exit_deadline = time.monotonic() + 8.0
        status = None
        while time.monotonic() < exit_deadline:
            drain(master, output, 0.05)
            waited, status = os.waitpid(pid, os.WNOHANG)
            if waited == pid:
                break
        else:
            os.kill(pid, signal.SIGTERM)
            os.waitpid(pid, 0)
            raise AssertionError("bounded demo did not stop before the timeout")

        if status is None or not os.WIFEXITED(status) or os.WEXITSTATUS(status) != 0:
            raise AssertionError(f"demo exited unsuccessfully: status={status}")

        while True:
            before = len(output)
            drain(master, output, 0.05)
            if len(output) == before:
                break
        transcript = bytes(output)
        for expected in (b"\x1b[?1049h", b"\x1b[?1049l",
                         b"CNA terminal diagnostics:", b"grid=100x30",
                         b"dropped_frames=", b"kitty_keyboard="):
            if expected not in transcript:
                raise AssertionError(f"missing terminal integration evidence: {expected!r}")
    finally:
        os.close(master)

    return 0


if __name__ == "__main__":
    sys.exit(main())
