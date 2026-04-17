/*
 * Server-side LPC port management
 *
 * Copyright 2026 Wine project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winternl.h"

#include "handle.h"
#include "thread.h"
#include "process.h"
#include "request.h"
#include "security.h"
#include "object.h"

/* Port access rights */
#define PORT_CONNECT          0x0001
#define PORT_ALL_ACCESS       (STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | PORT_CONNECT)

/* Port types */
#define PORT_TYPE_SERVER      0x01  /* Named port that server listens on */

/* Port flags */
#define PORT_FLAG_WAITABLE    0x0001

/* Maximum message size */
#define MAX_LPC_MESSAGE_SIZE  0x40000

static const WCHAR lpc_port_name[] = {'L','P','C',' ','P','o','r','t'};

struct type_descr lpc_port_type =
{
    { lpc_port_name, sizeof(lpc_port_name) },   /* name */
    PORT_ALL_ACCESS,                             /* valid_access */
    {                                            /* mapping */
        STANDARD_RIGHTS_READ | PORT_CONNECT,
        STANDARD_RIGHTS_WRITE,
        STANDARD_RIGHTS_EXECUTE | SYNCHRONIZE,
        PORT_ALL_ACCESS
    },
};

/* LPC port object */
struct lpc_port
{
    struct object       obj;             /* object header */
    unsigned int        port_type;       /* PORT_TYPE_* */
    unsigned int        flags;           /* PORT_FLAG_* */
    struct list         msg_queue;       /* list of pending messages */
    struct object      *queue_event;     /* event signaled when message arrives */
    unsigned int        max_msg_len;     /* maximum message length */
    unsigned int        max_connect_info;/* maximum connection info length */
    struct object      *wait_event;      /* event for WaitForSingleObject (waitable ports) */
    struct process     *server_process;  /* server process (for named ports) */
};

static void lpc_port_dump( struct object *obj, int verbose );
static struct object *lpc_port_get_sync( struct object *obj );
static void lpc_port_destroy( struct object *obj );

static const struct object_ops lpc_port_ops =
{
    sizeof(struct lpc_port),       /* size */
    &lpc_port_type,                /* type */
    lpc_port_dump,                 /* dump */
    NULL,                          /* add_queue */
    NULL,                          /* remove_queue */
    NULL,                          /* signaled */
    NULL,                          /* satisfied */
    no_signal,                     /* signal */
    no_get_fd,                     /* get_fd */
    lpc_port_get_sync,             /* get_sync */
    default_map_access,            /* map_access */
    default_get_sd,                /* get_sd */
    default_set_sd,                /* set_sd */
    default_get_full_name,         /* get_full_name */
    no_lookup_name,                /* lookup_name */
    directory_link_name,           /* link_name */
    default_unlink_name,           /* unlink_name */
    no_open_file,                  /* open_file */
    no_kernel_obj_list,            /* get_kernel_obj_list */
    no_close_handle,               /* close_handle */
    lpc_port_destroy               /* destroy */
};

static struct lpc_port *create_lpc_port( struct object *root, const struct unicode_str *name,
                                         unsigned int attr, unsigned int flags,
                                         unsigned int max_msg_len, unsigned int max_connect_info,
                                         const struct security_descriptor *sd )
{
    struct lpc_port *port;

    if (max_msg_len > MAX_LPC_MESSAGE_SIZE)
        max_msg_len = MAX_LPC_MESSAGE_SIZE;
    if (max_connect_info > MAX_LPC_MESSAGE_SIZE)
        max_connect_info = MAX_LPC_MESSAGE_SIZE;

    if ((port = create_named_object( root, &lpc_port_ops, name, attr, sd )))
    {
        if (get_error() != STATUS_OBJECT_NAME_EXISTS)
        {
            port->port_type = PORT_TYPE_SERVER;
            port->flags = flags;
            list_init( &port->msg_queue );
            port->queue_event = create_internal_sync( 0, 0 );
            port->max_msg_len = max_msg_len ? max_msg_len : MAX_LPC_MESSAGE_SIZE;
            port->max_connect_info = max_connect_info ? max_connect_info : 256;
            port->wait_event = NULL;
            port->server_process = (struct process *)grab_object( current->process );

            if (!port->queue_event)
            {
                release_object( port );
                return NULL;
            }

            if (flags & PORT_FLAG_WAITABLE)
            {
                port->wait_event = create_internal_sync( 1, 0 );
                if (!port->wait_event)
                {
                    release_object( port );
                    return NULL;
                }
            }
        }
    }
    return port;
}

static void lpc_port_dump( struct object *obj, int verbose )
{
    struct lpc_port *port = (struct lpc_port *)obj;

    assert( obj->ops == &lpc_port_ops );
    fprintf( stderr, "LPC Port type=%s flags=%04x max_msg=%u max_connect=%u\n",
             port->port_type == PORT_TYPE_SERVER ? "SERVER" : "?",
             port->flags, port->max_msg_len, port->max_connect_info );
}

static struct object *lpc_port_get_sync( struct object *obj )
{
    struct lpc_port *port = (struct lpc_port *)obj;

    if (port->wait_event)
        return grab_object( port->wait_event );

    if (port->queue_event)
        return grab_object( port->queue_event );

    return NULL;
}

static void lpc_port_destroy( struct object *obj )
{
    struct lpc_port *port = (struct lpc_port *)obj;

    assert( obj->ops == &lpc_port_ops );

    if (port->queue_event) release_object( port->queue_event );
    if (port->wait_event) release_object( port->wait_event );
    if (port->server_process) release_object( port->server_process );
}

/* Create an LPC port */
DECL_HANDLER(create_lpc_port)
{
    struct lpc_port *port;
    struct unicode_str name;
    struct object *root;
    const struct security_descriptor *sd;
    const struct object_attributes *objattr = get_req_object_attributes( &sd, &name, &root );

    if (!objattr) return;

    if ((port = create_lpc_port( root, &name, objattr->attributes, req->flags,
                                 req->max_msg_len, req->max_connect_info, sd )))
    {
        if (get_error() == STATUS_OBJECT_NAME_EXISTS)
            reply->handle = alloc_handle( current->process, port, req->access, objattr->attributes );
        else
            reply->handle = alloc_handle_no_access_check( current->process, port,
                                                          req->access, objattr->attributes );
        release_object( port );
    }

    if (root) release_object( root );
}
