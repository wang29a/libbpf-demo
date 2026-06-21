# eBPF 函数追踪 Demo

> 用 eBPF 追踪用户态函数：bpftrace 一行命令验证想法 → libbpf 生产级统计

## 目录

```
├── README.md
├── demo.c                # 被观测的目标程序
├── uprobe-analysis.md    # bpftrace 深度分析（方法论）
└── libbpf/
    ├── README.md
    ├── dowork.h          # 共享结构体
    ├── dowork.bpf.c      # BPF 内核程序
    ├── dowork.c          # 用户态加载 + 统计引擎
    └── Makefile
```

## 环境准备

内核 >= 5.0，x86_64。

```bash
sudo apt install -y clang libbpf-dev libelf-dev

# 可选：bpftrace 快速原型
sudo apt install -y bpftrace
```

### bpftool

```bash
git clone https://github.com/libbpf/libbpf-bootstrap.git
cd libbpf-bootstrap && git submodule update --init --recursive
cd examples/c && make
```

如果 libbpf-bootstrap 装在其他路径，修改 `libbpf/Makefile` 的 BPFTOOL 变量。

## 快速上手

```bash
# 1. 编译
cd libbpf && make

# 2. 编译目标程序
cd .. && gcc -o demo demo.c

# 3. 终端1：启动目标程序
./demo

# 4. 终端2：追踪
sudo ./libbpf/dowork ./demo do_work

# 5. 高频函数加采样
sudo ./libbpf/dowork --sample-rate 100 ./demo do_work

# 6. 追踪任意程序
sudo ./libbpf/dowork /usr/bin/python3.12 PyEval_EvalCode
```

## 输出

```
═══ Report: ./demo:do_work ═══
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

QPS=每秒调用次数，P50/P90/P99/P999=延迟分位，直方图看分布形状，返回值负数=错误。

## 学习路径

```
bpftrace 一行命令     →   感受 eBPF，理解 hook/map/helper
  │
  ▼
uprobe-analysis.md   →   理解 uprobe/uretprobe、P99、大型软件方案
  │
  ▼
libbpf/dowork         →   手写生产级程序，运行时 attach，采样，多维度统计
```

## 踩坑

**1. bpftool 报 "not found for kernel"**

WSL2 内核和系统 bpftool 不匹配。用 libbpf-bootstrap 自带版本。

**2. "Failed to attach uprobe"**

函数符号不可见。用 `nm /path/to/binary | grep func` 检查符号是否存在。常见原因：二进制被 strip、函数是 static/inline、路径用了符号链接。

Python：`/usr/bin/python3` 是符号链接，用实际路径 `/usr/bin/python3.12`。

**3. 需要 sudo**

挂载 eBPF 探针需要 root 权限。

**4. P99 等于 MAX**

样本量不够时正常。100 个样本的 P99 就是最慢那一次。至少 1000 个样本再参考。

**5. 高频函数用采样**

函数每秒调用数万次以上时 ring buffer 可能溢出。`--sample-rate 100` 只记录 1% 调用，采样在内核态完成，不采样的事件零开销。

**6. 只支持 x86_64**

`dowork.bpf.c` 里 `struct pt_regs` 按 x86_64 写的。ARM64 需要改。

**7. 返回值不一定有意义**

uprobe 不知道函数实际返回类型，retval 只是 CPU 寄存器 rax 的原始值。
