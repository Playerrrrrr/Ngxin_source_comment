
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_HTTP_REQUEST_H_INCLUDED_
#define _NGX_HTTP_REQUEST_H_INCLUDED_


#define NGX_HTTP_MAX_URI_CHANGES           10
#define NGX_HTTP_MAX_SUBREQUESTS           50

/* must be 2^n */
#define NGX_HTTP_LC_HEADER_LEN             32


#define NGX_HTTP_DISCARD_BUFFER_SIZE       4096
#define NGX_HTTP_LINGERING_BUFFER_SIZE     4096


#define NGX_HTTP_VERSION_9                 9
#define NGX_HTTP_VERSION_10                1000
#define NGX_HTTP_VERSION_11                1001
#define NGX_HTTP_VERSION_20                2000
#define NGX_HTTP_VERSION_30                3000

#define NGX_HTTP_UNKNOWN                   0x00000001
#define NGX_HTTP_GET                       0x00000002
#define NGX_HTTP_HEAD                      0x00000004
#define NGX_HTTP_POST                      0x00000008
#define NGX_HTTP_PUT                       0x00000010
#define NGX_HTTP_DELETE                    0x00000020
#define NGX_HTTP_MKCOL                     0x00000040
#define NGX_HTTP_COPY                      0x00000080
#define NGX_HTTP_MOVE                      0x00000100
#define NGX_HTTP_OPTIONS                   0x00000200
#define NGX_HTTP_PROPFIND                  0x00000400
#define NGX_HTTP_PROPPATCH                 0x00000800
#define NGX_HTTP_LOCK                      0x00001000
#define NGX_HTTP_UNLOCK                    0x00002000
#define NGX_HTTP_PATCH                     0x00004000
#define NGX_HTTP_TRACE                     0x00008000
#define NGX_HTTP_CONNECT                   0x00010000

#define NGX_HTTP_CONNECTION_CLOSE          1
#define NGX_HTTP_CONNECTION_KEEP_ALIVE     2


#define NGX_NONE                           1


#define NGX_HTTP_PARSE_HEADER_DONE         1

#define NGX_HTTP_CLIENT_ERROR              10
#define NGX_HTTP_PARSE_INVALID_METHOD      10
#define NGX_HTTP_PARSE_INVALID_REQUEST     11
#define NGX_HTTP_PARSE_INVALID_VERSION     12
#define NGX_HTTP_PARSE_INVALID_09_METHOD   13

#define NGX_HTTP_PARSE_INVALID_HEADER      14


/* unused                                  1 */
#define NGX_HTTP_SUBREQUEST_IN_MEMORY      2
#define NGX_HTTP_SUBREQUEST_WAITED         4
#define NGX_HTTP_SUBREQUEST_CLONE          8
#define NGX_HTTP_SUBREQUEST_BACKGROUND     16

#define NGX_HTTP_LOG_UNSAFE                1


#define NGX_HTTP_CONTINUE                  100
#define NGX_HTTP_SWITCHING_PROTOCOLS       101
#define NGX_HTTP_PROCESSING                102
#define NGX_HTTP_EARLY_HINTS               103

#define NGX_HTTP_OK                        200
#define NGX_HTTP_CREATED                   201
#define NGX_HTTP_ACCEPTED                  202
#define NGX_HTTP_NO_CONTENT                204
#define NGX_HTTP_PARTIAL_CONTENT           206

#define NGX_HTTP_SPECIAL_RESPONSE          300
#define NGX_HTTP_MOVED_PERMANENTLY         301
#define NGX_HTTP_MOVED_TEMPORARILY         302
#define NGX_HTTP_SEE_OTHER                 303
#define NGX_HTTP_NOT_MODIFIED              304
#define NGX_HTTP_TEMPORARY_REDIRECT        307
#define NGX_HTTP_PERMANENT_REDIRECT        308

#define NGX_HTTP_BAD_REQUEST               400
#define NGX_HTTP_UNAUTHORIZED              401
#define NGX_HTTP_FORBIDDEN                 403
#define NGX_HTTP_NOT_FOUND                 404
#define NGX_HTTP_NOT_ALLOWED               405
#define NGX_HTTP_PROXY_AUTH_REQUIRED       407
#define NGX_HTTP_REQUEST_TIME_OUT          408
#define NGX_HTTP_CONFLICT                  409
#define NGX_HTTP_LENGTH_REQUIRED           411
#define NGX_HTTP_PRECONDITION_FAILED       412
#define NGX_HTTP_REQUEST_ENTITY_TOO_LARGE  413
#define NGX_HTTP_REQUEST_URI_TOO_LARGE     414
#define NGX_HTTP_UNSUPPORTED_MEDIA_TYPE    415
#define NGX_HTTP_RANGE_NOT_SATISFIABLE     416
#define NGX_HTTP_MISDIRECTED_REQUEST       421
#define NGX_HTTP_TOO_MANY_REQUESTS         429


/* Our own HTTP codes */

/* The special code to close connection without any response */
#define NGX_HTTP_CLOSE                     444

#define NGX_HTTP_NGINX_CODES               494

#define NGX_HTTP_REQUEST_HEADER_TOO_LARGE  494

#define NGX_HTTPS_CERT_ERROR               495
#define NGX_HTTPS_NO_CERT                  496

/*
 * We use the special code for the plain HTTP requests that are sent to
 * HTTPS port to distinguish it from 4XX in an error page redirection
 */
#define NGX_HTTP_TO_HTTPS                  497

/* 498 is the canceled code for the requests with invalid host name */

/*
 * HTTP does not define the code for the case when a client closed
 * the connection while we are processing its request so we introduce
 * own code to log such situation when a client has closed the connection
 * before we even try to send the HTTP header to it
 */
#define NGX_HTTP_CLIENT_CLOSED_REQUEST     499


#define NGX_HTTP_INTERNAL_SERVER_ERROR     500
#define NGX_HTTP_NOT_IMPLEMENTED           501
#define NGX_HTTP_BAD_GATEWAY               502
#define NGX_HTTP_SERVICE_UNAVAILABLE       503
#define NGX_HTTP_GATEWAY_TIME_OUT          504
#define NGX_HTTP_VERSION_NOT_SUPPORTED     505
#define NGX_HTTP_INSUFFICIENT_STORAGE      507


#define NGX_HTTP_LOWLEVEL_BUFFERED         0xf0
#define NGX_HTTP_WRITE_BUFFERED            0x10
#define NGX_HTTP_GZIP_BUFFERED             0x20
#define NGX_HTTP_SSI_BUFFERED              0x01
#define NGX_HTTP_SUB_BUFFERED              0x02
#define NGX_HTTP_COPY_BUFFERED             0x04


typedef enum {
    NGX_HTTP_INITING_REQUEST_STATE = 0,
    NGX_HTTP_READING_REQUEST_STATE,
    NGX_HTTP_PROCESS_REQUEST_STATE,

    NGX_HTTP_CONNECT_UPSTREAM_STATE,
    NGX_HTTP_WRITING_UPSTREAM_STATE,
    NGX_HTTP_READING_UPSTREAM_STATE,

    NGX_HTTP_WRITING_REQUEST_STATE,
    NGX_HTTP_LINGERING_CLOSE_STATE,
    NGX_HTTP_KEEPALIVE_STATE
} ngx_http_state_e;


typedef struct {
    ngx_str_t                         name;
    ngx_uint_t                        offset;
    ngx_http_header_handler_pt        handler;
} ngx_http_header_t;


typedef struct {
    ngx_str_t                         name;
    ngx_uint_t                        offset;
} ngx_http_header_out_t;


typedef struct {
    ngx_list_t                        headers;//存储所有header
    ngx_uint_t                        count;

    //下面的成员变量都是指向headers链表中对应header的指针，方便快速访问
    ngx_table_elt_t                  *host;
    ngx_table_elt_t                  *connection;
    ngx_table_elt_t                  *if_modified_since;
    ngx_table_elt_t                  *if_unmodified_since;
    ngx_table_elt_t                  *if_match;
    ngx_table_elt_t                  *if_none_match;
    ngx_table_elt_t                  *user_agent;
    ngx_table_elt_t                  *referer;
    ngx_table_elt_t                  *content_length;
    ngx_table_elt_t                  *content_range;
    ngx_table_elt_t                  *content_type;

    ngx_table_elt_t                  *range;//从资源的第几个字节开始传，断点续传
    ngx_table_elt_t                  *if_range;//一个tag，配合range使用，当断点续传的时候，如果资源变了，那么得重新传，强制range = 0，否则range才有效

    ngx_table_elt_t                  *transfer_encoding;
    ngx_table_elt_t                  *te;
    ngx_table_elt_t                  *expect;
    ngx_table_elt_t                  *upgrade;

#if (NGX_HTTP_GZIP || NGX_HTTP_HEADERS)
    ngx_table_elt_t                  *accept_encoding;
    ngx_table_elt_t                  *via;
#endif

    ngx_table_elt_t                  *authorization;
    ngx_table_elt_t                  *proxy_authorization;

    ngx_table_elt_t                  *keep_alive;

#if (NGX_HTTP_X_FORWARDED_FOR)
    ngx_table_elt_t                  *x_forwarded_for;
#endif

#if (NGX_HTTP_REALIP)
    ngx_table_elt_t                  *x_real_ip;
#endif

#if (NGX_HTTP_HEADERS)
    ngx_table_elt_t                  *accept;
    ngx_table_elt_t                  *accept_language;
#endif

#if (NGX_HTTP_DAV)
    ngx_table_elt_t                  *depth;
    ngx_table_elt_t                  *destination;
    ngx_table_elt_t                  *overwrite;
    ngx_table_elt_t                  *date;
#endif

    ngx_table_elt_t                  *cookie;

    ngx_str_t                         user;
    ngx_str_t                         passwd;

    ngx_str_t                         server;
    off_t                             content_length_n;
    time_t                            keep_alive_n;

    unsigned                          connection_type:2;
    unsigned                          chunked:1;
    unsigned                          multi:1;
    unsigned                          multi_linked:1;
    unsigned                          msie:1;
    unsigned                          msie6:1;
    unsigned                          opera:1;
    unsigned                          gecko:1;
    unsigned                          chrome:1;
    unsigned                          safari:1;
    unsigned                          konqueror:1;
} ngx_http_headers_in_t;


typedef struct {
    ngx_list_t                        headers;
    ngx_list_t                        trailers;

    ngx_uint_t                        status;
    ngx_str_t                         status_line;

    ngx_table_elt_t                  *server;
    ngx_table_elt_t                  *date;
    ngx_table_elt_t                  *content_length;
    ngx_table_elt_t                  *content_encoding;
    ngx_table_elt_t                  *location;
    ngx_table_elt_t                  *refresh;
    ngx_table_elt_t                  *last_modified;
    ngx_table_elt_t                  *content_range;
    ngx_table_elt_t                  *accept_ranges;
    ngx_table_elt_t                  *www_authenticate;
    ngx_table_elt_t                  *proxy_authenticate;
    ngx_table_elt_t                  *expires;
    ngx_table_elt_t                  *etag;

    ngx_table_elt_t                  *cache_control;
    ngx_table_elt_t                  *link;

    ngx_str_t                        *override_charset;

    size_t                            content_type_len;
    ngx_str_t                         content_type;
    ngx_str_t                         charset;
    u_char                           *content_type_lowcase;
    ngx_uint_t                        content_type_hash;

    off_t                             content_length_n;
    off_t                             content_offset;
    time_t                            date_time;
    time_t                            last_modified_time;
} ngx_http_headers_out_t;


typedef void (*ngx_http_client_body_handler_pt)(ngx_http_request_t *r);

/*
busy、free、bufs的关系
    busy是bufs的超集，每当调用ngx_chain_update_chains的时候会把bufs中buf放到busy
    同时把消费了的busy中的buf放到free

buf和bufs的关系
    bufs是当前buf的窗口

当工作区满后，将bufs写入到文件中，并清空bufs和buf，进行新一轮的读写

*/
typedef struct {
    /*
    临时文件。包体超过 client_body_buffer_size时，ngx_http_write_request_body() 惰性创建它
    ，随后超过缓冲的部分都写文件；bufs 里于是出现 in_file 的 buf。“惰性”意味着小请求根本不落盘
    */
    ngx_temp_file_t                  *temp_file;
    /*
    已收包体数据的链表（内存 buf + 可能指向临时文件的 file buf）。这就是最终交给调用方的东西——如 fastcgi/proxy 
    直接 r->request_body->bufs 发上游；也是 ngx_http_write_request_body() 往临时文件写的输入
    */
    ngx_chain_t                      *bufs;
    //当前用于 recv 的工作缓冲，一直指向一块内存不会修改
    ngx_buf_t                        *buf;
    //还需接收的字节数。-1 是特殊初值，表示“长度未知”
    off_t                             rest;
    //累计已收字节数
    off_t                             received;
    ngx_chain_t                      *free;
    ngx_chain_t                      *busy;
    //chunked 解码器的状态。只在 chunked 请求时分配，ngx_http_parse_chunked() 的多次调用之间靠它记住“读到 chunk 头还是 chunk 体”
    ngx_http_chunked_t               *chunked;
    //收完后的回调
    ngx_http_client_body_handler_pt   post_handler;
    unsigned                          filter_need_buffering:1;
    unsigned                          last_sent:1;
    //当最后的字节到达的时候置1
    unsigned                          last_saved:1;
} ngx_http_request_body_t;


typedef struct ngx_http_addr_conf_s  ngx_http_addr_conf_t;

typedef struct {
    ngx_http_addr_conf_t             *addr_conf;
    ngx_http_conf_ctx_t              *conf_ctx;

#if (NGX_HTTP_SSL || NGX_COMPAT)
    ngx_str_t                        *ssl_servername;
#if (NGX_PCRE)
    ngx_http_regex_t                 *ssl_servername_regex;
#endif
#endif

    ngx_chain_t                      *busy;
    ngx_int_t                         nbusy;

    ngx_chain_t                      *free;

    ngx_msec_t                        keepalive_timeout;

    unsigned                          ssl:1;
    unsigned                          proxy_protocol:1;
} ngx_http_connection_t;


typedef void (*ngx_http_cleanup_pt)(void *data);

typedef struct ngx_http_cleanup_s  ngx_http_cleanup_t;

struct ngx_http_cleanup_s {
    ngx_http_cleanup_pt               handler;
    void                             *data;
    ngx_http_cleanup_t               *next;
};


typedef ngx_int_t (*ngx_http_post_subrequest_pt)(ngx_http_request_t *r,
    void *data, ngx_int_t rc);

typedef struct {
    ngx_http_post_subrequest_pt       handler;
    void                             *data;
} ngx_http_post_subrequest_t;


typedef struct ngx_http_postponed_request_s  ngx_http_postponed_request_t;

struct ngx_http_postponed_request_s {
    ngx_http_request_t               *request;
    ngx_chain_t                      *out;
    ngx_http_postponed_request_t     *next;
};


typedef struct ngx_http_posted_request_s  ngx_http_posted_request_t;

struct ngx_http_posted_request_s {
    ngx_http_request_t               *request;
    ngx_http_posted_request_t        *next;
};


typedef ngx_int_t (*ngx_http_handler_pt)(ngx_http_request_t *r);
typedef void (*ngx_http_event_handler_pt)(ngx_http_request_t *r);


struct ngx_http_request_s {
    uint32_t                          signature;         /* "HTTP" */

    ngx_connection_t                 *connection;
    //注意这个和parse conf里分配的moudle没关系。这个是一个request产生后分配的，作用域是每个请求
    //当ctx[ctx_index]!=null的时候说明该request已经被该模块处理过了，且保存当前的模块处理的上下文
    /*
    ctx中具体的模块结构体由模块惰性创建
    rewrite 模块：保存匹配后的 URI、捕获组等（ngx_http_rewrite_module 的 ngx_http_request_t 相关状态）。
    upstream/fastcgi 模块：存放后端连接、已解析响应头等运行时状态。
    log 模块、chunked filter 等也在其中保存每个请求的中间数据。
    */
    void                            **ctx;
    /*
    作用域是全局
    */
    void                            **main_conf;
    void                            **srv_conf;
    void                            **loc_conf;
    //read_event_handler和write_event_handler被connection的read和write的handler调用
    ngx_http_event_handler_pt         read_event_handler;
    ngx_http_event_handler_pt         write_event_handler;

#if (NGX_HTTP_CACHE)
    ngx_http_cache_t                 *cache;
#endif

    ngx_http_upstream_t              *upstream;
    /*
    元素是 ngx_http_upstream_state_t（记录一次连接后端尝试的对端地址、connect_time、header_time、response_time、status、收发字节数）。
    u->state 指向属于当前这次尝试的那个元素。重试一次上游（next_upstream 逻辑）就会多一条记录
    */
    ngx_array_t                      *upstream_states;//保存本次 HTTP 请求所有 upstream 尝试的状态数组
                                         /* of ngx_http_upstream_state_t */

    ngx_pool_t                       *pool;
    ngx_buf_t                        *header_in;//请求头（连带可能捎带的 body 数据）的输入缓冲区

    //区分headers_in和header_in，前者是解析后的请求头，后者是原始请求头
    ngx_http_headers_in_t             headers_in;
    ngx_http_headers_out_t            headers_out;

    ngx_http_request_body_t          *request_body;

    time_t                            lingering_time;
    time_t                            start_sec;
    ngx_msec_t                        start_msec;

    ngx_uint_t                        method;
    ngx_uint_t                        http_version;

    ngx_str_t                         request_line;
    ngx_str_t                         uri;
    ngx_str_t                         args;
    ngx_str_t                         exten;
    ngx_str_t                         unparsed_uri;//？？

    ngx_str_t                         method_name;
    ngx_str_t                         http_protocol;
    ngx_str_t                         schema;

    ngx_chain_t                      *out;
    ngx_http_request_t               *main;
    ngx_http_request_t               *parent;
    ngx_http_postponed_request_t     *postponed;
    ngx_http_post_subrequest_t       *post_subrequest;
    ngx_http_posted_request_t        *posted_requests;
    /*
    
    */
    ngx_int_t                         phase_handler;
    ngx_http_handler_pt               content_handler;
    ngx_uint_t                        access_code;

    ngx_http_variable_value_t        *variables;//variables长度和cmcf->variable一样长

#if (NGX_PCRE)
    ngx_uint_t                        ncaptures;
    int                              *captures;
    u_char                           *captures_data;
#endif

    size_t                            limit_rate;
    size_t                            limit_rate_after;

    /* used to learn the Apache compatible response length without a header */
    size_t                            header_size;

    off_t                             request_length;

    ngx_uint_t                        err_status;

    ngx_http_connection_t            *http_connection;
    ngx_http_v2_stream_t             *stream;
    ngx_http_v3_parse_t              *v3_parse;

    ngx_http_log_handler_pt           log_handler;

    ngx_http_cleanup_t               *cleanup;//清理回调？

    in_port_t                         port;

    unsigned                          count:16;//引用计数
    unsigned                          subrequests:8;
    unsigned                          blocked:8;

    unsigned                          aio:1;

    unsigned                          http_state:4;

    /* URI with "/." and on Win32 with "//" */
    unsigned                          complex_uri:1;

    /* URI with "%" */
    unsigned                          quoted_uri:1;

    /* URI with "+" */
    unsigned                          plus_in_uri:1;

    /* URI with empty path */
    unsigned                          empty_path_in_uri:1;

    unsigned                          invalid_header:1;

    unsigned                          add_uri_to_alias:1;
    //当前 URI 是否仍适合依据原 location 进行前缀替换
    /*
    /api/1 rewrite-> /api/12   valid_location = 1
    /api/1 rewrite-> /ap/1     valid_location = 0
    */
    unsigned                          valid_location:1;
    /*
    为什么要有 valid_unparsed_uri？
    因为 r->unparsed_uri 是客户端原始请求目标，而 r->uri 是 Nginx 当前处理的、已经规范化或可能经过改写的 URI 路径。
    比如：
    location /api/ {
        rewrite ^/api/(.*)$ /internal/$1 break;
        proxy_pass http://127.0.0.1:8080;
    }
    客户端访问：
    GET /api/user?id=10 HTTP/1.1
    经过 rewrite：
    r->uri = "/internal/user";
    r->args = "id=10";
    原始请求目标仍可能保存着：
    r->unparsed_uri = "/api/user?id=10";
    但它已经不符合当前要代理的 URI 了，所以改写逻辑会使：
    r->valid_unparsed_uri = 0;
    */
    unsigned                          valid_unparsed_uri:1;
    unsigned                          uri_changed:1;
    unsigned                          uri_changes:4;

    unsigned                          request_body_in_single_buf:1;
    unsigned                          request_body_in_file_only:1;
    unsigned                          request_body_in_persistent_file:1;
    unsigned                          request_body_in_clean_file:1;
    unsigned                          request_body_file_group_access:1;
    unsigned                          request_body_file_log_level:3;
    unsigned                          request_body_no_buffering:1;

    unsigned                          subrequest_in_memory:1;
    unsigned                          waited:1;

#if (NGX_HTTP_CACHE)
    unsigned                          cached:1;
#endif

#if (NGX_HTTP_GZIP)
    unsigned                          gzip_tested:1;
    unsigned                          gzip_ok:1;
    unsigned                          gzip_vary:1;
#endif

#if (NGX_PCRE)
    unsigned                          realloc_captures:1;
#endif

    unsigned                          proxy:1;
    unsigned                          bypass_cache:1;
    unsigned                          no_cache:1;

    /*
     * instead of using the request context data in
     * ngx_http_limit_conn_module and ngx_http_limit_req_module
     * we use the bit fields in the request structure
     */
    unsigned                          limit_conn_status:2;
    unsigned                          limit_req_status:3;

    unsigned                          limit_rate_set:1;
    unsigned                          limit_rate_after_set:1;

#if 0
    unsigned                          cacheable:1;
#endif

    unsigned                          pipeline:1;
    unsigned                          chunked:1;
    unsigned                          header_only:1;
    unsigned                          expect_trailers:1;
    unsigned                          keepalive:1;
    unsigned                          lingering_close:1;
    unsigned                          discard_body:1;
    unsigned                          reading_body:1;
    unsigned                          internal:1;
    unsigned                          error_page:1;
    unsigned                          filter_finalize:1;
    /*
    0：请求尚未进入 post_action 后置处理阶段。
    1：请求已经进入 post_action 后置处理阶段。
    在 Nginx 中，后置处理阶段（post_action）的主要作用是：在当前 HTTP 请求的正常业务处理结束后
    ，再执行一个额外的内部请求，用来完成日志上报、流量统计、审计等辅助任务。
    */
    unsigned                          post_action:1;
    unsigned                          request_complete:1;
    unsigned                          request_output:1;
    unsigned                          header_sent:1;
    unsigned                          response_sent:1;
    unsigned                          expect_tested:1;
    unsigned                          root_tested:1;
    unsigned                          done:1;
    unsigned                          logged:1;
    unsigned                          terminated:1;

    unsigned                          buffered:4;

    unsigned                          main_filter_need_in_memory:1;
    unsigned                          filter_need_in_memory:1;
    unsigned                          filter_need_temporary:1;
    unsigned                          preserve_body:1;
    unsigned                          allow_ranges:1;
    unsigned                          subrequest_ranges:1;
    unsigned                          single_range:1;
    unsigned                          disable_not_modified:1;
    unsigned                          stat_reading:1;
    unsigned                          stat_writing:1;
    unsigned                          stat_processing:1;

    unsigned                          background:1;
    unsigned                          health_check:1;

    /* used to parse HTTP headers */

    ngx_uint_t                        state;

    ngx_uint_t                        header_hash;
    ngx_uint_t                        lowcase_index;
    u_char                            lowcase_header[NGX_HTTP_LC_HEADER_LEN];

    u_char                           *header_name_start;
    u_char                           *header_name_end;
    u_char                           *header_start;
    u_char                           *header_end;

    /*
     * a memory that can be reused after parsing a request line
     * via ngx_http_ephemeral_t
     */

    u_char                           *uri_start;
    u_char                           *uri_end;
    u_char                           *uri_ext;
    u_char                           *args_start;
    u_char                           *request_start;
    u_char                           *request_end;
    u_char                           *method_end;
    u_char                           *schema_start;
    u_char                           *schema_end;
    u_char                           *host_start;
    u_char                           *host_end;

    unsigned                          http_minor:16;
    unsigned                          http_major:16;
};


typedef struct {
    ngx_http_posted_request_t         terminal_posted_request;
} ngx_http_ephemeral_t;


#define ngx_http_ephemeral(r)   (void *) (&r->uri_start)

#define ngx_http_proxy_auth(r)  ((r)->method == NGX_HTTP_CONNECT)


extern ngx_http_header_t       ngx_http_headers_in[];
extern ngx_http_header_out_t   ngx_http_headers_out[];


#define ngx_http_set_log_request(log, r)                                      \
    ((ngx_http_log_ctx_t *) log->data)->current_request = r


#endif /* _NGX_HTTP_REQUEST_H_INCLUDED_ */
