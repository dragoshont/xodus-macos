/* SPDX-License-Identifier: GPL-3.0-only */
#include "pch.h"
#include <XAsyncProvider.h>
#include <cstddef>
#include <cstdio>

HC_DEFINE_TRACE_AREA(HTTPCLIENT, HCTraceLevel::Warning);

extern "C" HRESULT WINAPI XodusAsyncValidateAbi(
    uint32_t version, size_t block_size, size_t provider_size,
    size_t buffer_size_offset, size_t buffer_offset, size_t context_offset)
{
    if (version != 1 || block_size != sizeof(XAsyncBlock) ||
        provider_size != sizeof(XAsyncProviderData) ||
        buffer_size_offset != offsetof(XAsyncProviderData, bufferSize) ||
        buffer_offset != offsetof(XAsyncProviderData, buffer) ||
        context_offset != offsetof(XAsyncProviderData, context))
        return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
    return S_OK;
}

STDAPI_(void) HCTraceImplMessage(
    const struct HCTraceImplArea *, HCTraceLevel level, const char *, ...) noexcept
{
    /* Provider context and exception payloads must not enter diagnostics. */
    if (level == HCTraceLevel::Error || level == HCTraceLevel::Warning)
        std::fputs("Xodus async core reported an operation failure.\n", stderr);
}

namespace xbox { namespace httpclient { namespace detail {

HRESULT StdBadAllocToResult(const std::bad_alloc &, const char *, uint32_t)
{
    std::fputs("Xodus async core could not allocate operation state.\n", stderr);
    return E_OUTOFMEMORY;
}

HRESULT StdExceptionToResult(const std::exception &, const char *, uint32_t)
{
    std::fputs("Xodus async core encountered an exception at its API boundary.\n", stderr);
    return E_FAIL;
}

HRESULT UnknownExceptionToResult(const char *, uint32_t)
{
    std::fputs("Xodus async core encountered an unknown exception at its API boundary.\n", stderr);
    return E_FAIL;
}

}}}
