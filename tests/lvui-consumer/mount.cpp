#include "PhotosView.generated.h"
#include <iiGUI/Host.h>

#if defined(__ANDROID__)
__attribute__((used, retain))
#endif
ii::core::Result<ii::gui::WidgetMount> compile_photo_host_mount(
    ii::gui::Host& host, const lvui::native::BackendFactories& factories) {
    return lvui_generated::PhotosView::mount(host, {}, nullptr, factories);
}
