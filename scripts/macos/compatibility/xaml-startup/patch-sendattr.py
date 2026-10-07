import re,sys
root=sys.argv[1]
def sub(path, old, new, count=1):
    p=root+'/'+path; s=open(p).read()
    n=s.count(old)
    if n!=count: sys.exit(f"{path}: expected {count} got {n} for: {old[:70]!r}")
    s=s.replace(old,new); open(p,'w').write(s)

# ---- ntdll ----
N='dlls/ntdll/unix/alpc.c'
sub(N,'static NTSTATUS validate_attributes( const ALPC_MESSAGE_ATTRIBUTES *attr, BOOL sending )',
      'static NTSTATUS validate_attributes( const ALPC_MESSAGE_ATTRIBUTES *attr, BOOL sending, ULONG send_allowed )')
sub(N,'    if (sending && (attr->ValidAttributes & ~ALPC_MESSAGE_SECURITY_ATTRIBUTE))',
      '    if (sending && (attr->ValidAttributes & ~send_allowed))')
sub(N,'''    if ((status = validate_message( send_msg )) ||
        (status = validate_attributes( send_msg_attr, TRUE ))) return status;''',
      '''    if ((status = validate_message( send_msg )) ||
        (status = validate_attributes( send_msg_attr, TRUE, ALPC_MESSAGE_SECURITY_ATTRIBUTE ))) return status;''')
sub(N,'''    if ((status = validate_message( connect_msg )) ||
        (status = validate_attributes( send_msg_attr, TRUE )) ||
        (status = validate_attributes( recv_msg_attr, FALSE ))) return status;''',
      '''    if ((status = validate_message( connect_msg )) ||
        (status = validate_attributes( send_msg_attr, TRUE, ALPC_MESSAGE_SECURITY_ATTRIBUTE )) ||
        (status = validate_attributes( recv_msg_attr, FALSE, 0 ))) return status;''')
sub(N,'''    if ((status = validate_message( send_msg )) ||
        (status = validate_attributes( send_msg_attr, TRUE )) ||
        (status = validate_attributes( recv_msg_attr, FALSE ))) return status;''',
      '''    /* Windows accepts valid context and work-on-behalf attributes on send: the sender's message context
     * is returned with the reply, and the kernel supplies the work-on-behalf ticket */
    if ((status = validate_message( send_msg )) ||
        (status = validate_attributes( send_msg_attr, TRUE, ALPC_MESSAGE_SECURITY_ATTRIBUTE |
                                       ALPC_MESSAGE_CONTEXT_ATTRIBUTE | ALPC_MESSAGE_WORK_ON_BEHALF_ATTRIBUTE )) ||
        (status = validate_attributes( recv_msg_attr, FALSE, 0 ))) return status;''')
sub(N,'''        /* Should use source ALPC_MESSAGE_WORK_ON_BEHALF_ATTRIBUTE attribute and non-zero but don't
         * know how to use it. The receiving side always got zero somehow */
        dst->ticket = 0;''',
      '''        /* wineserver issues the ticket for the sending thread; like Windows, the caller's value is ignored */
        dst->ticket = 0;''')
sub(N,'''            req->msg_attr_size = wine_server_add_alpc_msg_attr(req, send_msg_attr, ALPC_MESSAGE_ATTRIBUTE_ALL, &server_msg_attr);''',
      '''            req->msg_attr_size = wine_server_add_alpc_msg_attr(req, send_msg_attr, ALPC_MESSAGE_ATTRIBUTE_ALL, &server_msg_attr);
            if (send_msg_attr && (send_msg_attr->ValidAttributes & ALPC_MESSAGE_CONTEXT_ATTRIBUTE))
            {
                const ALPC_CONTEXT_ATTR *context = alpc_get_message_attribute( send_msg_attr,
                                                                               ALPC_MESSAGE_CONTEXT_ATTRIBUTE );
                req->msg_context_valid = 1;
                req->msg_context = wine_server_client_ptr( context->MessageContext );
            }''')

# ---- protocol ----
P='server/protocol.def'
sub(P,'''@REQ(alpc_send_port)
    obj_handle_t  handle;                     /* ALPC port handle */
    unsigned int  flags;                      /* flags */
    unsigned int  msg_size;                   /* size of the message to be sent, including message data */
    unsigned int  msg_attr_size;              /* size of the message attributes to be sent */''',
      '''@REQ(alpc_send_port)
    obj_handle_t  handle;                     /* ALPC port handle */
    unsigned int  flags;                      /* flags */
    unsigned int  msg_size;                   /* size of the message to be sent, including message data */
    unsigned int  msg_attr_size;              /* size of the message attributes to be sent */
    unsigned int  msg_context_valid;          /* sender supplied a context attribute */
    client_ptr_t  msg_context;                /* sender message context, returned with the reply */''')

# ---- server ----
S='server/alpc.c'
sub(S,'''    struct alpc_port *completed_receiver;
    unsigned int next;                           /* next free entry */''',
      '''    struct alpc_port *completed_receiver;
    client_ptr_t msg_context;                    /* sender message context returned with the reply */
    int msg_context_valid;
    unsigned int next;                           /* next free entry */''')
sub(S,'''    entry->security_token = NULL;
    entry->completed_receiver = NULL;
    return id;''','''    entry->security_token = NULL;
    entry->completed_receiver = NULL;
    entry->msg_context = 0;
    entry->msg_context_valid = 0;
    return id;''')
sub(S,'''    entry->src_port = NULL;
    entry->dst_port = NULL;
    entry->delivered = 0;
    entry->next = 0;''','''    entry->src_port = NULL;
    entry->dst_port = NULL;
    entry->delivered = 0;
    entry->msg_context = 0;
    entry->msg_context_valid = 0;
    entry->next = 0;''')
sub(S,'''static int add_msg_attr_to_record( struct alpc_msg_entry *record, const struct alpc_port *port,
                                   const struct alpc_msg_attr_t *msg_attr, unsigned int id,
                                   int add_port_context, client_ptr_t port_context )''',
      '''static int add_msg_attr_to_record( struct alpc_msg_entry *record, const struct alpc_port *port,
                                   const struct alpc_msg_attr_t *msg_attr, unsigned int id,
                                   int add_port_context, client_ptr_t port_context, client_ptr_t msg_context )''')
sub(S,'''        record->context_attr->port_context = port_context;
        record->context_attr->msg_id = id;''','''        record->context_attr->port_context = port_context;
        record->context_attr->msg_context = msg_context;
        record->context_attr->msg_id = id;''')
sub(S,'''        const struct alpc_work_on_behalf_attr_t *src_attr = wine_server_alpc_get_message_attribute( msg_attr, ALPC_MESSAGE_WORK_ON_BEHALF_ATTRIBUTE );

        if (!(record->work_on_behalf_attr = malloc( sizeof(*record->work_on_behalf_attr))) )
            goto failed;

        memcpy( record->work_on_behalf_attr, src_attr, sizeof(*record->work_on_behalf_attr) );''',
      '''        if (!(record->work_on_behalf_attr = malloc( sizeof(*record->work_on_behalf_attr))) )
            goto failed;

        /* Windows replaces the sender's value with a kernel-issued ticket for the sending thread;
         * the ticket is opaque, wineserver derives it from the thread id and creation time */
        record->work_on_behalf_attr->ticket = current->id |
            ((unsigned __int64)(unsigned int)current->creation_time << 32);''')
sub(S,'''                           data_size_t msg_size, const struct alpc_msg_attr_t *msg_attr,
                           int add_port_context, client_ptr_t port_context )''',
      '''                           data_size_t msg_size, const struct alpc_msg_attr_t *msg_attr,
                           int add_port_context, client_ptr_t port_context, client_ptr_t msg_context,
                           const client_ptr_t *sender_msg_context )''')
sub(S,'''        !(id = alloc_msg_id( src_port, dst_port, msg_type )))
        return 0;''','''        !(id = alloc_msg_id( src_port, dst_port, msg_type )))
        return 0;
    if (id && sender_msg_context)
    {
        msg_id_entries[id - ALPC_MSG_ID_OFFSET].msg_context = *sender_msg_context;
        msg_id_entries[id - ALPC_MSG_ID_OFFSET].msg_context_valid = 1;
    }''')
sub(S,'''    if (!add_msg_attr_to_record( entry, dst_port, msg_attr, id, add_port_context, port_context ))''',
      '''    if (!add_msg_attr_to_record( entry, dst_port, msg_attr, id, add_port_context, port_context, msg_context ))''')
sub(S,'''static int validate_request( data_size_t prefix, data_size_t msg_size, data_size_t attr_size )''',
      '''static int validate_request( data_size_t prefix, data_size_t msg_size, data_size_t attr_size,
                             unsigned int allowed )''')
sub(S,'''        if (attr->attributes & ~ALPC_MESSAGE_SECURITY_ATTRIBUTE)
        { set_error( STATUS_NOT_SUPPORTED ); return 0; }''','''        if (attr->attributes & ~allowed)
        { set_error( STATUS_NOT_SUPPORTED ); return 0; }''')
sub(S,'''    if (!validate_request( 0, req->msg_size, req->msg_attr_size )) return;''',
      '''    if (!validate_request( 0, req->msg_size, req->msg_attr_size,
                           ALPC_MESSAGE_SECURITY_ATTRIBUTE | ALPC_MESSAGE_WORK_ON_BEHALF_ATTRIBUTE )) return;''')
sub(S,'''    if (add_msg_record(dst_port, port, msg_type, msg, req->msg_size, msg_attr, 1, port->port_context))
        free_msg_id(msg->id);''','''    if (msg->id >= ALPC_MSG_ID_OFFSET && msg->id - ALPC_MSG_ID_OFFSET < used_msg_id_entries &&
        msg_id_entries[msg->id - ALPC_MSG_ID_OFFSET].msg_context_valid)
        reply_msg_context = msg_id_entries[msg->id - ALPC_MSG_ID_OFFSET].msg_context;

    if (add_msg_record(dst_port, port, msg_type, msg, req->msg_size, msg_attr, 1, port->port_context,
                       reply_msg_context, req->msg_context_valid ? &req->msg_context : NULL))
        free_msg_id(msg->id);''')
sub(S,'''    const struct alpc_msg_attr_t *msg_attr = NULL;
    const unsigned char *data_ptr = get_req_data();
    unsigned short msg_type;''','''    const struct alpc_msg_attr_t *msg_attr = NULL;
    const unsigned char *data_ptr = get_req_data();
    client_ptr_t reply_msg_context = 0;
    unsigned short msg_type;''')
s=open(root+'/'+S).read()
# remaining connect/accept validate_request calls and add_msg_record calls
s2=re.sub(r'(validate_request\( req->connection_attr_size \+ req->server_sd_size \+ req->name_size \+[^;]*?)\)\)',
          lambda m: m.group(1)+', ALPC_MESSAGE_SECURITY_ATTRIBUTE ))', s, count=1)
s2=re.sub(r'(validate_request\( req->obj_attr_size \+ sizeof\(struct alpc_port_attr_t\),[^;]*?)\)\)',
          lambda m: m.group(1)+', ALPC_MESSAGE_SECURITY_ATTRIBUTE ))', s2, count=1)
open(root+'/'+S,'w').write(s2)
print("ok")
