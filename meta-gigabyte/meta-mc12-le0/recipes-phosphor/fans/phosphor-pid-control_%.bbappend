FILESEXTRAPATHS:append := "${THISDIR}/${PN}:"
SRC_URI:append = " file://config.json file://mc12-fan-init.service"
PACKAGECONFIG:append = " offline-failsafe handle-missing-object-paths"
SYSTEMD_SERVICE:${PN}:append = " mc12-fan-init.service"

FILES:${PN} = "${datadir}/swampd/config.json /usr/share/swampd/* /usr/bin/swampd /usr/bin/setsensor ${systemd_system_unitdir}/mc12-fan-init.service"

do_install:append(){
  install -d ${D}${datadir}/swampd
  install -m 0644 -D ${UNPACKDIR}/config.json ${D}${datadir}/swampd/
  install -m 0644 -D ${UNPACKDIR}/mc12-fan-init.service ${D}${systemd_system_unitdir}/mc12-fan-init.service
}
