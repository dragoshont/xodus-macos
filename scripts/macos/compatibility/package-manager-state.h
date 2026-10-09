#ifndef XODUS_PACKAGE_MANAGER_STATE_H
#define XODUS_PACKAGE_MANAGER_STATE_H
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum PM_STATE_CONTEXT {
    PM_STATE_UNAVAILABLE = -1,
    PM_STATE_DISABLED = 0,
    PM_STATE_ENABLED = 1
} PM_STATE_CONTEXT;

typedef struct PM_STATE_FACTS {
    PM_STATE_CONTEXT context;
    DWORD shared_flags;
    BYTE silo_scope;
    BYTE state_enabled;
} PM_STATE_FACTS;

PM_STATE_FACTS pm_state_query_context(void);
DWORD pm_state_resolve(PM_STATE_CONTEXT context, LPCWSTR source, LPCWSTR fallback,
                       LPWSTR target, DWORD bytes, LPDWORD required);
DWORD WINAPI GetPersistedRegistryLocationW(LPCWSTR source, LPCWSTR fallback,
                                          LPWSTR target, DWORD bytes, LPDWORD required);

#ifdef __cplusplus
}
#endif
#endif
