OpenBMC support for the [Gigabyte MC12-LE0 AM4 B550 Workstation uATX Motherboard](https://www.gigabyte.com/Enterprise/Server-Motherboard/MC12-LE0-rev-1x) (based on AST2500).

## Fan control

`phosphor-pid-control` drives all six motherboard PWM outputs using a
CPU-temperature stepwise curve, starting at 25% and reaching 100% at 95 °C.
Validate startup, airflow and temperatures with the installed fans before
unattended use. CPU temperature does not protect drives; fan stalls and stale
CPU readings are not detected by this policy.
