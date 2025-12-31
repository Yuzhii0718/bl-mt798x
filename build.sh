#!/bin/sh

# 查找并设置工具链路径
TOOLCHAIN_BIN=$(cd ./openwrt*/toolchain-mipsel*/bin 2>/dev/null; pwd)
if [ -z "$TOOLCHAIN_BIN" ]; then
	echo "Error:  Toolchain not found!  Please check openwrt*/toolchain-mipsel*/ exists."
	exit 1
fi

TOOLCHAIN="${TOOLCHAIN_BIN}/mipsel-openwrt-linux-"
Staging="${TOOLCHAIN_BIN%/bin}"
Staging="${Staging%/toolchain-*}"

if [ "$VERSION" = "2023" ]; then
    UBOOT_DIR=uboot-mtk-20230718-09eda825
elif [ "$VERSION" = "2025" ]; then
    UBOOT_DIR=uboot-mtk-20250711
else
    echo "Error: Unsupported VERSION. Please specify VERSION=2023/2025."
    exit 1
fi

echo "CROSS_COMPILE=${TOOLCHAIN}"
echo "STAGING_DIR=${Staging}"

# 检查必需参数
if [ -z "$SOC" ] || [ -z "$BOARD" ]; then
	echo "Usage: SOC=mt7621 BOARD=<board name> VERSION=2025 $0"
	echo "eg: SOC=mt7621 BOARD=nmbm_rfb VERSION=2025 $0"
	exit 1
fi

# Check if Python is installed on the system
command -v python3 >/dev/null 2>&1
if [ "$?" != "0" ]; then
	echo "Error: Python is not installed on this system."
	exit 1
fi

echo "Trying cross compiler..."
command -v "${TOOLCHAIN}gcc" >/dev/null 2>&1
if [ "$?" != "0" ]; then
	echo "Error: ${TOOLCHAIN}gcc not found!"
	exit 1
fi

ATF_CFG="${SOC}_${BOARD}_defconfig"
UBOOT_CFG="${SOC}_${BOARD}_defconfig"

if grep -Eq "CONFIG_FLASH_DEVICE_EMMC=y|_BOOT_DEVICE_EMMC=y" $ATF_DIR/configs/$ATF_CFG ; then
	# No fixed-mtdparts or multilayout for EMMC
	fixedparts=0
	multilayout=0
else
	# Build fixed-mtdparts by default for NAND
	fixedparts=${FIXED_MTDPARTS:-1}
	multilayout=${MULTI_LAYOUT:-0}
	if [ "$multilayout" = "1" ]; then
		UBOOT_CFG="${SOC}_${BOARD}_multi_layout_defconfig"
	fi
fi

echo "Building for:  ${SOC}_${BOARD}, fixed-mtdparts: $fixedparts, multi-layout: $multilayout"
echo "u-boot dir: $UBOOT_DIR"

# 检查 U-Boot 目录是否存在
if [ ! -d "$UBOOT_DIR" ]; then
	echo "Error: U-Boot directory '$UBOOT_DIR' not found!"
	exit 1
fi

# 检查配置文件是否存在
if [ ! -f "$UBOOT_DIR/configs/$UBOOT_CFG" ]; then
	echo "Error: U-Boot config '$UBOOT_CFG' not found in $UBOOT_DIR/configs/"
	exit 1
fi

echo "Build u-boot..."
rm -f "$UBOOT_DIR/u-boot.bin"
cp -f "$UBOOT_DIR/configs/$UBOOT_CFG" "$UBOOT_DIR/.config"

if [ "$fixedparts" = "1" ]; then
	echo "Build u-boot with fixed-mtdparts!"
	echo "CONFIG_MEDIATEK_UBI_FIXED_MTDPARTS=y" >> "$UBOOT_DIR/.config"
	echo "CONFIG_MTK_FIXED_MTD_MTDPARTS=y" >> "$UBOOT_DIR/.config"
fi

make -C "$UBOOT_DIR" olddefconfig
make -C "$UBOOT_DIR" clean
make -C "$UBOOT_DIR" CROSS_COMPILE="${TOOLCHAIN}" STAGING_DIR="${Staging}" -j $(nproc) all

if [ -f "$UBOOT_DIR/u-boot.bin" ]; then
	echo "u-boot build done!"
else
	echo "u-boot build fail!"
	exit 1
fi

# 输出最终文件
mkdir -p "output"
if [ -f "$UBOOT_DIR/u-boot.bin" ]; then
	cp -f "$UBOOT_DIR/u-boot.bin" "output/$SOC-u-boot-$BOARD-${VERSION}.bin"
	echo "$SOC-u-boot-$BOARD-${VERSION} build done"
	echo "Output:  output/$SOC-u-boot-$BOARD-${VERSION}.bin"
else
	echo "$SOC-uboot-$BOARD-${VERSION} build fail!"
	exit 1
fi