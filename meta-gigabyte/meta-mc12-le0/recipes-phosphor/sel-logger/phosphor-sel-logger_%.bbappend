# Threshold alarms and watchdog timeouts are the SEL sources on this board.
# log-alarm duplicates the threshold path, so leave it off.
PACKAGECONFIG:append = " log-threshold log-watchdog"
