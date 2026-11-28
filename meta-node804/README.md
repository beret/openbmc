# meta-node804: MC12-LE0 in the Node 804 chassis

This layer is mostly a zonal fan/thermal policy. using BMC and OS sensors for a Gigabyte MC12-LE0 MB in a dual-chamber SFF Node 804 case.
Current zones: CPU, MB Chamber, NVMe card, HDD chamber

`/host/` includes the systemd service for exposing OS sensors as ExternalSensor objects inside OpenBMC so OpenBMC can gracefully accept readings over IPMI/Redfish, or fallback to safe defaults. Currently polls (30s) across rotational disks, additional NVME disks not already served by the MB's temp sensor in the onboard M.2 slot, and a Broadcom SAS HBA.

Tested under TrueNAS Scale Linux 25, but no special dependencies.

MB fan mapping is not standardized, aside from CPU_FAN. Temp/PWM response per sensor/zone based on component tolerances and how loathsome its fans are.

Generic board commits and the basic CPU/MB sensor fan policy are also in this node804 branch, but in the `meta-gigabyte/meta-mc12-le0` board
layer.

## Select the build

```sh
cd openbmc/
./setup mc12-le0-node804
bitbake obmc-phosphor-image
```
local build.conf example has build caching turned on for builds across the generic gigabyte target and this one.

If you're setting manually:
```conf
MACHINE = "mc12-le0-node804"
BBLAYERS += "/path/to/openbmc/meta-node804"
```

## Fan zones

| Zone | Fans | Temperature source | Notes |
| --- | --- | --- | --- |
| 1 | `CPU_FAN` | `CPU_1` | unchanged |
| 2 | `SYS_FAN3`, `SYS_FAN5` | `CPU_1`, DIMM/VR/FCH/LAN, `HBA_Temp` | spare header + CPU-chamber intake |
| 3 | `SYS_FAN1` | `NVMe_Temp` | NVMe blower only |
| 4 | `SYS_FAN2`, `SYS_FAN4` | `HDD_Temp` | drive exhaust + drive intake |

Each fan belongs to exactly one zone but sensors can appear in multiple zones or response curves.

Fan tach limits are Node 804-specific; other MC12-LE0 builds keep the board
layer's generic limits.

## Host bridge service

The BMC creates three writable `dbus-sensors` ExternalSensors. The host pushes a
value every 30s but you can go as fast as the BMC lets you. After 90s without a push the sensor value becomes `NaN`.
That fails the corresponding NVMe/HDD zone to `failsafePercent: 100`. HBA
temp is a missing-acceptable input in zone 2 (MB chamber), so that falls back to other signals.

| Sensor | D-Bus object | Redfish `MemberId` | IPMI sensor |
| --- | --- | --- | --- |
| NVMe hottest sensor | `/xyz/openbmc_project/sensors/temperature/NVMe_Temp` | `temperature_NVMe_Temp` | `0xE0` |
| HDD hottest drive | `/xyz/openbmc_project/sensors/temperature/HDD_Temp` | `temperature_HDD_Temp` | `0xE1` |
| HBA ROC temperature | `/xyz/openbmc_project/sensors/temperature/HBA_Temp` | `temperature_HBA_Temp` | `0xE2` |

All three accept whole degrees Celsius, 0-100. Send all three every cycle even
when a value is unchanged: each D-Bus write pets the ExternalSensor watchdog.

Unlike the stock MB sensors, ExternalSensors shouldn't disappear due to host-side power cycles, so those should gracefully recover from missing data once you send a fresh update.
Creating the sensors is a blocking task for starting `swampd`, just like waiting for CPU_TEMP to start the generic MB policy on the board layer.

(If you lose stock MB sensors, they will not be reintegrated until you reboot phosphor-pid-control or the whole BMC. (dynamic sensor discovery is an oBMC option, but more of a hassle.)


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

This currently relies on using the external network to reach the BMC, as the internal virtual ethernet bridge interface is not implemented.

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

The Node 804 SDR entries are appended to the board's static map in
`recipes-phosphor/ipmi/phosphor-ipmi-sensor-inventory-native/`; they are
required only for IPMI. Redfish writes the same D-Bus property without an SDR
record. Their numbers are pinned (`0xE0`–`0xE2`), so the host preflight stays
valid when board sensors come and go.

## Host-side aggregation

The host script is tracked in this layer under
`meta-node804/host/`. That directory also carries a generic installer, unit
template and example config; the infrastructure repository supplies the
site-specific deployment, credentials, and POSTINIT handling.

- `NVMe_Temp` = maximum of every temperature sensor on the two PCIe-card NVMe
  disks (`nvme_smart_health_information_log.temperature_sensors[]`, falling
  back to `temperature`). The onboard M.2 boot disk at
  `/dev/disk/by-path/pci-0000:06:00.0-nvme-1` is excluded by the host script;
  its nearby board sensor (`RT_M2M`) is an input to the zone 2 motherboard/passive components group.
- `HDD_Temp` = maximum `temperature.current` across every rotational,
  non-NVMe block device (`/sys/block/*/queue/rotational == 1`). This picks up
  all SAS/SATA HDDs and excludes NVMe, boot SATA SSDs, loop, dm and md. An
  explicit `hdd` list overrides discovery. The host polls HDDs with
  `smartctl -n standby` so sleeping drives are not spun up.
- `HBA_Temp` = the Broadcom/LSI ROC temperature from
  `storcli /c0 show temperature J`, read as root through `/dev/mpt3ctl`. It is
  a missing-acceptable input to the zone 2 motherboard passive group, so it
  affects the max only when the host bridge is publishing it.
- If all HDDs are in standby, sets a dummy 0 C reading, the policy already has minimum-output.
  If all of an NVMe or HDD group cannot be read, stop sending that group so
  its zone fails safe after 90 s.

## Files

| Path | Purpose |
| --- | --- |
| `conf/machine/mc12-le0-node804.conf` | inherits `mc12-le0`, selects this policy |
| `entity-manager/node804-fans.json` | Node 804 fan limits, replacing the board defaults |
| `entity-manager/node804-host-sensors.json` | three host-fed ExternalSensors |
| `phosphor-pid-control/config.node804.json` | four-zone static PID policy |
| `phosphor-pid-control/20-wait-host-sensors.conf` | waits for all three sensors before swampd starts |
| `phosphor-ipmi-sensor-inventory-native/host-sensors.yaml` | IPMI sensor numbers and D-Bus mapping |
