"""Summarize the frame trace written by the WineMetalGL window experiment."""

import argparse
from collections import Counter
import math
import mmap
from pathlib import Path
import re
from statistics import mean
import struct
import xml.etree.ElementTree as ET

HEADER = struct.Struct("<QQ")
EVENT = struct.Struct("<QQQQIIII")
PRESENT = 16_384
NAME_COUNT = PRESENT + 8
NAME_LENGTH = 64
EVENTS_START = HEADER.size + NAME_COUNT * NAME_LENGTH


def benchmark_xml(path):
    # The engine can emit an unescaped user or machine name in the root tag.
    xml = re.sub(r"<benchmark\b[^>]*>", "<benchmark>", path.read_text(), count=1)
    return ET.fromstring(xml)


def frame_intervals(trace, samples, trim_ms):
    with trace.open("rb") as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
        version, count = HEADER.unpack_from(data)
        maximum = (len(data) - EVENTS_START) // EVENT.size
        if version != 1 or not 0 < count <= maximum:
            raise ValueError("Expected a complete version 1 trace")
        rows = data[EVENTS_START:EVENTS_START + count * EVENT.size]
        presents = [(timestamp, context) for timestamp, _, _, context, operation, *_ in
                    EVENT.iter_unpack(rows) if timestamp and operation == PRESENT]

    if not presents:
        raise ValueError("Trace contains no presents")
    context, _ = Counter(context for _, context in presents).most_common(1)[0]
    timestamps = sorted(timestamp for timestamp, key in presents if key == context)
    if len(timestamps) <= samples:
        raise ValueError("Fewer presents than the game benchmark reported")
    timestamps = timestamps[-samples - 1:]
    start = timestamps[0] + trim_ms * 1_000_000
    end = timestamps[-1] - trim_ms * 1_000_000
    intervals = [(after - before) / 1_000_000 for before, after in zip(timestamps, timestamps[1:])
                 if start < after < end]
    if not intervals:
        raise ValueError("No frame intervals in the selected race window")
    return sorted(intervals)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("benchmark", type=Path, help="XML for the same game run")
    parser.add_argument("--trim-seconds", type=float, default=8)
    parser.add_argument("--exclude-confirmed-pauses", type=float, metavar="SECONDS",
                        help="Remove intervals at least this long after independently confirming game pauses")
    args = parser.parse_args()
    pause_threshold = args.exclude_confirmed_pauses
    if pause_threshold is not None and (not math.isfinite(pause_threshold) or pause_threshold <= 0):
        parser.error("--exclude-confirmed-pauses must be a finite positive duration")

    benchmark = benchmark_xml(args.benchmark)
    samples = int(benchmark.find(".//*[@samples]").attrib["samples"])
    intervals = frame_intervals(args.trace, samples, args.trim_seconds * 1000)
    if pause_threshold is not None:
        cutoff_ms = pause_threshold * 1000
        paused = [interval for interval in intervals if interval >= cutoff_ms]
        print(f"Excluded {len(paused):,} confirmed pauses totaling {sum(paused) / 1000:.2f} seconds")
        intervals = [interval for interval in intervals if interval < cutoff_ms]
        if not intervals:
            raise ValueError("No frame intervals remain after excluding confirmed pauses")
    frame_count = len(intervals)
    slowest = math.ceil(frame_count * .01)
    print(f"Game average {float(benchmark.find('average').attrib['av_fps']):.2f} FPS")
    print(f"{frame_count:,} presents in the trimmed window, {1000 / mean(intervals):.2f} FPS")
    print(f"1% low {1000 / mean(intervals[-slowest:]):.2f} FPS")
    for percentile in (95, 99):
        interval = intervals[math.ceil(frame_count * percentile / 100) - 1]
        print(f"p{percentile} {interval:.2f} ms")
    for rate in (60, 30):
        limit = 1000 / rate
        count = sum(interval > limit for interval in intervals)
        print(f">{limit:.2f} ms: {count:,} ({count / frame_count:.1%})")
    print(f"Longest interval {intervals[-1]:.2f} ms")


if __name__ == "__main__":
    main()
