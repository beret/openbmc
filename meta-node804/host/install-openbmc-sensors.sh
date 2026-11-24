#!/bin/sh
# Install the OpenBMC host sensor bridge on a Linux host.
#
# Generic across OpenBMC projects that publish ExternalSensors for this
# bridge. Site-specific deployment (pool paths, POSTINIT, credentials) belongs
# in the infrastructure repo, not here.
#
# Usage:
#   sudo ./install-openbmc-sensors.sh
#   sudo ./install-openbmc-sensors.sh \
#       --install-dir /mnt/zippy/tools \
#       --config /mnt/zippy/tools/openbmc-sensors.json \
#       --unit-dir /etc/systemd/system \
#       --mount-dir /mnt/zippy/tools
#
# Existing config is never overwritten. With --skip-systemd the files are
# installed but no unit is enabled or started.
set -eu

usage() {
    cat <<'EOF'
Usage: install-openbmc-sensors.sh [options]

  --install-dir DIR    script and installer directory
                       (default /usr/local/libexec/openbmc-sensors)
  --config FILE        sensor config (default /etc/openbmc-sensors/config.json)
  --unit-dir DIR       systemd unit directory (default /etc/systemd/system)
  --service-name NAME  systemd service name (default openbmc-sensors)
  --mount-dir DIR      add RequiresMountsFor=DIR to the unit
  --no-start           enable but do not start the unit
  --skip-systemd       install files only; do not call systemctl
  -h, --help           show this help
EOF
}

die() {
    echo "install-openbmc-sensors: $*" >&2
    exit 1
}

# Install only when the source and destination differ. The stable installer on
# the target is run from --install-dir, where source and destination are the
# same file.
copy_if_different() {
    [ "$1" = "$2" ] || install -m "$3" "$1" "$2"
}

install_dir=/usr/local/libexec/openbmc-sensors
config=/etc/openbmc-sensors/config.json
unit_dir=/etc/systemd/system
service_name=openbmc-sensors
mount_dir=
no_start=0
skip_systemd=0

while [ $# -gt 0 ]; do
    case $1 in
        --install-dir) install_dir=${2:?}; shift 2 ;;
        --config) config=${2:?}; shift 2 ;;
        --unit-dir) unit_dir=${2:?}; shift 2 ;;
        --service-name) service_name=${2:?}; shift 2 ;;
        --mount-dir) mount_dir=${2:?}; shift 2 ;;
        --no-start) no_start=1; shift ;;
        --skip-systemd) skip_systemd=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) usage >&2; exit 2 ;;
    esac
done

if [ "$skip_systemd" != 1 ] && [ "$(id -u)" -ne 0 ]; then
    die "run as root (or use --skip-systemd)"
fi

src_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
for asset in openbmc-sensors.py openbmc-sensors.example.json openbmc-sensors.service.in; do
    [ -f "$src_dir/$asset" ] || die "missing $src_dir/$asset"
done

[ -d "$install_dir" ] || install -d -m 0755 "$install_dir"
config_dir=$(dirname -- "$config")
[ -d "$config_dir" ] || install -d -m 0700 "$config_dir"
[ -d "$unit_dir" ] || install -d -m 0755 "$unit_dir"

script=$install_dir/openbmc-sensors.py
copy_if_different "$src_dir/openbmc-sensors.py" "$script" 0755
if [ -f "$src_dir/install-openbmc-sensors.sh" ]; then
    copy_if_different "$src_dir/install-openbmc-sensors.sh" \
        "$install_dir/openbmc-sensors-install.sh" 0755
fi
for asset in openbmc-sensors.example.json openbmc-sensors.service.in; do
    copy_if_different "$src_dir/$asset" "$install_dir/$asset" 0644
done

if [ ! -e "$config" ]; then
    install -m 0600 "$src_dir/openbmc-sensors.example.json" "$config"
    echo "created $config; edit the nested credentials and sensor policy"
else
    echo "keeping existing $config"
fi

python=$(command -v python3)
mount_line=
[ -n "$mount_dir" ] && mount_line="RequiresMountsFor=$mount_dir"
unit=$unit_dir/$service_name.service
unit_tmp=$(mktemp)
trap 'rm -f "$unit_tmp"' EXIT
sed \
    -e "s|@SCRIPT@|$script|g" \
    -e "s|@CONFIG@|$config|g" \
    -e "s|@PYTHON@|$python|g" \
    -e "s|@MOUNT_LINE@|$mount_line|g" \
    "$src_dir/openbmc-sensors.service.in" > "$unit_tmp"
install -m 0644 "$unit_tmp" "$unit"
echo "installed $script"
echo "installed $unit"

if [ "$skip_systemd" = 1 ]; then
    echo "--skip-systemd: unit not enabled"
    exit 0
fi

systemctl daemon-reload
systemctl enable "$service_name.service"
if [ "$no_start" != 1 ] && "$script" --config "$config" --check; then
    systemctl restart "$service_name.service"
    echo "started $service_name.service"
else
    echo "installed and enabled $service_name.service; not started"
    echo "edit $config, then: systemctl restart $service_name.service"
fi
