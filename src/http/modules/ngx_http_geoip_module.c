
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include <GeoIP.h>
#include <GeoIPCity.h>


typedef struct {
    GeoIP      *country;
    GeoIP      *city;
} ngx_http_geoip_conf_t;


typedef struct {
    ngx_str_t  *name;
    uintptr_t   data;
} ngx_http_geoip_var_t;


typedef const char *(*ngx_http_geoip_variable_handler_pt)(GeoIP *, u_long addr);

static ngx_int_t ngx_http_geoip_country_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_geoip_city_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_geoip_region_name_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_geoip_city_float_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_geoip_city_int_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static GeoIPRecord *ngx_http_geoip_get_city_record(ngx_http_request_t *r);

static ngx_int_t ngx_http_geoip_add_variables(ngx_conf_t *cf);
static void *ngx_http_geoip_create_conf(ngx_conf_t *cf);
static char *ngx_http_geoip_country(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_geoip_city(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static void ngx_http_geoip_cleanup(void *data);


static ngx_command_t  ngx_http_geoip_commands[] = {

    { ngx_string("geoip_country"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_http_geoip_country,
      NGX_HTTP_MAIN_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("geoip_city"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_http_geoip_city,
      NGX_HTTP_MAIN_CONF_OFFSET,
      0,
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_geoip_module_ctx = {
    ngx_http_geoip_add_variables,          /* preconfiguration */
    NULL,                                  /* postconfiguration */

    ngx_http_geoip_create_conf,            /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    NULL,                                  /* create location configuration */
    NULL                                   /* merge location configuration */
};


ngx_module_t  ngx_http_geoip_module = {
    NGX_MODULE_V1,
    &ngx_http_geoip_module_ctx,            /* module context */
    ngx_http_geoip_commands,               /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_http_variable_t  ngx_http_geoip_vars[] = {

    { ngx_string("geoip_country_code"), NULL,
      ngx_http_geoip_country_variable,
      (uintptr_t) GeoIP_country_code_by_ipnum, 0, 0 },

    { ngx_string("geoip_country_code3"), NULL,
      ngx_http_geoip_country_variable,
      (uintptr_t) GeoIP_country_code3_by_ipnum, 0, 0 },

    { ngx_string("geoip_country_name"), NULL,
      ngx_http_geoip_country_variable,
      (uintptr_t) GeoIP_country_name_by_ipnum, 0, 0 },

    { ngx_string("geoip_city_continent_code"), NULL,
      ngx_http_geoip_city_variable,
      offsetof(GeoIPRecord, continent_code), 0, 0 },

    { ngx_string("geoip_city_country_code"), NULL,
      ngx_http_geoip_city_variable,
      offsetof(GeoIPRecord, country_code), 0, 0 },

    { ngx_string("geoip_city_country_code3"), NULL,
      ngx_http_geoip_city_variable,
      offsetof(GeoIPRecord, country_code3), 0, 0 },

    { ngx_string("geoip_city_country_name"), NULL,
      ngx_http_geoip_city_variable,
      offsetof(GeoIPRecord, country_name), 0, 0 },

    { ngx_string("geoip_region"), NULL,
      ngx_http_geoip_city_variable,
      offsetof(GeoIPRecord, region), 0, 0 },

    { ngx_string("geoip_region_name"), NULL,
      ngx_http_geoip_region_name_variable,
      0, 0, 0 },

    { ngx_string("geoip_city"), NULL,
      ngx_http_geoip_city_variable,
      offsetof(GeoIPRecord, city), 0, 0 },

    { ngx_string("geoip_postal_code"), NULL,
      ngx_http_geoip_city_variable,
      offsetof(GeoIPRecord, postal_code), 0, 0 },

    { ngx_string("geoip_latitude"), NULL,
      ngx_http_geoip_city_float_variable,
      offsetof(GeoIPRecord, latitude), 0, 0 },

    { ngx_string("geoip_longitude"), NULL,
      ngx_http_geoip_city_float_variable,
      offsetof(GeoIPRecord, longitude), 0, 0 },

    { ngx_string("geoip_dma_code"), NULL,
      ngx_http_geoip_city_int_variable,
      offsetof(GeoIPRecord, dma_code), 0, 0 },

    { ngx_string("geoip_area_code"), NULL,
      ngx_http_geoip_city_int_variable,
      offsetof(GeoIPRecord, area_code), 0, 0 },

    { ngx_null_string, NULL, NULL, 0, 0, 0 }
};


static ngx_int_t
ngx_http_geoip_country_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    ngx_http_geoip_variable_handler_pt  handler =
        (ngx_http_geoip_variable_handler_pt) data;

    u_long                  addr;
    const char             *val;
    struct sockaddr_in     *sin;
    ngx_http_geoip_conf_t  *gcf;

    gcf = ngx_http_get_module_main_conf(r, ngx_http_geoip_module);

    if (gcf->country == NULL) {
        goto not_found;
    }

    if (r->connection->sockaddr->sa_family != AF_INET) {
        goto not_found;
    }

    sin = (struct sockaddr_in *) r->connection->sockaddr;
    addr = ntohl(sin->sin_addr.s_addr);

    val = handler(gcf->country, addr);

    if (val == NULL) {
        goto not_found;
    }

    v->len = ngx_strlen(val);
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;
    v->data = (u_char *) val;

    return NGX_OK;

not_found:

    v->not_found = 1;

    return NGX_OK;
}


static ngx_int_t
ngx_http_geoip_city_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    char         *val;
    size_t        len;
    GeoIPRecord  *gr;

    gr = ngx_http_geoip_get_city_record(r);
    if (gr == NULL) {
        goto not_found;
    }

    val = *(char **) ((char *) gr + data);
    if (val == NULL) {
        goto no_value;
    }

    len = ngx_strlen(val);
    v->data = ngx_pnalloc(r->pool, len);
    if (v->data == NULL) {
        GeoIPRecord_delete(gr);
        return NGX_ERROR;
    }

    ngx_memcpy(v->data, val, len);

    v->len = len;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;

    GeoIPRecord_delete(gr);

    return NGX_OK;

no_value:

    GeoIPRecord_delete(gr);

not_found:

    v->not_found = 1;

    return NGX_OK;
}


static ngx_int_t
ngx_http_geoip_region_name_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    size_t        len;
    const char   *val;
    GeoIPRecord  *gr;

    gr = ngx_http_geoip_get_city_record(r);
    if (gr == NULL) {
        goto not_found;
    }

    val = GeoIP_region_name_by_code(gr->country_code, gr->region);

    GeoIPRecord_delete(gr);

    if (val == NULL) {
        goto not_found;
    }

    len = ngx_strlen(val);
    v->data = ngx_pnalloc(r->pool, len);
    if (v->data == NULL) {
        return NGX_ERROR;
    }

    ngx_memcpy(v->data, val, len);

    v->len = len;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;

    return NGX_OK;

not_found:

    v->not_found = 1;

    return NGX_OK;
}


static ngx_int_t
ngx_http_geoip_city_float_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    float         val;
    GeoIPRecord  *gr;

    gr = ngx_http_geoip_get_city_record(r);
    if (gr == NULL) {
        v->not_found = 1;
        return NGX_OK;
    }

    v->data = ngx_pnalloc(r->pool, NGX_INT64_LEN + 5);
    if (v->data == NULL) {
        GeoIPRecord_delete(gr);
        return NGX_ERROR;
    }

    val = *(float *) ((char *) gr + data);

    v->len = ngx_sprintf(v->data, "%.4f", val) - v->data;

    GeoIPRecord_delete(gr);

    return NGX_OK;
}


static ngx_int_t
ngx_http_geoip_city_int_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    int           val;
    GeoIPRecord  *gr;

    gr = ngx_http_geoip_get_city_record(r);
    if (gr == NULL) {
        v->not_found = 1;
        return NGX_OK;
    }

    v->data = ngx_pnalloc(r->pool, NGX_INT64_LEN);
    if (v->data == NULL) {
        GeoIPRecord_delete(gr);
        return NGX_ERROR;
    }

    val = *(int *) ((char *) gr + data);

    v->len = ngx_sprintf(v->data, "%d", val) - v->data;

    GeoIPRecord_delete(gr);

    return NGX_OK;
}


static GeoIPRecord *
ngx_http_geoip_get_city_record(ngx_http_request_t *r)
{
    u_long                  addr;
    struct sockaddr_in     *sin;
    ngx_http_geoip_conf_t  *gcf;

    gcf = ngx_http_get_module_main_conf(r, ngx_http_geoip_module);

    if (gcf->city && r->connection->sockaddr->sa_family == AF_INET) {

        sin = (struct sockaddr_in *) r->connection->sockaddr;
        addr = ntohl(sin->sin_addr.s_addr);

        return GeoIP_record_by_ipnum(gcf->city, addr);
    }

    return NULL;
}


static ngx_int_t
ngx_http_geoip_add_variables(ngx_conf_t *cf)
{
    ngx_http_variable_t  *var, *v;

    for (v = ngx_http_geoip_vars; v->name.len; v++) {
        var = ngx_http_add_variable(cf, &v->name, v->flags);
        if (var == NULL) {
            return NGX_ERROR;
        }

        var->get_handler = v->get_handler;
        var->data = v->data;
    }

    return NGX_OK;
}


static void *
ngx_http_geoip_create_conf(ngx_conf_t *cf)
{
    ngx_pool_cleanup_t     *cln;
    ngx_http_geoip_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_geoip_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    cln = ngx_pool_cleanup_add(cf->pool, 0);
    if (cln == NULL) {
        return NULL;
    }

    cln->handler = ngx_http_geoip_cleanup;
    cln->data = conf;

    return conf;
}


static char *
ngx_http_geoip_country(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_geoip_conf_t  *gcf = conf;

    ngx_str_t  *value;

    if (gcf->country) {
        return "is duplicate";
    }

    value = cf->args->elts;

    gcf->country = GeoIP_open((char *) value[1].data, GEOIP_MEMORY_CACHE);

    if (gcf->country == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "GeoIO_open(\"%V\") failed", &value[1]);

        return NGX_CONF_ERROR;
    }

    switch (gcf->country->databaseType) {

    case GEOIP_COUNTRY_EDITION:
    case GEOIP_PROXY_EDITION:
    case GEOIP_NETSPEED_EDITION:

        return NGX_CONF_OK;

    default:
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid GeoIP database \"%V\" type:%d",
                           &value[1], gcf->country->databaseType);
        return NGX_CONF_ERROR;
    }
}


static char *
ngx_http_geoip_city(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_geoip_conf_t  *gcf = conf;

    ngx_str_t  *value;

    if (gcf->city) {
        return "is duplicate";
    }

    value = cf->args->elts;

    gcf->city = GeoIP_open((char *) value[1].data, GEOIP_MEMORY_CACHE);

    if (gcf->city == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "GeoIO_open(\"%V\") failed", &value[1]);

        return NGX_CONF_ERROR;
    }

    switch (gcf->city->databaseType) {

    case GEOIP_CITY_EDITION_REV0:
    case GEOIP_CITY_EDITION_REV1:

        return NGX_CONF_OK;

    default:
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid GeoIP City database \"%V\" type:%d",
                           &value[1], gcf->city->databaseType);
        return NGX_CONF_ERROR;
    }
}


static void
ngx_http_geoip_cleanup(void *data)
{
    ngx_http_geoip_conf_t  *gcf = data;

    if (gcf->country) {
        GeoIP_delete(gcf->country);
    }

    if (gcf->city) {
        GeoIP_delete(gcf->city);
    }
}
