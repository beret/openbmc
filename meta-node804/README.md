# meta-node804: MC12-LE0 in the Node 804 chassis

This layer is the **system-specific** fan/thermal policy for the Fractal Design
Node 804 build. It does not change the generic `mc12-le0` machine or the board
layer.

## Select the build

```conf
MACHINE = "mc12-le0-node804"
BBLAYERS += "/path/to/openbmc/meta-node804"
```

Add `meta-node804` after `meta-gigabyte/meta-mc12-le0` in `bblayers.conf`.
Builds with `MACHINE = "mc12-le0"` keep the generic two-zone policy from the
board layer and never install this layer's recipes.

## Fan zones

| Zone | Fans | Temperature source | Notes |
| --- | --- | --- | --- |
| 1 | `CPU_FAN` | `CPU_1` | unchanged |
| 2 | `SYS_FAN3`, `SYS_FAN5` | `CPU_1`, DIMM/VR/FCH/LAN, `HBA_Temp` | spare header + CPU-chamber intake |
| 3 | `SYS_FAN1` | `NVMe_Temp` | NVMe blower only |
| 4 | `SYS_FAN2`, `SYS_FAN4` | `HDD_Temp` | drive exhaust + drive intake |

Each fan belongs to exactly one zone. `SYS_FAN1`, `SYS_FAN2` and `SYS_FAN4` are
removed from zone 2 in `config.node804.json`.

## Host bridge contract

The BMC creates three writable `dbus-sensors` ExternalSensors. The host pushes a
value every 30 s; after 90 s without a push the sensor value becomes `NaN`.
That fails the corresponding NVMe/HDD zone to `failsafePercent: 100`; the HBA
value is a missing-acceptable input in zone 2, so a missing HBA is simply
excluded from that zone's maximum.

| Sensor | D-Bus object | Redfish `MemberId` | IPMI sensor |
| --- | --- | --- | --- |
| NVMe hottest sensor | `/xyz/openbmc_project/sensors/temperature/NVMe_Temp` | `temperature_NVMe_Temp` | `0xE0` |
| HDD hottest drive | `/xyz/openbmc_project/sensors/temperature/HDD_Temp` | `temperature_HDD_Temp` | `0xE1` |
| HBA ROC temperature | `/xyz/openbmc_project/sensors/temperature/HBA_Temp` | `temperature_HBA_Temp` | `0xE2` |

All three accept whole degrees Celsius, 0-100. Send all three every cycle even
when a value is unchanged: each D-Bus write pets the ExternalSensor watchdog.

At BMC boot with the host already on, zones 3 and 4 start from `NaN` and hold
their 100 % failsafe until the first host push (up to one interval). This is
the same behavior as a stopped bridge; start the host service early, or accept
the short fan burst.

### Redfish

```
PATCH /redfish/v1/Chassis/<chassis>/Thermal/
Content-Type: application/json

{"Temperatures":[
  {"MemberId":"temperature_NVMe_Temp","ReadingCelsius":62.0},
  {"MemberId":"temperature_HDD_Temp","ReadingCelsius":41.0},
  {"MemberId":"temperature_HBA_Temp","ReadingCelsius":57.0}
]}
```

The chassis name comes from `GET /redfish/v1/Chassis`. Use a user with
`ConfigureComponents` privilege. The PATCH is one request for all sensors; if
one `MemberId` is missing the whole update fails.

### IPMI

```
ipmitool raw 0x04 0x30 0xE0 0x00 <NVMe degC> 0x00 0x00 0x00 0x00 0x00 0x00 0x00
ipmitool raw 0x04 0x30 0xE1 0x00 <HDD degC>  0x00 0x00 0x00 0x00 0x00 0x00 0x00
ipmitool raw 0x04 0x30 0xE2 0x00 <HBA degC>  0x00 0x00 0x00 0x00 0x00 0x00 0x00
```

In-band `/dev/ipmi0` works and needs no BMC credentials; LAN uses
`-I lanplus -H <bmc> -U <user>`. `ipmitool` exposes no high-level command for
Set Sensor Reading, so this uses `raw`.

The host preflights `Get Sensor Reading` (NetFn 0x04, Cmd 0x2D) before pushing.
Set Sensor Reading returns success for an unknown sensor number, so it cannot
detect a BMC still running an older image; Get Sensor Reading returns `0xcb`
until the three SDR entries exist.

The SDR entries in
`recipes-phosphor/ipmi/phosphor-ipmi-sensor-inventory-native/` are required only
for IPMI. Redfish writes the same D-Bus property without an SDR record.

## Host-side aggregation

The host script is tracked in this layer under
`meta-node804/host/`. That directory also carries a generic installer, unit
template and example config; the infrastructure repository supplies the
site-specific deployment, credentials and POSTINIT handling.

- `NVMe_Temp` = maximum of every temperature sensor on the two PCIe-card NVMe
  disks (`nvme_smart_health_information_log.temperature_sensors[]`, falling
  back to `temperature`). The onboard M.2 boot disk at
  `/dev/disk/by-path/pci-0000:06:00.0-nvme-1` is excluded by the host script;
  its nearby board sensor (`RT_M2M`) is an input to the zone 2 board passive
  fan group, not the NVMe blower zone.
- `HDD_Temp` = maximum `temperature.current` across every rotational,
  non-NVMe block device (`/sys/block/*/queue/rotational == 1`). This picks up
  all SAS/SATA HDDs and excludes NVMe, boot SATA SSDs, loop, dm and md. An
  explicit `hdd` list overrides discovery. The host polls HDDs with
  `smartctl -n standby` so sleeping drives are not spun up.
- `HBA_Temp` = the Broadcom/LSI ROC temperature from
  `storcli /c0 show temperature J`, read as root through `/dev/mpt3ctl`. It is
  a missing-acceptable input to the zone 2 motherboard passive group, so it
  affects the max only when the host bridge is publishing it.
- Do not sum temperatures. Fan control needs the hottest sensor, not an
  accumulated value.
- If all HDDs are in standby, push a cool value (30 C) so the HDD zone stays at
  its floor. If an NVMe or HDD group cannot be read, stop pushing that group so
  its zone fails safe after 90 s. A missing HBA only removes it from the zone 2
  maximum.

## Files

| Path | Purpose |
| --- | --- |
| `conf/machine/mc12-le0-node804.conf` | inherits `mc12-le0`, selects this policy |
| `entity-manager/node804-drive-temps.json` | three ExternalSensors |
| `phosphor-pid-control/config.node804.json` | four-zone static PID policy |
| `phosphor-pid-control/20-wait-drive-sensors.conf` | waits for all three sensors before swampd starts |
| `phosphor-ipmi-sensor-inventory-native/drive-temp-sensors.yaml` | IPMI sensor numbers and D-Bus mapping |
