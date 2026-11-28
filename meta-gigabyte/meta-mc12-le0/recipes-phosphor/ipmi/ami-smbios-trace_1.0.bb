SUMMARY = "IPMI request tracer for MC12-LE0 bring-up"
DESCRIPTION = "Pass-through ipmid filter that records every IPMI request and a \
bounded body to /tmp/ami-ipmi-trace.log. User/session and host-interface \
credential bodies are redacted. Bring-up only; it sees requests, not replies."

LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/Apache-2.0;md5=89aea4e17d99a7cacdbeed46a0096b10"

DEPENDS = "phosphor-ipmi-host phosphor-logging sdbusplus"

FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}/files:"

SRC_URI = " \
    file://trace.cpp;subdir=ami-smbios-trace \
    file://meson.build;subdir=ami-smbios-trace \
    "

S = "${UNPACKDIR}/ami-smbios-trace"

inherit meson pkgconfig

FILES:${PN} += "${libdir}/ipmid-providers"
