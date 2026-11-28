# Replace the generic sample sensor.yaml (CPU/DIMM/PSU records this board does
# not expose). Same filename so the base do_install picks it up.
FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"
