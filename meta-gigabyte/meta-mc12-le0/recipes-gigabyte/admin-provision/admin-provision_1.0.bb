SUMMARY = "Provision the MC12-LE0 admin password from the FRU serial"
DESCRIPTION = "On first boot, set the non-root admin fallback password from the \
board serial when it is a valid 11- or 12-character alphanumeric value, and mark \
the password expired. Invalid or missing serials leave the 0penBmc fallback."
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/Apache-2.0;md5=89aea4e17d99a7cacdbeed46a0096b10"

SRC_URI = " \
    file://mc12-le0-admin-provision;subdir=admin-provision \
    file://mc12-le0-admin-account.service;subdir=admin-provision \
    file://mc12-le0-admin-provision.service;subdir=admin-provision \
    file://mc12-le0-admin-failure;subdir=admin-provision \
    file://mc12-le0-admin-failure.service;subdir=admin-provision \
    "
S = "${UNPACKDIR}/admin-provision"

inherit systemd features_check

REQUIRED_DISTRO_FEATURES = "systemd"
SYSTEMD_SERVICE:${PN} = "mc12-le0-admin-account.service mc12-le0-admin-provision.service"

# Set to "0" from local.conf when testing first-login password-change handling;
# the fallback account then stays usable without an expiry.
ADMIN_FORCE_PASSWORD_CHANGE ?= "1"

RDEPENDS:${PN} += " \
    entity-manager \
    shadow \
    systemd \
    "

FILES:${PN} += " \
    ${sysconfdir}/default/admin-provision \
    ${systemd_system_unitdir}/mc12-le0-admin-failure.service \
    "
CONFFILES:${PN} += "${sysconfdir}/default/admin-provision"

do_install() {
    install -d ${D}${bindir}
    install -d ${D}${systemd_system_unitdir}
    install -d ${D}${sysconfdir}/default

    install -m 0755 ${S}/mc12-le0-admin-provision ${D}${bindir}/
    install -m 0755 ${S}/mc12-le0-admin-failure ${D}${bindir}/
    install -m 0644 ${S}/mc12-le0-admin-account.service ${D}${systemd_system_unitdir}/
    install -m 0644 ${S}/mc12-le0-admin-provision.service ${D}${systemd_system_unitdir}/
    install -m 0644 ${S}/mc12-le0-admin-failure.service ${D}${systemd_system_unitdir}/
    printf '%s\n' \
        "ADMIN_FORCE_PASSWORD_CHANGE=${ADMIN_FORCE_PASSWORD_CHANGE}" \
        "ADMIN_FALLBACK_HASH=${DEFAULT_OPENBMC_PASSWORD}" \
        > ${D}${sysconfdir}/default/admin-provision
    chmod 0600 ${D}${sysconfdir}/default/admin-provision
}
