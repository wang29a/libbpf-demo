# dowork — libbpf 通用函数追踪

> 追踪任意用户态函数，统计延迟分位、直方图、返回值分布、调用频率

## 环境要求

```bash
sudo apt install -y clang libbpf-dev libelf-dev
```

| 包 | 用途 |
|----|------|
| clang | 编译 BPF C 代码 |
| libbpf-dev | libbpf 头文件和动态库 |
| libelf-dev | ELF 解析（libbpf 依赖） |

### bpftool

```bash
git clone https://github.com/libbpf/libbpf-bootstrap.git ~/eBPF/libbpf-bootstrap
cd ~/eBPF/libbpf-bootstrap && git submodule update --init --recursive
cd examples/c && make
```

> bpftool 随 libbpf-bootstrap 编译生成。如果 libbpf-bootstrap 装在其他路径，修改 Makefile 的 BPFTOOL 变量。

## 快速开始

```bash
make
sudo ./dowork <二进制路径> <函数名>
```

```bash
# 全量追踪
sudo ./dowork ../demo do_work

# 采样模式（高频函数用）
sudo ./dowork --sample-rate 100 ../demo do_work

# 追踪系统程序
sudo ./dowork /usr/bin/python3.12 PyEval_EvalCode
```

Ctrl+C 输出统计报告：

```
═══ Report: ../demo:do_work ═══
  Duration: 13.5s   Calls: 42   QPS: 3

     MIN      AVG      P50      P90      P99     P999      MAX
      52      147       89      458      984     1056     1056  (us)

  Latency Histogram (us, log2 scale)
  range         count
  [32, 64)           5  ████████
  [64, 128)         12  ████████████████████
  [128, 256)         5  ████████
  [256, 512)         8  █████████████

  Return Value Distribution
      negative        zero    positive
             0          42           0

  Top Callers (by PID)
     PID      CALLS
  381234          42  ████████████████████
```

## 文件结构

```
dowork.h        # 共享数据结构
dowork.bpf.c    # BPF 内核程序（uprobe + uretprobe + 采样）
dowork.c        # 用户态加载 + 统计引擎
Makefile        # 编译
```

## 关键设计

### 无 vmlinux.h

```c
/* 5 行寄存器结构，替代 2M+ 行的 vmlinux.h */
struct pt_regs {
    unsigned long r15, r14, r13, r12, rbp, rbx;
    unsigned long r11, r10, r9, r8;
    unsigned long rax, rcx, rdx, rsi, rdi;   // arg0=rdi, ret=rax
    unsigned long orig_rax, rip, cs, eflags, rsp, ss;
};
```

### 运行时 attach

目标和函数名由命令行传入，同一二进制可追踪任意程序，无需重新编译。

### 内核态采样

`--sample-rate 100` 在内核态完成采样决策——不采样的事件只做一次计数器自增，不走 start_map 和 ring buffer，高频函数也能用。

### ring buffer

结构化数据从内核直接投递到用户态，替代 bpf_printk。

### tid 做 map key

多线程安全——不同线程同时调用同一函数时，用线程 ID 区分各自的入口时间戳。

## 与 bpftrace 对比

| 维度 | bpftrace | dowork (libbpf) |
|------|----------|-----------------|
| 启动 | 一行命令 | 编译后 ./dowork |
| 部署 | 需要 bpftrace | 单个二进制 |
| 统计 | hist() 对数直方图 | P50/P90/P99/P999 + 直方图 + 返回值分布 |
| 高频函数 | 全量 | 支持采样 |
| 生产适用 | 调试/原型 | 可部署为 daemon |

## 扩展方向

- `--output json` 机器可读输出
- 丢事件计数器（ring buffer 满时告警）
- Prometheus metrics endpoint
