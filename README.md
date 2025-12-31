# u-boot-mtk-2025 for mt7621

From 1715173329's [bl-mt798x-oss](https://github.com/1715173329/bl-mt798x-oss/tree/mt7621)

**NOT TESTED YET**

You **MUST** test it by yourself, and **have risk of BRICK**!

## Get Source

```bash
git clone --no-checkout <repository_url>
cd <repository_directory>
git sparse-checkout init --cone
git sparse-checkout set <UBOOT_DIR>
git checkout mt7621
```

> UBOOT_DIR: uboot-mtk-20230718-09eda825/uboot-mtk-20250711

## Prepare

1. Install dependencies:

    ```bash
    sudo apt install gcc-aarch64-linux-gnu build-essential flex bison libssl-dev device-tree-compiler qemu-user-static
    ```

2. Get toolchain:

   ```bash
   wget -O - https://github.com/DragonBluep/uboot-mt7621/releases/download/20230517/openwrt-toolchain-ramips-mt7621_gcc-12.3.0_musl.Linux-x86_64.tar.xz | tar --xz -xf -
   ```

   > The toolchain directory and U-Boot directory should be in the same directory

## Build

```bash
chmod +x build.sh
SOC=mt7621 BOARD=rfb VERSION=2025 ./build.sh
```

> BOARD: rfb/nand_rfb/ax_rfb/nand_ax_rfb

> VERSION: 2023/2025
