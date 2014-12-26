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
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <string.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

#include "vohttpd.h"

#define HTTP_HEADER_END     "\r\n\r\n"
#define HTTP_GET            "GET"
#define HTTP_POST           "POST"
#define HTTP_FONT           "Helvetica,Arial,sans-serif"

#define TIMEOUT             3000

static vohttpd g_set;

/* input, file path: /var/www/html/index.html
 * output, file name: index.html
 * return, the length of the file name.
 */
int get_name_from_path(const char *path, char *name, size_t size)
{
    char *p1, *p2, *p;

    p1 = strrchr(path, '/');
    p2 = strrchr(path, '\\');
    if(p1 == NULL && p2 == NULL) {
        strncpy(name, path, size);
        return strlen(name);
    }

    p = max(p1, p2) + 1;
    strncpy(name, p, size);
    return strlen(name);
}

/* alloc a new buffer for http request.
 * we use its socket as http request key to make it simple.
 * socket data contains full information of a request.
 */
socket_data* socketdata_new(linear_hash *socks, int sock)
{
    socket_data *d;

    d = (socket_data *)linear_hash_set(socks, (uint)sock);
    if(d == NULL)
        return NULL;      // buffer has full...show error page?

    memset(d, 0, sizeof(socket_data));
    d->sock = sock;
    d->set = &g_set;
    return d;
}

void socketdata_delete(linear_hash *socks, int sock)
{
    socket_data *d;

    d = (socket_data *)linear_hash_get(socks, sock);
    if(d == NULL)
        return;

    close(sock);
    // delete the map file in tmp folder(created when post data > BUFFER_SIZE)
    if(d->type == SOCKET_DATA_MMAP && d->body) {
        char map[MESSAGE_SIZE];
        munmap(d->body, d->size);
        snprintf(map, MESSAGE_SIZE, "%s" HTTP_CGI_BIN MMAP_FILE_NAME, d->set->base, d->sock);
        remove(map);       // the map file might not exists.
    }

    linear_hash_remove(socks, sock);
}

uint vohttpd_decode_content_size(socket_data *d)
{
    char *p;

    p = strstr(d->head, HTTP_CONTENT_LENGTH);
    if(p == NULL)
        return 0;
    p += sizeof(HTTP_CONTENT_LENGTH) - 1;

    while((*p < '0' || *p > '9') && *p != '\r' && *p != '\n')
        p++;

    return (uint)atoi(p);
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
    const char *p = strrchr(path, '.');
    return p == NULL ? p : (p + 1);
}

int vohttpd_is_folder(const char *path)
{
    struct stat s;
    if(lstat(path, &s) < 0)
        return 0;
    return S_ISDIR(s.st_mode);
}

int vohttpd_http_file(socket_data *d, const char *param)
{
    char buf[SENDBUF_SIZE], *p;
    char path[MESSAGE_SIZE];
    const char* ext;
    int size, total = 0;
    FILE *fp;

    // file name might contains parameter, we should cut it.
    if(p = strchr(param, '?'), p != NULL)
        strncpy(path, param, p - param);
    else
        strncpy(path, param, MESSAGE_SIZE);

    total = vohttpd_file_size(path);
    if(total == -1)
        return d->set->error_page(d, 404, NULL);

    ext = vohttpd_file_extend(path);
    size = vohttpd_reply_head(buf, 200);
    size += snprintf(buf + size, SENDBUF_SIZE - size, "%s: %d\r\n", HTTP_CONTENT_LENGTH, total);
    size += snprintf(buf + size, SENDBUF_SIZE - size, "%s: %s\r\n", HTTP_DATE_TIME, vohttpd_gmtime());
    size += snprintf(buf + size, SENDBUF_SIZE - size, "%s: %s\r\n", HTTP_CONTENT_TYPE, vohttpd_mime_map(ext));
    size += snprintf(buf + size, SENDBUF_SIZE - size, "%s: %s\r\n", HTTP_CONNECTION, "close");
    strcat(buf + size, "\r\n"); size += 2;

    size = d->set->send(d->sock, buf, size, 0);
    if(size <= 0)
        return -1;

    fp = fopen(path, "rb");
    if(fp == NULL)
        return d->set->error_page(d, 405, NULL);

    while(!feof(fp)) {
        size = fread(buf, 1, SENDBUF_SIZE, fp);
        if(size <= 0)
            break;
        size = d->set->send(d->sock, buf, size, 0);
        if(size <= 0)
            break;
        total += size;
    }
    fclose(fp);
    return total;
}

int vohttpd_http_folder(socket_data *d, const char *path)
{
    char buf[SENDBUF_SIZE];
    int size, total = 0;
    DIR *dir;
    struct dirent *dp;

    dir = opendir(path);
    if(dir == NULL)
        return d->set->error_page(d, 404, "can not open the folder.");

    size = vohttpd_reply_head(buf, 200);
    size += snprintf(buf + size, SENDBUF_SIZE - size, "%s: %s\r\n", HTTP_DATE_TIME, vohttpd_gmtime());
    size += snprintf(buf + size, SENDBUF_SIZE - size, "%s: %s\r\n", HTTP_CONTENT_TYPE, vohttpd_mime_map("html"));
    size += snprintf(buf + size, SENDBUF_SIZE - size, "%s: %s\r\n", HTTP_CONNECTION, "close");
    strcat(buf + size, "\r\n"); size += 2;
    size = d->set->send(d->sock, buf, size, 0);

    size = snprintf(buf, SENDBUF_SIZE, "<html><head><title>%s</title></head>"
        "<body style=\"font-family:" HTTP_FONT "\"><h2>%s</h2><hr>", path, path);
    total += d->set->send(d->sock, buf, size, 0);

    while(dp = readdir(dir), dp) {
        if(strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0)
            continue;
        if(dp->d_type == DT_DIR)
            size = snprintf(buf, SENDBUF_SIZE, "<p id=\"dir\"><b>[%s]</b><p>", dp->d_name);
        else
            size = snprintf(buf, SENDBUF_SIZE, "<p id=\"file\">%s</p>", dp->d_name);
        total += d->set->send(d->sock, buf, size, 0);
    }
    closedir(dir);

    size = snprintf(buf, SENDBUF_SIZE, "</body></html>");
    total += d->set->send(d->sock, buf, size, 0);
    return total;
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
        snprintf(path, MESSAGE_SIZE, "%s/%sindex.html", g_set.base, head);
        if(vohttpd_file_size(path) != (uint)(-1))
            return d->set->http_file(d, path);
        // no index.html, we show it as folder.
    }

    // the address might be a file.
    snprintf(path, MESSAGE_SIZE, "%s%s", g_set.base, head);
    if(vohttpd_is_folder(path))
        return d->set->http_folder(d, path);
    else
        return d->set->http_file(d, path);
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
int vohttpd_data_filter(socket_data *d)
{
    string_reference fn, pa;
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
    total = snprintf(body, MESSAGE_SIZE,
        "<html><head><title>%s</title></head><body style=\"font-family:%s;\">"
        "<h1 style=\"color:#0040F0\">%d %s</h1><p style=\"font-size:14px;\">%s"
        "</p></body></html>", msg, HTTP_FONT, code, msg, err);

    size = vohttpd_reply_head(head, code);
    size += snprintf(head + size, MESSAGE_SIZE - size, "%s: %s\r\n", HTTP_CONTENT_TYPE, vohttpd_mime_map("html"));
    size += snprintf(head + size, MESSAGE_SIZE - size, "%s: %s\r\n", HTTP_DATE_TIME, vohttpd_gmtime());
    size += snprintf(head + size, MESSAGE_SIZE - size, "%s: %d\r\n", HTTP_CONTENT_LENGTH, total);
    strcat(head, "\r\n"); size += 2;

    size = d->set->send(d->sock, head, size, 0);
    if(size <= 0)
        return -1;
    total = d->set->send(d->sock, body, total, 0);
    if(total <= 0)
        return -1;
    return total + size;
}

const char* vohttpd_unload_plugin(const char *path)
{
    char name[FUNCTION_SIZE];
    void *h = NULL;
    int  id = 0;

    _plugin_query query;
    _plugin_cleanup clean;
    plugin_info info;

    if(get_name_from_path(path, name, FUNCTION_SIZE) >= FUNCTION_SIZE)
        return "library name is too long.";
    h = (void *)string_hash_get(g_set.funcs, name);
    if(h == NULL)
        return "library has not been loaded.";

    query = dlsym(h, LIBRARY_QUERY);
    if(query == NULL)
        return "can not find query interface.";
    string_hash_remove(g_set.funcs, name);

    while(query(id++, &info) >= 0) {
        _plugin_func func = dlsym(h, info.name);
        if(func == NULL)
            continue;       // no such interface.
        if(func != (_plugin_func)string_hash_get(g_set.funcs, info.name))
            continue;       // not current interface.
        string_hash_remove(g_set.funcs, info.name);
    }

    clean = dlsym(h, LIBRARY_CLEANUP);
    if(clean)
        clean();

    dlclose(h);
    return NULL;
}

const char* vohttpd_load_plugin(const char *path)
{
    char name[FUNCTION_SIZE];
    void *h = NULL;
    int id = 0;

    _plugin_query query;
    plugin_info info;

    if(get_name_from_path(path, name, FUNCTION_SIZE) >= FUNCTION_SIZE)
        return "library name is too long.";
    if(strchr(name, '.') == NULL)
        return "library name is not correct.";
    if(string_hash_get(g_set.funcs, name))
        return "library has already loaded.";

    h = dlopen(path, RTLD_NOW);
    if(h == NULL)
        return dlerror();

    query = dlsym(h, LIBRARY_QUERY);
    if(query == NULL) {
        dlclose(h);
        return dlerror();
    }

    if(string_hash_set(g_set.funcs, name, (uchar *)h) == NULL) {
        dlclose(h);
        return "hash is full.";
    }

    while(query(id++, &info) >= 0) {
        _plugin_func func = dlsym(h, info.name);
        if(func == NULL)
            continue;       // no such interface.
        if(string_hash_get(g_set.funcs, info.name))
            continue;       // already exists same name interface.
        if(!string_hash_set(g_set.funcs, info.name, (uchar *)func))
            continue;       // hash full?
    }
    return NULL;
}

int vohttpd_send(int sock, const void *data, int size, int type)
{
    return send(sock, data, size, 0);
}

void vohttpd_init()
{
    // default parameters.
    g_set.port = 80;
    g_set.base = "/var/www/html";

    // alloc buffer for globle pointer(maybe make them to static is better?)
    g_set.funcs = string_hash_alloc(FUNCTION_SIZE, FUNCTION_COUNT);
    g_set.socks = linear_hash_alloc(sizeof(socket_data), BUFFER_COUNT);

    // set default callback.
    g_set.send = vohttpd_send;
    g_set.http_filter = vohttpd_data_filter;
    g_set.error_page = vohttpd_error_page;
    g_set.load_plugin = vohttpd_load_plugin;
    g_set.unload_plugin = vohttpd_unload_plugin;
    g_set.http_file = vohttpd_http_file;
    g_set.http_folder = vohttpd_http_folder;

    // ignore the signal, or it will stop our server once client disconnected.
    signal(SIGPIPE, SIG_IGN);
}

void vohttpd_uninit()
{
    safe_free(g_set.funcs);
    safe_free(g_set.socks);
}

void vohttpd_loop()
{
    int socksrv, sockmax, sock, b = 1, count, size;
    uint i;
    char *p;

    struct sockaddr_in addr;
    socklen_t len;
    struct timeval tmv;
    fd_set fdr;
    socket_data *d;

    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(g_set.port);
    len = sizeof(struct sockaddr_in);

    socksrv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    setsockopt(socksrv, SOL_SOCKET, SO_REUSEADDR, (const char *)&b, sizeof(int));
    if(bind(socksrv, (struct sockaddr*)&addr, sizeof(struct sockaddr)) < 0) {
        printf("can not bind to address, %d:%s.\n", errno, strerror(errno));
        return;
    }

    // we can not handle much request at same time, so limit listen backlog.
    if(listen(socksrv, min(SOMAXCONN, BUFFER_COUNT / 2)) < 0) {
        printf("can not listen to port, %d:%s.\n", errno, strerror(errno));
        return;
    }

    // for simple embed server, my choice is select for better compatible.
    // but for heavy load situation in linux, better to change this to epoll.
    while(1) {
        FD_ZERO(&fdr);
        FD_SET(socksrv, &fdr);

        // queue the hash and pickout the max socket.
        sockmax = socksrv;
        for(i = 0; i < g_set.socks->max; i++) {
            d = (socket_data *)linear_hash_val(g_set.socks, i);
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
                d = (socket_data *)linear_hash_val(g_set.socks, i);
                if(d->sock == (int)LINEAR_HASH_NULL)
                    continue;
                socketdata_delete(g_set.socks, d->sock);
            }
            continue;
        }

        if(FD_ISSET(socksrv, &fdr)) {
            count--;
            memset(&addr, 0, sizeof(struct sockaddr_in));

            sock = accept(socksrv, (struct sockaddr*)&addr, &len);
            if(socketdata_new(g_set.socks, sock) == NULL)
                close(sock);
        }

        for(i = 0; i < g_set.socks->max; i++) {
            if(count <= 0)
                break;

            d = (socket_data *)linear_hash_val(g_set.socks, i);
            if((uint)d->sock == LINEAR_HASH_NULL)
                continue;

            if(FD_ISSET(d->sock, &fdr)) {
                count--;

                // body size = 0, we process the reuqest.
                // body size != 0, we put it to buffer, wait for full body then process.
                if(d->size == 0) {

                    // receive http head data.
                    size = recv(d->sock, d->head + d->used, RECVBUF_SIZE - d->used, 0);
                    if(size <= 0) {
                        socketdata_delete(g_set.socks, d->sock);
                        continue;
                    }
                    d->used += size;

                    // FIXME: we do not have to check from beginning every time.
                    // if new recv size > 4, we can check new recv.
                    p = strstr(d->head, HTTP_HEADER_END);
                    if(p == NULL) {
                        // we have filled the buffer but still not get the end of head,
                        // the head size exceeds the allowed size, return error.
                        if(d->used >= RECVBUF_SIZE) {
                            g_set.error_page(d, 413, NULL);
                            socketdata_delete(g_set.socks, d->sock);
                        }
                        // not get the header end, so we wait next recv.
                        continue;
                    }

                    p += sizeof(HTTP_HEADER_END) - 1;

                    // now check the content size.
                    d->recv = d->head + d->used - p;
                    d->body = p;
                    d->type = SOCKET_DATA_STACK;
                    d->size = vohttpd_decode_content_size(d);
                    if(d->size == 0 || d->recv >= d->size) {  // no content or already get full data.
                        g_set.http_filter(d);
                        socketdata_delete(g_set.socks, d->sock);
                        // no body in this http request, it should be GET.
                        continue;
                    }

                    // the head buffer can not contain the body data(too big)
                    // we have to alloc memory for it.
                    if(d->size - d->recv > RECVBUF_SIZE - d->used) {
                        char map[MESSAGE_SIZE];
                        int  fd;

                        // create empty file for mmap.
                        snprintf(map, MESSAGE_SIZE, "%s" HTTP_CGI_BIN MMAP_FILE_NAME, d->set->base, d->sock);
                        fd = open(map, O_RDWR | O_CREAT, S_IRWXU);
                        if(fd < 0) {
                            g_set.error_page(d, 413, strerror(errno));
                            socketdata_delete(g_set.socks, d->sock);
                            continue;
                        }

                        // resize the file, or mmap pointer might fail.
                        lseek(fd, d->size - 1, SEEK_SET);
                        write(fd, "\0", 1);     // set file size to d->size.

                        // clear up in function socketdata_delete.
                        d->body = mmap(NULL, d->size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
                        close(fd);

                        if(d->body == MAP_FAILED) {
                            g_set.error_page(d, 413, strerror(errno));
                            socketdata_delete(g_set.socks, d->sock);
                            continue;
                        }
                        d->type = SOCKET_DATA_MMAP;

                        if(d->recv) {
                            memcpy(d->body, p, d->recv);
                            memset(p, 0, d->recv); // clean head, easy to debug.
                        }
                        // now we should goto body data receive process.
                    }

                } else {

                    // receive http body data.
                    size = recv(d->sock, d->body + d->recv, d->size - d->recv, 0);
                    if(size <= 0) {
                        socketdata_delete(g_set.socks, d->sock);
                        continue;
                    }
                    d->recv += size;

                    if(d->recv >= d->size) {
                        g_set.http_filter(d);
                        socketdata_delete(g_set.socks, d->sock);
                        continue;
                    }
                }
            }
        }

        // do some clean up for next loop.
    }

    close(socksrv);
}

void vohttpd_show_status()
{
    uint i, pos, count = 0;

    printf("PORT:\t%d\n", g_set.port);
    printf("PATH:\t%s\n", g_set.base);

    printf("PLUGINS:\n");
    for(i = 0; i < g_set.funcs->max; i++) {
        pos = i * g_set.funcs->unit;
        if(string_hash_empty(g_set.funcs, pos))
            continue;
        if(!strchr(string_hash_key(g_set.funcs, pos), '.'))
            continue;
        printf("\t%s\n", string_hash_key(g_set.funcs, pos));
        count++;
    }
    if(count == 0)
        printf("\t(empty)\n");
}

void vohttpd_show_usage()
{
    printf("usage: vohttpd [-bdhp?]\n\n");
    printf("example: vohttpd -d/var/www/html/cgi-bin/votest.so -p8080\n");
    printf("\t-b[path]  set www home/base folder, default /var/www/html.\n"
           "\t-d[path]  preload plugin.\n"
           "\t-h,-?     show this usage.\n"
           "\t-p[port]  set server listen port, default 8080.\n"
           "\n");
}

int main(int argc, char *argv[])
{
    vohttpd_init();

    while(argc--) {
        const char *errstr;
        if(argv[argc][0] != '-' && argv[argc][0] != '/')
            continue;
        switch(argv[argc][1]) {

        case 'd':   // preload plugins.
            errstr = vohttpd_load_plugin(argv[argc] + 2);
            if(errstr != NULL)
                printf("load_plugin(%s) error: %s\n", argv[argc] + 2, errstr);
            break;

        case 'p':   // default port.
            g_set.port = atoi(argv[argc] + 2);
            break;

        case 'b':   // default home/base path.
            g_set.base = argv[argc] + 2;
            break;

        case 'h':
        case '?':
            vohttpd_show_usage();
            return -1;
        }
    }

    vohttpd_show_status();

    vohttpd_loop();
    vohttpd_uninit();
    return 0;
}

