/*  vohttpd common library
 *
 *  author: Qin Wei(me@vonger.cn)
 *  compile:
 *      cc -shared -o libvohttpd.plugin.so ../vohttpdext.c libvohttpd.plugin.c
 *
 */
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#include "../vohttpd.h"



int vohttpd_library_query(int id, plugin_info *out)
{
    static plugin_info info[] = {
    { ".note", "http plugin control interface, return data in json format." },
    };
    if(id >= sizeof(info) / sizeof(plugin_info))
        return -1;
    memcpy(out, &info[id], sizeof(plugin_info));
    return id;
}

