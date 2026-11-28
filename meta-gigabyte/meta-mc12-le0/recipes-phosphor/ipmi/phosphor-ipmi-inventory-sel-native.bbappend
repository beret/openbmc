# Replace the generic inventory-to-SEL map. CPU and DIMM inventory objects do
# not exist on this board, and their old numbers collide with the board SDR
# entries. Keep only the system/motherboard callout fallbacks on free numbers.
FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"
