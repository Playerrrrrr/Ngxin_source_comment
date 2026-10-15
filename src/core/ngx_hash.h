
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_HASH_H_INCLUDED_
#define _NGX_HASH_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>

//表示哈希表中桶里的一个元素，由ngx_hash_key_t转换而来，包含了key的长度、key的内容和value的指针
typedef struct {
    void             *value;//value指针用于存储哈希表中key对应的值，非空表示该元素有效，空表示该元素是哨兵，表示该桶的结束
    u_short           len;
    u_char            name[1];
} ngx_hash_elt_t;


typedef struct {
    ngx_hash_elt_t  **buckets;
    ngx_uint_t        size;
} ngx_hash_t;//表示哈希表，包含了桶的指针和桶的数量


typedef struct {
    ngx_hash_t        hash;
    void             *value;//该层的一个业务指针
} ngx_hash_wildcard_t;//表示通配符哈希表，包含了一个哈希表和一个value指针，value指针用于存储通配符匹配的值


typedef struct {
    ngx_str_t         key;
    ngx_uint_t        key_hash;
    void             *value;
} ngx_hash_key_t;


typedef ngx_uint_t (*ngx_hash_key_pt) (u_char *data, size_t len);


typedef struct {
    ngx_hash_t            hash;
    ngx_hash_wildcard_t  *wc_head;
    ngx_hash_wildcard_t  *wc_tail;
} ngx_hash_combined_t;

//初始化哈希表的结构体，包含了哈希表指针、哈希函数指针、最大桶数量、每个桶的大小、哈希表名称、内存池和临时内存池
//这个结构体并不提供具体的数据，只提供初始化用到的参数
//hash是回参
typedef struct {
    ngx_hash_t       *hash;
    ngx_hash_key_pt   key;//hash函数指针，用于计算key的hash值

    ngx_uint_t        max_size;//最大hash表的bucket数量
    ngx_uint_t        bucket_size;//每个bucket的大小，单位是字节

    char             *name;
    ngx_pool_t       *pool;
    ngx_pool_t       *temp_pool;
} ngx_hash_init_t;


#define NGX_HASH_SMALL            1
#define NGX_HASH_LARGE            2

#define NGX_HASH_LARGE_ASIZE      16384
#define NGX_HASH_LARGE_HSIZE      10007

#define NGX_HASH_WILDCARD_KEY     1
#define NGX_HASH_READONLY_KEY     2


typedef struct {
    ngx_uint_t        hsize;

    ngx_pool_t       *pool;
    ngx_pool_t       *temp_pool;

    //全匹配
    ngx_array_t       keys;//全匹配用普通hash表，元素类型是ngx_hash_key_t，作为names参数传入ngx_hash_init
    //相对于应该二维数组，第一个维度是ngx_array_t，第二个维度是 ngx_string_t
    //第一个维度的大小是hsize，其作用是查重
    ngx_array_t      *keys_hash;

    //匹配b.com.cn
    ngx_array_t       dns_wc_head;//与key类似，作为names参数传入ngx_hash_wildcard_init
    ngx_array_t      *dns_wc_head_hash;//与keys_hash类似

    ngx_array_t       dns_wc_tail;
    ngx_array_t      *dns_wc_tail_hash;
} ngx_hash_keys_arrays_t;


typedef struct ngx_table_elt_s  ngx_table_elt_t;

struct ngx_table_elt_s {
    ngx_uint_t        hash;
    ngx_str_t         key;
    ngx_str_t         value;
    u_char           *lowcase_key;
    ngx_table_elt_t  *next;
};


void *ngx_hash_find(ngx_hash_t *hash, ngx_uint_t key, u_char *name, size_t len);
void *ngx_hash_find_wc_head(ngx_hash_wildcard_t *hwc, u_char *name, size_t len);
void *ngx_hash_find_wc_tail(ngx_hash_wildcard_t *hwc, u_char *name, size_t len);
void *ngx_hash_find_combined(ngx_hash_combined_t *hash, ngx_uint_t key,
    u_char *name, size_t len);

ngx_int_t ngx_hash_init(ngx_hash_init_t *hinit, ngx_hash_key_t *names,
    ngx_uint_t nelts);
ngx_int_t ngx_hash_wildcard_init(ngx_hash_init_t *hinit, ngx_hash_key_t *names,
    ngx_uint_t nelts);

#define ngx_hash(key, c)   ((ngx_uint_t) key * 31 + c)
ngx_uint_t ngx_hash_key(u_char *data, size_t len);
ngx_uint_t ngx_hash_key_lc(u_char *data, size_t len);
ngx_uint_t ngx_hash_strlow(u_char *dst, u_char *src, size_t n);

//初始化ngx_hash_keys_arrays_t
/*
从temp_pool中分配这几个字段的空间
    ngx_array_t       keys;
    ngx_array_t      *keys_hash;
    ngx_array_t       dns_wc_head;
    ngx_array_t      *dns_wc_head_hash;
    ngx_array_t       dns_wc_tail;
    ngx_array_t      *dns_wc_tail_hash;
*/
ngx_int_t ngx_hash_keys_array_init(ngx_hash_keys_arrays_t *ha, ngx_uint_t type);
ngx_int_t ngx_hash_add_key(ngx_hash_keys_arrays_t *ha, ngx_str_t *key,
    void *value, ngx_uint_t flags);


#endif /* _NGX_HASH_H_INCLUDED_ */
