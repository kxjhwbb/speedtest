# 📌 macOS 下静态嵌入 OpenSSL .a 归档与 Actions Runner 选型

> **文档元数据 (Metadata)**:
> - **记录时间 / Timestamp**: 2026-10-09 13:45:00 (UTC+8)
> - **作者 / Maintainer**: kxjhwbb
> - **涉及平台 / Platforms**: macOS (Apple Silicon arm64)
> - **标签 / Tags**: #macOS #StaticLinking #dyld #AppleSilicon #Homebrew
> - **核心概述 / Summary**: macOS 系统禁止全局静态链接系统库，但第三方库（如 OpenSSL）必须通过直接指定 `.a` 绝对路径静态嵌入，避免异机运行出现 `dyld: Library not loaded`。

---

## 🔴 1. 踩坑现象 (Problem Symptom)
1. **Runner 卡死**：在 CI 中使用 `runs-on: macos-13` 时，任务一直显示 `Waiting for a runner...` 无法被调度。
2. **异机缺失动态库**：编译出的 macOS 二进制文件体积只有 60KB，下载到其他未安装 Homebrew OpenSSL 的 Mac 上运行时报错：
```bash
dyld: Library not loaded: /opt/homebrew/opt/openssl@3/lib/libssl.3.dylib
  Referenced from: /usr/local/bin/bb-speedtest-darwin-arm64
  Reason: image not found
zsh: abort      ./bb-speedtest-darwin-arm64
```

---

## 🔍 2. 根本原因 (Root Cause Analysis)
1. **GitHub 淘汰 Intel Mac Runner**：GitHub 已全面下线淘汰 Intel `macos-13` 机器，必须迁移到 `macos-latest`（Apple Silicon M 系列）。
2. **macOS 链接器优先动态链接机制**：
   - 使用 `-L"${OPENSSL_DIR}/lib" -lssl -lcrypto` 时，Clang 优先链接 Homebrew 的 `.dylib` 动态库。
   - macOS（Darwin 内核）不允许全局使用 `-static` 参数（因为 `libSystem.B.dylib` 必须动态加载），但如果不显式指定静态归档，第三方库不会被打包进可执行文件。

---

## ✅ 3. 解决方案 (Solution & Best Practices)
1. **统一使用 `macos-latest`**：
   ```yaml
   runs-on: macos-latest
   ```
2. **直接传入 OpenSSL 静态库归档文件路径**：
   ```bash
   OPENSSL_DIR=$(brew --prefix openssl@3)
   clang -O3 -flto -fomit-frame-pointer -Wall \
     -I"${OPENSSL_DIR}/include" \
     speedtest.c \
     "${OPENSSL_DIR}/lib/libssl.a" \
     "${OPENSSL_DIR}/lib/libcrypto.a" \
     -o bb-speedtest-darwin-arm64 \
     -lpthread -lz
   ```

---

## 🧪 4. 验证方式 (Verification)
使用 `otool -L` 检查二进制依赖，确认输出中**没有**任何 Homebrew 或第三方 `.dylib`，仅依赖系统内置库：

```bash
$ otool -L bb-speedtest-darwin-arm64
bb-speedtest-darwin-arm64:
	/usr/lib/libSystem.B.dylib (compatibility version 1.0.0, current version 1336.0.0)
	/usr/lib/libz.1.dylib (compatibility version 1.0.0, current version 1.2.12)
```
产物体积恢复为正常的 3.5MB ~ 4.5MB。
