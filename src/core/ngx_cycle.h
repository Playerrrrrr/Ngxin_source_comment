
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_CYCLE_H_INCLUDED_
#define _NGX_CYCLE_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


#ifndef NGX_CYCLE_POOL_SIZE
#define NGX_CYCLE_POOL_SIZE     NGX_DEFAULT_POOL_SIZE
#endif


#define NGX_DEBUG_POINTS_STOP   1
#define NGX_DEBUG_POINTS_ABORT  2


typedef struct ngx_shm_zone_s  ngx_shm_zone_t;

typedef ngx_int_t (*ngx_shm_zone_init_pt) (ngx_shm_zone_t *zone, void *data);

struct ngx_shm_zone_s {
    void                     *data;
    ngx_shm_t                 shm;
    ngx_shm_zone_init_pt      init;
    void                     *tag;
    void                     *sync;
    ngx_uint_t                noreuse;  /* unsigned  noreuse:1; */
};


struct ngx_cycle_s {
    /*
    当 cycle->conf_ctx 作为全局根数组时，只有 NGX_CORE_MODULE 类型的模块在这个数组里的槽位是有值的（非空），
    其他所有非 Core 模块（如 HTTP 模块、Event 模块等）在这个全局数组里的槽位全是 NULL
    在其他模块解析的时候cf->ctx（cycle->conf_ctx）指向各自的结构体
    比如配置文件解析到events关键字的时候会通过ngx_conf_handler函数调用ngx_events_block函数，其中会修改
    cf->ctx的值，使其指向ngx_http_conf_ctx_t
    */
    void                     ****conf_ctx;
    ngx_pool_t               *pool;

    ngx_log_t                *log;
    ngx_log_t                 new_log;
    /*
    ngx_memzero(&init_cycle) → log_use_stderr = 0
    ngx_use_stderr = 1（全局，初始打开）

                │  解析 nginx.conf
                │  ┌─ error_log /var/log/nginx/error.log;  → log_use_stderr 保持 0
                │  └─ error_log stderr;                    → log_use_stderr = 1
                ▼
    ngx_log_redirect_stderr():
    ├─ log_use_stderr == 1 → 跳过，不重定向
    └─ log_use_stderr == 0 → dup2(日志fd, STDERR_FILENO)

    main() 末尾：
    ├─ ngx_log_redirect_stderr(cycle)     ← 首次重定向
    ├─ ngx_close_file(初始stderr的fd)      ← 关闭早期日志
    └─ ngx_use_stderr = 0                 ← 关闭控制台回显

    reload 时：
    └─ ngx_log_redirect_stderr(new_cycle) ← dup2 到新日志 fd

    reopen 时：
    └─ ngx_log_redirect_stderr(cycle)     ← dup2 到 reopen 后的新 fd
    */
    ngx_uint_t                log_use_stderr;  /* unsigned  log_use_stderr:1; */
    //file[i]表示fd为i的socket对应的ngx_connection_t
    ngx_connection_t        **files;
    ngx_connection_t         *free_connections;
    ngx_uint_t                free_connection_n;

    ngx_module_t            **modules;
    ngx_uint_t                modules_n;
    ngx_uint_t                modules_used;    /* unsigned  modules_used:1; */

    ngx_queue_t               reusable_connections_queue;
    ngx_uint_t                reusable_connections_n;
    time_t                    connections_reuse_time;

    ngx_array_t               listening;
    ngx_array_t               paths;

    ngx_array_t               config_dump;
    ngx_rbtree_t              config_dump_rbtree;
    ngx_rbtree_node_t         config_dump_sentinel;
    /*
    ngx_list_t 链表，元素类型为 ngx_open_file_t
    其内存分配在ngx_init_cycle中
    在解析配置文件的时候，遇到error_log access_log指令填充ngx_open_file_s的fd 、name字段
    解析完配置文件也是在ngx_init_cycle打开文件
    */
    ngx_list_t                open_files;
    ngx_list_t                shared_memory;

    ngx_uint_t                connection_n;
    ngx_uint_t                files_n;

    ngx_connection_t         *connections;
    ngx_event_t              *read_events;
    ngx_event_t              *write_events;

    ngx_cycle_t              *old_cycle;

    ngx_str_t                 conf_file;
    //保存的是命令行 -g 参数传入的全局配置指令文本
    //nginx -g "daemon off; error_log stderr notice;"下conf_param = daemon off; error_log stderr notice
    ngx_str_t                 conf_param;
    //配置文件的路径
    ngx_str_t                 conf_prefix;
    //运行时合作文件的基准路径，比如/usr/local/nginx，用来和其他相对路径拼凑得到绝对路径
    ngx_str_t                 prefix;
    ngx_str_t                 error_log;
    ngx_str_t                 lock_file;
    ngx_str_t                 hostname;
};


/*
 存储所有NGX_DIRECT_CONF定义的配置
*/
typedef struct {
    ngx_flag_t                daemon;
    ngx_flag_t                master;

    ngx_msec_t                timer_resolution;
    ngx_msec_t                shutdown_timeout;

    ngx_int_t                 worker_processes;
    ngx_int_t                 debug_points;

    ngx_int_t                 rlimit_nofile;
    off_t                     rlimit_core;

    int                       priority;

    ngx_uint_t                cpu_affinity_auto;
    ngx_uint_t                cpu_affinity_n;
    ngx_cpuset_t             *cpu_affinity;

    char                     *username;
    ngx_uid_t                 user;
    ngx_gid_t                 group;

    ngx_str_t                 working_directory;
    ngx_str_t                 lock_file;

    ngx_str_t                 pid;//这里存储的是存储pid的文件的路径
    ngx_str_t                 oldpid;//oldpid=pid+".old_bin"，在reload的时候将pid文件的名字改成oldpid

    ngx_array_t               env;
    char                    **environment;

    ngx_uint_t                transparent;  /* unsigned  transparent:1; */
} ngx_core_conf_t;


#define ngx_is_init_cycle(cycle)  (cycle->conf_ctx == NULL)


ngx_cycle_t *ngx_init_cycle(ngx_cycle_t *old_cycle);
ngx_int_t ngx_create_pidfile(ngx_str_t *name, ngx_log_t *log);
void ngx_delete_pidfile(ngx_cycle_t *cycle);
ngx_int_t ngx_signal_process(ngx_cycle_t *cycle, char *sig);
void ngx_reopen_files(ngx_cycle_t *cycle, ngx_uid_t user);
char **ngx_set_environment(ngx_cycle_t *cycle, ngx_uint_t *last);
ngx_pid_t ngx_exec_new_binary(ngx_cycle_t *cycle, char *const *argv);
ngx_cpuset_t *ngx_get_cpu_affinity(ngx_uint_t n);
ngx_shm_zone_t *ngx_shared_memory_add(ngx_conf_t *cf, ngx_str_t *name,
    size_t size, void *tag);
void ngx_set_shutdown_timer(ngx_cycle_t *cycle);


extern volatile ngx_cycle_t  *ngx_cycle;
extern ngx_array_t            ngx_old_cycles;
extern ngx_module_t           ngx_core_module;
extern ngx_uint_t             ngx_test_config;
extern ngx_uint_t             ngx_dump_config;
extern ngx_uint_t             ngx_quiet_mode;


#endif /* _NGX_CYCLE_H_INCLUDED_ */
