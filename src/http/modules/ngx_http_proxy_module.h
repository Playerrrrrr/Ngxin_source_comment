
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_HTTP_PROXY_H_INCLUDED_
#define _NGX_HTTP_PROXY_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct {
    ngx_array_t                    caches;  /* ngx_http_file_cache_t * */
} ngx_http_proxy_main_conf_t;


typedef struct {
    ngx_str_t                      key_start;
    ngx_str_t                      schema;
    ngx_str_t                      host_header;
    ngx_str_t                      port;
    ngx_str_t                      uri;
} ngx_http_proxy_vars_t;


typedef struct {
    ngx_array_t                   *flushes;
    //lengths values 用来在ngx_http_proxy_create_request中根据proxy_set_header中设置header的值
    /*
    location /api {
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Debug xyz;
        proxy_pass http://127.0.0.1:8080;
    }
    lengths: 
        len:9 
        len:动态计算$remote_addr
        null---每个kv的len和value指令都用null隔开
        len:7 
        len:3 
        null 
        null---多一个null用来标记结束
    values:
        value:X-Real-IP
        value:动态计算$remote_addr
        null
        value:X-Debug
        value:xyz
        null
    */
    ngx_array_t                   *lengths;
    ngx_array_t                   *values;
    ngx_hash_t                     hash;
} ngx_http_proxy_headers_t;


typedef struct {
    ngx_http_upstream_conf_t       upstream;

    ngx_array_t                   *body_flushes;
    //包体求值指令，来自proxy_set_body '{"source":"nginx"}';
    //ngx_http_proxy_merge_loc_conf() 中调用 ngx_http_script_compile()
    ngx_array_t                   *body_lengths;
    ngx_array_t                   *body_values;
    ngx_str_t                      body_source;
    //在ngx_http_proxy_init_headers中赋值
    ngx_http_proxy_headers_t       headers;
#if (NGX_HTTP_CACHE)
    ngx_http_proxy_headers_t       headers_cache;
#endif
    ngx_array_t                   *headers_source;
    //proxy_set_header Host ...;的求值脚本指令,ngx_http_proxy_init_headers中赋值
    //ngx_conf_set_keyval_slot通用handler将host和key存储，在merge conf阶段进行编译
    ngx_http_complex_value_t      *host_value;

    //proxy_pass的求值指令
    //在ngx_http_proxy_pass中编译
    ngx_array_t                   *proxy_lengths;
    ngx_array_t                   *proxy_values;

    ngx_array_t                   *redirects;
    ngx_array_t                   *cookie_domains;
    ngx_array_t                   *cookie_paths;
    ngx_array_t                   *cookie_flags;

    //这个字段由proxy_method指令决定：proxy_method POST;
    ngx_http_complex_value_t      *method;
    //在配置期ngx_http_proxy_pass中被赋值：plcf->location = clcf->name;               
    ngx_str_t                      location;
    ngx_str_t                      url;

#if (NGX_HTTP_CACHE)
    ngx_http_complex_value_t       cache_key;
#endif

    ngx_http_proxy_vars_t          vars;

    ngx_flag_t                     redirect;

    ngx_uint_t                     http_version;

    ngx_uint_t                     headers_hash_max_size;
    ngx_uint_t                     headers_hash_bucket_size;

#if (NGX_HTTP_SSL || NGX_COMPAT)
    ngx_uint_t                     ssl;
    ngx_uint_t                     ssl_protocols;
    ngx_str_t                      ssl_ciphers;
    ngx_uint_t                     ssl_verify_depth;
    ngx_str_t                      ssl_trusted_certificate;
    ngx_str_t                      ssl_crl;
    ngx_array_t                   *ssl_conf_commands;
#endif
} ngx_http_proxy_loc_conf_t;


typedef struct {
    ngx_http_status_t              status;
    ngx_http_chunked_t             chunked;
    /*
    根据proxy_pass后面的url填充vars：
    location /api {
        proxy_pass http://127.0.0.1:8080;
    } 
    vars.schema      = "http://";
    vars.host_header = "127.0.0.1:8080";
    vars.port        = "8080";
    vars.uri         = "";
    因为proxy_pass中可能有变量，所以要运行时求值
    ngx_http_proxy_handler -> ngx_http_proxy_eval -> ngx_http_proxy_set_vars
    */
    ngx_http_proxy_vars_t          vars;
    off_t                          internal_body_length;

    ngx_chain_t                   *free;
    ngx_chain_t                   *busy;

    ngx_buf_t                     *trailers;

    unsigned                       head:1;
    unsigned                       internal_chunked:1;
    unsigned                       header_sent:1;
} ngx_http_proxy_ctx_t;


ngx_int_t ngx_http_proxy_eval(ngx_http_request_t *r, ngx_http_proxy_ctx_t *ctx,
    ngx_http_proxy_loc_conf_t *plcf);
#if (NGX_HTTP_CACHE)
ngx_int_t ngx_http_proxy_create_key(ngx_http_request_t *r);
#endif
ngx_int_t ngx_http_proxy_rewrite_redirect(ngx_http_request_t *r,
    ngx_table_elt_t *h, size_t prefix);
ngx_int_t ngx_http_proxy_rewrite_cookie(ngx_http_request_t *r,
    ngx_table_elt_t *h);

#if (NGX_HTTP_V2)
ngx_int_t ngx_http_proxy_v2_handler(ngx_http_request_t *r);
#endif


extern ngx_module_t  ngx_http_proxy_module;


#endif /* _NGX_HTTP_PROXY_H_INCLUDED_ */
