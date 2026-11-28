# Opt-in bring-up/test profile for the MC12-LE0 board.
#
# This is not part of the default build and is not a production profile.
# Recipes do not enable diagnostics, probes, or extra packages merely because
# this file exists; they must explicitly opt in with a df-mc12-le0-bringup
# override or a DISTRO_FEATURES check.
#
# Root login is deliberately not enabled here. Any change to root access needs
# separate review plus verified firmware update and recovery procedures.
#
# Enable from local.conf:
#   require conf/distro/include/mc12-le0-bringup.inc

DISTRO_FEATURES += "mc12-le0-bringup"
DISTROOVERRIDES .= ":df-mc12-le0-bringup"

# IPMI request tracing for further OEM protocol discovery. Request-only,
# redacted; never install in production.
IMAGE_INSTALL:append = " ami-smbios-trace"
