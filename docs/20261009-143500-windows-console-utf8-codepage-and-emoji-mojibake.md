# 📌 Windows 控制台 UTF-8 代码页与 Emoji 乱码陷阱

> **文档元数据 (Metadata)**:
> - **记录时间 / Timestamp**: 2026-10-09 14:35:00 (UTC+8)
> - **作者 / Maintainer**: kxjhwbb
> - **涉及平台 / Platforms**: Windows (cmd.exe, PowerShell, ConHost)
> - **标签 / Tags**: #Windows #Codepage #UTF8 #Mojibake #Console
> - **核心概述 / Summary**: Windows 终端默认使用 GBK (CP936) 代码页，输出多字节 UTF-8 Emoji 会被解析为双字节中文字符引发乱码，需通过 `SetConsoleOutputCP(CP_UTF8)` 并采用通用 ASCII 标识符解决。

---

## 🔴 1. 踩坑现象 (Problem Symptom)
在中文 Windows 系统的 `cmd.exe` 或经典 PowerShell 中运行 `bb-speedtest-windows-x86_64.exe` 时，终端出现大量不相关的杂乱汉字：
```text
D:\Downloads> bb-speedtest-windows-x86_64.exe
稞播 Fetching Speedtest.net server list...
鈹?Measuring latency to 20 candidate servers...
============================================================
稞經 bb-speedtest (High Performance Native C Edition)
稞渇 Target Server: [1536] STC (Hong Kong, Hong Kong)
  Server Latency: 115.79 ms
稞象锅? Test Mode: Default (Download Only)
============================================================
------------------------------------------------------------
稞措 Running Download Speed Test...
...
鈴憋符 Download Progress: 10s/10s ...
```

---

## 🔍 2. 根本原因 (Root Cause Analysis)
1. **代码页编码不匹配**：
   - 中文版 Windows 的控制台默认代码页为 **CP936 (GBK / GB2312)**。
   - C 程序源文件中包含 3~4 字节的 UTF-8 编码 Emoji 图标（如 `🚀` 为 `\xf0\x9f\x9a\x80`，`↳` 为 `\xe2\x86\xb3`）。
   - Windows 控制台在 GBK 模式下将这 4 个字节每 2 个字节解析为一个 GBK 汉字，`\xf0\x9f` 被映射为汉字 `稞`，`\x9a\x80` 被映射为汉字 `播`，导致严重的“乱码（Mojibake）”。

---

## ✅ 3. 解决方案 (Solution & Best Practices)

### 1. 初始化设置控制台 UTF-8 代码页
在 Windows 入口处调用 Windows API 强制将输入输出代码页切换为 UTF-8（CP 65001）：
```c
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
```

### 2. 终端符号采用规范的标准 ASCII 字符
为了确保在各种极端终端环境（如老旧 Windows cmd、点阵字体、串口终端、无 Emoji 字体的精简系统）下均能 100% 清晰呈现，统一使用干净的 ASCII 标签：
- `[*] ...` 替代放大镜与闪电 Emoji
- `[!] ...` 替代警告与错误 Emoji
- `[+] ...` 替代汇总统计 Emoji
- `-> ...` 替代箭头 Emoji
- `* ...` 替代列表圆点

---

## 🧪 4. 验证方式 (Verification)
在 Windows cmd.exe 默认窗口下直接运行，输出干净清晰无任何乱码：
```text
[*] Fetching Speedtest.net server list...
[*] Measuring latency to 20 candidate servers...
============================================================
* bb-speedtest (High Performance Native C Edition)
* Target Server: [1536] STC (Hong Kong, Hong Kong)
* Server Latency: 115.79 ms
* Test Mode: Default (Download Only)
============================================================
```
