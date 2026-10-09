/* Generated WIDL 11.0 declarations for the token transport fixture. */

#ifdef _WIN32
#ifndef __REQUIRED_RPCNDR_H_VERSION__
#define __REQUIRED_RPCNDR_H_VERSION__ 475
#endif
#include <rpc.h>
#include <rpcndr.h>
#endif

#ifndef COM_NO_WINDOWS_H
#include <windows.h>
#include <ole2.h>
#endif

#ifndef __token_probe_h__
#define __token_probe_h__

/* Forward declarations */

/* Headers for imported files */


#ifdef __cplusplus
extern "C" {
#endif

/*****************************************************************************
 * TokenProbe interface (v1.0)
 */
#ifndef __TokenProbe_INTERFACE_DEFINED__
#define __TokenProbe_INTERFACE_DEFINED__

extern RPC_IF_HANDLE TokenProbe_v1_0_c_ifspec;
extern RPC_IF_HANDLE TokenProbe_v1_0_s_ifspec;
LONG __cdecl CheckToken(
    handle_t binding,
    hyper token);

LONG __cdecl StopServer(
    handle_t binding);


#endif  /* __TokenProbe_INTERFACE_DEFINED__ */

/* Begin additional prototypes for all interfaces */


/* End additional prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __token_probe_h__ */
