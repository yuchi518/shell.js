/*
 * Shell.js (jssh), JavaScript shell
 * Copyright (C) 2015 Yuchi (yuchi518@gmail.com)

 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation. For the terms of this
 * license, see <http://www.gnu.org/licenses>.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
//
// Created by Yuchi on 2015/12/12.
//

#include "jsc_net.h"
#include "mongoose.h"
#include "common.h"

struct mg_serve_http_opts s_http_server_opts;
static void _httpd_handler(struct mg_connection *nc, int ev, void *p)
{
    if (ev == MG_EV_HTTP_REQUEST) {
        //mg_serve_http(nc, (struct http_message *) p, s_http_server_opts);
        mg_send_head(nc, 200, 10, NULL);
        mg_printf(nc, "1234567890");
        //mg_close_conn(nc);
    }
}

static void* _httpd(void* param)
{
    const char *s_http_port = "8080";
    struct mg_mgr mgr;
    struct mg_connection *nc;

    mg_mgr_init(&mgr, &s_http_server_opts);
    nc = mg_bind(&mgr, s_http_port, _httpd_handler);

    // Set up HTTP server parameters
    mg_set_protocol_http_websocket(nc);
    s_http_server_opts.document_root = "/";  // Serve current directory
    s_http_server_opts.dav_document_root = "/";  // Allow access via WebDav
    s_http_server_opts.enable_directory_listing = "yes";

    printf("Starting web server on port %s\n", s_http_port);
    for (;;) {
        mg_mgr_poll(&mgr, 1000);
    }
    mg_mgr_free(&mgr);

    return NULL;
}

static enum v7_err jsc_httpd(struct v7* v7, v7_val_t* result)
{
    run(_httpd, 0, NULL);

    *result = v7_mk_undefined();
    return V7_OK;
}

void jsc_install_net_lib(struct v7* v7)
{
    v7_set_method(v7, v7_get_global(v7), "httpd", &jsc_httpd);
}

void jsc_uninstall_net_lib(struct v7* v7)
{

}
