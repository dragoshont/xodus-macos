#include <windows.h>
#include <roapi.h>
#include <cstdio>

struct Callback : IApartmentShutdown
{
    LONG refs = 1;
    bool agile = true;
    unsigned calls = 0;
    UINT64 id = 0;
    HRESULT WINAPI QueryInterface(REFIID iid, void **out) override
    {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid == IID_IAgileObject && !agile) return E_NOINTERFACE;
        if (iid != IID_IUnknown && iid != IID_IApartmentShutdown && iid != IID_IAgileObject)
            return E_NOINTERFACE;
        *out = static_cast<IApartmentShutdown *>(this);
        AddRef();
        return S_OK;
    }
    ULONG WINAPI AddRef() override { return InterlockedIncrement(&refs); }
    ULONG WINAPI Release() override { return InterlockedDecrement(&refs); }
    void WINAPI OnUninitialize(UINT64 apartment) override
    {
        ++calls;
        id = apartment;
    }
};

int main()
{
    Callback retained, removed, nonagile;
    nonagile.agile = false;
    UINT64 live = 0, a = 0, b = 0;
    APARTMENT_SHUTDOWN_REGISTRATION_COOKIE ca = nullptr, cb = nullptr;
    if (FAILED(RoInitialize(RO_INIT_SINGLETHREADED))) return 2;
    if (FAILED(RoGetApartmentIdentifier(&live))) return 3;
    APARTMENT_SHUTDOWN_REGISTRATION_COOKIE denied_cookie = nullptr;
    UINT64 denied_id = 0;
    bool denial = RoRegisterForApartmentShutdown(&nonagile, &denied_id, &denied_cookie) ==
                  static_cast<HRESULT>(0x8000001c) && !denied_cookie && nonagile.refs == 1;
    HRESULT ra = RoRegisterForApartmentShutdown(&retained, &a, &ca);
    HRESULT rb = RoRegisterForApartmentShutdown(&removed, &b, &cb);
    std::printf("REGISTER_RESULTS hr_a=%08lx hr_b=%08lx refs_a=%ld refs_b=%ld cookies=%p,%p\n",
                static_cast<unsigned long>(ra), static_cast<unsigned long>(rb),
                retained.refs, removed.refs, ca, cb);
    bool registration = SUCCEEDED(ra) && SUCCEEDED(rb) && ca && cb && ca != cb &&
                        a == live && b == live && retained.refs == 2 && removed.refs == 2;
    HRESULT unregistered = RoUnregisterForApartmentShutdown(cb);
    bool removal = SUCCEEDED(unregistered) && removed.refs == 1;
    RoUninitialize();
    bool delivered = retained.calls == 1 && retained.id == live && retained.refs == 1 &&
                     removed.calls == 0 && removed.refs == 1;
    std::printf("ACTUAL_APARTMENT_SHUTDOWN registration=%d removal=%d callback_delivery=%d nonagile_denied=%d "
                "retained_calls=%u removed_calls=%u actual_id=%llx\n",
                registration, removal, delivered, denial, retained.calls, removed.calls,
                static_cast<unsigned long long>(live));
    return registration && removal && delivered && denial ? 0 : 4;
}
