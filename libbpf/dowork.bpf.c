/* Kernel-side BPF program: trace ANY user-space function entry + return  */
/* Only needs struct pt_regs — no vmlinux.h, no BTF.                       */

struct pt_regs {
    unsigned long r15, r14, r13, r12, rbp, rbx;
    unsigned long r11, r10, r9, r8;
    unsigned long rax, rcx, rdx, rsi, rdi;      /* arg1=rdi, ret=rax  */
    unsigned long orig_rax, rip, cs, eflags, rsp, ss;
};

#include <linux/types.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "dowork.h"

char LICENSE[] SEC("license") = "Dual BSD/GPL";

/* Sampling rate: 1=all, 100=record 1% of calls. Set from userspace. */
const volatile __u64 sample_rate = 1;

/* Per-CPU call counter for sampling decision. */
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} call_cnt SEC(".maps");

/* Map: tid -> start_ns. Keyed by tid for thread-safety. */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10240);
    __type(key, __u32);
    __type(value, struct start_data);
} start_map SEC(".maps");

/* Ring buffer for kernel→user event delivery. */
struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 256 * 1024);
} events SEC(".maps");

SEC("uprobe")
int func_entry(struct pt_regs *ctx)
{
    /* Sampling decision: increment per-CPU counter, check if sampled */
    __u32 zero = 0;
    __u64 *cnt = bpf_map_lookup_elem(&call_cnt, &zero);
    if (cnt && sample_rate > 1 && (*cnt)++ % sample_rate != 0)
        return 0;   /* not sampled — skip, no start_map insert */

    struct start_data data = { .start_ns = bpf_ktime_get_ns() };
    __u32 tid = (__u32)bpf_get_current_pid_tgid();
    bpf_map_update_elem(&start_map, &tid, &data, BPF_ANY);
    return 0;
}

SEC("uretprobe")
int func_return(struct pt_regs *ctx)
{
    __u32 tid = (__u32)bpf_get_current_pid_tgid();
    struct start_data *data = bpf_map_lookup_elem(&start_map, &tid);
    if (!data)
        return 0;   /* entry was not sampled (or missed) */

    struct do_work_event *event;
    event = bpf_ringbuf_reserve(&events, sizeof(*event), 0);
    if (!event) {
        bpf_map_delete_elem(&start_map, &tid);
        return 0;
    }

    event->duration_ns = bpf_ktime_get_ns() - data->start_ns;
    event->pid         = bpf_get_current_pid_tgid() >> 32;
    event->tid         = tid;
    event->retval      = (int)PT_REGS_RC(ctx);

    bpf_ringbuf_submit(event, 0);
    bpf_map_delete_elem(&start_map, &tid);
    return 0;
}
