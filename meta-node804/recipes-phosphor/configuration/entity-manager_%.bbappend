FILESEXTRAPATHS:prepend := "${THISDIR}/entity-manager:"
SRC_URI:append = " \
    file://node804-fans.json \
    file://node804-host-sensors.json \
    "

# node804-fans.json replaces the board layer's generic fan records via
# DisableNode; FOUND() makes that run after the board config.
do_install:append:mc12-le0-node804() {
    install -m 0444 ${UNPACKDIR}/node804-fans.json -D \
        -t ${D}${datadir}/entity-manager/configurations/node804
    install -m 0444 ${UNPACKDIR}/node804-host-sensors.json -D \
        -t ${D}${datadir}/entity-manager/configurations/node804
}
