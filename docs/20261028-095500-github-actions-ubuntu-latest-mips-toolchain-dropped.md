# 📌 GitHub Actions ubuntu-latest 升级导致 MIPS 交叉编译工具链缺失陷阱

> **文档元数据 (Metadata)**:
> - **记录时间 / Timestamp**: 2026-10-28 09:55:00 (UTC+8)
> - **作者 / Maintainer**: kxjhwbb
> - **涉及平台 / Platforms**: Linux MIPS / MIPSel / CI/CD (GitHub Actions)
> - **标签 / Tags**: #GitHubActions #MIPS #MIPSel #UbuntuLatest #CrossCompile
> - **核心概述 / Summary**: GitHub Actions 的 `ubuntu-latest` 运行器镜像轮转至更新的 Ubuntu 版本（如 26.04 / 24.04+），而上游 Debian 12/13 及 Ubuntu 新版因架构老旧已全面废弃并移除了 32 位 MIPS 软件包源（`gcc-mips-linux-gnu` 与 `gcc-mipsel-linux-gnu`）。需将 CI Runner 版本显式固定为 `runs-on: ubuntu-22.04` 以锁定长期稳定的 MIPS 交叉编译环境。

---

## 🔴 1. 踩坑现象 (Problem Symptom)

在 GitHub Actions 触发 Release 构建时，`build-linux-cross (mips)` 和 `build-linux-cross (mipsel)` 矩阵任务在安装交叉编译器步骤抛出严重错误：

```text
Run sudo apt-get update
Hit:2 http://azure.archive.ubuntu.com/ubuntu resolute InRelease
Get:6 https://packages.microsoft.com/ubuntu/26.04/prod resolute InRelease [3964 B]
...
Reading package lists...
Building dependency tree...
Reading state information...
Package gcc-mipsel-linux-gnu is not available, but is referred to by another package.
This may mean that the package is missing, has been obsoleted, or
is only available from another source

E: Package 'gcc-mipsel-linux-gnu' has no installation candidate
Error: Process completed with exit code 100.
```

---

## 🔍 2. 根本原因 (Root Cause Analysis)

1. **`ubuntu-latest` 浮动标签的不可控性**：
   - GitHub Actions 的 `ubuntu-latest` 并非固定发行版，会随着 GitHub 镜像生命周期自动指向更新的 Ubuntu 版本（日志中可见已更新至 Ubuntu 26.04 `resolute`）。
2. **上游生态全面下线 32 位 MIPS**：
   - Debian 官方自 Debian 10 废弃大端 `mips`，Debian 12 彻底下线小端 `mipsel`，Debian 13 移除了全系 32 位 MIPS 支持。
   - 同步自 Debian 的 Ubuntu 在新版本（24.04/26.04+）中同步移除了 `gcc-mips-linux-gnu` 和 `gcc-mipsel-linux-gnu`，导致 `apt-get` 找不到任何安装候选包（no installation candidate）。
3. **Ubuntu 22.04 (Jammy) 是保留完整 MIPS 支持的稳定 LTS 镜像**：
   - Ubuntu 22.04 LTS（Jammy Jellyfish）的 Universe 源中依然完整保留并维护了 `gcc-mips-linux-gnu` 与 `gcc-mipsel-linux-gnu`。

---

## ✅ 3. 解决方案 (Solution & Best Practices)

### 1. 锁定 Runner 镜像为 `ubuntu-22.04`
将 GitHub Actions 工作流中所有 Linux 交叉编译及依赖特定工具链的环境从浮动的 `ubuntu-latest` 显式固定为 `ubuntu-22.04`：

```yaml
  build-linux-cross:
    name: Build Linux (${{ matrix.arch }})
    runs-on: ubuntu-22.04
```

### 2. 启用 Universe 软件源保护
在执行 `apt-get update` 前显式激活 Universe 仓库：
```yaml
      - name: Install Cross Toolchain
        run: |
          sudo add-apt-repository universe -y || true
          sudo apt-get update
          sudo apt-get install -y build-essential ${{ matrix.cross_pkg }}
```

### 3. 同步固定 Windows 与 Musl 构建环境
避免依赖环境在未来新版 Ubuntu 镜像中发生底层 glibc 或交叉库兼容性劣化。

### 4. 切勿遗漏 actions/cache 缓存键失效 (Cache Invalidation)
> [!WARNING] 跨系统 Runner 切换必踩的二级暗坑
> 如果工作流中使用了 `actions/cache` 缓存编译好的静态 OpenSSL 库，切换 Runner 到 `ubuntu-22.04` 时必须同步更新缓存键（如 `openssl-3.2-u22-...`）。
> 否则系统会复用此前在 Ubuntu 24.04/26.04 上由较新 glibc 编译的 `libcrypto.a`，该库引用了 C23 新增符号（如 `__isoc23_strtoul`、`__isoc23_sscanf`），当在 Ubuntu 22.04（glibc 2.35）上静态链接时，将引发大量 `undefined reference to '__isoc23_*'` 链接失败错误。

---

## 🧪 4. 验证方式 (Verification)
在 GitHub Actions 重新触发流水线，`gcc-mipsel-linux-gnu` 与 `gcc-mips-linux-gnu` 成功拉取并安装，交叉编译正常完成。
