SUMMARY = "AMI SMBIOS transport for the MC12-LE0 host BIOS"
DESCRIPTION = "Runs the stock SCU handshake and answers the NetFn 0x2E OEM \
window-address and SMBIOS-transfer commands, then republishes the pushed \
tables to smbios-mdr. This is the transport only: the NetFn 0x32 host-interface \
credential channel is handled separately."

LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/Apache-2.0;md5=89aea4e17d99a7cacdbeed46a0096b10"

DEPENDS = "phosphor-ipmi-host phosphor-logging sdbusplus"

FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}/files:"

SRC_URI = " \
    file://main.cpp;subdir=ami-smbios \
    file://meson.build;subdir=ami-smbios \
    "

S = "${UNPACKDIR}/ami-smbios"

inherit meson pkgconfig

# ipmid dlopens every regular .so in this directory.
FILES:${PN} += "${libdir}/ipmid-providers"
