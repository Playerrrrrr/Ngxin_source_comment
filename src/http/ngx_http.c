
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

/*解析
http{
    variables_hash_max_size 114514;
}
的过程

首先看ngx_command_t结构体的字段:
struct ngx_command_s {
    ngx_str_t             name;
    ngx_uint_t            type;
    char               *(*set)(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
    ngx_uint_t            conf;
    ngx_uint_t            offset;
    void                 *post;
};
name    = variables_hash_max_size
type    = NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1
set     = ngx_conf_set_num_slot(将字符串"114514"赋值给ngx_http_core_main_conf_t的variables_hash_max_size字段)
conf    = NGX_HTTP_MAIN_CONF_OFFSET
offset  = offsetof(ngx_http_core_main_conf_t, variables_hash_max_size)
post    = null


来看conf这个字段,这个字段表示offsetof(ngx_http_conf_ctx_t, main_conf),而ngx_http_conf_ctx_t:

typedef struct {
    void        **main_conf;
    void        **srv_conf;
    void        **loc_conf;
} ngx_http_conf_ctx_t;

说明variables_hash_max_size这个参数存储在ngx_http_conf_ctx_t的main_conf中
offsetof表示variables_hash_max_size这个参数又存储中ngx_http_core_main_conf_t的variables_hash_max_size字段中
main_conf和ngx_http_core_main_conf_t是一个层级关系:

            ....._conf_t
main_conf-> ....._conf_t
            ngx_http_core_main_conf_t -> variables_hash_max_size
            ....._conf_t

这一切是怎么串起来的呢?
主要是ngx_conf_parse和ngx_conf_handler这两个函数在起作用:
首先传入nginx.conf文件给ngx_conf_parse:
http{
    variables_hash_max_size 114514;
}

然后解析到http关键字,调用ngx_conf_handler,发现ngx_http_module的ngx_http_commands命令对参数http感兴趣
接着调用ngx_http_commands对象的ngx_http_block函数.
在这个过程中,因为ngx_http_commands & NGX_MAIN_CONF == true,所以conf 被设置为 &cf->ctx[ngx_http_module.index],即指向该数组格子的地址
但是因为ngx_http_module.ctx.create_conf == null,所以在初始化cf->ctx数组的时候cf->ctx[ngx_http_module.index]为
空.所以传入ngx_http_block函数的conf指向的是一个内容为空的指针

因为http已经解析完了,所以开始解析http后面的块内容

然后到ngx_http_block函数中,这里有一个大动作: 替换cf->ctx,让其指向ngx_http_conf_ctx_t对象.这也说得过去,因为要解析
的是http块里的内容,上下文也要换一下.而且在ngx_http_block函数中把cf->module_tye设置为NGX_HTTP_MODULE,cmd_type设置为
NGX_HTTP_MAIN_CONF,这样做的目的是为了让接下来处理的的参数只能是被定义为http模块的参数.
然后ngx_http_block又递归调用ngx_conf_parse函数,开始解析下一个字段variables_hash_max_size,解析完交给ngx_conf_handler.
ngx_conf_handler发现ngx_http_core_module里的ngx_http_core_commands有命令对其感兴趣,我们记作这个命令为X.

接下来是重点

ngx_conf_handler中有一段这样的代码:


            if (cmd->type & NGX_DIRECT_CONF) {

                conf = ((void **) cf->ctx)[cf->cycle->modules[i]->index];

            } else if (cmd->type & NGX_MAIN_CONF) {
                conf = &(((void **) cf->ctx)[cf->cycle->modules[i]->index]);

            } else if (cf->ctx) {
                confp = *(void **) ((char *) cf->ctx + cmd->conf);

                if (confp) {
                    conf = confp[cf->cycle->modules[i]->ctx_index];
                }
            }


由于X命令的type是NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,所以这里走以下代码:
-

            confp = *(void **) ((char *) cf->ctx + cmd->conf);

            if (confp) {
                conf = confp[cf->cycle->modules[i]->ctx_index];
            }

第一行代码是在ngx_http_conf_ctx_t对象中进行偏移,这里是偏移到ngx_http_conf_ctx_t.main_conf(void**)中.
第二行代码是在main_conf进行偏移,其偏移量是ctx_index,即ngx_http_core_module在http模块中的编码,注意,不是
全局编号.
而这个指针就是指向ngx_http_core_main_conf_t的指针,其赋值给conf
最后把conf传递给ngx_conf_set_num_slot函数,其会利用offsetof(ngx_http_core_main_conf_t, variables_hash_max_size)
来找到要赋值的字段variables_hash_max_size

注意ngx_http_core_main_conf_t对象是在ngx_http_block函数通过各个模块的create_main_conf回调创建的
*/




    /*
     * ==========================================================================
     * ==========================================================================
     *
     *  读图约定：
     *    N      = ngx_http_max_module，即 HTTP 模块总数；
     *    [k]    = 指针数组的第 k 格。每个 HTTP 模块按自己的 ctx_index 在
     *             main_conf / srv_conf / loc_conf 三类数组中各占固定的一格；
     *    core   = ngx_http_core_module（核心模块）在数组中所占的那一格；
     *    ---->  = 指针指向。
     *
     *  --------------------------------------------------------------------------
     *  第 1 层：http{} 块自己的配置上下文（每个 http 块仅此一份）
     *  --------------------------------------------------------------------------
     *
     *            ngx_http_conf_ctx_t
     *            +------------------+
     *            |     main_conf ---+----> [0][1][2]...[N-1]  http 级 main_conf 数组
     *            |                  |                          其中 core 那格 ----> 第 2 层
     *            |      srv_conf ---+----> [0][1][2]...[N-1]  http 级 srv_conf 数组
     *            |      loc_conf ---+----> [0][1][2]...[N-1]  http 级 loc_conf 数组
     *            +------------------+
     *
     *  --------------------------------------------------------------------------
     *  第 2 层：http 级 main_conf 数组中，core 那格指向的结构
     *  --------------------------------------------------------------------------
     *
     *            ngx_http_core_main_conf_t
     *            +--------------------------------+
     *            |              ...               |
     *            |    servers : ngx_array_t       |
     *            |              ...               |
     *            +--------------------------------+
     *                        |
     *                        |  servers.elts：数组首地址；servers.nelts = server{} 个数
     *                        v
     *            +----------+----------+----------+
     *            |   [0]    |   [1]    |   ...    |     每格为一个指针，元素类型
     *            +----|-----+----|-----+-----|----+     ngx_http_core_srv_conf_t*
     *                 |          |           |
     *                 v          v           v
     *             server{}1  server{}2     ...      各自指向对应 server{} 的第 3 层结构  core那格子====> (A)
     *
     *  --------------------------------------------------------------------------
     *  第 3 层 / 第 4 层：每个 server{} 独享的两个结构（以 server{}1 为例）
     *  --------------------------------------------------------------------------
     *
     *            ngx_http_core_srv_conf_t
     *            +----------------------+
     *            |         ...          |
     *            |    ctx --------------+=====> (B) 指向下方本 server{} 的上下文
     *            |         ...          |
     *            +----------------------+
     *                      ^    ^
     *                      |    |
     *                     (A)  (C)
     *
     *            server{}1 的 ngx_http_conf_ctx_t   <===== (B)
     *            +------------------+
     *            |     main_conf ---+----> 复用第 1 层的 main_conf 数组（不新建）
     *            |      srv_conf ---+----> [0][1][2]...[N-1]  server 级 srv_conf 数组（新建）
     *            |                  |          其中 core 那格 ----> (C)
     *            |      loc_conf ---+----> [0][1][2]...[N-1]  server 级 loc_conf 数组（新建）
     *            +------------------+
     *
     *            (A) 第 2 层 servers 数组中的一格指向本结构，每个 server{} 恰占一格；
     *            (B) ctx 字段回指所属 server{} 的配置上下文；
     *            (C) server 级 srv_conf 数组中 core 那格恰好就是本结构。
     *                (A)(B)(C) 三条指针把每个 server{} 与 http{} 双向关联成闭环。
     *
     *            server 级 srv_conf / loc_conf 数组的每一格，由各模块的
     *            create_srv_conf() / create_loc_conf() 回调生成并填入，未实现
     *            回调的模块对应格为空（NULL）；server 级 loc_conf 数组同时
     *            是后续 location{} 配置继承的起点。
     *
     *  一句话总结：
     *    http{} 的 main_conf 数组全局共享；每个 server{} 新建自己的 srv_conf /
     *    loc_conf 数组与配置上下文，其中 core 那格即本 server 的
     *    ngx_http_core_srv_conf_t —— 它被收进 http{} 的 servers 数组，又用
     *    ctx 字段回指所属上下文，使 http 层与每个 server 层双向可达。
     * ==========================================================================
     */




     /*





     
     */

static char *ngx_http_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_http_init_phases(ngx_conf_t *cf,
    ngx_http_core_main_conf_t *cmcf);
static ngx_int_t ngx_http_init_headers_in_hash(ngx_conf_t *cf,
    ngx_http_core_main_conf_t *cmcf);
static ngx_int_t ngx_http_init_phase_handlers(ngx_conf_t *cf,
    ngx_http_core_main_conf_t *cmcf);

static ngx_int_t ngx_http_add_addresses(ngx_conf_t *cf,
    ngx_http_core_srv_conf_t *cscf, ngx_http_conf_port_t *port,
    ngx_http_listen_opt_t *lsopt);
static ngx_int_t ngx_http_add_address(ngx_conf_t *cf,
    ngx_http_core_srv_conf_t *cscf, ngx_http_conf_port_t *port,
    ngx_http_listen_opt_t *lsopt);
static ngx_int_t ngx_http_add_server(ngx_conf_t *cf,
    ngx_http_core_srv_conf_t *cscf, ngx_http_conf_addr_t *addr);

static char *ngx_http_merge_servers(ngx_conf_t *cf,
    ngx_http_core_main_conf_t *cmcf, ngx_http_module_t *module,
    ngx_uint_t ctx_index);
static char *ngx_http_merge_locations(ngx_conf_t *cf,
    ngx_queue_t *locations, void **loc_conf, ngx_http_module_t *module,
    ngx_uint_t ctx_index);
static ngx_int_t ngx_http_init_locations(ngx_conf_t *cf,
    ngx_http_core_srv_conf_t *cscf, ngx_http_core_loc_conf_t *pclcf);
static ngx_int_t ngx_http_init_static_location_trees(ngx_conf_t *cf,
    ngx_http_core_loc_conf_t *pclcf);
static ngx_int_t ngx_http_escape_location_name(ngx_conf_t *cf,
    ngx_http_core_loc_conf_t *clcf);
static ngx_int_t ngx_http_cmp_locations(const ngx_queue_t *one,
    const ngx_queue_t *two);
static ngx_int_t ngx_http_join_exact_locations(ngx_conf_t *cf,
    ngx_queue_t *locations);
static void ngx_http_create_locations_list(ngx_queue_t *locations,
    ngx_queue_t *q);
static ngx_http_location_tree_node_t *
    ngx_http_create_locations_tree(ngx_conf_t *cf, ngx_queue_t *locations,
    size_t prefix);

static ngx_int_t ngx_http_optimize_servers(ngx_conf_t *cf,
    ngx_http_core_main_conf_t *cmcf, ngx_array_t *ports);
static ngx_int_t ngx_http_server_names(ngx_conf_t *cf,
    ngx_http_core_main_conf_t *cmcf, ngx_http_conf_addr_t *addr);
static ngx_int_t ngx_http_cmp_conf_addrs(const void *one, const void *two);
static int ngx_libc_cdecl ngx_http_cmp_dns_wildcards(const void *one,
    const void *two);

static ngx_int_t ngx_http_init_listening(ngx_conf_t *cf,
    ngx_http_conf_port_t *port);
static ngx_listening_t *ngx_http_add_listening(ngx_conf_t *cf,
    ngx_http_conf_addr_t *addr);
static ngx_int_t ngx_http_add_addrs(ngx_conf_t *cf, ngx_http_port_t *hport,
    ngx_http_conf_addr_t *addr);
#if (NGX_HAVE_INET6)
static ngx_int_t ngx_http_add_addrs6(ngx_conf_t *cf, ngx_http_port_t *hport,
    ngx_http_conf_addr_t *addr);
#endif

ngx_uint_t   ngx_http_max_module;


ngx_http_output_header_filter_pt  ngx_http_top_header_filter;
ngx_http_output_header_filter_pt  ngx_http_top_early_hints_filter;
ngx_http_output_body_filter_pt    ngx_http_top_body_filter;
//ngx_http_top_request_body_filter目前来看只指向ngx_http_request_body_save_filter，并没有pipeline
ngx_http_request_body_filter_pt   ngx_http_top_request_body_filter;


ngx_str_t  ngx_http_html_default_types[] = {
    ngx_string("text/html"),
    ngx_null_string
};

//解析http块中的参数的cmd在ngx_http_core_module.c文件中
static ngx_command_t  ngx_http_commands[] = {

    { ngx_string("http"),
      NGX_MAIN_CONF|NGX_CONF_BLOCK|NGX_CONF_NOARGS,
      ngx_http_block,
      0,
      0,
      NULL },

      ngx_null_command
};


static ngx_core_module_t  ngx_http_module_ctx = {
    ngx_string("http"),
    NULL,
    NULL
};


ngx_module_t  ngx_http_module = {
    NGX_MODULE_V1,
    &ngx_http_module_ctx,                  /* module context */
    ngx_http_commands,                     /* module directives */
    NGX_CORE_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static char *
ngx_http_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char                        *rv;
    ngx_uint_t                   mi, m, s;
    ngx_conf_t                   pcf;
    ngx_http_module_t           *module;
    ngx_http_conf_ctx_t         *ctx;
    ngx_http_core_loc_conf_t    *clcf;
    ngx_http_core_srv_conf_t   **cscfp;
    ngx_http_core_main_conf_t   *cmcf;

    if (*(ngx_http_conf_ctx_t **) conf) {
        return "is duplicate";
    }

    /* the main http context */

    ctx = ngx_pcalloc(cf->pool, sizeof(ngx_http_conf_ctx_t));
    if (ctx == NULL) {
        return NGX_CONF_ERROR;
    }

    *(ngx_http_conf_ctx_t **) conf = ctx;


    /* count the number of the http modules and set up their indices */

    ngx_http_max_module = ngx_count_modules(cf->cycle, NGX_HTTP_MODULE);


    /* the http main_conf context, it is the same in the all http contexts */

    ctx->main_conf = ngx_pcalloc(cf->pool,
                                 sizeof(void *) * ngx_http_max_module);
    if (ctx->main_conf == NULL) {
        return NGX_CONF_ERROR;
    }


    /*
     * the http null srv_conf context, it is used to merge
     * the server{}s' srv_conf's
     */

    ctx->srv_conf = ngx_pcalloc(cf->pool, sizeof(void *) * ngx_http_max_module);
    if (ctx->srv_conf == NULL) {
        return NGX_CONF_ERROR;
    }


    /*
     * the http null loc_conf context, it is used to merge
     * the server{}s' loc_conf's
     */

    ctx->loc_conf = ngx_pcalloc(cf->pool, sizeof(void *) * ngx_http_max_module);
    if (ctx->loc_conf == NULL) {
        return NGX_CONF_ERROR;
    }


    /*
     * create the main_conf's, the null srv_conf's, and the null loc_conf's
     * of the all http modules
     */

    for (m = 0; cf->cycle->modules[m]; m++) {
        if (cf->cycle->modules[m]->type != NGX_HTTP_MODULE) {
            continue;
        }

        module = cf->cycle->modules[m]->ctx;
        mi = cf->cycle->modules[m]->ctx_index;

        if (module->create_main_conf) {
            ctx->main_conf[mi] = module->create_main_conf(cf);
            if (ctx->main_conf[mi] == NULL) {
                return NGX_CONF_ERROR;
            }
        }

        if (module->create_srv_conf) {
            ctx->srv_conf[mi] = module->create_srv_conf(cf);
            if (ctx->srv_conf[mi] == NULL) {
                return NGX_CONF_ERROR;
            }
        }

        if (module->create_loc_conf) {
            ctx->loc_conf[mi] = module->create_loc_conf(cf);
            if (ctx->loc_conf[mi] == NULL) {
                return NGX_CONF_ERROR;
            }
        }
    }

    pcf = *cf;
    cf->ctx = ctx;

    for (m = 0; cf->cycle->modules[m]; m++) {
        if (cf->cycle->modules[m]->type != NGX_HTTP_MODULE) {
            continue;
        }

        module = cf->cycle->modules[m]->ctx;

        if (module->preconfiguration) {
            if (module->preconfiguration(cf) != NGX_OK) {
                return NGX_CONF_ERROR;
            }
        }
    }

    /* parse inside the http{} block */

    cf->module_type = NGX_HTTP_MODULE;
    cf->cmd_type = NGX_HTTP_MAIN_CONF;
    rv = ngx_conf_parse(cf, NULL);

    if (rv != NGX_CONF_OK) {
        goto failed;
    }

    /*
     * init http{} main_conf's, merge the server{}s' srv_conf's
     * and its location{}s' loc_conf's
     */

    cmcf = ctx->main_conf[ngx_http_core_module.ctx_index];
    cscfp = cmcf->servers.elts;

    for (m = 0; cf->cycle->modules[m]; m++) {
        if (cf->cycle->modules[m]->type != NGX_HTTP_MODULE) {
            continue;
        }

        module = cf->cycle->modules[m]->ctx;
        mi = cf->cycle->modules[m]->ctx_index;

        /* init http{} main_conf's */

        if (module->init_main_conf) {
            rv = module->init_main_conf(cf, ctx->main_conf[mi]);
            if (rv != NGX_CONF_OK) {
                goto failed;
            }
        }

        rv = ngx_http_merge_servers(cf, cmcf, module, mi);
        if (rv != NGX_CONF_OK) {
            goto failed;
        }
    }


    /* create location trees */

    for (s = 0; s < cmcf->servers.nelts; s++) {

        clcf = cscfp[s]->ctx->loc_conf[ngx_http_core_module.ctx_index];

        if (ngx_http_init_locations(cf, cscfp[s], clcf) != NGX_OK) {
            return NGX_CONF_ERROR;
        }
        //这里clcf->locations只下inclusive[前缀匹配^~ ab和通用匹配/ab]的节点了
        //构建的tree挂在clcf->static_location下面
        //进入 init_static_location_trees 时，队列里只剩“能靠字符串比较判定”的 location（前缀、^~，加上挂 exact 槽的 =）
        //去建树；而“要靠运行期计算判定”的 regex/predicate/named 已经分别进了三个数组，noname 这种“不参与 URI 匹配”的伪
        //location 则被彻底移出检索体系。
        if (ngx_http_init_static_location_trees(cf, clcf) != NGX_OK) {
            return NGX_CONF_ERROR;
        }
    }


    if (ngx_http_init_phases(cf, cmcf) != NGX_OK) {//初始化cmcf->phases数组
        return NGX_CONF_ERROR;
    }

    if (ngx_http_init_headers_in_hash(cf, cmcf) != NGX_OK) {
        return NGX_CONF_ERROR;
    }


    for (m = 0; cf->cycle->modules[m]; m++) {
        if (cf->cycle->modules[m]->type != NGX_HTTP_MODULE) {
            continue;
        }

        module = cf->cycle->modules[m]->ctx;

        if (module->postconfiguration) {
            if (module->postconfiguration(cf) != NGX_OK) {//往phase阶段加入自己的handler
                return NGX_CONF_ERROR;
            }
        }
    }

    if (ngx_http_variables_init_vars(cf) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    /*
     * http{}'s cf->ctx was needed while the configuration merging
     * and in postconfiguration process
     */

    *cf = pcf;


    if (ngx_http_init_phase_handlers(cf, cmcf) != NGX_OK) {
        return NGX_CONF_ERROR;
    }


    /* optimize the lists of ports, addresses and server names */

    if (ngx_http_optimize_servers(cf, cmcf, cmcf->ports) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;

failed:

    *cf = pcf;

    return rv;
}


static ngx_int_t
ngx_http_init_phases(ngx_conf_t *cf, ngx_http_core_main_conf_t *cmcf)
{
    if (ngx_array_init(&cmcf->phases[NGX_HTTP_POST_READ_PHASE].handlers,
                       cf->pool, 1, sizeof(ngx_http_handler_pt))
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    if (ngx_array_init(&cmcf->phases[NGX_HTTP_SERVER_REWRITE_PHASE].handlers,
                       cf->pool, 1, sizeof(ngx_http_handler_pt))
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    if (ngx_array_init(&cmcf->phases[NGX_HTTP_REWRITE_PHASE].handlers,
                       cf->pool, 1, sizeof(ngx_http_handler_pt))
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    if (ngx_array_init(&cmcf->phases[NGX_HTTP_PREACCESS_PHASE].handlers,
                       cf->pool, 1, sizeof(ngx_http_handler_pt))
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    if (ngx_array_init(&cmcf->phases[NGX_HTTP_ACCESS_PHASE].handlers,
                       cf->pool, 2, sizeof(ngx_http_handler_pt))
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    if (ngx_array_init(&cmcf->phases[NGX_HTTP_PRECONTENT_PHASE].handlers,
                       cf->pool, 2, sizeof(ngx_http_handler_pt))
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    if (ngx_array_init(&cmcf->phases[NGX_HTTP_CONTENT_PHASE].handlers,
                       cf->pool, 4, sizeof(ngx_http_handler_pt))
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    if (ngx_array_init(&cmcf->phases[NGX_HTTP_LOG_PHASE].handlers,
                       cf->pool, 1, sizeof(ngx_http_handler_pt))
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    return NGX_OK;
}

//用全局ngx_http_headers_in初始化cmcf->headers_in_hash哈希表
static ngx_int_t
ngx_http_init_headers_in_hash(ngx_conf_t *cf, ngx_http_core_main_conf_t *cmcf)
{
    ngx_array_t         headers_in;
    ngx_hash_key_t     *hk;
    ngx_hash_init_t     hash;
    ngx_http_header_t  *header;

    if (ngx_array_init(&headers_in, cf->temp_pool, 32, sizeof(ngx_hash_key_t))
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    for (header = ngx_http_headers_in; header->name.len; header++) {
        hk = ngx_array_push(&headers_in);
        if (hk == NULL) {
            return NGX_ERROR;
        }

        hk->key = header->name;
        hk->key_hash = ngx_hash_key_lc(header->name.data, header->name.len);
        hk->value = header;
    }

    hash.hash = &cmcf->headers_in_hash;
    hash.key = ngx_hash_key_lc;
    hash.max_size = 512;
    hash.bucket_size = ngx_align(64, ngx_cacheline_size);
    hash.name = "headers_in_hash";
    hash.pool = cf->pool;
    hash.temp_pool = NULL;

    if (ngx_hash_init(&hash, headers_in.elts, headers_in.nelts) != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_init_phase_handlers(ngx_conf_t *cf, ngx_http_core_main_conf_t *cmcf)
{
    ngx_int_t                   j;
    ngx_uint_t                  i, n;
    ngx_uint_t                  find_config_index, use_rewrite, use_access;
    ngx_http_handler_pt        *h;
    ngx_http_phase_handler_t   *ph;
    ngx_http_phase_handler_pt   checker;

    cmcf->phase_engine.server_rewrite_index = (ngx_uint_t) -1;
    cmcf->phase_engine.location_rewrite_index = (ngx_uint_t) -1;
    find_config_index = 0;
    use_rewrite = cmcf->phases[NGX_HTTP_REWRITE_PHASE].handlers.nelts ? 1 : 0;
    use_access = cmcf->phases[NGX_HTTP_ACCESS_PHASE].handlers.nelts ? 1 : 0;

    n = 1                  /* find config phase */
        + use_rewrite      /* post rewrite phase */
        + use_access;      /* post access phase */

    for (i = 0; i < NGX_HTTP_LOG_PHASE; i++) {
        n += cmcf->phases[i].handlers.nelts;
    }

    ph = ngx_pcalloc(cf->pool,
                     n * sizeof(ngx_http_phase_handler_t) + sizeof(void *));
    if (ph == NULL) {
        return NGX_ERROR;
    }

    cmcf->phase_engine.handlers = ph;
    n = 0;
/*

下面那些没有checker的phase都是让checker自己干活，没用调用handler

下标  checker              handler(module)          next
─────────────────────────────────────────────────────────────
 [0]  rewrite_checker      rewrite_handler           1   ← server_rewrite_index=0
 [1]  find_config_checker  (无，自己做location匹配)   0   ← find_config_index=1,这个checker（ngx_http_core_find_config_phase）会自己调用r->phase_hanlder++，所以不会死循环
 [2]  rewrite_checker      rewrite_handler           3   ← location_rewrite_index=2
 [3]  post_rewrite_checker (无)                      1   ← 唯一回边：uri_changed→跳回[1]
 [4]  generic_checker      limit_req_handler         6
 [5]  generic_checker      limit_conn_handler        6
 [6]  access_checker       access_handler            9
 [7]  access_checker       auth_basic_handler        9
 [8]  post_access_checker  (无)                      9   ← 检查r->access_code，有则终结
 [9]  generic_checker      try_files_handler         11
[10]  generic_checker      mirror_handler            11
[11]  content_checker      index_handler             14
[12]  content_checker      autoindex_handler         14
[13]  content_checker      static_handler            14
[14]  NULL（哨兵，alloc 时多留的 sizeof(void*)），循环在此终止
*/
    for (i = 0; i < NGX_HTTP_LOG_PHASE; i++) {
        h = cmcf->phases[i].handlers.elts;//第i个阶段的模块

        switch (i) {

        case NGX_HTTP_SERVER_REWRITE_PHASE:
            if (cmcf->phase_engine.server_rewrite_index == (ngx_uint_t) -1) {
                cmcf->phase_engine.server_rewrite_index = n;
            }
            checker = ngx_http_core_rewrite_phase;

            break;

        case NGX_HTTP_FIND_CONFIG_PHASE:
            find_config_index = n;

            ph->checker = ngx_http_core_find_config_phase;
            n++;
            ph++;

            continue;

        case NGX_HTTP_REWRITE_PHASE:
            if (cmcf->phase_engine.location_rewrite_index == (ngx_uint_t) -1) {
                cmcf->phase_engine.location_rewrite_index = n;
            }
            checker = ngx_http_core_rewrite_phase;

            break;

        case NGX_HTTP_POST_REWRITE_PHASE:
            if (use_rewrite) {
                ph->checker = ngx_http_core_post_rewrite_phase;
                ph->next = find_config_index;
                n++;
                ph++;
            }

            continue;

        case NGX_HTTP_ACCESS_PHASE:
            checker = ngx_http_core_access_phase;
            n++;
            break;

        case NGX_HTTP_POST_ACCESS_PHASE:
            if (use_access) {
                ph->checker = ngx_http_core_post_access_phase;
                ph->next = n;
                ph++;
            }

            continue;

        case NGX_HTTP_CONTENT_PHASE:
            checker = ngx_http_core_content_phase;
            break;

        default:
            checker = ngx_http_core_generic_phase;
        }

        n += cmcf->phases[i].handlers.nelts;

        for (j = cmcf->phases[i].handlers.nelts - 1; j >= 0; j--) {
            ph->checker = checker;
            ph->handler = h[j];
            ph->next = n;
            ph++;
        }
    }

    return NGX_OK;
}


static char *
ngx_http_merge_servers(ngx_conf_t *cf, ngx_http_core_main_conf_t *cmcf,
    ngx_http_module_t *module, ngx_uint_t ctx_index)
{
    char                        *rv;
    ngx_uint_t                   s;
    ngx_http_conf_ctx_t         *ctx, saved;
    ngx_http_core_loc_conf_t    *clcf;
    ngx_http_core_srv_conf_t   **cscfp;

    cscfp = cmcf->servers.elts;
    ctx = (ngx_http_conf_ctx_t *) cf->ctx;
    saved = *ctx;
    rv = NGX_CONF_OK;

    for (s = 0; s < cmcf->servers.nelts; s++) {

        /* merge the server{}s' srv_conf's */

        ctx->srv_conf = cscfp[s]->ctx->srv_conf;

        if (module->merge_srv_conf) {
            rv = module->merge_srv_conf(cf, saved.srv_conf[ctx_index],
                                        cscfp[s]->ctx->srv_conf[ctx_index]);
            if (rv != NGX_CONF_OK) {
                goto failed;
            }
        }

        if (module->merge_loc_conf) {

            /* merge the server{}'s loc_conf */

            ctx->loc_conf = cscfp[s]->ctx->loc_conf;

            //合并server层和http层的location
            rv = module->merge_loc_conf(cf, saved.loc_conf[ctx_index],
                                        cscfp[s]->ctx->loc_conf[ctx_index]);
            if (rv != NGX_CONF_OK) {
                goto failed;
            }

            /* merge the locations{}' loc_conf's */

            clcf = cscfp[s]->ctx->loc_conf[ngx_http_core_module.ctx_index];
            //cscfp[s]->ctx->loc_conf   ：传入server层的loc_conf
            //clcf->locations           ：传入location层的loc_conf
            //注意，上面已经把server层和http层的loc_conf给合并了，所以这里传cf更多是传入其他上下文
            rv = ngx_http_merge_locations(cf, clcf->locations,
                                          cscfp[s]->ctx->loc_conf,
                                          module, ctx_index);
            if (rv != NGX_CONF_OK) {
                goto failed;
            }
        }
    }

failed:

    *ctx = saved;

    return rv;
}

//这个函数是个递归函数
//其语义是：将loc_conf作为父层，将其与ngx_queue_t中的loc_conf合并
static char *
ngx_http_merge_locations(ngx_conf_t *cf, ngx_queue_t *locations,
    void **loc_conf, ngx_http_module_t *module, ngx_uint_t ctx_index)
{
    char                       *rv;
    ngx_queue_t                *q;
    ngx_http_conf_ctx_t        *ctx, saved;
    ngx_http_core_loc_conf_t   *clcf;
    ngx_http_location_queue_t  *lq;

    if (locations == NULL) {
        return NGX_CONF_OK;
    }

    ctx = (ngx_http_conf_ctx_t *) cf->ctx;
    saved = *ctx;

    
    for (q = ngx_queue_head(locations);
         q != ngx_queue_sentinel(locations);
         q = ngx_queue_next(q))
    {
        lq = (ngx_http_location_queue_t *) q;

        clcf = lq->exact ? lq->exact : lq->inclusive;
        ctx->loc_conf = clcf->loc_conf;

        rv = module->merge_loc_conf(cf, loc_conf[ctx_index],
                                    clcf->loc_conf[ctx_index]);
        if (rv != NGX_CONF_OK) {
            return rv;
        }
        /*
        处理location里包location的情况
        location{
            location{
                ....
            }
        }
        */
        rv = ngx_http_merge_locations(cf, clcf->locations, clcf->loc_conf,
                                      module, ctx_index);
        if (rv != NGX_CONF_OK) {
            return rv;
        }
    }

    *ctx = saved;

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_init_locations(ngx_conf_t *cf, ngx_http_core_srv_conf_t *cscf,
    ngx_http_core_loc_conf_t *pclcf)
{
    ngx_uint_t                   n, p;
    ngx_queue_t                 *q, *locations, *named, *predicate, tail;
    ngx_http_core_loc_conf_t    *clcf;
    ngx_http_location_queue_t   *lq;
    ngx_http_core_loc_conf_t   **clcfp;
#if (NGX_PCRE)
    ngx_uint_t                   r;
    ngx_queue_t                 *regex;
#endif

    locations = pclcf->locations;

    if (locations == NULL) {
        return NGX_OK;
    }
    /*
    regex	location ~ 模式 / location ~* 模式
    predicate	location $变量 {...}
    named	location @名字 {...}
    */
    //exact放在inclusive前面
    ngx_queue_sort(locations, ngx_http_cmp_locations);

    named = NULL;
    n = 0;
#if (NGX_PCRE)
    regex = NULL;
    r = 0;
#endif
    predicate = NULL;
    p = 0;
    /*
    下面就做两件事：
        1：找到predicate、noname、name节点、正则节点的第一个节点
        2：把节点name节点赋值给ngx_http_core_srv_conf_t.named_locations
           把predicate节点赋值给ngx_http_core_loc_conf_t.predicate_locations
           把正则节点赋值给ngx_http_core_loc_conf_t.regex_locations
    剩下的就是/abc  = /abc ...类似的节点了，这些会被构建成wildcard hash表
    */
    for (q = ngx_queue_head(locations);
         q != ngx_queue_sentinel(locations);
         q = ngx_queue_next(q))
    {
        lq = (ngx_http_location_queue_t *) q;

        clcf = lq->exact ? lq->exact : lq->inclusive;
        //递归排序location块里的locations
        if (ngx_http_init_locations(cf, NULL, clcf) != NGX_OK) {
            return NGX_ERROR;
        }

#if (NGX_PCRE)
        //计数有多少个regex
        if (clcf->regex) {
            r++;

            if (regex == NULL) {//regex的尾指针
                regex = q;
            }

            continue;
        }

#endif
        //计数有多少个predicate
        if (clcf->predicate) {
            p++;

            if (predicate == NULL) {//predicate的尾指针
                predicate = q;
            }

            continue;
        }

        if (clcf->named) {//是否是location
            n++;

            if (named == NULL) {
                named = q;
            }

            continue;
        }

        if (clcf->noname) {//因为noname是最后面的
            break;
        }
    }
    //此时q指向第一个noname
    if (q != ngx_queue_sentinel(locations)) {
        ngx_queue_split(locations, q, &tail);//从q开始分裂队列
        //此时tail为一个只有noname的队列的头节点
    }
    //把queue中的named节点摘出来，存放到(ngx_http_core_srv_conf*)cscf->named_locations中
    if (named) {
        clcfp = ngx_palloc(cf->pool,
                           (n + 1) * sizeof(ngx_http_core_loc_conf_t *));

        if (clcfp == NULL) {//named只能是server里的location用，所以放在server层管理
            return NGX_ERROR;
        }

        cscf->named_locations = clcfp;

        for (q = named;
             q != ngx_queue_sentinel(locations);
             q = ngx_queue_next(q))
        {
            lq = (ngx_http_location_queue_t *) q;

            *(clcfp++) = lq->exact;
        }

        *clcfp = NULL;

        ngx_queue_split(locations, named, &tail);
    }

    if (predicate) {
        clcfp = ngx_palloc(cf->pool,
                           (p + 1) * sizeof(ngx_http_core_loc_conf_t *));
        if (clcfp == NULL) {
            return NGX_ERROR;
        }

        pclcf->predicate_locations = clcfp;

        for (q = predicate;
             q != ngx_queue_sentinel(locations);
             q = ngx_queue_next(q))
        {
            lq = (ngx_http_location_queue_t *) q;

            *(clcfp++) = lq->exact;
        }

        *clcfp = NULL;

        ngx_queue_split(locations, predicate, &tail);
    }

#if (NGX_PCRE)

    if (regex) {

        clcfp = ngx_palloc(cf->pool,
                           (r + 1) * sizeof(ngx_http_core_loc_conf_t *));
        if (clcfp == NULL) {
            return NGX_ERROR;
        }

        pclcf->regex_locations = clcfp;

        for (q = regex;
             q != ngx_queue_sentinel(locations);
             q = ngx_queue_next(q))
        {
            lq = (ngx_http_location_queue_t *) q;

            *(clcfp++) = lq->exact;
        }

        *clcfp = NULL;

        ngx_queue_split(locations, regex, &tail);
    }

#endif

    return NGX_OK;
}


static ngx_int_t
ngx_http_init_static_location_trees(ngx_conf_t *cf,
    ngx_http_core_loc_conf_t *pclcf)
{
    ngx_queue_t                *q, *locations;
    ngx_http_core_loc_conf_t   *clcf, **clcfp;
    ngx_http_location_queue_t  *lq;
    /*
    location $var {} 是一个完整的上下文——它和 location /a {} 一样，里面可以嵌套普通前缀、
    =、正则、甚至更多$var。嵌套的这些子location需要属于它自己上下文的static_locations树。
    */
    if (pclcf->predicate_locations) {
        for (clcfp = pclcf->predicate_locations; *clcfp; clcfp++) {
            if (ngx_http_init_static_location_trees(cf, *clcfp) != NGX_OK) {
                return NGX_ERROR;
            }
        }
    }

    locations = pclcf->locations;

    if (locations == NULL) {
        return NGX_OK;
    }

    if (ngx_queue_empty(locations)) {
        return NGX_OK;
    }

    for (q = ngx_queue_head(locations);
         q != ngx_queue_sentinel(locations);
         q = ngx_queue_next(q))
    {
        lq = (ngx_http_location_queue_t *) q;

        clcf = lq->exact ? lq->exact : lq->inclusive;

        if (ngx_http_init_static_location_trees(cf, clcf) != NGX_OK) {
            return NGX_ERROR;
        }
    }

    if (ngx_http_join_exact_locations(cf, locations) != NGX_OK) {
        return NGX_ERROR;
    }

    ngx_http_create_locations_list(locations, ngx_queue_head(locations));

    pclcf->static_locations = ngx_http_create_locations_tree(cf, locations, 0);
    if (pclcf->static_locations == NULL) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


ngx_int_t
ngx_http_add_location(ngx_conf_t *cf, ngx_queue_t **locations,
    ngx_http_core_loc_conf_t *clcf)
{
    ngx_http_location_queue_t  *lq;
    /*
    locations是惰性创建，所以必须是二级指针
    */
    if (*locations == NULL) {
        *locations = ngx_palloc(cf->temp_pool,
                                sizeof(ngx_http_location_queue_t));
        if (*locations == NULL) {
            return NGX_ERROR;
        }

        ngx_queue_init(*locations);
    }

    lq = ngx_palloc(cf->temp_pool, sizeof(ngx_http_location_queue_t));
    if (lq == NULL) {
        return NGX_ERROR;
    }

    if (clcf->exact_match
#if (NGX_PCRE)
        || clcf->regex
#endif
        || clcf->predicate || clcf->named || clcf->noname)
    {
        /* “整串相等才成立”类：=、正则、@、变量、内部 */
        lq->exact = clcf;
        lq->inclusive = NULL;

    } else {
        /* “前缀包含即成立”类：普通前缀、^~ */
        lq->exact = NULL;
        lq->inclusive = clcf;
    }

    lq->name = &clcf->name;
    lq->file_name = cf->conf_file->file.name.data;
    lq->line = cf->conf_file->line;

    ngx_queue_init(&lq->list);//

    ngx_queue_insert_tail(*locations, &lq->queue);

    if (ngx_http_escape_location_name(cf, clcf) != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_escape_location_name(ngx_conf_t *cf, ngx_http_core_loc_conf_t *clcf)
{
    u_char     *p;
    size_t      len;
    uintptr_t   escape;

    escape = 2 * ngx_escape_uri(NULL, clcf->name.data, clcf->name.len,
                                NGX_ESCAPE_URI);

    if (escape) {
        len = clcf->name.len + escape;

        p = ngx_pnalloc(cf->pool, len);
        if (p == NULL) {
            return NGX_ERROR;
        }

        clcf->escaped_name.len = len;
        clcf->escaped_name.data = p;

        ngx_escape_uri(p, clcf->name.data, clcf->name.len, NGX_ESCAPE_URI);

    } else {
        clcf->escaped_name = clcf->name;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_cmp_locations(const ngx_queue_t *one, const ngx_queue_t *two)
{
    ngx_int_t                   rc;
    ngx_http_core_loc_conf_t   *first, *second;
    ngx_http_location_queue_t  *lq1, *lq2;

    lq1 = (ngx_http_location_queue_t *) one;
    lq2 = (ngx_http_location_queue_t *) two;

    first = lq1->exact ? lq1->exact : lq1->inclusive;
    second = lq2->exact ? lq2->exact : lq2->inclusive;

    if (first->noname && !second->noname) { 
        /* shift no named locations to the end */
        return 1;
    }

    if (!first->noname && second->noname) {
        /* shift no named locations to the end */
        return -1;
    }

    if (first->noname || second->noname) {
        /* do not sort no named locations */
        return 0;
    }

    if (first->named && !second->named) {
        /* shift named locations to the end */
        return 1;
    }

    if (!first->named && second->named) {
        /* shift named locations to the end */
        return -1;
    }

    if (first->named && second->named) {
        return ngx_strcmp(first->name.data, second->name.data);
    }

    if (first->predicate && !second->predicate) {
        /* shift predicate locations to the end */
        return 1;
    }

    if (!first->predicate && second->predicate) {
        /* shift predicate locations to the end */
        return -1;
    }

    if (first->predicate || second->predicate) {
        /* do not sort the predicate locations */
        return 0;
    }

#if (NGX_PCRE)

    if (first->regex && !second->regex) {
        /* shift the regex matches to the end */
        return 1;
    }

    if (!first->regex && second->regex) {
        /* shift the regex matches to the end */
        return -1;
    }

    if (first->regex || second->regex) {
        /* do not sort the regex matches */
        return 0;
    }

#endif

    rc = ngx_filename_cmp(first->name.data, second->name.data,
                          ngx_min(first->name.len, second->name.len) + 1);

    if (rc == 0 && !first->exact_match && second->exact_match) {
        /* an exact match must be before the same inclusive one */
        return 1;
    }

    return rc;
}

/*
 * ngx_http_join_exact_locations() —— 同名合并
 *
 * 前提：ngx_queue_sort(ngx_http_cmp_locations) 已把队列按名字排好，
 *       同名的两条一定相邻，且 exact(=) 一定排在 inclusive 前面。
 *
 * 职责：a) 把相邻同名的 "= /a"(exact 槽) 与 "/a"(inclusive 槽) 合并成
 *          一条 exact+inclusive 双指针记录 —— 一个名字将来只建一个树节点；
 *       b) 同槽同名（配置真写重了）在此报 "duplicate location"。
 *
 * 记号：E = exact 槽（location = /a）；I = inclusive 槽（location /a、^~ /a）
 *
 * --------------------------------------------------------------------------
 * 例 1：合法配置的完整合并过程
 * --------------------------------------------------------------------------
 *
 *      location = /a     { ... }              <- E
 *      location  /a      { ... }              <- I，与上一条同名
 *      location  /a/b    { ... }              <- I
 *      location  /a/bc   { ... }              <- I
 *      location  /b      { ... }              <- I
 *
 *      排序后的队列（同名相邻，E 在前）：
 *
 *          [/a E] --> [/a I] --> [/a/b I] --> [/a/bc I] --> [/b I]
 *
 *      第 1 轮   q = [/a E]        x = [/a I]
 *                名字相同（长度相等且逐字节相同），槽位互补（一 E 一 I）
 *                => lq->inclusive = lx->inclusive，合并为 [/a E+I]
 *                   x 出队；q 不前进，继续和新邻居比较
 *
 *      第 2 轮   q = [/a E+I]      x = [/a/b I]
 *                名字不同（长度 2 != 4）=> q 前进
 *
 *      第 3 轮   q = [/a/b I]      x = [/a/bc I]
 *                名字不同（长度 4 != 5）=> q 前进
 *
 *      第 4 轮   q = [/a/bc I]     x = [/b I]
 *                名字不同 => q 前进；q 到达队尾（/b），循环结束
 *
 *      产出队列：
 *
 *          [/a E+I] --> [/a/b I] --> [/a/bc I] --> [/b I]
 *
 *      交给下一步 create_locations_list() 按前缀包含关系挂成嵌套链：
 *
 *          /a(E+I) -list-> /a/b -list-> /a/bc          /b（留在外层队列）
 *
 *      建树后（create_locations_tree()）请求期的效果：
 *
 *          URI /a      -> 整串等于 /a 的 E  -> 立即命中返回，跳过正则
 *          URI /a/bc/x -> 依次命中 /a(I) -> /a/b(I) -> /a/bc(I)
 *                         每下一层覆盖上一层，最长前缀 /a/bc 胜出
 *          URI /b      -> /b(I)
 *
 * --------------------------------------------------------------------------
 * 例 2：非法配置 —— 同槽同名，报 "duplicate location"
 * --------------------------------------------------------------------------
 *
 *      location /a     +  location /a       两个 I 槽同名   => 冲突
 *      location = /a   +  location = /a     两个 E 槽同名   => 冲突
 *      location ^~ /a  +  location /a       ^~ 也占 I 槽    => 冲突
 *      location = /a   +  location /a       E 与 I 互补     => 合法（即例 1）
 *
 * --------------------------------------------------------------------------
 * 备注：
 *   1) 合并方向固定为 "把 x 的 I 搬进 q"：排序保证 E 在前，
 *      所以 lq->inclusive = lx->inclusive 不会把有效配置覆盖成 NULL；
 *      若同名出现第三次，因 q 合并后不前进，下一轮即同槽相遇 -> 报错；
 *   2) ngx_filename_cmp 在大小写不敏感文件系统（win32 / mac 默认）
 *      上忽略大小写，/A 与 /a 也会被判为同名；Linux 上是两个 location；
 *   3) 名字比较先比长度再比内容：ngx_str_t 的 data 不保证以 NUL 结尾，
 *      必须按精确长度比较，不能交给 strcmp。
 */
static ngx_int_t
ngx_http_join_exact_locations(ngx_conf_t *cf, ngx_queue_t *locations)
{
    ngx_queue_t                *q, *x;
    ngx_http_location_queue_t  *lq, *lx;

    q = ngx_queue_head(locations);

    while (q != ngx_queue_last(locations)) {

        x = ngx_queue_next(q);

        lq = (ngx_http_location_queue_t *) q;
        lx = (ngx_http_location_queue_t *) x;

        /*
        ngx_http_cmp_locations 排序时有一条专门规则（1255-1257 行）：名字相同时，exact必须排在 inclusive 前面。
        所以进入这个函数时，同名项一定紧挨着
        */
       /*
       这个逻辑是将队列中同名的exact和inclusive节点合并，并把inclusive节点删掉
       */
        if (lq->name->len == lx->name->len
            && ngx_filename_cmp(lq->name->data, lx->name->data, lx->name->len)
               == 0)
        {
            //一个名字只允许至多一个 exact + 至多一个 inclusive
            /*
            location =  /a      +   location /a	        ✔ 合法，正是要合并的对象
            location    /a      +   location /a	        ✘ duplicate
            location =  /a      +   location = /a	    ✘ duplicate
            location ^~ /a      +   location /a	        ✘ duplicate（^~ 也占 inclusive 槽，两者同槽同名）
            */
            if ((lq->exact && lx->exact) || (lq->inclusive && lx->inclusive)) {
                ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                              "duplicate location \"%V\" in %s:%ui",
                              lx->name, lx->file_name, lx->line);

                return NGX_ERROR;
            }

            lq->inclusive = lx->inclusive;/* 把后者的 inclusive 搬进前者 */

            ngx_queue_remove(x);//删掉exclusive节点

            continue;
            /*
            走个例子

            location = /a  { ... }
            location  /a  { ... }
            location  /a/b { ... }
            location  /b  { ... }
            排序后：[/a(exact), /a(inclusive), /a/b, /b]

            轮次	    q	x	动作
            1	    /a(exact)	/a(inclusive)	同名 → 合并成 exact+inclusive 双指针项，x 出队，q 不动
            2	    /a(合并后)	/a/b	长度不同 → q 前进
            3	    /a/b	/b	不同 → q 前进，q 成为队尾，结束
            产出：[/a(exact+inclusive), /a/b, /b]——每个名字只剩一条，交给下一步 create_locations_list / create_locations_tree 建树。
            */
        }

        q = ngx_queue_next(q);
    }

    return NGX_OK;
}
/*
 * ngx_http_create_locations_list() —— 把排好序的平铺队列整理成嵌套链
 *
 * 输入前提：队列已由 ngx_http_cmp_locations 排序 —— 短前缀一定排在它的
 *           更长前缀之前（'/a' 在 '/ab' 前），孩子们紧挨在父亲后面连续排列。
 *
 * 做的事：  对每个 inclusive 的节点 q，把队列中紧随其后、名字以 q 的名字
 *           为前缀的一串节点切出来挂进 q 的 list，平铺队列变成链上套链。
 *
 * 关键限制：ngx_queue_split 只能“从某节点切到队尾”，切不出中间一段，
 *           所以流程是【先整段挂进来，再把不是孩子的那段还回去】——
 *           这就是函数里出现两次 split/add 的原因。
 *
 * 记号：[/x] = 名字为 /x 的队列节点，X = 队尾哨兵(sentinel)
 *
 *      输入队列（已排序）：
 *
 *          [/] --> [/a] --> [/ab] --> [/ac] --> [/b] --> [/bc] --> X
 *
 * 第 1 步  q = "/"（len=1）
 *          x 从 /a 向队尾扫描：后面所有名字都以 "/" 开头，无一触发
 *          break，x 一路走到哨兵
 *
 *          split 于 /a 处，切出的整段挂进 "/"->list：
 *
 *          locations:  [/]
 *          "/"->list:  [/a] --> [/ab] --> [/ac] --> [/b] --> [/bc]
 *
 *          x 已是哨兵，后面全是孩子、没有要还的：递归进入 "/"->list
 *          （第 2 步），外层到此结束
 *
 * 第 2 步  在 "/"->list 内，q = "/a"（len=2）
 *          x 扫描：/ab、/ac 均以 "/a" 开头 -> 通过；
 *                  /b  第二字节 'b' != 'a'  -> break，x 停在 /b
 *
 *          第一次 split/add：/ab 起的整段挂进 "/a"->list；
 *          第二次 split/add：[/b /bc] 不是 "/a" 的孩子，从 "/a"->list
 *          切出来还回外层队列
 *
 *          "/"->list:  [/a] --> [/b] --> [/bc]
 *          "/a"->list: [/ab] --> [/ac]
 *
 *          先递归 "/a"->list（第 3 步），再从 x = /b 继续外层（第 4 步）
 *
 * 第 3 步  在 "/a"->list 内，q = "/ab"
 *          x = /ac：第三字节 'c' != 'b' -> break
 *          q 前进后恰好等于 x（没有以 "/ab" 为前缀的名字）：
 *          直接递归(x)，而 /ac 是队尾 -> 立即返回，本链结束
 *
 * 第 4 步  在 "/"->list 内，q = "/b"
 *          x = /bc 以 "/b" 开头 -> 通过，x 走到哨兵
 *          split 于 /bc 处：[/bc] 挂进 "/b"->list；递归进链，
 *          /bc 是队尾 -> 返回，全部完成
 *
 * 最终产物（横向是兄弟，纵向 list 是父子）：
 *
 *          locations:  [/]
 *                         |
 *                        list
 *                         |
 *                       [/a] --------------> [/b]
 *                         |                    |
 *                        list                 list
 *                         |                    |
 *                    [/ab] --> [/ac]         [/bc]
 *
 * 下游：    create_locations_tree() 消费此结构 —— 横向兄弟链的中点成为
 *          树节点（左半 -> left，右半 -> right），纵向 list 成为下探
 *          子树（node->tree，节点名取去掉父前缀后的后缀）。
 */
static void
ngx_http_create_locations_list(ngx_queue_t *locations, ngx_queue_t *q)
{
    u_char                     *name;
    size_t                      len;
    ngx_queue_t                *x, tail;
    ngx_http_location_queue_t  *lq, *lx;

    if (q == ngx_queue_last(locations)) {
        return;
    }

    lq = (ngx_http_location_queue_t *) q;

    /* 光杆 exact（只有 = /x、没有普通 /x）不能当父亲挂孩子，跳过 */

    if (lq->inclusive == NULL) {
        ngx_http_create_locations_list(locations, ngx_queue_next(q));
        return;
    }

    len = lq->name->len;
    name = lq->name->data;

    /* x 从 q 的下一个节点向队尾扫描，停在第一个【名字不以 q 的名字为
     * 前缀】的节点上；若一路都是前缀，则停在队尾哨兵。
     * 例：q = "/"（第 1 步）时后面全都以 "/" 开头，x = 哨兵；
     *     q = "/a"（第 2 步）时 x 停在 /b */

    for (x = ngx_queue_next(q);
         x != ngx_queue_sentinel(locations);
         x = ngx_queue_next(x))
    {
        lx = (ngx_http_location_queue_t *) x;

        if (len > lx->name->len
            || ngx_filename_cmp(name, lx->name->data, len) != 0)
        {
            break;
        }
    }

    /* q 前进到原 q 的下一个节点：从它到 x 之前的一串都是原 q 的孩子
     * 例（第 1 步）：指向 /a，即 "/" 的第一个孩子 */

    q = ngx_queue_next(q);

    if (q == x) {
        /* 没有更长前缀的孩子（第 3 步的 /ab），从 x 起继续处理兄弟 */

        ngx_http_create_locations_list(locations, x);
        return;
    }

    /* 第一次切分：从 q 到队尾整段切出，挂进原 q 的 list
     * 例（第 2 步）：/ab 起的整段先进 "/a"->list，[/b /bc] 也被带了进来 */

    ngx_queue_split(locations, q, &tail);
    ngx_queue_add(&lq->list, &tail);

    /* x 已是哨兵：后面全是孩子，没有要还回去的（第 1、4 步），
     * 递归处理孩子链后即完成 */

    if (x == ngx_queue_sentinel(locations)) {
        ngx_http_create_locations_list(&lq->list, ngx_queue_head(&lq->list));
        return;
    }

    /* 第二次切分：把误带进来的【非孩子段，x 到队尾】从 lq->list 切出，
     * 还回外层队列 —— 因为 split 只能切到队尾，才有这次“退货”
     * 例（第 2 步）：[/b /bc] 从 "/a"->list 还回 "/"->list */

    ngx_queue_split(&lq->list, x, &tail);
    ngx_queue_add(locations, &tail);

    /* 递归处理孩子链（原 q 的 list），再从 x 起处理外层的剩余兄弟 */

    ngx_http_create_locations_list(&lq->list, ngx_queue_head(&lq->list));
    ngx_http_create_locations_list(locations, x);
}

/*
 * to keep cache locality for left leaf nodes, allocate nodes in following
 * order: node, left subtree, right subtree, inclusive subtree
 */
//其实cf这种哪都有的上下文可以理解为把一堆函数组成一个类，cf
/*
 * ngx_http_create_locations_tree() —— 把嵌套链建成三叉搜索树（TST）
 *
 * 输入：    上一个函数 create_locations_list() 的产物 —— 横向兄弟链，
 *           各节点的孩子挂在它的 list 里。沿用同一个例子：
 *
 *               locations:  [/]
 *                              |
 *                             list
 *                              |
 *                           [/a] ---------> [/b]
 *                             |               |
 *                            list            list
 *                             |               |
 *                        [/ab] --> [/ac]    [/bc]
 *
 * 输出：    static_locations 三叉树。每层规则：
 *              a) 取队列【中点】建节点 —— 保证左右两半大致等分，树平衡；
 *              b) 中点左边的建 node->left，右边的建 node->right；
 *              c) 中点自己的 list（孩子链）建 node->tree（下探一层）。
 *
 * prefix：  当前层所有名字已被上层节点消耗掉的公共前缀长度。
 *           节点名只存后缀（"b" 而非 "/b"）—— 路径压缩，且节点是
 *           变长结构（offsetof(name) + len），一次 palloc 搞定。
 *
 * 三叉的含义（请求期 find_static_location 按字节比较）：
 *           当前字节 < 节点字节 -> 走 left；> -> 走 right；
 *           相等 -> 消耗字节，沿 node->tree 下探续比后续字节。
 *
 * 演算（注意：ngx_queue_middle 对两个元素取【后一个】；
 *       ngx_queue_split(q) 使中点落入 tail 一侧）：
 *
 * 第 1 步  队列 [ / ]，prefix = 0
 *          中点 = /  -> 建节点 "/"（全名 "/"，后缀就是 "/"）
 *          split 后左右皆空 -> 直接进 inclusive：
 *          /->list = [ /a  /b ] 非空
 *          -> node->tree = 递归（第 2 步，prefix = 0 + 1 = 1）
 *
 * 第 2 步  队列 [ /a  /b ]，prefix = 1
 *          中点 = /b -> 建节点 "b"（"/b" 去掉 1 字节前缀 = "b"）
 *          split：左 = [ /a ]，tail = [ /b ]
 *          左非空 -> node->left = 递归（第 3 步，prefix 不变 = 1）
 *          remove(q) 后 tail 空 -> 进 inclusive：
 *          /b->list = [ /bc ] 非空
 *          -> node->tree = 递归（第 4 步，prefix = 1 + 1 = 2）
 *
 * 第 3 步  队列 [ /a ]，prefix = 1
 *          中点 = /a -> 建节点 "a"。split 左右皆空 -> inclusive：
 *          /a->list = [ /ab  /ac ] 非空
 *          -> node->tree = 递归（第 5 步，prefix = 1 + 1 = 2）
 *
 * 第 4 步  队列 [ /bc ]，prefix = 2
 *          中点 = /bc -> 建节点 "c"（"/bc" 去掉 2 字节 = "c"）
 *          左右皆空、list 空 -> 返回节点 "c"
 *
 * 第 5 步  队列 [ /ab  /ac ]，prefix = 2
 *          中点 = /ac -> 建节点 "c"。split：左 = [ /ab ]，tail = [ /ac ]
 *          左非空 -> node->left = 递归（第 6 步）
 *          remove(q) 后 tail 空、/ac->list 空 -> 返回（带着 left = "b"）
 *
 * 第 6 步  队列 [ /ab ]，prefix = 2
 *          中点 = /ab -> 建节点 "b"。皆空 -> 返回节点 "b"
 *
 * 最终的树（" loc=x " 表示该槽指向 location x 的配置）：
 *
 *     ["/" loc=/]
 *        |
 *        +-- tree --> ["b" loc=/b]
 *                        |-- left --> ["a" loc=/a]
 *                        |               |
 *                        |               +-- tree --> ["c" loc=/ac]
 *                        |               |                |
 *                        |               |                +-- left --> ["b" loc=/ab]
 *                        |
 *                        +-- tree --> ["c" loc=/bc]
 *
 * 请求期验证（find_static_location，URI = "/ab"）：
 *
 *     "/" : 首字节 '/' 相等，URI 更长 -> loc=/ 记为候选，下钻 tree
 *     "b" : 首字节 'a' < 'b' -> 走 left
 *     "a" : 首字节 'a' 相等，剩余 "b" 更长 -> loc=/a 候选，下钻 tree
 *     "c" : 字节 'b' < 'c' -> 走 left
 *     "b" : "b" 整串相等 -> loc=/ab 候选，返回 NGX_AGAIN
 *
 *     候选逐层被覆盖：/ -> /a -> /ab，最长前缀自然胜出。
 */
static ngx_http_location_tree_node_t *
ngx_http_create_locations_tree(ngx_conf_t *cf, ngx_queue_t *locations,
    size_t prefix)
{
    size_t                          len;
    ngx_queue_t                    *q, tail;
    ngx_http_location_queue_t      *lq;
    ngx_http_location_tree_node_t  *node;

    /* 取中点做本层节点：左右两半大致等分，树才平衡
     * 例（第 2 步）：[ /a  /b ] 的中点是 /b */

    q = ngx_queue_middle(locations);

    lq = (ngx_http_location_queue_t *) q;

    /* 节点名长度 = 全名 - 上层已消耗的公共前缀
     * 例（第 2 步）："/b".len(2) - prefix(1) = 1，即后缀 "b" */

    len = lq->name->len - prefix;

    /* 变长节点：name 柔性数组只开后缀那么长，一次分配，不存冗余前缀 */

    node = ngx_palloc(cf->pool,
                      offsetof(ngx_http_location_tree_node_t, name) + len);
    if (node == NULL) {
        return NULL;
    }

    node->left = NULL;
    node->right = NULL;
    node->tree = NULL;

    /* exact/inclusive 双指针直接继承队列节点 —— join_exact_locations
     * 合并的成果在这里被抄进树节点 */

    node->exact = lq->exact;
    node->inclusive = lq->inclusive;

    /* 自动补斜杠 301 的标志（如 proxy_pass 挂在 /x/ 上）：任一槽带上即置位 */

    node->auto_redirect = (u_char) ((lq->exact && lq->exact->auto_redirect)
                           || (lq->inclusive && lq->inclusive->auto_redirect));

    node->len = (u_short) len;
    ngx_memcpy(node->name, &lq->name->data[prefix], len);

    /* 中点切两半：locations = 比中点小的左半，tail = 中点 + 右半 */

    ngx_queue_split(locations, q, &tail);

    if (ngx_queue_empty(locations)) {
        /*
         * ngx_queue_split() insures that if left part is empty,
         * then right one is empty too
         */
        goto inclusive;
    }

    /* 左半递归建左子树；prefix 不变 —— 同层名字的前缀消耗深度相同 */

    node->left = ngx_http_create_locations_tree(cf, locations, prefix);
    if (node->left == NULL) {
        return NULL;
    }

    /* 中点自己已变成树节点，从 tail 里摘掉，剩下的才是纯右半 */

    ngx_queue_remove(q);

    if (ngx_queue_empty(&tail)) {
        goto inclusive;
    }

    /* 右半递归建右子树 */
    node->right = ngx_http_create_locations_tree(cf, &tail, prefix);
    if (node->right == NULL) {
        return NULL;
    }
    //往下再走inclusive
inclusive:

    /* 孩子链建下探子树：prefix+len，本节点名也计入公共前缀，
     * 孩子节点名从更深的位置取后缀
     * 例（第 3 步）："a" 的孩子 /ab、/ac 用 prefix = 2，节点名 "b"、"c" */

    if (ngx_queue_empty(&lq->list)) {
        return node;
    }
    //给直接的子节点建树
    node->tree = ngx_http_create_locations_tree(cf, &lq->list, prefix + len);
    if (node->tree == NULL) {
        return NULL;
    }

    return node;
}


ngx_int_t
ngx_http_add_listen(ngx_conf_t *cf, ngx_http_core_srv_conf_t *cscf,
    ngx_http_listen_opt_t *lsopt)
{
    in_port_t                   p;
    ngx_uint_t                  i;
    struct sockaddr            *sa;
    ngx_http_conf_port_t       *port;
    ngx_http_core_main_conf_t  *cmcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    if (cmcf->ports == NULL) {
        cmcf->ports = ngx_array_create(cf->temp_pool, 2,
                                       sizeof(ngx_http_conf_port_t));
        if (cmcf->ports == NULL) {
            return NGX_ERROR;
        }
    }
    //拿到端口
    sa = lsopt->sockaddr;
    p = ngx_inet_get_port(sa);

    port = cmcf->ports->elts;

    /*
    下面的逻辑是在现有ports里的ngx_http_conf_port_t中找与函数传入的
    sockaddr(ngx_http_listen_opt_t是sockaddr的封装)匹配的ngx_http_conf_port_t
    项，并将其加入
    如果遍历完了ports都没找到匹配的，则新建一个ngx_http_conf_port_t，并将其加入
    */
    for (i = 0; i < cmcf->ports->nelts; i++) {

        if (p != port[i].port
            || lsopt->type != port[i].type
            || sa->sa_family != port[i].family)
        {
            continue;
        }

        /* a port is already in the port list */
        /*
        ngx_http_add_addresses根据新加入的addr更新port[i]，所以更复杂
        */
        return ngx_http_add_addresses(cf, cscf, &port[i], lsopt);
    }

    /* add a port to the port list */

    port = ngx_array_push(cmcf->ports);
    if (port == NULL) {
        return NGX_ERROR;
    }

    port->family = sa->sa_family;
    port->type = lsopt->type;
    port->port = p;
    port->addrs.elts = NULL;

    return ngx_http_add_address(cf, cscf, port, lsopt);
}

/*
ngx_http_add_addresses负责向一个已经存在的addr中添加server，其会调用ngx_http_add_address
ngx_http_add_address负责创建一个server，向其中添加server
*/
static ngx_int_t
ngx_http_add_addresses(ngx_conf_t *cf, ngx_http_core_srv_conf_t *cscf,
    ngx_http_conf_port_t *port, ngx_http_listen_opt_t *lsopt)
{
    ngx_uint_t             i, default_server, proxy_protocol,
                           protocols, protocols_prev;
    ngx_http_conf_addr_t  *addr;
#if (NGX_HTTP_SSL)
    ngx_uint_t             ssl;
#endif
#if (NGX_HTTP_V2)
    ngx_uint_t             http2;
#endif
#if (NGX_HTTP_V3)
    ngx_uint_t             quic;
#endif

    /*
     * we cannot compare whole sockaddr struct's as kernel
     * may fill some fields in inherited sockaddr struct's
     */

    addr = port->addrs.elts;

    for (i = 0; i < port->addrs.nelts; i++) {
        //比较两个sockaddr是否相同：addr和port字段，因为调用方是ngx_http_add_listen，所以port
        //肯定相同，就是看addr了
        if (ngx_cmp_sockaddr(lsopt->sockaddr, lsopt->socklen,
                             addr[i].opt.sockaddr,
                             addr[i].opt.socklen, 0)
            != NGX_OK)
        {
            continue;
        }

        /* the address is already in the address list */
        //知道匹配的addr就加入到其中
        if (ngx_http_add_server(cf, cscf, &addr[i]) != NGX_OK) {
            return NGX_ERROR;
        }

        /* preserve default_server bit during listen options overwriting */
        default_server = addr[i].opt.default_server;

        proxy_protocol = lsopt->proxy_protocol || addr[i].opt.proxy_protocol;
        protocols = lsopt->proxy_protocol;
        protocols_prev = addr[i].opt.proxy_protocol;

#if (NGX_HTTP_SSL)
        ssl = lsopt->ssl || addr[i].opt.ssl;
        protocols |= lsopt->ssl << 1;
        protocols_prev |= addr[i].opt.ssl << 1;
#endif
#if (NGX_HTTP_V2)
        http2 = lsopt->http2 || addr[i].opt.http2;
        protocols |= lsopt->http2 << 2;
        protocols_prev |= addr[i].opt.http2 << 2;
#endif
#if (NGX_HTTP_V3)
        quic = lsopt->quic || addr[i].opt.quic;
#endif

        if (lsopt->set) {

            if (addr[i].opt.set) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "duplicate listen options for %V",
                                   &addr[i].opt.addr_text);
                return NGX_ERROR;
            }

            addr[i].opt = *lsopt;
        }

        /* check the duplicate "default" server for this address:port */

        if (lsopt->default_server) {

            if (default_server) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "a duplicate default server for %V",
                                   &addr[i].opt.addr_text);
                return NGX_ERROR;
            }

            default_server = 1;
            addr[i].default_server = cscf;
        }

        /* check for conflicting protocol options */

        if ((protocols | protocols_prev) != protocols_prev) {

            /* options added */

            if ((addr[i].opt.set && !lsopt->set)
                || addr[i].protocols_changed
                || (protocols | protocols_prev) != protocols)
            {
                ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                                   "protocol options redefined for %V",
                                   &addr[i].opt.addr_text);
            }

            addr[i].protocols = protocols_prev;
            addr[i].protocols_set = 1;
            addr[i].protocols_changed = 1;

        } else if ((protocols_prev | protocols) != protocols) {

            /* options removed */

            if (lsopt->set
                || (addr[i].protocols_set && protocols != addr[i].protocols))
            {
                ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                                   "protocol options redefined for %V",
                                   &addr[i].opt.addr_text);
            }

            addr[i].protocols = protocols;
            addr[i].protocols_set = 1;
            addr[i].protocols_changed = 1;

        } else {

            /* the same options */

            if ((lsopt->set && addr[i].protocols_changed)
                || (addr[i].protocols_set && protocols != addr[i].protocols))
            {
                ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                                   "protocol options redefined for %V",
                                   &addr[i].opt.addr_text);
            }

            addr[i].protocols = protocols;
            addr[i].protocols_set = 1;
        }

        addr[i].opt.default_server = default_server;
        addr[i].opt.proxy_protocol = proxy_protocol;
#if (NGX_HTTP_SSL)
        addr[i].opt.ssl = ssl;
#endif
#if (NGX_HTTP_V2)
        addr[i].opt.http2 = http2;
#endif
#if (NGX_HTTP_V3)
        addr[i].opt.quic = quic;
#endif

        return NGX_OK;
    }

    /* add the address to the addresses list that bound to this port */
    //有port匹配，但是port下没addr匹配，复用ngx_http_add_address函数
    return ngx_http_add_address(cf, cscf, port, lsopt);
}


/*
 * add the server address, the server names and the server core module
 * configurations to the port list
 */

static ngx_int_t
ngx_http_add_address(ngx_conf_t *cf, ngx_http_core_srv_conf_t *cscf,
    ngx_http_conf_port_t *port, ngx_http_listen_opt_t *lsopt)
{
    ngx_http_conf_addr_t  *addr;

    if (port->addrs.elts == NULL) {
        if (ngx_array_init(&port->addrs, cf->temp_pool, 4,
                           sizeof(ngx_http_conf_addr_t))
            != NGX_OK)
        {
            return NGX_ERROR;
        }
    }

#if (NGX_HTTP_V2 && NGX_HTTP_SSL                                              \
     && !defined TLSEXT_TYPE_application_layer_protocol_negotiation)

    if (lsopt->http2 && lsopt->ssl) {
        ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                           "nginx was built with OpenSSL that lacks ALPN "
                           "support, HTTP/2 is not enabled for %V",
                           &lsopt->addr_text);
    }

#endif

    addr = ngx_array_push(&port->addrs);
    if (addr == NULL) {
        return NGX_ERROR;
    }

    addr->opt = *lsopt;//sockaddr信息以及其他封装信息
    addr->protocols = 0;
    addr->protocols_set = 0;
    addr->protocols_changed = 0;
    addr->hash.buckets = NULL;
    addr->hash.size = 0;
    addr->wc_head = NULL;
    addr->wc_tail = NULL;
#if (NGX_PCRE)
    addr->nregex = 0;
    addr->regex = NULL;
#endif
    addr->default_server = cscf;//第一个定义这个addrs的那个server的配置，从这里可以看出来，这个函数只在没有匹配到对应的addr时才会调用
    addr->servers.elts = NULL;

    return ngx_http_add_server(cf, cscf, addr);
}


/* add the server core module configuration to the address:port */

static ngx_int_t
ngx_http_add_server(ngx_conf_t *cf, ngx_http_core_srv_conf_t *cscf,
    ngx_http_conf_addr_t *addr)
{
    ngx_uint_t                  i;
    ngx_http_core_srv_conf_t  **server;

    if (addr->servers.elts == NULL) {
        if (ngx_array_init(&addr->servers, cf->temp_pool, 4,
                           sizeof(ngx_http_core_srv_conf_t *))
            != NGX_OK)
        {
            return NGX_ERROR;
        }

    } else {
        server = addr->servers.elts;
        for (i = 0; i < addr->servers.nelts; i++) {
            //ngx_http_core_srv_conf_t地址相同表示server相同
            //不允许在一个server块中监听两个相同的socket
            if (server[i] == cscf) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "a duplicate listen %V",
                                   &addr->opt.addr_text);
                return NGX_ERROR;
            }
        }
    }
    
    server = ngx_array_push(&addr->servers);
    if (server == NULL) {
        return NGX_ERROR;
    }

    *server = cscf;

    return NGX_OK;
}

/*
 * 配置解析完成后的“监听体系收尾”：把 http 块里所有 listen 指令攒下的
 * ports（ngx_http_conf_port_t 数组，每个元素是“同一端口上的所有地址”）
 * 从“原始登记信息”加工成运行期可直接使用的形式。
 *
 * 输入：cmcf->ports —— listen 指令解析阶段（ngx_http_listen）登记的
 *       端口表，每个 port->addrs 里挂着该端口上各 address 的
 *       ngx_http_conf_addr_t（含 servers 数组、default_server）。
 *
 * 对每个端口做三件事：
 *
 * 1. 排序：ngx_http_cmp_conf_addrs 把 addrs 排成
 *    [显式 bind 的地址] -> [隐式地址] -> [通配 *:port 垫底]。
 *    这是后面 init_listening 里“通配收编隐式地址”的前提。
 *
 * 2. 虚拟主机查找表（ngx_http_server_names）：对每个 addr:port，
 *    若它上面挂了多个 server（servers.nelts > 1）或 default_server
 *    带 regex captures，就用这些 server 的 server_name 建
 *    hash/wc_head/wc_tail/regex 查找表存进 addr 的 hash 字段。
 *    运行期请求带着 Host 头来时，ngx_http_find_virtual_server
 *    就查这张表挑虚拟主机。只有一个 server 且无 captures 的地址
 *    不需要建表——Host 无从二选一，default_server 恒命中。
 *
 * 3. 生成监听 socket 配置（ngx_http_init_listening）：把排好序的
 *    addrs 换算成 ngx_listening_t（进 cycle->listening，后续
 *    ngx_init_cycle 统一 socket()/bind()/listen()），并把每个地址
 *    的运行期配置（default_server、虚拟主机表等）打包进
 *    ls->servers（ngx_http_port_t）。
 *
 * 本函数由 ngx_http_block() 在 location 树、server 配置全部就绪后
 * 调用（ngx_http.c:538）。
 */
static ngx_int_t
ngx_http_optimize_servers(ngx_conf_t *cf, ngx_http_core_main_conf_t *cmcf,
    ngx_array_t *ports)
{
    ngx_uint_t             p, a;
    ngx_http_conf_port_t  *port;
    ngx_http_conf_addr_t  *addr;

    if (ports == NULL) {
        return NGX_OK;
    }

    port = ports->elts;
    for (p = 0; p < ports->nelts; p++) {
        //bind的放最前面，wildcard放后面
        ngx_sort(port[p].addrs.elts, (size_t) port[p].addrs.nelts,
                 sizeof(ngx_http_conf_addr_t), ngx_http_cmp_conf_addrs);

        /*
         * check whether all name-based servers have the same
         * configuration as a default server for given address:port
         */

        addr = port[p].addrs.elts;
        for (a = 0; a < port[p].addrs.nelts; a++) {

            if (addr[a].servers.nelts > 1
#if (NGX_PCRE)
                || addr[a].default_server->captures
#endif
               )
            {
                //每一个ngx_http_conf_addr_t创建一个哈希表，其key是该addr:port下的server names
                //供后续http报文的host字段进行索引
                if (ngx_http_server_names(cf, cmcf, &addr[a]) != NGX_OK) {
                    return NGX_ERROR;
                }
            }
        }
        //一个port+一个addr 生成一个listening，最终会被cycle.listening指向
        if (ngx_http_init_listening(cf, &port[p]) != NGX_OK) {
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}

/*
给ngx_http_conf_addr_t构建哈希表
*/
static ngx_int_t
ngx_http_server_names(ngx_conf_t *cf, ngx_http_core_main_conf_t *cmcf,
    ngx_http_conf_addr_t *addr)
{
    ngx_int_t                   rc;
    ngx_uint_t                  n, s;
    ngx_hash_init_t             hash;
    ngx_hash_keys_arrays_t      ha;
    ngx_http_server_name_t     *name;
    ngx_http_core_srv_conf_t  **cscfp;
#if (NGX_PCRE)
    ngx_uint_t                  regex, i;

    regex = 0;
#endif

    ngx_memzero(&ha, sizeof(ngx_hash_keys_arrays_t));

    ha.temp_pool = ngx_create_pool(NGX_DEFAULT_POOL_SIZE, cf->log);
    if (ha.temp_pool == NULL) {
        return NGX_ERROR;
    }

    ha.pool = cf->pool;

    if (ngx_hash_keys_array_init(&ha, NGX_HASH_LARGE) != NGX_OK) {
        goto failed;
    }

    cscfp = addr->servers.elts;

    for (s = 0; s < addr->servers.nelts; s++) {

        name = cscfp[s]->server_names.elts;

        for (n = 0; n < cscfp[s]->server_names.nelts; n++) {

#if (NGX_PCRE)
            if (name[n].regex) {
                regex++;
                continue;
            }
#endif
            //初始化哈希表，这里key是server name 而value是ngx_http_core_srv_conf_t
            rc = ngx_hash_add_key(&ha, &name[n].name, name[n].server,
                                  NGX_HASH_WILDCARD_KEY);

            if (rc == NGX_ERROR) {
                goto failed;
            }

            if (rc == NGX_DECLINED) {
                ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                              "invalid server name or wildcard \"%V\" on %V",
                              &name[n].name, &addr->opt.addr_text);
                goto failed;
            }

            if (rc == NGX_BUSY) {
                ngx_log_error(NGX_LOG_WARN, cf->log, 0,
                              "conflicting server name \"%V\" on %V, ignored",
                              &name[n].name, &addr->opt.addr_text);
            }
        }
    }

    hash.key = ngx_hash_key_lc;
    hash.max_size = cmcf->server_names_hash_max_size;
    hash.bucket_size = cmcf->server_names_hash_bucket_size;
    hash.name = "server_names_hash";
    hash.pool = cf->pool;

    if (ha.keys.nelts) {
        hash.hash = &addr->hash;
        hash.temp_pool = NULL;
        //初始化excat
        if (ngx_hash_init(&hash, ha.keys.elts, ha.keys.nelts) != NGX_OK) {
            goto failed;
        }
    }

    if (ha.dns_wc_head.nelts) {

        ngx_qsort(ha.dns_wc_head.elts, (size_t) ha.dns_wc_head.nelts,
                  sizeof(ngx_hash_key_t), ngx_http_cmp_dns_wildcards);

        hash.hash = NULL;
        hash.temp_pool = ha.temp_pool;

        if (ngx_hash_wildcard_init(&hash, ha.dns_wc_head.elts,
                                   ha.dns_wc_head.nelts)
            != NGX_OK)
        {
            goto failed;
        }

        addr->wc_head = (ngx_hash_wildcard_t *) hash.hash;
    }

    if (ha.dns_wc_tail.nelts) {

        ngx_qsort(ha.dns_wc_tail.elts, (size_t) ha.dns_wc_tail.nelts,
                  sizeof(ngx_hash_key_t), ngx_http_cmp_dns_wildcards);

        hash.hash = NULL;
        hash.temp_pool = ha.temp_pool;

        if (ngx_hash_wildcard_init(&hash, ha.dns_wc_tail.elts,
                                   ha.dns_wc_tail.nelts)
            != NGX_OK)
        {
            goto failed;
        }

        addr->wc_tail = (ngx_hash_wildcard_t *) hash.hash;
    }

    ngx_destroy_pool(ha.temp_pool);

#if (NGX_PCRE)

    if (regex == 0) {
        return NGX_OK;
    }

    addr->nregex = regex;
    addr->regex = ngx_palloc(cf->pool, regex * sizeof(ngx_http_server_name_t));
    if (addr->regex == NULL) {
        return NGX_ERROR;
    }

    i = 0;

    for (s = 0; s < addr->servers.nelts; s++) {

        name = cscfp[s]->server_names.elts;

        for (n = 0; n < cscfp[s]->server_names.nelts; n++) {
            if (name[n].regex) {
                addr->regex[i++] = name[n];
            }
        }
    }

#endif

    return NGX_OK;

failed:

    ngx_destroy_pool(ha.temp_pool);

    return NGX_ERROR;
}

//按照wildcard和bind字段排序
//wildcard在最后面，bind在前面
static ngx_int_t
ngx_http_cmp_conf_addrs(const void *one, const void *two)
{
    ngx_http_conf_addr_t  *first, *second;

    first = (ngx_http_conf_addr_t *) one;
    second = (ngx_http_conf_addr_t *) two;

    if (first->opt.wildcard) {
        /* a wildcard address must be the last resort, shift it to the end */
        return 1;
    }

    if (second->opt.wildcard) {
        /* a wildcard address must be the last resort, shift it to the end */
        return -1;
    }

    if (first->opt.bind && !second->opt.bind) {
        /* shift explicit bind()ed addresses to the start */
        return -1;
    }

    if (!first->opt.bind && second->opt.bind) {
        /* shift explicit bind()ed addresses to the start */
        return 1;
    }

    /* do not sort by default */

    return 0;
}


static int ngx_libc_cdecl
ngx_http_cmp_dns_wildcards(const void *one, const void *two)
{
    ngx_hash_key_t  *first, *second;

    first = (ngx_hash_key_t *) one;
    second = (ngx_hash_key_t *) two;

    return ngx_dns_strcmp(first->key.data, second->key.data);
}


static ngx_int_t
ngx_http_init_listening(ngx_conf_t *cf, ngx_http_conf_port_t *port)
{
    ngx_uint_t                 i, last, bind_wildcard;
    ngx_listening_t           *ls;
    ngx_http_port_t           *hport;
    ngx_http_conf_addr_t      *addr;

    addr = port->addrs.elts;
    last = port->addrs.nelts;

    /*
     * If there is a binding to an "*:port" then we need to bind() to
     * the "*:port" only and ignore other implicit bindings.  The bindings
     * have been already sorted: explicit bindings are on the start, then
     * implicit bindings go, and wildcard binding is in the end.
     */
    
    if (addr[last - 1].opt.wildcard) {
        addr[last - 1].opt.bind = 1;
        bind_wildcard = 1;

    } else {
        bind_wildcard = 0;
    }

    i = 0;
    /*
    addr++;
        last--;hport->naddrs = i + 1;这我没搞懂，这难道不会让一个addr被多个listening监听吗

    解答：
    排序是整个算法的前提：ngx_cmp_conf_addrs 把显式 bind 排前、隐式居中、通配垫底
    和 2331-2336 的注释），所以“显式阶段逐格消费 + 通配轮一次吞掉剩余全部”刚好把数组
    切成不重叠的段。如果没有这个排序保证，addr++ 只挪一格确实会留尾巴出错
    */
    while (i < last) {
            /*
        * 通配收编规则：存在 *:port 监听时（bind_wildcard=1），跳过一切隐式
        * 地址（bind=0），不为它们单独建 socket —— 内核不允许 bind 重叠地址，
        * *:port 与 10.0.0.1:port 同时 bind 会 EADDRINUSE；且通配 socket 反正
        * 会代收这些地址的全部连接。
        *
        * 注意：跳过的是"建 socket"，不是配置本身 —— 被跳过的 addr 仍会经
        * hport->naddrs = i + 1 / ngx_http_add_addrs() 并入末尾那个通配
        * socket 的 addrs[] 表；运行期 ngx_http_init_connection 按本地目的
        * IP 在表中反查，连接照样被认领回各自地址的配置。
        *
        * bind=1（用户写了 bind 参数，或末尾通配地址被上面强制置 1）则表示
        * 必须拥有自己的 socket，尊重之，继续走 ngx_http_add_listening()。
        */
       /*
       如果没有wild_card的话就给每个隐式的也bind，因为wild_card可以接受一个端口上
       所有ip地址的包，只要看到达的连接的目的地址后转发即可
       */
      /*
        worker 从 *:8080 accept 得到 fd
                │
                │  getsockname(fd)          ← ngx_connection_local_sockaddr()
                │  内核回答："这条连接的本地端是 10.0.0.2:8080"
                v
        查这张表（ngx_http_port_t.addrs —— 收编时"认领"关系就记在这里）
                │
                │  [0] {addr=10.0.0.1, conf_A}      ┐
                │  [1] {addr=10.0.0.2, conf_B}  ────┼── 比对目的IP ──> 命中 [1]
                │  [2] {addr=*      , conf_W}      ┘   （最后一格 * 兜底）
                v
        hc->addr_conf = conf_B        ← 连接从此用 10.0.0.2 那套
                │                        default_server、virtual_names……
                v
        请求头到达，再按 Host 在 virtual_names 里挑最终虚拟主机
      */
        if (bind_wildcard && !addr[i].opt.bind) {
            i++;
            continue;
        }

        ls = ngx_http_add_listening(cf, &addr[i]);
        if (ls == NULL) {
            return NGX_ERROR;
        }

        hport = ngx_pcalloc(cf->pool, sizeof(ngx_http_port_t));
        if (hport == NULL) {
            return NGX_ERROR;
        }

        ls->servers = hport;

        hport->naddrs = i + 1;

        switch (ls->sockaddr->sa_family) {

#if (NGX_HAVE_INET6)
        case AF_INET6:
            if (ngx_http_add_addrs6(cf, hport, addr) != NGX_OK) {
                return NGX_ERROR;
            }
            break;
#endif
        default: /* AF_INET */
            if (ngx_http_add_addrs(cf, hport, addr) != NGX_OK) {
                return NGX_ERROR;
            }
            break;
        }

        addr++;
        last--;//--是因为当前已经处理了一个元素
    }

    return NGX_OK;
}

//区分ngx_http_add_listening和ngx_http_add_listen
static ngx_listening_t *
ngx_http_add_listening(ngx_conf_t *cf, ngx_http_conf_addr_t *addr)
{
    ngx_listening_t           *ls;
    ngx_http_core_loc_conf_t  *clcf;
    ngx_http_core_srv_conf_t  *cscf;

    ls = ngx_create_listening(cf, addr->opt.sockaddr, addr->opt.socklen);//一个存储在cycle.listening的对象
    if (ls == NULL) {
        return NULL;
    }

    ls->addr_ntop = 1;

    ls->handler = ngx_http_init_connection;

    cscf = addr->default_server;
    ls->pool_size = cscf->connection_pool_size;

    clcf = cscf->ctx->loc_conf[ngx_http_core_module.ctx_index];

    ls->logp = clcf->error_log;
    ls->log.data = &ls->addr_text;
    ls->log.handler = ngx_accept_log_error;

#if (NGX_WIN32)
    {
    ngx_iocp_conf_t  *iocpcf = NULL;

    if (ngx_get_conf(cf->cycle->conf_ctx, ngx_events_module)) {
        iocpcf = ngx_event_get_conf(cf->cycle->conf_ctx, ngx_iocp_module);
    }
    if (iocpcf && iocpcf->acceptex_read) {
        ls->post_accept_buffer_size = cscf->client_header_buffer_size;
    }
    }
#endif

    ls->type = addr->opt.type;
    ls->protocol = addr->opt.protocol;
    ls->backlog = addr->opt.backlog;
    ls->rcvbuf = addr->opt.rcvbuf;
    ls->sndbuf = addr->opt.sndbuf;

    ls->keepalive = addr->opt.so_keepalive;
#if (NGX_HAVE_KEEPALIVE_TUNABLE)
    ls->keepidle = addr->opt.tcp_keepidle;
    ls->keepintvl = addr->opt.tcp_keepintvl;
    ls->keepcnt = addr->opt.tcp_keepcnt;
#endif

#if (NGX_HAVE_DEFERRED_ACCEPT && defined SO_ACCEPTFILTER)
    ls->accept_filter = addr->opt.accept_filter;
#endif

#if (NGX_HAVE_DEFERRED_ACCEPT && defined TCP_DEFER_ACCEPT)
    ls->deferred_accept = addr->opt.deferred_accept;
#endif

#if (NGX_HAVE_INET6)
    ls->ipv6only = addr->opt.ipv6only;
#endif

#if (NGX_HAVE_SETFIB)
    ls->setfib = addr->opt.setfib;
#endif

#if (NGX_HAVE_TCP_FASTOPEN)
    ls->fastopen = addr->opt.fastopen;
#endif

#if (NGX_HAVE_REUSEPORT)
    ls->reuseport = addr->opt.reuseport;
#endif

    ls->wildcard = addr->opt.wildcard;

#if (NGX_HTTP_V3)
    ls->quic = addr->opt.quic;
#endif

    return ls;
}

//把addr里的数据赋值给hport
static ngx_int_t
ngx_http_add_addrs(ngx_conf_t *cf, ngx_http_port_t *hport,
    ngx_http_conf_addr_t *addr)
{
    ngx_uint_t                 i;
    ngx_http_in_addr_t        *addrs;
    struct sockaddr_in        *sin;
    ngx_http_virtual_names_t  *vn;

    hport->addrs = ngx_pcalloc(cf->pool,
                               hport->naddrs * sizeof(ngx_http_in_addr_t));
    if (hport->addrs == NULL) {
        return NGX_ERROR;
    }

    addrs = hport->addrs;

    for (i = 0; i < hport->naddrs; i++) {

        sin = (struct sockaddr_in *) addr[i].opt.sockaddr;
        addrs[i].addr = sin->sin_addr.s_addr;
        addrs[i].conf.default_server = addr[i].default_server;
#if (NGX_HTTP_SSL)
        addrs[i].conf.ssl = addr[i].opt.ssl;
#endif
#if (NGX_HTTP_V2)
        addrs[i].conf.http2 = addr[i].opt.http2;
#endif
#if (NGX_HTTP_V3)
        addrs[i].conf.quic = addr[i].opt.quic;
#endif
        addrs[i].conf.proxy_protocol = addr[i].opt.proxy_protocol;
        //addr下没有server
        if (addr[i].hash.buckets == NULL
            && (addr[i].wc_head == NULL
                || addr[i].wc_head->hash.buckets == NULL)
            && (addr[i].wc_tail == NULL
                || addr[i].wc_tail->hash.buckets == NULL)
#if (NGX_PCRE)
            && addr[i].nregex == 0
#endif
            )
        {
            continue;
        }

        vn = ngx_palloc(cf->pool, sizeof(ngx_http_virtual_names_t));
        if (vn == NULL) {
            return NGX_ERROR;
        }

        addrs[i].conf.virtual_names = vn;

        vn->names.hash = addr[i].hash;
        vn->names.wc_head = addr[i].wc_head;
        vn->names.wc_tail = addr[i].wc_tail;
#if (NGX_PCRE)
        vn->nregex = addr[i].nregex;
        vn->regex = addr[i].regex;
#endif
    }

    return NGX_OK;
}


#if (NGX_HAVE_INET6)

static ngx_int_t
ngx_http_add_addrs6(ngx_conf_t *cf, ngx_http_port_t *hport,
    ngx_http_conf_addr_t *addr)
{
    ngx_uint_t                 i;
    ngx_http_in6_addr_t       *addrs6;
    struct sockaddr_in6       *sin6;
    ngx_http_virtual_names_t  *vn;

    hport->addrs = ngx_pcalloc(cf->pool,
                               hport->naddrs * sizeof(ngx_http_in6_addr_t));
    if (hport->addrs == NULL) {
        return NGX_ERROR;
    }

    addrs6 = hport->addrs;

    for (i = 0; i < hport->naddrs; i++) {

        sin6 = (struct sockaddr_in6 *) addr[i].opt.sockaddr;
        addrs6[i].addr6 = sin6->sin6_addr;
        addrs6[i].conf.default_server = addr[i].default_server;
#if (NGX_HTTP_SSL)
        addrs6[i].conf.ssl = addr[i].opt.ssl;
#endif
#if (NGX_HTTP_V2)
        addrs6[i].conf.http2 = addr[i].opt.http2;
#endif
#if (NGX_HTTP_V3)
        addrs6[i].conf.quic = addr[i].opt.quic;
#endif
        addrs6[i].conf.proxy_protocol = addr[i].opt.proxy_protocol;

        if (addr[i].hash.buckets == NULL
            && (addr[i].wc_head == NULL
                || addr[i].wc_head->hash.buckets == NULL)
            && (addr[i].wc_tail == NULL
                || addr[i].wc_tail->hash.buckets == NULL)
#if (NGX_PCRE)
            && addr[i].nregex == 0
#endif
            )
        {
            continue;
        }

        vn = ngx_palloc(cf->pool, sizeof(ngx_http_virtual_names_t));
        if (vn == NULL) {
            return NGX_ERROR;
        }

        addrs6[i].conf.virtual_names = vn;

        vn->names.hash = addr[i].hash;
        vn->names.wc_head = addr[i].wc_head;
        vn->names.wc_tail = addr[i].wc_tail;
#if (NGX_PCRE)
        vn->nregex = addr[i].nregex;
        vn->regex = addr[i].regex;
#endif
    }

    return NGX_OK;
}

#endif


char *
ngx_http_types_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    ngx_array_t     **types;
    ngx_str_t        *value, *default_type;
    ngx_uint_t        i, n, hash;
    ngx_hash_key_t   *type;

    types = (ngx_array_t **) (p + cmd->offset);

    if (*types == (void *) -1) {
        return NGX_CONF_OK;
    }

    default_type = cmd->post;

    if (*types == NULL) {
        *types = ngx_array_create(cf->temp_pool, 1, sizeof(ngx_hash_key_t));
        if (*types == NULL) {
            return NGX_CONF_ERROR;
        }

        if (default_type) {
            type = ngx_array_push(*types);
            if (type == NULL) {
                return NGX_CONF_ERROR;
            }

            type->key = *default_type;
            type->key_hash = ngx_hash_key(default_type->data,
                                          default_type->len);
            type->value = (void *) 4;
        }
    }

    value = cf->args->elts;

    for (i = 1; i < cf->args->nelts; i++) {

        if (value[i].len == 1 && value[i].data[0] == '*') {
            *types = (void *) -1;
            return NGX_CONF_OK;
        }

        hash = ngx_hash_strlow(value[i].data, value[i].data, value[i].len);
        value[i].data[value[i].len] = '\0';

        type = (*types)->elts;
        for (n = 0; n < (*types)->nelts; n++) {

            if (ngx_strcmp(value[i].data, type[n].key.data) == 0) {
                ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                                   "duplicate MIME type \"%V\"", &value[i]);
                goto next;
            }
        }

        type = ngx_array_push(*types);
        if (type == NULL) {
            return NGX_CONF_ERROR;
        }

        type->key = value[i];
        type->key_hash = hash;
        type->value = (void *) 4;

    next:

        continue;
    }

    return NGX_CONF_OK;
}


char *
ngx_http_merge_types(ngx_conf_t *cf, ngx_array_t **keys, ngx_hash_t *types_hash,
    ngx_array_t **prev_keys, ngx_hash_t *prev_types_hash,
    ngx_str_t *default_types)
{
    ngx_hash_init_t  hash;

    if (*keys) {

        if (*keys == (void *) -1) {
            return NGX_CONF_OK;
        }

        hash.hash = types_hash;
        hash.key = NULL;
        hash.max_size = 2048;
        hash.bucket_size = 64;
        hash.name = "test_types_hash";
        hash.pool = cf->pool;
        hash.temp_pool = NULL;

        if (ngx_hash_init(&hash, (*keys)->elts, (*keys)->nelts) != NGX_OK) {
            return NGX_CONF_ERROR;
        }

        return NGX_CONF_OK;
    }

    if (prev_types_hash->buckets == NULL) {

        if (*prev_keys == NULL) {

            if (ngx_http_set_default_types(cf, prev_keys, default_types)
                != NGX_OK)
            {
                return NGX_CONF_ERROR;
            }

        } else if (*prev_keys == (void *) -1) {
            *keys = *prev_keys;
            return NGX_CONF_OK;
        }

        hash.hash = prev_types_hash;
        hash.key = NULL;
        hash.max_size = 2048;
        hash.bucket_size = 64;
        hash.name = "test_types_hash";
        hash.pool = cf->pool;
        hash.temp_pool = NULL;

        if (ngx_hash_init(&hash, (*prev_keys)->elts, (*prev_keys)->nelts)
            != NGX_OK)
        {
            return NGX_CONF_ERROR;
        }
    }

    *types_hash = *prev_types_hash;

    return NGX_CONF_OK;

}


ngx_int_t
ngx_http_set_default_types(ngx_conf_t *cf, ngx_array_t **types,
    ngx_str_t *default_type)
{
    ngx_hash_key_t  *type;

    *types = ngx_array_create(cf->temp_pool, 1, sizeof(ngx_hash_key_t));
    if (*types == NULL) {
        return NGX_ERROR;
    }

    while (default_type->len) {

        type = ngx_array_push(*types);
        if (type == NULL) {
            return NGX_ERROR;
        }

        type->key = *default_type;
        type->key_hash = ngx_hash_key(default_type->data,
                                      default_type->len);
        type->value = (void *) 4;

        default_type++;
    }

    return NGX_OK;
}
