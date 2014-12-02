/*  vohttpd common library
 *
 *  author: Qin Wei(me@vonger.cn)
 *  compile:
 *      cc -shared -o libvohttpd.plugin.so ../vohttpdext.c libvohttpd.plugin.c
 *
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/time.h>

#include "../vohttpd.h"

int plugin_upload(socket_data *d, string_reference *pa)
{
    char path[MESSAGE_SIZE], name[FUNCTION_SIZE];
    FILE *fp;

    if(pa->size >= FUNCTION_SIZE)
        return vohttpd_error_page(d, 413, "Plugin name is too long.");
    string_reference_dup(pa, name);

    sprintf(path, "%s" HTTP_CGI_BIN "%s", d->set->base, name);
    fp = fopen(path, "w");
    if(fp == NULL)
        return vohttpd_error_page(d, 403, "Can not create local file.");
    if(pa->size != fwrite(pa->ref, 1, pa->size, fp))
        return vohttpd_error_page(d, 403, "Can not write local file.");
    fclose(fp);

    return vohttpd_error_page(d, 200, NULL);
}

int plugin_remove(socket_data *d, string_reference *pa)
{
    char path[MESSAGE_SIZE], name[FUNCTION_SIZE];

    if(pa->size >= FUNCTION_SIZE)
        return vohttpd_error_page(d, 413, "Plugin name is too long.");
    string_reference_dup(pa, name);

    sprintf(path, "%s" HTTP_CGI_BIN "%s", d->set->base, name);
    if(remove(path))
        return vohttpd_error_page(d, 403, "Can not remove local file.");

    return vohttpd_error_page(d, 200, NULL);
}

static plugin_info g_info[] = {
    {
        plugin_upload,
        "plugin_upload",
        "save data to local, data will append to temp file."
    },

    {
        plugin_remove,
        "plugin_remove",
        "delete plugin from cgi-bin folder, parameter is the file name."
    },
};
#define VOHTTPD_PLUGIN "contains plugin control functions"
#define vohttpd_set_note(pp, note) if(pp){*pp = note;}
void* vohttpd_library_query(char *func, const char **note)
{
    static uint index;

    if(func == NULL) {
        vohttpd_set_note(note, VOHTTPD_PLUGIN);
        index = 0;
        return NULL;
    }

    if(index >= sizeof(g_info) / sizeof(plugin_info))
        return NULL;
    if(strlen(g_info[index].name) >= FUNCTION_SIZE) {
        printf("[%s] PLUGIN FATAL\nInternal function name is too long.\n",
            vohttpd_gmtime());
        return NULL;
    }

    strcpy(func, g_info[index].name);
    vohttpd_set_note(note, g_info[index].note);
    return g_info[index++].func;
}
