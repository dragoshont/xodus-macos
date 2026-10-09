#include <windows.h>
#include <stdio.h>

HRESULT WINAPI CoRegisterServerShutdownDelay(HANDLE stop_event, DWORD milliseconds)
{
    fprintf(stderr, "CoRegisterServerShutdownDelay event=%p milliseconds=%lu\n",
            stop_event, (unsigned long)milliseconds);
    if ((!stop_event) != (!milliseconds)) return E_INVALIDARG;
    /* No delay can have been registered by this bounded provider. */
    if (!stop_event) return S_OK;
    fprintf(stderr, "COM server shutdown-delay registration is not implemented\n");
    return E_NOTIMPL;
}
