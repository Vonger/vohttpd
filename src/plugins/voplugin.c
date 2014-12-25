/*  vohttpd common library
 *
 *  author: Qin Wei(me@vonger.cn)
 *  compile:
 *      cc -shared -o libvohttpd.plugin.so ../vohttpdext.c libvohttpd.plugin.c
 *
 */
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/mman.h>

#include "../vohttpd.h"

char *memstr(char *src, int size, const char *key)
{
    size_t len = strlen(key);
    while(size) {
        if(!memcmp(src, key, len))
            return src;

        src++;
        size--;
    }
    return NULL;
}

int plugin_json_status(socket_data *d, const char *status)
{
    char head[MESSAGE_SIZE], buf[MESSAGE_SIZE];
    int size, total = 0;

    total += snprintf(buf + total, MESSAGE_SIZE - total, "{\"status\":\"%s\"}", status);
    size = vohttpd_reply_head(head, 200);
    size += snprintf(head + size, MESSAGE_SIZE, "%s: %s\r\n", HTTP_CONTENT_TYPE, vohttpd_mime_map("json"));
    size += snprintf(head + size, MESSAGE_SIZE, "%s: %s\r\n", HTTP_DATE_TIME, vohttpd_gmtime());
    size += snprintf(head + size, MESSAGE_SIZE, "%s: %d\r\n", HTTP_CONTENT_LENGTH, total);
    strcat(head + size, "\r\n"); size += 2;

    size = d->set->send(d->sock, head, size, 0);
    if(size <= 0)
        return -1;
    size = d->set->send(d->sock, buf, total, 0);
    if(size <= 0)
        return -1;
    return 0;
}

/* plugins returned json format:
 * {
 *  "status":"success",
 *  "plugins": [
 *   {"name":"libvohttpd.plugin","note":"http ...","status":"loaded"},
 *   {"name":"libvohttpd.test","status":"error"}
 *  ]
 * }
 */
int plugin_list(socket_data *d, string_reference *pa)
{
    char head[MESSAGE_SIZE], buf[SENDBUF_SIZE];
    int size = 0, total = 0, i;

    vohttpd_unused(pa);
    total += snprintf(buf + total, SENDBUF_SIZE - total, "{\"status\":\"success\",\"plugins\":[");
    for(i = 0; i < d->set->funcs->max; i++) {
        int pos = i * d->set->funcs->unit, code;
        char *key = string_hash_key(d->set->funcs, pos);
        void *h = (void *)string_hash_val(d->set->funcs, pos);
        _plugin_query query;
        plugin_info   info;

        if(strchr(key, '.') == NULL)
            continue;

        total += snprintf(buf + total, SENDBUF_SIZE - total, "{\"name\":\"%s\",", key);
        query = dlsym(h, LIBRARY_QUERY);
        if(query == NULL) {
            total += snprintf(buf + total, SENDBUF_SIZE - total, "\"status\":\"no query interface\"}");
            continue;
        }
        if(code = query(0, &info), code < 0) {
            total += snprintf(buf + total, SENDBUF_SIZE - total, "\"status\":\"error %d\"}", code);
            continue;
        }

        total += snprintf(buf + total, SENDBUF_SIZE - total, "\"note\":\"%s\",", info.note);
        total += snprintf(buf + total, SENDBUF_SIZE - total, "\"status\":\"loaded\"},");
    }
    buf[--total] = '\0';   // remove last ','
    strcat(buf, "]}"); total += 2;


    size += vohttpd_reply_head(head, 200);
    size += snprintf(head + size, MESSAGE_SIZE, "%s: %s\r\n", HTTP_CONTENT_TYPE, vohttpd_mime_map("json"));
    size += snprintf(head + size, MESSAGE_SIZE, "%s: %s\r\n", HTTP_DATE_TIME, vohttpd_gmtime());
    size += snprintf(head + size, MESSAGE_SIZE, "%s: %d\r\n", HTTP_CONTENT_LENGTH, total);
    strcat(head + size, "\r\n"); size += 2;

    size = d->set->send(d->sock, head, size, 0);
    if(size <= 0)
        return -1;
    size = d->set->send(d->sock, buf, total, 0);
    if(size <= 0)
        return -1;
    return 0;
}

/* functions returned json format:
 * {
 *  "status":"success",
 *  "interfaces": [
 *   {"name":"plugin_list","note":"list all loaded ...","status":"loaded" },
 *   {"name":"plugin_upload","note":"upload plugi ...","status":"loaded" },
 *   {"name":"plugin_load","note":"load plugin by ...","status":"not loaded" }
 *  ]
 * }
 */
int plugin_list_interface(socket_data *d, string_reference *pa)
{
    char head[MESSAGE_SIZE], buf[SENDBUF_SIZE];
    char name[FUNCTION_SIZE] = {0};
    int size = 0, total = 0, count = 0, i;

    if(pa != NULL) {
        if(pa->size <= 0)
            return plugin_json_status(d, "plugin name is empty.");
        if(pa->size >= FUNCTION_SIZE)
            return plugin_json_status(d, "plugin name is too long.");
        string_reference_dup(pa, name);
    }

    for(i = 0; i < d->set->funcs->max; i++) {
        int pos = i * d->set->funcs->unit, id = 1;
        char *key = string_hash_key(d->set->funcs, pos);
        void *h = (void *)string_hash_val(d->set->funcs, pos);

        _plugin_query query;
        _plugin_func  func;
        plugin_info   info;

        if(strcmp(key, name))
            continue;
        count++;

        total += snprintf(buf + total, SENDBUF_SIZE - total, "{\"status\":\"success\",");
        query = dlsym(h, LIBRARY_QUERY);
        if(query == NULL || query(0, &info) < 0)
            continue;
        total += snprintf(buf + total, SENDBUF_SIZE - total, "\"interfaces\":[");

        while(query(id++, &info) >= 0) {
            const char *status = "loaded";
            func = (_plugin_func)dlsym(h, info.name);
            if(func != (_plugin_func)string_hash_get(d->set->funcs, info.name))
                status = "name conflict";
            total += snprintf(buf + total, SENDBUF_SIZE - total, "{\"name\":\"%s\",\"note\":\"%s\","
                "\"status\":\"%s\"},", info.name, info.note, status);
            if(total >= SENDBUF_SIZE)
                break;
        }
        buf[--total] = '\0';   // remove last ','
    }

    if(total >= SENDBUF_SIZE) {
        // FIXME: we should alloc buffer for this, SENDBUF_SIZE is limited.
        total = snprintf(buf, SENDBUF_SIZE, "{\"status\":\"buffer is not enough!\"}");
    } else {
        strcat(buf, "]}");
        total += 2;
    }
    if(count == 0)
        total = snprintf(buf, SENDBUF_SIZE, "{\"status\":\"no matched plugin.\"}");

    size += vohttpd_reply_head(head, 200);
    size += snprintf(head + size, MESSAGE_SIZE, "%s: %s\r\n", HTTP_CONTENT_TYPE, vohttpd_mime_map("json"));
    size += snprintf(head + size, MESSAGE_SIZE, "%s: %s\r\n", HTTP_DATE_TIME, vohttpd_gmtime());
    size += snprintf(head + size, MESSAGE_SIZE, "%s: %d\r\n", HTTP_CONTENT_LENGTH, total);
    strcat(head + size, "\r\n"); size += 2;

    size = d->set->send(d->sock, head, size, 0);
    if(size <= 0)
        return -1;
    size = d->set->send(d->sock, buf, total, 0);
    if(size <= 0)
        return -1;
    return 0;
}

int plugin_load(socket_data *d, string_reference *pa)
{
    char name[FUNCTION_SIZE] = {0};
    const char *msg;
    if(pa == NULL || pa->size <= 0)
        return plugin_json_status(d, "plugin name is empty.");
    if(pa->size >= FUNCTION_SIZE)
        return plugin_json_status(d, "plugin name is too long.");
    if(memstr(pa->ref, pa->size, "/") || memstr(pa->ref, pa->size, "\\"))
        return plugin_json_status(d, "plugin name is incorrect.");
    string_reference_dup(pa, name);

    msg = d->set->load_plugin(name);
    return plugin_json_status(d, msg == NULL ? "success" : msg);
}

int plugin_unload(socket_data *d, string_reference *pa)
{
    char name[FUNCTION_SIZE] = {0};
    const char *msg;
    if(pa == NULL || pa->size <= 0)
        return plugin_json_status(d, "plugin name is empty.");
    if(pa->size >= FUNCTION_SIZE)
        return plugin_json_status(d, "plugin name is too long.");
    if(memstr(pa->ref, pa->size, "/") || memstr(pa->ref, pa->size, "\\"))
        return plugin_json_status(d, "plugin name is incorrect.");
    string_reference_dup(pa, name);
    if(!strcmp(name, "voplugin.so"))
        return plugin_json_status(d, "can not unload current running plugin.");

    msg = d->set->unload_plugin(name);
    return plugin_json_status(d, msg == NULL ? "success" : msg);
}

/* install/uninstall return json format:
 * {
 *  "status":"success"
 * }
 * status: success or error status.
 */
int plugin_install(socket_data *d, string_reference *pa)
{
    char boundary[MESSAGE_SIZE] = {0}, path[MESSAGE_SIZE], *p, *e;
    char name[FUNCTION_SIZE] = {0};
    const char *msg;

    // get boundary.
    p = strstr(pa->ref, "\r\n");
    if(p == NULL)
        return plugin_json_status(d, "data is not correct.");
    if(p - pa->ref >= MESSAGE_SIZE)
        return plugin_json_status(d, "boundary is too long.");
    memcpy(boundary, pa->ref, p - pa->ref);

    // get file name.
    p = strstr(p, "filename=\"");
    if(p == NULL)
        return plugin_json_status(d, "can not find file name.");
    p += sizeof("filename=\"") - 1;
    e = strchr(p, '\"');
    if(e == NULL)
        return plugin_json_status(d, "can not get file name.");
    if(e - p >= FUNCTION_SIZE)
        return plugin_json_status(d, "plugin name is too long.");
    memcpy(name, p, e - p);
    if(string_hash_get(d->set->funcs, name) != NULL)
        return plugin_json_status(d, "unload exists plugin first.");

    // get file data, store to local.
    p = strstr(e, "\r\n\r\n");
    if(p == NULL)
        return plugin_json_status(d, "no content begin mark.");
    p += 4;

    // FIXME: find the boundary before last, guess tail size = MESSAGE_SIZE.
    e = memstr(pa->ref + pa->size - MESSAGE_SIZE, MESSAGE_SIZE, boundary);
    if(e == NULL)
        return plugin_json_status(d, "no end of content.");

    // write file to local, default: /var/www/html/cgi-bin/.
    snprintf(path, MESSAGE_SIZE, "%s" HTTP_CGI_BIN "%s", d->set->base, name);

    switch(d->type) {
    case SOCKET_DATA_MMAP: {  // mmap memory
        char map[MESSAGE_SIZE];
        int size = e - p - 2;
        snprintf(map, MESSAGE_SIZE, "%s" HTTP_CGI_BIN MMAP_FILE_NAME, d->set->base, d->sock);

        memmove(d->body, p, size);
        msync(d->body, size, MS_SYNC);
        munmap(d->body, d->size);

        // set buffer to NULL to avoid pointer issue.
        d->body = NULL;
        truncate(map, size);
        rename(map, path);
        break; }

    case SOCKET_DATA_STACK: {
        FILE *fp = fopen(path, "wb");
        if(fp == NULL)
            return plugin_json_status(d, "can not open file.");
        fwrite(p, 1, e - p - 2, fp);
        fclose(fp);
        break; }
    }

    // load plugin to vphttpd.
    msg = d->set->load_plugin(path);
    if(msg != NULL)  // error, delete uploaded file.
        remove(path);
    return plugin_json_status(d, msg == NULL ? "success" : msg);
}

int plugin_uninstall(socket_data *d, string_reference *pa)
{
    char name[FUNCTION_SIZE] = {0};
    const char *msg;

    if(pa == NULL || pa->size <= 0)
        return plugin_json_status(d, "plugin name is empty.");
    if(pa->size >= FUNCTION_SIZE)
        return plugin_json_status(d, "plugin name is too long.");
    string_reference_dup(pa, name);
    if(!strcmp(name, "voplugin.so"))
        return plugin_json_status(d, "can not unload current running plugin.");

    msg = d->set->unload_plugin(name);
    if(msg == NULL)
        remove(name);
    return plugin_json_status(d, msg == NULL ? "success" : msg);
}

int vohttpd_library_query(int id, plugin_info *out)
{
    static plugin_info info[] = {
    { ".", "http plugin control interface, return data in json format." },
    { "plugin_list", "list all loaded plugins." },
    { "plugin_list_interface", "list all functions by the plugin name." },
    { "plugin_load", "load plugin by its name, search cgi-bin first, then vohttpd default plugin folder." },
    { "plugin_unload", "unload plugin by its name, search cgi-bin first, then vohttpd default plugin folder." },
    { "plugin_install", "install plugin to vohttpd temp/default plugin folder." },
    { "plugin_uninstall", "remove plugin it from vohttpd temp/default folder." },
    };
    if(id >= sizeof(info) / sizeof(plugin_info))
        return -1;
    memcpy(out, &info[id], sizeof(plugin_info));
    return id;
}

