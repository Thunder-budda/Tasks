# Luckfox Pico Mini RT 内核配置

在 Luckfox Pico Mini 的原厂 SDK 基础上，构建 `PREEMPT_RT` 内核镜像具体步骤。

## 1. 目标板级配置

板级配置文件：

```bash
project/cfg/BoardConfig_IPC/BoardConfig-SD_CARD-Buildroot-RV1103_Luckfox_Pico_Mini-IPC.mk
```

内核 defconfig：

```bash
sysdrv/source/kernel/arch/arm/configs/luckfox_rv1106_linux_defconfig
```

## 2. 需要修改的文件

### 2.1 修改 `arch/arm/Kconfig`

文件：

```bash
sysdrv/source/kernel/arch/arm/Kconfig
```

需要加入/修改的关键项：

```diff
+ select ARCH_SUPPORTS_RT if HAVE_POSIX_CPU_TIMERS_TASK_WORK
- select HAVE_ARCH_JUMP_LABEL if !XIP_KERNEL && !CPU_ENDIAN_BE32 && MMU
+ select HAVE_ARCH_JUMP_LABEL if !XIP_KERNEL && !CPU_ENDIAN_BE32 && MMU && !PREEMPT_RT
+ select HAVE_PREEMPT_LAZY
+ select HAVE_POSIX_CPU_TIMERS_TASK_WORK if !KVM
+ select KMAP_LOCAL
```

作用：

- 允许 ARM32 选择 `CONFIG_PREEMPT_RT`
- RT 下关闭 `JUMP_LABEL`
- 补齐 `PREEMPT_RT` 所需依赖

### 2.2 修改 `rockchip-cpufreq.c`

文件：

```bash
sysdrv/source/kernel/drivers/cpufreq/rockchip-cpufreq.c
```

修改：

```diff
- if (disable) {
+ if (IS_ENABLED(CONFIG_SMP) && disable) {
```

作用：

- RV1103 单核配置下，避免 `wake_up_if_idle` 链接失败

### 2.3 修改内核 defconfig

文件：

```bash
sysdrv/source/kernel/arch/arm/configs/luckfox_rv1106_linux_defconfig
```

关键配置应变为：

```config
CONFIG_EXPERT=y
# CONFIG_PREEMPT_NONE is not set
# CONFIG_PREEMPT_VOLUNTARY is not set
# CONFIG_PREEMPT is not set
CONFIG_PREEMPT_RT=y
CONFIG_HIGH_RES_TIMERS=y
# CONFIG_HZ_100 is not set
CONFIG_HZ_1000=y
CONFIG_HZ=1000
CONFIG_IKCONFIG=y
CONFIG_IKCONFIG_PROC=y
```

## 3. 生成 RT 内核配置

```bash
cd /home/kamchio/libs/luckfox-pico

env PATH=/home/kamchio/libs/luckfox-pico/tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf/bin:$PATH \
make O=/home/kamchio/libs/luckfox-pico/sysdrv/source/objs_kernel_rt \
-C /home/kamchio/libs/luckfox-pico/sysdrv/source/kernel \
ARCH=arm \
CROSS_COMPILE=arm-rockchip830-linux-uclibcgnueabihf- \
luckfox_rv1106_linux_defconfig olddefconfig
```

验证：

```bash
grep -E 'CONFIG_PREEMPT_RT|CONFIG_PREEMPT_NONE|CONFIG_HZ_1000|CONFIG_HZ=|CONFIG_HIGH_RES_TIMERS|CONFIG_IKCONFIG_PROC' \
/home/kamchio/libs/luckfox-pico/sysdrv/source/objs_kernel_rt/.config
```

应该要输出：

```text
CONFIG_HIGH_RES_TIMERS=y
# CONFIG_PREEMPT_NONE is not set
CONFIG_PREEMPT_RT=y
CONFIG_IKCONFIG_PROC=y
CONFIG_HZ_1000=y
CONFIG_HZ=1000
```

## 4. 构建 RT 固件镜像

为避免 `mkimage` 走到别的 SDK 工具，构建时把 Luckfox 自带工具放到 PATH 前面：

```bash
cd /home/kamchio/libs/luckfox-pico

env PATH=/home/kamchio/libs/luckfox-pico/sysdrv/tools/pc/uboot_tools:/home/kamchio/libs/luckfox-pico/sysdrv/source/uboot/rkbin/tools:/home/kamchio/libs/luckfox-pico/tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf/bin:$PATH \
./build.sh all
```

这一步会同时重建：

- u-boot
- RT kernel
- rootfs
- kernel modules
- `sd_update.img`

这样可以避免只换 boot 后出现 `.ko` 的 `vermagic` 与 `preempt_rt` 不匹配的问题。

## 5. 保留原厂镜像并另存 RT 镜像

原厂镜像先备份：

```bash
cp /home/kamchio/libs/luckfox-pico/output/image/boot.img /home/kamchio/libs/luckfox-pico/output/image/boot-stock.img
cp /home/kamchio/libs/luckfox-pico/output/image/sd_update.img /home/kamchio/libs/luckfox-pico/output/image/sd_update-stock.img
```

RT 构建完成后另存：

```bash
cp /home/kamchio/libs/luckfox-pico/output/image/boot.img /home/kamchio/libs/luckfox-pico/output/image/boot-rt.img
cp /home/kamchio/libs/luckfox-pico/output/image/sd_update.img /home/kamchio/libs/luckfox-pico/output/image/sd_update-rt.img
```

## 6. 替换 SD 卡 boot 区

Luckfox SD 镜像的 boot 分区不是标准 `/dev/sdb1` 这种分区节点方式，而是固定偏移布局：

```text
env      32K
idblock  512K
uboot    256K
boot 起始偏移 = 32K + 512K + 256K = 800K = 819200 bytes
819200 / 512 = 1600
```

因此替换 boot 区时，写入目标是整盘 `/dev/sdb` 的固定 offset，不是 `/dev/sdb1`。

先卸载：

```bash
udisksctl unmount -b /dev/sdb1
```

写入 RT boot：

```bash
cd /home/kamchio/libs/luckfox-pico

sudo dd if=output/image/boot-rt.img of=/dev/sdb bs=512 seek=1600 conv=notrunc,fsync status=progress
sync
```

如果要整卡替换为 RT 镜像：

```bash
cd /home/kamchio/libs/luckfox-pico

sudo dd if=output/image/sd_update-rt.img of=/dev/sdb bs=4M conv=fsync status=progress
sync
```

## 7. 启动后验证

```bash
adb shell 'uname -a; zcat /proc/config.gz 2>/dev/null | grep -E "CONFIG_PREEMPT_RT|CONFIG_PREEMPT_NONE|CONFIG_HZ=|CONFIG_HZ_1000|CONFIG_HIGH_RES_TIMERS|CONFIG_IKCONFIG_PROC"'
```

成功会看到看到：

```text
PREEMPT_RT
CONFIG_PREEMPT_RT=y
CONFIG_HZ_1000=y
CONFIG_HZ=1000
```
