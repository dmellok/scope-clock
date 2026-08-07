"""Turn bridge-esp32/.env into compile-time -D flags.

PlatformIO has no native .env support, so this pre-build script reads the file
and appends each KEY=value as a preprocessor define. Values are stringified by
SCons rather than pasted into a command line, which is what keeps characters
like ! $ ` and spaces in a Wi-Fi password from being mangled by the shell.

.env is gitignored; .env.example is the tracked template. A missing .env is not
an error — the firmware falls back to placeholder credentials so that a fresh
clone still compiles.
"""
Import("env")  # noqa: F821  (injected by PlatformIO/SCons)

import os

ENV_PATH = os.path.join(env["PROJECT_DIR"], ".env")  # noqa: F821

# Only these are accepted, so a stray line in .env cannot inject arbitrary
# defines into the build.
ALLOWED = ("WIFI_SSID", "WIFI_PASS", "TZ_POSIX", "NTP1")
SECRET = ("WIFI_PASS",)


def main():
    if not os.path.isfile(ENV_PATH):
        print("load_env: no .env found; building with placeholder credentials.")
        print("load_env: cp .env.example .env and fill it in.")
        return

    defines, skipped = [], []
    with open(ENV_PATH, "r", encoding="utf-8") as handle:
        for raw in handle:
            line = raw.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, _, value = line.partition("=")
            key, value = key.strip(), value.strip()
            # Take the value literally to end of line. Stripping matched quotes
            # is a convenience; anything else, including '#', stays put, so a
            # password may contain any of it.
            if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
                value = value[1:-1]
            if key not in ALLOWED:
                skipped.append(key)
                continue
            defines.append((key, env.StringifyMacro(value)))  # noqa: F821

    if defines:
        env.Append(CPPDEFINES=defines)  # noqa: F821
        shown = ", ".join(
            k if k not in SECRET else "%s=<hidden>" % k for k, _ in defines
        )
        print("load_env: %d define(s) from .env: %s" % (len(defines), shown))
    if skipped:
        print("load_env: ignored unrecognised key(s): %s" % ", ".join(skipped))


main()
