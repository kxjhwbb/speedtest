# 📌 Windows 与 Windows ARM64 llvm-mingw 交叉编译及 Winsock 兼容

> **文档元数据 (Metadata)**:
> - **记录时间 / Timestamp**: 2026-10-09 14:12:00 (UTC+8)
> - **作者 / Maintainer**: kxjhwbb
> - **涉及平台 / Platforms**: Windows (x86_64, ARM64)
> - **标签 / Tags**: #Windows #ARM64 #llvm-mingw #Winsock #CrossCompile
> - **核心概述 / Summary**: Windows 缺少 GNU 的 `strcasestr`，需要手写兼容实现；通过 llvm-mingw 在 Linux 下极速交叉编译 Windows x86_64 与 ARM64 静态二进制。

---

## 🔴 1. 踩坑现象 (Problem Symptom)
1. **MinGW 缺失 `strcasestr`**：编译报错 `implicit declaration of function 'strcasestr'` 或 `undefined reference to 'strcasestr'`。
2. **GitHub Actions 上 Windows runner 极其缓慢且 ARM64 工具链难以配置**。
3. **GitHub Marketplace 没有 `setup-llvm-mingw` 官方 Action**（报错 `Error: Unable to resolve action`）。

---

## 🔍 2. 根本原因 (Root Cause Analysis)
1. `strcasestr` 是 GNU/BSD 扩展函数，Windows CRT（MSVCRT/UCRT）并不提供此 API。
2. Windows 下 Socket 属于 Winsock2 规范，需要先初始化 `WSAStartup`，关闭套接字需使用 `closesocket`，超时参数类型是毫秒整数 `DWORD`。

---

## ✅ 3. 解决方案 (Solution & Best Practices)

### 1. 手写跨平台大小写匹配函数 `my_strcasestr`
```c
static const char* my_strcasestr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
    size_t nlen = strlen(needle);
    if (nlen == 0) return haystack;
    for (; *haystack; haystack++) {
#ifdef _WIN32
        if (_strnicmp(haystack, needle, nlen) == 0) return haystack;
#else
        if (strncasecmp(haystack, needle, nlen) == 0) return haystack;
#endif
    }
    return NULL;
}
```

### 2. 在 Ubuntu 上直接部署官方预编译 `llvm-mingw` 工具链
在 `ubuntu-latest` 上拉取官方预编译 tarball 并解压到 `/opt/llvm-mingw`：
```yaml
- name: Install LLVM MinGW Cross-Compiler Toolchain
  run: |
    URL="https://github.com/mstorsjo/llvm-mingw/releases/download/20261006/llvm-mingw-20261006-ucrt-ubuntu-22.04-x86_64.tar.xz"
    curl -sL "$URL" -o /tmp/llvm-mingw.tar.xz
    sudo mkdir -p /opt/llvm-mingw
    sudo tar -xf /tmp/llvm-mingw.tar.xz -C /opt/llvm-mingw --strip-components=1
    echo "/opt/llvm-mingw/bin" >> $GITHUB_PATH
```

### 3. OpenSSL 目标与静态链接库
- OpenSSL Target：统一使用 `mingw64` 配合 `no-asm` 与 `--cross-compile-prefix=x86_64-w64-mingw32-` / `aarch64-w64-mingw32-`。
- 链接参数：`-static -lssl -lcrypto -lws2_32 -lgdi32 -lcrypt32 -lbcrypt -lpthread`。
