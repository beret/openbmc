FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

SRC_URI:append = " \
    file://0001-systemintfcmds-handle-host-message-flag-and-global-e.patch \
    file://0002-chassishandler-accept-commit-write-in-boot-option-handshake.patch \
    file://0003-chassishandler-clear-one-time-boot-flag-on-override-clear.patch \
    "
