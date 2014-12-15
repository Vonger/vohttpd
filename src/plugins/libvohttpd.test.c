/*  vohttpd common library
 *
 *  author: Qin Wei(me@vonger.cn)
 *  compile:
 *      cc -shared -o libvohttpd.test.so ../vohttpdext.c libvohttpd.test.c
 *
 */
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#include "../vohttpd.h"

int test_text(socket_data *d, string_reference *pa)
{
    char head[MESSAGE_SIZE], buf[MESSAGE_SIZE];
    int size, total;

    total = sprintf(buf, "<html><head><title>test_text</title></head><body><h"
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

int vohttpd_library_query(int id, plugin_info *out)
{
    static plugin_info info[] = {
    { ".note", "contains test functions for vohttpd." },
    { "test_text", "it will always show hello world." },
    };
    if(id >= sizeof(info) / sizeof(plugin_info))
        return -1;
    memcpy(out, &info[id], sizeof(plugin_info));
    return id;
}

