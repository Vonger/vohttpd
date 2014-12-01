/* vohttpdext: move public functions here, to save plugin(dynamic library) size.
 *
 * author: Qin Wei(me@vonger.cn)
 * compile: cc -c vohttpdext.c -o vohttpdext.o
 *
 */

#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <sys/socket.h>

#include "vohttpd.h"

#define DATETIME_SIZE       32
#define MIME_TYPE_SIZE      48

typedef struct _mime_node {
    const char *key;
    const char *type;
}mime_node;

static mime_node mime_nodes[] = {
    { "html", "text/html" },
    { "css", "text/css" },
    { "js", "application/x-javascript" },
    { "gif", "image/gif" },
    { "jpg", "image/jpeg" },
    { "png", "image/png" },
    { "ico", "image/vnd.microsoft.icon" },
    { "txt", "text/plain"},
    { "swf", "application/x-shockwave-flash" },
    { "exe", "application/binary" },
    { "gz", "application/gzip" },
    { "pdf", "application/pdf" },
    { "rtf", "application/rtf" },
    { "zip", "application/zip" },
    { "wav", "audio/x-wav" },
    { "jpeg", "image/jpeg" },
    { "tiff", "image/tiff" },
    { "mov", "video/quicktime" },
    { "mp4", "video/mp4" },
    { "avi", "video/x-msvideo" },
    { "xml", "text/xml" },
};

/* check str->size to make sure buffer is enough for the string */
char* string_reference_dup(string_reference *str, char *buf)
{
    if(str == NULL)
        return "";

    memcpy(buf, str->ref, str->size);
    *(buf + str->size) = '\0';
    return buf;
}

// input: 4byte extend file name, such as txt, wav, html ... etc.
const char *vohttpd_mime_map(const char *ext)
{
    uint i, len;
    if(ext == NULL)
        return "application/octet-stream";

    len = strlen(ext);
    for(i = 0; i < sizeof(mime_nodes) / sizeof(mime_node); i++) {
        if(memcmp(ext, mime_nodes[i].key, len))
            continue;
        return mime_nodes[i].type;
    }
    return "application/octet-stream";
}

const char *vohttpd_code_message(int code)
{
    switch(code) {
    case 200:
        return "OK";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 405:
        return "Access Denied";
    case 413:
        return "Request too large";
    case 501:
        return "Not Implemented";
    default:
        return "Unknown";
    }
}

int vohttpd_reply_head(char *d, int code)
{
    int size = 0;
    size += sprintf(d + size, "HTTP/1.1 %d %s\r\n", code, vohttpd_code_message(code));
    size += sprintf(d + size, "Server: " VOHTTPD_NAME "\r\n");
    return size;
}

const char *vohttpd_gmtime()
{
    static char out[DATETIME_SIZE];
    time_t t;
    struct tm * pt;

    t = time(NULL);
    pt = gmtime(&t);
    strftime(out, DATETIME_SIZE, "%a, %d %b %Y %H:%M:%S GMT", pt);
    return out;
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

// return only head parameters, do not care about function name.
// for example: http://vonger.cn/cgi-bin/hello?sometext,and,ok,
// it will return "sometext,and,ok"
int vohttpd_head_parameter(socket_data *d, string_reference *s)
{
    char *end, *p;
    p = strstr(d->head, "\r\n");
    if(p == NULL) {
        s->size = 0;
        return 0;
    }

    while(p != d->head) {
        if(*p == '?') {
            s->ref = p + 1;
            break;
        }
        if(*p == ' ')
            end = p;
        p--;
    }
    s->size = end - p;
    return s->size;
}

int vohttpd_get_parameter(string_reference *s)
{
    char *p, *end;
    p = s->ref;
    end = s->ref + s->size;
    while(p != end) {
        if(*p == ',') {
            s->size = p - s->ref;
            return 1;
        }
        p++;
    }
    return 0;
}
