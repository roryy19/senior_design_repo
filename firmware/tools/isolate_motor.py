#!/usr/bin/env python3
"""
isolate_motor.py — Extract time vs motor level for one motor from ESP32 logs.

Default: one CSV row per level-change event.
With --tick-ms N: forward-fill to emit one row every N ms, holding the
last-known level between changes (continuous stream suitable for plots).

Usage:
    # One row per change event
    python isolate_motor.py <motor 0-7> <logfile> -o motor2.csv

    # Forward-fill every 50 ms for continuous output
    python isolate_motor.py <motor 0-7> <logfile> -o motor2.csv --tick-ms 50

    # Live stream: idf.py monitor | this script > motor2.csv
    python isolate_motor.py <motor 0-7>
"""

import argparse
import re
import sys

RE_MOTOR = re.compile(
    r"SENSOR:\s+Motor\s+(\d+):\s+level\s+(\d+)/7\s+\(was\s+(\d+)\)"
)
RE_TIME = re.compile(r"^I\s+\((\d+)\)")


def collect_events(motor_idx: int, fh_in):
    """Pull (time_ms, level) tuples for the target motor from the log."""
    events = []
    last_time = 0
    for raw in fh_in:
        line = raw.rstrip("\n")
        m_t = RE_TIME.search(line)
        if m_t:
            last_time = int(m_t.group(1))
        m_m = RE_MOTOR.search(line)
        if m_m and int(m_m.group(1)) == motor_idx:
            events.append((last_time, int(m_m.group(2))))
    return events


def write_events(events, fh_out):
    fh_out.write("time_ms,level\n")
    for t, lvl in events:
        fh_out.write(f"{t},{lvl}\n")


def write_filled(events, tick_ms, fh_out):
    """Forward-fill to uniform tick intervals."""
    fh_out.write("time_ms,level\n")
    if not events:
        return 0

    start_t = events[0][0]
    end_t   = events[-1][0]
    level   = events[0][1]
    ev_idx  = 0

    rows = 0
    t = start_t
    while t <= end_t:
        while ev_idx < len(events) and events[ev_idx][0] <= t:
            level = events[ev_idx][1]
            ev_idx += 1
        fh_out.write(f"{t},{level}\n")
        rows += 1
        t += tick_ms
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("motor", type=int, help="motor index 0..7")
    ap.add_argument("logfile", nargs="?",
                    help="path to log file (omit to read stdin)")
    ap.add_argument("-o", "--output",
                    help="write CSV to this file (default: stdout)")
    ap.add_argument("--tick-ms", type=int, default=0,
                    help="forward-fill to a row every N ms "
                         "(0 = only emit change events, default)")
    args = ap.parse_args()

    if not (0 <= args.motor <= 7):
        print("motor must be 0..7", file=sys.stderr)
        sys.exit(1)
    if args.tick_ms < 0:
        print("--tick-ms must be >= 0", file=sys.stderr)
        sys.exit(1)

    fh_in = open(args.logfile, encoding="utf-8", errors="replace") \
            if args.logfile else sys.stdin
    fh_out = open(args.output, "w", encoding="utf-8") \
             if args.output else sys.stdout

    try:
        events = collect_events(args.motor, fh_in)
        if args.tick_ms > 0:
            rows = write_filled(events, args.tick_ms, fh_out)
            print(f"[isolate_motor] motor {args.motor}: {len(events)} events, "
                  f"{rows} rows written ({args.tick_ms}ms ticks)",
                  file=sys.stderr)
        else:
            write_events(events, fh_out)
            print(f"[isolate_motor] motor {args.motor}: "
                  f"{len(events)} rows written",
                  file=sys.stderr)
    finally:
        if args.logfile:
            fh_in.close()
        if args.output:
            fh_out.close()


if __name__ == "__main__":
    main()
