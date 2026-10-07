#!/usr/bin/env python3
"""Remove personal data from game logs and run artifacts before they go into the repo.

    redact.py [--check] FILE_OR_DIR ...

Learns the account identifiers from the AoE IV log lines themselves (Steam name, SteamID64, Relic
profile ID), then replaces every occurrence in every given text file. Also masks session tokens, the
local time zone in the log header, and macOS/Linux home-directory paths.

--check   report what would change and exit 1 if anything is found (use before committing)
"""
import os
import re
import sys

LEARN = [
    (r"Current Steam name is \[([^\]]+)\]", "<steam-name>"),
    (r"/steam/(7656119\d{10})", "<steamid64>"),
    (r"(7656119\d{10})", "<steamid64>"),
    (r"for user ID (\d+)", "<profile-id>"),
    (r'"PresenceMessage",(\d+)', "<profile-id>"),
    (r"Human Player: \d+ (\S+) (\d+)", None),  # name and profile id
]
FIXED = [
    (re.compile(r"sessionToken=[A-Za-z0-9]+"), "sessionToken=<redacted>"),
    (re.compile(r"\[[A-Za-z .]+ (?:Standard|Daylight) Time UTC [+-]\d\d:\d\d\]"), "[<timezone>]"),
    (re.compile(r"/Users/[^/\s\"']+"), "<home>"),
    (re.compile(r"/home/(?!xuser\b)[^/\s\"']+"), "<home>"),
    (re.compile(r"\b7656119\d{10}\b"), "<steamid64>"),
]


def text_files(paths):
    for p in paths:
        if os.path.isdir(p):
            for root, _, names in os.walk(p):
                for n in names:
                    yield os.path.join(root, n)
        else:
            yield p


def is_text(path):
    try:
        with open(path, "rb") as f:
            return b"\0" not in f.read(8192)
    except OSError:
        return False


def learn(files):
    values = {}
    for path in files:
        s = open(path, errors="replace").read()
        for pattern, label in LEARN:
            for m in re.finditer(pattern, s):
                if label is None:
                    values[m.group(1)] = "<steam-name>"
                    values[m.group(2)] = "<profile-id>"
                else:
                    values[m.group(1)] = label
    # never treat short or generic tokens as identifiers
    return {v: l for v, l in values.items()
            if len(v) >= 4 and not v.startswith("<") and v.lower() not in ("xuser", "user", "player")}


def main():
    args = sys.argv[1:]
    check = "--check" in args
    args = [a for a in args if a != "--check"]
    if not args:
        sys.exit(__doc__)
    me = os.path.realpath(__file__)
    files = [f for f in text_files(args) if is_text(f) and os.path.realpath(f) != me]
    values = learn(files)
    if values:
        print("identifiers learned:", ", ".join(f"{l}" for l in sorted(set(values.values()))))
    changed = 0
    for path in files:
        s = open(path, errors="replace").read()
        new = s
        for value, label in sorted(values.items(), key=lambda kv: -len(kv[0])):
            new = re.sub(r"(?<![A-Za-z0-9])" + re.escape(value) + r"(?![A-Za-z0-9])", label, new)
        for pattern, repl in FIXED:
            new = pattern.sub(repl, new)
        if new != s:
            changed += 1
            print(("would redact " if check else "redacted ") + path)
            if not check:
                open(path, "w").write(new)
    if check and changed:
        sys.exit(1)
    print(f"{changed} file(s) {'need redaction' if check else 'changed'}")


if __name__ == "__main__":
    main()
