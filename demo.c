#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

// 这是我们要观测的目标函数
// 接收两个参数，随机休眠几微秒模拟"工作"
int do_work(const char *task_name, int duration_us)
{
    usleep(duration_us);
    return rand() % 100;  // 返回值：模拟工作结果
}

int main()
{
    srand(time(NULL));

    printf("Demo process started, PID=%d\n", getpid());
    printf("Press Ctrl+C to stop\n\n");

    while (1) {
        // 以不同参数反复调用 do_work
        int ret = do_work("fast_task",  50 + rand() % 200);    // 50~250us
        printf("  fast_task returned: %d\n", ret);
        usleep(100000);

        do_work("slow_task", 500 + rand() % 1000);              // 500~1500us
        printf("  slow_task done\n");
        usleep(200000);
    }
    return 0;
}
