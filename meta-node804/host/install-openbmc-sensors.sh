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
    echo "install-openbmc-sensors: run as root (or use --skip-systemd)" >&2
    exit 1
fi

src_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
for file in openbmc-sensors.py openbmc-sensors.example.json openbmc-sensors.service.in; do
    [ -f "$src_dir/$file" ] || {
        echo "install-openbmc-sensors: missing $src_dir/$file" >&2
        exit 1
    }
done

[ -d "$install_dir" ] || install -d -m 0755 "$install_dir"
config_dir=$(dirname -- "$config")
[ -d "$config_dir" ] || install -d -m 0700 "$config_dir"
[ -d "$unit_dir" ] || install -d -m 0755 "$unit_dir"

script=$install_dir/openbmc-sensors.py
if [ "$src_dir/openbmc-sensors.py" != "$script" ]; then
    install -m 0755 "$src_dir/openbmc-sensors.py" "$script"
fi
stable_installer=$install_dir/openbmc-sensors-install.sh
if [ "$src_dir/install-openbmc-sensors.sh" != "$stable_installer" ] &&
    [ -f "$src_dir/install-openbmc-sensors.sh" ]; then
    install -m 0755 "$src_dir/install-openbmc-sensors.sh" "$stable_installer"
fi
# Keep the assets the stable installer needs for a boot-time rerun. The
# source and destination are the same file when the installer is run from
# --install-dir, so skip the copy in that case.
for asset in openbmc-sensors.example.json openbmc-sensors.service.in; do
    if [ "$src_dir/$asset" != "$install_dir/$asset" ]; then
        install -m 0644 "$src_dir/$asset" "$install_dir/$asset"
    fi
done

if [ ! -e "$config" ]; then
    install -m 0600 "$src_dir/openbmc-sensors.example.json" "$config"
    echo "created $config; edit the nested redfish/ipmi credentials and sensor policy"
else
    echo "keeping existing $config"
fi

python=$(command -v python3)
mount_line=
if [ -n "$mount_dir" ]; then
    mount_line="RequiresMountsFor=$mount_dir"
fi
unit_tmp=$(mktemp)
trap 'rm -f "$unit_tmp"' EXIT
sed \
    -e "s|@SCRIPT@|$script|g" \
    -e "s|@CONFIG@|$config|g" \
    -e "s|@PYTHON@|$python|g" \
    -e "s|@MOUNT_LINE@|$mount_line|g" \
    "$src_dir/openbmc-sensors.service.in" > "$unit_tmp"
install -m 0644 "$unit_tmp" "$unit_dir/$service_name.service"

if [ "$skip_systemd" = 1 ]; then
    echo "installed $script"
    echo "installed $unit_dir/$service_name.service; --skip-systemd: not enabled"
    exit 0
fi

systemctl daemon-reload
systemctl enable "$service_name.service"

transport=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("transport","redfish"))' "$config" 2>/dev/null || echo redfish)
startable=$(python3 - "$config" <<'PY'
import json
import sys

try:
    with open(sys.argv[1], "r", encoding="utf-8") as f:
        config = json.load(f)
except Exception:
    print(0)
    raise SystemExit(0)
transport = config.get("transport", "redfish")
if transport == "ipmi-open":
    print(1)
    raise SystemExit(0)
section = config.get("ipmi" if transport == "ipmi-lanplus" else "redfish", {})
print(1 if section.get("password") else 0)
PY
)
if [ "$no_start" != 1 ] && [ "$startable" = 1 ]; then
    systemctl restart "$service_name.service"
    echo "started $service_name.service (transport=$transport)"
else
    echo "installed and enabled $service_name.service; not started"
    echo "after editing $config credentials: systemctl restart $service_name.service"
fi
