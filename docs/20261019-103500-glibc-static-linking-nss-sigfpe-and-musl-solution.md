# 📌 Glibc 静态编译 getaddrinfo 触发 SIGFPE 崩溃与 Musl 终极静态方案

> **文档元数据 (Metadata)**:
> - **记录时间 / Timestamp**: 2026-10-19 10:35:00 (UTC+8)
> - **作者 / Maintainer**: kxjhwbb
> - **涉及平台 / Platforms**: Linux x86_64, fnOS (飞牛OS), Debian, Alpine, OpenWrt 等所有 Linux 发行版
> - **标签 / Tags**: #glibc #musl #SIGFPE #StaticLinking #NSS #getaddrinfo #fnOS
> - **核心概述 / Summary**: 在现代 Ubuntu 上使用 `gcc -static` 编译的 Linux x86_64 二进制文件，由于 glibc NSS 机制强制在运行时 `dlopen` 宿主机的动态共享库，在不同 glibc 版本或第三方发行版（如 fnOS）上执行域名解析时会触发段错误或 `zsh: floating point exception` (SIGFPE)；改用 `musl-gcc` 配合静态 OpenSSL 构建完全无外部依赖的真静态文件可完美解决。

---

## 🔴 1. 踩坑现象 (Problem Symptom)
在 fnOS（飞牛私有云 OS，基于 Debian 12）或其他非原生 Ubuntu 系统的 x86_64 设备上运行 `bb-speedtest-linux-x86_64`：
```bash
fnOS ~ # ./bb-speedtest-linux-x86_64
[*] Fetching Speedtest.net server list...
zsh: floating point exception  ./bb-speedtest-linux-x86_64
```
程序在打印完第一句提示、刚开始执行网络请求和域名解析时立即崩溃。

---

## 🔍 2. 根本原因 (Root Cause Analysis)

### 1. Glibc 的伪静态机制与 NSS 动态加载陷阱
- GNU C 库（`glibc`）官方在设计上**不支持**也不推荐真正的静态网络程序。
- 即使在编译时传入了 `-static` 参数，glibc 的名字解析器（Name Service Switch, NSS，即 `getaddrinfo` 系列函数）在底层依然会调用内置的 `dlopen` 去宿主机的 `/lib` 目录下动态加载 `libnss_dns.so.2`、`libnss_files.so.2` 或 `libnss_resolve.so.2`。

### 2. Glibc 版本错位与 SIGFPE 崩溃
- GitHub Actions 运行在 `ubuntu-latest`（Ubuntu 24.04），内置 **glibc 2.39**。
- fnOS 运行在 Debian 12，内置 **glibc 2.36**。
- 当静态二进制（glibc 2.39）调用 `getaddrinfo("www.speedtest.net")` 时，glibc 的 stub `dlopen` 试图把宿主机 glibc 2.36 的 `libnss_dns.so` 加载到没有初始化动态链接器的内存空间中。
- 符号重定位失败或结构体偏移不兼容引发未定义行为（未捕获的算术指针计算异常），内核向进程发送 **SIGFPE**（Floating point exception / 算术硬件异常）信号导致程序闪退。

---

## ✅ 3. 解决方案 (Solution & Best Practices)

将 Linux x86_64 构建环境从 **glibc** 切换至标准 **musl libc**（`musl-tools` / `musl-gcc`）：

1. **Musl 原生真静态**：
   `musl libc` 不使用 NSS 动态机制，内部拥有轻量、独立、纯 C 实现的 DNS 递归解析器，编译出来的二进制文件内部没有任何 `dlopen`，也不依赖宿主机的任何 `.so` 文件。
2. **构建脚本配置**：
   ```bash
   # 安装 musl 工具链并建立内核头文件符号链接
   sudo apt-get install -y build-essential musl-tools
   sudo ln -sf /usr/include/linux /usr/include/x86_64-linux-musl/linux
   sudo ln -sf /usr/include/asm-generic /usr/include/x86_64-linux-musl/asm-generic
   sudo ln -sf /usr/include/x86_64-linux-gnu/asm /usr/include/x86_64-linux-musl/asm

   # 使用 musl-gcc 静态编译 OpenSSL
   CC=musl-gcc ./Configure linux-x86_64 no-shared no-tests no-docs no-legacy no-engine no-ui-console ...

   # 链接最终完全独立的静态可执行文件
   musl-gcc -O3 -flto -fomit-frame-pointer -Wall \
     -I$OPENSSL_DIR/include \
     speedtest.c \
     $OPENSSL_DIR/lib64/libssl.a \
     $OPENSSL_DIR/lib64/libcrypto.a \
     -o bb-speedtest-linux-x86_64 \
     -static -lpthread
   ```

---

## 🧪 4. 验证方式 (Verification)

使用 `readelf` 检查二进制文件，必须显示完全无任何动态段：
```bash
$ readelf -d bb-speedtest-linux-x86_64
There is no dynamic section in this file.
```
在任意安装了 Linux 内核的机器（不论是 fnOS、OpenWrt、Debian、Ubuntu、CentOS 还是 Alpine）上均可开箱即用，彻底杜绝 SIGFPE 与 NSS 崩溃。
