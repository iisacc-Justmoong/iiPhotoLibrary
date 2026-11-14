#include "PhotoViewModel.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace iiPhotoLibrary::presentation {
struct PhotoLifetime final { PhotoViewModel* owner=nullptr; bool updating=false; std::optional<double> dragZoomStart; };
namespace {
struct UpdateGuard final { std::shared_ptr<PhotoLifetime> life; ~UpdateGuard(){life->updating=false;} };
bool valid(const PhotoEntry& entry) {
    return !entry.id.empty()&&!entry.galleryKey.empty()&&entry.id.size()<=4096&&entry.galleryKey.size()<=4096;
}
}
PhotoRow::PhotoRow(const PhotoEntry& entry,std::weak_ptr<PhotoLifetime> life)
    :id(entry.id),galleryKey(entry.galleryKey),name(entry.name),preview(entry.preview),
     previewRevision(entry.previewRevision),created(entry.created),bytes(entry.bytes),video(entry.video),lifetime_(std::move(life)) {}
bool PhotoRow::apply(const PhotoEntry& entry) {
    auto life=lifetime_.lock();
    if(!life||!life->owner||!active_)return false;
    galleryKey.set(entry.galleryKey);if(!life->owner)return false;
    name.set(entry.name);if(!life->owner)return false;
    preview.set(entry.preview);if(!life->owner)return false;
    previewRevision.set(entry.previewRevision);if(!life->owner)return false;
    created.set(entry.created);if(!life->owner)return false;
    bytes.set(entry.bytes);if(!life->owner)return false;
    video.set(entry.video);return life->owner!=nullptr;
}
void PhotoRow::select() {if(auto life=lifetime_.lock();life&&life->owner&&active_)life->owner->select(id,true);}
void PhotoRow::selectForMenu() {if(auto life=lifetime_.lock();life&&life->owner&&active_)life->owner->select(id,false);}
void PhotoRow::use(PhotoUse operation) {if(auto life=lifetime_.lock();life&&life->owner&&active_)life->owner->use(operation,id);}
void PhotoRow::open(){use(PhotoUse::open);} void PhotoRow::copy(){use(PhotoUse::copy);}
void PhotoRow::duplicate(){use(PhotoUse::duplicate);} void PhotoRow::share(){use(PhotoUse::share);}
void PhotoRow::trash(){use(PhotoUse::trash);}

PhotoViewModel::PhotoViewModel()
    :update([this](const PhotoState& value){return apply(value);}),
     updateFlags([this](const PhotoFlags& value){return applyFlags(value);}),
     change([this](const PhotoChange& value){return applyChange(value);}),
     splice([this](const PhotoSplice& value){return applySplice(value);}),
     reorder([this](const std::vector<std::string>& value){return applyOrder(value);}),
     lifetime_(std::make_shared<PhotoLifetime>()) {lifetime_->owner=this;}
PhotoViewModel::~PhotoViewModel(){lifetime_->owner=nullptr;for(auto& [id,row]:byId_)row->active_=false;}
void PhotoViewModel::setActions(PhotoActions actions){actions_=std::move(actions);}
bool PhotoViewModel::apply(const PhotoState& value){
    const auto life=lifetime_;if(life->updating)return false;
    std::unordered_set<std::string> ids,keys;ids.reserve(value.entries.size());keys.reserve(value.entries.size());
    for(const auto& entry:value.entries)
        if(!valid(entry)||!ids.insert(entry.id).second||!keys.insert(entry.galleryKey).second)return false;
    life->updating=true;UpdateGuard guard{life};
    std::vector<std::shared_ptr<PhotoRow>> next;next.reserve(value.entries.size());
    std::unordered_map<std::string,std::shared_ptr<PhotoRow>> nextById;nextById.reserve(value.entries.size());
    std::unordered_map<std::string,std::string> nextKeys;nextKeys.reserve(value.entries.size());
    for(const auto& entry:value.entries){
        auto found=byId_.find(entry.id);
        auto row=found==byId_.end()?std::shared_ptr<PhotoRow>(new PhotoRow(entry,life)):found->second;
        if(found!=byId_.end()){
            const auto oldSource=row->preview.get(),oldRevision=row->previewRevision.get();
            if(!row->apply(entry))return false;
            auto invalidate=actions_.previewChanged;
            if(invalidate&&!entry.preview.empty()&&oldSource==entry.preview&&oldRevision!=entry.previewRevision){
                invalidate(entry.preview);if(!life->owner)return false;
            }
        }
        next.push_back(row);nextById.emplace(entry.id,row);nextKeys.emplace(entry.galleryKey,entry.id);
    }
    for(auto& [id,row]:byId_)if(!nextById.contains(id))row->active_=false;
    byId_=std::move(nextById);keyToId_=std::move(nextKeys);
    const bool removed=!selectedId.get().empty()&&!byId_.contains(selectedId.get());
    if(removed){selectedId.set("");if(!life->owner)return false;informationOpen.set(false);if(!life->owner)return false;}
    if(!updateSelection(removed||selectedId.get().empty()?nullptr:byId_.at(selectedId.get())))return false;
    if(!assignFlags({value.active,value.busy,value.canShare,value.access,value.status}))return false;
    const bool noRows=next.empty();rows.set(std::move(next));if(!life->owner)return false;
    if(!visibleIds_.empty()){refreshVisibleIds();if(!life->owner)return false;}
    empty.set(noRows);if(!life->owner)return false;if(noRows)awaitingInitial_=true;
    return !visible_||!awaitingInitial_||noRows||revealNewest();
}
bool PhotoViewModel::applyFlags(const PhotoFlags& value){
    auto life=lifetime_;if(life->updating)return false;
    life->updating=true;UpdateGuard guard{life};return assignFlags(value);
}
bool PhotoViewModel::assignFlags(const PhotoFlags& value){
    auto life=lifetime_;
    active.set(value.active);if(!life->owner)return false;
    busy.set(value.busy);if(!life->owner)return false;
    canShare.set(value.canShare);if(!life->owner)return false;
    access.set(value.access);if(!life->owner)return false;
    status.set(value.status);return life->owner!=nullptr;
}
bool PhotoViewModel::applyChange(const PhotoChange& value){
    const auto life=lifetime_;if(life->updating||value.id.empty())return false;
    auto found=byId_.find(value.id);
    if(!value.entry&&found==byId_.end())return true;
    if(value.entry){
        if(value.entry->id!=value.id||!valid(*value.entry))return false;
        auto key=keyToId_.find(value.entry->galleryKey);
        if(key!=keyToId_.end()&&key->second!=value.id)return false;
    }
    life->updating=true;UpdateGuard guard{life};
    if(found!=byId_.end()&&value.entry){
        auto row=found->second;const auto oldKey=row->galleryKey.get();
        const auto oldSource=row->preview.get(),oldRevision=row->previewRevision.get();
        if(!row->apply(*value.entry))return false;
        if(oldKey!=value.entry->galleryKey){
            keyToId_.erase(oldKey);keyToId_[value.entry->galleryKey]=value.id;
            if(!visibleIds_.empty()){refreshVisibleIds();if(!life->owner)return false;}
        }
        if(selectedId.get()==value.id&&!updateSelection(row))return false;
        auto invalidate=actions_.previewChanged;
        if(invalidate&&!value.entry->preview.empty()&&oldSource==value.entry->preview&&oldRevision!=value.entry->previewRevision){
            invalidate(value.entry->preview);if(!life->owner)return false;
        }
        return true;
    }
    auto next=rows.get();
    if(found==byId_.end()){
        auto row=std::shared_ptr<PhotoRow>(new PhotoRow(*value.entry,life));
        next.push_back(row);keyToId_[value.entry->galleryKey]=value.id;byId_[value.id]=std::move(row);
    }else{
        auto row=found->second;row->active_=false;keyToId_.erase(row->galleryKey.get());byId_.erase(found);
        std::erase_if(next,[&](const auto& item){return item==row;});
        if(selectedId.get()==value.id){selectedId.set("");if(!life->owner)return false;informationOpen.set(false);if(!life->owner)return false;if(!updateSelection(nullptr))return false;}
    }
    const bool noRows=next.empty();rows.set(std::move(next));if(!life->owner)return false;
    if(!visibleIds_.empty()){refreshVisibleIds();if(!life->owner)return false;}
    empty.set(noRows);if(!life->owner)return false;if(noRows)awaitingInitial_=true;
    return !visible_||!awaitingInitial_||noRows||revealNewest();
}
bool PhotoViewModel::applySplice(const PhotoSplice& value){
    auto life=lifetime_;if(life->updating)return false;
    const auto& current=rows.get();
    if(value.index>current.size()||value.removedIds.size()>current.size()-value.index)return false;
    for(std::size_t i=0;i<value.removedIds.size();++i)
        if(current[value.index+i]->id!=value.removedIds[i])return false;
    if(value.removedIds.empty()&&value.inserted.empty())return true;

    // Prepare the new indexes and rows before publishing a single structural
    // change. Invalid or duplicate identities leave the current view intact.
    auto nextById=byId_;auto nextKeys=keyToId_;
    std::vector<std::shared_ptr<PhotoRow>> removed;removed.reserve(value.removedIds.size());
    for(std::size_t i=0;i<value.removedIds.size();++i){
        auto row=current[value.index+i];removed.push_back(row);
        nextById.erase(row->id);nextKeys.erase(row->galleryKey.get());
    }
    std::vector<std::shared_ptr<PhotoRow>> inserted;inserted.reserve(value.inserted.size());
    for(const auto& entry:value.inserted){
        if(!valid(entry)||nextById.contains(entry.id)||nextKeys.contains(entry.galleryKey))return false;
        auto row=std::shared_ptr<PhotoRow>(new PhotoRow(entry,life));
        nextById.emplace(entry.id,row);nextKeys.emplace(entry.galleryKey,entry.id);inserted.push_back(std::move(row));
    }
    std::vector<std::shared_ptr<PhotoRow>> next;next.reserve(current.size()-removed.size()+inserted.size());
    next.insert(next.end(),current.begin(),current.begin()+value.index);
    next.insert(next.end(),inserted.begin(),inserted.end());
    next.insert(next.end(),current.begin()+value.index+removed.size(),current.end());
    const bool selectedRemoved=std::ranges::any_of(removed,[&](const auto& row){return row->id==selectedId.get();});
    life->updating=true;UpdateGuard guard{life};
    byId_=std::move(nextById);keyToId_=std::move(nextKeys);
    for(auto& row:removed)row->active_=false;
    if(selectedRemoved){selectedId.set("");if(!life->owner)return false;informationOpen.set(false);if(!life->owner)return false;if(!updateSelection(nullptr))return false;}
    const bool noRows=next.empty();rows.set(std::move(next));if(!life->owner)return false;
    if(!visibleIds_.empty()){refreshVisibleIds();if(!life->owner)return false;}
    empty.set(noRows);if(!life->owner)return false;if(noRows)awaitingInitial_=true;
    return !visible_||!awaitingInitial_||noRows||revealNewest();
}
bool PhotoViewModel::applyOrder(const std::vector<std::string>& ids){
    auto life=lifetime_;if(life->updating||ids.size()!=rows.get().size())return false;
    std::vector<std::shared_ptr<PhotoRow>> next;next.reserve(ids.size());
    std::unordered_set<std::string> seen;seen.reserve(ids.size());
    for(const auto& id:ids){
        const auto found=byId_.find(id);
        if(found==byId_.end()||!seen.insert(id).second)return false;
        next.push_back(found->second);
    }
    life->updating=true;UpdateGuard guard{life};rows.set(std::move(next));return life->owner!=nullptr;
}
void PhotoViewModel::select(std::string_view id,bool showInformation){
    auto life=lifetime_;if(!visible_||life->updating)return;
    auto found=byId_.find(std::string(id));if(found==byId_.end())return;
    life->updating=true;UpdateGuard guard{life};
    auto selected=found->second;auto previous=byId_.find(selectedId.get());
    if(previous!=byId_.end()&&previous->second!=selected){previous->second->selected.set(false);if(!life->owner)return;}
    selectedId.set(selected->id);if(!life->owner)return;
    selected->selected.set(true);if(!life->owner)return;
    if(!updateSelection(selected))return;
    informationOpen.set(showInformation);
}
bool PhotoViewModel::updateSelection(const std::shared_ptr<PhotoRow>& row){
    auto life=lifetime_;
    selectedName.set(row?row->name.get():std::string{});if(!life->owner)return false;
    selectedPreview.set(row?row->preview.get():std::string{});if(!life->owner)return false;
    selectedCreated.set(row?row->created.get():std::string{});if(!life->owner)return false;
    selectedBytes.set(row?row->bytes.get():0);if(!life->owner)return false;
    selectedVideo.set(row&&row->video.get());return life->owner!=nullptr;
}
void PhotoViewModel::connectLibrary(){auto action=actions_.connect;if(visible_&&action&&!busy.get()&&access.get()!="unsupported")action();}
void PhotoViewModel::pickFiles(){auto action=actions_.pickFiles;if(visible_&&action&&active.get()&&!busy.get())action();}
void PhotoViewModel::refresh(){auto action=actions_.refresh;if(visible_&&action&&active.get()&&!busy.get())action();}
bool PhotoViewModel::dropFiles(const std::vector<std::string>& files){
    auto action=actions_.addFiles;
    if(!visible_||!action||!active.get()||busy.get()||files.empty()||files.size()>256||
       std::ranges::any_of(files,[](const auto& file){return file.empty()||file.size()>32768;}))return false;
    action(files);return true;
}
bool PhotoViewModel::pinch(double scale){
    const auto current=thumbnailSize.get();
    if(!visible_||!std::isfinite(scale)||scale<=0||!std::isfinite(current)||current<=0)return false;
    thumbnailSize.set(std::clamp(current*scale,56.,400.));return true;
}
bool PhotoViewModel::dragZoom(double scale,bool first){
    const auto current=thumbnailSize.get();
    if(!visible_||!std::isfinite(scale)||scale<=0||!std::isfinite(current)||current<=0)return false;
    auto life=lifetime_;
    if(first)life->dragZoomStart=current;
    if(!life->dragZoomStart)return false;
    thumbnailSize.set(std::clamp(*life->dragZoomStart*scale,56.,400.));return true;
}
void PhotoViewModel::setViewportSize(double width,double height){
    // Preserve the legacy gallery's 600-point compact breakpoint.
    if(!visible_||!std::isfinite(width)||!std::isfinite(height)||width<=0||height<=0||width>8192||height>8192)return;
    compactLayout.set(width<600);
}
void PhotoViewModel::closeInformation(){informationOpen.set(false);}
void PhotoViewModel::use(PhotoUse operation){
    use(operation,selectedId.get());
}
void PhotoViewModel::use(PhotoUse operation,std::string_view target){
    auto action=actions_.use;const auto id=std::string(target);
    if(!visible_||!action||id.empty()||!byId_.contains(id)||busy.get()||(operation==PhotoUse::share&&!canShare.get()))return;
    auto life=lifetime_;action(operation,id);if(life->owner&&operation==PhotoUse::trash&&selectedId.get()==id)informationOpen.set(false);
}
void PhotoViewModel::openOriginal(){use(PhotoUse::open);} void PhotoViewModel::copy(){use(PhotoUse::copy);}
void PhotoViewModel::duplicate(){use(PhotoUse::duplicate);} void PhotoViewModel::share(){use(PhotoUse::share);}
void PhotoViewModel::trash(){use(PhotoUse::trash);}
void PhotoViewModel::setVisibleKeys(std::vector<std::string> keys){
    if(!visible_&&!keys.empty())return;
    visibleIds_.clear();publishVisible(std::move(keys));
}
void PhotoViewModel::publishVisible(std::vector<std::string> keys){
    if(keys==visibleKeys_)return;
    auto life=lifetime_;auto previous=visibleKeys_;visibleKeys_=std::move(keys);
    auto action=actions_.prioritize;if(action){const auto snapshot=visibleKeys_;
        try{action(snapshot);}catch(...){if(life->owner==this)visibleKeys_=std::move(previous);throw;}}
}
void PhotoViewModel::setVisibleIds(const std::vector<std::string>& ids){
    if(!visible_&&!ids.empty())return;
    visibleIds_=ids;refreshVisibleIds();
}
void PhotoViewModel::refreshVisibleIds(){
    std::vector<std::string> keys;keys.reserve(visibleIds_.size());
    for(const auto& id:visibleIds_)if(auto found=byId_.find(id);found!=byId_.end())keys.push_back(found->second->galleryKey.get());
    publishVisible(std::move(keys));
}
bool PhotoViewModel::revealNewest(){
    if(rows.get().empty())return true;
    auto life=lifetime_;const auto id=rows.get().back()->id;awaitingInitial_=false;
    if(revealKey.get()!=id)revealKey.set(id);
    else {if(revealRevision.get()>=9007199254740991ull)throw std::overflow_error("Photo reveal revision exhausted");revealRevision.set(revealRevision.get()+1);}
    return life->owner!=nullptr;
}
void PhotoViewModel::hide(){auto life=lifetime_;visible_=false;life->dragZoomStart.reset();awaitingInitial_=true;informationOpen.set(false);if(life->owner==this&&!visible_)setVisibleKeys({});}
void PhotoViewModel::show(){if(visible_)return;visible_=true;(void)revealNewest();}
namespace {
std::shared_ptr<PhotoViewModel> requireModel(std::shared_ptr<PhotoViewModel> model){
    if(!model)throw std::invalid_argument("Photo backend needs a view model");return model;
}
}
PhotoBackend::PhotoBackend():PhotoBackend(std::make_shared<PhotoViewModel>()){}
PhotoBackend::PhotoBackend(std::shared_ptr<PhotoViewModel> value)
    :model(requireModel(std::move(value))),rows(model->rows),active(model->active),busy(model->busy),canShare(model->canShare),
     informationOpen(model->informationOpen),empty(model->empty),motionEnabled(model->motionEnabled),compactLayout(model->compactLayout),selectedVideo(model->selectedVideo),selectedBytes(model->selectedBytes),revealRevision(model->revealRevision),
     thumbnailSize(model->thumbnailSize),viewportWidth(model->viewportWidth),viewportHeight(model->viewportHeight),
     access(model->access),status(model->status),selectedId(model->selectedId),selectedName(model->selectedName),
     selectedPreview(model->selectedPreview),selectedCreated(model->selectedCreated),revealKey(model->revealKey){}
} // namespace iiPhotoLibrary::presentation
