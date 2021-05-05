
/*
 * Copyright (C) Roman Arutyunyan
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_HTTP_V3_TABLES_H_INCLUDED_
#define _NGX_HTTP_V3_TABLES_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct {
    ngx_str_t                     name;
    ngx_str_t                     value;
} ngx_http_v3_header_t;


typedef struct {
    ngx_http_v3_header_t        **elts;
    ngx_uint_t                    nelts;
    ngx_uint_t                    base;
    size_t                        size;
    size_t                        capacity;
} ngx_http_v3_dynamic_table_t;


ngx_int_t ngx_http_v3_ref_insert(ngx_connection_t *c, ngx_uint_t dynamic,
    ngx_uint_t index, ngx_str_t *value);
ngx_int_t ngx_http_v3_insert(ngx_connection_t *c, ngx_str_t *name,
    ngx_str_t *value);
ngx_int_t ngx_http_v3_set_capacity(ngx_connection_t *c, ngx_uint_t capacity);
ngx_int_t ngx_http_v3_duplicate(ngx_connection_t *c, ngx_uint_t index);
ngx_int_t ngx_http_v3_ack_header(ngx_connection_t *c, ngx_uint_t stream_id);
ngx_int_t ngx_http_v3_inc_insert_count(ngx_connection_t *c, ngx_uint_t inc);
ngx_int_t ngx_http_v3_lookup_static(ngx_connection_t *c, ngx_uint_t index,
    ngx_str_t *name, ngx_str_t *value);
ngx_int_t ngx_http_v3_lookup(ngx_connection_t *c, ngx_uint_t index,
    ngx_str_t *name, ngx_str_t *value);
ngx_int_t ngx_http_v3_decode_insert_count(ngx_connection_t *c,
    ngx_uint_t *insert_count);
ngx_int_t ngx_http_v3_check_insert_count(ngx_connection_t *c,
    ngx_uint_t insert_count);
ngx_int_t ngx_http_v3_set_param(ngx_connection_t *c, uint64_t id,
    uint64_t value);


#endif /* _NGX_HTTP_V3_TABLES_H_INCLUDED_ */
