# 📌 跨架构交叉编译 OpenSSL 汇编冲突与 no-asm 纯 C 兼容方案

> **文档元数据 (Metadata)**:
> - **记录时间 / Timestamp**: 2026-10-09 13:30:00 (UTC+8)
> - **作者 / Maintainer**: kxjhwbb
> - **涉及平台 / Platforms**: Linux MIPS / ARM / RISC-V / Windows
> - **标签 / Tags**: #OpenSSL #CrossCompile #Assembly #no-asm
> - **核心概述 / Summary**: 交叉编译 OpenSSL 时各架构汇编实现易与编译器 ABI 发生冲突，配置 `no-asm` 使用高移植性纯 C 算法可彻底解决兼容问题。

---

## 🔴 1. 踩坑现象 (Problem Symptom)
在为 ARM32 (`armhf`/`armel`) 或 MIPS (`mipsel`/`mips`) 交叉编译 OpenSSL 静态库时，汇编器报错：
```text
crypto/sha/sha256-armv4.S: Assembler messages:
crypto/sha/sha256-armv4.S:145: Error: instruction not supported in ARM code -- `vld1.8 {d0,d1},[r0]!`
crypto/bn/bn-mips.S: Assembler messages:
crypto/bn/bn-mips.S:42: Error: opcode not supported on this processor: mips32 (mips32) `dmultu $4,$5'
```

---

## 🔍 2. 根本原因 (Root Cause Analysis)
1. **指令集细分版本冲突**：
   - OpenSSL 内部针对 SIMD/向量指令（如 ARM NEON/VFP、MIPS 64/Release 2）编写了硬编码汇编代码。
   - 交叉编译器工具链的默认 ABI（如 ARM softfp vs hardfp，ARMv5TE vs ARMv7-A，MIPS r2 vs r6）与 OpenSSL 默认选取的汇编文件指令集不兼容，导致汇编阶段直接报错。

---

## ✅ 3. 解决方案 (Solution & Best Practices)
在执行 OpenSSL 的 `./Configure` 时，显式传入 **`no-asm`** 标志，强制关闭架构特定汇编代码，采用标准 ANSI C 算法实现：

```bash
./Configure <TARGET> \
  --cross-compile-prefix=<PREFIX>- \
  no-shared no-tests no-docs no-legacy no-engine no-ui-console no-asm \
  --prefix=$GITHUB_WORKSPACE/openssl-target \
  --openssldir=$GITHUB_WORKSPACE/openssl-target

make -j$(nproc) build_libs
make install_dev
```

### 平台目标对照表：
| 架构名称 | 编译器前缀 | OpenSSL Target |
| :--- | :--- | :--- |
| **aarch64** | `aarch64-linux-gnu-` | `linux-aarch64` |
| **armhf** | `arm-linux-gnueabihf-` | `linux-armv4` |
| **armel** | `arm-linux-gnueabi-` | `linux-armv4` |
| **mipsel** | `mipsel-linux-gnu-` | `linux-mips32` |
| **mips** | `mips-linux-gnu-` | `linux-mips32` |
| **riscv64** | `riscv64-linux-gnu-` | `linux64-riscv64` |
| **i686** | `i686-linux-gnu-` | `linux-x86` |
| **Windows x86_64** | `x86_64-w64-mingw32-` | `mingw64` |
| **Windows ARM64** | `aarch64-w64-mingw32-` | `mingw64` |
