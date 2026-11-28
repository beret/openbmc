SUMMARY = "USB Ethernet link for the BIOS Redfish host interface"
DESCRIPTION = "Presents an RNDIS USB Ethernet function to the host over the \
AST2500 virtual hub, using the vendor/product IDs the stock firmware uses. \
The BIOS Redfish client reaches bmcweb at 169.254.0.17 over this link; \
systemd-networkd assigns that address. Ethernet only: the KVM HID function \
stays on its own vhub port."

LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/Apache-2.0;md5=89aea4e17d99a7cacdbeed46a0096b10"

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI = " \
    file://host-if-gadget-setup.sh \
    file://host-if-gadget.service \
    file://70-usb-host-if.network \
    "

S = "${UNPACKDIR}"

inherit systemd features_check

REQUIRED_DISTRO_FEATURES = "systemd"

SYSTEMD_SERVICE:${PN} = "host-if-gadget.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${UNPACKDIR}/host-if-gadget-setup.sh \
        ${D}${bindir}/host-if-gadget-setup

    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${UNPACKDIR}/host-if-gadget.service \
        ${D}${systemd_system_unitdir}/

    install -d ${D}${systemd_unitdir}/network
    install -m 0644 ${UNPACKDIR}/70-usb-host-if.network \
        ${D}${systemd_unitdir}/network/
}

FILES:${PN} += "${systemd_unitdir}/network/70-usb-host-if.network"
