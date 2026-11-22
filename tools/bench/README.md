# Bench materiel BATMino

Projet ESP-IDF separe qui mesure la bande passante de la carte, puis se
reflash avec `main/mino.bin` du firmware.

    mkdir -p main && cp bench.c main/ && cp main_CMakeLists.txt main/CMakeLists.txt
    # CMakeLists.txt et partitions.csv a la racine du projet
    export IDF_TOOLS_PATH=/opt/espressif-tools && . /opt/esp-idf/export.sh
    idf.py build && idf.py -p /dev/ttyACM0 -b 460800 flash

Resultats (17/10/2026, CPU 160 MHz, PSRAM octal 80 MHz, -O2) :

    SRAM interne   memcpy 243 Mo/s | memset 486 Mo/s
    PSRAM 8 Mo     memcpy  21 Mo/s
    flash QIO 80   lecture 25 Mo/s hors cache
    SD secteur brut 20 MHz 1 bit : 2,34 Mo/s ecriture et lecture
    SD secteur brut 40 MHz 1 bit : 4,65 Mo/s ecriture et 4,70 Mo/s lecture
    SD via FATFS   20 MHz : 0,34 / 1,22 Mo/s  |  40 MHz : 0,37 / 1,60 Mo/s

Lecture : le bus SD est a son plafond theorique en 20 MHz 1 bit ; FATFS coute
un facteur ~7 en ecriture ; passer a 40 MHz double le debit brut.

Attention : flasher ce projet ecrase le firmware. Restaurer avec
`python -m esptool --chip esp32s3 -p /dev/ttyACM0 -b 460800 write_flash 0x10000 ../build/mino.bin`
