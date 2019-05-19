To recompile DTB:
$ git clone https://github.com/RobertCNelson/dtb-rebuilder
$ cd dtb-rebuilder
$ cp ../am57xx-beagle-x15-revc_extra_io.dts src/arm
$ make src/arm/am57xx-beagle-x15-revc_extra_io.dtb

To reverse engineer the full device tree source from compiled device tree blob:
$ dtc -I dtb -O dts -f src/arm/am57xx-beagle-x15-revc_extra_io.dtb -o reverse_engineered.dts
