# DIMM inventory comes from the SPD EEPROMs (dimm-inventory). Without this,
# smbios-mdr also publishes Dimm objects from Type 17 and every slot appears
# twice in Redfish Memory.
PACKAGECONFIG:append = " smbios-no-dimm"
