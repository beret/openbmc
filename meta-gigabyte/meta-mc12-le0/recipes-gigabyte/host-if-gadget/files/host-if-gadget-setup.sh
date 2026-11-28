#!/bin/sh
#
# Present an RNDIS USB Ethernet function to the host over the AST2500 vhub.
#
# The host BIOS opens a UEFI Simple Network interface on this device and sends
# its Redfish requests to https://169.254.0.17. The stock firmware presents the
# same transport with AMI's USB IDs, so keep them: the BIOS USB stack matches
# the device it expects. The link address itself is systemd-networkd's job
# (70-usb-host-if.network), not this script's.
#
# Ethernet only. A HID function in the same configuration makes strictly
# parsing hosts discard the configuration; the KVM HID belongs on its own vhub
# port. With RNDIS as the only function its interface numbers stay 0/1, so
# f_rndis' fixed CDC call-management bDataInterface is correct and no kernel
# patch is needed.

set -eu

GADGET_NAME=host-if
GADGET=/sys/kernel/config/usb_gadget/$GADGET_NAME
IFACE=usb0

# One UDC port per gadget. Port 1 is the network function; the KVM owns the
# other port, so do not fall back to it.
UDC=1e6a0000.usb-vhub:p1

log()
{
    echo "host-if-gadget: $*"
}

# systemd runs this with "stop" from ExecStop.
if [ "${1:-}" = "stop" ]; then
    if [ -s "$GADGET/UDC" ]; then
        echo "" > "$GADGET/UDC"
    fi
    exit 0
fi

[ -d /sys/kernel/config/usb_gadget ] || {
    log "configfs is not mounted"
    exit 1
}
[ -e "/sys/class/udc/$UDC" ] || {
    log "UDC $UDC is not available"
    exit 1
}

# A bound gadget cannot be reconfigured and needs no further setup.
if [ -s "$GADGET/UDC" ]; then
    log "already bound to $(cat "$GADGET/UDC")"
    exit 0
fi

mkdir -p "$GADGET/strings/0x409" \
         "$GADGET/configs/c.1/strings/0x409" \
         "$GADGET/functions/rndis.usb0"
cd "$GADGET"

# AMI's USB IDs, so the host's firmware matches the device it expects:
#   vendor 0x046b (American Megatrends Inc.)
#   product 0xffb0 ("Composite Hid and Ethernet Device" in the stock stack)
echo 0x046b > idVendor
echo 0xffb0 > idProduct
echo 0x0100 > bcdDevice
echo 0x0200 > bcdUSB

# Plain Ethernet device class. The 0xEF/0x02/0x01 composite shape that f_rndis
# emits with an interface-association descriptor is rejected by some hosts.
echo 0x02 > bDeviceClass
echo 0x00 > bDeviceSubClass
echo 0x00 > bDeviceProtocol

echo "American Megatrends Inc." > strings/0x409/manufacturer
echo "Virtual Ethernet" > strings/0x409/product
echo "1234567890" > strings/0x409/serialnumber

echo "RNDIS" > configs/c.1/strings/0x409/configuration
echo 250 > configs/c.1/MaxPower

echo 02:00:00:00:01:01 > functions/rndis.usb0/dev_addr
echo 02:00:00:00:01:02 > functions/rndis.usb0/host_addr
# Pin the netdev name so 70-usb-host-if.network always matches it.
echo "$IFACE" > functions/rndis.usb0/ifname

if [ ! -L configs/c.1/rndis.usb0 ]; then
    ln -s functions/rndis.usb0 configs/c.1/
fi

log "binding to $UDC"
echo "$UDC" > UDC
