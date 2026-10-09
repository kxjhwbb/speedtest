# 📘 bb-speedtest 架构设计与跨平台交叉编译避坑指南

> **文档元数据 (Metadata)**:
> - **作者 / Maintainer**: kxjhwbb & Antigravity AI
> - **记录时间 / Timestamp**: 2026-10-09 14:12:00 (UTC+8)
> - **版本 / Version**: v1.0.7
> - **项目地址 / Repository**: https://github.com/kxjhwbb/speedtest
>
> **文档目的**：本文档沉淀了在构建极低损耗、全平台静态 C 语言测速工具（bb-speedtest）过程中的核心架构决策、底层网络调优技巧以及 GitHub Actions 多架构交叉编译的踩坑与避坑实战经验，供后续开发者与 AI 快速参考。

---

## 目录
1. [嵌入式路由器环境与二进制链接陷阱](#1-嵌入式路由器环境与二进制链接陷阱)
2. [32位架构中的 64位原子操作缺失陷阱](#2-32位架构中的-64位原子操作缺失陷阱)
3. [跨架构 OpenSSL 静态编译与汇编兼容性](#3-跨架构-openssl-静态编译与汇编兼容性)
4. [GitHub Actions CI/CD 踩坑实录](#4-github-actions-cicd-踩坑实录)
5. [高性能网络测速引擎的损耗优化法则](#5-高性能网络测速引擎的损耗优化法则)
6. [Windows 与 Windows ARM64 交叉编译及 Winsock 兼容避坑](#6-windows-与-windows-arm64-交叉编译及-winsock-兼容避坑)

---

## 1. 嵌入式路由器环境与二进制链接陷阱

### 🔴 踩坑现象
在 OpenWrt、Padavan 或 Cudy 等嵌入式路由器终端上运行交叉编译出的程序时，明明文件存在且有 `+x` 权限，系统却直接报错：
```bash
zsh: no such file or directory: ./bb-speedtest
# 或
sh: ./bb-speedtest: not found
```

### 🔍 根本原因
- 绝大多数嵌入式路由器基于 **musl libc**，而 Ubuntu/Debian 交叉编译器默认动态链接到 **glibc**，生成的 ELF 二进制文件头部硬编码指定了解释器路径（如 `/lib/ld-linux-aarch64.so.1`）。
- 路由器上只有 `/lib/ld-musl-aarch64.so.1`，内核在加载可执行文件阶段发现解释器不存在，直接抛出 `ENOENT`（即 `No such file or directory`）。

### ✅ 解决方案
- **必须完全静态链接（Statically Linked）**：编译时强制指定 `-static`。
- 将 C 运行时、OpenSSL 加密库、线程库全部打入单一二进制文件。
- 验证方式：运行 `file bb-speedtest`，输出必须包含 `statically linked` 且不包含 `interpreter`。

---

## 2. 32位架构中的 64位原子操作缺失陷阱

### 🔴 踩坑现象
在为 32 位 MIPS（`mipsel`/`mips`）和 32 位 ARM（`armhf`/`armel`）编译时，链接阶段报大量未定义符号：
```text
undefined reference to '__atomic_fetch_add_8'
undefined reference to '__atomic_store_8'
undefined reference to '__atomic_load_8'
undefined reference to '__atomic_is_lock_free'
collect2: error: ld returned 1 exit status
```

### 🔍 根本原因
- 32 位 CPU（如 MIPS 24Kc/MT7621、ARMv7 Cortex-A7）硬件指令集**只支持原生 32 位的原子操作指令**，无法在单个指令周期内原子操作 64 位数据（`uint64_t`）。
- 当代码（或 OpenSSL 3.x 内部）使用 64 位原子变量（如 `atomic_uint_fast64_t g_total_bytes`）时，GCC 会生成对编译器辅助函数（Compiler Helper Functions）的调用（函数名以 `_8` 结尾，代表 8 字节/64 位）。
- GCC 的标准 C 运行时中没有内置这些 64 位原子函数，它们存放在独立的 `libatomic.a` 中。

### ✅ 解决方案
- 在交叉编译 32 位目标时，**必须在链接选项末尾显式链接 `-latomic`**：
  ```bash
  $(CC) speedtest.c -o bb-speedtest -static -lssl -lcrypto -latomic -lpthread -ldl
  ```

---

## 3. 跨架构 OpenSSL 静态编译与汇编兼容性

### 🔴 踩坑现象
直接通过各交叉编译器构建 OpenSSL 时，报错提示汇编语法不识别、浮点 ABI 冲突（如 `VFP/NEON` 不匹配），或者 MIPS 架构提示 `instruction not supported`。

### 🔍 根本原因
- OpenSSL 内部针对特定架构（如 ARMv7-A NEON、MIPS Release 2）编写了硬编码汇编代码。
- 不同的交叉编译器工具链默认的目标浮点 ABI 或指令集版本（如 MIPS r2 vs r6，ARM softfp vs hardfp）各不相同，极易产生汇编层面的冲突。

### ✅ 解决方案
- 在交叉编译 OpenSSL 时强制加上 **`no-asm`** 标志，关闭硬件特定汇编，强制采用高移植性的纯 C 语言算法实现：
  ```bash
  ./Configure <target> no-shared no-tests no-docs no-legacy no-engine no-ui-console no-asm
  ```
- 配合目标平台标识：
  - `aarch64` ➜ `linux-aarch64`
  - `armhf` / `armel` ➜ `linux-armv4`
  - `mipsel` / `mips` ➜ `linux-mips32`
  - `riscv64` ➜ `linux64-riscv64`
  - `i686` ➜ `linux-x86`

---

## 4. GitHub Actions CI/CD 踩坑实录

### 🔴 踩坑现象 1：`macos-13` 任务一直卡在 Waiting 状态
- 日志提示：`Waiting for a runner to pick up this job...`，排队数十分钟甚至死锁。

### 🔍 根本原因
- GitHub 官方自 2024 年底起已逐步**下线并淘汰（Deprecate & Sunset）**基于 Intel x86 芯片的 `macos-13` Runner。
- Intel macOS 虚拟机资源池极其匮乏，导致任务无限期等待调度。

### ✅ 解决方案
- 统一使用官方主力的 Apple Silicon 机器：**`macos-latest`**（对应 M 系列芯片，秒级分配）。
- macOS 编译产物以原生的 Apple Silicon `arm64` 为主（涵盖当前全部主流 Mac 设备）。

---

### 🔴 踩坑现象 2：推送一次代码触发两个重复的 Workflow Run
- 每次推送代码和打标签时，Actions 界面出现两条完全一样的流水线。

### 🔍 根本原因
- Workflow 配置了 `on: push: branches: [main]` 与 `on: push: tags: ['v*']`。
- 执行 `git push origin main && git push origin v1.0.0` 时，Git 分别向服务端推送了分支与标签，触发了两次事件。

### ✅ 最佳实践
- 如果仅希望在发布版本时打包，移除 `branches: [main]`，仅保留 `tags: ['v*']` 与 `workflow_dispatch`（手动触发）。

---

### 🔴 踩坑现象 3：macOS 产物体积只有几十 KB 且跨设备报错 `dyld: Library not loaded`
- **现象**：macOS 编译产物 `bb-speedtest-darwin-arm64` 体积仅 60KB 左右，复制到其他 Mac 上运行时报错 `dyld: Library not loaded: /opt/homebrew/opt/openssl@3/lib/libssl.3.dylib`。
- **根本原因**：
  - 在 macOS 下使用 `-L"${OPENSSL_DIR}/lib" -lssl -lcrypto` 时，Clang 默认优先动态链接 Homebrew 路径下的 `.dylib` 共享库。
  - 二进制文件中只包含自身几十 KB 的测速逻辑代码，OpenSSL 协议栈并未打包进去。
- **解决方案**：
  - macOS 系统（Darwin）禁止全局静态链接系统库（不允许使用 `-static` 链接 `libSystem.B.dylib`），但**第三方库必须静态嵌入**。
  - 直接在 Clang 命令行中传入 OpenSSL 静态库归档文件的绝对路径：
    ```bash
    clang speedtest.c "${OPENSSL_DIR}/lib/libssl.a" "${OPENSSL_DIR}/lib/libcrypto.a" -o bb-speedtest-darwin-arm64 -lpthread -lz
    ```
  - 打包后体积约为 3.5MB~4.5MB，使用 `otool -L` 检查可确认仅依赖系统级的 `/usr/lib/libSystem.B.dylib` 和 `/usr/lib/libz.1.dylib`，可以在任意 Apple Silicon Mac 上免安装依赖直接运行。

---

## 5. 高性能网络测速引擎的损耗优化法则

在 2.5G/万兆网络以及弱性能 ARM 路由器上测速时，程序本身的瓶颈主要不在网络线路上，而在于**系统调用损耗与内核缓冲区竞争**。

### 🚀 优化关键点总结：
1. **彻底杜绝单字节读取 HTTP Header**：
   - 传统简单做法：每次读 1 字节匹配 `\r\n\r\n`。这会导致一次请求产生 500+ 次系统调用。
   - 优化做法：一次性用 128KB 缓冲区读入，在内存中直接 `strstr(buf, "\r\n\r\n")`，并将多余读取的字节零拷贝计入 Body 流量。
2. **扩大 TCP Socket 窗口 (`SO_RCVBUF` / `SO_SNDBUF`)**：
   - 内核默认缓冲区较小（通常几十 KB），在高带宽时延积（BDP）场景下会导致 TCP 滑动窗口收缩跑不满带宽。
   - 启动前显式设置 `SO_RCVBUF` 和 `SO_SNDBUF` 为 2MB。
3. **启用 `TCP_NODELAY`**：
   - 禁用 Nagle 算法，避免微小请求头的攒包延迟。
4. **批量原子变量同步（消除 CPU Cacheline 乒乓效应）**：
   - 各工作线程内部维护局部累加器，积累满 1MB 后再通过 `atomic_fetch_add_explicit(..., relaxed)` 批量回写全局计数器，避免多核心跨缓存行写竞争。

---

## 6. Windows 与 Windows ARM64 交叉编译及 Winsock 兼容避坑

### 🔴 踩坑现象 1：MinGW / MSYS2 下缺少 `strcasestr`
- Windows MinGW-w64 报错：`implicit declaration of function 'strcasestr'` 或 `undefined reference to 'strcasestr'`。
- **原因**：`strcasestr` 是 GNU/BSD 扩展，标准 Windows C 运行时（MSVCRT/UCRT）并不提供此函数。
- **解决方案**：自行实现轻量级的 `my_strcasestr`，在 Windows 下使用 `_strnicmp`，在 POSIX 下使用 `strncasecmp`。

### 🔴 踩坑现象 2：GitHub Actions 上 Windows ARM64 交叉编译工具链
- 传统 MSYS2 运行在 Windows runner 上极其缓慢且对 ARM64 交叉编译配置繁琐，且 GitHub Actions 市场上没有现成的 `setup-llvm-mingw` 官方 Action。
- **解决方案**：
  - 在 `ubuntu-latest` 上直接下载官方预编译的 `llvm-mingw`（基于 Ubuntu 22.04 的 ucrt tarball）并解压至 `/opt/llvm-mingw`，将 `bin` 加入 `$GITHUB_PATH`。
  - 工具链前缀：`x86_64-w64-mingw32` (x86_64) 与 `aarch64-w64-mingw32` (ARM64)。
  - OpenSSL 静态编译目标：统一使用 `mingw64` 配合 `no-asm` 与 `--cross-compile-prefix` 进行交叉构建。
  - 静态编译链接参数：`-static -lssl -lcrypto -lws2_32 -lgdi32 -lcrypt32 -lbcrypt -lpthread`。

### 🔴 踩坑现象 3：Winsock 初始化与套接字类型
- Windows 下 socket 必须在调用前执行 `WSAStartup(MAKEWORD(2, 2), &wsaData)`，并在程序退出时调用 `WSACleanup()`。
- 关闭套接字在 POSIX 下使用 `close(fd)`，在 Windows 下必须使用 `closesocket(s)`。
- 设置超时时间 `SO_RCVTIMEO`/`SO_SNDTIMEO` 时，POSIX 使用 `struct timeval`，而 Windows Winsock 使用 `DWORD`（毫秒整数）。


