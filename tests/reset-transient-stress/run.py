#!/usr/bin/env python3
#
# Copyright 2011-2026 Branimir Karadzic. All rights reserved.
# License: https://github.com/bkaradzic/bgfx/blob/master/LICENSE
#

import argparse
import pathlib
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--iterations", type=int, default=200)
    parser.add_argument("--exhaust-every", type=int, default=1)
    parser.add_argument("--renderer", default="auto")
    parser.add_argument("--shader-root", default="examples/runtime/shaders")
    parser.add_argument("--fault", default="none",
                        choices=["none", "reset-no-rebind", "double-destroy", "leak"])
    parser.add_argument("--no-build", action="store_true")
    args = parser.parse_args()

    test_dir = pathlib.Path(__file__).resolve().parent
    root = test_dir.parent.parent
    binary = root / ".build" / "reset-transient-stress"

    if not args.no_build:
        subprocess.run(["make", "-C", str(test_dir), "build"], check=True)

    trace = tempfile.NamedTemporaryFile(prefix="bgfx-reset-transient-", delete=False)
    trace.close()

    command = [
        str(binary),
        "--iterations", str(args.iterations),
        "--exhaust-every", str(args.exhaust_every),
        "--renderer", args.renderer,
        "--shader-root", args.shader_root,
        "--trace", trace.name,
        "--fault", args.fault,
    ]
    result = subprocess.run(command, cwd=root)

    if result.returncode == 0:
        pathlib.Path(trace.name).unlink(missing_ok=True)
        return 0

    print("\nFAILURE REPLAY (operations since last verified frame):", file=sys.stderr)
    lines = pathlib.Path(trace.name).read_text(errors="replace").splitlines()
    start = 0
    for index, line in enumerate(lines):
        if line.startswith("CHECKPOINT "):
            start = index
    for line in lines[start:]:
        print("  " + line, file=sys.stderr)
    print("\nreplay trace: " + trace.name, file=sys.stderr)
    return result.returncode if result.returncode > 0 else 1


if __name__ == "__main__":
    sys.exit(main())
