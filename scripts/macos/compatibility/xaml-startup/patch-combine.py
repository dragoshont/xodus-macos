#!/usr/bin/env python3
"""Narrow backport of upstream Wine 949c1ed5f4f97bae035892110c832b042ba994d4
("iertutil: Add PrivateCoInternetCombineIUri()", 2026-04-01).

Upstream first moved the IUri implementation from urlmon to iertutil
(f337b3f8, 4245a00c, ...).  This tree still has combine_uri()/Uri objects in
urlmon, so the upstream function body is added verbatim next to combine_uri()
in urlmon/uri.c (private export) and iertutil forwards to it."""
import os, shutil, sys

root = sys.argv[1] if len(sys.argv) > 1 else "."

def edit(rel, old, new):
    p = os.path.join(root, rel); t = open(p).read()
    if new in t: return
    if old not in t: sys.exit("anchor missing " + rel)
    if not os.path.exists(p + ".pre-combine"): shutil.copy2(p, p + ".pre-combine")
    open(p, "w").write(t.replace(old, new, 1)); print("patched", rel)

BODY = '''/***********************************************************************
 *           PrivateCoInternetCombineIUri (urlmon.@)
 *
 * xodus-combine: upstream Wine 949c1ed5 implements this in iertutil; here the
 * IUri implementation still lives in urlmon, so iertutil forwards to it.
 * Unlike upstream, dwReserved is ignored, matching native (audit-combine).
 */
HRESULT WINAPI PrivateCoInternetCombineIUri(IUri *pBaseUri, IUri *pRelativeUri,
                                            DWORD dwCombineFlags, IUri **ppCombinedUri,
                                            DWORD_PTR dwReserved)
{
    Uri *relative, *base;

    TRACE("(%p %p %lx %p %Ix)\\n", pBaseUri, pRelativeUri, dwCombineFlags, ppCombinedUri, dwReserved);

    if (!ppCombinedUri)
        return E_INVALIDARG;

    if (!pBaseUri || !pRelativeUri)
    {
        *ppCombinedUri = NULL;
        return E_INVALIDARG;
    }

    relative = get_uri_obj(pRelativeUri);
    base = get_uri_obj(pBaseUri);
    if (!relative || !base)
    {
        *ppCombinedUri = NULL;
        FIXME("(%p %p %lx %p %Ix) Unknown IUri types not supported yet.\\n", pBaseUri, pRelativeUri,
              dwCombineFlags, ppCombinedUri, dwReserved);
        return E_NOTIMPL;
    }

    /* Measured native 26100: dwReserved is not used as combine extras. */
    return combine_uri(base, relative, dwCombineFlags, ppCombinedUri, 0);
}

/***********************************************************************
 *           CoInternetCombineIUri (urlmon.@)
 */'''

edit("dlls/urlmon/uri.c",
     "/***********************************************************************\n *           CoInternetCombineIUri (urlmon.@)\n */",
     BODY)
edit("dlls/urlmon/urlmon.spec",
     "@ stdcall CoInternetCombineIUri(ptr ptr long ptr long)\n",
     "@ stdcall CoInternetCombineIUri(ptr ptr long ptr long)\n@ stdcall -private PrivateCoInternetCombineIUri(ptr ptr long ptr long)\n")
edit("dlls/iertutil/iertutil.spec",
     "@ stdcall -private DllGetClassObject(ptr ptr ptr)\n",
     "@ stdcall -private DllGetClassObject(ptr ptr ptr)\n@ stdcall PrivateCoInternetCombineIUri(ptr ptr long ptr long) urlmon.PrivateCoInternetCombineIUri\n")
