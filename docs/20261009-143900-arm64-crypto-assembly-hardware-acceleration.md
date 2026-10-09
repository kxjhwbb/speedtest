# 📌 ARM64 硬件加密扩展 (ARMv8 Crypto) 汇编加速与 300M 性能瓶颈陷阱

> **文档元数据 (Metadata)**:
> - **记录时间 / Timestamp**: 2026-10-09 14:39:00 (UTC+8)
> - **作者 / Maintainer**: kxjhwbb
> - **涉及平台 / Platforms**: Linux ARM64 (aarch64), ARM 路由器, 树莓派
> - **标签 / Tags**: #ARM64 #ARMv8Crypto #AES-NI #Performance #OpenSSL #Assembly
> - **核心概述 / Summary**: 在 ARM64 交叉编译 OpenSSL 时若全局传入 `no-asm`，会导致 TLS/AES 解密退化为纯 CPU 软件模拟，万兆/千兆网络下测速受限在 300Mbps；启用 ARMv8 汇编与 `-march=armv8-a+crypto+crc` 即可满速跑至 900M+。

---

## 🔴 1. 踩坑现象 (Problem Symptom)
- 本地使用 GCC 编译的 `bb-speedtest-aarch64` 在 ARM64 路由器（如 MT7981 / Cudy）上测速可以轻松达到 **900+ Mbps**。
- 但从 GitHub Actions Release 下载的 `bb-speedtest-linux-aarch64` 二进制文件，在同一台设备同一线路上测速**只能跑到约 300 Mbps**，CPU 单核占用率直接达到 100%。

---

## 🔍 2. 根本原因 (Root Cause Analysis)
1. **全局 `no-asm` 误伤 ARM64 硬件指令**：
   - 此前为了解决 32 位 MIPS/ARM 的汇编 ABI 冲突，在 CI 中全局配置了 OpenSSL 的 `no-asm` 参数。
   - 这导致为 `aarch64` 编译 OpenSSL 静态库时，**彻底关闭了 `aesv8-armx.S`、`ghashv8-armx.S` 和 NEON 向量汇编优化**。
2. **纯软件模拟 vs 硬件指令集**：
   - HTTPS 测速过程涉及海量数据的 AES-GCM 解密。
   - 没有汇编加速时，OpenSSL 只能使用纯 C 语言逐字节移位计算，单核处理能力上限仅在 300Mbps 左右；
   - 启用 ARMv8 Crypto 硬件指令（`AESE`/`AESD`/`PMULL`）时，芯片内部专有硬件加密协处理器以近乎内存总线速度处理数据，吞吐量提升 3~5 倍以上。

---

## ✅ 3. 解决方案 (Solution & Best Practices)

1. **精细化 OpenSSL 编译参数矩阵**：
   - 仅对 32 位小众架构开启 `no-asm`。
   - 对 **`aarch64` 明确保留默认汇编支持（不加 `no-asm`）**，OpenSSL 自带运行期 CPU 特征探测，完美兼容所有 ARM64 CPU。
2. **启用 ARMv8 硬件扩展编译参数**：
   - 为 aarch64 显式开启 `-march=armv8-a+crypto+crc`：
   ```yaml
   - arch: aarch64
     target: aarch64-linux-gnu
     cross_pkg: gcc-aarch64-linux-gnu
     openssl_target: linux-aarch64
     openssl_asm_flag: ""
     cflags: "-O3 -flto -fomit-frame-pointer -march=armv8-a+crypto+crc"
   ```

---

## 🧪 4. 验证方式 (Verification)
检查编译后的 `libcrypto.a` 或可执行文件，确认包含 `aesv8_set_encrypt_key` 等硬件加密符号：
```bash
$ aarch64-linux-gnu-nm bb-speedtest-linux-aarch64 | grep aesv8
0000000000451a40 T aesv8_set_encrypt_key
0000000000451800 T aesv8_encrypt
```
在路由器实测，下载测速瞬间压满千兆（900M+ Mbps），CPU 占用率低于 15%。
