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
#define PORT_TYPE_CLIENT      0x02  /* Client's end of connection */
#define PORT_TYPE_CHANNEL     0x03  /* Server's per-client communication channel */

/* Port flags */
#define PORT_FLAG_WAITABLE    0x0001

/* LPC message types */
#define LPC_REQUEST            1
#define LPC_REPLY              2
#define LPC_DATAGRAM           3
#define LPC_LOST_REPLY         4
#define LPC_PORT_CLOSED        5
#define LPC_CLIENT_DIED        6
#define LPC_EXCEPTION          7
#define LPC_DEBUG_EVENT        8
#define LPC_ERROR_EVENT        9
#define LPC_CONNECTION_REQUEST 10

/* Maximum message size */
#define MAX_LPC_MESSAGE_SIZE  0x40000
#define MAX_LPC_DATA_SIZE     0x100000

/* Global counter for generating unique message IDs */
static unsigned int global_msg_id_counter = 0;

/* Global list of pending connection requests */
static struct list global_pending_connects = LIST_INIT(global_pending_connects);

/* Global list of pending requests awaiting replies */
static struct list global_pending_requests = LIST_INIT(global_pending_requests);

/* Pending request tracking for request/reply correlation */
struct pending_request
{
    struct list         entry;           /* entry in global_pending_requests */
    unsigned int        msg_id;          /* message ID waiting for reply */
    struct lpc_port    *client_port;     /* client port to deliver reply to */
};

/* Entry in thread's list of ports to notify on termination */
struct lpc_terminate_port_entry
{
    struct list         entry;           /* entry in thread's lpc_terminate_ports list */
    struct lpc_port    *port;            /* the LPC port (client port) */
};

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

/* Internal message structure */
struct lpc_message
{
    struct list         entry;           /* queue entry */
    struct list         global_entry;    /* entry in global_pending_connects */
    struct lpc_port    *sender_port;     /* port that sent this message */
    struct lpc_port    *server_port;     /* server port (for connection requests) */
    struct thread      *sender_thread;   /* thread that sent this message */
    unsigned int        msg_id;          /* unique message ID */
    unsigned int        msg_type;        /* LPC message type */
    process_id_t        client_pid;      /* sender's process ID */
    thread_id_t         client_tid;      /* sender's thread ID */
    client_ptr_t        port_context;    /* port context for this message */
    data_size_t         data_size;       /* size of message data */
    char                data[1];         /* variable-length message data */
};

/* LPC port object */
struct lpc_port
{
    struct object       obj;             /* object header */
    unsigned int        port_type;       /* PORT_TYPE_* */
    unsigned int        flags;           /* PORT_FLAG_* */
    struct lpc_port    *connection_port; /* reference to connection port */
    struct lpc_port    *connected_port;  /* paired port: client <-> channel */
    struct list         msg_queue;       /* list of pending messages */
    struct list         pending_connects;/* list of pending connection messages */
    struct object      *queue_event;     /* event signaled when message arrives */
    unsigned int        max_msg_len;     /* maximum message length */
    unsigned int        max_connect_info;/* maximum connection info length */
    struct object      *wait_event;      /* event for WaitForSingleObject (waitable ports) */
    struct process     *server_process;  /* server process (for named ports) */
    client_ptr_t        port_context;    /* user-defined port context */
    struct thread      *client_thread;   /* client thread (for NtCompleteConnectPort) */
    struct object      *connect_event;   /* event signaled when connection completes */
    unsigned int        connect_status;  /* STATUS_SUCCESS or error code from accept */
};

static void lpc_port_dump( struct object *obj, int verbose );
static struct object *lpc_port_get_sync( struct object *obj );
static int lpc_port_close_handle( struct object *obj, struct process *process, obj_handle_t handle );
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
    lpc_port_close_handle,         /* close_handle */
    lpc_port_destroy               /* destroy */
};

/* Allocate a new message with the given data size */
static struct lpc_message *alloc_lpc_message( data_size_t data_size )
{
    struct lpc_message *msg;
    data_size_t alloc_size;

    if (data_size > MAX_LPC_DATA_SIZE)
    {
        set_error( STATUS_INVALID_PARAMETER );
        return NULL;
    }

    alloc_size = max( sizeof(struct lpc_message), offsetof(struct lpc_message, data) + data_size );
    msg = mem_alloc( alloc_size );
    if (msg)
    {
        memset( msg, 0, alloc_size );
        list_init( &msg->global_entry );
        msg->data_size = data_size;
    }
    return msg;
}

/* Free a message */
static void free_lpc_message( struct lpc_message *msg )
{
    if (msg)
    {
        if (!list_empty( &msg->global_entry ))
            list_remove( &msg->global_entry );
        if (msg->sender_port) release_object( msg->sender_port );
        if (msg->server_port) release_object( msg->server_port );
        if (msg->sender_thread) release_object( msg->sender_thread );
        free( msg );
    }
}

/* Get the next globally unique message ID */
static unsigned int get_next_msg_id( void )
{
    return ++global_msg_id_counter;
}

/* Signal the port's queue event to wake waiting threads */
static void signal_port_queue( struct lpc_port *port )
{
    if (port->queue_event)
        signal_sync( port->queue_event );
    if (port->wait_event)
        signal_sync( port->wait_event );
}

/* Reset the port's queue event after receiving a message */
static void reset_port_queue( struct lpc_port *port )
{
    if (list_empty( &port->msg_queue ) && list_empty( &port->pending_connects ))
    {
        if (port->queue_event)
            reset_sync( port->queue_event );
        if (port->wait_event)
            reset_sync( port->wait_event );
    }
}

/* Track a pending request awaiting reply */
static void track_pending_request( unsigned int msg_id, struct lpc_port *client_port )
{
    struct pending_request *pr = mem_alloc( sizeof(*pr) );
    if (pr)
    {
        pr->msg_id = msg_id;
        pr->client_port = (struct lpc_port *)grab_object( client_port );
        list_add_tail( &global_pending_requests, &pr->entry );
    }
}

/* Find and remove a pending request by message ID */
static struct lpc_port *find_pending_request_client( unsigned int msg_id )
{
    struct pending_request *pr;

    LIST_FOR_EACH_ENTRY( pr, &global_pending_requests, struct pending_request, entry )
    {
        if (pr->msg_id == msg_id)
        {
            struct lpc_port *client = pr->client_port;
            list_remove( &pr->entry );
            free( pr );
            return client;
        }
    }
    return NULL;
}

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
            port->connection_port = (struct lpc_port *)grab_object( port );
            port->connected_port = NULL;
            list_init( &port->msg_queue );
            list_init( &port->pending_connects );
            port->queue_event = create_internal_sync( 0, 0 );
            port->max_msg_len = max_msg_len ? max_msg_len : MAX_LPC_MESSAGE_SIZE;
            port->max_connect_info = max_connect_info ? max_connect_info : 256;
            port->wait_event = NULL;
            port->server_process = (struct process *)grab_object( current->process );
            port->port_context = 0;
            port->client_thread = NULL;
            port->connect_event = NULL;
            port->connect_status = STATUS_PENDING;

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

/* Create a client port (internal, no name) */
static struct lpc_port *create_port_internal( unsigned int port_type, struct lpc_port *connection_port )
{
    struct lpc_port *port;

    port = alloc_object( &lpc_port_ops );
    if (!port) return NULL;

    port->port_type = port_type;
    port->flags = 0;
    port->connection_port = (struct lpc_port *)grab_object( connection_port );
    port->connected_port = NULL;
    list_init( &port->msg_queue );
    list_init( &port->pending_connects );
    port->queue_event = create_internal_sync( 0, 0 );
    port->max_msg_len = connection_port->max_msg_len;
    port->max_connect_info = connection_port->max_connect_info;
    port->wait_event = NULL;
    port->server_process = NULL;
    port->port_context = 0;
    port->client_thread = NULL;
    port->connect_event = NULL;
    port->connect_status = STATUS_PENDING;

    if (port_type == PORT_TYPE_CLIENT)
    {
        port->connect_event = (struct object *)create_server_internal_sync( 1, 0 );
        if (!port->connect_event)
        {
            release_object( port );
            return NULL;
        }
    }

    if (!port->queue_event)
    {
        release_object( port );
        return NULL;
    }

    return port;
}

static void lpc_port_dump( struct object *obj, int verbose )
{
    struct lpc_port *port = (struct lpc_port *)obj;
    static const char *type_names[] = { "???", "SERVER", "CLIENT", "CHANNEL" };
    const char *type_name = port->port_type < 4 ? type_names[port->port_type] : "???";

    assert( obj->ops == &lpc_port_ops );
    fprintf( stderr, "LPC Port type=%s flags=%04x max_msg=%u max_connect=%u\n",
             type_name, port->flags, port->max_msg_len, port->max_connect_info );
}

static struct object *lpc_port_get_sync( struct object *obj )
{
    struct lpc_port *port = (struct lpc_port *)obj;
    assert( obj->ops == &lpc_port_ops );

    /* For clients with a connect_event, return it for connection wait */
    if (port->port_type == PORT_TYPE_CLIENT && port->connect_event)
        return grab_object( port->connect_event );

    if (port->wait_event)
        return grab_object( port->wait_event );

    if (port->queue_event)
        return grab_object( port->queue_event );

    return NULL;
}

/* Break circular references when the last handle to a port is closed */
static int lpc_port_close_handle( struct object *obj, struct process *process, obj_handle_t handle )
{
    struct lpc_port *port = (struct lpc_port *)obj;
    struct pending_request *pr, *next_pr;
    struct lpc_message *msg, *next_msg;

    if (obj->handle_count == 1)
    {
        /* Clean up any pending requests that reference this port */
        LIST_FOR_EACH_ENTRY_SAFE( pr, next_pr, &global_pending_requests, struct pending_request, entry )
        {
            if (pr->client_port == port)
            {
                list_remove( &pr->entry );
                release_object( pr->client_port );
                free( pr );
            }
        }

        /* For SERVER ports, reject all pending connection requests so clients don't hang */
        if (port->port_type == PORT_TYPE_SERVER)
        {
            LIST_FOR_EACH_ENTRY_SAFE( msg, next_msg, &port->pending_connects, struct lpc_message, entry )
            {
                struct lpc_port *client_port = msg->sender_port;
                if (client_port)
                {
                    client_port->connect_status = STATUS_PORT_CONNECTION_REFUSED;
                    if (client_port->connect_event)
                        signal_sync( client_port->connect_event );
                }
                list_remove( &msg->entry );
                free_lpc_message( msg );
            }
        }

        /* Break the bidirectional connected_port reference */
        if (port->connected_port)
        {
            struct lpc_port *peer = port->connected_port;

            /* Clear peer's reference to us first */
            if (peer->connected_port == port)
            {
                peer->connected_port = NULL;
                release_object( port );
            }

            /* Clear our reference to peer */
            port->connected_port = NULL;
            release_object( peer );
        }

        /* For SERVER ports, release the self-reference from connection_port */
        if (port->port_type == PORT_TYPE_SERVER && port->connection_port == port)
        {
            port->connection_port = NULL;
            release_object( port );
        }
        /* For client/channel ports, release the connection_port reference
         * to allow the named server port to be destroyed */
        else if (port->port_type != PORT_TYPE_SERVER && port->connection_port)
        {
            release_object( port->connection_port );
            port->connection_port = NULL;
        }
    }

    return 1;
}

static void lpc_port_destroy( struct object *obj )
{
    struct lpc_port *port = (struct lpc_port *)obj;
    struct lpc_message *msg, *next_msg;

    assert( obj->ops == &lpc_port_ops );

    LIST_FOR_EACH_ENTRY_SAFE( msg, next_msg, &port->msg_queue, struct lpc_message, entry )
    {
        list_remove( &msg->entry );
        free_lpc_message( msg );
    }

    LIST_FOR_EACH_ENTRY_SAFE( msg, next_msg, &port->pending_connects, struct lpc_message, entry )
    {
        list_remove( &msg->entry );
        free_lpc_message( msg );
    }

    if (port->queue_event) release_object( port->queue_event );
    if (port->wait_event) release_object( port->wait_event );
    if (port->connect_event) release_object( port->connect_event );
    if (port->connection_port && port->connection_port != port)
        release_object( port->connection_port );
    if (port->connected_port && port->connected_port != port)
        release_object( port->connected_port );
    if (port->server_process) release_object( port->server_process );
    if (port->client_thread) release_object( port->client_thread );
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

/* Connect to an LPC port */
DECL_HANDLER(connect_lpc_port)
{
    struct lpc_port *connection_port;
    struct lpc_port *client_port;
    struct lpc_message *msg;
    struct unicode_str name;
    struct object *root;
    const struct security_descriptor *sd;
    const struct object_attributes *objattr = get_req_object_attributes( &sd, &name, &root );
    data_size_t info_size = req->info_size;

    if (!objattr) return;

    connection_port = (struct lpc_port *)open_named_object( root, &lpc_port_ops, &name, objattr->attributes );
    if (root) release_object( root );

    if (!connection_port)
    {
        set_error( STATUS_OBJECT_NAME_NOT_FOUND );
        return;
    }

    if (connection_port->port_type != PORT_TYPE_SERVER)
    {
        set_error( STATUS_INVALID_PORT_HANDLE );
        release_object( connection_port );
        return;
    }

    /* Create client port */
    client_port = create_port_internal( PORT_TYPE_CLIENT, connection_port );
    if (!client_port)
    {
        release_object( connection_port );
        return;
    }

    /* Create connection request message */
    msg = alloc_lpc_message( info_size );
    if (!msg)
    {
        release_object( client_port );
        release_object( connection_port );
        return;
    }

    msg->msg_id = get_next_msg_id();
    msg->msg_type = LPC_CONNECTION_REQUEST;
    msg->sender_port = (struct lpc_port *)grab_object( client_port );
    msg->server_port = (struct lpc_port *)grab_object( connection_port );
    msg->sender_thread = (struct thread *)grab_object( current );
    msg->client_pid = current->process->id;
    msg->client_tid = current->id;
    if (info_size) memcpy( msg->data, get_req_data(), info_size );

    /* Queue on server's pending_connects list and global list */
    list_add_tail( &connection_port->pending_connects, &msg->entry );
    list_add_tail( &global_pending_connects, &msg->global_entry );

    /* Signal server that connection request is pending */
    signal_port_queue( connection_port );

    /* Return handle to client port */
    reply->handle = alloc_handle_no_access_check( current->process, client_port,
                                                  req->access, objattr->attributes );
    release_object( client_port );
    release_object( connection_port );
}

/* Listen for connection requests */
DECL_HANDLER(listen_lpc_port)
{
    struct lpc_port *port;
    struct lpc_message *msg;

    port = (struct lpc_port *)get_handle_obj( current->process, req->handle,
                                               0, &lpc_port_ops );
    if (!port) return;

    if (port->port_type != PORT_TYPE_SERVER)
    {
        set_error( STATUS_INVALID_PORT_HANDLE );
        release_object( port );
        return;
    }

    /* Get first pending connection request */
    if (list_empty( &port->pending_connects ))
    {
        set_error( STATUS_PENDING );
        release_object( port );
        return;
    }

    msg = LIST_ENTRY( list_head( &port->pending_connects ), struct lpc_message, entry );

    reply->msg_size = msg->data_size;
    reply->client_pid = msg->client_pid;
    reply->client_tid = msg->client_tid;
    reply->msg_id = msg->msg_id;
    reply->message = 0;

    if (msg->data_size)
        set_reply_data( msg->data, min( msg->data_size, get_reply_max_size() ) );

    release_object( port );
}

/* Find a pending connection message by message ID */
static struct lpc_message *find_pending_connect_global( unsigned int msg_id )
{
    struct lpc_message *msg;

    LIST_FOR_EACH_ENTRY( msg, &global_pending_connects, struct lpc_message, global_entry )
    {
        if (msg->msg_id == msg_id)
            return msg;
    }
    return NULL;
}

/* Accept or reject a connection */
DECL_HANDLER(accept_lpc_connect)
{
    struct lpc_port *connection_port;
    struct lpc_port *comm_port = NULL;
    struct lpc_port *client_port;
    struct lpc_message *msg;

    msg = find_pending_connect_global( req->msg_id );
    if (!msg)
    {
        set_error( STATUS_INVALID_CID );
        return;
    }

    connection_port = msg->server_port;
    if (!connection_port || connection_port->port_type != PORT_TYPE_SERVER)
    {
        set_error( STATUS_INVALID_PORT_HANDLE );
        return;
    }

    client_port = msg->sender_port;

    /* Remove from port's pending list */
    list_remove( &msg->entry );

    if (req->accept)
    {
        comm_port = create_port_internal( PORT_TYPE_CHANNEL, connection_port );
        if (!comm_port)
        {
            free_lpc_message( msg );
            return;
        }

        comm_port->connected_port = (struct lpc_port *)grab_object( client_port );
        client_port->connected_port = (struct lpc_port *)grab_object( comm_port );
        client_port->connect_status = STATUS_SUCCESS;
        comm_port->port_context = req->context;

        if (msg->sender_thread)
            comm_port->client_thread = (struct thread *)grab_object( msg->sender_thread );

        reply->handle = alloc_handle_no_access_check( current->process, comm_port,
                                                       PORT_ALL_ACCESS, 0 );
        release_object( comm_port );
    }
    else
    {
        client_port->connect_status = STATUS_PORT_CONNECTION_REFUSED;
        if (client_port->connect_event)
            signal_sync( client_port->connect_event );
        reply->handle = 0;
    }

    free_lpc_message( msg );
}

/* Complete the connection (wake the client) */
DECL_HANDLER(complete_lpc_connect)
{
    struct lpc_port *port;
    struct lpc_port *client_port;

    port = (struct lpc_port *)get_handle_obj( current->process, req->handle,
                                               PORT_CONNECT, &lpc_port_ops );
    if (!port) return;

    if (port->port_type != PORT_TYPE_CHANNEL)
    {
        set_error( STATUS_INVALID_PORT_HANDLE );
        release_object( port );
        return;
    }

    client_port = port->connected_port;
    if (client_port)
    {
        /* Signal the connect_event to wake the client. */
        if (client_port->connect_event)
        {
            signal_sync( client_port->connect_event );
        }
    }

    if (port->client_thread)
    {
        release_object( port->client_thread );
        port->client_thread = NULL;
    }

    release_object( port );
}

/* Get connection status for client port */
DECL_HANDLER(get_lpc_connect_status)
{
    struct lpc_port *port;

    port = (struct lpc_port *)get_handle_obj( current->process, req->handle,
                                               PORT_CONNECT, &lpc_port_ops );
    if (!port) return;

    if (port->port_type != PORT_TYPE_CLIENT)
    {
        set_error( STATUS_INVALID_PORT_HANDLE );
        release_object( port );
        return;
    }

    reply->status = port->connect_status;
    release_object( port );
}

/* Send a request message and get a message ID for tracking the reply */
DECL_HANDLER(request_lpc_reply)
{
    struct lpc_port *port;
    struct lpc_port *target_port;
    struct lpc_message *msg;
    data_size_t data_size = get_req_data_size();

    port = (struct lpc_port *)get_handle_obj( current->process, req->handle,
                                               PORT_CONNECT, &lpc_port_ops );
    if (!port) return;

    if (port->port_type == PORT_TYPE_CLIENT)
    {
        if (!port->connected_port || !port->connected_port->connection_port)
        {
            set_error( STATUS_PORT_DISCONNECTED );
            release_object( port );
            return;
        }
        target_port = port->connected_port->connection_port;
    }
    else if (port->port_type == PORT_TYPE_CHANNEL)
    {
        if (!port->connected_port)
        {
            set_error( STATUS_PORT_DISCONNECTED );
            release_object( port );
            return;
        }
        target_port = port->connected_port;
    }
    else
    {
        set_error( STATUS_INVALID_PORT_HANDLE );
        release_object( port );
        return;
    }

    msg = alloc_lpc_message( data_size );
    if (!msg)
    {
        release_object( port );
        return;
    }

    msg->sender_port = (struct lpc_port *)grab_object( port );
    msg->sender_thread = (struct thread *)grab_object( current );
    msg->msg_id = get_next_msg_id();
    msg->msg_type = req->msg_type ? req->msg_type : LPC_REQUEST;
    msg->client_pid = current->process->id;
    msg->client_tid = current->id;
    msg->port_context = port->port_context;
    if (data_size)
        memcpy( msg->data, get_req_data(), data_size );

    if (msg->msg_type == LPC_REQUEST && port->port_type == PORT_TYPE_CLIENT)
        track_pending_request( msg->msg_id, port );

    list_add_tail( &target_port->msg_queue, &msg->entry );
    signal_port_queue( target_port );

    reply->msg_id = msg->msg_id;

    release_object( port );
}

/* Receive a message and optionally reply to a previous message */
DECL_HANDLER(reply_wait_receive_lpc)
{
    struct lpc_port *port;
    struct lpc_port *receive_port;
    struct lpc_message *msg;
    data_size_t reply_size = get_req_data_size();

    port = (struct lpc_port *)get_handle_obj( current->process, req->handle,
                                               PORT_CONNECT, &lpc_port_ops );
    if (!port) return;

    /* Handle reply to previous message */
    if (req->reply_msg_id)
    {
        struct lpc_port *client_port;
        struct lpc_message *reply_msg;

        client_port = find_pending_request_client( req->reply_msg_id );
        if (client_port)
        {
            reply_msg = alloc_lpc_message( reply_size );
            if (reply_msg)
            {
                reply_msg->msg_type = LPC_REPLY;
                reply_msg->msg_id = req->reply_msg_id;
                reply_msg->client_pid = current->process->id;
                reply_msg->client_tid = current->id;
                reply_msg->data_size = reply_size;
                if (reply_size)
                    memcpy( reply_msg->data, get_req_data(), reply_size );

                list_add_tail( &client_port->msg_queue, &reply_msg->entry );
                signal_port_queue( client_port );
            }
            release_object( client_port );
        }
    }

    if (port->port_type == PORT_TYPE_CHANNEL || port->port_type == PORT_TYPE_CLIENT)
        receive_port = port;
    else if (port->port_type == PORT_TYPE_SERVER)
        receive_port = port;
    else
    {
        set_error( STATUS_INVALID_PORT_HANDLE );
        release_object( port );
        return;
    }

    if (list_empty( &receive_port->msg_queue ))
    {
        if (port->port_type == PORT_TYPE_SERVER && !list_empty( &port->pending_connects ))
        {
            /* Peek at pending connection request without removing it.
             * The message stays in pending_connects until accept_lpc_connect
             * is called, which will remove and free it properly. */
            msg = LIST_ENTRY( list_head( &port->pending_connects ), struct lpc_message, entry );

            reply->msg_id = msg->msg_id;
            reply->msg_type = msg->msg_type;
            reply->client_pid = msg->client_pid;
            reply->client_tid = msg->client_tid;
            reply->context = msg->port_context;
            reply->data_size = msg->data_size;

            if (msg->data_size)
                set_reply_data( msg->data, min( msg->data_size, get_reply_max_size() ) );

            reset_port_queue( receive_port );
            release_object( port );
            return;
        }
        else
        {
            set_error( STATUS_PENDING );
            release_object( port );
            return;
        }
    }
    else
    {
        msg = LIST_ENTRY( list_head( &receive_port->msg_queue ), struct lpc_message, entry );
        list_remove( &msg->entry );
    }

    reply->msg_id = msg->msg_id;
    reply->msg_type = msg->msg_type;
    reply->client_pid = msg->client_pid;
    reply->client_tid = msg->client_tid;
    reply->context = msg->port_context;
    reply->data_size = msg->data_size;

    if (msg->data_size)
        set_reply_data( msg->data, min( msg->data_size, get_reply_max_size() ) );

    free_lpc_message( msg );
    reset_port_queue( receive_port );
    release_object( port );
}

/* Register a port for thread termination notification */
DECL_HANDLER(register_lpc_terminate_port)
{
    struct lpc_port *port;
    struct lpc_terminate_port_entry *entry;

    port = (struct lpc_port *)get_handle_obj( current->process, req->handle,
                                               0, &lpc_port_ops );
    if (!port) return;

    /* Only client ports should be registered for termination */
    if (port->port_type != PORT_TYPE_CLIENT)
    {
        set_error( STATUS_INVALID_PORT_HANDLE );
        release_object( port );
        return;
    }

    /* Check if already registered */
    LIST_FOR_EACH_ENTRY( entry, &current->lpc_terminate_ports, struct lpc_terminate_port_entry, entry )
    {
        if (entry->port == port)
        {
            release_object( port );
            return;
        }
    }

    /* Add to thread's terminate port list */
    entry = mem_alloc( sizeof(*entry) );
    if (entry)
    {
        entry->port = port;  /* transfer the reference */
        list_add_tail( &current->lpc_terminate_ports, &entry->entry );
    }
    else
    {
        release_object( port );
    }
}

/* Send LPC_CLIENT_DIED messages to all registered ports for a thread.
 * Called from cleanup_thread in thread.c */
void lpc_send_client_died( struct thread *thread )
{
    struct lpc_terminate_port_entry *entry, *next;

    LIST_FOR_EACH_ENTRY_SAFE( entry, next, &thread->lpc_terminate_ports,
                              struct lpc_terminate_port_entry, entry )
    {
        struct lpc_port *client_port = entry->port;

        /* Send LPC_CLIENT_DIED to the server port via the communication channel */
        if (client_port && client_port->connected_port)
        {
            struct lpc_port *comm_port = client_port->connected_port;
            struct lpc_port *server_port = comm_port->connection_port;

            if (server_port && server_port->port_type == PORT_TYPE_SERVER)
            {
                struct lpc_message *died_msg = alloc_lpc_message( 0 );
                if (died_msg)
                {
                    died_msg->msg_id = get_next_msg_id();
                    died_msg->msg_type = LPC_CLIENT_DIED;
                    died_msg->client_pid = thread->process->id;
                    died_msg->client_tid = thread->id;
                    died_msg->port_context = comm_port->port_context;

                    /* Queue on server port */
                    list_add_tail( &server_port->msg_queue, &died_msg->entry );
                    signal_port_queue( server_port );
                }
            }
        }

        /* Clean up the entry */
        list_remove( &entry->entry );
        release_object( client_port );
        free( entry );
    }
}
