#pragma once

#include <windows.foundation.collections.h>
#include <windows.foundation.h>
#include <windows.h>
#include <windows.system.h>
#include <windows.ui.core.h>
#include <wrl.h>


namespace display_commanderhooks::wgi {

// CoreWindowProxy - A proxy wrapper for ICoreWindow that logs all method calls
class CoreWindowProxy : public ABI::Windows::UI::Core::ICoreWindow {
  private:
    Microsoft::WRL::ComPtr<ABI::Windows::UI::Core::ICoreWindow> m_originalCoreWindow;
    volatile LONG m_refCount;

  public:
    CoreWindowProxy(Microsoft::WRL::ComPtr<ABI::Windows::UI::Core::ICoreWindow> originalCoreWindow);
    virtual ~CoreWindowProxy() = default;

    // IUnknown methods
    STDMETHOD(QueryInterface)(REFIID riid, void **ppvObject) override;
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;

    // IInspectable methods
    STDMETHOD(GetIids)(ULONG *iidCount, IID **iids) override;
    STDMETHOD(GetRuntimeClassName)(HSTRING *className) override;
    STDMETHOD(GetTrustLevel)(TrustLevel *trustLevel) override;

    // ICoreWindow property getters
    STDMETHOD(get_AutomationHostProvider)(IInspectable **value) override;
    STDMETHOD(get_Bounds)(ABI::Windows::Foundation::Rect *value) override;
    STDMETHOD(get_CustomProperties)(ABI::Windows::Foundation::Collections::IPropertySet **value) override;
    STDMETHOD(get_Dispatcher)(ABI::Windows::UI::Core::ICoreDispatcher **value) override;
    STDMETHOD(get_FlowDirection)(ABI::Windows::UI::Core::CoreWindowFlowDirection *value) override;
    STDMETHOD(get_IsInputEnabled)(boolean *value) override;
    STDMETHOD(get_PointerCursor)(ABI::Windows::UI::Core::ICoreCursor **value) override;
    STDMETHOD(get_PointerPosition)(ABI::Windows::Foundation::Point *value) override;
    STDMETHOD(get_Visible)(boolean *value) override;

    // ICoreWindow property setters
    STDMETHOD(put_FlowDirection)(ABI::Windows::UI::Core::CoreWindowFlowDirection value) override;
    STDMETHOD(put_IsInputEnabled)(boolean value) override;
    STDMETHOD(put_PointerCursor)(ABI::Windows::UI::Core::ICoreCursor *value) override;

    // ICoreWindow methods
    STDMETHOD(Activate)() override;
    STDMETHOD(Close)() override;
    STDMETHOD(GetAsyncKeyState)(ABI::Windows::System::VirtualKey virtualKey,
                                ABI::Windows::UI::Core::CoreVirtualKeyStates *keyState) override;
    STDMETHOD(GetKeyState)(ABI::Windows::System::VirtualKey virtualKey,
                           ABI::Windows::UI::Core::CoreVirtualKeyStates *keyState) override;
    STDMETHOD(ReleasePointerCapture)() override;
    STDMETHOD(SetPointerCapture)() override;

    // ICoreWindow event handlers - Activated
    STDMETHOD(add_Activated)(
        ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::UI::Core::CoreWindow *,
                                                     ABI::Windows::UI::Core::WindowActivatedEventArgs *> *handler,
        EventRegistrationToken *token) override;
    STDMETHOD(remove_Activated)(EventRegistrationToken token) override;

    // ICoreWindow event handlers - AutomationProviderRequested
    STDMETHOD(add_AutomationProviderRequested)(
        ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::UI::Core::CoreWindow *,
                                                     ABI::Windows::UI::Core::AutomationProviderRequestedEventArgs *>
            *handler,
        EventRegistrationToken *token) override;
    STDMETHOD(remove_AutomationProviderRequested)(EventRegistrationToken token) override;

    // ICoreWindow event handlers - CharacterReceived
    STDMETHOD(add_CharacterReceived)(
        ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::UI::Core::CoreWindow *,
                                                     ABI::Windows::UI::Core::CharacterReceivedEventArgs *> *handler,
        EventRegistrationToken *token) override;
    STDMETHOD(remove_CharacterReceived)(EventRegistrationToken token) override;

    // ICoreWindow event handlers - Closed
    STDMETHOD(add_Closed)(
        ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::UI::Core::CoreWindow *,
                                                     ABI::Windows::UI::Core::CoreWindowEventArgs *> *handler,
        EventRegistrationToken *token) override;
    STDMETHOD(remove_Closed)(EventRegistrationToken token) override;

    // ICoreWindow event handlers - InputEnabled
    STDMETHOD(add_InputEnabled)(
        ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::UI::Core::CoreWindow *,
                                                     ABI::Windows::UI::Core::InputEnabledEventArgs *> *handler,
        EventRegistrationToken *token) override;
    STDMETHOD(remove_InputEnabled)(EventRegistrationToken token) override;

    // ICoreWindow event handlers - KeyDown
    STDMETHOD(add_KeyDown)(ABI::Windows::Foundation::ITypedEventHandler<
                               ABI::Windows::UI::Core::CoreWindow *, ABI::Windows::UI::Core::KeyEventArgs *> *handler,
                           EventRegistrationToken *token) override;
    STDMETHOD(remove_KeyDown)(EventRegistrationToken token) override;

    // ICoreWindow event handlers - KeyUp
    STDMETHOD(add_KeyUp)(ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::UI::Core::CoreWindow *,
                                                                      ABI::Windows::UI::Core::KeyEventArgs *> *handler,
                         EventRegistrationToken *token) override;
    STDMETHOD(remove_KeyUp)(EventRegistrationToken token) override;

    // ICoreWindow event handlers - PointerCaptureLost
    STDMETHOD(add_PointerCaptureLost)(
        ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::UI::Core::CoreWindow *,
                                                     ABI::Windows::UI::Core::PointerEventArgs *> *handler,
        EventRegistrationToken *token) override;
    STDMETHOD(remove_PointerCaptureLost)(EventRegistrationToken token) override;

    // ICoreWindow event handlers - PointerEntered
    STDMETHOD(add_PointerEntered)(
        ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::UI::Core::CoreWindow *,
                                                     ABI::Windows::UI::Core::PointerEventArgs *> *handler,
        EventRegistrationToken *token) override;
    STDMETHOD(remove_PointerEntered)(EventRegistrationToken token) override;

    // ICoreWindow event handlers - PointerExited
    STDMETHOD(add_PointerExited)(
        ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::UI::Core::CoreWindow *,
                                                     ABI::Windows::UI::Core::PointerEventArgs *> *handler,
        EventRegistrationToken *token) override;
    STDMETHOD(remove_PointerExited)(EventRegistrationToken token) override;

    // ICoreWindow event handlers - PointerMoved
    STDMETHOD(add_PointerMoved)(
        ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::UI::Core::CoreWindow *,
                                                     ABI::Windows::UI::Core::PointerEventArgs *> *handler,
        EventRegistrationToken *token) override;
    STDMETHOD(remove_PointerMoved)(EventRegistrationToken token) override;

    // ICoreWindow event handlers - PointerPressed
    STDMETHOD(add_PointerPressed)(
        ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::UI::Core::CoreWindow *,
                                                     ABI::Windows::UI::Core::PointerEventArgs *> *handler,
        EventRegistrationToken *token) override;
    STDMETHOD(remove_PointerPressed)(EventRegistrationToken token) override;

    // ICoreWindow event handlers - PointerReleased
    STDMETHOD(add_PointerReleased)(
        ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::UI::Core::CoreWindow *,
                                                     ABI::Windows::UI::Core::PointerEventArgs *> *handler,
        EventRegistrationToken *token) override;
    STDMETHOD(remove_PointerReleased)(EventRegistrationToken token) override;

    // ICoreWindow event handlers - PointerWheelChanged
    STDMETHOD(add_PointerWheelChanged)(
        ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::UI::Core::CoreWindow *,
                                                     ABI::Windows::UI::Core::PointerEventArgs *> *handler,
        EventRegistrationToken *token) override;
    STDMETHOD(remove_PointerWheelChanged)(EventRegistrationToken token) override;

    // ICoreWindow event handlers - SizeChanged
    STDMETHOD(add_SizeChanged)(
        ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::UI::Core::CoreWindow *,
                                                     ABI::Windows::UI::Core::WindowSizeChangedEventArgs *> *handler,
        EventRegistrationToken *token) override;
    STDMETHOD(remove_SizeChanged)(EventRegistrationToken token) override;

    // ICoreWindow event handlers - VisibilityChanged
    STDMETHOD(add_VisibilityChanged)(
        ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::UI::Core::CoreWindow *,
                                                     ABI::Windows::UI::Core::VisibilityChangedEventArgs *> *handler,
        EventRegistrationToken *token) override;
    STDMETHOD(remove_VisibilityChanged)(EventRegistrationToken token) override;

    // ICoreWindow event handlers - TouchHitTesting
    STDMETHOD(add_TouchHitTesting)(
        ABI::Windows::Foundation::ITypedEventHandler<ABI::Windows::UI::Core::CoreWindow *,
                                                     ABI::Windows::UI::Core::TouchHitTestingEventArgs *> *handler,
        EventRegistrationToken *token) override;
    STDMETHOD(remove_TouchHitTesting)(EventRegistrationToken token) override;
};

} // namespace display_commanderhooks::wgi
