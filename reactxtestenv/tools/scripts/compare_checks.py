"""
PROJECT:     ReactX tests
LICENSE:     MIT (https://spdx.org/licenses/MIT)
PURPOSE:     Run a check tool on System32 and ReactX and diff what each reports
COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>

compare.py <bin dir> [--tool apicheck|dxgicheck] [check ...]
"""

import subprocess
import sys


def run(exe, runtime, checks):
    result = subprocess.run([exe, "-runtime", runtime] + checks, capture_output=True, text=True, timeout=60)
    return result.stdout.splitlines()


def main():
    tool = "apicheck"
    checks = sys.argv[2:]
    if checks[:1] == ["--tool"]:
        tool = checks[1]
        checks = checks[2:]
    exe = sys.argv[1].rstrip("\\/") + "\\" + tool + ".exe"
    system = run(exe, "system", checks)
    reactx = run(exe, "reactx", checks)

    failures = [line for line in system + reactx if line.startswith("FAIL")]
    facts = {}
    for name, lines in (("system", system), ("reactx", reactx)):
        for line in lines:
            if " = " in line and not line.startswith("failures"):
                key, value = line.split(" = ", 1)
                facts.setdefault(key, {})[name] = value

    differences = 0
    for key, values in facts.items():
        if values.get("system") != values.get("reactx"):
            differences += 1
            print("DIFF %-36s system: %s" % (key, values.get("system")))
            print("     %-36s reactx: %s" % ("", values.get("reactx")))

    for line in failures:
        print(line)

    print("%d facts, %d differ, %d expectation failures" % (len(facts), differences, len(failures)))
    return 1 if differences or failures else 0


if __name__ == "__main__":
    sys.exit(main())
