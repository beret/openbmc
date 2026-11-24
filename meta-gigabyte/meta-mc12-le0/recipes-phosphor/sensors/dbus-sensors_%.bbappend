# The host CPU is AMD: there is no PECI interface to talk to, so the Intel CPU
# sensor daemon can never start and only fills the journal with
#   PECI set dev names to /dev/peci-default, /dev/peci-0
#   error getting SpecialMode status: 'No route to host'
# CPU temperature arrives over SB-TSI and is published by hwmontempsensor.
PACKAGECONFIG:remove = " intelcpusensor"
