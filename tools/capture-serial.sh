#!/bin/bash
# capture-serial.sh [secondes] : capture le log de boot de la carte.
# Le port USB-Serial/JTAG ne renvoie rien sans stty ; un reset est necessaire
# pour voir le demarrage.
D=${1:-20}
stty -F /dev/ttyACM0 115200 raw -echo 2>/dev/null
( timeout "$D" cat /dev/ttyACM0 > /tmp/mino-boot.log 2>/dev/null & )
sleep 0.5
~/.hermes-usb/python/bin/python3 -m esptool --port /dev/ttyACM0 --after hard_reset --no-stub chip_id >/dev/null 2>&1
wait
grep -aE "historique|hist|Mino demarre|SD |partition" /tmp/mino-boot.log | head -20
echo "--- fin du log (${D}s) : /tmp/mino-boot.log"
