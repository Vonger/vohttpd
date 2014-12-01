/* vohttpd: simple, light weight, embedded web server.
 *
 * author: Qin Wei(me@vonger.cn)
 * compile: cc -g vohttpd.c vohttpdext.c -o vohttpd
 *
 * TODO: add auth function.
 *   move to plugin loader compose.
 * TODO: add https function.
 *   maybe use polarSSL.
 */

#include <stdlib.h>
#include <memory.h>
#include <stdio.h>

#include "vohttpd.h"

#define safe_free(p)  if(p) { free(p); p = NULL; }

/* linear hash, every unit must start with its key. */
#define LINEAR_HASH_NULL         ((uint)(-1))
#define linear_hash_key(h, p)    (*(uint *)((h)->data + (p) * (h)->unit))
#define linear_hash_value(h, p)  ((h)->data + (p) * (h)->unit)
#define linear_hash_empty(h, p)  (linear_hash_key((h), (p)) == LINEAR_HASH_NULL)
#define linear_hash_clear(h, p)  {linear_hash_key((h), (p)) = LINEAR_HASH_NULL;}

typedef struct _linear_hash {
    uint   unit;            // the size for each unit.
    uint   max;             // the max allowed unit.

    uchar  data[1];
}linear_hash;

linear_hash* linear_hash_alloc(uint unit, uint max)
{
    linear_hash *lh =
        (linear_hash *)malloc(max * unit + sizeof(linear_hash));
    if(lh == NULL)
        return NULL;

    lh->unit = unit;
    lh->max = max;

    while(max--)
        linear_hash_clear(lh, max);
    return lh;
}

uchar* linear_hash_get(linear_hash *lh, uint key)
{
    uint pos = key % lh->max, i;
    // match node in the first hit.
    if(linear_hash_key(lh, pos) == key)
        return linear_hash_value(lh, pos);

    // try to hit next node if we miss the first.
    for(i = pos + 1; ; i++) {
        if(i >= lh->max)
            i = 0;
        if(i == pos)
            break;

        if(linear_hash_key(lh, i) == key)
            return linear_hash_value(lh, i);
    }
    return NULL;
}

uchar* linear_hash_set(linear_hash *lh, uint key)
{
    uint pos = key % lh->max, i;
    // first hit, this hash node is empty.
    if(linear_hash_empty(lh, pos))
        return linear_hash_value(lh, pos);

    // try to find another empty node.
    for(i = pos + 1; ; i++) {
        if(i >= lh->max)
            i = 0;
        if(i == pos)
            break;

        if(linear_hash_empty(lh, i))
            return linear_hash_value(lh, i);
    }
    return NULL;
}

void linear_hash_remove(linear_hash *lh, uint key)
{
    uchar* d = linear_hash_get(lh, key);
    if(d == NULL)
        return;
    linear_hash_clear(lh, (d - lh->data) / lh->unit);
}


/* string hash, get data by string, make it faster. */
typedef linear_hash              string_hash;
#define string_hash_p1(h, p)     ((char *)((h->data + p) + sizeof(uchar *)))
#define string_hash_p2(h, p)     (*((uchar **)(h->data + p)))
#define string_hash_empty(h, p)  (*string_hash_p1((h), (p)) == '\0')
#define string_hash_clear(h, p)  {*string_hash_p1((h), (p)) = '\0';}

string_hash* string_hash_alloc(uint unit, uint max)
{
    string_hash *sh =
        (string_hash *)malloc(max * (unit + sizeof(uchar *)) + sizeof(string_hash));
    if(sh == NULL)
        return NULL;
    memset(sh, 0, max * (unit + sizeof(uchar *)) + sizeof(string_hash));

    sh->unit = unit + sizeof(uchar *);
    sh->max = max;
    return sh;
}

uint string_hash_from(char *str)
{
    uint hash = *str;
    while(*str++)
        hash = hash * 31 + *str;
    return hash;
}

uchar* string_hash_get(string_hash *sh, char *key)
{
    uint pos = (string_hash_from(key) % sh->max) * sh->unit, i;
    // match node in the first hit.
    if(strcmp(string_hash_p1(sh, pos), key) == 0)
        return string_hash_p2(sh, pos);

    // try to hit next node if we miss the first.
    for(i = pos + sh->unit;; i += sh->unit) {
        if(i >= sh->unit * sh->max)
            i = 0;
        if(i == pos)
            break;
        if(strcmp(string_hash_p1(sh, i), key) == 0)
            return string_hash_p2(sh, i);
    }
    return NULL;
}

uchar* string_hash_set(string_hash *sh, char *key, uchar *value)
{
    uint pos = (string_hash_from(key) % sh->max) * sh->unit, i;
    // first hit, this hash node is empty.
    if(string_hash_empty(sh, pos)) {
        strcpy(string_hash_p1(sh, pos), key);
        memcpy(&string_hash_p2(sh, pos), &value, sizeof(uchar *));
        return string_hash_p2(sh, pos);
    }

    // try to find another empty node.
    for(i = pos + sh->unit;; i += sh->unit) {
        if(i >= sh->unit * sh->max)
            i = 0;
        if(i == pos)
            break;

        if(string_hash_empty(sh, i)) {
            strcpy(string_hash_p1(sh, i), key);
            memcpy(&string_hash_p2(sh, i), &value, sizeof(uchar *));
            return string_hash_p2(sh, i);
        }
    }
    return NULL;
}

void string_hash_remove(string_hash *sh, char *key)
{
    uint pos = (string_hash_from(key) % sh->max) * sh->unit, i;
    // match node in the first hit.
    if(strcmp(string_hash_p1(sh, pos), key) == 0) {
        string_hash_clear(sh, pos);
        return;
    }

    // try to hit next node if we miss the first.
    for(i = pos + sh->unit;; i += sh->unit) {
        if(i >= sh->unit * sh->max)
            i = 0;
        if(i == pos)
            break;
        if(strcmp(string_hash_p1(sh, i), key) == 0) {
            string_hash_clear(sh, i);
            return;
        }
    }
}


#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/signal.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <dlfcn.h>

#define TIMEOUT             3000

#define max(a, b)           ((a) > (b) ? (a) : (b))
#define min(a, b)           ((a) < (b) ? (a) : (b))

global_setting vohttpd_setting;
filter_init vohttpd_filter_init;

// store library(.so, .dll) name, handle, and function(no '.'), handle.
static string_hash* g_funcs;
static linear_hash* g_socks;

void socketdata_init(socket_data *d)
{
    if(d->body < d->head || d->body >= d->head + RECVBUF_SIZE)
        safe_free(d->body);
    memset(d->head, 0, RECVBUF_SIZE);

    d->recv = 0;
    d->size = 0;
    d->used = 0;

    d->set = &vohttpd_setting;
}

void socketdata_remove(linear_hash *g_socks, int sock)
{
    socket_data *d = (socket_data *)linear_hash_get(g_socks, sock);
    close(sock);
    // check if the body point to head, if so we do not release it.
    if(d->body < d->head || d->body >= d->head + RECVBUF_SIZE)
        safe_free(d->body);
    linear_hash_remove(g_socks, sock);
}

uint vohttpd_decode_content_size(socket_data *d)
{
    char *p;

    p = strstr(d->head, HTTP_CONTENT_LENGTH);
    if(p == NULL)
        return 0;
    p += sizeof(HTTP_CONTENT_LENGTH);

    while((*p < '0' || *p > '9') && *p != '\r' && *p != '\n')
        p++;

    return (uint)atoi(p);
}

uint vohttpd_decode_connection(socket_data *d)
{
    char *p, *end;
    uint ret;

    p = strstr(d->head, HTTP_CONNECTION);
    if(p == NULL)
        return 0;
    p += sizeof(HTTP_CONNECTION);

    end = strstr(p, "\r\n");
    if(end == NULL)
        return 0;

    *end = '\0';
    ret = strstr(p, "keep-alive") ? 1 : 0;
    *end = '\r';

    return ret;
}


uint vohttpd_file_size(const char *path)
{
    FILE *fp;
    uint size;

    fp = fopen(path, "rb");
    if(fp == NULL)
        return (uint)(-1);
    fseek(fp, 0, SEEK_END);
    size = (uint)ftell(fp);

    fclose(fp);
    return size;
}

const char *vohttpd_file_extend(const char *path)
{
    const char *p = path + strlen(path);
    while(path != p) {
        if(*p == '.')
            return p + 1;
        p--;
    }
    return NULL;
}

int vohttpd_http_file(socket_data *d, const char *path)
{
    char buf[SENDBUF_SIZE];
    const char* ext;
    int size, total = 0;
    FILE *fp;

    total = vohttpd_file_size(path);
    if(total == -1)
        return vohttpd_error_page(d, 404, NULL);

    ext = vohttpd_file_extend(path);
    size = vohttpd_reply_head(buf, 200);
    size += sprintf(buf + size, "%s: %d\r\n", HTTP_CONTENT_LENGTH, total);
    size += sprintf(buf + size, "%s: %s\r\n", HTTP_DATE_TIME, vohttpd_gmtime());
    size += sprintf(buf + size, "%s: %s\r\n", HTTP_CONTENT_TYPE, vohttpd_mime_map(ext));
    size += sprintf(buf + size, "%s: %s\r\n", HTTP_CONNECTION, "close");
    strcat(buf + size, "\r\n"); size += 2;

    size = send(d->sock, buf, size, 0);
    if(size <= 0)
        return -1;

    fp = fopen(path, "rb");
    if(fp == NULL)
        return vohttpd_error_page(d, 405, NULL);

    while(!feof(fp)) {
        size = fread(buf, 1, SENDBUF_SIZE, fp);
        if(size <= 0)
            break;
        size = send(d->sock, buf, size, 0);
        if(size <= 0)
            break;
        total += size;
    }
    fclose(fp);
    return total;
}

int vohttpd_default_index(socket_data *d, const char *path)
{
    char buf[SENDBUF_SIZE];
    int size;

    size = vohttpd_reply_head(buf, 200);
    size += sprintf(buf + size, "%s: %s\r\n", HTTP_DATE_TIME, vohttpd_gmtime());
    size += sprintf(buf + size, "%s: %s\r\n", HTTP_CONTENT_TYPE, vohttpd_mime_map("html"));
    size += sprintf(buf + size, "%s: %s\r\n", HTTP_CONNECTION, "close");
    strcat(buf + size, "\r\n"); size += 2;

    size = send(d->sock, buf, size, 0);
    if(size <= 0)
        return -1;

    size = sprintf(buf, "<html><head><title>Folder</title></head><body style=\""
        "font-family:%s\"><h1 style=\"color:#0040F0\">%s</h1><p style=\"font-s"
        "ize: 14px;\">Sorry, this feature is not supported yet... :'(</p></body"
        "></html>", HTTP_FONT, path);
    size = send(d->sock, buf, size, 0);
    if(size <= 0)
        return -1;
    return 0;
}

int vohttpd_default(socket_data *d, string_reference *fn)
{
    char head[MESSAGE_SIZE];
    char path[MESSAGE_SIZE];

    if(fn->size >= MESSAGE_SIZE)
        return vohttpd_error_page(d, 413, NULL);
    string_reference_dup(fn, head);
    if(strstr(head, ".."))
        return vohttpd_error_page(d, 403, NULL);

    if(head[fn->size - 1] == '/') {
        sprintf(path, "%s/%sindex.html", vohttpd_setting.base, head);
        if(vohttpd_file_size(path) != (uint)(-1)) {
            // there is index file in folder.
            return vohttpd_http_file(d, path);
        } else {
            sprintf(path, "%s%s", vohttpd_setting.base, head);
            // no such index file, we try to load default, always show folder.
            return vohttpd_default_index(d, path);
        }
    }

    // the address might be a file.
    sprintf(path, "%s%s", vohttpd_setting.base, head);
    return vohttpd_http_file(d, path);
}

int vohttpd_function(socket_data *d, string_reference *fn, string_reference *pa)
{
    char name[FUNCTION_SIZE];
    plugin_func func;

    if(fn->size >= FUNCTION_SIZE)
        return vohttpd_error_page(d, 413, NULL);
    string_reference_dup(fn, name);
    if(strchr(name, '.') != NULL)   // this is library handle.
        return vohttpd_error_page(d, 403, NULL);

    func = (plugin_func)string_hash_get(g_funcs, name);
    if(func == NULL)
        return vohttpd_error_page(d, 404, NULL);
    return func(d, pa);
}

// return:
//  0: get header do not have special request.
//  1: get header contains function.
//  2: get header contains function and parameters.
//  < 0: not a valid header.
int vohttpd_decode_get(socket_data *d, string_reference *fn, string_reference *pa)
{
    char *p, *e, *f1, *f2;
    int ret;

    p = d->head + sizeof(HTTP_GET);
    e = strstr(p, "\r\n");
    if(e == NULL)
        return -1;

    while(*p == ' ' && e != p)
        p++;
    if(e == p)
        return -1;
    while(*e != ' ' && e != p)
        e--;
    if(e == p)
        return -1;

    *e = '\0';

    f1 = strstr(p, HTTP_CGI_BIN);
    if(f1 == NULL) {
        ret = 0;
        fn->ref = p;
        fn->size = e - p;
    } else {
        f1 += sizeof(HTTP_CGI_BIN) - 1;
        f2 = strchr(f1, '?');
        if(f2 == NULL) {
            fn->ref = f1;
            fn->size = e - f1;
            ret = 1;
        } else {
            fn->ref = f1;
            fn->size = f2 - f1;
            ret = 2;
            pa->ref = ++f2;
            pa->size = e - f2;
        }
    }

    *e = ' ';
    return ret;
}

int vohttpd_decode_post(socket_data *d, string_reference *fn, string_reference *pa)
{
    char *p, *e, *f1;
    int ret;

    p = d->head + sizeof(HTTP_POST);
    e = strstr(p, "\r\n");
    if(e == NULL)
        return -1;

    while(*p == ' ' && e != p)
        p++;
    if(e == p)
        return -1;
    while(*e != ' ' && e != p)
        e--;
    if(e == p)
        return -1;

    *e = '\0';
    f1 = strstr(p, HTTP_CGI_BIN);
    *e = ' ';

    ret = 0;
    if(f1 != NULL) {
        f1 += sizeof(HTTP_CGI_BIN) - 1;

        fn->ref = f1;
        fn->size = e - f1;

        pa->ref = d->body;
        pa->size = d->recv;

        ret = 2;
    }

    return ret;
}

// return:
//  0: "Connection: close", close and remove socket.
//  1: "Connection: keep-alive", wait for next request.
int vohttpd_filter(socket_data *d)
{
    string_reference fn, pa;

    if(vohttpd_filter_init && vohttpd_filter_init(d))
        return 0;

    if(memcmp(d->head, "GET", 3) == 0) {
        switch(vohttpd_decode_get(d, &fn, &pa)) {
        case 0: {
            vohttpd_default(d, &fn);
            break; }
        case 1: {
            pa.ref = ""; pa.size = 0;
            vohttpd_function(d, &fn, &pa);
            break; }
        case 2: {
            vohttpd_function(d, &fn, &pa);
            break; }
        default:
            vohttpd_error_page(d, 404, NULL);
            break;
        }
    } else if(memcmp(d->head, "POST", 4) == 0) {
        switch(vohttpd_decode_post(d, &fn, &pa)) {
        case 2:
            vohttpd_function(d, &fn, &pa);
            break;
        default:
            vohttpd_error_page(d, 404, NULL);
            break;
        }
    } else {
        vohttpd_error_page(d, 501, NULL);
    }

    return 0;
}

int vohttpd_unload_library(socket_data *d, string_reference *pa)
{
    char path[MESSAGE_SIZE], name[FUNCTION_SIZE], buf[SENDBUF_SIZE];
    void *h = NULL;
    int size, total = 0;

    plugin_query query;
    plugin_cleanup clean;
    plugin_func func;

    if(pa->size >= FUNCTION_SIZE)
        return vohttpd_error_page(d, 413, "Library name is too long.");
    string_reference_dup(pa, name);
    h = (void *)string_hash_get(g_funcs, name);
    if(h == NULL)
        return vohttpd_error_page(d, 404, "Library has not been loaded.");
    sprintf(path, "%s" HTTP_CGI_BIN "%s", vohttpd_setting.base, name);

    query = dlsym(h, LIBRARY_QUERY);
    if(query == NULL)
        return vohttpd_error_page(d, 404, "Can not find query interface.");
    size = vohttpd_reply_head(buf, 200);
    size += sprintf(buf + size, "%s: %s\r\n", HTTP_DATE_TIME, vohttpd_gmtime());
    size += sprintf(buf + size, "%s: %s\r\n", HTTP_CONTENT_TYPE, vohttpd_mime_map("html"));
    size += sprintf(buf + size, "%s: %s\r\n", HTTP_CONNECTION, "close");
    strcat(buf + size, "\r\n"); size += 2;
    size = send(d->sock, buf, size, 0);

    size = sprintf(buf, "<html><head><title>Unload Library</title></head><body "
        "style=\"font-family:%s\"><h1>%s</h1>", HTTP_FONT, name);
    send(d->sock, buf, size, 0);        // just keep loading, do not check send.

    string_hash_remove(g_funcs, name);
    query(NULL, NULL);     // init library query.
    while(func = (plugin_func)query(name, NULL), func) {
        const char *status = "INVALID";
        if(func = (plugin_func)dlsym(h, name), func == NULL) {
            status = "<b style=\"color:#F04000\">NOT FOUND</b>";
        } else {
            if(func != (plugin_func)string_hash_get(g_funcs, name)) {
                status = "<b style=\"color:#CC0000\">NOT EXIST</b>";
            } else {
                string_hash_remove(g_funcs, name);
                status = "<b style=\"color:#CC9900\">REMOVED</b>";
                total++;
            }
        }

        size = sprintf(buf, "<p>%s&nbsp&nbsp<b>%s</b></p>", status, name);
        send(d->sock, buf, size, 0);
    }

    clean = dlsym(h, LIBRARY_CLEANUP);
    if(clean)
        clean();

    size = sprintf(buf, "<p><b style=\"color:#339900\">The library has removed."
        "</b></p></body></html>");
    send(d->sock, buf, size, 0);

    dlclose(h);
    return printf("[%s] UNLOAD SUCCESS\n%s\n", vohttpd_gmtime(), path);
}

int vohttpd_load_library(socket_data *d, string_reference *pa)
{
    char path[MESSAGE_SIZE], name[FUNCTION_SIZE], buf[SENDBUF_SIZE];
    const char *note;
    void *h = NULL;
    int size, total = 0;

    plugin_query query;
    plugin_func func;

    if(pa->size >= FUNCTION_SIZE)
        return vohttpd_error_page(d, 413, "Library name is too long.");
    string_reference_dup(pa, name);
    if(strchr(name, '.') == NULL)
        return vohttpd_error_page(d, 403, "Library name is not correct.");
    if(string_hash_get(g_funcs, name))
        return vohttpd_error_page(d, 403, "Library has already loaded.");
    sprintf(path, "%s" HTTP_CGI_BIN "%s", vohttpd_setting.base, name);

    h = dlopen(path, RTLD_NOW);
    if(h == NULL)
        return vohttpd_error_page(d, 404, dlerror());
    if(string_hash_set(g_funcs, name, (uchar *)h) == NULL) {
        dlclose(h);
        return vohttpd_error_page(d, 413, "Sorry, hash is full.");
    }


    query = dlsym(h, LIBRARY_QUERY);
    if(query == NULL) {
        vohttpd_unload_library(d, pa);
        return vohttpd_error_page(d, 404, "Can not find the library.");
    }

    size = vohttpd_reply_head(buf, 200);
    size += sprintf(buf + size, "%s: %s\r\n", HTTP_DATE_TIME, vohttpd_gmtime());
    size += sprintf(buf + size, "%s: %s\r\n", HTTP_CONTENT_TYPE, vohttpd_mime_map("html"));
    size += sprintf(buf + size, "%s: %s\r\n", HTTP_CONNECTION, "close");
    strcat(buf + size, "\r\n"); size += 2;
    size = send(d->sock, buf, size, 0);

    size = sprintf(buf, "<html><head><title>Load Library</title></head><body "
        "style=\"font-family:%s\"><h1>%s</h1>", HTTP_FONT, name);
    send(d->sock, buf, size, 0);        // just keep loading, do not check send.

    // call library query function, query libraries.
    query(NULL, &note);     // init library query.
    if(note != NULL) {
        size = sprintf(buf, "<p><i style=\"font-size:14px;color:#999999\">%s"
            "</i></p>", note);
        send(d->sock, buf, size, 0);
    }
    while(func = (plugin_func)query(name, &note), func) {
        const char *status = "INVALID";
        if(func = (plugin_func)dlsym(h, name), func == NULL) {
            status = "<b style=\"color:#F04000\">NOT FOUND</b>";
        } else if(string_hash_get(g_funcs, name)) {
            status = "<b style=\"color:#CC0000\">DUPLICATE</b>";
        } else if(!string_hash_set(g_funcs, name, (uchar *)func)) {
            status = "<b style=\"color:#00CC99\">HASH FULL</b>";
        } else {
            status = "<b style=\"color:#CC9900\">IMPLANTED</b>";
            total++;
        }

        size = sprintf(buf, "<p>%s&nbsp&nbsp<b>%s</b>&nbsp&nbsp&nbsp&nbsp&nbsp<"
            "i style=\"font-size:14px;color:#999999\">%s</i></p>", status, name, note);
        send(d->sock, buf, size, 0);
    }
    size = sprintf(buf, "<p><b style=\"color:#339900\">The library has loaded."
        "</b></p></body></html>");
    send(d->sock, buf, size, 0);
    return printf("[%s] LOAD SUCCESS\n%s\n", vohttpd_gmtime(), path);
}

int vohttpd_query_library(socket_data *d, string_reference *pa)
{
    char name[FUNCTION_SIZE], buf[MESSAGE_SIZE];
    uint i;
    int size, tlib = 0, tfunc = 0;

    size = vohttpd_reply_head(buf, 200);
    size += sprintf(buf + size, "%s: %s\r\n", HTTP_DATE_TIME, vohttpd_gmtime());
    size += sprintf(buf + size, "%s: %s\r\n", HTTP_CONTENT_TYPE, vohttpd_mime_map("html"));
    size += sprintf(buf + size, "%s: %s\r\n", HTTP_CONNECTION, "close");
    strcat(buf + size, "\r\n"); size += 2;
    size = send(d->sock, buf, size, 0);

    size = sprintf(buf, "<html><head><title>Query Library</title></head><body "
        "style=\"font-family:%s\">", HTTP_FONT);
    send(d->sock, buf, size, 0);        // just keep loading, do not check send.

    for(i = 0; i < g_funcs->max; i++) {
        uint pos = i * g_funcs->unit;
        if(string_hash_empty(g_funcs, pos))
            continue;
        strcpy(name, string_hash_p1(g_funcs, pos));
        if(strchr(name, '.')) {
            plugin_func func;
            plugin_query query;
            const char *note;
            void *h = (void *)string_hash_p2(g_funcs, pos);

            if(pa->size && memcmp(pa->ref, name, pa->size))
                continue;
            size = sprintf(buf, "<h1>%s</h1>", name);
            send(d->sock, buf, size, 0);
            tlib++;

            query = dlsym(h, LIBRARY_QUERY);
            query(NULL, &note);
            if(note != NULL) {
                size = sprintf(buf, "<p><i style=\"font-size:14px;color:#999999\">%s</i></p>", note);
                send(d->sock, buf, size, 0);
            }
            while(func = (plugin_func)query(name, &note), func) {
                const char *status = "INVALID";
                if(func = (plugin_func)dlsym(h, name), func == NULL) {
                    status = "<b style=\"color:#F04000\">NOT FOUND</b>";
                } else {
                    if(func != (plugin_func)string_hash_get(g_funcs, name)) {
                        status = "<b style=\"color:#CC0000\">NOT EXIST</b>";
                    } else {
                        status = "<b style=\"color:#CC9900\">IMPLANTED</b>";
                        tfunc++;
                    }
                }
                size = sprintf(buf, "<p>%s&nbsp&nbsp<b>%s</b>&nbsp&nbsp&nbsp&nb"
                    "sp&nbsp<i style=\"font-size:14px;color:#999999\">%s</i></p>", status, name, note);
                send(d->sock, buf, size, 0);
            }

            size = sprintf(buf, "<br><br>");
            send(d->sock, buf, size, 0);
        }
    }

    size = sprintf(buf, "<p><b>Total Library: %d</b></p><p><b>Total Implanted: "
        "%d</b></p></body></html>", tlib, tfunc);
    send(d->sock, buf, size, 0);
    return 0;
}

int main(int argc, char *argv[])
{
    int socksrv, sockmax, sock, b = 1;
    uint i, count, size;
    char *p;

    struct sockaddr_in addr;
    socklen_t len;
    struct timeval tmv;
    fd_set fdr;
    socket_data *d;

    switch(argc) {
    case 1:
        vohttpd_setting.port = 8080;
        vohttpd_setting.base = "/var/www/html";
        break;

    case 2:
        vohttpd_setting.port = atoi(argv[1]);
        vohttpd_setting.base = "/var/www/html";
        break;

    case 3:
        vohttpd_setting.port = atoi(argv[1]);
        vohttpd_setting.base = argv[2];
        break;

    default:
        printf("usage: vohttpd [port] [home]\n");
        return -1;
    }

    // alloc buffer for globle pointer(maybe make them to static is better?)
    g_funcs = string_hash_alloc(FUNCTION_SIZE, FUNCTION_COUNT);
    g_socks = linear_hash_alloc(sizeof(socket_data), BUFFER_COUNT);

    // register basic functions here.
    string_hash_set(g_funcs, "load_library", (uchar *)vohttpd_load_library);
    string_hash_set(g_funcs, "unload_library", (uchar *)vohttpd_unload_library);
    string_hash_set(g_funcs, "query_library", (uchar *)vohttpd_query_library);

    // ignore the signal, or it will stop our server once client disconnected.
    signal(SIGPIPE, SIG_IGN);

    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(vohttpd_setting.port);
    len = sizeof(struct sockaddr_in);

    socksrv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    setsockopt(socksrv, SOL_SOCKET, SO_REUSEADDR, (const char *)&b, sizeof(int));
    if(bind(socksrv, (struct sockaddr*)&addr, sizeof(struct sockaddr)) < 0) {
        printf("can not bind to address, %d:%s.\n", errno, strerror(errno));
        return -1;
    }

    // we can not handle much request at same time, so limit listen backlog.
    if(listen(socksrv, min(SOMAXCONN, BUFFER_COUNT / 2)) < 0) {
        printf("can not listen to port, %d:%s.\n", errno, strerror(errno));
        return -1;
    }

    // for simple embed server, my choice is select for better compatible.
    // but for heavy load situation in linux, better to change this to epoll.
    while(1) {
        FD_ZERO(&fdr);
        FD_SET(socksrv, &fdr);

        // queue the hash and pickout the max socket.
        sockmax = socksrv;
        for(i = 0; i < g_socks->max; i++) {
            d = (socket_data *)linear_hash_value(g_socks, i);
            sockmax = max(d->sock, sockmax);
            if(d->sock != (int)LINEAR_HASH_NULL)
                FD_SET(d->sock, &fdr);
        }

        tmv.tv_sec = TIMEOUT / 1000;
        tmv.tv_usec = TIMEOUT % 1000 * 1000;

        count = select(sockmax + 1, &fdr, NULL, NULL, &tmv);
        if(count <= 0) {
            // clean up all sockets, they are time out.
            for(i = 0; i < g_socks->max; i++) {
                d = (socket_data *)linear_hash_value(g_socks, i);
                if(d->sock == (int)LINEAR_HASH_NULL)
                    continue;
                linear_hash_remove(g_socks, (uint)d->sock);
                close(d->sock);
            }
            continue;
        }

        if(FD_ISSET(socksrv, &fdr)) {
            count--;
            memset(&addr, 0, sizeof(struct sockaddr_in));

            sock = accept(socksrv, (struct sockaddr*)&addr, &len);
            d = (socket_data *)linear_hash_set(g_socks, (uint)sock);
            if(d == NULL) {
                close(sock);     // no free buffer, maybe return error page here?
                continue;
            }
            memset(d, 0, sizeof(socket_data));
            d->sock = sock;
        }

        for(i = 0; i < g_socks->max; i++) {
            if(count <= 0)
                break;

            d = (socket_data *)linear_hash_value(g_socks, i);
            if((uint)d->sock == LINEAR_HASH_NULL)
                continue;

            if(FD_ISSET(d->sock, &fdr)) {
                count--;

                if(d->size == 0) {

                    // receive http head data.
                    size = recv(d->sock, d->head + d->used, RECVBUF_SIZE - d->used, 0);
                    if(size <= 0) {
                        socketdata_remove(g_socks, d->sock);
                        continue;
                    }
                    d->used += size;

                    // OPTIMIZE: we do not have to check from beginning every time.
                    // if new recv size > 4, we can check new recv.
                    p = strstr(d->head, HTTP_HEADER_END);
                    if(p == NULL) {
                        if(d->used >= RECVBUF_SIZE) {
                            vohttpd_error_page(d, 413, NULL);
                            socketdata_remove(g_socks, d->sock);
                        }
                        // not get the header end, so we wait next recv.
                        continue;
                    }

                    p += sizeof(HTTP_HEADER_END) - 1;

                    // now check the content size.
                    d->recv = p - d->head;
                    d->size = vohttpd_decode_content_size(d);
                    if(d->size == 0) {// no content.
                        if(!vohttpd_filter(d))
                            socketdata_remove(g_socks, d->sock);
                        // no body in this http request, it should be GET.
                        continue;
                    }
                    // the head buffer can not contain the body data
                    // we have to alloc memory for it.
                    if(d->size > RECVBUF_SIZE - d->used + d->recv) {
                        d->body = malloc(d->size);
                        memset(d->body, 0, d->size);
                        memcpy(d->body, d->head, d->recv);

                        // now we should goto body data receive process.
                    }

                } else {

                    // receive http body data.
                    size = recv(d->sock, d->body + d->recv, d->size - d->recv, 0);
                    if(size <= 0) {
                        socketdata_remove(g_socks, d->sock);
                        continue;
                    }
                    d->recv += size;
                    if(d->recv >= d->size) {
                        if(!vohttpd_filter(d))
                            socketdata_remove(g_socks, d->sock);
                        continue;
                    }
                }
            }
        }

        // do some clean up for next loop.
    }

    close(socksrv);
    safe_free(g_funcs);
    safe_free(g_socks);
    return 0;
}

