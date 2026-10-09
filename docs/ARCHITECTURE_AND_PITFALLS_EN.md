# 📘 bb-speedtest Architecture & Cross-Platform Pitfalls Guide

[**中文文档**](ARCHITECTURE_AND_PITFALLS.md) | [**English Guide**](ARCHITECTURE_AND_PITFALLS_EN.md)

> **Document Metadata**:
> - **Maintainer**: kxjhwbb
> - **Timestamp**: 2026-10-09 14:18:00 (UTC+8)
> - **Version**: v1.0.8
> - **Repository**: https://github.com/kxjhwbb/speedtest
>
> **Purpose**: This document records key architecture decisions, low-level network tuning techniques, and cross-platform compilation pitfalls and solutions encountered while building `bb-speedtest` (a zero-dependency, high-performance, statically-linked C Speedtest CLI tool).

---

## Table of Contents
1. [Embedded Router Environment & Dynamic Linking Pitfalls](#1-embedded-router-environment--dynamic-linking-pitfalls)
2. [Missing 64-bit Atomics in 32-bit Architectures](#2-missing-64-bit-atomics-in-32-bit-architectures)
3. [Cross-Architecture OpenSSL Static Compilation & Assembly Conflicts](#3-cross-architecture-openssl-static-compilation--assembly-conflicts)
4. [GitHub Actions CI/CD Pitfalls & Best Practices](#4-github-actions-cicd-pitfalls--best-practices)
5. [High-Performance Network Engine Optimization Rules](#5-high-performance-network-engine-optimization-rules)
6. [Windows & Windows ARM64 Cross-Compilation & Winsock Compatibility](#6-windows--windows-arm64-cross-compilation--winsock-compatibility)

---

## 1. Embedded Router Environment & Dynamic Linking Pitfalls

### 🔴 Problem
When running binaries compiled on Ubuntu/Debian directly on embedded routers (OpenWrt, Padavan, Cudy, etc.), even with execution permissions granted (`chmod +x`), the system throws:
```bash
zsh: no such file or directory: ./bb-speedtest
# or
sh: ./bb-speedtest: not found
```

### 🔍 Root Cause
- Most embedded router systems run on **musl libc**, whereas standard Linux toolchains dynamically link against **glibc**. The ELF binary header hardcodes dynamic loader paths like `/lib/ld-linux-aarch64.so.1`.
- The router only contains `/lib/ld-musl-aarch64.so.1`. When the kernel loads the ELF file and fails to find the specified dynamic linker, it returns `ENOENT` (`No such file or directory`).

### ✅ Solution
- **Fully Static Linking**: Enforce `-static` during compilation.
- Bundle the C runtime, OpenSSL cryptographic engine, and thread libraries directly into a single self-contained binary.
- Verify using `file bb-speedtest`: the output must state `statically linked` and must not contain any `interpreter` reference.

---

## 2. Missing 64-bit Atomics in 32-bit Architectures

### 🔴 Problem
When compiling for 32-bit MIPS (`mipsel`/`mips`) and 32-bit ARM (`armhf`/`armel`), linker errors occur with undefined atomic symbols:
```text
undefined reference to '__atomic_fetch_add_8'
undefined reference to '__atomic_store_8'
undefined reference to '__atomic_load_8'
undefined reference to '__atomic_is_lock_free'
collect2: error: ld returned 1 exit status
```

### 🔍 Root Cause
- 32-bit hardware instruction sets (e.g. MIPS 24Kc, ARMv7 Cortex-A7) only natively support 32-bit atomic instructions. They cannot atomically manipulate 64-bit integers (`uint64_t`) in a single hardware instruction cycle.
- When atomic 64-bit variables (such as `atomic_uint_fast64_t g_total_bytes`) are used in C11 code or inside OpenSSL 3.x, GCC generates calls to compiler helper functions ending with `_8` (denoting 8 bytes / 64 bits).
- These functions reside in `libatomic.a`, which is not automatically linked by default in cross-toolchains.

### ✅ Solution
- Explicitly link `-latomic` at the end of the linker command line:
  ```bash
  $(CC) speedtest.c -o bb-speedtest -static -lssl -lcrypto -latomic -lpthread -ldl
  ```

---

## 3. Cross-Architecture OpenSSL Static Compilation & Assembly Conflicts

### 🔴 Problem
Cross-compiling OpenSSL directly across different toolchains produces assembly syntax errors, ABI mismatches (e.g. `VFP/NEON`), or `instruction not supported` errors on MIPS/ARM targets.

### 🔍 Root Cause
- OpenSSL includes handwritten assembly optimized for specific hardware variants (e.g. ARMv7-A NEON, MIPS Release 2).
- Different cross-compiler toolchains use varying default floating-point ABIs and instruction sets (e.g. MIPS r2 vs r6, ARM softfp vs hardfp), causing assembly-level instruction incompatibilities.

### ✅ Solution
- Pass **`no-asm`** to OpenSSL's `./Configure` to disable platform-specific assembly and compile clean, highly portable pure C implementations:
  ```bash
  ./Configure <target> no-shared no-tests no-docs no-legacy no-engine no-ui-console no-asm
  ```
- Target mappings:
  - `aarch64` ➜ `linux-aarch64`
  - `armhf` / `armel` ➜ `linux-armv4`
  - `mipsel` / `mips` ➜ `linux-mips32`
  - `riscv64` ➜ `linux64-riscv64`
  - `i686` ➜ `linux-x86`

---

## 4. GitHub Actions CI/CD Pitfalls & Best Practices

### 🔴 Pitfall 1: `macos-13` Runner Stalls in Infinite Waiting Queue
- Symptom: Workflow hangs on `Waiting for a runner to pick up this job...` for 30+ minutes.
- Root Cause: GitHub has sunset/deprecated Intel x86 `macos-13` runners, leading to severe resource starvation.
- Solution: Migrate to official Apple Silicon **`macos-latest`** (M-series runners, allocated instantaneously).

### 🔴 Pitfall 2: Duplicate Workflow Runs on Single Push
- Symptom: Pushing a branch and a tag simultaneously triggers two identical workflow pipelines.
- Root Cause: Workflow configured with both `on: push: branches: [main]` and `on: push: tags: ['v*']`.
- Solution: Retain only `tags: ['v*']` and `workflow_dispatch` (manual trigger) for release workflows.

### 🔴 Pitfall 3: macOS Binary Only ~60KB & Fails on Other Macs with `dyld: Library not loaded`
- Symptom: `bb-speedtest-darwin-arm64` binary is tiny (~60KB) and crashes on machines without Homebrew OpenSSL.
- Root Cause: Using `-L"${OPENSSL_DIR}/lib" -lssl -lcrypto` on macOS causes Clang to dynamically link to Homebrew's `.dylib` libraries instead of embedding the static code.
- Solution: Pass the exact paths of static archive files directly to Clang:
  ```bash
  clang speedtest.c "${OPENSSL_DIR}/lib/libssl.a" "${OPENSSL_DIR}/lib/libcrypto.a" -o bb-speedtest-darwin-arm64 -lpthread -lz
  ```
  This produces a self-contained ~3.5MB - 4.5MB binary that runs out of the box on any Apple Silicon Mac without external dependencies.

---

## 5. High-Performance Network Engine Optimization Rules

When benchmarking at 2.5Gbps / 10Gbps line rates or on resource-constrained ARM routers, the bottleneck shifts from network bandwidth to **system call overhead and kernel buffer contention**.

### 🚀 Optimization Highlights:
1. **Zero Single-Byte HTTP Header Reads**:
   - Instead of reading 1 byte in a loop to match `\r\n\r\n` (which triggers 500+ system calls per connection), read in 128KB chunks, locate the header boundary with `strstr` in user memory, and zero-copy count the remaining buffer as body payload.
2. **Expand TCP Socket Buffers (`SO_RCVBUF` / `SO_SNDBUF`)**:
   - Default OS socket buffers (tens of KB) limit TCP window scaling across high Bandwidth-Delay Product (BDP) links. Set socket receive/send buffers explicitly to 2MB.
3. **Enable `TCP_NODELAY`**:
   - Disable Nagle's algorithm to eliminate small request packet batching latencies.
4. **Batched Atomic Synchronization (Prevent Cacheline Bouncing)**:
   - Maintain thread-local transfer accumulators. Flush to the global atomic counter using `atomic_fetch_add_explicit(..., memory_order_relaxed)` once per 1MB rather than on every socket read/write, avoiding inter-core cacheline contention.

---

## 6. Windows & Windows ARM64 Cross-Compilation & Winsock Compatibility

### 🔴 Pitfall 1: Missing `strcasestr` in Windows MinGW
- MinGW-w64 runtime does not provide `strcasestr` as it is a GNU/BSD extension.
- Solution: Implement a portable `my_strcasestr` using `_strnicmp` on Windows and `strncasecmp` on POSIX.

### 🔴 Pitfall 2: Toolchain Setup for Windows ARM64 Cross-Compilation
- Running MSYS2 directly on Windows runners is slow and cumbersome for ARM64 cross-compilation.
- Solution:
  - Download official prebuilt `llvm-mingw` toolchains on `ubuntu-latest` and extract to `/opt/llvm-mingw`.
  - Toolchain prefixes: `x86_64-w64-mingw32` (x86_64) and `aarch64-w64-mingw32` (ARM64).
  - OpenSSL configuration: Use `mingw64` with `no-asm` and `--cross-compile-prefix` for both architectures.
  - Linker flags: `-static -lssl -lcrypto -lws2_32 -lgdi32 -lcrypt32 -lbcrypt -lpthread`.

### 🔴 Pitfall 3: Winsock Initialization & Socket Types
- Call `WSAStartup(MAKEWORD(2, 2), &wsaData)` on startup and `WSACleanup()` on exit.
- Use `closesocket(s)` instead of `close(fd)`.
- Use `DWORD` (milliseconds) for `SO_RCVTIMEO` / `SO_SNDTIMEO` socket timeout values instead of `struct timeval`.
