# MC12-LE0 LEDs

Known BMC-controlled LEDs and the stock firmware evidence.

## Summary

| GPIO | OpenBMC name | Location | Colour | Stock use |
| ---: | --- | --- | --- | --- |
| 48 / G0 | `led-bmc-heartbeat` | motherboard SMT LED near the AST2500 | green | live/heartbeat |
| 49 / G1 | `led-rear-id` | rear I/O ID LED | blue | ID LED, toggled by the ID button |
| 50 / G2 | `led-front-status-green` | FP_1 pins 8/10 | green | system status |
| 51 / G3 | `led-front-status-amber` | FP_1 pins 8/10 | amber | system status |
| — | — | FP_1 pins 1/5 | blue | ATX power LED, 5 V standby; not BMC-driven |
| — | — | FP_1 pins 12/14, 22/24 | — | LAN LEDs, PHY-driven |

The BMC heartbeat is the green SMT LED on the motherboard. The rear ID LED
is the blue LED on the rear I/O panel. The front-panel status LED is a
separate bicolor green/amber LED on FP_1 pins 8/10.

## Stock firmware evidence

`/etc/rcS.d/S11atos-recovery.sh` names the pins directly:

```sh
LED_ID=49
LED_ST_G=50
LED_ST_A=51
...
led_init()
{
    # init front plane ID LED
    gpiotool --set-dir-output $LED_ID
    gpiotool --set-data-high $LED_ID

    # init front plane STATUS LED
    gpiotool --set-dir-output $LED_ST_G
    gpiotool --set-dir-output $LED_ST_A
    gpiotool --set-data-high $LED_ST_G
    gpiotool --set-data-high $LED_ST_A
}
```

During BMC recovery the script blinks the ID LED and the green status LED
together at 1 Hz (`led_blink()`).

`usr/local/lib/libgbt.so` in the stock CramFS confirms the same set:

- `.rodata:0x83d9c` is the table `{48, 49, 50, 51}` (G0–G3).
- `led_init()` configures all four as outputs, then drives G2 low.
- `set_manual_liveLED()` drives GPIO48; `led_routine()` toggles 48.
- `cb_irq_led()` responds to GPIO134 (Q6, ID button) by toggling 49.
- `set_fault_led()`: 0 = green solid (50 low), 1 = green blink,
  2 = amber solid (51 low), 3 = amber blink, 4 = both off.

All four are active-low.

## FP_1 header

Relevant pins from the board layout (section 10):

| Pin | Signal | BMC connection |
| ---: | --- | --- |
| 1/5 | Power LED ± | 5 V standby; blue ATX LED, not BMC-driven |
| 4/6 | ID LED ± | GPIO49 / G1 |
| 7/9 | HDD LED ± | reserved for Gigabyte systems |
| 8/10 | System Status LED ± | GPIO50 / G2 green, GPIO51 / G3 amber |
| 11 | Power Button | GPIO24 / D0 `button-power-n`, GPIO25 / D1 `control-power-n` |
| 15 | Reset Button | GPIO26 / D2 `button-reset-n`, GPIO27 / D3 `control-reset` |
| 19 | ID Button | GPIO134 / Q6 `id-button` |
| 23 | NMI Switch | GPIO29 / D5 `control-nmi` |

## Status LED drive

FP_1 pins 8/10 are a two-wire differential pair, driven by the stock
`set_fault_led()` states:

| State | G2 (50) | G3 (51) | Bicolor result |
| --- | ---: | ---: | --- |
| green solid | 0 | 1 | green |
| green blink | toggles | 1 | green blinks |
| amber solid | 1 | 0 | amber |
| amber blink | 1 | toggles | amber blinks |
| off | 1 | 1 | off |

This matches a reverse-parallel bicolor LED: one die conducts G3 → G2
(green), the other G2 → G3 (amber). All GPIOs are active-low, and the board
provides the series current limiting.

### Single-colour LED workaround

A single-colour LED can be connected across 8/10. It has one die, so it
conducts in one direction only: it lights for one state and is
reverse-biased for the other. Choose the polarity for the state you want
visible (normally green: G2 low / G3 high) and treat the dark state as the
opposite indication. It cannot display both green and amber.

Reverse bias is the GPIO high level, about 3.3 V, below the usual 5 V LED
reverse-voltage rating, so a standard LED is not expected to be damaged.
Verify Vr for an unknown LED; if it is below 3.3 V, add an anti-parallel
diode or use a bicolor LED. Forward current is set by the board's series
resistor, so an extra series resistor only reduces brightness.

With the current groups, `enclosure_fault` sets green Off and amber Blink;
on a single-colour LED that means the LED goes dark for the fault, and the
amber blink is invisible. Driving G2 as the fault blink would give a visible
blink but would depart from the stock green/amber mapping.

## OpenBMC behaviour

- `bmc_booted`:
  - `led_bmc_heartbeat` — Blink, 50 % / 1000 ms after `multi-user.target`.
  - `led_front_status_green` — On.
  - `led_front_status_amber` — Off.
- `enclosure_identify`: `led_rear_id` — On.
- `enclosure_fault`: `led_front_status_green` — Off;
  `led_front_status_amber` — Blink, 50 % / 1000 ms.
- `power_on`: empty.

The bicolor status LEDs appear in both `bmc_booted` and `enclosure_fault`, so
each LED keeps the same priority across groups: green `Off`, amber `Blink`.
The fault actions therefore win while `enclosure_fault` is asserted, and the
`bmc_booted` state is restored when it clears. `enclosure_fault` has no
automatic asserter yet; it can be driven over D-Bus, and a fault monitor can
be added later.

`default-state = "on"` on the heartbeat and green status LEDs keeps them
steady during boot. The heartbeat starts blinking and the green status stays
solid once `obmc-led-group-start@bmc_booted` runs after `multi-user.target`.
