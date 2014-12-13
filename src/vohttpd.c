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
#define string_hash_p1(h, p)     ((char *)((h->data + p) + sizeof(uchar *)))
#define string_hash_p2(h, p)     (*((uchar **)(h->data + p)))
#define string_hash_empty(h, p)  (*string_hash_p1((h), (p)) == '\0')
#define string_hash_clear(h, p)  {*string_hash_p1((h), (p)) = '\0';}

string_hash* string_hash_alloc(uint unit, uint max)
{
    string_hash *sh = (string_hash *)
        malloc(max * (unit + sizeof(uchar *)) + sizeof(string_hash));
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

int get_name_from_path(const char *path, char *name, size_t size)
{
    char *p1, *p2, *p;

    p1 = strrchr(path, '/');
    p2 = strrchr(path, '\\');
    if(!p1 || !p2) {
        strncpy(name, path, size);
        return strlen(path);
    }

    p = max(p1, p2) + 1;
    strncpy(name, p, size);
    return strlen(p);
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

#define HTTP_HEADER_END     "\r\n\r\n"
#define HTTP_CONTENT_LENGTH "Content-Length"
#define HTTP_CONTENT_TYPE   "Content-Type"
#define HTTP_DATE_TIME      "Date"
#define HTTP_CONNECTION     "Connection"
#define HTTP_GET            "GET"
#define HTTP_POST           "POST"
#define HTTP_FONT           "Helvetica,Arial,sans-serif"

#define TIMEOUT             3000

static vohttpd g_set;

// store library(.so, .dll) name, handle, and function(no '.'), handle.
void socketdata_init(socket_data *d)
{
    if(d->body < d->head || d->body >= d->head + RECVBUF_SIZE)
        safe_free(d->body);
    memset(d->head, 0, RECVBUF_SIZE);

    d->recv = 0;
    d->size = 0;
    d->used = 0;

    d->set = &g_set;
}

void socketdata_remove(linear_hash *socks, int sock)
{
    socket_data *d = (socket_data *)linear_hash_get(socks, sock);
    close(sock);
    // check if the body point to head, if so we do not release it.
    if(d->body < d->head || d->body >= d->head + RECVBUF_SIZE)
        safe_free(d->body);
    linear_hash_remove(d->set->socks, sock);
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
        return d->set->error_page(d, 404, NULL);

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
        return d->set->error_page(d, 405, NULL);

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
        return d->set->error_page(d, 413, NULL);
    string_reference_dup(fn, head);
    if(strstr(head, ".."))
        return d->set->error_page(d, 403, NULL);

    if(head[fn->size - 1] == '/') {
        sprintf(path, "%s/%sindex.html", g_set.base, head);
        if(vohttpd_file_size(path) != (uint)(-1)) {
            // there is index file in folder.
            return vohttpd_http_file(d, path);
        } else {
            sprintf(path, "%s%s", g_set.base, head);
            // no such index file, we try to load default, always show folder.
            return vohttpd_default_index(d, path);
        }
    }

    // the address might be a file.
    sprintf(path, "%s%s", g_set.base, head);
    return vohttpd_http_file(d, path);
}

int vohttpd_function(socket_data *d, string_reference *fn, string_reference *pa)
{
    char name[FUNCTION_SIZE];
    _plugin_func func;

    if(fn->size >= FUNCTION_SIZE)
        return d->set->error_page(d, 413, NULL);
    string_reference_dup(fn, name);
    if(strchr(name, '.') != NULL)   // this is library handle.
        return d->set->error_page(d, 403, NULL);

    func = (_plugin_func)string_hash_get(d->set->funcs, name);
    if(func == NULL)
        return d->set->error_page(d, 404, NULL);
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

    if(d->set->filter_init && ((_filter_init)d->set->filter_init)(d))
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
            d->set->error_page(d, 404, NULL);
            break;
        }
    } else if(memcmp(d->head, "POST", 4) == 0) {
        switch(vohttpd_decode_post(d, &fn, &pa)) {
        case 2:
            vohttpd_function(d, &fn, &pa);
            break;
        default:
            d->set->error_page(d, 404, NULL);
            break;
        }
    } else {
        d->set->error_page(d, 501, NULL);
    }

    return 0;
}

int vohttpd_error_page(socket_data *d, int code, const char *err)
{
    char head[MESSAGE_SIZE], body[MESSAGE_SIZE];
    const char *msg;
    int  size, total = 0;

    if(err == NULL)
        err = "Sorry, I have tried my best... :'(";

    msg = vohttpd_code_message(code);
    total = sprintf(body,
        "<html><head><title>%s</title></head><body style=\"font-family:%s;\">"
        "<h1 style=\"color:#0040F0\">%d %s</h1><p style=\"font-size:14px;\">%s"
        "</p></body></html>", msg, HTTP_FONT, code, msg, err);

    size = vohttpd_reply_head(head, code);
    size += sprintf(head + size, "%s: %s\r\n", HTTP_CONTENT_TYPE, vohttpd_mime_map("html"));
    size += sprintf(head + size, "%s: %s\r\n", HTTP_DATE_TIME, vohttpd_gmtime());
    size += sprintf(head + size, "%s: %d\r\n", HTTP_CONTENT_LENGTH, total);
    strcat(head, "\r\n"); size += 2;

    size = send(d->sock, head, size, 0);
    if(size <= 0)
        return -1;
    total = send(d->sock, body, total, 0);
    if(total <= 0)
        return -1;
    return total + size;
}

const char* vohttpd_unload_plugin(socket_data *d, const char *path)
{
    char name[FUNCTION_SIZE];
    void *h = NULL;

    _plugin_query query;
    _plugin_func func;
    _plugin_cleanup clean;

    if(get_name_from_path(path, name, FUNCTION_SIZE) >= FUNCTION_SIZE)
        return "library name is too long.";
    h = (void *)string_hash_get(d->set->funcs, name);
    if(h == NULL)
        return "library has not been loaded.";

    query = dlsym(h, LIBRARY_QUERY);
    if(query == NULL) {
        dlclose(h);
        return "can not find query interface.";
    }
    string_hash_remove(d->set->funcs, name);

    query(NULL, NULL);
    while(func = (_plugin_func)query(name, NULL), func) {
        func = (_plugin_func)dlsym(h, name);
        if(func == NULL)
            continue;
        if(func != (_plugin_func)string_hash_get(d->set->funcs, name))
            continue;
        string_hash_remove(d->set->funcs, name);
    }

    clean = dlsym(h, LIBRARY_CLEANUP);
    if(clean)
        clean();

    dlclose(h);
    return NULL;
}

const char* vohttpd_load_plugin(socket_data *d, const char *path)
{
    char name[FUNCTION_SIZE];
    void *h = NULL;

    _plugin_query query;
    _plugin_func func;

    if(get_name_from_path(path, name, FUNCTION_SIZE) >= FUNCTION_SIZE)
        return "library name is too long.";
    if(strchr(path, '.') == NULL)
        return "library name is not correct.";
    if(string_hash_get(d->set->funcs, name))
        return "library has already loaded.";

    h = dlopen(path, RTLD_NOW);
    if(h == NULL)
        return "can not find file.";
    if(string_hash_set(d->set->funcs, name, (uchar *)h) == NULL) {
        dlclose(h);
        return "hash is full.";
    }

    query = dlsym(h, LIBRARY_QUERY);
    if(query == NULL) {
        vohttpd_unload_plugin(d, name);
        dlclose(h);
        return "can not find the library.";
    }

    query(NULL, NULL);
    while(func = (_plugin_func)query(name, NULL), func) {
        if(string_hash_get(d->set->funcs, name))
            continue;
        func = (_plugin_func)dlsym(h, name);
        if(func == NULL)
            continue;
        if(!string_hash_set(d->set->funcs, name, (uchar *)func))
            continue;
    }

    dlclose(h);
    return NULL;
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
        g_set.port = 8080;
        g_set.base = "/var/www/html";
        break;

    case 2:
        g_set.port = atoi(argv[1]);
        g_set.base = "/var/www/html";
        break;

    case 3:
        g_set.port = atoi(argv[1]);
        g_set.base = argv[2];
        break;

    default:
        printf("usage: vohttpd [port] [home]\n");
        return -1;
    }

    // alloc buffer for globle pointer(maybe make them to static is better?)
    g_set.funcs = string_hash_alloc(FUNCTION_SIZE, FUNCTION_COUNT);
    g_set.socks = linear_hash_alloc(sizeof(socket_data), BUFFER_COUNT);

    // set default function callback.
    g_set.error_page = vohttpd_error_page;
    g_set.load_plugin = vohttpd_load_plugin;
    g_set.unload_plugin = vohttpd_unload_plugin;

    // ignore the signal, or it will stop our server once client disconnected.
    signal(SIGPIPE, SIG_IGN);

    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(g_set.port);
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
        for(i = 0; i < g_set.socks->max; i++) {
            d = (socket_data *)linear_hash_value(g_set.socks, i);
            sockmax = max(d->sock, sockmax);
            if(d->sock != (int)LINEAR_HASH_NULL)
                FD_SET(d->sock, &fdr);
        }

        tmv.tv_sec = TIMEOUT / 1000;
        tmv.tv_usec = TIMEOUT % 1000 * 1000;

        count = select(sockmax + 1, &fdr, NULL, NULL, &tmv);
        if(count <= 0) {
            // clean up all sockets, they are time out.
            for(i = 0; i < g_set.socks->max; i++) {
                d = (socket_data *)linear_hash_value(g_set.socks, i);
                if(d->sock == (int)LINEAR_HASH_NULL)
                    continue;
                linear_hash_remove(g_set.socks, (uint)d->sock);
                close(d->sock);
            }
            continue;
        }

        if(FD_ISSET(socksrv, &fdr)) {
            count--;
            memset(&addr, 0, sizeof(struct sockaddr_in));

            sock = accept(socksrv, (struct sockaddr*)&addr, &len);
            d = (socket_data *)linear_hash_set(g_set.socks, (uint)sock);
            if(d == NULL) {
                close(sock);     // no free buffer, maybe return error page here?
                continue;
            }
            memset(d, 0, sizeof(socket_data));
            d->sock = sock;
        }

        for(i = 0; i < g_set.socks->max; i++) {
            if(count <= 0)
                break;

            d = (socket_data *)linear_hash_value(g_set.socks, i);
            if((uint)d->sock == LINEAR_HASH_NULL)
                continue;

            if(FD_ISSET(d->sock, &fdr)) {
                count--;

                if(d->size == 0) {

                    // receive http head data.
                    size = recv(d->sock, d->head + d->used, RECVBUF_SIZE - d->used, 0);
                    if(size <= 0) {
                        socketdata_remove(g_set.socks, d->sock);
                        continue;
                    }
                    d->used += size;

                    // OPTIMIZE: we do not have to check from beginning every time.
                    // if new recv size > 4, we can check new recv.
                    p = strstr(d->head, HTTP_HEADER_END);
                    if(p == NULL) {
                        if(d->used >= RECVBUF_SIZE) {
                            g_set.error_page(d, 413, NULL);
                            socketdata_remove(g_set.socks, d->sock);
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
                            socketdata_remove(g_set.socks, d->sock);
                        // no body in this http request, it should be GET.
                        continue;
                    }

                    // the head buffer can not contain the body data
                    // we have to alloc memory for it.
                    //
                    // TODO: change this part to file mapping might be better.
                    // in most situation, embed device do not have to transfer
                    // much data unless receiving a file, so file mapping will
                    // save a lot of memory and save cost on store file.
                    // one thread process make this simple, map file name can be
                    // temp.[sock], once the socket is closed, delete that temp
                    // file. In plugin, we can move that file to another folder
                    // when we get full of it to save upload file.
                    // add a function: vohttpd_temp_filename(socket_data)
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
                        socketdata_remove(g_set.socks, d->sock);
                        continue;
                    }
                    d->recv += size;
                    if(d->recv >= d->size) {
                        if(!vohttpd_filter(d))
                            socketdata_remove(g_set.socks, d->sock);
                        continue;
                    }
                }
            }
        }

        // do some clean up for next loop.
    }

    close(socksrv);
    safe_free(g_set.funcs);
    safe_free(g_set.socks);
    return 0;
}

