/* ALPC attribute layouts from Wine eba89375, include/winternl.h (LGPL-2.1+). */
#define ALPC_MESSAGE_SECURITY_ATTRIBUTE       0x80000000
#define ALPC_MESSAGE_VIEW_ATTRIBUTE           0x40000000
#define ALPC_MESSAGE_CONTEXT_ATTRIBUTE        0x20000000
#define ALPC_MESSAGE_HANDLE_ATTRIBUTE         0x10000000
#define ALPC_MESSAGE_TOKEN_ATTRIBUTE          0x08000000
#define ALPC_MESSAGE_DIRECT_ATTRIBUTE         0x04000000
#define ALPC_MESSAGE_WORK_ON_BEHALF_ATTRIBUTE  0x02000000
#define ALPC_MESSAGE_ATTRIBUTE_ALL            0xfe000000

typedef struct _ALPC_MESSAGE_ATTRIBUTES
{
    ULONG AllocatedAttributes;
    ULONG ValidAttributes;
} ALPC_MESSAGE_ATTRIBUTES;

typedef struct _ALPC_SECURITY_ATTR
{
    ULONG Flags;
    SECURITY_QUALITY_OF_SERVICE *QoS;
    HANDLE ContextHandle;
} ALPC_SECURITY_ATTR;

typedef struct _ALPC_VIEW_ATTR
{
    ULONG Flags;
    HANDLE SectionHandle;
    void *ViewBase;
    SIZE_T ViewSize;
} ALPC_VIEW_ATTR;

typedef struct _ALPC_CONTEXT_ATTR
{
    void *PortContext;
    void *MessageContext;
    ULONG Sequence;
    ULONG MessageId;
    ULONG CallbackId;
} ALPC_CONTEXT_ATTR;

typedef struct _ALPC_HANDLE_ATTR
{
    ULONG Flags;
    HANDLE Handle;
    ULONG ObjectType;
    ACCESS_MASK DesiredAccess;
} ALPC_HANDLE_ATTR;

typedef struct _ALPC_TOKEN_ATTR
{
    ULONGLONG TokenId;
    ULONGLONG AuthenticationId;
    ULONGLONG ModifiedId;
} ALPC_TOKEN_ATTR;

typedef struct _ALPC_DIRECT_ATTR { HANDLE Event; } ALPC_DIRECT_ATTR;
typedef struct _ALPC_WORK_ON_BEHALF_ATTR { ULONGLONG Ticket; } ALPC_WORK_ON_BEHALF_ATTR;
