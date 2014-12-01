#ifndef VOHTTPD_H
#define VOHTTPD_H

#ifdef __cplusplus
extern "C" {
#endif

#define RECVBUF_SIZE        4096
#define SENDBUF_SIZE        4096
#define MESSAGE_SIZE        256
#define BUFFER_COUNT        12
#define FUNCTION_SIZE       32
#define FUNCTION_COUNT      256
#define LIBRARY_QUERY       "vohttpd_library_query"
#define LIBRARY_CLEANUP     "vohttpd_library_cleanup"
#define VOHTTPD_NAME        "vohttpd v0.1"

#define HTTP_HEADER_END     "\r\n\r\n"
#define HTTP_CONTENT_LENGTH "Content-Length"
#define HTTP_CONTENT_TYPE   "Content-Type"
#define HTTP_DATE_TIME      "Date"
#define HTTP_CONNECTION     "Connection"
#define HTTP_GET            "GET"
#define HTTP_POST           "POST"
#define HTTP_CGI_BIN        "/cgi-bin/"
#define HTTP_FONT           "Helvetica,Arial,sans-serif"
#define vohttpd_unused(p)   ((void *)p)

typedef unsigned char uchar;
typedef unsigned int  uint;

typedef struct _string_reference {
    char*   ref;        // point to string start byte.
    uint    size;       // size of the string.
} string_reference;

typedef struct _plugin_info {
    void*       func;
    const char* name;
    const char* note;
} plugin_info;

typedef struct _global_setting {
    unsigned short port;
    char*          base;
} global_setting;

typedef struct _socket_data {
    int   sock;

    // this static buffer is used to store http header.
    // if post header + body size < RECVBUF_SIZE, all store here.
    uint   used;        // received header size.
    char   head[RECVBUF_SIZE];

    // if in post mode the receive buffer exceed our head buffer size,
    // we alloc a buffer for the body.
    uint   size;        // max size of the body buffer.
    uint   recv;        // received data size.
    char*  body;        // point to head + used if head buffer is enough.

    global_setting *set;// pointer to global setting.
} socket_data;

// query exported functions in the plugin, plugin must have this interface.
typedef void* (*plugin_query)(char *func, const char **note);
// clean up when the plugin is about to unload, not necessary.
typedef void* (*plugin_cleanup)();
// exported functions interface must in this format.
typedef int   (*plugin_func)(socket_data *, string_reference *pa);
// predeal function, every request will come to this, return 0 to continue.
typedef int   (*filter_init)(socket_data *);

extern filter_init vohttpd_filter_init;

// helper functions:
extern char* string_reference_dup(string_reference *str, char *buf);
extern int vohttpd_reply_head(char *d, int code);
extern int vohttpd_error_page(socket_data *d, int code, const char *err);
extern int vohttpd_http_file(socket_data *d, const char *path);
extern int vohttpd_head_parameter(socket_data *d, string_reference *s);
extern int vohttpd_get_parameter(string_reference *s);
extern const char *vohttpd_mime_map(const char *ext);
extern const char *vohttpd_gmtime();

#ifdef __cplusplus
}
#endif

#endif // VOHTTPD_H
