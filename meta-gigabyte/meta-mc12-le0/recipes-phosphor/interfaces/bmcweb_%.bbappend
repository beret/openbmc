# Use phosphor-logging over D-Bus for Redfish EventLog; the legacy
# /var/log/redfish backend is not populated on this image.
PACKAGECONFIG:append = " redfish-dbus-log"

EXTRA_OEMESON:append = " \
    -Dhttp-body-limit=65 \
"
