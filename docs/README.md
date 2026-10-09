# 📚 bb-speedtest 文档与踩坑记录规范 (Documentation Standard)

本目录用于沉淀 `bb-speedtest` 在跨平台编译、嵌入式设备适配、网络高性能调优以及 CI/CD 自动化构建过程中的所有技术决策与避坑指南。

---

## 📐 1. 文件命名规范 (Naming Convention)

所有技术记录和踩坑文档统一采用 **`日期时间-英文或拼音短标题.md`** 的命名格式：

```text
YYYYMMDD-HHMMSS-brief-topic-title.md
```

### 示例：
- `20261009-130000-musl-static-linking-in-embedded-routers.md`
- `20261009-131500-missing-64bit-atomics-on-32bit-mips-and-arm.md`
- `20261009-141200-windows-arm64-llvm-mingw-and-winsock-compatibility.md`

---

## 📝 2. 单篇文档统一结构模板 (Document Template)

每篇踩坑或技术文档必须包含以下标准模块：

```markdown
# 📌 [文档标题]

> **文档元数据 (Metadata)**:
> - **记录时间 / Timestamp**: YYYY-MM-DD HH:MM:SS (UTC+8)
> - **作者 / Maintainer**: kxjhwbb
> - **涉及平台 / Platforms**: Linux / macOS / Windows / MIPS / ARM / RISC-V
> - **标签 / Tags**: #CrossCompile #StaticLinking #Networking #CI-CD
> - **核心概述 / Summary**: 简要说明遇到的问题与核心解决手段。

---

## 🔴 1. 踩坑现象 (Problem Symptom)
- 详细记录错误日志、报错代码、系统输出截图或异常表现。

## 🔍 2. 根本原因 (Root Cause Analysis)
- 深入分析底层机制（如 libc 解释器、硬件指令集、动态库加载、编译器 ABI 差异等）。

## ✅ 3. 解决方案 (Solution & Best Practices)
- 给出经过验证的最终代码、编译参数或配置变更。

## 🧪 4. 验证方式 (Verification)
- 给出验证命令与预期输出（如 file、readelf、otool、nm 等）。
```

---

## 🗂️ 3. 踩坑与技术文档索引 (Pitfalls Index)

| 编号 | 文档名称 | 涉及主题 | 记录时间 |
| :--- | :--- | :--- | :--- |
| 01 | [`20261009-130000-musl-static-linking-in-embedded-routers.md`](20261009-130000-musl-static-linking-in-embedded-routers.md) | 嵌入式路由器 musl libc 与动态链接 `not found` 陷阱 | 2026-10-09 13:00:00 |
| 02 | [`20261009-131500-missing-64bit-atomics-on-32bit-mips-and-arm.md`](20261009-131500-missing-64bit-atomics-on-32bit-mips-and-arm.md) | 32位架构 (MIPS/ARM32) 缺失 64位原子操作与 `-latomic` 链接 | 2026-10-09 13:15:00 |
| 03 | [`20261009-133000-openssl-cross-compilation-no-asm-compatibility.md`](20261009-133000-openssl-cross-compilation-no-asm-compatibility.md) | 跨架构交叉编译 OpenSSL 汇编冲突与 `no-asm` 纯 C 兼容方案 | 2026-10-09 13:30:00 |
| 04 | [`20261009-134500-macos-static-openssl-embedding-and-actions-runners.md`](20261009-134500-macos-static-openssl-embedding-and-actions-runners.md) | macOS 下静态嵌入 OpenSSL `.a` 归档与 Actions Runner 选型 | 2026-10-09 13:45:00 |
| 05 | [`20261009-140000-high-performance-network-engine-tuning.md`](20261009-140000-high-performance-network-engine-tuning.md) | 极低损耗 C 语言测速引擎底层调优法则 (2MB Socket / 批量原子) | 2026-10-09 14:00:00 |
| 06 | [`20261009-141200-windows-arm64-llvm-mingw-and-winsock-compatibility.md`](20261009-141200-windows-arm64-llvm-mingw-and-winsock-compatibility.md) | Windows 与 Windows ARM64 llvm-mingw 交叉编译及 Winsock 兼容 | 2026-10-09 14:12:00 |
| 07 | [`20261009-143000-github-actions-cache-acceleration.md`](20261009-143000-github-actions-cache-acceleration.md) | GitHub Actions 多架构依赖缓存加速 (3分钟降至20秒) | 2026-10-09 14:30:00 |
