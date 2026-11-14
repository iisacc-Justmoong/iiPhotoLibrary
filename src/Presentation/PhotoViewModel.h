#pragma once

#include "PresentationExport.h"
#include <iiSDKCore/Core/Property/Observable.h>
#include <iiSDKCore/Runtime/Query.h>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace iiPhotoLibrary::presentation {

struct PhotoEntry final {
    std::string id, galleryKey, name, preview, previewRevision, created;
    std::uint64_t bytes=0;
    bool video=false;
    bool operator==(const PhotoEntry&) const=default;
};
struct PhotoState final {
    bool active=false, busy=false, canShare=false;
    std::string access, status;
    std::vector<PhotoEntry> entries;
};
struct PhotoFlags final {
    bool active=false, busy=false, canShare=false;
    std::string access, status;
};
struct PhotoChange final {
    std::string id;
    std::optional<PhotoEntry> entry; // null removes the row; a new entry is appended.
};
// One structural change against the current ordering. Removed IDs must match
// the contiguous range at index; inserted entries replace that range.
struct PhotoSplice final {
    std::size_t index=0;
    std::vector<std::string> removedIds;
    std::vector<PhotoEntry> inserted;
};
enum class PhotoUse { open, copy, duplicate, share, trash };
struct PhotoActions final {
    std::function<void()> connect, pickFiles, refresh;
    std::function<void(const std::vector<std::string>&)> addFiles;
    std::function<void(PhotoUse,std::string_view)> use;
    std::function<void(const std::vector<std::string>&)> prioritize;
    std::function<void(std::string_view)> previewChanged;
};
class PhotoViewModel;
struct PhotoLifetime;

class IIPHOTO_PRESENTATION_EXPORT PhotoRow final {
public:
    const std::string id;
    ii::core::Observable<std::string> galleryKey, name, preview, previewRevision, created;
    ii::core::Observable<std::uint64_t> bytes;
    ii::core::Observable<bool> video, selected{false};
    void select();
    void selectForMenu();
    void open(); void copy(); void duplicate(); void share(); void trash();
private:
    friend class PhotoViewModel;
    PhotoRow(const PhotoEntry&,std::weak_ptr<PhotoLifetime>);
    bool apply(const PhotoEntry&);
    void use(PhotoUse);
    std::weak_ptr<PhotoLifetime> lifetime_;
    bool active_=true;
};

// Owner-thread state. Queries are invoked by the host only on change; no timer
// or asynchronous work is created by the presentation model.
class IIPHOTO_PRESENTATION_EXPORT PhotoViewModel final {
public:
    PhotoViewModel();
    ~PhotoViewModel();
    PhotoViewModel(const PhotoViewModel&)=delete;
    PhotoViewModel& operator=(const PhotoViewModel&)=delete;
    void setActions(PhotoActions);
    ii::runtime::QueryPort<PhotoState,bool> update;
    ii::runtime::QueryPort<PhotoFlags,bool> updateFlags;
    ii::runtime::QueryPort<PhotoChange,bool> change;
    ii::runtime::QueryPort<PhotoSplice,bool> splice;
    ii::runtime::QueryPort<std::vector<std::string>,bool> reorder;
    ii::core::Observable<std::vector<std::shared_ptr<PhotoRow>>> rows{{}};
    ii::core::Observable<bool> active{false}, busy{false}, canShare{false}, informationOpen{false}, empty{true}, motionEnabled{true}, compactLayout{false};
    ii::core::Observable<bool> selectedVideo{false};
    ii::core::Observable<std::uint64_t> selectedBytes{0};
    ii::core::Observable<std::uint64_t> revealRevision{0};
    // Zero lets the host layout determine each viewport axis.
    ii::core::Observable<double> thumbnailSize{128}, viewportWidth{0}, viewportHeight{0};
    ii::core::Observable<std::string> access{""}, status{""}, selectedId{""}, selectedName{""}, selectedPreview{""}, selectedCreated{""}, revealKey{""};
    void connectLibrary();
    void pickFiles();
    void refresh();
    bool dropFiles(const std::vector<std::string>& files);
    bool pinch(double scale);
    bool dragZoom(double scale,bool first);
    void setViewportSize(double width,double height);
    void closeInformation();
    void openOriginal();
    void copy();
    void duplicate();
    void share();
    void trash();
    void setVisibleKeys(std::vector<std::string> keys);
    void setVisibleIds(const std::vector<std::string>& ids);
    void hide();
    void show();
private:
    friend class PhotoRow;
    bool apply(const PhotoState&);
    bool applyFlags(const PhotoFlags&);
    bool assignFlags(const PhotoFlags&);
    bool applyChange(const PhotoChange&);
    bool applySplice(const PhotoSplice&);
    bool applyOrder(const std::vector<std::string>&);
    void select(std::string_view,bool showInformation);
    void use(PhotoUse);
    void use(PhotoUse,std::string_view);
    bool updateSelection(const std::shared_ptr<PhotoRow>&);
    bool revealNewest();
    void publishVisible(std::vector<std::string> keys);
    void refreshVisibleIds();
    std::shared_ptr<PhotoLifetime> lifetime_;
    PhotoActions actions_;
    std::unordered_map<std::string,std::shared_ptr<PhotoRow>> byId_;
    std::unordered_map<std::string,std::string> keyToId_;
    std::vector<std::string> visibleKeys_;
    std::vector<std::string> visibleIds_;
    bool visible_=true,awaitingInitial_=true;
};

// LVUI owns this thin adapter; the application may pass the same model to its
// service bridge through the generated backend factory.
class IIPHOTO_PRESENTATION_EXPORT PhotoBackend final {
public:
    PhotoBackend();
    explicit PhotoBackend(std::shared_ptr<PhotoViewModel>);
    std::shared_ptr<PhotoViewModel> model;
    ii::core::Observable<std::vector<std::shared_ptr<PhotoRow>>>& rows;
    ii::core::Observable<bool>& active,&busy,&canShare,&informationOpen,&empty,&motionEnabled,&compactLayout,&selectedVideo;
    ii::core::Observable<std::uint64_t>& selectedBytes,&revealRevision;
    ii::core::Observable<double>& thumbnailSize,&viewportWidth,&viewportHeight;
    ii::core::Observable<std::string>& access,&status,&selectedId,&selectedName,&selectedPreview,&selectedCreated,&revealKey;
    void connectLibrary(){model->connectLibrary();} void pickFiles(){model->pickFiles();} void refresh(){model->refresh();}
    void closeInformation(){model->closeInformation();} void openOriginal(){model->openOriginal();}
    bool dropFiles(const std::vector<std::string>& files){return model->dropFiles(files);}
    bool pinch(double scale){return model->pinch(scale);}
    bool dragZoom(double scale,bool first){return model->dragZoom(scale,first);}
    void viewportSize(double width,double height){model->setViewportSize(width,height);}
    void copy(){model->copy();} void duplicate(){model->duplicate();} void share(){model->share();} void trash(){model->trash();}
    void visibleItems(const std::vector<std::string>& ids){model->setVisibleIds(ids);}
};
} // namespace iiPhotoLibrary::presentation
