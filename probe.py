#!/usr/bin/env python3
"""Inspect or launch a local DiRT 3 Complete Edition Mac bundle."""

import argparse
import hashlib
import os
import plistlib
import subprocess
import sys
from contextlib import nullcontext
from pathlib import Path

DEFAULT_BUNDLE = Path("/Applications/DiRT 3 Complete Edition.app")
REFERENCE_SHA256 = "0415a9ecbaccdc25752a327c7b8a0dbd62435a308d0a95652d4a286e6a579014"


def fingerprint(path):
    digest = hashlib.sha256()
    with path.open("rb") as binary:
        for block in iter(lambda: binary.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def read_bundle(path):
    bundle = path.expanduser().resolve(strict=True)
    with (bundle / "Contents" / "Info.plist").open("rb") as plist:
        info = plistlib.load(plist)
    if info.get("CFBundleIdentifier") != "com.feralinteractive.dirt3":
        raise ValueError("expected the com.feralinteractive.dirt3 app bundle")
    name = info.get("CFBundleExecutable", "")
    if not isinstance(name, str) or not name or Path(name).name != name:
        raise ValueError("invalid CFBundleExecutable")
    executable = bundle / "Contents" / "MacOS" / name
    archs = subprocess.check_output(["/usr/bin/lipo", "-archs", executable], text=True)
    return bundle, info, executable, archs.split()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("inspect", "check", "run"))
    parser.add_argument("--app", type=Path, default=DEFAULT_BUNDLE)
    parser.add_argument("--engine", type=Path, help="path to the compiled mac32 executable")
    parser.add_argument("--log", type=Path, help="write runtime output to a new file")
    parser.add_argument("--timeout", type=int, help="terminate the engine after this many seconds")
    parser.add_argument("--benchmark", action="store_true", help="run the game's bundled Aspen benchmark")
    args = parser.parse_args()
    if args.mode == "inspect" and (args.engine or args.log or args.timeout is not None):
        parser.error("--engine, --log and --timeout apply to check and run")
    if args.mode != "inspect" and args.engine is None:
        parser.error("check and run require --engine")
    if args.timeout is not None and args.timeout <= 0:
        parser.error("--timeout must be positive")
    if args.benchmark and args.mode != "run":
        parser.error("--benchmark applies to run")

    try:
        bundle, info, executable, archs = read_bundle(args.app)
        sha256 = fingerprint(executable)
        print(f"Bundle: {bundle}")
        print(f"Version: {info.get('CFBundleShortVersionString')} ({info.get('CFBundleVersion')})")
        print(f"Architectures: {', '.join(archs)}")
        print(f"SHA-256: {sha256}", flush=True)
        if args.mode == "inspect":
            return subprocess.run(
                ["/usr/bin/otool", "-arch", "i386", "-L", executable], check=False
            ).returncode
        if "i386" not in archs:
            raise ValueError("mac32 requires an i386 slice")
        if sha256 != REFERENCE_SHA256:
            print("This executable differs from the recorded test build.", file=sys.stderr, flush=True)

        engine = args.engine.expanduser().resolve(strict=True)
        command = [engine, "-v"]
        environment = None
        if args.mode == "check":
            command.extend(
                ["-L", bundle / "Contents" / "Frameworks", "-L", executable.parent, "--deps", executable]
            )
        else:
            command.append(bundle)
        if args.benchmark:
            if sha256 != REFERENCE_SHA256:
                raise ValueError("benchmark mode requires the supported Feral build")
            benchmark = "example_benchmark.xml"
            if not (bundle / "Contents" / "Resources" / "Data" / benchmark).is_file():
                raise ValueError(f"the game bundle is missing {benchmark}")
            command.extend(["-benchmark", benchmark])
            environment = {
                **os.environ,
                "MAC32_DIRT3_DIRECT": "1",
                "MAC32_DIRT3_SKIP_STARTUP_VIDEOS": "1",
            }

        log = args.log.expanduser().resolve() if args.log else None
        if log is not None and bundle in log.parents:
            raise ValueError("choose a log path outside the game bundle")
        with log.open("xb") if log else nullcontext() as output:
            result = subprocess.run(
                command,
                cwd=bundle.parent,
                env=environment,
                stdout=output,
                stderr=subprocess.STDOUT if output else None,
                timeout=args.timeout,
                check=False,
            )
        if result.returncode < 0:
            print(f"mac32 stopped on signal {-result.returncode}.", file=sys.stderr)
            return 128 - result.returncode
        return result.returncode
    except subprocess.TimeoutExpired:
        print(f"mac32 exceeded the {args.timeout}s timeout.", file=sys.stderr)
        return 124
    except (OSError, ValueError, plistlib.InvalidFileException, subprocess.CalledProcessError) as exc:
        print(f"probe: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
