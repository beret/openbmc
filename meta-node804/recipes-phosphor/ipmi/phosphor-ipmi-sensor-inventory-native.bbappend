FILESEXTRAPATHS:prepend := "${THISDIR}/phosphor-ipmi-sensor-inventory-native:"
SRC_URI:append = " file://host-sensors.yaml"

# Add the three ExternalSensors to the static SDR map so the IPMI transport can
# address them by sensor number. Redfish does not need these records; it writes
# the same D-Bus Sensor.Value through /redfish/v1/Chassis/<chassis>/Thermal/.
do_install:append() {
    cat ${UNPACKDIR}/host-sensors.yaml >> ${D}${sensor_datadir}/sensor.yaml
}
