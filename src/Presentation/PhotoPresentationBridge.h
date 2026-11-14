#pragma once

#include "PresentationBridgeExport.h"
#include <QObject>
#include <QPointer>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace iiPhotoLibrary { class PhotoController; }
namespace iiPhotoLibrary::presentation {
class PhotoViewModel;

// Optional adapter for the existing Qt photo service. The native presentation
// model and LVUI declaration do not depend on Qt or this adapter.
class IIPHOTO_BRIDGE_EXPORT PhotoPresentationBridge final : public QObject {
public:
    PhotoPresentationBridge(PhotoController*,std::shared_ptr<PhotoViewModel>,
        std::function<void()> pickFiles={},std::function<void(std::string_view)> invalidatePreview={},QObject* parent=nullptr);
    bool reconcile();
    void addFiles(const std::vector<std::string>& urls);
private:
    struct PendingRemoval final { int first=0; std::vector<std::string> ids; };
    void scheduleReconcile();
    void scheduleFlags();
    bool publishFlags();
    QPointer<PhotoController> controller_;
    std::weak_ptr<PhotoViewModel> model_;
    std::function<void()> pickFiles_;
    std::function<void(std::string_view)> invalidatePreview_;
    std::optional<PendingRemoval> pendingRemoval_;
    bool scheduled_=false,flagsScheduled_=false;
};
} // namespace iiPhotoLibrary::presentation
