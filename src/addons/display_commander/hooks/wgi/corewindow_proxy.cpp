#include "corewindow_proxy.hpp"
#include "../../utils.hpp"

namespace renodx::hooks::wgi {

// CoreWindowProxy Implementation
CoreWindowProxy::CoreWindowProxy(Microsoft::WRL::ComPtr<ABI::Windows::UI::Core::ICoreWindow> originalCoreWindow)
    : m_originalCoreWindow(originalCoreWindow), m_refCount(1)
{
    LogInfo("CoreWindowProxy: Created proxy for ICoreWindow");
}

STDMETHODIMP CoreWindowProxy::QueryInterface(REFIID riid, void** ppvObject)
{
    if (ppvObject == nullptr) return E_POINTER;

    if (riid == IID_IUnknown || riid == ABI::Windows::UI::Core::IID_ICoreWindow) {
        *ppvObject = static_cast<ABI::Windows::UI::Core::ICoreWindow*>(this);
        AddRef();
        return S_OK;
    }

    return m_originalCoreWindow->QueryInterface(riid, ppvObject);
}

STDMETHODIMP_(ULONG) CoreWindowProxy::AddRef()
{
    return InterlockedIncrement(&m_refCount);
}

STDMETHODIMP_(ULONG) CoreWindowProxy::Release()
{
    ULONG refCount = InterlockedDecrement(&m_refCount);
    if (refCount == 0) {
        delete this;
    }
    return refCount;
}

// IInspectable methods - delegate to original
STDMETHODIMP CoreWindowProxy::GetIids(ULONG* iidCount, IID** iids)
{
    LogInfo("CoreWindowProxy::GetIids called");
    return m_originalCoreWindow->GetIids(iidCount, iids);
}

STDMETHODIMP CoreWindowProxy::GetRuntimeClassName(HSTRING* className)
{
    LogInfo("CoreWindowProxy::GetRuntimeClassName called");
    return m_originalCoreWindow->GetRuntimeClassName(className);
}

STDMETHODIMP CoreWindowProxy::GetTrustLevel(TrustLevel* trustLevel)
{
    LogInfo("CoreWindowProxy::GetTrustLevel called");
    return m_originalCoreWindow->GetTrustLevel(trustLevel);
}

// ICoreWindow property getters - delegate to original with logging
STDMETHODIMP CoreWindowProxy::get_AutomationHostProvider(IInspectable** value)
{
    LogInfo("CoreWindowProxy::get_AutomationHostProvider called");
    return m_originalCoreWindow->get_AutomationHostProvider(value);
}

STDMETHODIMP CoreWindowProxy::get_Bounds(ABI::Windows::Foundation::Rect* value)
{
    LogInfo("CoreWindowProxy::get_Bounds called");
    return m_originalCoreWindow->get_Bounds(value);
}

STDMETHODIMP CoreWindowProxy::get_CustomProperties(ABI::Windows::Foundation::Collections::IPropertySet** value)
{
    LogInfo("CoreWindowProxy::get_CustomProperties called");
    return m_originalCoreWindow->get_CustomProperties(value);
}

STDMETHODIMP CoreWindowProxy::get_Dispatcher(ABI::Windows::UI::Core::ICoreDispatcher** value)
{
    LogInfo("CoreWindowProxy::get_Dispatcher called");
    return m_originalCoreWindow->get_Dispatcher(value);
}

STDMETHODIMP CoreWindowProxy::get_FlowDirection(ABI::Windows::UI::Core::CoreWindowFlowDirection* value)
{
    LogInfo("CoreWindowProxy::get_FlowDirection called");
    return m_originalCoreWindow->get_FlowDirection(value);
}

STDMETHODIMP CoreWindowProxy::put_FlowDirection(ABI::Windows::UI::Core::CoreWindowFlowDirection value)
{
    LogInfo("CoreWindowProxy::put_FlowDirection called with value: %d", value);
    return m_originalCoreWindow->put_FlowDirection(value);
}

STDMETHODIMP CoreWindowProxy::get_IsInputEnabled(boolean* value)
{
    LogInfo("CoreWindowProxy::get_IsInputEnabled called");
    return m_originalCoreWindow->get_IsInputEnabled(value);
}

STDMETHODIMP CoreWindowProxy::put_IsInputEnabled(boolean value)
{
    LogInfo("CoreWindowProxy::put_IsInputEnabled called with value: %s", value ? "true" : "false");
    return m_originalCoreWindow->put_IsInputEnabled(value);
}

STDMETHODIMP CoreWindowProxy::get_PointerCursor(ABI::Windows::UI::Core::ICoreCursor** value)
{
    LogInfo("CoreWindowProxy::get_PointerCursor called");
    return m_originalCoreWindow->get_PointerCursor(value);
}

STDMETHODIMP CoreWindowProxy::put_PointerCursor(ABI::Windows::UI::Core::ICoreCursor* value)
{
    LogInfo("CoreWindowProxy::put_PointerCursor called");
    return m_originalCoreWindow->put_PointerCursor(value);
}

STDMETHODIMP CoreWindowProxy::get_PointerPosition(ABI::Windows::Foundation::Point* value)
{
    LogInfo("CoreWindowProxy::get_PointerPosition called");
    return m_originalCoreWindow->get_PointerPosition(value);
}

STDMETHODIMP CoreWindowProxy::get_Visible(boolean* value)
{
    LogInfo("CoreWindowProxy::get_Visible called");
    return m_originalCoreWindow->get_Visible(value);
}

STDMETHODIMP CoreWindowProxy::Activate()
{
    LogInfo("CoreWindowProxy::Activate called");
    return m_originalCoreWindow->Activate();
}

STDMETHODIMP CoreWindowProxy::Close()
{
    LogInfo("CoreWindowProxy::Close called");
    return m_originalCoreWindow->Close();
}

STDMETHODIMP CoreWindowProxy::GetAsyncKeyState(ABI::Windows::System::VirtualKey virtualKey, ABI::Windows::UI::Core::CoreVirtualKeyStates* keyState)
{
    LogInfo("CoreWindowProxy::GetAsyncKeyState called with virtualKey: %d", static_cast<int>(virtualKey));
    return m_originalCoreWindow->GetAsyncKeyState(virtualKey, keyState);
}

STDMETHODIMP CoreWindowProxy::GetKeyState(ABI::Windows::System::VirtualKey virtualKey, ABI::Windows::UI::Core::CoreVirtualKeyStates* keyState)
{
    LogInfo("CoreWindowProxy::GetKeyState called with virtualKey: %d", static_cast<int>(virtualKey));
    return m_originalCoreWindow->GetKeyState(virtualKey, keyState);
}

STDMETHODIMP CoreWindowProxy::ReleasePointerCapture()
{
    LogInfo("CoreWindowProxy::ReleasePointerCapture called");
    return m_originalCoreWindow->ReleasePointerCapture();
}

STDMETHODIMP CoreWindowProxy::SetPointerCapture()
{
    LogInfo("CoreWindowProxy::SetPointerCapture called");
    return m_originalCoreWindow->SetPointerCapture();
}

// Event handler methods - delegate to original with logging
STDMETHODIMP CoreWindowProxy::add_Activated(ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::UI::Core::CoreWindow*, ABI::Windows::UI::Core::WindowActivatedEventArgs*>* handler, EventRegistrationToken* token)
{
    LogInfo("CoreWindowProxy::add_Activated called");
    return m_originalCoreWindow->add_Activated(handler, token);
}

STDMETHODIMP CoreWindowProxy::remove_Activated(EventRegistrationToken token)
{
    LogInfo("CoreWindowProxy::remove_Activated called");
    return m_originalCoreWindow->remove_Activated(token);
}

STDMETHODIMP CoreWindowProxy::add_AutomationProviderRequested(ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::UI::Core::CoreWindow*, ABI::Windows::UI::Core::AutomationProviderRequestedEventArgs*>* handler, EventRegistrationToken* token)
{
    LogInfo("CoreWindowProxy::add_AutomationProviderRequested called");
    return m_originalCoreWindow->add_AutomationProviderRequested(handler, token);
}

STDMETHODIMP CoreWindowProxy::remove_AutomationProviderRequested(EventRegistrationToken token)
{
    LogInfo("CoreWindowProxy::remove_AutomationProviderRequested called");
    return m_originalCoreWindow->remove_AutomationProviderRequested(token);
}

STDMETHODIMP CoreWindowProxy::add_CharacterReceived(ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::UI::Core::CoreWindow*, ABI::Windows::UI::Core::CharacterReceivedEventArgs*>* handler, EventRegistrationToken* token)
{
    LogInfo("CoreWindowProxy::add_CharacterReceived called");
    return m_originalCoreWindow->add_CharacterReceived(handler, token);
}

STDMETHODIMP CoreWindowProxy::remove_CharacterReceived(EventRegistrationToken token)
{
    LogInfo("CoreWindowProxy::remove_CharacterReceived called");
    return m_originalCoreWindow->remove_CharacterReceived(token);
}

STDMETHODIMP CoreWindowProxy::add_Closed(ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::UI::Core::CoreWindow*, ABI::Windows::UI::Core::CoreWindowEventArgs*>* handler, EventRegistrationToken* token)
{
    LogInfo("CoreWindowProxy::add_Closed called");
    return m_originalCoreWindow->add_Closed(handler, token);
}

STDMETHODIMP CoreWindowProxy::remove_Closed(EventRegistrationToken token)
{
    LogInfo("CoreWindowProxy::remove_Closed called");
    return m_originalCoreWindow->remove_Closed(token);
}

STDMETHODIMP CoreWindowProxy::add_InputEnabled(ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::UI::Core::CoreWindow*, ABI::Windows::UI::Core::InputEnabledEventArgs*>* handler, EventRegistrationToken* token)
{
    LogInfo("CoreWindowProxy::add_InputEnabled called");
    return m_originalCoreWindow->add_InputEnabled(handler, token);
}

STDMETHODIMP CoreWindowProxy::remove_InputEnabled(EventRegistrationToken token)
{
    LogInfo("CoreWindowProxy::remove_InputEnabled called");
    return m_originalCoreWindow->remove_InputEnabled(token);
}

STDMETHODIMP CoreWindowProxy::add_KeyDown(ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::UI::Core::CoreWindow*, ABI::Windows::UI::Core::KeyEventArgs*>* handler, EventRegistrationToken* token)
{
    LogInfo("CoreWindowProxy::add_KeyDown called");
    return m_originalCoreWindow->add_KeyDown(handler, token);
}

STDMETHODIMP CoreWindowProxy::remove_KeyDown(EventRegistrationToken token)
{
    LogInfo("CoreWindowProxy::remove_KeyDown called");
    return m_originalCoreWindow->remove_KeyDown(token);
}

STDMETHODIMP CoreWindowProxy::add_KeyUp(ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::UI::Core::CoreWindow*, ABI::Windows::UI::Core::KeyEventArgs*>* handler, EventRegistrationToken* token)
{
    LogInfo("CoreWindowProxy::add_KeyUp called");
    return m_originalCoreWindow->add_KeyUp(handler, token);
}

STDMETHODIMP CoreWindowProxy::remove_KeyUp(EventRegistrationToken token)
{
    LogInfo("CoreWindowProxy::remove_KeyUp called");
    return m_originalCoreWindow->remove_KeyUp(token);
}

STDMETHODIMP CoreWindowProxy::add_PointerCaptureLost(ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::UI::Core::CoreWindow*, ABI::Windows::UI::Core::PointerEventArgs*>* handler, EventRegistrationToken* token)
{
    LogInfo("CoreWindowProxy::add_PointerCaptureLost called");
    return m_originalCoreWindow->add_PointerCaptureLost(handler, token);
}

STDMETHODIMP CoreWindowProxy::remove_PointerCaptureLost(EventRegistrationToken token)
{
    LogInfo("CoreWindowProxy::remove_PointerCaptureLost called");
    return m_originalCoreWindow->remove_PointerCaptureLost(token);
}

STDMETHODIMP CoreWindowProxy::add_PointerEntered(ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::UI::Core::CoreWindow*, ABI::Windows::UI::Core::PointerEventArgs*>* handler, EventRegistrationToken* token)
{
    LogInfo("CoreWindowProxy::add_PointerEntered called");
    return m_originalCoreWindow->add_PointerEntered(handler, token);
}

STDMETHODIMP CoreWindowProxy::remove_PointerEntered(EventRegistrationToken token)
{
    LogInfo("CoreWindowProxy::remove_PointerEntered called");
    return m_originalCoreWindow->remove_PointerEntered(token);
}

STDMETHODIMP CoreWindowProxy::add_PointerExited(ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::UI::Core::CoreWindow*, ABI::Windows::UI::Core::PointerEventArgs*>* handler, EventRegistrationToken* token)
{
    LogInfo("CoreWindowProxy::add_PointerExited called");
    return m_originalCoreWindow->add_PointerExited(handler, token);
}

STDMETHODIMP CoreWindowProxy::remove_PointerExited(EventRegistrationToken token)
{
    LogInfo("CoreWindowProxy::remove_PointerExited called");
    return m_originalCoreWindow->remove_PointerExited(token);
}

STDMETHODIMP CoreWindowProxy::add_PointerMoved(ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::UI::Core::CoreWindow*, ABI::Windows::UI::Core::PointerEventArgs*>* handler, EventRegistrationToken* token)
{
    LogInfo("CoreWindowProxy::add_PointerMoved called");
    return m_originalCoreWindow->add_PointerMoved(handler, token);
}

STDMETHODIMP CoreWindowProxy::remove_PointerMoved(EventRegistrationToken token)
{
    LogInfo("CoreWindowProxy::remove_PointerMoved called");
    return m_originalCoreWindow->remove_PointerMoved(token);
}

STDMETHODIMP CoreWindowProxy::add_PointerPressed(ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::UI::Core::CoreWindow*, ABI::Windows::UI::Core::PointerEventArgs*>* handler, EventRegistrationToken* token)
{
    LogInfo("CoreWindowProxy::add_PointerPressed called");
    return m_originalCoreWindow->add_PointerPressed(handler, token);
}

STDMETHODIMP CoreWindowProxy::remove_PointerPressed(EventRegistrationToken token)
{
    LogInfo("CoreWindowProxy::remove_PointerPressed called");
    return m_originalCoreWindow->remove_PointerPressed(token);
}

STDMETHODIMP CoreWindowProxy::add_PointerReleased(ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::UI::Core::CoreWindow*, ABI::Windows::UI::Core::PointerEventArgs*>* handler, EventRegistrationToken* token)
{
    LogInfo("CoreWindowProxy::add_PointerReleased called");
    return m_originalCoreWindow->add_PointerReleased(handler, token);
}

STDMETHODIMP CoreWindowProxy::remove_PointerReleased(EventRegistrationToken token)
{
    LogInfo("CoreWindowProxy::remove_PointerReleased called");
    return m_originalCoreWindow->remove_PointerReleased(token);
}

STDMETHODIMP CoreWindowProxy::add_PointerWheelChanged(ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::UI::Core::CoreWindow*, ABI::Windows::UI::Core::PointerEventArgs*>* handler, EventRegistrationToken* token)
{
    LogInfo("CoreWindowProxy::add_PointerWheelChanged called");
    return m_originalCoreWindow->add_PointerWheelChanged(handler, token);
}

STDMETHODIMP CoreWindowProxy::remove_PointerWheelChanged(EventRegistrationToken token)
{
    LogInfo("CoreWindowProxy::remove_PointerWheelChanged called");
    return m_originalCoreWindow->remove_PointerWheelChanged(token);
}

STDMETHODIMP CoreWindowProxy::add_SizeChanged(ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::UI::Core::CoreWindow*, ABI::Windows::UI::Core::WindowSizeChangedEventArgs*>* handler, EventRegistrationToken* token)
{
    LogInfo("CoreWindowProxy::add_SizeChanged called");
    return m_originalCoreWindow->add_SizeChanged(handler, token);
}

STDMETHODIMP CoreWindowProxy::remove_SizeChanged(EventRegistrationToken token)
{
    LogInfo("CoreWindowProxy::remove_SizeChanged called");
    return m_originalCoreWindow->remove_SizeChanged(token);
}

STDMETHODIMP CoreWindowProxy::add_VisibilityChanged(ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::UI::Core::CoreWindow*, ABI::Windows::UI::Core::VisibilityChangedEventArgs*>* handler, EventRegistrationToken* token)
{
    LogInfo("CoreWindowProxy::add_VisibilityChanged called");
    return m_originalCoreWindow->add_VisibilityChanged(handler, token);
}

STDMETHODIMP CoreWindowProxy::remove_VisibilityChanged(EventRegistrationToken token)
{
    LogInfo("CoreWindowProxy::remove_VisibilityChanged called");
    return m_originalCoreWindow->remove_VisibilityChanged(token);
}

STDMETHODIMP CoreWindowProxy::add_TouchHitTesting(ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::UI::Core::CoreWindow*, ABI::Windows::UI::Core::TouchHitTestingEventArgs*>* handler, EventRegistrationToken* token)
{
    LogInfo("CoreWindowProxy::add_TouchHitTesting called");
    return m_originalCoreWindow->add_TouchHitTesting(handler, token);
}

STDMETHODIMP CoreWindowProxy::remove_TouchHitTesting(EventRegistrationToken token)
{
    LogInfo("CoreWindowProxy::remove_TouchHitTesting called");
    return m_originalCoreWindow->remove_TouchHitTesting(token);
}

} // namespace renodx::hooks::wgi
