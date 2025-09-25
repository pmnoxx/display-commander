#include "new_ui_wrapper.hpp"

namespace ui::new_ui {

bool ShouldUseNewUI() {
    // For now, always return true to use the new UI
    // Later this can be made configurable
    return true;
}

} // namespace ui::new_ui
