
# ATF and u-boot for philips-hy3000

- https://cmi.hanwckf.top/p/mt798x-uboot-usage

![](/u-boot.gif)

## Prepare

```
sudo apt install gcc-aarch64-linux-gnu build-essential flex bison libssl-dev device-tree-compiler qemu-user-static python2.7
```

## Build

```bash
chmod +x build.sh
SOC=mt7981 BOARD=philips_hy3000 VERSION=2022 ./build.sh
```

> VERSION=2022/2023

Generate file will be in `output`

## Generate GPT with python2.7

```bash
chmod +x generate_gpt.sh
./generate_gpt.sh
```

Generate file will be in `output_gpt`

## Use Action to build

- [x] Build FIP
- [ ] Build GPT (Optional)
