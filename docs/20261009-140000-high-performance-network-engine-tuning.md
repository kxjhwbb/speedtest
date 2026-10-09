# 📌 极低损耗 C 语言测速引擎底层调优法则 (2MB Socket / 批量原子)

> **文档元数据 (Metadata)**:
> - **记录时间 / Timestamp**: 2026-10-09 14:00:00 (UTC+8)
> - **作者 / Maintainer**: kxjhwbb
> - **涉及平台 / Platforms**: 全平台 (Linux / macOS / Windows / 嵌入式路由器)
> - **标签 / Tags**: #HighPerformance #Networking #Socket #Atomics #ZeroCopy
> - **核心概述 / Summary**: 在 2.5G/万兆网络以及弱性能 ARM 路由器上测速时，系统瓶颈在于系统调用开销与多核 Cacheline 竞争，需采用大缓冲区与批量原子更新。

---

## 🔴 1. 踩坑现象 (Problem Symptom)
- 在千兆/2.5G 或弱 CPU 路由器（如 MT7621）上，CPU 占用率直接飙升至 100%，网络带宽跑不满。
- 多线程并发测速时，多核 CPU 出现明显的锁竞争和吞吐量下降。

---

## 🔍 2. 根本原因 (Root Cause Analysis)
1. **单字节读取 HTTP Header 导致系统调用风暴**：
   - 传统简单实现每次 `recv(fd, &ch, 1, 0)` 匹配 `\r\n\r\n`，一次 HTTP 请求触发 500+ 次内核上下文切换。
2. **默认 TCP 窗口过小**：
   - 内核默认 Socket 缓冲区仅几十 KB，在高带宽时延积（BDP）场景下导致 TCP 滑动窗口受限。
3. **全局原子变量跨核 Cacheline 乒乓（False Sharing & Cacheline Bouncing）**：
   - 所有工作线程每次读写几 KB 均调用 `atomic_fetch_add` 修改同一全局变量，引发 CPU L1/L2 缓存频繁失效。

---

## ✅ 3. 解决方案 (Solution & Best Practices)

### 1. 128KB 块读与零拷贝 Header 解析
一次性将 128KB 读入用户态缓冲区，在内存中直接使用 `strstr` 匹配 Header 结尾，并将随后的数据无缝计入 Body 流量：
```c
int n = recv(fd, recv_buf, BUF_SIZE, 0);
char *hdr_end = strstr(recv_buf, "\r\n\r\n");
if (hdr_end) {
    size_t header_len = (hdr_end + 4) - recv_buf;
    size_t body_bytes = n - header_len;
    local_bytes += body_bytes;
}
```

### 2. 显式扩大 TCP 缓冲区至 2MB 并启用 `TCP_NODELAY`
```c
int opt = 2 * 1024 * 1024;
setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (char*)&opt, sizeof(opt));
setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (char*)&opt, sizeof(opt));

int nodelay = 1;
setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, sizeof(nodelay));
```

### 3. 工作线程 1MB 批量原子累加
工作线程内部维护局部累加器，攒满 1MB 再批量同步到全局原子变量中：
```c
local_bytes += n;
if (local_bytes >= 1024 * 1024) {
    atomic_fetch_add_explicit(&g_total_bytes, local_bytes, memory_order_relaxed);
    local_bytes = 0;
}
```
