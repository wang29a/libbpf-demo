/* Shared definitions between BPF program and userspace loader */
#ifndef __DOWORK_H__
#define __DOWORK_H__

#include <linux/types.h>

/* Per-thread data stored in BPF map on function entry */
struct start_data {
    __u64 start_ns;
};

/* Event sent to userspace via ring buffer on function return */
struct do_work_event {
    __u64 duration_ns;
    __u32 pid;
    __u32 tid;
    int retval;
};

#endif
