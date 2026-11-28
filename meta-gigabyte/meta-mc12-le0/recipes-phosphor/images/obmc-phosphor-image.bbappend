# Default non-root admin fallback for WebUI/SSH access.
#
# The first-boot mc12-le0-admin-provision service replaces this password from
# the FRU serial when it is a valid 11- or 12-character alphanumeric value, and
# marks it expired. The first login must use bmcweb, which requires a password
# change; SSH, Redfish, and the web interface work after that change. If the
# serial is unavailable or invalid, the 0penBmc fallback remains for that
# initial bmcweb login.
#
# Root access is deliberately unchanged for now; restricting it needs a
# validated recovery path first.

EXTRA_USERS_PARAMS:append:pn-obmc-phosphor-image = " \
    useradd -m -N -s /bin/sh \
        -G redfish,priv-admin,ipmi,hostconsole \
        -p ${DEFAULT_OPENBMC_PASSWORD} admin; \
"
