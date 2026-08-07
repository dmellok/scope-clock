"""Point the OTA uploader at the bridge, using credentials from .env.

Separate from load_env.py because of a phase constraint, learned the hard way:

  * -D defines must be appended in a PRE script. Appended in a post script they
    are silently ignored — the build still succeeds and the firmware quietly
    falls back to its placeholder credentials.
  * UPLOADERFLAGS does not exist until the platform's own builder has run, so a
    PRE script's changes to it are replaced.

Hence: load_env.py runs pre for the defines, this runs post for the upload.
Only applies to the espota env; the USB env is left untouched.
"""
Import("env")  # noqa: F821

import os

if env.subst("$UPLOAD_PROTOCOL") == "espota":  # noqa: F821
    path = os.path.join(env["PROJECT_DIR"], ".env")  # noqa: F821
    cfg = {}
    if os.path.isfile(path):
        with open(path, "r", encoding="utf-8") as handle:
            for line in handle:
                line = line.strip()
                if not line or line.startswith("#") or "=" not in line:
                    continue
                key, _, value = line.partition("=")
                cfg[key.strip()] = value.strip()

    host = cfg.get("OTA_HOST")
    pw = cfg.get("OTA_PASS")
    if host:
        env.Replace(UPLOAD_PORT="%s.local" % host)  # noqa: F821
        print("ota_target: flashing %s.local over the network" % host)
    else:
        print("ota_target: no OTA_HOST in .env — set upload_port by hand")
    if pw and pw != "CHANGE_ME":
        env.Append(UPLOADERFLAGS=["--auth=%s" % pw])  # noqa: F821
        print("ota_target: authenticated")
    else:
        print("ota_target: WARNING no OTA_PASS — anyone on this LAN can flash it")
