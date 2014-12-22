/* vohttpdext: move public functions here, to save plugin(dynamic library) size.
 *
 * author: Qin Wei(me@vonger.cn)
 * compile: cc -c vohttpdext.c -o vohttpdext.o
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/socket.h>

#include "vohttpd.h"

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
        return linear_hash_val(lh, pos);

    // try to hit next node if we miss the first.
    for(i = pos + 1; ; i++) {
        if(i >= lh->max)
            i = 0;
        if(i == pos)
            break;

        if(linear_hash_key(lh, i) == key)
            return linear_hash_val(lh, i);
    }
    return NULL;
}

uchar* linear_hash_set(linear_hash *lh, uint key)
{
    uint pos = key % lh->max, i;
    // first hit, this hash node is empty.
    if(linear_hash_empty(lh, pos))
        return linear_hash_val(lh, pos);

    // try to find another empty node.
    for(i = pos + 1; ; i++) {
        if(i >= lh->max)
            i = 0;
        if(i == pos)
            break;

        if(linear_hash_empty(lh, i))
            return linear_hash_val(lh, i);
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

uint string_hash_from(const char *str)
{
    uint hash = *str;
    while(*str++)
        hash = hash * 31 + *str;
    return hash;
}

uchar* string_hash_get(string_hash *sh, const char *key)
{
    uint pos = (string_hash_from(key) % sh->max) * sh->unit, i;
    // match node in the first hit.
    if(strcmp(string_hash_key(sh, pos), key) == 0)
        return string_hash_val(sh, pos);

    // try to hit next node if we miss the first.
    for(i = pos + sh->unit;; i += sh->unit) {
        if(i >= sh->unit * sh->max)
            i = 0;
        if(i == pos)
            break;
        if(strcmp(string_hash_key(sh, i), key) == 0)
            return string_hash_val(sh, i);
    }
    return NULL;
}

uchar* string_hash_set(string_hash *sh, const char *key, uchar *value)
{
    uint pos = (string_hash_from(key) % sh->max) * sh->unit, i;
    // first hit, this hash node is empty.
    if(string_hash_empty(sh, pos)) {
        strcpy(string_hash_key(sh, pos), key);
        memcpy(&string_hash_val(sh, pos), &value, sizeof(uchar *));
        return string_hash_val(sh, pos);
    }

    // try to find another empty node.
    for(i = pos + sh->unit;; i += sh->unit) {
        if(i >= sh->unit * sh->max)
            i = 0;
        if(i == pos)
            break;

        if(string_hash_empty(sh, i)) {
            strcpy(string_hash_key(sh, i), key);
            memcpy(&string_hash_val(sh, i), &value, sizeof(uchar *));
            return string_hash_val(sh, i);
        }
    }
    return NULL;
}

void string_hash_remove(string_hash *sh, const char *key)
{
    uint pos = (string_hash_from(key) % sh->max) * sh->unit, i;
    // match node in the first hit.
    if(strcmp(string_hash_key(sh, pos), key) == 0) {
        string_hash_clear(sh, pos);
        return;
    }

    // try to hit next node if we miss the first.
    for(i = pos + sh->unit;; i += sh->unit) {
        if(i >= sh->unit * sh->max)
            i = 0;
        if(i == pos)
            break;
        if(strcmp(string_hash_key(sh, i), key) == 0) {
            string_hash_clear(sh, i);
            return;
        }
    }
}

#define DATETIME_SIZE       32
#define MIME_TYPE_SIZE      48
#define LINEAR_HASH_NULL    ((uint)(-1))

typedef struct _mime_node {
    const char *key;
    const char *type;
}mime_node;

static mime_node mime_nodes[] = {
    { "html", "text/html" },
    { "css", "text/css" },
    { "js", "application/x-javascript" },
    { "json", "application/json" },
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
        return mime_nodes->type;

    len = strlen(ext);
    for(i = 0; i < sizeof(mime_nodes) / sizeof(mime_node); i++) {
        if(memcmp(ext, mime_nodes[i].key, len))
            continue;
        return mime_nodes[i].type;
    }
    return mime_nodes->type;
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

// return only head parameters, do not contains data before function parameter.
// for example: http://vonger.cn/cgi-bin/hello?sometext,and,ok,
// it will return "sometext,and,ok"
int vohttpd_uri_parameters(socket_data *d, string_reference *s)
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

int vohttpd_uri_first_parameter(string_reference *s, string_reference *first)
{
    char *p, *end;
    p = s->ref;
    end = s->ref + s->size;
    while(p != end) {
        if(*p == ',') {
            first->ref = s->ref;
            first->size = p - s->ref;
            return 1;
        }
        p++;
    }
    return 0;
}

