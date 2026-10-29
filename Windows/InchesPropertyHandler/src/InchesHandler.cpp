// InchesHandler.cpp
//
// A *wrapping* Windows property handler. It loads the property handler that was
// registered for an extension before us (e.g. Microsoft's image handler for .jpg),
// forwards every call to it, and adds one read-only computed property:
//
//   InchesHandler.Image.Dimensions   "6.67 x 10 in"  (label: Dimensions (inches))
//
// computed as pixels / DPI from System.Image.{Horizontal,Vertical}{Size,Resolution}
// as reported by the wrapped handler.
//
// Error handling: if anything goes wrong (wrapped handler can't be found, created
// or initialized; dimensions/DPI missing or invalid), the row reads "Error"
// and the reason is written with OutputDebugString (view with Sysinternals
// DebugView, lines prefixed "InchesHandler:"). Initialize still succeeds in that
// case, so Explorer keeps showing the file-system properties instead of an empty
// Details tab. Without a wrapped handler the store is read-only.
//
// One DLL serves many CLSIDs: every CLSID whose first 15 bytes match
// CLSID_InchesHandlerBase is ours. install.ps1 assigns one CLSID per extension
// and records the wrapped handler's CLSID in the registry value
//   HKLM\SOFTWARE\Classes\CLSID\{our clsid}\WrappedHandler

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlwapi.h>
#include <propsys.h>
#include <propvarutil.h>
#include <initguid.h>
#include <propkey.h>
#include <new>
#include <cwchar>
#include <cstring>

// ---------------------------------------------------------------------------
// GUIDs and property keys
// ---------------------------------------------------------------------------

// {E35046DE-611F-44CA-BC68-2E042D1CAE00} - last byte is the per-extension slot
static const GUID CLSID_InchesHandlerBase =
    { 0xe35046de, 0x611f, 0x44ca, { 0xbc, 0x68, 0x2e, 0x04, 0x2d, 0x1c, 0xae, 0x00 } };

// {3AF33E32-8AE8-4B1E-BA39-340A1D46E3D0} - must match formatID in InchesProps.propdesc
static const GUID FMTID_InchesHandler =
    { 0x3af33e32, 0x8ae8, 0x4b1e, { 0xba, 0x39, 0x34, 0x0a, 0x1d, 0x46, 0xe3, 0xd0 } };

static const PROPERTYKEY PKEY_Inches_Dimensions = { FMTID_InchesHandler, 2 };

static const PROPERTYKEY* const g_ourKeys[] = {
    &PKEY_Inches_Dimensions
};
static const DWORD c_ourKeyCount = ARRAYSIZE(g_ourKeys);

static const wchar_t c_errorText[] = L"Error";

static LONG g_cDllRef = 0;

static bool IsOurClsid(REFCLSID clsid)
{
    // Data1, Data2, Data3 and Data4[0..6] must match; Data4[7] is the slot.
    return memcmp(&clsid, &CLSID_InchesHandlerBase, sizeof(GUID) - 1) == 0;
}

static int OurKeyIndex(REFPROPERTYKEY key)
{
    for (DWORD i = 0; i < c_ourKeyCount; i++)
        if (IsEqualPropertyKey(key, *g_ourKeys[i]))
            return (int)i;
    return -1;
}

static void LogError(const wchar_t* what, HRESULT hr)
{
    wchar_t msg[256];
    swprintf_s(msg, L"InchesHandler: %ls (hr=0x%08lX)\n", what, (unsigned long)hr);
    OutputDebugStringW(msg);
}

// Reads HKLM\SOFTWARE\Classes\CLSID\{clsid}\WrappedHandler
static HRESULT GetWrappedClsid(REFCLSID ourClsid, CLSID* pInner)
{
    wchar_t clsidStr[64];
    if (!StringFromGUID2(ourClsid, clsidStr, ARRAYSIZE(clsidStr)))
        return E_FAIL;

    wchar_t keyPath[128];
    swprintf_s(keyPath, L"SOFTWARE\\Classes\\CLSID\\%ls", clsidStr);

    wchar_t value[64];
    DWORD cb = sizeof(value);
    LSTATUS ls = RegGetValueW(HKEY_LOCAL_MACHINE, keyPath, L"WrappedHandler",
                              RRF_RT_REG_SZ, nullptr, value, &cb);
    if (ls != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(ls);

    HRESULT hr = CLSIDFromString(value, pInner);
    if (SUCCEEDED(hr) && IsOurClsid(*pInner))
        hr = E_UNEXPECTED; // refuse to wrap ourselves (would recurse forever)
    return hr;
}

// "6.6667" -> "6.67", "10.00" -> "10"
static void FormatInches(double inches, wchar_t* buf, size_t cch)
{
    swprintf_s(buf, cch, L"%.2f", inches);
    wchar_t* dot = wcschr(buf, L'.');
    if (dot)
    {
        wchar_t* end = buf + wcslen(buf) - 1;
        while (end > dot && *end == L'0') *end-- = L'\0';
        if (end == dot) *end = L'\0';
    }
}

// ---------------------------------------------------------------------------
// The handler
// ---------------------------------------------------------------------------

class CInchesHandler final
    : public IPropertyStore
    , public IPropertyStoreCapabilities
    , public IInitializeWithStream
{
public:
    // hrResolve: result of looking up the wrapped CLSID. If it failed, the
    // handler still works but reports "Error" in its row.
    CInchesHandler(REFCLSID innerClsid, HRESULT hrResolve)
        : m_cRef(1), m_innerClsid(innerClsid), m_hrResolve(hrResolve)
    {
        for (auto& pv : m_values) PropVariantInit(&pv);
        InterlockedIncrement(&g_cDllRef);
    }

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_IPropertyStore)
            *ppv = static_cast<IPropertyStore*>(this);
        else if (riid == IID_IPropertyStoreCapabilities)
            *ppv = static_cast<IPropertyStoreCapabilities*>(this);
        else if (riid == IID_IInitializeWithStream)
            *ppv = static_cast<IInitializeWithStream*>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_cRef); }
    IFACEMETHODIMP_(ULONG) Release() override
    {
        ULONG c = InterlockedDecrement(&m_cRef);
        if (c == 0) delete this;
        return c;
    }

    // IInitializeWithStream
    IFACEMETHODIMP Initialize(IStream* pstm, DWORD grfMode) override
    {
        if (m_initialized) return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
        if (!pstm) return E_INVALIDARG;
        m_initialized = true;

        if (FAILED(m_hrResolve))
        {
            LogError(L"WrappedHandler registry value missing or invalid", m_hrResolve);
            SetError();
            return S_OK;
        }

        IInitializeWithStream* innerInit = nullptr;
        HRESULT hr = CoCreateInstance(m_innerClsid, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_IInitializeWithStream,
                                      reinterpret_cast<void**>(&innerInit));
        if (FAILED(hr))
        {
            LogError(L"could not create wrapped handler", hr);
            SetError();
            return S_OK;
        }

        hr = innerInit->Initialize(pstm, grfMode);
        if (FAILED(hr))
            LogError(L"wrapped handler failed to initialize", hr);
        else
        {
            hr = innerInit->QueryInterface(IID_IPropertyStore,
                                           reinterpret_cast<void**>(&m_inner));
            if (FAILED(hr))
                LogError(L"wrapped handler has no IPropertyStore", hr);
        }

        if (SUCCEEDED(hr))
        {
            // Optional on the inner handler.
            innerInit->QueryInterface(IID_IPropertyStoreCapabilities,
                                      reinterpret_cast<void**>(&m_innerCaps));
            ComputeInches();
        }
        else
        {
            SetError();
        }
        innerInit->Release();
        return S_OK;
    }

    // IPropertyStore
    IFACEMETHODIMP GetCount(DWORD* pcProps) override
    {
        if (!pcProps) return E_POINTER;
        *pcProps = 0;
        if (!m_initialized) return E_UNEXPECTED;
        DWORD inner = 0;
        if (m_inner)
        {
            HRESULT hr = m_inner->GetCount(&inner);
            if (FAILED(hr)) return hr;
        }
        *pcProps = inner + m_ourCount;
        return S_OK;
    }

    IFACEMETHODIMP GetAt(DWORD iProp, PROPERTYKEY* pkey) override
    {
        if (!pkey) return E_POINTER;
        if (!m_initialized) return E_UNEXPECTED;
        DWORD inner = 0;
        if (m_inner)
        {
            HRESULT hr = m_inner->GetCount(&inner);
            if (FAILED(hr)) return hr;
            if (iProp < inner) return m_inner->GetAt(iProp, pkey);
        }
        DWORD i = iProp - inner;
        if (i >= m_ourCount) return E_INVALIDARG;
        *pkey = *g_ourKeys[i];
        return S_OK;
    }

    IFACEMETHODIMP GetValue(REFPROPERTYKEY key, PROPVARIANT* pv) override
    {
        if (!pv) return E_POINTER;
        PropVariantInit(pv);
        if (!m_initialized) return E_UNEXPECTED;
        int i = OurKeyIndex(key);
        if (i >= 0)
            return PropVariantCopy(pv, &m_values[i]);
        if (!m_inner)
            return S_OK; // unknown property: VT_EMPTY, per IPropertyStore convention
        return m_inner->GetValue(key, pv);
    }

    IFACEMETHODIMP SetValue(REFPROPERTYKEY key, REFPROPVARIANT propvar) override
    {
        if (!m_initialized) return E_UNEXPECTED;
        if (OurKeyIndex(key) >= 0 || !m_inner) return STG_E_ACCESSDENIED;
        return m_inner->SetValue(key, propvar);
    }

    IFACEMETHODIMP Commit() override
    {
        if (!m_initialized) return E_UNEXPECTED;
        if (!m_inner) return S_OK; // nothing could have been changed
        return m_inner->Commit();
    }

    // IPropertyStoreCapabilities
    IFACEMETHODIMP IsPropertyWritable(REFPROPERTYKEY key) override
    {
        if (OurKeyIndex(key) >= 0 || !m_inner) return S_FALSE;
        if (m_innerCaps) return m_innerCaps->IsPropertyWritable(key);
        return S_OK; // inner handler made no claim; same default the shell assumes
    }

private:
    ~CInchesHandler()
    {
        for (auto& pv : m_values) PropVariantClear(&pv);
        if (m_innerCaps) m_innerCaps->Release();
        if (m_inner) m_inner->Release();
        InterlockedDecrement(&g_cDllRef);
    }

    // Sets our row(s) to "Error". If even that allocation fails, the row is
    // hidden (m_ourCount = 0) rather than shown blank.
    void SetError()
    {
        m_ourCount = 0;
        for (DWORD i = 0; i < c_ourKeyCount; i++)
        {
            PropVariantClear(&m_values[i]);
            if (FAILED(InitPropVariantFromString(c_errorText, &m_values[i])))
                return;
        }
        m_ourCount = c_ourKeyCount;
    }

    HRESULT GetInnerUInt(REFPROPERTYKEY key, ULONG* out)
    {
        PROPVARIANT pv; PropVariantInit(&pv);
        HRESULT hr = m_inner->GetValue(key, &pv);
        if (SUCCEEDED(hr))
            hr = (pv.vt == VT_EMPTY) ? HRESULT_FROM_WIN32(ERROR_NOT_FOUND)
                                     : PropVariantToUInt32(pv, out);
        PropVariantClear(&pv);
        return hr;
    }

    HRESULT GetInnerDouble(REFPROPERTYKEY key, double* out)
    {
        PROPVARIANT pv; PropVariantInit(&pv);
        HRESULT hr = m_inner->GetValue(key, &pv);
        if (SUCCEEDED(hr))
            hr = (pv.vt == VT_EMPTY) ? HRESULT_FROM_WIN32(ERROR_NOT_FOUND)
                                     : PropVariantToDouble(pv, out);
        PropVariantClear(&pv);
        return hr;
    }

    void ComputeInches()
    {
        ULONG w = 0, h = 0;
        double dpiX = 0, dpiY = 0;
        HRESULT hr;

        if (FAILED(hr = GetInnerUInt(PKEY_Image_HorizontalSize, &w)))
            { LogError(L"width unavailable", hr); SetError(); return; }
        if (FAILED(hr = GetInnerUInt(PKEY_Image_VerticalSize, &h)))
            { LogError(L"height unavailable", hr); SetError(); return; }
        if (FAILED(hr = GetInnerDouble(PKEY_Image_HorizontalResolution, &dpiX)))
            { LogError(L"horizontal DPI unavailable", hr); SetError(); return; }
        if (FAILED(GetInnerDouble(PKEY_Image_VerticalResolution, &dpiY)))
            dpiY = dpiX; // tolerate a missing vertical DPI: assume square pixels

        if (w == 0 || h == 0 || dpiX <= 0 || dpiY <= 0)
            { LogError(L"zero size or non-positive DPI", E_INVALIDARG); SetError(); return; }

        wchar_t sw[32], sh[32], buf[96];
        FormatInches(w / dpiX, sw, ARRAYSIZE(sw));
        FormatInches(h / dpiY, sh, ARRAYSIZE(sh));

        PROPVARIANT tmp; PropVariantInit(&tmp);
        swprintf_s(buf, L"%ls × %ls in", sw, sh);
        hr = InitPropVariantFromString(buf, &tmp);
        if (FAILED(hr))
        {
            LogError(L"could not build result string", hr);
            SetError();
            return;
        }

        PropVariantClear(&m_values[0]);
        m_values[0] = tmp; // ownership transferred
        m_ourCount = c_ourKeyCount;
    }

    LONG m_cRef;
    CLSID m_innerClsid;
    HRESULT m_hrResolve;
    bool m_initialized = false;
    IPropertyStore* m_inner = nullptr;             // null if wrapping failed
    IPropertyStoreCapabilities* m_innerCaps = nullptr;
    PROPVARIANT m_values[c_ourKeyCount];
    DWORD m_ourCount = 0;                          // 0 or c_ourKeyCount
};

// ---------------------------------------------------------------------------
// Class factory
// ---------------------------------------------------------------------------

class CClassFactory final : public IClassFactory
{
public:
    explicit CClassFactory(REFCLSID clsid) : m_cRef(1), m_clsid(clsid)
    {
        InterlockedIncrement(&g_cDllRef);
    }

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_IClassFactory)
        {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_cRef); }
    IFACEMETHODIMP_(ULONG) Release() override
    {
        ULONG c = InterlockedDecrement(&m_cRef);
        if (c == 0) delete this;
        return c;
    }

    IFACEMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) override
    {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (pUnkOuter) return CLASS_E_NOAGGREGATION;

        // A lookup failure is not fatal here: the handler reports it as "Error".
        CLSID inner = CLSID_NULL;
        HRESULT hrResolve = GetWrappedClsid(m_clsid, &inner);

        CInchesHandler* p = new (std::nothrow) CInchesHandler(inner, hrResolve);
        if (!p) return E_OUTOFMEMORY;
        HRESULT hr = p->QueryInterface(riid, ppv);
        p->Release();
        return hr;
    }

    IFACEMETHODIMP LockServer(BOOL fLock) override
    {
        if (fLock) InterlockedIncrement(&g_cDllRef);
        else       InterlockedDecrement(&g_cDllRef);
        return S_OK;
    }

private:
    ~CClassFactory() { InterlockedDecrement(&g_cDllRef); }
    LONG m_cRef;
    CLSID m_clsid;
};

// ---------------------------------------------------------------------------
// DLL exports
// ---------------------------------------------------------------------------

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(hModule);
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (!IsOurClsid(rclsid)) return CLASS_E_CLASSNOTAVAILABLE;

    CClassFactory* f = new (std::nothrow) CClassFactory(rclsid);
    if (!f) return E_OUTOFMEMORY;
    HRESULT hr = f->QueryInterface(riid, ppv);
    f->Release();
    return hr;
}

STDAPI DllCanUnloadNow()
{
    return g_cDllRef == 0 ? S_OK : S_FALSE;
}
