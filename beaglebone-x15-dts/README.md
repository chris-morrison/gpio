# Beagleboard-X15 setup

## Table of Contents
- [Initial setup](#Initial-setup)
- [Customising the kernel](#Customising-the-kernel)
- [Build the device tree blob](#Build-the-device-tree-blob)
- [Boot via tftp](#Boot-via-tftp)
- [Committing changes to the SD card](#Committing-changes-to-the-SD-card)
- [bbx15 as a development platform](#bbx15-as-a-development-platform)

## Initial setup
The Beagleboard-X15 (bbx15) ships with an image loaded onto its internal flash memory - it will boot as a linux machine out of the box.  This guide sets up a different image on a micro SD card.
### Items needed
#### Hardware
* bbx15
* Power supply
* TTL to USB serial cable
* Micro SD card (16GB+)
* Micro SD card reader/writer
* Optional: Heatsink + fan

Refer to The [bbx15 quickstart guide](https://github.com/beagleboard/beagleboard-x15/blob/master/BeagleBoard-X15_Quick-Start-Guide.pdf) for recommendations on power supplies, USB-serial cables, etc.
#### Software
* [Latest bbx15 image](https://beagleboard.org/latest-images). This guide uses bbx15-debian-9.5-lxqt-armhf-2018-10-07-4gb.img.xz
* [Etcher](https://www.balena.io/etcher/) (or any other means of getting the bbx15 image onto the micro SD card).  This guide uses v1.5.35
* Putty
### Steps
* Use Etcher to write the bbx15 image to the SD card.
* Insert the SD card into the bbx15 and connect the USB-serial cable between the PC and bbx15
* From the PC, open a serial terminal (115200, 8N1) on this connection
* Power on the bbx15 - the system should boot, and present a login prompt
```
Debian GNU/Linux 9 BeagleBoard-X15 ttyS2
BeagleBoard.org Debian Image 2018-10-07
Support/FAQ: http://elinux.org/Beagleboard:BeagleBoneBlack_Debian
default username:password is [debian:temppwd]
BeagleBoard-X15 login:
```
* Log into the bbx15 (default login details are displayed above the prompt)
* **Optional (but recommended)** Change the password of both the `debian` and `root` accounts (note: initial `root` password is 'root')
```sh
debian@BeagleBoard-X15:~$ passwd
Changing password for debian.
(current) UNIX password:
Enter new UNIX password:
Retype new UNIX password:
passwd: password updated successfully
debian@BeagleBoard-X15:~$ su
Password:
root@BeagleBoard-X15:/home/debian# passwd
Enter new UNIX password:
Retype new UNIX password:
passwd: password updated successfully
root@BeagleBoard-X15:/home/debian#
```
* The bbx15 image only creates a 4GB partition - resize the partition to use the full amount of space available on the SD card.  Follow [this](https://elinux.org/Beagleboard%3AExpanding\_File\_System\_Partition\_On\_A\_microSD) guide
    * When it asks `partition #1 contains a ext4 signature. do you want to remove the signature?`, select "No"


## Customising the kernel
This step reconfigures/rebuilds the kernel, to add support for different features (in this case, increasing the number of serial ports and applying a patch to fix the PWM driver)
### (Extra) Items needed
#### Software
* Specifically a Linux PC (this guide uses Ubuntu 18.10)
### Steps
* Clone the kernel project https://github.com/RobertCNelson/ti-linux-kernel-dev/ and checkout the desired version.  This guide uses 4.14.71-ti-r80, since that is the one present in bbx15-debian-9.5-lxqt-armhf-2018-10-07-4gb.img.xz
```sh
cd ~/workspace
git clone https://github.com/RobertCNelson/ti-linux-kernel-dev/
cd ti-linux-kernel-dev
git checkout tags/4.14.71-ti-r80
```
* To fix an issue with the PWM driver in that version, apply [this](https://patchwork.ozlabs.org/patch/943148/
) patch:

```diff
diff --git a/drivers/pwm/pwm-omap-dmtimer.c b/drivers/pwm/pwm-omap-dmtimer.c
index 665da3c8fbceb..d3d7ea7a53146 100644
--- a/drivers/pwm/pwm-omap-dmtimer.c
+++ b/drivers/pwm/pwm-omap-dmtimer.c
@@ -264,8 +264,9 @@  static int pwm_omap_dmtimer_probe(struct platform_device *pdev)
  	timer_pdata = dev_get_platdata(&timer_pdev->dev);
 	if (!timer_pdata) {
-		dev_err(&pdev->dev, "dmtimer pdata structure NULL\n");
-		ret = -EINVAL;
+		dev_info(&pdev->dev,
+			 "dmtimer pdata structure NULL, deferring probe\n");
+		ret = -EPROBE_DEFER;
 		goto put;
 	}
```

* Optional: Edit `system.sh` (e.g. to use more cores to speed up compilation)
* Execute the `build_kernel.sh` script.  The first time it runs, it will likely prompt for the installation of dependencies.  Keep executing the script (and installing dependencies) until it succeeds.
* During the kernel configuration stage, increase the number of 8250 serial ports.  The resulting kernel config file will have these settings:
```sh
CONFIG_SERIAL_8250_NR_UARTS=12          # '12' is the number chosen in the config menu.  Anything >= 9 is ok for our purposes
CONFIG_SERIAL_8250_RUNTIME_UARTS=12
```
    * This is necessary, because the intent is to use 9 serial ports on the bbx15
* The resulting output will be put into `ti-linux-kernel-dev/deploy`

## Build the device tree blob
### Building
* Clone the device tree builder
```sh
cd ~/workspace
git clone https://github.com/RobertCNelson/dtb-rebuilder
cd dtb-rebuilder
```
* Create/edit the dts file, as desired, then compile the dtb.  E.g. Assume [am57xx-beagle-x15-revc\_extra\_io.dts](https://github.com/chris-morrison/gpio/blob/master/beaglebone-x15-dts/am57xx-beagle-x15-revc_extra_io.dts) has been copied to `dtb-rebuilder/src/arm/am57xx-beagle-x15-revc_extra_io.dts`
```sh
make src/arm/am57xx-beagle-x15-revc_extra_io.dtb
```
### Reverse engineering
* To reverse engineer from device tree blob to device tree source (useful for debugging/understanding)
```sh
dtc -I dtb -O dts -f src/arm/am57xx-beagle-x15-revc_extra_io.dtb -o src/arm/reverse_engineered.dts
```

## Boot via tftp
Test the new kernel and dtb before writing them to the SD card, by booting from TFTP.
### (Extra) Items needed
#### Hardware
* Ethernet connection to the bbx15
    * Connect the Ethernet to the highest/top RJ45 socket on the bbx15

#### Software
* tftp server

### Steps
* This assumes the following:
    * bbx15 IP is 192.168.0.16
    * PC/tftp server is 192.168.0.8
* Gather the necessary files in one folder on the PC:
```sh
mkdir ~/workspace/tftp
cd ~/worksapce/tftp
cp ~/workspace/dtb-rebuilder/src/arm/am57xx-beagle-x15-revc_extra_io.dtb .
cp ~/workspace/ti-linux-kernel-dev/deploy/4.14.71-ti-r80.zImage .
scp debian@192.168.0.8:/boot/initrd.img-4.14.71-ti-r80 . # cheat by grabbing the existing intrd from the bbx15
```
* Turn the ramdisk image into a format that can be used by uboot
```sh
mkimage -n 'Ramdisk Image'  -A arm -O linux -T ramdisk -C gzip -d initrd.img-4.14.71-ti-r80 initramfs.uImage
```
* Start the tftp server, pointing to the `~/workspace/tftp` folder
* Reboot the bbx15 and in the serial terminal, press `SPACE` to stop the auto-boot process:
```
U-Boot 2017.01-00360-gc604741cb3 (Aug 11 2017 - 15:47:09 -0500), Build: jenkins-github_Bootloader-Builder-592
CPU  : DRA752-GP ES2.0
Model: TI AM5728 BeagleBoard-X15
Board: BeagleBoard X15 REV C.00
DRAM:  2 GiB
MMC:   OMAP SD/MMC: 0, OMAP SD/MMC: 1
** Unable to use mmc 0:1 for loading the env **
Using default environment
setup_board_eeprom_env: beagle_x15_revc
SCSI:  SATA link 0 timeout.
AHCI 0001.0300 32 slots 1 ports 3 Gbps 0x1 impl SATA mode
flags: 64bit ncq stag pm led clo only pmp pio slum part ccc apst
scanning bus for devices...
Found 0 device(s).
Net:   <ethaddr> not set. Validating first E-fuse MAC
cpsw
Press SPACE to abort autoboot in 2 seconds
=>                                                                    # now at uboot prompt, after pressing space
```
* Copy the following commands, and paste them into the serial terminal in one action:
```
setenv autoload no
setenv pxefile_addr_r '0x50000000'
setenv kernel_addr_r '0x82000000'
setenv initrd_addr_r '0x88080000'
setenv fdt_addr_r '0x88000000'
setenv initrd_high '0xffffffff'
setenv fdt_high '0xffffffff'
setenv loadkernel 'tftp ${kernel_addr_r} 4.14.71-ti-r80.zImage'
setenv loadinitrd 'tftp ${initrd_addr_r} initramfs.uImage; setenv initrd_size ${filesize}'
setenv loadfdt 'tftp ${fdt_addr_r} am57xx-beagle-x15-revc_extra_io.dtb'
setenv bootargs 'console=ttyO2,115200n8 loglevel=7 clocksource="gp_timer" root=/dev/mmcblk0p1 ro rootfstype=ext4 rootwait'
setenv bootcmd 'dhcp; setenv serverip 192.168.0.8; run loadkernel; run loadinitrd; run loadfdt; bootz ${kernel_addr_r} ${initrd_addr_r}:${initrd_size} ${fdt_addr_r}'
boot
```
* The bbx15 will now boot via tftp.  Perform any checks to confirm that the kernel/dtb changes have been successful

## Committing changes to the SD card
To make the changes permanent:
#### Kernel
* From the PC
```sh
scp ~/workspace/ti-linux-kernel-dev/deploy/4.14.71-ti-r80.zImage debian@192.168.0.8:~/
```
* From the bbx15
```sh
su
cp /boot/4.14.71-ti-r80.zImage /boot/4.14.71-ti-r80.zImage.orig  # optional - keep a copy of the current kernel
mv /home/debian/4.14.71-ti-r80.zImage /boot/vmlinuz-4.14.71-ti-r80
```
#### DTB
* From the PC
```sh
scp ~/workspace/dtb-rebuilder/src/arm/am57xx-beagle-x15-revc_extra_io.dtb debian@192.168.0.8:~/
```
* From the bbx15
```sh
su
mv /home/debian/am57xx-beagle-x15-revc_extra_io.dtb /boot/dtbs/4.14.71-ti-r80/
```
Update the `/boot/uEnv.txt` file:
```sh
Change
 #dtb=
To
dtb=am57xx-beagle-x15-revc_extra_io.dtb
```

## bbx15 as a development platform
The bbx15 is a linux computer in its own right, and can be used to compile its own software. 

(Aside: it can even be used to recompile the kernel/dtb, but it's much slower than a PC and has thermal issues under heavy workload, if not using an active cooling solution)

* Install linux headers (useful for kernel module development)
```sh
apt-get install linux-headers-$(uname -r)
```
* Install devmem2 (useful for poking registers)
```sh
wget http://free-electrons.com/pub/mirror/devmem2.c
gcc -o devmem2 devmem2.c
mv devmem2 /usr/bin
```


