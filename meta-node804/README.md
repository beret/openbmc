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
| 2 | `SYS_FAN3`, `SYS_FAN5` | `CPU_1`, DIMM/VR/FCH/LAN | spare header + CPU-chamber intake |
| 3 | `SYS_FAN1` | `NVMe_Temp` | NVMe blower only |
| 4 | `SYS_FAN2`, `SYS_FAN4` | `HDD_Temp` | drive exhaust + drive intake |

Each fan belongs to exactly one zone. `SYS_FAN1`, `SYS_FAN2` and `SYS_FAN4` are
removed from zone 2 in `config.node804.json`.

## Host feed contract

The BMC creates two writable `dbus-sensors` ExternalSensors. The host pushes a
value every 30 s; after 90 s without a push the sensor value becomes `NaN`,
which fails only the corresponding zone to `failsafePercent: 100`.

| Sensor | D-Bus object | Redfish `MemberId` | IPMI sensor |
| --- | --- | --- | --- |
| NVMe hottest sensor | `/xyz/openbmc_project/sensors/temperature/NVMe_Temp` | `temperature_NVMe_Temp` | `0xE0` |
| HDD hottest drive | `/xyz/openbmc_project/sensors/temperature/HDD_Temp` | `temperature_HDD_Temp` | `0xE1` |

Both accept whole degrees Celsius, 0-100. Send both every cycle even when the
value is unchanged: each D-Bus write pets the ExternalSensor watchdog.

At BMC boot with the host already on, zones 3 and 4 start from `NaN` and hold
their 100 % failsafe until the first host push (up to one interval). This is
the same behavior as a stopped feed; start the host service early, or accept
the short fan burst.

### Redfish

```
PATCH /redfish/v1/Chassis/<chassis>/Thermal/
Content-Type: application/json

{"Temperatures":[
  {"MemberId":"temperature_NVMe_Temp","ReadingCelsius":62.0},
  {"MemberId":"temperature_HDD_Temp","ReadingCelsius":41.0}
]}
```

The chassis name comes from `GET /redfish/v1/Chassis`. Use a user with
`ConfigureComponents` privilege. The PATCH is one request for both sensors; if
one `MemberId` is missing the whole update fails.

### IPMI

```
ipmitool raw 0x04 0x30 0xE0 0x00 <NVMe degC> 0x00 0x00 0x00 0x00 0x00 0x00 0x00
ipmitool raw 0x04 0x30 0xE1 0x00 <HDD degC>  0x00 0x00 0x00 0x00 0x00 0x00 0x00
```

In-band `/dev/ipmi0` works and needs no BMC credentials; LAN uses
`-I lanplus -H <bmc> -U <user>`. `ipmitool` exposes no high-level command for
Set Sensor Reading, so this uses `raw`.

The SDR entries in
`recipes-phosphor/ipmi/phosphor-ipmi-sensor-inventory-native/` are required only
for IPMI. Redfish writes the same D-Bus property without an SDR record.

## Host-side aggregation

The host script is tracked in the infrastructure repository as
`hosts/toby/scripts/node804-fanfeed.py`.

- `NVMe_Temp` = maximum of every temperature sensor on both NVMe disks
  (`nvme_smart_health_information_log.temperature_sensors[]`, falling back to
  `temperature`).
- `HDD_Temp` = maximum `temperature.current` across all SAS HDDs in the drive
  chamber. Use a serial/device allowlist; do not include boot SSDs.
- Do not sum temperatures. Fan control needs the hottest sensor, not an
  accumulated value.
- If all HDDs are in standby, push a cool value (30 C) so the HDD zone stays at
  its floor. If a group cannot be read, stop pushing that group so its zone
  fails safe after 90 s.

## Files

| Path | Purpose |
| --- | --- |
| `conf/machine/mc12-le0-node804.conf` | inherits `mc12-le0`, selects this policy |
| `entity-manager/node804-drive-temps.json` | two ExternalSensors |
| `phosphor-pid-control/config.node804.json` | four-zone static PID policy |
| `phosphor-pid-control/20-wait-drive-sensors.conf` | waits for both sensors before swampd starts |
| `phosphor-ipmi-sensor-inventory-native/drive-temp-sensors.yaml` | IPMI sensor numbers and D-Bus mapping |
