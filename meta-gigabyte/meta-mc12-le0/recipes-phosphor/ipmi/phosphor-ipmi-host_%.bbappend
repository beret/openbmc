FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

SRC_URI:append = " \
    file://0001-systemintfcmds-handle-host-message-flag-and-global-e.patch \
    "
