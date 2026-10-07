"""Work-on-behalf ticket / context slice. Usage: patch-wob-a.py <wine source dir>

Edits the current file, so later slices are preserved. An edit whose
replacement is already present is skipped; every other anchor must match
exactly once. A `.pre-wob` backup is kept the first time a file is changed.
Apply after patch-sendattr.py (see README, "Patch application order")."""
import os, shutil, sys
S = sys.argv[1]

def patch(rel, pairs):
    p = os.path.join(S, rel)
    src = open(p).read()
    orig = src
    for old, new in pairs:
        if src.count(new) == 1 and new != old:
            continue
        if src.count(old) != 1:
            sys.exit("anchor count %d in %s: %r" % (src.count(old), rel, old[:70]))
        src = src.replace(old, new)
    if src != orig:
        if not os.path.exists(p + ".pre-wob"):
            shutil.copy2(p, p + ".pre-wob")
        open(p, "w").write(src)
    print("patched" if src != orig else "already applied", rel)

patch("server/protocol.def", [
("""    obj_handle_t token;        /* impersonation token */
    int          disable_boost;/* disable thread priority boost */
    unsigned int mask;         /* setting mask (see below) */""",
"""    obj_handle_t token;        /* impersonation token */
    int          disable_boost;/* disable thread priority boost */
    client_ptr_t wob_ticket;   /* work-on-behalf ticket override */
    unsigned int mask;         /* setting mask (see below) */"""),
("""#define SET_THREAD_INFO_DISABLE_BOOST   0x80
""",
"""#define SET_THREAD_INFO_DISABLE_BOOST   0x80
#define SET_THREAD_INFO_WOB_TICKET      0x100
"""),
("""/* Disconnect an ALPC port */
@REQ(alpc_disconnect_port)""",
"""/* Query basic information about an ALPC port */
@REQ(alpc_query_port)
    obj_handle_t  handle;                     /* ALPC port handle */
@REPLY
    unsigned int  flags;                      /* port flags */
    unsigned int  seq_no;                     /* messages delivered for this endpoint */
    client_ptr_t  port_context;               /* port context */
@END


/* Disconnect an ALPC port */
@REQ(alpc_disconnect_port)"""),
])

patch("server/thread.h", [
("""    timeout_t              creation_time; /* Thread creation time */""",
"""    timeout_t              creation_time; /* Thread creation time */
    client_ptr_t           wob_ticket;    /* work-on-behalf ticket override, 0 when unset */"""),
("""extern struct thread *get_thread_from_id( thread_id_t id );""",
"""extern struct thread *get_thread_from_id( thread_id_t id );
extern client_ptr_t get_thread_wob_identity( const struct thread *thread );
extern client_ptr_t get_thread_wob_ticket( const struct thread *thread );"""),
])

patch("server/thread.c", [
("""    thread->creation_time = current_time;""",
"""    thread->creation_time = current_time;
    thread->wob_ticket    = 0;"""),
("""/* get a thread from a handle (and increment the refcount) */
struct thread *get_thread_from_handle(""",
"""/* Windows issues an opaque per-thread work-on-behalf ticket; wineserver derives it from
 * the thread id and the low part of the creation time */
client_ptr_t get_thread_wob_identity( const struct thread *thread )
{
    return thread->id | ((client_ptr_t)(unsigned int)thread->creation_time << 32);
}

/* ticket carried by work-on-behalf ALPC attributes: the override if set, else the identity */
client_ptr_t get_thread_wob_ticket( const struct thread *thread )
{
    return thread->wob_ticket ? thread->wob_ticket : get_thread_wob_identity( thread );
}

/* only tickets of live threads are accepted; zero clears the override */
static unsigned int set_thread_wob_ticket( struct thread *thread, client_ptr_t ticket )
{
    thread_id_t id = (thread_id_t)ticket;
    struct object *obj;
    struct thread *owner;

    if (ticket)
    {
        if (!id || (id & 3)) return STATUS_INVALID_CID;
        if (!(obj = get_ptid_entry( id )) || obj->ops != &thread_ops) return STATUS_NOT_FOUND;
        owner = (struct thread *)obj;
        if (owner->state == TERMINATED || get_thread_wob_identity( owner ) != ticket) return STATUS_NOT_FOUND;
    }
    thread->wob_ticket = ticket;
    return STATUS_SUCCESS;
}

/* get a thread from a handle (and increment the refcount) */
struct thread *get_thread_from_handle("""),
("""    if (req->mask & SET_THREAD_INFO_DISABLE_BOOST)
        set_thread_disable_boost( thread, req->disable_boost );""",
"""    if (req->mask & SET_THREAD_INFO_DISABLE_BOOST)
        set_thread_disable_boost( thread, req->disable_boost );
    if (req->mask & SET_THREAD_INFO_WOB_TICKET)
    {
        unsigned int status;

        if (thread != current) set_error( STATUS_INVALID_PARAMETER );
        else if ((status = set_thread_wob_ticket( thread, req->wob_ticket ))) set_error( status );
    }"""),
])

patch("server/alpc.c", [
("""    client_ptr_t             port_context; /* opaque port context set by NtAlpcAcceptConnectPort() */
    struct alpc_port_attr_t  port_attr;    /* port attributes */
};""",
"""    client_ptr_t             port_context; /* opaque port context set by NtAlpcAcceptConnectPort() */
    struct alpc_port_attr_t  port_attr;    /* port attributes */
    unsigned int             seq_no;       /* messages delivered for this endpoint */
    int                      server_side;  /* communication port created by NtAlpcAcceptConnectPort() */
    client_ptr_t             accept_context; /* context passed to NtAlpcAcceptConnectPort() */
    obj_handle_t             client_handle;  /* handle returned by NtAlpcConnectPort() */
};"""),
("""        /* Windows replaces the sender's value with a kernel-issued ticket for the sending thread;
         * the ticket is opaque, wineserver derives it from the thread id and creation time */
        record->work_on_behalf_attr->ticket = current->id |
            ((unsigned __int64)(unsigned int)current->creation_time << 32);""",
"""        /* Windows replaces the sender's value with the sending thread's kernel ticket,
         * or with the ticket the thread is currently working on behalf of */
        record->work_on_behalf_attr->ticket = get_thread_wob_ticket( current );"""),
("""static int add_msg_record( struct alpc_port *dst_port, struct alpc_port *src_port,""",
"""/* Windows counts a delivered message against the endpoint it is meant for; client requests
 * are queued on the connection port but counted on the server communication port */
static struct alpc_port *get_seq_owner( struct alpc_port *dst_port, struct alpc_port *src_port,
                                        unsigned short msg_type )
{
    if (dst_port->type == CONNECTION_PORT && msg_type != ALPC_MESSAGE_TYPE_CONNECTION_REQUEST &&
        src_port && src_port != dst_port && src_port->type == COMMUNICATION_PORT && src_port->peer_port)
        return src_port->peer_port;
    return dst_port;
}

static int add_msg_record( struct alpc_port *dst_port, struct alpc_port *src_port,"""),
("""    list_add_tail( &dst_port->msg_list, &entry->entry );
    wake_up( &dst_port->obj, 1 );
    return 1;""",
"""    list_add_tail( &dst_port->msg_list, &entry->entry );
    get_seq_owner( dst_port, src_port, msg_type )->seq_no++;
    wake_up( &dst_port->obj, 1 );
    return 1;"""),
("""            port->port_context = 0;
            port->port_attr = *port_attr;""",
"""            port->port_context = 0;
            port->port_attr = *port_attr;
            port->seq_no = 0;
            port->server_side = 0;
            port->accept_context = 0;
            port->client_handle = 0;"""),
("""    reply->handle = alloc_handle( current->process, communication_port, ALPC_PORT_ALL_ACCESS, objattr->attributes );
    if (reply->handle && !add_msg_record( connection_port, communication_port, ALPC_MESSAGE_TYPE_CONNECTION_REQUEST,""",
"""    reply->handle = alloc_handle( current->process, communication_port, ALPC_PORT_ALL_ACCESS, objattr->attributes );
    communication_port->client_handle = reply->handle;
    if (reply->handle && !add_msg_record( connection_port, communication_port, ALPC_MESSAGE_TYPE_CONNECTION_REQUEST,"""),
("""            dst_port->port_context = req->port_context;
            msg_id_entries[msg->id - ALPC_MSG_ID_OFFSET].completed_receiver = connection_port;""",
"""            dst_port->port_context = req->port_context;
            communication_port->server_side = 1;
            communication_port->accept_context = req->port_context;
            msg_id_entries[msg->id - ALPC_MSG_ID_OFFSET].completed_receiver = connection_port;"""),
("""/* Send a message and attributes to an ALPC object */
DECL_HANDLER(alpc_send_port)""",
"""/* Query basic information about an ALPC port */
DECL_HANDLER(alpc_query_port)
{
    struct alpc_port *port = get_alpc_port_obj( current->process, req->handle, ALPC_PORT_QUERY_STATE );

    if (!port) return;
    reply->seq_no = port->seq_no;
    if (port->type == CONNECTION_PORT)
    {
        /* Windows always reports connection ports as accepting LPC requests */
        reply->flags = port->port_attr.flags | ALPC_PORTFLG_ALLOW_LPC_REQUESTS;
        reply->port_context = 0;
    }
    else
    {
        reply->flags = 0;
        reply->port_context = port->server_side ? port->accept_context : port->client_handle;
    }
    release_object( port );
}

/* Send a message and attributes to an ALPC object */
DECL_HANDLER(alpc_send_port)"""),
])

patch("include/winternl.h", [
("""NTSYSAPI NTSTATUS  WINAPI NtAlpcSendWaitReceivePort(""",
"""NTSYSAPI NTSTATUS  WINAPI NtAlpcQueryInformation(HANDLE,ULONG,void*,ULONG,ULONG*);
NTSYSAPI NTSTATUS  WINAPI NtAlpcSendWaitReceivePort("""),
("""NTSYSAPI DWORD     WINAPI RtlGetThreadErrorMode(void);""",
"""NTSYSAPI DWORD     WINAPI RtlGetThreadErrorMode(void);
NTSYSAPI NTSTATUS  WINAPI RtlGetThreadWorkOnBehalfTicket(void*,ULONG);"""),
("""NTSYSAPI NTSTATUS  WINAPI RtlSetThreadErrorMode(DWORD,LPDWORD);""",
"""NTSYSAPI NTSTATUS  WINAPI RtlSetThreadErrorMode(DWORD,LPDWORD);
NTSYSAPI NTSTATUS  WINAPI RtlSetThreadWorkOnBehalfTicket(const void*);"""),
])

patch("dlls/ntdll/ntdll.spec", [
("""@ stdcall -arch=win64 -syscall NtAlpcImpersonateClientOfPort(long ptr ptr)
""",
"""@ stdcall -arch=win64 -syscall NtAlpcImpersonateClientOfPort(long ptr ptr)
@ stdcall -arch=win64 -syscall NtAlpcQueryInformation(long long ptr long ptr)
"""),
("""@ stdcall RtlGetThreadErrorMode()
""",
"""@ stdcall RtlGetThreadErrorMode()
@ stdcall RtlGetThreadWorkOnBehalfTicket(ptr long)
"""),
("""@ stdcall RtlSetThreadErrorMode(long ptr)
""",
"""@ stdcall RtlSetThreadErrorMode(long ptr)
@ stdcall RtlSetThreadWorkOnBehalfTicket(ptr)
"""),
])

patch("dlls/ntdll/thread.c", [
("""DWORD WINAPI RtlGetThreadErrorMode( void )
{
    return NtCurrentTeb()->HardErrorMode;
}
""",
"""DWORD WINAPI RtlGetThreadErrorMode( void )
{
    return NtCurrentTeb()->HardErrorMode;
}


/***********************************************************************
 *              RtlGetThreadWorkOnBehalfTicket  (NTDLL.@)
 *
 * Returns the ticket set by RtlSetThreadWorkOnBehalfTicket, or zero.
 */
NTSTATUS WINAPI RtlGetThreadWorkOnBehalfTicket( void *ticket, ULONG flags )
{
    TRACE( "%p, %#lx\\n", ticket, flags );

    if (flags > 2) return STATUS_INVALID_PARAMETER_2;
    memcpy( ticket, NtCurrentTeb()->WorkingOnBehalfOfTicket, sizeof(NtCurrentTeb()->WorkingOnBehalfOfTicket) );
    return STATUS_SUCCESS;
}


/***********************************************************************
 *              RtlSetThreadWorkOnBehalfTicket  (NTDLL.@)
 *
 * Makes the current thread work on behalf of the thread owning the ticket; zero clears it.
 */
NTSTATUS WINAPI RtlSetThreadWorkOnBehalfTicket( const void *ticket )
{
    UCHAR value[sizeof(NtCurrentTeb()->WorkingOnBehalfOfTicket)];
    NTSTATUS status;

    memcpy( value, ticket, sizeof(value) );
    TRACE( "%p (%I64x)\\n", ticket, *(ULONG64 *)value );

    status = NtSetInformationThread( NtCurrentThread(), ThreadWorkOnBehalfTicket, value, sizeof(value) );
    if (!status) memcpy( NtCurrentTeb()->WorkingOnBehalfOfTicket, value, sizeof(value) );
    return status;
}
"""),
])

patch("dlls/ntdll/unix/thread.c", [
("""    case ThreadQuerySetWin32StartAddress:
    {
        const PRTL_THREAD_START_ROUTINE *entry = data;""",
"""    case ThreadWorkOnBehalfTicket:
    {
        ULONG64 ticket;

        if (length != sizeof(ticket))
        {
            THREAD_BASIC_INFORMATION info;

            if (handle != NtCurrentThread() &&
                !NtQueryInformationThread( handle, ThreadBasicInformation, &info, sizeof(info), NULL ) &&
                info.ClientId.UniqueThread != NtCurrentTeb()->ClientId.UniqueThread)
                return STATUS_INVALID_PARAMETER;
            return STATUS_INFO_LENGTH_MISMATCH;
        }
        memcpy( &ticket, data, sizeof(ticket) );
        SERVER_START_REQ( set_thread_info )
        {
            req->handle     = wine_server_obj_handle( handle );
            req->mask       = SET_THREAD_INFO_WOB_TICKET;
            req->wob_ticket = ticket;
            status = wine_server_call( req );
        }
        SERVER_END_REQ;
        return status;
    }

    case ThreadQuerySetWin32StartAddress:
    {
        const PRTL_THREAD_START_ROUTINE *entry = data;"""),
])

patch("dlls/ntdll/unix/alpc.c", [
("""NTSTATUS WINAPI NtAlpcDisconnectPort( HANDLE port_handle, ULONG flags )
{""",
"""/***********************************************************************
 *             NtAlpcQueryInformation  (NTDLL.@)
 */
NTSTATUS WINAPI NtAlpcQueryInformation( HANDLE port_handle, ULONG info_class, void *buffer,
                                        ULONG length, ULONG *ret_len )
{
    struct
    {
        ULONG Flags;
        ULONG SequenceNo;
        void *PortContext;
    } info;
    NTSTATUS status;

    TRACE( "%p, %u, %p, %u, %p.\\n", port_handle, (unsigned int)info_class, buffer, (unsigned int)length, ret_len );

    if (info_class != 0 /* AlpcBasicInformation */)
    {
        FIXME( "unsupported information class %u\\n", (unsigned int)info_class );
        return STATUS_INVALID_INFO_CLASS;
    }

    SERVER_START_REQ( alpc_query_port )
    {
        req->handle = wine_server_obj_handle( port_handle );
        if (!(status = wine_server_call( req )))
        {
            info.Flags       = reply->flags;
            info.SequenceNo  = reply->seq_no;
            info.PortContext = wine_server_get_ptr( reply->port_context );
        }
    }
    SERVER_END_REQ;
    if (status) return status;

    if (ret_len) *ret_len = sizeof(info);
    if (length < sizeof(info)) return STATUS_INFO_LENGTH_MISMATCH;
    memcpy( buffer, &info, sizeof(info) );
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI NtAlpcDisconnectPort( HANDLE port_handle, ULONG flags )
{"""),
])