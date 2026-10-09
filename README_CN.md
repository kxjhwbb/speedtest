# 🚀 bb-speedtest (中文文档)

[**English**](README.md) | [**中文文档**](README_CN.md)

**bb-speedtest** 是一款高性能、轻量级、零依赖的跨平台 Speedtest 测速客户端（纯 C 语言原生实现）。专为嵌入式 Linux 设备（OpenWrt / Padavan / Cudy 等路由器）、NAS、开发板（树莓派 / RISC-V）以及服务器设计。

---

## ✨ 特性

- ⚡ **极致性能**：C 语言原生实现，全链路 `-O3 -flto` 深度优化，支持 ARMv8 Crypto / AES-NI 硬件加密加速，轻松压满千兆、2.5G 与万兆网络。
- 📦 **完全静态编译**：零外部依赖，内置 TLS/SSL 加密协议栈。可在任意 Linux 发行版（glibc、musl libc、OpenWrt、Alpine、Debian、CentOS 等）及 macOS 上开箱即用。
- 🔍 **智能服务器择优**：自动从 Speedtest.net (Ookla) 获取附近的候选测速服务器，并发测试 Ping 延迟并自动选取最优节点。
- 📋 **列表与指定节点**：支持 `-L` 查看附近服务器列表及延迟；支持 `-s <ID>` 手动指定特定服务器 ID 测速。
- 📤 **支持下行与上行测试**：默认高速下行下载测速，支持 `-U` 开启下行+上行完整双向测速。
- 🛡️ **动态防缓存**：每次请求自动携带随机 UUID 参数，规避代理、CDN 或运营商本地缓存影响。
- 📊 **实时统计与块数计算**：实时显示 Mbps / MB/s 速率、传输进度以及 25MB 数据块的完成数量。

---

## 🎯 支持架构与预编译文件列表

每次在 GitHub 提交 Release 标签时，GitHub Actions 会自动编译并发布以下 10 个平台的静态二进制文件：

| 系统 / 架构 | 二进制文件名 | 适用设备 |
| :--- | :--- | :--- |
| **Linux x86_64** | `bb-speedtest-linux-x86_64` | Intel / AMD 64位软路由、PC、VPS、服务器 |
| **Linux aarch64** | `bb-speedtest-linux-aarch64` | 树莓派 4/5、ARM 路由器 (Cudy/MT7981/MT7986/高通等)、Apple Silicon Linux、甲骨文 ARM |
| **Linux armhf** | `bb-speedtest-linux-armhf` | 树莓派 2/3、老款 ARMv7 路由器 |
| **Linux armel** | `bb-speedtest-linux-armel` | 老款 ARMv5TE / ARMv6 嵌入式设备 |
| **Linux mipsel** | `bb-speedtest-linux-mipsel` | MIPS 小端路由器 (MT7621 / MT7620 等经典 OpenWrt 路由) |
| **Linux mips** | `bb-speedtest-linux-mips` | MIPS 大端设备 (Atheros AR9344 等) |
| **Linux riscv64** | `bb-speedtest-linux-riscv64` | RISC-V 架构开发板与服务器 |
| **Linux i686** | `bb-speedtest-linux-i686` | 32位 x86 旧设备 |
| **macOS Apple Silicon** | `bb-speedtest-darwin-arm64` | Apple Silicon Mac (M1 / M2 / M3 / M4) |
| **macOS Intel** | `bb-speedtest-darwin-x86_64` | Intel Mac |

---

## 🚀 快速上手

### 1. 下载并赋予执行权限

```bash
# 以 ARM64 路由器为例 (可在 Releases 页面下载最新版)
wget -O bb-speedtest https://github.com/kxjhwbb/speedtest/releases/latest/download/bb-speedtest-linux-aarch64
chmod +x bb-speedtest
```

### 2. 常用命令示例

```bash
# 1. 默认模式：自动优选最低延迟服务器测速 (默认 4 线程，10 秒，仅测下行)
./bb-speedtest

# 2. 双向测速：先测下行下载，紧接着测上行上传
./bb-speedtest -U

# 3. 查看附近可用服务器列表及延迟
./bb-speedtest -L

# 4. 手动指定服务器 ID 测速 (例如指定 HKBN 节点: 65463)
./bb-speedtest -s 65463 -U

# 5. 自定义测速时长与并发线程 (例如 15 秒，8 线程)
./bb-speedtest -d 15 -t 8 -U

# 6. 直接指定自定义测速 URL
./bb-speedtest -u "https://speedtest21.hkbn.net.prod.hosts.ooklaserver.net:8080/download?size=25000000"
```

---

## ⚙️ 命令行参数大全

```text
用法: ./bb-speedtest [选项]
选项:
  -U            开启上行测速 (默认仅测下行；加上此选项将先测下行、再测上行)
  --only-upload 仅测上行速度 (跳过下行下载测试)
  -L            列出附近的所有 Speedtest 服务器及延迟
  -s <ID>       手动指定 Speedtest 服务器 ID 进行测速
  -u <URL>      直接指定测速 URL
  -d <秒数>     测速持续时间 (默认: 10 秒)
  -t <线程数>   并发下载/上传线程数 (默认: 4 线程)
  -T <秒数>     单次连接超时时间 (默认: 10 秒)
  -b <字节数>   单块字节大小用于统计 (默认: 25000000 字节)
  -h            显示此帮助信息
```

---

## 🛠️ 本地编译构建

```bash
# 安装构建依赖 (Debian / Ubuntu)
sudo apt-get install -y build-essential libssl-dev zlib1g-dev libzstd-dev

# 使用 Makefile 编译
make

# 或者手动静态编译
gcc -O3 -flto -fomit-frame-pointer -Wall speedtest.c -o bb-speedtest -static -lssl -lcrypto -lz -lzstd -lpthread -ldl
```

---

## ⚖️ 免责声明与商标声明 (Disclaimer)

- **非官方工具**：本项目为独立的第三方开源工具，仅供个人网络质量诊断、学习交流与学术研究使用。
- **商标归属**：`Speedtest®` 与 `Ookla®` 是 Ookla, LLC 的注册商标。本项目与 Ookla, LLC **没有任何隶属、赞助或背书关系**。
- **合理使用**：公共测速服务器带宽由全球各 ISP 与赞助商提供，请合理使用本工具，切勿用于高频恶意刷流、DDoS 压测或滥用公共测速服务器资源。

---

## 📄 开源许可证

本项目基于 [MIT License](LICENSE) 开源。
