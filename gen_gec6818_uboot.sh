#!/bin/bash

uboot_raw_bin_path="/home/dengxh/data/u-boot_gec6818/u-boot.bin"
out_file_name="boot.bin"
nsih_name="drone-sd-64.txt"
bl1_name="bl1-drone.bin"
bootloader_name="u-boot.bin"

cp ../bl1-artik710/out/${bl1_name} ./
cp ../u-boot_nanopi3/${bootloader_name} ./

./mk6818 ${out_file_name} ${nsih_name} ${bl1_name} ${uboot_raw_bin_path} 1
echo "done!"
