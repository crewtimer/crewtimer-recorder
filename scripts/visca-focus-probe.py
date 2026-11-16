#!/usr/bin/env python3
"""Probe how a VISCA camera responds to direct focus positions.

Talks raw VISCA over TCP the same way the recorder does (no VISCA-over-IP
header; AIDA listens on 52381). Quit the recorder or disconnect its camera
control first: the camera may accept only one control connection.

Every command prints the camera's raw replies with timings, so a refused
request ("completion without moving", or an error 90 6y ..) is visible.

Examples:
  visca-focus-probe.py 192.168.1.50 status
  visca-focus-probe.py 192.168.1.50 --zoom 11618 goto 13269
  visca-focus-probe.py 192.168.1.50 steps          # are small moves refused?
  visca-focus-probe.py 192.168.1.50 bisect 6000    # how far toward 6000 can it go?
  visca-focus-probe.py 192.168.1.50 --zoom 11618 --compound goto 14500 6000
  visca-focus-probe.py 192.168.1.50 --zoom 11618 goto --iterate 13269 20000 0
"""

import argparse
import socket
import sys
import time

ERRORS = {
    0x01: "message length error",
    0x02: "syntax error",
    0x03: "command buffer full",
    0x04: "command cancelled",
    0x05: "no socket",
    0x41: "command not executable",
}


def hexs(data: bytes) -> str:
    return " ".join(f"{b:02x}" for b in data)


def nibbles(value: int) -> bytes:
    return bytes([(value >> 12) & 0xF, (value >> 8) & 0xF, (value >> 4) & 0xF, value & 0xF])


class Visca:
    def __init__(self, host: str, port: int, verbose: bool):
        self.sock = socket.create_connection((host, port), timeout=5)
        self.buf = b""
        self.verbose = verbose
        # With --compound, the zoom sent alongside every focus position.
        self.compound_zoom: int | None = None
        # With --iterate, (inf, near edge) found for each zoom reading.
        self.edges: dict[int, tuple[int, int]] = {}

    def _read_message(self, deadline: float) -> bytes | None:
        while b"\xff" not in self.buf:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return None
            self.sock.settimeout(remaining)
            try:
                chunk = self.sock.recv(256)
            except socket.timeout:
                return None
            if not chunk:
                raise ConnectionError("camera closed the connection")
            self.buf += chunk
        msg, _, self.buf = self.buf.partition(b"\xff")
        return msg + b"\xff"

    def send(self, packet: bytes, label: str, quiet: bool = False) -> list[tuple[float, bytes]]:
        """Sends a packet and returns (ms, reply) pairs up to the final reply (5y or 6y)."""
        self.buf = b""
        start = time.monotonic()
        self.sock.sendall(packet)
        replies = []
        deadline = start + 5
        while True:
            msg = self._read_message(deadline)
            if msg is None:
                replies.append(((time.monotonic() - start) * 1000, b""))
                break
            replies.append(((time.monotonic() - start) * 1000, msg))
            if len(msg) >= 2 and 0x50 <= msg[1] <= 0x7F:
                break
        if self.verbose or not quiet:
            for ms, msg in replies:
                print(f"    {label:<14} {ms:7.1f} ms  {describe(msg)}")
        return replies

    def position(self, axis: str) -> int:
        code = 0x48 if axis == "focus" else 0x47
        replies = self.send(bytes([0x81, 0x09, 0x04, code, 0xFF]), f"{axis}?", quiet=True)
        msg = replies[-1][1]
        if len(msg) != 7 or msg[1] != 0x50:
            raise RuntimeError(f"no {axis} position: {describe(msg)}")
        return (msg[2] << 12) | (msg[3] << 8) | (msg[4] << 4) | msg[5]

    def autofocus(self) -> bool:
        msg = self.send(bytes([0x81, 0x09, 0x04, 0x38, 0xFF]), "af?", quiet=True)[-1][1]
        return len(msg) >= 3 and msg[2] == 2

    def manual_focus(self):
        self.send(bytes([0x81, 0x01, 0x04, 0x38, 0x03, 0xFF]), "af off", quiet=True)

    def settle(self, axis: str, limit_s: float = 8.0) -> tuple[int, float]:
        """Polls until the position is unchanged for 3 reads; returns (position, seconds)."""
        start = time.monotonic()
        last = self.position(axis)
        still = 0
        while still < 3 and time.monotonic() - start < limit_s:
            time.sleep(0.1)
            pos = self.position(axis)
            still = still + 1 if pos == last else 0
            last = pos
        return last, time.monotonic() - start

    def direct(self, axis: str, value: int) -> list[tuple[float, bytes]]:
        code = 0x48 if axis == "focus" else 0x47
        return self.send(bytes([0x81, 0x01, 0x04, code]) + nibbles(value) + b"\xff", f"{axis} {value}")

    def zoom_focus(self, zoom: int, focus: int) -> list[tuple[float, bytes]]:
        """ZoomFocus Direct: 8x 01 04 47 <zoom> <focus> FF."""
        return self.send(
            bytes([0x81, 0x01, 0x04, 0x47]) + nibbles(zoom) + nibbles(focus) + b"\xff",
            f"z{zoom} f{focus}",
        )

    def focus_to(self, value: int) -> list[tuple[float, bytes]]:
        """Sends a focus position: FocusDirect, or ZoomFocus Direct when a compound zoom is set."""
        if self.compound_zoom is None:
            return self.direct("focus", value)
        return self.zoom_focus(self.compound_zoom, value)


def describe(msg: bytes) -> str:
    if not msg:
        return "(no reply within 5 s)"
    text = hexs(msg)
    if len(msg) >= 2:
        kind = msg[1] & 0xF0
        if kind == 0x40:
            return f"{text}  ack"
        if kind == 0x50:
            return f"{text}  completion"
        if kind == 0x60 and len(msg) >= 3:
            return f"{text}  ERROR: {ERRORS.get(msg[2], 'unknown')}"
    return text


def goto(cam: Visca, target: int, tolerance: int) -> tuple[bool, int, int]:
    """Sends a direct focus position; returns (reached, start, final)."""
    start = cam.position("focus")
    replies = cam.focus_to(target)
    final, secs = cam.settle("focus")
    error = any(len(m) >= 2 and m[1] & 0xF0 == 0x60 for _, m in replies)
    reached = abs(final - target) <= tolerance
    if reached:
        verdict = "reached"
    elif final == start:
        verdict = "DID NOT MOVE" + (" (error reply)" if error else " (no error reply)")
    else:
        verdict = f"stopped short by {target - final:+d}"
    print(f"  focus {start} -> request {target} ({target - start:+d}) -> {final}  [{verdict}, settled {secs:.1f} s]")
    return reached, start, final


def cmd_status(cam: Visca, _args):
    print(f"  zoom {cam.position('zoom')}  focus {cam.position('focus')}  "
          f"autofocus {'on' if cam.autofocus() else 'off'}")


def near_edge_by_search(cam: Visca, inf: int, tolerance: int) -> int:
    """Binary-searches inf..inf+7000 for the highest focus FocusDirect accepts (~13 probes)."""
    low, high = inf, inf + 7000  # low is accepted; high is assumed refused
    probes = 0
    while high - low > 1:
        f = (low + high) // 2
        probes += 1
        reached, _, final = goto(cam, f, tolerance)
        if reached:
            low = f
        else:
            high = f
            if abs(final - inf) > tolerance:
                print(f"    note: refused {f} landed at {final}, not at inf {inf}")
    print(f"  near edge {low} after {probes} probes")
    return low


def near_edge_by_drive(cam: Visca) -> int:
    """Drives continuous Near until the lens stops, and reads where it ends. Unverified."""
    cam.send(bytes([0x81, 0x01, 0x04, 0x08, 0x37, 0xFF]), "near drive")
    try:
        near, secs = cam.settle("focus", limit_s=60)
    finally:
        cam.send(bytes([0x81, 0x01, 0x04, 0x08, 0x00, 0xFF]), "focus stop")
    print(f"  near edge {near} (drove to the end in {secs:.1f} s)")
    return near


def goto_iterate(cam: Visca, target: int, args):
    """Sends a focus position as requested; if refused, clamps it to the edges at this zoom."""
    # Finish the zoom first: poll until two readings match.
    zoom = cam.position("zoom")
    while (next_zoom := cam.position("zoom")) != zoom:
        zoom = next_zoom
        time.sleep(0.1)
    print(f"  zoom {zoom}: trying {target} as requested")
    reached, start, final = goto(cam, target, args.tolerance)
    if reached:
        return

    # A refused value sends the lens to inf, so where it landed is inf. If it didn't move,
    # it was already there.
    inf = final
    print(f"  inf (far edge) {inf}" + (" (lens did not move)" if final == start else ""))
    if target <= inf:
        print(f"  {target} is beyond inf; resting at inf {inf}")
        return

    # The near edge, cached per zoom; an inf that moved means the zoom drifted, so find it again.
    cached = cam.edges.get(zoom)
    if cached and abs(cached[0] - inf) <= args.tolerance:
        near = cached[1]
        print(f"  near edge {near} (cached)")
    else:
        if cached:
            print(f"  cached inf {cached[0]} no longer matches: finding the near edge again")
        if args.near_method == "drive":
            near = near_edge_by_drive(cam)
        else:
            near = near_edge_by_search(cam, inf, args.tolerance)
        cam.edges[zoom] = (inf, near)

    sent = min(target, near - 2)
    if sent == target:
        print(f"  WARNING: {target} is inside {inf}..{near - 2} but was refused; trying again")
    else:
        print(f"  sending {sent} (clamped from {target} to near edge - 2)")
    reached, _, final = goto(cam, sent, args.tolerance)
    if not reached:
        print(f"  WARNING: landed at {final}, not {sent}")


def cmd_goto(cam: Visca, args):
    cam.manual_focus()
    for target in args.targets:
        if args.iterate:
            goto_iterate(cam, target, args)
        else:
            goto(cam, target, args.tolerance)


def cmd_steps(cam: Visca, args):
    """Tries moves of growing size each way from the current position, returning in between."""
    cam.manual_focus()
    base = cam.position("focus")
    print(f"  base focus {base}")
    for size in args.sizes:
        for sign in (1, -1):
            target = base + sign * size
            if not 0 <= target <= 0xFFFF:
                continue
            goto(cam, target, args.tolerance)
            reached, _, _ = goto(cam, base, args.tolerance)
            if not reached:
                print("  could not return to base; stopping")
                return


def cmd_bisect(cam: Visca, args):
    """Finds how far toward a target the lens goes, halving the gap after each refusal."""
    cam.manual_focus()
    good = cam.position("focus")
    target = args.target
    print(f"  from {good} toward {target}")
    reached, _, final = goto(cam, target, args.tolerance)
    if reached:
        print("  target reachable directly")
        return
    if final != good:
        print(f"  lens moved but stopped at {final}: a mechanical end, not a refusal")
    bad = target
    good = cam.position("focus")
    while abs(bad - good) > args.resolution:
        mid = (good + bad) // 2
        reached, start, final = goto(cam, mid, args.tolerance)
        if reached:
            good = final
        else:
            bad = mid
            if final != start:  # moved part way: that much is reachable
                good = final
    print(f"\n  reachable up to {good}; refused at {bad} (gap {abs(bad - good)})")
    # A range limit refuses the same value from any start; a move-size rule won't.
    print("  re-trying the refused value from the reachable side:")
    goto(cam, bad, args.tolerance)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("host")
    p.add_argument("--port", type=int, default=52381)
    p.add_argument("--zoom", type=int, help="set this zoom position first and wait for it")
    p.add_argument("--tolerance", type=int, default=2, help="focus units counted as reaching a target")
    p.add_argument("-v", "--verbose", action="store_true", help="also print position queries")
    p.add_argument("--compound", action="store_true",
                   help="send focus as ZoomFocus Direct (81 01 04 47 <zoom> <focus> FF) with the "
                        "--zoom value, or the current zoom; it checks only the wide focus limits")
    sub = p.add_subparsers(dest="command", required=True)
    sub.add_parser("status", help="print zoom, focus and autofocus")
    g = sub.add_parser("goto", help="send one or more direct focus positions in turn")
    g.add_argument("targets", type=int, nargs="+")
    g.add_argument("--iterate", action="store_true",
                   help="send each target as requested; if refused, take where the lens landed "
                        "as inf, find the near edge (cached per zoom), and send it clamped to near-2")
    g.add_argument("--near-method", choices=["search", "drive"], default="search",
                   help="find the near edge by binary search (~13 probes, default) or by "
                        "driving continuous Near to the end (cheaper, unverified)")
    s = sub.add_parser("steps", help="test growing move sizes each way from the current focus")
    s.add_argument("--sizes", type=int, nargs="+", default=[1, 2, 5, 10, 20, 50, 100, 300, 1000])
    b = sub.add_parser("bisect", help="find the furthest reachable focus toward a target")
    b.add_argument("target", type=int)
    b.add_argument("--resolution", type=int, default=4, help="stop when the gap is this small")
    args = p.parse_args()
    if args.compound and getattr(args, "iterate", False):
        p.error("--iterate uses FocusDirect; ZoomFocus reinterprets focus at the target zoom")

    cam = Visca(args.host, args.port, args.verbose)
    if args.zoom is not None:
        cam.direct("zoom", args.zoom)
        zoom, secs = cam.settle("zoom")
        print(f"  zoom settled at {zoom} after {secs:.1f} s")
    if args.compound:
        # The zoom just commanded, not the reading, which can differ by a few units.
        cam.compound_zoom = args.zoom if args.zoom is not None else cam.position("zoom")
        print(f"  focus moves use ZoomFocus Direct with zoom {cam.compound_zoom}")
    {"status": cmd_status, "goto": cmd_goto, "steps": cmd_steps, "bisect": cmd_bisect}[args.command](cam, args)


if __name__ == "__main__":
    try:
        main()
    except (ConnectionError, OSError, RuntimeError) as e:
        sys.exit(f"error: {e}")
