FILESEXTRAPATHS:append := "${THISDIR}/${PN}:"
SRC_URI:append = " file://config.json file://10-wait-cpu-sensor.conf"
PACKAGECONFIG:append = " offline-failsafe handle-missing-object-paths"

FILES:${PN} = "${datadir}/swampd/config.json /usr/share/swampd/* /usr/bin/swampd /usr/bin/setsensor ${systemd_system_unitdir}/phosphor-pid-control.service.d/10-wait-cpu-sensor.conf"

do_install:append(){
  install -d ${D}${datadir}/swampd
  install -m 0644 -D ${UNPACKDIR}/config.json ${D}${datadir}/swampd/
  install -d ${D}${systemd_system_unitdir}/phosphor-pid-control.service.d
  install -m 0644 -D ${UNPACKDIR}/10-wait-cpu-sensor.conf ${D}${systemd_system_unitdir}/phosphor-pid-control.service.d/
}
