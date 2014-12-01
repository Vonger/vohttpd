/*  vohttpd common library
 *
 *  author: Qin Wei(me@vonger.cn)
 *  compile: cc -shared -o libvohttpd.test.so vohttpdext.c libvohttpd.test.c
 *
 */
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#include "vohttpd.h"

int common_echo(socket_data *d, string_reference *pa)
{
    char buf[MESSAGE_SIZE];
    int size;

    size = vohttpd_reply_head(buf, 200);
    size += sprintf(buf + size, "%s: %s\r\n", HTTP_CONTENT_TYPE, vohttpd_mime_map("html"));
    size += sprintf(buf + size, "%s: %s\r\n", HTTP_DATE_TIME, vohttpd_gmtime());
    size += sprintf(buf + size, "%s: %d\r\n", HTTP_CONTENT_LENGTH, pa->size);
    strcat(buf + size, "\r\n"); size += 2;

    size = send(d->sock, buf, size, 0);
    if(size <= 0)
        return -1;
    size = send(d->sock, pa->ref, pa->size, 0);
    if(size <= 0)
        return -1;
    return 0;
}

int common_text(socket_data *d, string_reference *pa)
{
    char head[MESSAGE_SIZE], buf[MESSAGE_SIZE];
    int size, total;

    total = sprintf(buf, "<html><head><title>common_text</title></head><body><h"
        "1>Hello World!</h1></body></html>");

    size = vohttpd_reply_head(head, 200);
    size += sprintf(head + size, "%s: %s\r\n", HTTP_CONTENT_TYPE, vohttpd_mime_map("html"));
    size += sprintf(head + size, "%s: %s\r\n", HTTP_DATE_TIME, vohttpd_gmtime());
    size += sprintf(head + size, "%s: %d\r\n", HTTP_CONTENT_LENGTH, total);
    strcat(head + size, "\r\n"); size += 2;

    size = send(d->sock, head, size, 0);
    if(size <= 0)
        return -1;
    size = send(d->sock, buf, total, 0);
    if(size <= 0)
        return -1;
    return 0;
}

static plugin_info g_info[] = {
    {common_echo, "test_echo", "it will return what it get from parameter."},
    {common_text, "test_text", "it will always show hello world."},
};
#define VOHTTPD_PLUGIN "contains test functions for vohttpd."
#define vohttpd_set_note(pp, note) if(pp){*pp = note;}
void* vohttpd_library_query(char *func, const char **note)
{
    static uint index;

    if(func == NULL) {
        vohttpd_set_note(note, VOHTTPD_PLUGIN);
        index = 0;
        return NULL;
    }

    if(index >= sizeof(g_info) / sizeof(library_info))
        return NULL;
    if(strlen(g_info[index].name) >= FUNCTION_SIZE) {
        printf("[%s] PLUGIN FATAL\nInternal function name is too long.\n", vohttpd_gmtime());
        return NULL;
    }

    strcpy(func, g_info[index].name);
    vohttpd_set_note(note, g_info[index].note);
    return g_info[index++].func;
}

