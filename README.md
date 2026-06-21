# eBPF Demo — 从 bpftrace 到 libbpf 的函数级追踪

> 用 eBPF 追踪用户态函数：一行 bpftrace 验证想法 → libbpf 生产级程序

## 目录

```
demo/
├── README.md             # 你在这里
├── demo.c                # 被观测的目标程序（有 do_work 函数）
├── demo                  # 编译后的二进制
├── uprobe-analysis.md    # bpftrace uprobe/uretprobe 深度分析
├── libbpf/               # libbpf 生产级追踪程序
│   ├── README.md
│   ├── dowork.h          # 共享结构体
│   ├── dowork.bpf.c      # BPF 内核程序
│   ├── dowork.c          # 用户态加载 + P50/P90/P99 统计
│   └── Makefile
└── .gitignore
```

## 快速体验

### 1. bpftrace 一行命令（原型验证）

```bash
# 编译目标程序
gcc -o demo demo.c

# 追踪 do_work 函数调用
sudo bpftrace -e '
  uprobe:./demo:do_work { @start[tid] = nsecs; }
  uretprobe:./demo:do_work /@start[tid]/ {
    $d = (nsecs - @start[tid]) / 1000;
    @lat = hist($d);
    delete(@start[tid]);
  }'
```

### 2. libbpf 生产版（精确分位统计）

```bash
cd libbpf && make

# 终端 1：运行目标程序
cd .. && ./demo

# 终端 2：追踪 do_work
sudo ./dowork ../demo do_work
```

输出：

```
  MIN      AVG      P50      P90      P99     P999      MAX
   52      174       89      458      984     1023     1023  (us)
  (156 calls)
```

### 3. 追踪任意程序

```bash
sudo ./dowork /usr/bin/python3.12 PyEval_EvalCode
```

## 文件说明

| 文件 | 内容 |
|------|------|
| `demo.c` | 被观测的 C 程序，有一个 `do_work(task_name, duration_us)` 函数 |
| `uprobe-analysis.md` | bpftrace 分析手册：P99 统计、大型软件方案、lhist/print/ring buffer 三种方式对比 |
| `libbpf/` | 完整 libbpf 项目：运行时 attach、ring buffer、百分位统计 |

## 环境要求

```bash
# bpftrace（原型验证）
sudo apt install -y bpftrace

# libbpf 编译（生产程序）
sudo apt install -y clang libbpf-dev libelf-dev
```

| 工具 | 依赖包 |
|------|--------|
| bpftrace | `bpftrace` |
| libbpf 编译 | `clang` `libbpf-dev` `libelf-dev` |

```bash
# bpftool（需先 clone 并编译 libbpf-bootstrap）
git clone https://github.com/libbpf/libbpf-bootstrap.git ~/eBPF/libbpf-bootstrap
cd ~/eBPF/libbpf-bootstrap && git submodule update --init --recursive
cd examples/c && make
```

> WSL2：确保内核支持 BTF。

## 学习路径

```
bpftrace 一行命令     →   感受 eBPF，理解 hook/map/helper
  │
  ▼
uprobe-analysis.md   →   理解 uprobe/uretprobe 原理、P99 方法、大型软件策略
  │
  ▼
libbpf/dowork         →   手写生产级 BPF 程序，编译部署，运行时 attach
```

## 环境

- WSL2 Ubuntu 24.04
- Linux 6.6.87.2-microsoft-standard-WSL2
- bpftrace v0.20.2
- clang 18 / libbpf 1.3
