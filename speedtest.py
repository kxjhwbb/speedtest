#!/usr/bin/env python3
"""
HKBN / Ookla 下载测速脚本 (HKBN Speedtest Script)
使用标准库编写，无需额外安装依赖包。
"""

import sys
import time
import uuid
import argparse
import threading
import urllib.request

DEFAULT_URL = "https://speedtest21.hkbn.net.prod.hosts.ooklaserver.net:8080/download?size=25000000"

class SpeedTester:
    def __init__(self, url=DEFAULT_URL, threads=4, duration=10, timeout=10, block_size=25000000):
        self.url = url
        self.threads = threads
        self.duration = duration
        self.timeout = timeout
        self.block_size = block_size
        self.total_bytes = 0
        self.completed_blocks = 0
        self.is_running = False
        self._lock = threading.Lock()

    def _worker(self):
        while self.is_running:
            sep = "&" if "?" in self.url else "?"
            full_url = f"{self.url}{sep}nocache={uuid.uuid4()}&guid={uuid.uuid4()}"
            try:
                req = urllib.request.Request(
                    full_url,
                    headers={
                        "User-Agent": "Mozilla/5.0 (compatible; OoklaSpeedtest-Py/1.0)",
                        "Cache-Control": "no-cache",
                        "Pragma": "no-cache"
                    }
                )
                with urllib.request.urlopen(req, timeout=self.timeout) as response:
                    while self.is_running:
                        chunk = response.read(64 * 1024)
                        if not chunk:
                            with self._lock:
                                self.completed_blocks += 1
                            break
                        with self._lock:
                            self.total_bytes += len(chunk)
            except Exception:
                time.sleep(0.1)

    def run(self):
        block_mb = self.block_size / (1024 * 1024)
        print("=" * 60)
        print("🚀 开始 Ookla/HKBN 下载速度测试")
        print(f"📌 测试链接: {self.url}")
        print(f"📦 单块大小: {block_mb:.2f} MB ({self.block_size:,} 字节)")
        print(f"⚙️  并发线程: {self.threads} 线程 | 测试时长: {self.duration} 秒")
        print("=" * 60)

        self.is_running = True
        thread_list = []
        for _ in range(self.threads):
            t = threading.Thread(target=self._worker, daemon=True)
            t.start()
            thread_list.append(t)

        start_time = time.time()
        last_time = start_time
        last_bytes = 0

        try:
            while True:
                time.sleep(1)
                now = time.time()
                elapsed = now - start_time
                if elapsed >= self.duration:
                    break

                with self._lock:
                    current_bytes = self.total_bytes
                    completed_count = self.completed_blocks

                delta_bytes = current_bytes - last_bytes
                delta_time = now - last_time

                speed_mbps = (delta_bytes * 8) / (delta_time * 1024 * 1024)
                speed_mbs = delta_bytes / (delta_time * 1024 * 1024)
                blocks_float = current_bytes / self.block_size

                sys.stdout.write(
                    f"\r⏱️  进度: {int(elapsed):02d}s/{self.duration}s | "
                    f"实时: {speed_mbps:6.2f} Mbps ({speed_mbs:5.2f} MB/s) | "
                    f"📦 块个数: {blocks_float:5.1f} 个 (完整:{completed_count}个)"
                )
                sys.stdout.flush()

                last_time = now
                last_bytes = current_bytes

        except KeyboardInterrupt:
            print("\n\n⚠️ 用户手动中断测试")
        finally:
            self.is_running = False

        total_time = time.time() - start_time
        with self._lock:
            final_bytes = self.total_bytes
            final_completed = self.completed_blocks

        avg_mbps = (final_bytes * 8) / (total_time * 1024 * 1024)
        avg_mbs = final_bytes / (total_time * 1024 * 1024)
        total_mb = final_bytes / (1024 * 1024)
        total_blocks = final_bytes / self.block_size

        print("\n" + "=" * 60)
        print("📊 测试结果:")
        print(f"  • 总耗时: {total_time:.2f} 秒")
        print(f"  • 单块规格: {block_mb:.2f} MB ({self.block_size:,} 字节)")
        print(f"  • 块数统计: 折合 {total_blocks:.2f} 个 (完整下载 {final_completed} 个)")
        print(f"  • 总下载流量: {total_mb:.2f} MB ({final_bytes:,} 字节)")
        print(f"  • 平均速度: {avg_mbps:.2f} Mbps ({avg_mbs:.2f} MB/s)")
        print("=" * 60)


def main():
    parser = argparse.ArgumentParser(description="HKBN / Ookla 下载测速工具")
    parser.add_argument("-u", "--url", type=str, default=DEFAULT_URL, help="自定义测速 URL")
    parser.add_argument("-t", "--threads", type=int, default=4, help="并发下载线程数 (默认: 4)")
    parser.add_argument("-d", "--duration", type=int, default=10, help="测速持续时间/秒 (默认: 10)")
    parser.add_argument("-b", "--block-size", type=int, default=25000000, help="统计单块大小/字节 (默认: 25000000)")
    parser.add_argument("--timeout", type=int, default=10, help="单个连接超时时间/秒 (默认: 10)")
    args = parser.parse_args()

    tester = SpeedTester(url=args.url, threads=args.threads, duration=args.duration, timeout=args.timeout, block_size=args.block_size)
    tester.run()


if __name__ == "__main__":
    main()
