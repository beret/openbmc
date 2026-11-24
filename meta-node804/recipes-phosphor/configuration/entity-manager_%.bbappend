FILESEXTRAPATHS:prepend := "${THISDIR}/entity-manager:"
SRC_URI:append = " file://node804-drive-temps.json"

# Entity Manager merges every configuration whose Probe matches, so this file
# adds the three host-fed sensors without touching the board JSON.
do_install:append:mc12-le0-node804() {
    install -m 0444 ${UNPACKDIR}/node804-drive-temps.json -D \
        -t ${D}${datadir}/entity-manager/configurations/node804
}
