#!/usr/bin/env python3
"""
openbmc-sensors.py - bridge host sensor values to the OpenBMC BMC.

The matching BMC policy is the OpenBMC `meta-node804` layer. It creates
writable dbus-sensors ExternalSensors and PID zones; this script is the only
producer for the current sensors. Adding another sensor means adding its
ExternalSensor and zone on the BMC and another collector/push in this script.

BMC contract (see meta-node804/README.md in the OpenBMC checkout):

  NVMe_Temp  /xyz/openbmc_project/sensors/temperature/NVMe_Temp
             IPMI sensor 0xE0, Redfish MemberId temperature_NVMe_Temp
             zone 3 -> SYS_FAN1 (NVMe blower)
  HDD_Temp   /xyz/openbmc_project/sensors/temperature/HDD_Temp
             IPMI sensor 0xE1, Redfish MemberId temperature_HDD_Temp
             zone 4 -> SYS_FAN2 (drive exhaust), SYS_FAN4 (drive intake)

Both sensors have a 90 s timeout. Push every 30 s even when the value is
unchanged: each write pets the watchdog. A missing push lets that zone go to
100 percent; it does not affect the other zone. IPMI transports preflight Get
Sensor Reading because Set Sensor Reading succeeds for an unknown number.

Aggregation is MAX, never sum:

  NVMe_Temp = max(temperature_sensors[] on every non-excluded NVMe disk)
  HDD_Temp  = max(temperature.current on every configured SAS/SATA HDD)

A drive in standby is skipped without being spun up: HDD smartctl calls use
`-n standby`, and an all-standby group sends `standby_temp`. A read error is not
papered over: that group is simply not pushed and its zone fails safe.

Usage:
  sudo ./install-openbmc-sensors.sh \
      --install-dir /usr/local/libexec/openbmc-sensors \
      --config /etc/openbmc-sensors/config.json \
      --password-file /etc/openbmc-sensors/password
  /usr/local/libexec/openbmc-sensors/openbmc-sensors.py \
      --config /etc/openbmc-sensors/config.json --once

  # Debug without touching the BMC:
  openbmc-sensors.py --config ... --dry-run --once

Config keys (all optional unless marked):
  transport: "redfish" (default), "ipmi-open" or "ipmi-lanplus"
  password_file: common BMC password file for Redfish and IPMI LAN
  redfish_password_file, ipmi_password_file: per-transport overrides
  redfish_url, redfish_user, chassis, verify_tls
  ipmi_host, ipmi_user                            # lanplus only
  nvme: list of NVMe block devices               # default /dev/nvme[0-9]*
  nvme_exclude: stable paths to drop from nvme    # default the onboard M.2 boot disk
  hdd: list of HDD devices                       # empty = every rotational non-NVMe disk
  hdd_type: smartctl -d value (e.g. "scsi")      # default auto
  sysfs_root: sysfs mount                        # default /sys; containers/tests only
  interval: seconds between pushes               # default 30, keep below 90
  standby_temp: C sent when all HDDs are asleep   # default 30
"""

from __future__ import annotations

import argparse
import base64
import glob
import json
import logging
import math
import os
import re
import ssl
import subprocess
import sys
import time
import urllib.error
import urllib.request

DEFAULT_CONFIG_PATH = "/etc/openbmc-sensors/config.json"
DEFAULT_CONFIG = {
    "transport": "redfish",
    "password_file": "",
    "redfish_url": "",
    "redfish_user": "",
    "redfish_password_file": "",
    "chassis": "chassis",
    "verify_tls": True,
    "ipmi_host": "",
    "ipmi_user": "",
    "ipmi_password_file": "",
    "nvme": [],
    "nvme_exclude": ["/dev/disk/by-path/pci-0000:06:00.0-nvme-1"],
    "hdd": [],
    "hdd_type": "auto",
    "sysfs_root": "/sys",
    "interval": 30,
    "standby_temp": 30,
}
NVME_SENSOR = 0xE0
HDD_SENSOR = 0xE1

LOG = logging.getLogger("openbmc-sensors")


def read_password(path: str) -> str:
    if not path:
        raise ValueError("password_file is empty")
    mode = os.stat(path).st_mode & 0o777
    if mode & 0o077:
        LOG.warning("password file %s is readable by group/other (mode %03o)", path, mode)
    with open(path, "r", encoding="utf-8") as f:
        return f.readline().rstrip("\n")


def nvme_base(device: str) -> str | None:
    """Return /dev/nvmeN for a whole disk, partition, or by-path symlink."""
    real = os.path.realpath(device)
    match = re.match(r"/dev/(nvme\d+)", real)
    return f"/dev/{match.group(1)}" if match else None


def discover_hdds(sysfs_root: str) -> list[str]:
    """Return /dev names for every rotational, non-NVMe block device.

    The kernel sets queue/rotational to 1 for spinning disks and 0 for SSDs,
    NVMe namespaces, loops and device-mapper targets. This is the same signal
    lsblk reports; using sysfs avoids parsing lsblk output.
    """
    devices: list[str] = []
    skip_prefixes = ("nvme", "loop", "dm-", "md", "sr", "zram", "ram", "fd")
    for block in sorted(glob.glob(os.path.join(sysfs_root, "block", "*"))):
        name = os.path.basename(block)
        if name.startswith(skip_prefixes):
            continue
        try:
            with open(os.path.join(block, "queue", "rotational"), "r", encoding="utf-8") as f:
                rotational = int(f.read().strip())
        except (OSError, ValueError):
            continue
        if rotational == 1:
            devices.append(f"/dev/{name}")
    return devices


def load_config(path: str) -> dict:
    with open(path, "r", encoding="utf-8") as f:
        user = json.load(f)
    unknown = sorted(set(user) - set(DEFAULT_CONFIG))
    if unknown:
        LOG.warning("ignoring unknown config keys: %s", ", ".join(unknown))
    cfg = dict(DEFAULT_CONFIG)
    cfg.update({k: v for k, v in user.items() if k in DEFAULT_CONFIG})
    cfg["config_path"] = path
    common_password = cfg["password_file"]
    if common_password:
        if not cfg["redfish_password_file"]:
            cfg["redfish_password_file"] = common_password
        if not cfg["ipmi_password_file"]:
            cfg["ipmi_password_file"] = common_password
    for key in ("nvme", "nvme_exclude", "hdd"):
        if not isinstance(cfg[key], list):
            raise ValueError(f"config.{key} must be a list")
    if not isinstance(cfg["sysfs_root"], str):
        raise ValueError("config.sysfs_root must be a string")
    candidates = cfg["nvme"] or sorted(
        d for d in glob.glob("/dev/nvme[0-9]*") if re.fullmatch(r"/dev/nvme\d+", d)
    )
    excluded: set[str] = set()
    for path_to_drop in cfg["nvme_exclude"]:
        base = nvme_base(path_to_drop)
        if base:
            excluded.add(base)
        else:
            LOG.warning("cannot resolve nvme_exclude path %s; it will not be excluded", path_to_drop)
    resolved: list[str] = []
    for device in candidates:
        base = nvme_base(device)
        if base is None:
            LOG.warning("skipping NVMe device %s: not a /dev/nvmeN path", device)
            continue
        if base in excluded:
            LOG.info("excluding NVMe %s (%s) from NVMe_Temp", device, base)
            continue
        if base not in resolved:
            resolved.append(base)
    if not resolved:
        raise ValueError("no NVMe devices left after nvme/nvme_exclude filtering")
    cfg["nvme"] = resolved
    if not cfg["hdd"]:
        cfg["hdd"] = discover_hdds(cfg["sysfs_root"])
        if not cfg["hdd"]:
            raise ValueError(f"no rotational HDDs found under {cfg['sysfs_root']}/block")
        LOG.info("discovered HDDs: %s", cfg["hdd"])
    if not 5 <= int(cfg["interval"]) < 90:
        raise ValueError("interval must be at least 5 s and below the 90 s BMC timeout")
    return cfg


def smartctl_json(device: str, device_type: str, no_wake: bool = False) -> dict:
    cmd = ["smartctl", "-j"]
    if no_wake:
        cmd += ["-n", "standby"]
    cmd.append("-a")
    if device_type and device_type != "auto":
        cmd += ["-d", device_type]
    cmd.append(device)
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=20)
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise RuntimeError(f"smartctl {device}: {exc}") from exc
    text = proc.stdout.strip()
    data: dict | None = None
    if text:
        try:
            data = json.loads(text)
        except json.JSONDecodeError as exc:
            if proc.returncode != 2:
                raise RuntimeError(f"smartctl {device}: invalid JSON: {exc}") from exc
    combined = proc.stdout + proc.stderr
    if data is None:
        if proc.returncode == 2 and re.search(r"Device is in .*mode", combined):
            return {"power_mode": "STANDBY"}
        raise RuntimeError(f"smartctl {device}: no JSON output (exit {proc.returncode})")
    if proc.returncode == 2 and "power_mode" not in data:
        # SCSI reports the low-power mode as text, not JSON; keep the caller's
        # standby detection working without treating an open failure as cool.
        if re.search(r"Device is in .*mode", combined):
            data["power_mode"] = "STANDBY"
    return data


def is_standby(data: dict) -> bool:
    mode = str(data.get("power_mode", ""))
    return "STANDBY" in mode.upper()


def nvme_sensors(data: dict) -> list[float]:
    log = data.get("nvme_smart_health_information_log") or {}
    values = []
    for value in log.get("temperature_sensors") or []:
        if isinstance(value, (int, float)) and value > 0:
            values.append(float(value))
    composite = log.get("temperature")
    if not values and isinstance(composite, (int, float)) and composite > 0:
        values.append(float(composite))
    return values


def hdd_sensor(data: dict) -> float | None:
    current = (data.get("temperature") or {}).get("current")
    if isinstance(current, (int, float)) and current > 0:
        return float(current)
    # SATA fallback: SMART attribute 194, current temperature in the low byte
    # of the raw value (or the leading number in raw.string).
    for attr in (data.get("ata_smart_attributes") or {}).get("table") or []:
        if attr.get("id") == 194:
            raw = attr.get("raw") or {}
            text = str(raw.get("string") or "")
            match = re.match(r"\s*(\d+)", text)
            if match:
                return float(match.group(1))
            value = raw.get("value")
            if isinstance(value, (int, float)) and value > 0:
                return float(int(value) & 0xFF)
    return None


def clamp_temp(value: float) -> int:
    """Round up and clamp to the BMC's 0..100 whole-degree range."""
    return max(0, min(100, math.ceil(float(value))))


def collect(cfg: dict) -> tuple[int | None, int | None, list[str]]:
    notes: list[str] = []
    nvme_values: list[float] = []
    hdd_values: list[float] = []
    hdd_standby = 0

    for device in cfg["nvme"]:
        try:
            data = smartctl_json(device, "auto")
        except RuntimeError as exc:
            notes.append(str(exc))
            continue
        values = nvme_sensors(data)
        if not values:
            notes.append(f"{device}: no NVMe temperature sensor")
            continue
        LOG.debug("%s NVMe sensors: %s", device, values)
        nvme_values.extend(values)

    for device in cfg["hdd"]:
        try:
            data = smartctl_json(device, cfg["hdd_type"], no_wake=True)
        except RuntimeError as exc:
            notes.append(str(exc))
            continue
        value = hdd_sensor(data)
        if value is None:
            if is_standby(data):
                hdd_standby += 1
            else:
                notes.append(f"{device}: no HDD temperature")
            continue
        LOG.debug("%s HDD temperature: %.1f", device, value)
        hdd_values.append(value)

    nvme_max = clamp_temp(max(nvme_values)) if nvme_values else None
    if hdd_values:
        hdd_max: int | None = clamp_temp(max(hdd_values))
    elif hdd_standby == len(cfg["hdd"]) and cfg["standby_temp"] is not None:
        hdd_max = clamp_temp(cfg["standby_temp"])
        notes.append(f"all {hdd_standby} HDDs in standby; sending {hdd_max} C")
    else:
        hdd_max = None

    for name, value in (("NVMe_Temp", nvme_max), ("HDD_Temp", hdd_max)):
        if value is not None:
            LOG.info("%s=%d C", name, value)
    for note in notes:
        LOG.warning("%s", note)
    return nvme_max, hdd_max, notes


def redfish_request(cfg: dict, nvme: int | None, hdd: int | None) -> tuple[str, bytes]:
    temperatures = []
    if nvme is not None:
        temperatures.append({"MemberId": "temperature_NVMe_Temp", "ReadingCelsius": nvme})
    if hdd is not None:
        temperatures.append({"MemberId": "temperature_HDD_Temp", "ReadingCelsius": hdd})
    if not temperatures:
        raise RuntimeError("nothing to push")
    if not cfg["redfish_url"]:
        raise RuntimeError("redfish_url is empty")
    url = cfg["redfish_url"].rstrip("/") + f"/redfish/v1/Chassis/{cfg['chassis']}/Thermal/"
    return url, json.dumps({"Temperatures": temperatures}).encode("utf-8")


def push_redfish(cfg: dict, nvme: int | None, hdd: int | None) -> None:
    url, body = redfish_request(cfg, nvme, hdd)
    request = urllib.request.Request(url, data=body, method="PATCH")
    request.add_header("Content-Type", "application/json")
    user = cfg["redfish_user"]
    password = read_password(cfg["redfish_password_file"])
    request.add_header(
        "Authorization", "Basic " + base64.b64encode(f"{user}:{password}".encode()).decode()
    )
    context = ssl.create_default_context()
    if not cfg["verify_tls"]:
        context.check_hostname = False
        context.verify_mode = ssl.CERT_NONE
    try:
        with urllib.request.urlopen(request, context=context, timeout=20) as response:
            if response.status >= 300:
                raise RuntimeError(f"Redfish PATCH returned {response.status}")
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", "replace")[:400]
        raise RuntimeError(f"Redfish PATCH failed: HTTP {exc.code}: {detail}") from exc


def ipmi_command(cfg: dict) -> list[str]:
    transport = cfg["transport"]
    if transport == "ipmi-open":
        return ["ipmitool", "-I", "open"]
    if transport == "ipmi-lanplus":
        if not cfg["ipmi_host"] or not cfg["ipmi_user"]:
            raise RuntimeError("ipmi_host and ipmi_user are required for ipmi-lanplus")
        return ["ipmitool", "-I", "lanplus", "-H", cfg["ipmi_host"], "-U", cfg["ipmi_user"], "-E"]
    raise RuntimeError(f"unsupported transport {transport!r}")


def ipmi_commands(cfg: dict, nvme: int | None, hdd: int | None) -> list[list[str]]:
    base = ipmi_command(cfg)
    commands = []
    for sensor, value in ((NVME_SENSOR, nvme), (HDD_SENSOR, hdd)):
        if value is None:
            continue
        commands.append(
            base
            + [
                "raw", "0x04", "0x30", f"0x{sensor:02X}", "0x00", f"0x{value:02X}",
                "0x00", "0x00", "0x00", "0x00", "0x00", "0x00", "0x00",
            ]
        )
    if not commands:
        raise RuntimeError("nothing to push")
    return commands


def ipmi_env(cfg: dict) -> dict[str, str]:
    env = dict(os.environ)
    if cfg["transport"] == "ipmi-lanplus":
        env["IPMI_PASSWORD"] = read_password(cfg["ipmi_password_file"])
    return env


def ipmi_present_sensors(cfg: dict) -> set[int]:
    """Return the node804 sensor numbers actually present in the BMC SDR.

    Set Sensor Reading returns success for an unknown sensor number on the
    static SDR path, so it cannot be used to detect a stale BMC image. Get
    Sensor Reading returns 0xcb for a missing number.
    """
    present: set[int] = set()
    env = ipmi_env(cfg)
    for sensor in (NVME_SENSOR, HDD_SENSOR):
        cmd = ipmi_command(cfg) + [
            "raw", "0x04", "0x2D", f"0x{sensor:02X}",
        ]
        proc = subprocess.run(cmd, capture_output=True, text=True, env=env)
        if proc.returncode == 0:
            present.add(sensor)
        else:
            detail = (proc.stderr or proc.stdout).strip().splitlines()
            LOG.error("BMC has no sensor 0x%02X; flash the meta-node804 image (%s)",
                      sensor, detail[0] if detail else f"exit {proc.returncode}")
    return present


def push_ipmi(cfg: dict, nvme: int | None, hdd: int | None) -> None:
    env = ipmi_env(cfg)
    for cmd in ipmi_commands(cfg, nvme, hdd):
        LOG.debug("running %s", " ".join(cmd))
        subprocess.run(cmd, check=True, env=env, capture_output=True, text=True)


def push(cfg: dict, nvme: int | None, hdd: int | None) -> None:
    if cfg["transport"] == "redfish":
        push_redfish(cfg, nvme, hdd)
    else:
        push_ipmi(cfg, nvme, hdd)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Push Node 804 NVMe and SAS HDD maximum temperatures to the BMC.",
        epilog="See meta-node804/README.md for the BMC sensor contract.",
    )
    parser.add_argument("--config", default=DEFAULT_CONFIG_PATH, help=f"JSON config (default {DEFAULT_CONFIG_PATH})")
    parser.add_argument("--once", action="store_true", help="run one cycle and exit")
    parser.add_argument("--dry-run", action="store_true", help="print values and payload, do not contact the BMC")
    parser.add_argument("--verbose", action="store_true", help="debug logging")
    args = parser.parse_args()

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)s %(message)s",
    )
    try:
        cfg = load_config(args.config)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        LOG.error("config: %s", exc)
        return 2

    LOG.info("transport=%s interval=%ss NVMe=%s HDD=%s",
             cfg["transport"], cfg["interval"], cfg["nvme"], cfg["hdd"])

    ok = True
    while True:
        nvme, hdd, _ = collect(cfg)
        if not args.dry_run and cfg["transport"] != "redfish":
            try:
                present = ipmi_present_sensors(cfg)
            except (OSError, RuntimeError, ValueError) as exc:
                LOG.error("IPMI sensor preflight failed: %s", exc)
                present = set()
            if NVME_SENSOR not in present:
                nvme = None
            if HDD_SENSOR not in present:
                hdd = None
            if len(present) != 2:
                ok = False
        if nvme is None or hdd is None:
            ok = False
            missing = [n for n, v in (("NVMe_Temp", nvme), ("HDD_Temp", hdd)) if v is None]
            LOG.error("not pushing %s; its PID zone will time out", ", ".join(missing))
        if nvme is not None or hdd is not None:
            if args.dry_run:
                try:
                    if cfg["transport"] == "redfish":
                        url, body = redfish_request(cfg, nvme, hdd)
                        LOG.info("dry-run PATCH %s %s", url, body.decode())
                    else:
                        for cmd in ipmi_commands(cfg, nvme, hdd):
                            LOG.info("dry-run: %s", " ".join(cmd))
                except RuntimeError as exc:
                    ok = False
                    LOG.error("dry-run payload: %s", exc)
            else:
                try:
                    push(cfg, nvme, hdd)
                    ok = True
                except (RuntimeError, OSError, subprocess.CalledProcessError) as exc:
                    ok = False
                    LOG.error("push failed: %s", exc)
        if args.once:
            return 0 if ok else 1
        time.sleep(int(cfg["interval"]))


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
