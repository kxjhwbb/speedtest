# 📌 嵌入式路由器 musl libc 与动态链接 not found 陷阱

> **文档元数据 (Metadata)**:
> - **记录时间 / Timestamp**: 2026-10-09 13:00:00 (UTC+8)
> - **作者 / Maintainer**: kxjhwbb
> - **涉及平台 / Platforms**: Linux OpenWrt / Padavan / Cudy / MIPS / ARM
> - **标签 / Tags**: #Embedded #StaticLinking #musl #glibc
> - **核心概述 / Summary**: 嵌入式路由器由于采用 musl libc，执行标准 glibc 动态链接程序会报找不到文件，必须强制使用 `-static` 进行全静态链接。

---

## 🔴 1. 踩坑现象 (Problem Symptom)
在 OpenWrt、Padavan 或 Cudy 等嵌入式路由器终端上运行交叉编译出的程序时，明明文件存在且赋予了执行权限（`chmod +x bb-speedtest`），直接执行却报错：
```bash
root@OpenWrt:~# ./bb-speedtest
sh: ./bb-speedtest: not found
# 或在 zsh 下:
zsh: no such file or directory: ./bb-speedtest
```

---

## 🔍 2. 根本原因 (Root Cause Analysis)
1. **动态解释器路径缺失**：
   - 绝大多数嵌入式路由器系统采用极简的 **musl libc**。
   - 而标准的 Ubuntu/Debian 交叉编译器默认使用 **glibc** 动态链接，生成的 ELF 二进制文件头部硬编码了动态解释器路径（如 `/lib/ld-linux-aarch64.so.1` 或 `/lib/ld-linux-armhf.so.3`）。
2. **内核加载报错机制**：
   - 路由器文件系统内只有 `/lib/ld-musl-aarch64.so.1`。
   - 当 Linux 内核加载可执行文件时，发现 ELF 请求的动态链接器在宿主系统上根本不存在，内核直接返回 `ENOENT`（No such file or directory），终端 Shell 进而打印 `not found`。

---

## ✅ 3. 解决方案 (Solution & Best Practices)
编译时必须强制开启 **`-static`**，将 C 运行时、OpenSSL 加密库、线程库全部打包进单一二进制文件：

```bash
# 编译命令示例
$(CC) -O3 -flto -fomit-frame-pointer speedtest.c -o bb-speedtest \
    -static -lssl -lcrypto -latomic -lpthread -ldl
```

---

## 🧪 4. 验证方式 (Verification)
使用 `file` 命令检查编译产物，输出必须明确包含 **`statically linked`**，且不能包含 `interpreter`：

```bash
$ file bb-speedtest-linux-aarch64
bb-speedtest-linux-aarch64: ELF 64-bit LSB executable, ARM aarch64, version 1 (SYSV), statically linked, stripped
```
