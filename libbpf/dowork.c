/* User-space loader: runtime attach + multi-dimension statistics          */
/*                                                                       */
/* Usage:  sudo ./dowork <binary_path> <func_name>                       */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <bpf/libbpf.h>
#include "dowork.skel.h"
#include "dowork.h"

/* -- Tracked data per event -- */
struct event {
    __u64 duration_ns;
    int   retval;
    __u32 pid;
};

static struct event *events    = NULL;
static size_t        evt_count = 0;
static size_t        evt_cap   = 0;
static volatile int  running   = 1;
static __u64         start_time_ns;  /* for QPS calculation */

/* -- Per-PID stats -- */
#define MAX_PIDS 128
static struct { __u32 pid; __u64 cnt; } pid_stats[MAX_PIDS];
static int n_pids;

/* -- Retval distribution -- */
static __u64 ret_neg, ret_zero, ret_pos;  /* negative / zero / positive */
static __u64 ret_neg1;                     /* specifically -1 (common error) */

#define US(ns) ((unsigned long long)((ns) / 1000))
#define MS(ns) ((unsigned long long)((ns) / 1000000))

/* -- Helpers -- */
static int cmp_dur(const void *a, const void *b)
{
    __u64 da = ((const struct event *)a)->duration_ns;
    __u64 db = ((const struct event *)b)->duration_ns;
    return (da > db) - (da < db);
}

static void add_event(const struct do_work_event *e)
{
    if (evt_count >= evt_cap) {
        size_t nc = evt_cap ? evt_cap * 2 : 4096;
        void *p = realloc(events, nc * sizeof(*events));
        if (!p) { fprintf(stderr, "OOM\n"); return; }
        events = p;
        evt_cap = nc;
    }
    events[evt_count].duration_ns = e->duration_ns;
    events[evt_count].retval      = e->retval;
    events[evt_count].pid         = e->pid;
    evt_count++;

    /* Per-PID */
    int i;
    for (i = 0; i < n_pids; i++)
        if (pid_stats[i].pid == e->pid) { pid_stats[i].cnt++; break; }
    if (i == n_pids && n_pids < MAX_PIDS) {
        pid_stats[n_pids].pid = e->pid;
        pid_stats[n_pids].cnt = 1;
        n_pids++;
    }

    /* Retval */
    if (e->retval < 0) {
        ret_neg++;
        if (e->retval == -1) ret_neg1++;
    } else if (e->retval == 0) {
        ret_zero++;
    } else {
        ret_pos++;
    }
}

/* -- Latency histogram (log2 buckets, like bpftrace's hist()) -- */
static void print_histogram(void)
{
    if (evt_count == 0) return;

    /* log2 buckets: [0,1) [1,2) [2,4) [4,8) ... index 0-63 */
    __u64 buckets[64] = {0};
    __u64 max_bucket = 0;
    int   max_idx    = 0;

    for (size_t i = 0; i < evt_count; i++) {
        __u64 d = events[i].duration_ns / 1000;  /* us */
        int idx;
        if (d == 0)      idx = 0;
        else if (d < 2)  idx = 1;   /* [1,2) */
        else {
            /* log2: find highest set bit */
            idx = 64 - __builtin_clzll(d);
            if (d == (1ULL << (idx - 1))) idx--;  /* exact power of 2 */
        }
        if (idx > 63) idx = 63;
        buckets[idx]++;
        if (buckets[idx] > max_bucket) { max_bucket = buckets[idx]; max_idx = idx; }
    }

    printf("\n  Latency Histogram (us, log2 scale)\n");
    printf("  %-12s %8s  %s\n", "range", "count", "");
    printf("  %-12s %8s  %s\n", "-----------", "--------", "---------------------");

    /* skip leading empty buckets, but show at most one empty for context */
    int first = 0;
    while (first <= max_idx && buckets[first] == 0) first++;
    if (first > 0) first--;

    static const char bar[] = "########################################";
    for (int i = first; i <= max_idx; i++) {
        /* skip trailing empty buckets (collapse long empty runs) */
        if (buckets[i] == 0 && i > max_idx - 3) continue;
        int bar_len = max_bucket ? (int)(40 * buckets[i] / max_bucket) : 0;
        if (bar_len == 0 && buckets[i] > 0) bar_len = 1;

        char range[48];
        if (i == 0)      snprintf(range, sizeof(range), "[0, 1)");
        else if (i == 1) snprintf(range, sizeof(range), "[1, 2)");
        else {
            __u64 lo = 1ULL << (i - 1);
            __u64 hi = 1ULL << i;
            snprintf(range, sizeof(range), "[%llu, %llu)",
                     (unsigned long long)lo, (unsigned long long)hi);
        }
        printf("  %-12s %8llu  %.*s\n", range,
               (unsigned long long)buckets[i],
               bar_len, bar);
    }
}

/* -- Percentile report -- */
static void print_latency(void)
{
    if (evt_count == 0) { printf("No events collected.\n"); return; }

    qsort(events, evt_count, sizeof(*events), cmp_dur);

    __u64 min  = events[0].duration_ns;
    __u64 max  = events[evt_count - 1].duration_ns;
    __u64 p50  = events[(size_t)(evt_count * 0.50)].duration_ns;
    __u64 p90  = events[(size_t)(evt_count * 0.90)].duration_ns;
    __u64 p99  = events[(size_t)(evt_count * 0.99)].duration_ns;
    __u64 p999 = events[(size_t)(evt_count * 0.999)].duration_ns;

    unsigned long long total = 0;
    for (size_t i = 0; i < evt_count; i++) total += events[i].duration_ns;
    __u64 avg = total / evt_count;

    printf("\n");
    printf("  %8s %8s %8s %8s %8s %10s %10s\n",
           "MIN", "AVG", "P50", "P90", "P99", "P999", "MAX");
    printf("  %8llu %8llu %8llu %8llu %8llu %10llu %10llu  (us)\n",
           US(min), US(avg), US(p50), US(p90), US(p99), US(p999), US(max));
}

/* -- Return value distribution -- */
static void print_retval(void)
{
    if (evt_count == 0) return;
    printf("\n  Return Value Distribution\n");
    printf("  %12s  %10s  %10s\n", "negative", "zero", "positive");
    printf("  %12llu  %10llu  %10llu", (unsigned long long)ret_neg,
           (unsigned long long)ret_zero, (unsigned long long)ret_pos);
    if (ret_neg1) printf("   (err=-1: %llu)", (unsigned long long)ret_neg1);
    printf("\n");
}

/* -- Per-PID breakdown -- */
static int cmp_pid(const void *a, const void *b)
{
    return ((const typeof(pid_stats[0]) *)b)->cnt
         - ((const typeof(pid_stats[0]) *)a)->cnt;
}

static void print_pid_breakdown(void)
{
    if (n_pids == 0) return;
    qsort(pid_stats, n_pids, sizeof(pid_stats[0]), cmp_pid);

    printf("\n  Top Callers (by PID)\n");
    printf("  %8s  %10s  %s\n", "PID", "CALLS", "");
    int show = n_pids < 12 ? n_pids : 12;
    for (int i = 0; i < show; i++) {
        int bar_len = (int)(20 * pid_stats[i].cnt / pid_stats[0].cnt);
        static const char pbar[] = "####################";
        printf("  %8u  %10llu  %.*s\n", pid_stats[i].pid,
               (unsigned long long)pid_stats[i].cnt,
               bar_len, pbar);
    }
}

/* -- Ring buffer callback -- */
static int handle_event(void *ctx, void *data, size_t data_sz)
{
    struct do_work_event *e = data;
    add_event(e);

    static __u64 cnt = 0;
    if (++cnt % 100 == 0)
        printf("[%llu events] latest: %8llu us (ret=%d)\n",
               (unsigned long long)cnt, US(e->duration_ns), e->retval);
    return 0;
}

static void sig_handler(int sig) { running = 0; }

static int libbpf_print_fn(enum libbpf_print_level level,
                           const char *format, va_list args)
{
    if (level == LIBBPF_DEBUG) return 0;
    return vfprintf(stderr, format, args);
}

int main(int argc, char **argv)
{
    struct dowork_bpf *skel = NULL;
    struct ring_buffer *rb  = NULL;
    const char *binary_path = NULL, *func_name = NULL;
    int err;

    __u64 sample_rate = 1; /* default: record every call */

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--sample-rate") == 0 && i + 1 < argc) {
            sample_rate = strtoull(argv[++i], NULL, 10);
            if (sample_rate < 1) sample_rate = 1;
        } else if (!binary_path) {
            binary_path = argv[i];
        } else {
            func_name = argv[i];
        }
    }

    if (!binary_path || !func_name) {
        fprintf(stderr, "Usage: sudo %s [--sample-rate N] <binary_path> <func_name>\n", argv[0]);
        return 1;
    }

    libbpf_set_print(libbpf_print_fn);
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    skel = dowork_bpf__open();
    if (!skel) { fprintf(stderr, "Failed to open BPF skeleton\n"); return 1; }

    skel->rodata->sample_rate = sample_rate;

    err = dowork_bpf__load(skel);
    if (err) { fprintf(stderr, "Failed to load: %d\n", err); goto cleanup; }

    LIBBPF_OPTS(bpf_uprobe_opts, entry_opts,
        .func_name = func_name, .retprobe = false);
    skel->links.func_entry = bpf_program__attach_uprobe_opts(
        skel->progs.func_entry, -1, binary_path, 0, &entry_opts);
    err = libbpf_get_error(skel->links.func_entry);
    if (err) {
        fprintf(stderr, "Failed to attach uprobe: %d\n", err);
        goto cleanup;
    }

    LIBBPF_OPTS(bpf_uprobe_opts, return_opts,
        .func_name = func_name, .retprobe = true);
    skel->links.func_return = bpf_program__attach_uprobe_opts(
        skel->progs.func_return, -1, binary_path, 0, &return_opts);
    err = libbpf_get_error(skel->links.func_return);
    if (err) {
        fprintf(stderr, "Failed to attach uretprobe: %d\n", err);
        goto cleanup;
    }

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    start_time_ns = ts.tv_sec * 1000000000ULL + ts.tv_nsec;

    printf("Tracing %s:%s  (Ctrl+C to stop)", binary_path, func_name);
    if (sample_rate > 1) printf(", sample_rate=1/%llu", (unsigned long long)sample_rate);
    printf("\n\n");

    rb = ring_buffer__new(bpf_map__fd(skel->maps.events),
                          handle_event, NULL, NULL);
    if (!rb) { fprintf(stderr, "Failed to create ring buffer\n"); err = -1; goto cleanup; }

    while (running) {
        err = ring_buffer__poll(rb, 100);
        if (err == -EINTR) break;
        if (err < 0) { fprintf(stderr, "Poll error: %d\n", err); break; }
    }

    /* -- Full report -- */
    clock_gettime(CLOCK_MONOTONIC, &ts);
    __u64 end_ns = ts.tv_sec * 1000000000ULL + ts.tv_nsec;
    double elapsed_s = (end_ns - start_time_ns) / 1e9;
    double qps = elapsed_s > 0 ? evt_count / elapsed_s : 0;

    printf("\n");
    printf("═══ Report: %s:%s ═══\n", binary_path, func_name);
    printf("  Duration: %.1fs    Calls: %zu    QPS: %.0f\n",
           elapsed_s, evt_count, qps);

    print_latency();
    print_histogram();
    print_retval();
    print_pid_breakdown();
    printf("\n");
    err = 0;

cleanup:
    ring_buffer__free(rb);
    dowork_bpf__destroy(skel);
    free(events);
    return err;
}
