FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

SRC_URI:append = " file://server.ttyS3.conf"

# The stock AMI firmware exposes host COM1 through UART4 as /dev/ttyS3.
OBMC_CONSOLE_HOST_TTY = "ttyS3"
