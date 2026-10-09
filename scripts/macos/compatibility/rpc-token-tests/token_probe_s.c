/* WIDL 11.0 stub with the measured FC_SYSTEM_HANDLE token descriptor. */
#include <string.h>

#include "token_probe.h"

#define TYPE_FORMAT_STRING_SIZE 9
#define PROC_FORMAT_STRING_SIZE 79

typedef struct _MIDL_TYPE_FORMAT_STRING
{
    short Pad;
    unsigned char Format[TYPE_FORMAT_STRING_SIZE];
} MIDL_TYPE_FORMAT_STRING;

typedef struct _MIDL_PROC_FORMAT_STRING
{
    short Pad;
    unsigned char Format[PROC_FORMAT_STRING_SIZE];
} MIDL_PROC_FORMAT_STRING;


static const MIDL_TYPE_FORMAT_STRING __MIDL_TypeFormatString;
static const MIDL_PROC_FORMAT_STRING __MIDL_ProcFormatString;

/*****************************************************************************
 * TokenProbe interface
 */

static RPC_DISPATCH_TABLE TokenProbe_v1_0_DispatchTable;
static const MIDL_SERVER_INFO TokenProbe_ServerInfo;

static const RPC_SERVER_INTERFACE TokenProbe___RpcServerInterface =
{
    sizeof(RPC_SERVER_INTERFACE),
    {{0x72b7b240,0xa0f0,0x4d30,{0x8d,0x6a,0x84,0xf0,0x8d,0xf4,0xdf,0x25}},{1,0}},
    {{0x8a885d04,0x1ceb,0x11c9,{0x9f,0xe8,0x08,0x00,0x2b,0x10,0x48,0x60}},{2,0}},
    &TokenProbe_v1_0_DispatchTable,
    0,
    0,
    0,
    &TokenProbe_ServerInfo,
    0,
};
RPC_IF_HANDLE TokenProbe_v1_0_s_ifspec = (RPC_IF_HANDLE)& TokenProbe___RpcServerInterface;

static const MIDL_STUB_DESC TokenProbe_StubDesc;

#if !defined(__RPC_WIN64__)
#error  Invalid build platform for this stub.
#endif

static const unsigned short TokenProbe_FormatStringOffsetTable[] =
{
    0,  /* CheckToken */
    42,  /* StopServer */
};

static const MIDL_STUB_DESC TokenProbe_StubDesc =
{
    (void *)& TokenProbe___RpcServerInterface,
    MIDL_user_allocate,
    MIDL_user_free,
    {
        0,
    },
    0,
    0,
    0,
    0,
    __MIDL_TypeFormatString.Format,
    1, /* -error bounds_check flag */
    0x50002, /* Ndr library version */
    0,
    0x50200ca, /* MIDL Version 5.2.202 */
    0,
    0,
    0,  /* notify & notify_flag routine table */
    1,  /* Flags */
    0,  /* Reserved3 */
    0,  /* Reserved4 */
    0   /* Reserved5 */
};

static RPC_DISPATCH_FUNCTION TokenProbe_table[] =
{
    NdrServerCall2,
    NdrServerCall2,
    0
};
static RPC_DISPATCH_TABLE TokenProbe_v1_0_DispatchTable =
{
    2,
    TokenProbe_table
};

static const SERVER_ROUTINE TokenProbe_ServerRoutineTable[] =
{
    (void *)CheckToken,
    (void *)StopServer,
};

static const MIDL_SERVER_INFO TokenProbe_ServerInfo =
{
    &TokenProbe_StubDesc,
    TokenProbe_ServerRoutineTable,
    __MIDL_ProcFormatString.Format,
    TokenProbe_FormatStringOffsetTable,
    0,
    0,
    0,
    0
};

static const MIDL_PROC_FORMAT_STRING __MIDL_ProcFormatString =
{
    0,
    {
/* 0 (procedure TokenProbe::CheckToken) */
        0x00,	/* explicit handle */
        0x48,
        NdrFcLong(0x0),
        NdrFcShort(0x0),	/* method 0 */
        NdrFcShort(0x18),	/* stack size = 24 */
        0x32,	/* FC_BIND_PRIMITIVE */
        0x00,
        NdrFcShort(0x0),	/* stack offset = 0 */
        NdrFcShort(0x10),	/* client buffer = 16 */
        NdrFcShort(0x8),	/* server buffer = 8 */
        0x46,
        0x02,	/* 2 params */
        0x0a,
        0x01,
        NdrFcShort(0x0),
        NdrFcShort(0x0),
        NdrFcShort(0x0),
        NdrFcShort(0x0),
/* 30 (parameter token) */
        NdrFcShort(0x8b),
        NdrFcShort(0x8),	/* stack offset = 8 */
        NdrFcShort(0x2),
/* 36 (return value) */
        NdrFcShort(0x70),	/* flags: out, return, base type */
        NdrFcShort(0x10),	/* stack offset = 16 */
        0x08,	/* FC_LONG */
        0x0,
/* 42 (procedure TokenProbe::StopServer) */
        0x00,	/* explicit handle */
        0x48,
        NdrFcLong(0x0),
        NdrFcShort(0x1),	/* method 1 */
        NdrFcShort(0x10),	/* stack size = 16 */
        0x32,	/* FC_BIND_PRIMITIVE */
        0x00,
        NdrFcShort(0x0),	/* stack offset = 0 */
        NdrFcShort(0x0),	/* client buffer = 0 */
        NdrFcShort(0x8),	/* server buffer = 8 */
        0x44,
        0x01,	/* 1 params */
        0x0a,
        0x01,
        NdrFcShort(0x0),
        NdrFcShort(0x0),
        NdrFcShort(0x0),
        NdrFcShort(0x0),
/* 72 (return value) */
        NdrFcShort(0x70),	/* flags: out, return, base type */
        NdrFcShort(0x8),	/* stack offset = 8 */
        0x08,	/* FC_LONG */
        0x0,
        0x0
    }
};

static const MIDL_TYPE_FORMAT_STRING __MIDL_TypeFormatString =
{
    0,
    {
        NdrFcShort(0x0),
        0x3c, 0x05, NdrFcLong(0x8),
        0x0
    }
};
