# MC12-LE0 sensors

Derived from stock AMI firmware (board map `etc/devmaps/MC12/MC12-LE0-00.xml` inside
BMC image) and the ones currently included in this OpenBMC build

Addresses in the stock column are **8-bit** as written in the vendor map
(`0x98` = 7-bit `0x4c`); the OpenBMC column uses 7-bit as Linux does.

## Temperatures

| Stock name | no | Source | SDR template | OpenBMC | State |
| --- | --- | --- | --- | --- | --- |
| `CPU0_TEMP` | 0x01 | SB-TSI, i2c2 `0x98` | `CPU_TEMP` | `CPU_1` (SBTSI) | published |
| `CPU0_DTS` | 0x0c | alternate CPU source, `MINUS` of taker 5 | `DTS_TEMP` | — | not implemented |
| `X550_LAN_TEMP` | 0x08 | TMP75, i2c4 mux ch0 `0x98` | `MB_TEMP` | `X550_LAN_TEMP` (TMP75) | published |
| `B550_FCH_TEMP` | 0x09 | TMP75, i2c4 mux ch0 `0x9A` | `MB_TEMP` | `B550_FCH_TEMP` (TMP75) | published |
| `VR_TEMP` | 0x12 | TMP75, i2c4 mux ch0 `0x9C` | `MB_TEMP` | `VR_TEMP` (TMP75) | published |
| `RT_M2M` | 0x1C | NCT7802Y RTD1, i2c4 mux ch0 `0x50` | `GPU_TEMP` | `RT_M2M` (NCT7802) | published, needs `76630ca6` |
| `DIMMG0_TEMP` | 0x04 | max of 8× `I2C_MEM_TEMP`, i2c5 `0x30`–`0x3E` | `DIMM_TEMP` | `DIMMA0/A1/B0/B1_TEMP` (JC42) | published as four, not one max |

## Voltages

| Stock name | no | ADC ch | Nominal | OpenBMC | State |
| --- | --- | ---: | --- | --- | --- |
| `A_VDDPS5` | 0x44 | 4 | SoC/VDDP, ~0.9 V | `A_VDDPS5` | published, `ScaleFactor 1.0` |
| `VCORE` | 0x46 | 6 | core, 0.8–1.5 V | `VCORE` | published, `ScaleFactor 1.0` |
| `P1V2_VDDQ_S3_S0` | 0x48 | 8 | DDR4 VDDQ, 1.2 V | `P1V2_VDDQ` | published, `ScaleFactor 1.0`; stock marks it `hide="1"` |
| `P12V_S5_SCALED` | 0x40 | 0 | 12 V standby | — | **not published**: divider ratio unknown |
| `P5V_S0_SCALED` | 0x41 | 1 | 5 V | — | **not published**: divider ratio unknown |
| `P3V3_S0_SCALED` | 0x42 | 2 | 3.3 V | — | **not published**: divider ratio unknown |
| `P_VBAT_SCALED` | 0x45 | 5 | 3.0 V coin cell | — | **not published**: divider ratio unknown, GPIO-switched |
| `P3V3_S5` | 0x4c | 12 | 3.3 V standby | — | **not published**: divider ratio unknown |
| `A_VDD18S5` | 0x4d | 13 | 1.8 V | — | **not published**: divider ratio unknown |

The six unpublished rails are readable as raw millivolts; what is missing is
the board divider ratio. `meta-amd` daytonax's OpenBMC config uses (`0.112` 12 V, 
`0.272` 5 V, `0.411` 3.3 V, `0.750` 1.8 V). Those are similar to the back-solved values
on a test board, when assuming the board is at normal rail voltages.

## Fans and PWM

| Stock name | no | Tach ch | PWM ch | OpenBMC | State |
| --- | --- | ---: | ---: | --- | --- |
| `CPU0_FAN` | 0xA0 | 0 | 0 | `CPU_FAN`, `CPU_FAN_PWM` | published |
| `SYS_FAN1` | 0xA2 | 2 | 2 | `SYS_FAN1`, `SYS_FAN1_PWM` | published |
| `SYS_FAN2` | 0xA3 | 3 | 3 | `SYS_FAN2`, `SYS_FAN2_PWM` | published |
| `SYS_FAN4` | 0xA4 | 4 | 4 | `SYS_FAN4`, `SYS_FAN4_PWM` | published |
| `SYS_FAN3` | 0xA5 | 5 | 5 | `SYS_FAN3`, `SYS_FAN3_PWM` | published |
| `SYS_FAN5` | 0xA6 | 6 | 6 | `SYS_FAN5`, `SYS_FAN5_PWM` | published |

Tach is published in RPM. PWM is raw 0–255 at the sysfs and
`fan_pwm` level, 0–100 % on D-Bus (`Unit.Percent`). `phosphor-pid-control` sets
all six outputs per fan policy.

## Power and PSU


| Stock name | no | Source | OpenBMC | State |
| --- | --- | --- | --- | --- |
| `APML_CAPPING` | 0x180 | SB-RMI, i2c2 `0x78` | `sbrmi@3c` (hwmon, unpublished) | untested|
| `APML_PWR` | 0x182 | SB-RMI, same device | - | same |
| `CPU_INFO` | 0x100 | SB-RMI CPUID window | — | - |
| `PS1_Status` | 0xE6 | PMBus, i2c3 `0xB0` | — | not implemented |
| `PS2_Status` | 0xE7 | PMBus, i2c3 `0xB2` | — | not implemented |
| `PSU1_POWER` | 0x108 | PMBus cmd `0x97`, `0xB0` | — | not implemented |
| `PSU2_POWER` | 0x109 | PMBus cmd `0x97`, `0xB2` | — | not implemented |

SB-RMI is largely for EPYC CPUs, but this support was in stock BMC, so the modules are
enabled in this build just in case. When available sysfs should show, but this is not
wired to `entity-manager` or similar. 


PMBus is on the board and requires compatible PSUs, untested. The stock map polls two
PMBus addresses on `i2c-3`, so two supplies can share that bus.

Kernel module and userspace tools already include PMBus support from base
`aspeed_g6_defconfig`, but endpoints are not configured. 

## Discrete / Not a Sensor

| Stock name | no | Source | OpenBMC | State |
| --- | --- | --- | --- | --- |
| `CPU0_Status` | 0xe4 | GPIO b1=121, b7=120 | — | not implemented |
| `INTSN_EC_FUN1` | 0x210 | EC firmware, i2c7 `0x24` | — | not implemented; no EC answers |
| `RAID_INFO` | 0x240 | i2c4 mux ch5 `0x02`, 60 s poll | — | not implemented |

None of these is a threshold sensor: the stock firmware emits them as discrete
SDR types (state bits or strings, not a number with limits), which is why they
cannot be expressed as Entity Manager temperature or voltage entries.

- **`CPU0_Status`** reads two AST2500 GPIO lines (bank P: 120, 121) into bits of
  the reading. Other Gigabyte boards use the same `CPU_STATUS` template with a
  different line pair per socket (`b1=209 b7=208` for CPU1), so the two bits
  encode processor state, not a temperature. Both lines are unclaimed in this
  device tree and AMI's bit-to-state mapping is not documented in the image, so
  it is left out.
- **`INTSN_EC_FUN1`** polls an embedded controller at `i2c7` `0x24` for
  EC-originated faults (power/thermal). Tested board answers on `i2c7` only at
  `0x51`.
- **`RAID_INFO`** is an adapter summary (state strings, not wattage) read from
  `0x02` behind a mux channel every 60 s. The PCA9545 here has four channels and
  none of them answers at `0x02`; no RAID/HBA is fitted.

## Notes on names and ranges

- **`RT_M2M`** is the M.2 module thermistor, not a room-temperature sensor:
  NCT7802Y remote channel RTD1 behind the I2C mux. The stock `GPU_TEMP` template name is
  inherited from AMI's shared templates and says nothing about a GPU. It is an
  input to the `temp_passive` board fan group; a missing host-off sensor is
  acceptable.
- **`CPU0_DTS`** is the die-temperature form of the CPU reading, produced by a
  `MINUS` operation over two sources: the temperature itself
  (`APML_CPU`, SB-TSI) and `APML_CPU_INFO` (SB-RMI CPU metadata). So it is a
  subtraction against the CPU-reported offset (Tctl offset on AMD, Tjmax on
  Intel.) Two consequences: the value needs SB-RMI, which is not present on most AM4
  CPUs, and it may duplicate `CPU_1` when offset is 0/null.
- **`DIMMG0_TEMP`** is an aggregate: stock polls eight SPD addresses
  (`0x18`–`0x1F`) and publishes the maximum.
- **`A_VDDPS5`** is the SoC/VDDP rail (stock SDR template `P_VDDCR`); **`VCORE`**
  uses the `P_VDDCR_MC12` template. Both sit near 0.9 V and 0.8–1.5 V.
- **`P1V2_VDDQ_S3_S0`** is the DDR4 VDDQ rail. Stock hides it (`hide="1"`); we
  publish it because the channel has no divider.
- **`P12V_S5` / `P3V3_S5`** are *standby* rails (present with the host off);
  `P5V_S0` / `P3V3_S0` are run rails. Nominal ±5 %.
- **`P_VBAT_SCALED`** is the 3 V coin-cell battery through a GPIO-switched divider
  (GPIO 141).
- **`APML_CAPPING`** is the package power *limit* (watts, writable on a
  supporting CPU); **`APML_PWR`** is package power *consumption*. The `sbrmi`
  hwmon also exposes `power1_input` in microwatts.
- **Sensor numbers (`no`)** are stock SDR record numbers. OpenBMC's ipmid
  assigns numbers sequentially from the D-Bus sensor tree, so these values
  cannot be matched without unnecessarily patching ipmid.

## Address convention

The vendor map writes 8-bit addresses (`0x98`, `0xB0`, `0xE2`); the device tree
and Linux use 7-bit (`0x4c`, `0x58`, `0x71`). The mux is a PCA9545 at 7-bit
`0x71` on `i2c4`; channel 0 is `i2c-14`, which carries the three TMP75s and the
NCT7802Y.
