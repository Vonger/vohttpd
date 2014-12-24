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
#define VOHTTPD_NAME        "vohttpd v0.2"

#define HTTP_CONTENT_LENGTH "Content-Length"
#define HTTP_CONTENT_TYPE   "Content-Type"
#define HTTP_DATE_TIME      "Date"
#define HTTP_CONNECTION     "Connection"
#define HTTP_CGI_BIN        "/cgi-bin/"
#define MMAP_FILE_NAME      "mmap.%d"

#define vohttpd_unused(p)   ((void *)p)
#define safe_free(p)        if(p) { free(p); p = NULL; }

typedef unsigned char uchar;
typedef unsigned int  uint;

#define max(a, b)           ((a) > (b) ? (a) : (b))
#define min(a, b)           ((a) < (b) ? (a) : (b))

/* linear hash, every unit must start with its key. */
typedef struct _linear_hash {
    uint   unit;            // the size for each unit.
    uint   max;             // the max allowed unit.

    uchar  data[1];         // data buffer.
}linear_hash;
typedef linear_hash string_hash;

#define LINEAR_HASH_NULL         ((uint)(-1))
#define linear_hash_empty(h, p)  (linear_hash_key((h), (p)) == LINEAR_HASH_NULL)
#define linear_hash_clear(h, p)  {linear_hash_key((h), (p)) = LINEAR_HASH_NULL;}
#define string_hash_empty(h, p)  (*string_hash_key((h), (p)) == '\0')
#define string_hash_clear(h, p)  {*string_hash_key((h), (p)) = '\0';}
#define linear_hash_key(h, p)    (*(uint *)((h)->data + (p) * (h)->unit))
#define linear_hash_val(h, p)    ((h)->data + (p) * (h)->unit)
#define string_hash_key(h, p)    ((char *)(((h)->data + (p)) + sizeof(uchar *)))
#define string_hash_val(h, p)    (*((uchar **)((h)->data + (p))))

extern linear_hash* linear_hash_alloc(uint unit, uint max);
extern uchar* linear_hash_get(linear_hash *lh, uint key);
extern uchar* linear_hash_set(linear_hash *lh, uint key);
extern void linear_hash_remove(linear_hash *lh, uint key);
extern string_hash* string_hash_alloc(uint unit, uint max);
extern uchar* string_hash_get(string_hash *sh, const char *key);
extern uchar* string_hash_set(string_hash *sh, const char *key, uchar *value);
extern void string_hash_remove(string_hash *sh, const char *key);

typedef struct _string_reference {
    char*   ref;            // point to string start byte.
    uint    size;           // size of the string.
} string_reference;

typedef struct _vohttpd vohttpd;

enum SOCKET_DATA_TYPE {
    SOCKET_DATA_NULL,
    SOCKET_DATA_STACK,
    SOCKET_DATA_MMAP,
};

typedef struct _socket_data {
    int   sock;

    // this static buffer is used to store http header.
    // if post header + body size < RECVBUF_SIZE, all store here.
    uint   used;        // received header size.
    char   head[RECVBUF_SIZE];

    // if in post mode the receive buffer exceed our head buffer size,
    // we alloc a buffer for the body.
    uint   size;        // max size of the body buffer.
    uint   recv;        // received body data size.
    char*  body;        // point to head + used if head buffer is enough.
    uint   type;        //

    vohttpd* set;       // pointer to global setting.
} socket_data;

typedef struct _plugin_info {
    const char*     name;       // plugin function name, max 31 bytes.
    const char*     note;       // plugin function note/readme, max 2047 bytes.
} plugin_info;

// exported functions interface must in this format.
typedef int   (*_plugin_func)(socket_data *, string_reference *pa);
// query exported functions in the plugin, plugin must have this interface.
typedef int   (*_plugin_query)(int, plugin_info *);
// clean up when the plugin is about to unload, not necessary.
typedef int   (*_plugin_cleanup)();

// predeal function, every http request will come to this, return 0 to continue.
typedef int   (*_http_filter)(socket_data *);
// error page interface, used to customize error page.
typedef int   (*_error_page)(socket_data *, int, const char *);
// http file/index request function.
typedef int   (*_http_file)(socket_data *, const char *);
// http folder request function.
typedef int   (*_http_folder)(socket_data *, const char *);

typedef const char* (*_load_plugin)(const char *);
typedef const char* (*_unload_plugin)(const char *);
typedef int   (*_httpd_send)(int, const void*, int, int);

struct _vohttpd {
    unsigned short port;            // default http server port.
    const char*    base;            // default http folder path.

    linear_hash*   socks;           // store all accepted sockets.
    string_hash*   funcs;           // store all registered plugins(file, function).

    // common function hook.
    _httpd_send    send;
    _http_filter   http_filter;
    _http_file     http_file;
    _http_folder   http_folder;
    _error_page    error_page;

    _load_plugin   load_plugin;
    _unload_plugin unload_plugin;
};

// helper functions:
extern char* string_reference_dup(string_reference *str, char *buf);
extern int vohttpd_reply_head(char *d, int code);
extern int vohttpd_http_file(socket_data *d, const char *path);
extern int vohttpd_uri_parameters(socket_data *d, string_reference *s);
extern int vohttpd_first_parameter(string_reference *s, string_reference *f);
extern const char *vohttpd_code_message(int code);
extern const char *vohttpd_mime_map(const char *ext);
extern const char *vohttpd_gmtime();

#ifdef __cplusplus
}
#endif

#endif // VOHTTPD_H
