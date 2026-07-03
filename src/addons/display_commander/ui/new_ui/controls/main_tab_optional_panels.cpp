// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "main_tab_optional_panels.hpp"
#include "../../../modules/module_registry.hpp"
#include "panels_internal.hpp"

namespace ui::new_ui {

namespace {

enum class MainTabOptionalSectionKind {
    AudioControl,
    WindowButtons,
    InputControl,
    DlssControl,
    DxgiControl,
};

static constexpr MainTabOptionalSectionKind kMainTabOptionalPanelsDrawOrder[] = {
    MainTabOptionalSectionKind::AudioControl,
    MainTabOptionalSectionKind::InputControl, MainTabOptionalSectionKind::DlssControl,
    MainTabOptionalSectionKind::DxgiControl, MainTabOptionalSectionKind::WindowButtons,
};

static constexpr size_t kMainTabOptionalPanelsDrawOrderCount =
    sizeof(kMainTabOptionalPanelsDrawOrder) / sizeof(kMainTabOptionalPanelsDrawOrder[0]);

}  // namespace

void DrawMainTabOptionalPanelsInOrder(display_commander::ui::GraphicsApi api,
                                      display_commander::ui::IImGuiWrapper& imgui,
                                      reshade::api::effect_runtime* runtime) {
    for (size_t oi = 0; oi < kMainTabOptionalPanelsDrawOrderCount; ++oi) {
        const MainTabOptionalSectionKind k = kMainTabOptionalPanelsDrawOrder[oi];
        switch (k) {
            case MainTabOptionalSectionKind::AudioControl:
                if (modules::IsModuleEnabled("audio")) {
                    DrawMainTabOptionalPanelAudioControl(imgui);
                }
                break;
            case MainTabOptionalSectionKind::WindowButtons:
                DrawMainTabOptionalPanelWindowButtons(imgui);
                break;
            case MainTabOptionalSectionKind::InputControl:
                DrawMainTabOptionalPanelInputControl(imgui);
                break;
            case MainTabOptionalSectionKind::DlssControl:
                DrawMainTabOptionalPanelDlssControl(api, imgui);
                break;
            case MainTabOptionalSectionKind::DxgiControl:
                DrawMainTabOptionalPanelDxgiControl(api, imgui);
                break;
        }
    }

    modules::DrawEnabledModulesMainTabInline(imgui, runtime);
}

}  // namespace ui::new_ui
