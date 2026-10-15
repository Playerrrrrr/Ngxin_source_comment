
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_PROCESS_H_INCLUDED_
#define _NGX_PROCESS_H_INCLUDED_


#include <ngx_setaffinity.h>
#include <ngx_setproctitle.h>


typedef pid_t       ngx_pid_t;

#define NGX_INVALID_PID  -1

typedef void (*ngx_spawn_proc_pt) (ngx_cycle_t *cycle, void *data);

typedef struct {
    ngx_pid_t           pid;
    int                 status;
    ngx_socket_t        channel[2];//work进程的ngx_process_t[i].channel[0]==-1?是检测i号子进程是否死掉的手段

    ngx_spawn_proc_pt   proc;
    void               *data;
    char               *name;

    unsigned            respawn:1;//这个work进程能拉起来
    /*
    just_spawn 字段是一个一次性保护锁：标记某个进程是刚创建出来的，本次信号广播不要发给它。遇到时清除标记、跳过该进程。
    核心场景是 reload。看 reload 时 Master 主循环的执行顺序：
    
    ngx_start_worker_processes(cycle, ccf->worker_processes,
                            NGX_PROCESS_JUST_RESPAWN);   // ① 启动新 Worker
    ngx_start_cache_manager_processes(cycle, 1);

    ngx_msleep(100);                                        // ② 等 100ms 让新 Worker 初始化

    ngx_signal_worker_processes(cycle,
    ngx_signal_value(NGX_SHUTDOWN_SIGNAL)); // ③ 发退出信号给旧 Worker


    问题在第③步：ngx_signal_worker_processes 遍历 ngx_processes[] 数组，此时数组里既有刚创建的新 Worker，也有等待退出
    的旧 Worker。如果无差别地给每个进程发 NGX_SHUTDOWN_SIGNAL，刚拉起来的新 Worker 也会收到 quit 命令——reload 就变成自杀了。
    所以在ngx_signal_value函数里，如果看见一个进程的just_spawn==true会直接跳过
    */
    unsigned            just_spawn:1;//just == 刚刚
    unsigned            detached:1;
    /*
    exiting	exited	含义
        0	    0	正常运行
        1	    0	正在优雅关闭中（处理剩余连接）
        1	    1	被通知退出后顺利退场（正常预期）
        0	    1	意外死亡（崩溃、OOM），需要 respawn
    */
    unsigned            exiting:1;//当master让某个子进程退出的时候该标志置位1
    /*
    exited的状态修改是被worker进程死掉后内核向master进程发送的SIGCHLD信号驱动的，在ngx_process_get_status中被修改
    */
    unsigned            exited:1;//当该进程意外死亡的时候，该标志置位1
} ngx_process_t;


typedef struct {
    char         *path;
    char         *name;
    char *const  *argv;
    char *const  *envp;
} ngx_exec_ctx_t;


#define NGX_MAX_PROCESSES         1024

#define NGX_PROCESS_NORESPAWN     -1
#define NGX_PROCESS_JUST_SPAWN    -2
#define NGX_PROCESS_RESPAWN       -3
#define NGX_PROCESS_JUST_RESPAWN  -4
#define NGX_PROCESS_DETACHED      -5


#define ngx_getpid   getpid
#define ngx_getppid  getppid

#ifndef ngx_log_pid
#define ngx_log_pid  ngx_pid
#endif


ngx_pid_t ngx_spawn_process(ngx_cycle_t *cycle,
    ngx_spawn_proc_pt proc, void *data, char *name, ngx_int_t respawn);
ngx_pid_t ngx_execute(ngx_cycle_t *cycle, ngx_exec_ctx_t *ctx);
ngx_int_t ngx_init_signals(ngx_log_t *log);
void ngx_debug_point(void);


#if (NGX_HAVE_SCHED_YIELD)
#define ngx_sched_yield()  sched_yield()
#else
#define ngx_sched_yield()  usleep(1)
#endif


extern int            ngx_argc;
extern char         **ngx_argv;
extern char         **ngx_os_argv;

extern ngx_pid_t      ngx_pid;
extern ngx_pid_t      ngx_parent;
extern ngx_socket_t   ngx_channel;
extern ngx_int_t      ngx_process_slot;
extern ngx_int_t      ngx_last_process;
extern ngx_process_t  ngx_processes[NGX_MAX_PROCESSES];


#endif /* _NGX_PROCESS_H_INCLUDED_ */
