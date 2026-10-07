#!/usr/bin/env python3
"""Compact MIDL proxy/stub tables + NdrStubCall3 for Wine rpcrt4 (idempotent, .pre-ndr3 backups).

Native semantics measured from genuine x64 combase 10.0.26100.9278 / rpcrt4 10.0.26100.9444:
  NdrpFindInterface            pIIDLookupRtn == -1 -> search proxy entries by piid
  NdrpSelectProxyVtbl          proxy entry {info, piid, tag}: -1 -> g_StublessClientIInspectableVtbl,
                               -2 -> g_StublessClientVtbl
  NdrpTryResolveBuiltInStubVtableFromTag  stub entry {header, tag}: -1 CStdStubBufferVtbl,
                               -2 CStdStubBuffer2Vtbl, -3 CStdAsyncStubBufferVtbl, -4 CStdAsyncStubBuffer2Vtbl
  CStdStubBuffer_Invoke        pDispatchTable -1/-2: ProcNum <3 or >=count -> raise 0x6d1;
                               3..5 -> base stub Invoke; >=6 -> NdrStubCall2 (-1) / NdrStubCall3 (-2)
  rpcrt4 NdrStubCall3          NULL/DCE transfer syntax -> NdrStubCall2; NDR64 absent -> 0x800706C2
"""
import os, shutil, sys

W = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else
    '~/xodus-runs/xbox-service-principal-20261007-001/source/wine')
R = os.path.join(W, 'dlls/rpcrt4')
MARK = 'ndr3-compact'

def patch(path, edits, mark=MARK):
    path = os.path.join(R, path)
    src = open(path).read()
    if mark in src:
        print('already patched', path); return
    if not os.path.exists(path + '.pre-ndr3'):
        shutil.copy2(path, path + '.pre-ndr3')
    for old, new in edits:
        if src.count(old) != 1:
            sys.exit('anchor count %d in %s: %r' % (src.count(old), path, old[:80]))
        src = src.replace(old, new)
    open(path, 'w').write(src)
    print('patched', path)

# ---------------------------------------------------------------- rpcrt4.spec
patch('rpcrt4.spec', [(
'@ stdcall NdrStubCall2(ptr ptr ptr ptr)\n',
'@ stdcall NdrStubCall2(ptr ptr ptr ptr)\n'
'@ stdcall -arch=win64 NdrStubCall3(ptr ptr ptr ptr) # ndr3-compact\n')])

# ---------------------------------------------------------------- cpsf.h
patch('cpsf.h', [(
'BOOL fill_stubless_table(IUnknownVtbl *vtbl, DWORD num);\n',
'BOOL fill_stubless_table(IUnknownVtbl *vtbl, DWORD num);\n'
'/* ndr3-compact */\n'
'HRESULT StdProxy_ConstructFromVtbl(REFIID riid, LPUNKNOWN pUnkOuter, CInterfaceProxyVtbl *vtbl,\n'
'                                   PCInterfaceName name, const IID *delegated_iid,\n'
'                                   LPPSFACTORYBUFFER pPSFactory, LPRPCPROXYBUFFER *ppProxy,\n'
'                                   LPVOID *ppvObj);\n'
'#ifdef _WIN64\n'
'LONG WINAPI NdrStubCall3(struct IRpcStubBuffer *This, struct IRpcChannelBuffer *channel,\n'
'                         PRPC_MESSAGE msg, DWORD *phase);\n'
'#endif\n')])

# ---------------------------------------------------------------- cproxy.c
patch('cproxy.c', [
('''    fill_stubless_table( (IUnknownVtbl *)vtbl->Vtbl, count );
  }

  if (!IsEqualGUID(vtbl->header.piid, riid)) {''',
'''    fill_stubless_table( (IUnknownVtbl *)vtbl->Vtbl, count );
  }

  return StdProxy_ConstructFromVtbl( riid, pUnkOuter, vtbl, name,
                                     ProxyInfo->pDelegatedIIDs ? ProxyInfo->pDelegatedIIDs[Index] : NULL,
                                     pPSFactory, ppProxy, ppvObj );
}

/* ndr3-compact: shared by the classic and the compact (synthesized vtable) table formats */
HRESULT StdProxy_ConstructFromVtbl(REFIID riid, LPUNKNOWN pUnkOuter, CInterfaceProxyVtbl *vtbl,
                                   PCInterfaceName name, const IID *delegated_iid,
                                   LPPSFACTORYBUFFER pPSFactory, LPRPCPROXYBUFFER *ppProxy,
                                   LPVOID *ppvObj)
{
  StdProxyImpl *This;

  if (!IsEqualGUID(vtbl->header.piid, riid)) {'''),
('''{
  StdProxyImpl *This;
  PCInterfaceName name = ProxyInfo->pNamesArray[Index];''',
'''{
  PCInterfaceName name = ProxyInfo->pNamesArray[Index];'''),
('''  if(ProxyInfo->pDelegatedIIDs && ProxyInfo->pDelegatedIIDs[Index])
  {
      HRESULT r = create_proxy( ProxyInfo->pDelegatedIIDs[Index], NULL,''',
'''  if (delegated_iid)
  {
      HRESULT r = create_proxy( delegated_iid, NULL,'''),
])

# ---------------------------------------------------------------- cstub.c
patch('cstub.c', [
('''HRESULT WINAPI CStdStubBuffer_Invoke(LPRPCSTUBBUFFER iface,''',
'''static HRESULT WINAPI CStdStubBuffer_Delegating_Connect(LPRPCSTUBBUFFER iface, LPUNKNOWN lpUnkServer);

HRESULT WINAPI CStdStubBuffer_Invoke(LPRPCSTUBBUFFER iface,'''),
('''    if (header->pDispatchTable)
      header->pDispatchTable[pMsg->iMethod](iface, pChannel, (PRPC_MESSAGE)pMsg, &dwPhase);''',
'''    if ((LONG_PTR)header->pDispatchTable == -1 || (LONG_PTR)header->pDispatchTable == -2)
    {
      /* ndr3-compact: interpreted IInspectable-derived interface; methods 3-5 belong to the
       * delegated base stub, the rest are interpreted (-2 may carry NDR64 syntax info). */
      if (pMsg->iMethod < 3 || pMsg->iMethod >= header->DispatchTableCount)
        RpcRaiseException(RPC_S_PROCNUM_OUT_OF_RANGE);
      if (pMsg->iMethod < 6)
      {
        if (iface->lpVtbl->Connect != CStdStubBuffer_Delegating_Connect)
          RpcRaiseException(RPC_E_UNEXPECTED);
        NdrStubForwardingFunction(iface, pChannel, (PRPC_MESSAGE)pMsg, &dwPhase);
      }
#ifdef _WIN64
      else if ((LONG_PTR)header->pDispatchTable == -2)
        hr = NdrStubCall3(iface, pChannel, (PRPC_MESSAGE)pMsg, &dwPhase);
#endif
      else
        NdrStubCall2(iface, pChannel, (PRPC_MESSAGE)pMsg, &dwPhase);
    }
    else if (header->pDispatchTable)
      header->pDispatchTable[pMsg->iMethod](iface, pChannel, (PRPC_MESSAGE)pMsg, &dwPhase);'''),
])

# ---------------------------------------------------------------- ndr_stubless.c
patch('ndr_stubless.c', [(
'''#ifdef __aarch64__
__ASM_GLOBAL_FUNC( NdrClientCall3,''',
'''#ifdef _WIN64
/***********************************************************************
 *           NdrStubCall3 [RPCRT4.@]   (ndr3-compact)
 */
LONG WINAPI NdrStubCall3( struct IRpcStubBuffer *This, struct IRpcChannelBuffer *channel,
                          PRPC_MESSAGE msg, DWORD *phase )
{
    static const GUID ndr64_syntax_guid =
        {0x71710533, 0xbeba, 0x4937, {0x83, 0x19, 0xb5, 0xdb, 0xef, 0x9c, 0xcc, 0x36}};
    const RPC_SYNTAX_IDENTIFIER *syntax = msg->TransferSyntax;
    const MIDL_SERVER_INFO *info;
    ULONG_PTR i;

    TRACE( "This %p, channel %p, msg %p, phase %p\\n", This, channel, msg, phase );

    if (!syntax || IsEqualGUID( &syntax->SyntaxGUID, &ndr_syntax_id.SyntaxGUID ))
        return NdrStubCall2( This, channel, msg, phase );

    if (This) info = CStdStubBuffer_GetServerInfo( This );
    else info = ((RPC_SERVER_INTERFACE *)msg->RpcInterfaceInformation)->InterpreterInfo;

    for (i = 0; i < info->nCount; i++)
    {
        if (IsEqualGUID( &info->pSyntaxInfo[i].TransferSyntax.SyntaxGUID, &ndr64_syntax_guid ))
        {
            FIXME( "NDR64 server stubs are not supported\\n" );
            break;
        }
    }
    return HRESULT_FROM_WIN32( RPC_S_UNSUPPORTED_TRANS_SYN );
}
#endif

#ifdef __aarch64__
__ASM_GLOBAL_FUNC( NdrClientCall3,''')])

# ---------------------------------------------------------------- cpsf.c
COMPACT = r'''
/* ndr3-compact: compact tables emitted by recent MIDL versions (e.g. Windows
 * OneCoreUAPCommonProxyStub.dll).  pIIDLookupRtn is -1 and IIDs are found in the proxy
 * entries; proxy entries are { pStublessProxyInfo, piid, tag } and stub entries are
 * { CInterfaceStubHeader, tag }.  Native combase maps the tags to built-in vtables
 * (NdrpSelectProxyVtbl, NdrpTryResolveBuiltInStubVtableFromTag); the entries themselves
 * are read-only and carry no vtables, so equivalent tables are synthesized here. */

struct compact_proxy_entry
{
    const void *stubless_info;
    const IID *piid;
    LONG_PTR tag;   /* -1: IInspectable methods forwarded to the base proxy, -2: all stubless */
};

struct compact_stub_entry
{
    CInterfaceStubHeader header;
    LONG_PTR tag;   /* -1: standard, -2: delegating, -3/-4: async variants */
};

struct compact_stub
{
    struct compact_stub *next;
    const struct compact_stub_entry *entry;
    CInterfaceStubVtbl vtbl;
};

struct compact_proxy
{
    struct compact_proxy *next;
    const struct compact_proxy_entry *entry;
    const void *stubless_info;     /* vtbl[-2] for the stubless thunks */
    CInterfaceProxyVtbl vtbl;      /* { piid, methods... } */
};

static const IID iid_inspectable =
    {0xaf86e2e0, 0xb12d, 0x4c6a, {0x9c, 0x5a, 0xd7, 0xaa, 0x65, 0x10, 0x1e, 0x90}};

static SRWLOCK compact_lock = SRWLOCK_INIT;
static struct compact_stub *compact_stubs;
static struct compact_proxy *compact_proxies;

static BOOL is_compact_file( const ProxyFileInfo *info )
{
    return info->TableVersion >= 1 && (LONG_PTR)info->pIIDLookupRtn == -1;
}

static BOOL is_compact_stub_entry( const void *entry )
{
    LONG_PTR tag = ((const struct compact_stub_entry *)entry)->tag;
    return tag >= -4 && tag <= -1;
}

static BOOL is_compact_proxy_entry( const void *entry )
{
    LONG_PTR tag = ((const struct compact_proxy_entry *)entry)->tag;
    return tag == -1 || tag == -2;
}

static BOOL find_compact_iid( const ProxyFileInfo *info, REFIID riid, int *index )
{
    unsigned int i;

    for (i = 0; i < info->TableSize; i++)
    {
        const struct compact_proxy_entry *entry = (const void *)info->pProxyVtblList[i];
        if (IsEqualGUID( entry->piid, riid ))
        {
            *index = i;
            return TRUE;
        }
    }
    return FALSE;
}

static const IID *compact_delegated_iid( const ProxyFileInfo *info, int index )
{
    if (info->pDelegatedIIDs && info->pDelegatedIIDs[index]) return info->pDelegatedIIDs[index];
    return &iid_inspectable;
}

static ULONG WINAPI compact_stub_Release( IRpcStubBuffer *iface )
{
    return NdrCStdStubBuffer_Release( iface, ((CStdStubBuffer *)iface)->pPSFactory );
}

static ULONG WINAPI compact_stub2_Release( IRpcStubBuffer *iface )
{
    return NdrCStdStubBuffer2_Release( iface, ((CStdStubBuffer *)iface)->pPSFactory );
}

static CInterfaceStubVtbl *get_compact_stub_vtbl( const struct compact_stub_entry *entry )
{
    struct compact_stub *stub;

    AcquireSRWLockExclusive( &compact_lock );
    for (stub = compact_stubs; stub; stub = stub->next)
        if (stub->entry == entry) break;
    if (!stub && (stub = calloc( 1, sizeof(*stub) )))
    {
        stub->entry = entry;
        stub->vtbl.header = entry->header;
        if (entry->tag == -2)
        {
            stub->vtbl.Vtbl = CStdStubBuffer_Delegating_Vtbl;
            stub->vtbl.Vtbl.Release = compact_stub2_Release;
        }
        else
        {
            stub->vtbl.Vtbl = CStdStubBuffer_Vtbl;
            stub->vtbl.Vtbl.Release = compact_stub_Release;
        }
        stub->next = compact_stubs;
        compact_stubs = stub;
    }
    ReleaseSRWLockExclusive( &compact_lock );
    return stub ? &stub->vtbl : NULL;
}

static CInterfaceProxyVtbl *get_compact_proxy_vtbl( const struct compact_proxy_entry *entry, ULONG count )
{
    struct compact_proxy *proxy;
    IUnknownVtbl *methods;
    ULONG i;

    if (count < 3 || count >= NB_THUNK_ENTRIES)
    {
        FIXME( "%lu methods not supported\n", count );
        return NULL;
    }

    AcquireSRWLockExclusive( &compact_lock );
    for (proxy = compact_proxies; proxy; proxy = proxy->next)
        if (proxy->entry == entry) break;
    if (!proxy && (proxy = calloc( 1, offsetof( struct compact_proxy, vtbl.Vtbl[count] ) )))
    {
        proxy->entry = entry;
        proxy->stubless_info = entry->stubless_info;
        proxy->vtbl.header.piid = entry->piid;
        methods = (IUnknownVtbl *)proxy->vtbl.Vtbl;
        for (i = 3; i < count; i++) proxy->vtbl.Vtbl[i] = (void *)-1;
        if (entry->tag == -1)
        {
            for (i = 3; i < 6 && i < count; i++) proxy->vtbl.Vtbl[i] = NULL;
            fill_delegated_proxy_table( methods, count );
        }
        else
        {
            methods->QueryInterface = IUnknown_QueryInterface_Proxy;
            methods->AddRef = IUnknown_AddRef_Proxy;
            methods->Release = IUnknown_Release_Proxy;
        }
        fill_stubless_table( methods, count );
        proxy->next = compact_proxies;
        compact_proxies = proxy;
    }
    ReleaseSRWLockExclusive( &compact_lock );
    return proxy ? &proxy->vtbl : NULL;
}

static HRESULT create_compact_proxy( const ProxyFileInfo *info, int index, LPUNKNOWN outer, REFIID riid,
                                     LPPSFACTORYBUFFER factory, LPRPCPROXYBUFFER *proxy, LPVOID *obj )
{
    const struct compact_proxy_entry *entry = (const void *)info->pProxyVtblList[index];
    const struct compact_stub_entry *stub = (const void *)info->pStubVtblList[index];
    CInterfaceProxyVtbl *vtbl;

    TRACE( "compact proxy %s tag %Id methods %lu\n", debugstr_guid(entry->piid), entry->tag,
           stub->header.DispatchTableCount );
    if (!(vtbl = get_compact_proxy_vtbl( entry, stub->header.DispatchTableCount ))) return E_OUTOFMEMORY;
    return StdProxy_ConstructFromVtbl( riid, outer, vtbl, info->pNamesArray ? info->pNamesArray[index] : NULL,
                                       entry->tag == -1 ? compact_delegated_iid( info, index ) : NULL,
                                       factory, proxy, obj );
}

static HRESULT create_compact_stub( const ProxyFileInfo *info, int index, REFIID riid, LPUNKNOWN server,
                                    LPPSFACTORYBUFFER factory, LPRPCSTUBBUFFER *ppStub )
{
    const struct compact_stub_entry *entry = (const void *)info->pStubVtblList[index];
    PCInterfaceName name = info->pNamesArray ? info->pNamesArray[index] : NULL;
    CInterfaceStubVtbl *vtbl;

    TRACE( "compact stub %s tag %Id dispatch %p methods %lu\n", debugstr_guid(entry->header.piid),
           entry->tag, entry->header.pDispatchTable, entry->header.DispatchTableCount );
    if (entry->tag == -3 || entry->tag == -4)
    {
        FIXME( "async compact stub %s not supported\n", debugstr_guid(entry->header.piid) );
        return E_NOTIMPL;
    }
    if (!(vtbl = get_compact_stub_vtbl( entry ))) return E_OUTOFMEMORY;
    if (entry->tag == -2)
        return CStdStubBuffer_Delegating_Construct( riid, server, name, vtbl,
                                                    compact_delegated_iid( info, index ), factory, ppStub );
    return CStdStubBuffer_Construct( riid, server, name, vtbl, factory, ppStub );
}

static BOOL FindProxyInfo('''

patch('cpsf.c', [
('\nstatic BOOL FindProxyInfo(', COMPACT),
('''    if ((*pProxyFileList)->pIIDLookupRtn(riid, pIndex)) {''',
'''    if (is_compact_file(*pProxyFileList) ? find_compact_iid(*pProxyFileList, riid, pIndex)
                                          : (*pProxyFileList)->pIIDLookupRtn(riid, pIndex)) {'''),
('''    return E_NOINTERFACE;
  return StdProxy_Construct(riid, pUnkOuter, ProxyInfo, Index, iface, ppProxy, ppv);''',
'''    return E_NOINTERFACE;
  if (is_compact_proxy_entry(ProxyInfo->pProxyVtblList[Index]))
    return create_compact_proxy(ProxyInfo, Index, pUnkOuter, riid, iface, ppProxy, ppv);
  return StdProxy_Construct(riid, pUnkOuter, ProxyInfo, Index, iface, ppProxy, ppv);'''),
('''    return E_NOINTERFACE;

  if(ProxyInfo->pDelegatedIIDs && ProxyInfo->pDelegatedIIDs[Index])''',
'''    return E_NOINTERFACE;

  if (is_compact_stub_entry(ProxyInfo->pStubVtblList[Index]))
    return create_compact_stub(ProxyInfo, Index, riid, pUnkServer, iface, ppStub);

  if(ProxyInfo->pDelegatedIIDs && ProxyInfo->pDelegatedIIDs[Index])'''),
('''            void **pRpcStubVtbl = (void **)&stubs[j]->Vtbl;
''',
'''            void **pRpcStubVtbl = (void **)&stubs[j]->Vtbl;

            /* compact entries are read-only and have no vtables to fill */
            if (is_compact_stub_entry(stubs[j]) || is_compact_proxy_entry(proxies[j])) continue;
'''),
])
# Recent MIDL emits classic delegated proxy vtables pre-filled with imported
# IUnknown_*_Proxy / NdrProxyForwardingFunctionN / ObjectStublessClientN thunks in
# read-only .rdata; only fill the IUnknown slots that are actually empty.
patch('cstub.c', [(
'''    vtbl->QueryInterface = IUnknown_QueryInterface_Proxy;
    vtbl->AddRef = IUnknown_AddRef_Proxy;
    vtbl->Release = IUnknown_Release_Proxy;
    for (i = 0; i < num - 3; i++)''',
'''    /* ndr3-rofill: recent MIDL pre-fills these in read-only data */
    if (!vtbl->QueryInterface) vtbl->QueryInterface = IUnknown_QueryInterface_Proxy;
    if (!vtbl->AddRef) vtbl->AddRef = IUnknown_AddRef_Proxy;
    if (!vtbl->Release) vtbl->Release = IUnknown_Release_Proxy;
    for (i = 0; i < num - 3; i++)''')], mark='ndr3-rofill')
print('done')
