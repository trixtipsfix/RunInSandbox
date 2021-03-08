#pragma once
#include "Sandboxing.hpp"
#include "ProcCreate.hpp"
#include <tuple>
#include "../TestControl/ComSupport.hpp"
//#define DEBUG_COM_ACTIVATION


/** Attempt to create a COM server that runds through a specific user account.
    AppContainer problem:
      Process is created but CoGetClassObject activation gives E_ACCESSDENIED (The machine-default permission settings do not grant Local Activation permission for the COM Server) */
CComPtr<IUnknown> CoCreateAsUser_impersonate (CLSID clsid, IntegrityLevel mode, bool grant_appcontainer_permissions) {
    std::unique_ptr<ImpersonateThread> impersonate;
    bool explicit_process_create = (mode == IntegrityLevel::AppContainer);
    if (explicit_process_create) {
        // launch COM server process manually
        wchar_t clsid_str[39] = {};
        int ok = StringFromGUID2(clsid, const_cast<wchar_t*>(clsid_str), static_cast<int>(std::size(clsid_str)));
        if (!ok)
            abort(); // should never happen

        std::wstring exe_path = RegQuery::GetExePath(clsid_str);
        if (exe_path.empty())
            exe_path = RegQuery::GetExePath(clsid_str, KEY_WOW64_32KEY); // fallback to 32bit part of registry
        if (exe_path.empty()) {
            std::wcerr << L"ERROR: Unable to locate COM server EXE path." << std::endl;
            exit(-2);
        }

        if (grant_appcontainer_permissions) {
            // grant ALL_APPLICATION_PACKAGES permission to the COM EXE & DCOM LaunchPermission
            const wchar_t ac_str_sid[] = L"S-1-15-2-1"; // ALL_APP_PACKAGES


            DWORD existing_access = Permissions::TryAccessPath(ac_str_sid, exe_path.c_str());
            if (((existing_access & GENERIC_READ) == GENERIC_READ) || ((existing_access & FILE_GENERIC_READ) == FILE_GENERIC_READ)) {
                std::wcout << "AppContainer already have EXE access.\n";
            } else {
                DWORD err = Permissions::MakePathAppContainer(ac_str_sid, exe_path.c_str(), GENERIC_READ | GENERIC_EXECUTE);
                if (err != ERROR_SUCCESS) {
                    _com_error error(err);
                    std::wcerr << L"ERROR: Failed to grant AppContainer permissions to the EXE, MSG=\"" << error.ErrorMessage() << L"\" (" << err << L")" << std::endl;
                    exit(-2);
                }
            }

            std::wstring app_id = RegQuery::GetAppID(clsid_str);
            if (app_id.empty()) {
                std::wcerr << L"ERROR: Unable to locate COM server AppID." << std::endl;
                exit(-2);
            }
            DWORD err = Permissions::EnableLaunchActPermission(ac_str_sid, app_id.c_str());
            if (err != ERROR_SUCCESS) {
                _com_error error(err);
                std::wcerr << L"ERROR: Failed to grant AppContainer AppID LaunchPermission, MSG=\"" << error.ErrorMessage() << L"\" (" << err << L")" << std::endl;
                exit(-2);
            }
        }

        HandleWrap proc = ProcCreate(exe_path.c_str(), mode, {L"-Embedding"}); // mimic how svchost passes "-Embedding" argument
        // impersonate the process thread
        impersonate.reset(new ImpersonateThread(proc));
    } else {
        // impersonate a different integrity (or user)
        impersonate.reset(new ImpersonateThread(mode));
    }

    CComPtr<IUnknown> obj;
    // create COM object in a separate process
#ifdef DEBUG_COM_ACTIVATION
    // open Event Viewer, "Windows Logs" -> "System" log to see details on failures
    CComPtr<IClassFactory> cf;
    HRESULT hr = CoGetClassObject(clsid, CLSCTX_LOCAL_SERVER | CLSCTX_ENABLE_CLOAKING, NULL, IID_IClassFactory, (void**)&cf);
    if ((mode == IntegrityLevel::AppContainer) && (hr == E_ACCESSDENIED)) {
        std::wcerr << L"ERROR: CoGetClassObject access denied when trying to create a new COM server instance. Have you remember to grant AppContainer permissions?" << std::endl;
        exit(-3);
    } else {
        CHECK(hr);
    }
    hr = cf->CreateInstance(nullptr, IID_IUnknown, (void**)&obj);
    CHECK(hr);
#else
    HRESULT hr = obj.CoCreateInstance(clsid, nullptr, CLSCTX_LOCAL_SERVER | CLSCTX_ENABLE_CLOAKING);
    if ((mode == IntegrityLevel::AppContainer) && (hr == E_ACCESSDENIED)) {
        std::wcerr << L"ERROR: CoCreateInstance access denied when trying to create a new COM server instance. Have you remember to grant AppContainer permissions?" << std::endl;
        exit(-3);
    } else {
        CHECK(hr);
    }
#endif

    return obj;
}


/** Create a AppID and elevation-enabled COM server in a admin process.
    REF: https://docs.microsoft.com/en-us/windows/win32/com/the-com-elevation-moniker */
template <typename T>
static HRESULT CoCreateInstanceElevated (HWND window, const GUID clsid, T ** result) {
    if (!result)
        return E_INVALIDARG;
    if (*result)
        return E_INVALIDARG;

    wchar_t clsid_str[39] = {};
    int ok = StringFromGUID2(clsid, const_cast<wchar_t*>(clsid_str), static_cast<int>(std::size(clsid_str)));
    if (!ok)
        abort(); // should never happen

    std::wstring obj_name = L"Elevation:Administrator!new:";
    obj_name += clsid_str;

    BIND_OPTS3 options = {};
    options.cbStruct = sizeof(options);
    options.hwnd = window;
    options.dwClassContext = CLSCTX_LOCAL_SERVER;

    //std::wcout << L"CoGetObject: " << obj_name << L'\n';
    return ::CoGetObject(obj_name.c_str(), &options, __uuidof(T), reinterpret_cast<void**>(result));
}


/** Try to set a an attribute on an automation-compatible COM server. */
static bool SetComAttribute(CComPtr<IUnknown> & obj, CComBSTR name, CComVariant value) {
    CComPtr<IDispatch> obj_disp;
    if (FAILED(obj.QueryInterface(&obj_disp)))
        return false;

    // lookup attribute ID
    DISPID dispid = 0;
    if (FAILED(obj_disp->GetIDsOfNames(IID_NULL, &name, 1, LOCALE_USER_DEFAULT, &dispid)))
        return false;

    // prepare arguments
    DISPPARAMS params = {};
    DISPID type = DISPID_PROPERTYPUT;
    {
        params.cArgs = 1;
        params.rgvarg = &value;
        params.cNamedArgs = 1;
        params.rgdispidNamedArgs = &type;
    }

    // invoke call
    HRESULT hr = obj_disp->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYPUT, &params, NULL, NULL, NULL);
    return SUCCEEDED(hr);
}
