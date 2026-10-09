# 🚀 bb-speedtest

[**English**](README.md) | [**中文文档**](README_CN.md)

> 🤖 **Crafted with ❤️ through Human + AI Collaboration** (人类 + AI 协同打造)

**bb-speedtest** is a high-performance, lightweight, zero-dependency Speedtest CLI client written in pure C. Designed specifically for embedded Linux devices (OpenWrt / Padavan / Cudy routers), NAS, development boards (Raspberry Pi / RISC-V), and servers.

---

## ✨ Features

- ⚡ **Extreme Performance**: Native C implementation with `-O3 -flto` optimizations and ARMv8 Crypto / AES-NI hardware acceleration support. Easily saturates Gigabit, 2.5G, and 10G networks.
- 📦 **Statically Linked & Zero Dependency**: Self-contained with TLS/SSL stack. Runs out of the box on any Linux distribution (glibc, musl libc, OpenWrt, Alpine, Debian, CentOS, etc.) and macOS.
- 🔍 **Auto Server Selection**: Automatically fetches nearest server candidates from Speedtest.net (Ookla), measures concurrent ping latencies, and picks the best server.
- 📋 **List & Custom Server ID**: Support `-L` to view all nearby servers and their latencies; support `-s <ID>` to benchmark against a specific server ID.
- 📤 **Download & Upload Testing**: High-speed download testing by default, with optional full duplex (download + upload) benchmarking via `-U`.
- 🛡️ **Cache-Buster**: Appends dynamic UUIDs to requests to bypass CDN, ISP, and proxy caching.
- 📊 **Detailed Real-time Metrics**: Live display of current Mbps and MB/s throughput, transfer progress, and 25MB block transfer counts.

---

## 🎯 Supported Architectures & Prebuilt Binaries

Precompiled static binaries for 10 architectures are automatically built and published via GitHub Actions for every Release:

| OS / Architecture | Binary Name | Target Hardware / Devices |
| :--- | :--- | :--- |
| **Linux x86_64** | `bb-speedtest-linux-x86_64` | Intel / AMD 64-bit PCs, Soft Routers, VPS, Servers |
| **Linux aarch64** | `bb-speedtest-linux-aarch64` | Raspberry Pi 4/5, ARM Routers (Cudy, MT7981/MT7986), Apple Silicon Linux, Oracle ARM |
| **Linux armhf** | `bb-speedtest-linux-armhf` | Raspberry Pi 2/3, Legacy ARMv7 Routers |
| **Linux armel** | `bb-speedtest-linux-armel` | Legacy ARMv5TE / ARMv6 Embedded Devices |
| **Linux mipsel** | `bb-speedtest-linux-mipsel` | MIPS Little-Endian Routers (MT7621, MT7620, OpenWrt) |
| **Linux mips** | `bb-speedtest-linux-mips` | MIPS Big-Endian Routers (Atheros AR9344, etc.) |
| **Linux riscv64** | `bb-speedtest-linux-riscv64` | RISC-V 64-bit SBCs and Servers |
| **Linux i686** | `bb-speedtest-linux-i686` | Legacy 32-bit x86 Systems |
| **macOS Apple Silicon** | `bb-speedtest-darwin-arm64` | Apple Silicon Mac (M1 / M2 / M3 / M4) |
| **Windows x86_64** | `bb-speedtest-windows-x86_64.exe` | Windows 10 / 11 / Server (64-bit) |

---

## 🚀 Quick Start

### 1. Download and Grant Execution Permissions

```bash
# Example for ARM64 Linux Router (or download from Releases)
wget -O bb-speedtest https://github.com/kxjhwbb/speedtest/releases/latest/download/bb-speedtest-linux-aarch64
chmod +x bb-speedtest
```

### 2. Common Usage Examples

```bash
# 1. Default mode: auto-select lowest latency server and test download
./bb-speedtest

# 2. Full test: benchmark download followed by upload
./bb-speedtest -U

# 3. List nearby Speedtest servers and their latencies
./bb-speedtest -L

# 4. Test against a specific server ID (e.g., HKBN: 65463)
./bb-speedtest -s 65463 -U

# 5. Customize test duration and concurrent streams (e.g., 15s, 8 streams)
./bb-speedtest -d 15 -t 8 -U

# 6. Specify custom download URL directly
./bb-speedtest -u "https://speedtest21.hkbn.net.prod.hosts.ooklaserver.net:8080/download?size=25000000"
```

---

## ⚙️ Command-Line Options

```text
Usage: ./bb-speedtest [options]
Options:
  -U            Enable upload test (Default: download only. Performs download then upload)
  --only-upload Upload test only (Skip download test)
  -L            List nearby Speedtest servers and their latencies
  -s <ID>       Specify a Speedtest server ID manually
  -u <URL>      Specify a custom download URL directly
  -d <sec>      Test duration in seconds (Default: 10)
  -t <threads>  Number of concurrent streams/threads (Default: 4)
  -T <sec>      Socket connection timeout in seconds (Default: 10)
  -b <bytes>    Block size in bytes for stats (Default: 25000000)
  -h            Show this help message
```

---

## 🛠️ Build from Source

```bash
# Install dependencies (Debian / Ubuntu)
sudo apt-get install -y build-essential libssl-dev zlib1g-dev libzstd-dev

# Build with Makefile
make

# Or compile statically manually
gcc -O3 -flto -fomit-frame-pointer -Wall speedtest.c -o bb-speedtest -static -lssl -lcrypto -lz -lzstd -lpthread -ldl
```

---

## ⚖️ Disclaimer & Trademark Notice

- **Unofficial Tool**: This is an independent, open-source third-party tool created solely for personal network benchmarking, educational, and research purposes.
- **Trademark Notice**: `Speedtest®` and `Ookla®` are registered trademarks of Ookla, LLC. This project is **not** affiliated with, sponsored by, or endorsed by Ookla, LLC.
- **Fair Use**: Please use public speedtest servers responsibly. Do not use this tool for excessive stress-testing, automated polling at high frequency, or any malicious traffic generation that disrupts server operations.

---

## 📄 License

This project is licensed under the [MIT License](LICENSE).
