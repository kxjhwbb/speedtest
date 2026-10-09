# 📌 GitHub Actions 多架构依赖缓存加速 (3分钟降至20秒)

> **文档元数据 (Metadata)**:
> - **记录时间 / Timestamp**: 2026-10-09 14:30:00 (UTC+8)
> - **作者 / Maintainer**: kxjhwbb
> - **涉及平台 / Platforms**: GitHub Actions CI/CD (全平台)
> - **标签 / Tags**: #GitHubActions #Cache #CI-CD #OpenSSL #BuildPerformance
> - **核心概述 / Summary**: 跨架构从源码编译 OpenSSL 占用了流水线 90% 的时间，利用 `actions/cache@v4` 持久化各架构 OpenSSL 静态产物，将整体 Release 流水线从 3 分钟压至 20~30 秒。

---

## 🔴 1. 踩坑现象 (Problem Symptom)
- GitHub Actions 发布流水线包含 11 个架构，每次发布 Release 耗时约 2.5 ~ 3.5 分钟。
- 查看各 Job 日志发现，`speedtest.c` 自身编译仅需 1~2 秒，但 OpenSSL 源码编译阶段在每个 Runner 上都要重复执行 `./Configure` 和 `make -j$(nproc)`，消耗 90~120 秒。

---

## 🔍 2. 根本原因 (Root Cause Analysis)
- GitHub Actions 每次分配的虚拟机都是全新的干净环境，不保留上一次构建生成的任何产物。
- 11 个小众架构（MIPS/RISC-V/ARM32/Windows ARM64）没有现成的 Ubuntu/Debian 静态库包，每次都必须重新解压源码编译几百个 C 文件。

---

## ✅ 3. 解决方案 (Solution & Best Practices)

使用 **`actions/cache@v4`** 对各目标架构的 OpenSSL 静态编译输出目录（`$GITHUB_WORKSPACE/openssl-target`）进行哈希缓存：

```yaml
- name: Restore Cached OpenSSL
  id: cache-openssl
  uses: actions/cache@v4
  with:
    path: ${{ github.workspace }}/openssl-target
    key: openssl-3.2-${{ matrix.arch }}-${{ matrix.target }}-${{ matrix.openssl_target }}

- name: Build Static OpenSSL for Target Architecture
  if: steps.cache-openssl.outputs.cache-hit != 'true'
  run: |
    git clone --depth 1 -b openssl-3.2 https://github.com/openssl/openssl.git openssl-src
    cd openssl-src
    ./Configure ${{ matrix.openssl_target }} ...
    make -j$(nproc) build_libs
    make install_dev
```

### 缓存特性与规则：
- **命中效果**：第 2 次及以后的构建自动命中 Cache（Cache Hit），OpenSSL 耗时直接归零（0 秒），流水线 20~30 秒内完成全平台发版。
- **生命周期**：GitHub 默认保留 7 天无访问条目，每次触发自动续期 7 天，仓库共享 10GB 免费容量。
