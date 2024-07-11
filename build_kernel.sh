#!/bin/bash

export ARCH=arm
export CROSS_COMPILE=$(pwd)/../PLATFORM/prebuilts/gcc/linux-x86/arm/arm-eabi-4.8/bin/arm-eabi-
mkdir output

make -C $(pwd) O=output clean && make -C $(pwd) O=output mrproper
make -C $(pwd) O=output VARIANT_DEFCONFIG=msm8916_sec_j5xlte_eur_defconfig msm8916_sec_defconfig SELINUX_DEFCONFIG=selinux_defconfig SELINUX_LOG_DEFCONFIG=selinux_log_defconfig TIMA_DEFCONFIG=tima_defconfig
#make -C $(pwd) O=output menuconfig
make -C $(pwd) O=output -j$(nproc --all)

tools/dtbTool -s 2048 -o output/arch/arm/boot/dt.img -p output/scripts/dtc/ output/arch/arm/boot/dts/ -v
#cp output/arch/arm/boot/Image $(pwd)/arch/arm/boot/zImage

