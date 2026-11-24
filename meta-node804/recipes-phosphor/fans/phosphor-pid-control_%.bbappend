FILESEXTRAPATHS:prepend := "${THISDIR}/phosphor-pid-control:"
SRC_URI:append = " \
    file://config.node804.json \
    file://20-wait-drive-sensors.conf \
    "

FILES:${PN} += "${systemd_system_unitdir}/phosphor-pid-control.service.d/20-wait-drive-sensors.conf"

# The board bbappend installs the generic config.json first; the Node 804
# policy replaces it with the four-zone version. Fan membership is exclusive:
# SYS_FAN1/2/4 are removed from zone 2 in config.node804.json.
do_install:append:mc12-le0-node804() {
    install -m 0644 -D ${UNPACKDIR}/config.node804.json \
        ${D}${datadir}/swampd/config.json
    install -m 0644 -D ${UNPACKDIR}/20-wait-drive-sensors.conf \
        ${D}${systemd_system_unitdir}/phosphor-pid-control.service.d/20-wait-drive-sensors.conf
}
