# OpenBMC host sensor bundle

Host-side bridge for the `meta-node804` ExternalSensors. This directory is the
project-owned, reusable half: the Python collector, example config, unit
template and installer. Site deployment (pool paths, credentials, POSTINIT) is
in the infrastructure repository.

## Sensors

| Sensor | Transport | BMC contract |
| --- | --- | --- |
| `NVMe_Temp` | Redfish PATCH or IPMI 0x04/0x30 | IPMI 0xE0, Redfish `temperature_NVMe_Temp` |
| `HDD_Temp` | Redfish PATCH or IPMI 0x04/0x30 | IPMI 0xE1, Redfish `temperature_HDD_Temp` |

`NVMe_Temp` excludes the onboard M.2 boot disk by default and takes the maximum
of the remaining NVMe temperature sensors. `HDD_Temp` discovers every
rotational, non-NVMe block device and takes the maximum. HDD queries use
`smartctl -n standby`, so sleeping drives are not spun up.

Both sensors have a 90 s BMC timeout. The bridge pushes every 30 s; an
all-standby HDD group sends `standby_temp` (30 C). Do not sum temperatures.

## Install

```sh
sudo ./install-openbmc-sensors.sh \
    --install-dir /usr/local/libexec/openbmc-sensors \
    --config /etc/openbmc-sensors/config.json \
    --password-file /etc/openbmc-sensors/password
```

Default install paths:

| Path | Contents |
| --- | --- |
| `/usr/local/libexec/openbmc-sensors/openbmc-sensors.py` | bridge |
| `/usr/local/libexec/openbmc-sensors/openbmc-sensors-install.sh` | stable installer |
| `/etc/openbmc-sensors/config.json` | config, created from the example if absent |
| `/etc/systemd/system/openbmc-sensors.service` | generated unit |

Options: `--unit-dir`, `--service-name`, `--mount-dir` (adds
`RequiresMountsFor=`), `--no-start`, `--skip-systemd`. Existing config and
password files are preserved. The installer starts the unit when the transport
is `ipmi-open` or the password file is non-empty.

For a pool-backed host, use `--install-dir` and `--config` on the pool and
`--mount-dir` on the same path; the unit then waits for the mount.

## Verify

```sh
openbmc-sensors.py --config /etc/openbmc-sensors/config.json --dry-run --once
systemctl status openbmc-sensors.service
journalctl -u openbmc-sensors.service -f
```

IPMI transports preflight `Get Sensor Reading` for `0xE0`/`0xE1`. A BMC without
the matching SDR records returns `0xcb`; the bridge logs the missing sensor and
does not push, because `Set Sensor Reading` succeeds even for an unknown sensor
number. Redfish PATCH fails loudly on a missing `MemberId`.

The BMC contract is defined by `../README.md`; this bundle must not invent a
different sensor name, number or timeout.
