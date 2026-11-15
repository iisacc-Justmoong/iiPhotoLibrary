#include "PhotoPresentationC.h"
#include "PhotoViewModel.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace iiPhotoLibrary::presentation;
namespace {
thread_local char lastError[512]{};
void error(const char* value) noexcept { std::snprintf(lastError,sizeof(lastError),"%s",value); }

struct Context final {
    PhotoViewModel model;
    std::thread::id owner=std::this_thread::get_id();
    bool alive=true;
    unsigned callbackDepth=0;
    iiphoto_observer observer=nullptr;
    iiphoto_action action=nullptr;
    void *observerContext=nullptr, *actionContext=nullptr;
    struct RowSubscriptions final {
        std::weak_ptr<PhotoRow> row;
        std::vector<ii::core::ObservableSubscription> subscriptions;
    };
    std::vector<ii::core::ObservableSubscription> subscriptions;
    std::unordered_set<std::string> watchedRows;
    std::unordered_map<std::string,RowSubscriptions> rowSubscriptions;
};
struct CallbackScope final {
    explicit CallbackScope(Context& c):context(c){++context.callbackDepth;}
    ~CallbackScope(){--context.callbackDepth;}
    Context& context;
};
void notify(const std::shared_ptr<Context>& context,int property,const char* row=nullptr) {
    if (!context->alive || !context->observer) return;
    CallbackScope scope(*context);
    const auto observer=context->observer;
    observer(context->observerContext,property,row);
}
template<class T> void watch(const std::shared_ptr<Context>& context,ii::core::Observable<T>& value,
                             int property,std::vector<ii::core::ObservableSubscription>& target,
                             std::string row={}) {
    const std::weak_ptr<Context> weak=context;
    target.push_back(value.observe([weak,property,row=std::move(row)](const T&,std::uint64_t){
        if (auto locked=weak.lock()) notify(locked,property,row.empty()?nullptr:row.c_str());
    }));
}
void synchronizeRows(const std::shared_ptr<Context>& context) {
    if(context->watchedRows.empty()){context->rowSubscriptions.clear();return;}
    std::unordered_set<std::string> current;
    current.reserve(context->watchedRows.size());
    for (const auto& id:context->watchedRows) {
        auto row=context->model.findById(id);
        if(!row)continue;
        current.insert(row->id);
        auto existing=context->rowSubscriptions.find(row->id);
        if (existing!=context->rowSubscriptions.end() && existing->second.row.lock()==row) continue;
        if (existing!=context->rowSubscriptions.end()) context->rowSubscriptions.erase(existing);
        Context::RowSubscriptions subscriptions;subscriptions.row=row;
        auto& target=subscriptions.subscriptions;
        watch(context,row->galleryKey,IIPHOTO_ROW_GALLERY_KEY,target,row->id);
        watch(context,row->name,IIPHOTO_ROW_NAME,target,row->id);
        watch(context,row->preview,IIPHOTO_ROW_PREVIEW,target,row->id);
        watch(context,row->previewRevision,IIPHOTO_ROW_PREVIEW_REVISION,target,row->id);
        watch(context,row->created,IIPHOTO_ROW_CREATED,target,row->id);
        watch(context,row->bytes,IIPHOTO_ROW_BYTES,target,row->id);
        watch(context,row->video,IIPHOTO_ROW_VIDEO,target,row->id);
        watch(context,row->selected,IIPHOTO_ROW_SELECTED,target,row->id);
        context->rowSubscriptions.emplace(row->id,std::move(subscriptions));
    }
    std::erase_if(context->rowSubscriptions,[&](const auto& value){return !current.contains(value.first);});
}
void observe(const std::shared_ptr<Context>& context,iiphoto_observer observer,void* user) {
    context->subscriptions.clear();context->rowSubscriptions.clear();
    context->observer=observer;context->observerContext=user;
    if (!observer) return;
    auto& m=context->model;auto& target=context->subscriptions;
    const std::weak_ptr<Context> weak=context;
    target.push_back(m.rows.observe([weak](const auto&,std::uint64_t){
        if(auto locked=weak.lock();locked && locked->alive && locked->observer){
            synchronizeRows(locked);notify(locked,IIPHOTO_ROWS);
        }
    }));
    watch(context,m.active,IIPHOTO_ACTIVE,target);watch(context,m.busy,IIPHOTO_BUSY,target);
    watch(context,m.canShare,IIPHOTO_CAN_SHARE,target);watch(context,m.informationOpen,IIPHOTO_INFORMATION_OPEN,target);
    watch(context,m.empty,IIPHOTO_EMPTY,target);watch(context,m.motionEnabled,IIPHOTO_MOTION_ENABLED,target);
    watch(context,m.compactLayout,IIPHOTO_COMPACT_LAYOUT,target);watch(context,m.selectedVideo,IIPHOTO_SELECTED_VIDEO,target);
    watch(context,m.selectedBytes,IIPHOTO_SELECTED_BYTES,target);watch(context,m.revealRevision,IIPHOTO_REVEAL_REVISION,target);
    watch(context,m.thumbnailSize,IIPHOTO_THUMBNAIL_SIZE,target);watch(context,m.viewportWidth,IIPHOTO_VIEWPORT_WIDTH,target);
    watch(context,m.viewportHeight,IIPHOTO_VIEWPORT_HEIGHT,target);watch(context,m.access,IIPHOTO_ACCESS,target);
    watch(context,m.status,IIPHOTO_STATUS,target);watch(context,m.selectedId,IIPHOTO_SELECTED_ID,target);
    watch(context,m.selectedName,IIPHOTO_SELECTED_NAME,target);watch(context,m.selectedPreview,IIPHOTO_SELECTED_PREVIEW,target);
    watch(context,m.selectedCreated,IIPHOTO_SELECTED_CREATED,target);watch(context,m.revealKey,IIPHOTO_REVEAL_KEY,target);
    synchronizeRows(context);
}
void sendAction(const std::weak_ptr<Context>& weak,int kind,const std::vector<std::string>& values={}) {
    if (auto context=weak.lock();context && context->alive && context->action) {
        std::vector<const char*> borrowed;borrowed.reserve(values.size());
        for (const auto& value:values) borrowed.push_back(value.c_str());
        CallbackScope scope(*context);const auto action=context->action;
        action(context->actionContext,kind,borrowed.data(),borrowed.size());
    }
}
void setActions(const std::shared_ptr<Context>& context,iiphoto_action callback,void* user) {
    context->action=callback;context->actionContext=user;
    if (!callback) {context->model.setActions({});return;}
    const std::weak_ptr<Context> weak=context;
    PhotoActions actions;
    actions.connect=[weak]{sendAction(weak,IIPHOTO_ACTION_CONNECT);};
    actions.pickFiles=[weak]{sendAction(weak,IIPHOTO_ACTION_PICK_FILES);};
    actions.refresh=[weak]{sendAction(weak,IIPHOTO_ACTION_REFRESH);};
    actions.addFiles=[weak](const auto& values){sendAction(weak,IIPHOTO_ACTION_ADD_FILES,values);};
    actions.prioritize=[weak](const auto& values){sendAction(weak,IIPHOTO_ACTION_PRIORITIZE,values);};
    actions.previewChanged=[weak](std::string_view value){sendAction(weak,IIPHOTO_ACTION_PREVIEW_CHANGED,{std::string(value)});};
    actions.use=[weak](PhotoUse operation,std::string_view id){
        int kind;
        switch(operation){
        case PhotoUse::open:kind=IIPHOTO_ACTION_OPEN;break;case PhotoUse::copy:kind=IIPHOTO_ACTION_COPY;break;
        case PhotoUse::duplicate:kind=IIPHOTO_ACTION_DUPLICATE;break;case PhotoUse::share:kind=IIPHOTO_ACTION_SHARE;break;
        case PhotoUse::trash:kind=IIPHOTO_ACTION_TRASH;break;default:throw std::invalid_argument("unknown photo action");
        }
        sendAction(weak,kind,{std::string(id)});
    };
    context->model.setActions(std::move(actions));
}
template<class T> decltype(auto) requireReply(T&& reply) {
    if(!reply){if(reply.error().cause)std::rethrow_exception(reply.error().cause);throw std::runtime_error("photo query rejected");}
    return *std::forward<T>(reply);
}
bool boolean(int value) {
    if(value!=0 && value!=1)throw std::invalid_argument("invalid Boolean");return value!=0;
}
PhotoFlags flags(const iiphoto_flags* value) {
    if(!value||!value->access||!value->status)throw std::invalid_argument("invalid photo flags");
    return {boolean(value->active),boolean(value->busy),boolean(value->can_share),value->access,value->status};
}
PhotoEntry entry(const iiphoto_entry& value) {
    if(!value.id||!value.gallery_key||!value.name||!value.preview||!value.preview_revision||!value.created)
        throw std::invalid_argument("invalid photo entry");
    return {value.id,value.gallery_key,value.name,value.preview,value.preview_revision,value.created,value.bytes,boolean(value.video)};
}
std::vector<std::string> list(const char*const* values,size_t count) {
    if(count && !values)throw std::invalid_argument("missing string array");
    std::vector<std::string> result;result.reserve(count);
    for(size_t i=0;i<count;++i){if(!values[i])throw std::invalid_argument("missing string value");result.emplace_back(values[i]);}
    return result;
}
void copyText(const std::string& value,char* buffer,size_t capacity,size_t* length) {
    if(!length)throw std::invalid_argument("missing text length");*length=value.size();
    if(!buffer && capacity==0)return;
    if(!buffer || capacity<=value.size())throw std::length_error("photo text buffer too small");
    std::memcpy(buffer,value.c_str(),value.size()+1);
}
std::shared_ptr<PhotoRow> row(Context& context,size_t index) {
    const auto& rows=context.model.rows.get();if(index>=rows.size())throw std::out_of_range("photo row index");return rows[index];
}
}

struct iiphoto_presentation {std::shared_ptr<Context> impl;};
struct iiphoto_model_lease {std::shared_ptr<Context> impl;};
namespace {
std::shared_ptr<Context> checked(iiphoto_presentation* handle){
    if(!handle||!handle->impl||!handle->impl->alive)throw std::invalid_argument("photo model is closed");
    auto context=handle->impl;
    if(context->owner!=std::this_thread::get_id())throw std::logic_error("photo model requires its owner thread");
    return context;
}
template<class F> int invoke(iiphoto_presentation* handle,F&& function) noexcept {
    try {auto context=checked(handle);function(context);return 0;}
    catch(const std::exception& exception){error(exception.what());}catch(...){error("unknown photo presentation error");}
    return -1;
}
}
extern "C" {
const char* iiphoto_last_error(void){return lastError;}
iiphoto_presentation* iiphoto_create(void){
    try{return new iiphoto_presentation{std::make_shared<Context>()};}
    catch(const std::exception& e){error(e.what());}catch(...){error("cannot create photo model");}return nullptr;
}
int iiphoto_destroy(iiphoto_presentation* h){
    if(!h)return 0;
    try{auto c=checked(h);c->alive=false;c->observer=nullptr;c->action=nullptr;delete h;return 0;}
    catch(const std::exception& e){error(e.what());}catch(...){error("cannot destroy photo model");}return -1;
}
int iiphoto_observe(iiphoto_presentation* h,iiphoto_observer cb,void* user){return invoke(h,[&](auto c){observe(c,cb,user);});}
int iiphoto_watch_rows(iiphoto_presentation* h,const char*const* ids,size_t count){
    return invoke(h,[&](auto c){const auto values=list(ids,count);std::unordered_set<std::string> selected;
        selected.reserve(values.size());for(const auto& value:values){
            if(value.empty()||value.size()>4096)throw std::invalid_argument("invalid watched photo ID");selected.insert(value);
        }
        c->watchedRows=std::move(selected);if(c->observer)synchronizeRows(c);
    });
}
int iiphoto_set_actions(iiphoto_presentation* h,iiphoto_action cb,void* user){return invoke(h,[&](auto c){setActions(c,cb,user);});}
int iiphoto_update(iiphoto_presentation* h,const iiphoto_flags* f,const iiphoto_entry* values,size_t count,int* accepted){
    return invoke(h,[&](auto c){if(!accepted||(count&&!values))throw std::invalid_argument("invalid photo update");
        const auto state=flags(f);PhotoState full{state.active,state.busy,state.canShare,state.access,state.status,{}};
        full.entries.reserve(count);for(size_t i=0;i<count;++i)full.entries.push_back(entry(values[i]));
        *accepted=requireReply(c->model.update.send(full));});
}
int iiphoto_update_flags(iiphoto_presentation* h,const iiphoto_flags* f,int* accepted){
    return invoke(h,[&](auto c){if(!accepted)throw std::invalid_argument("missing update result");*accepted=requireReply(c->model.updateFlags.send(flags(f)));});
}
int iiphoto_change(iiphoto_presentation* h,const char* id,const iiphoto_entry* value,int* accepted){
    return invoke(h,[&](auto c){if(!id||!accepted)throw std::invalid_argument("invalid photo change");
        PhotoChange change{id,value?std::optional<PhotoEntry>{entry(*value)}:std::nullopt};*accepted=requireReply(c->model.change.send(change));});
}
int iiphoto_reorder(iiphoto_presentation* h,const char*const* ids,size_t count,int* accepted){
    return invoke(h,[&](auto c){if(!accepted)throw std::invalid_argument("missing reorder result");*accepted=requireReply(c->model.reorder.send(list(ids,count)));});
}
int iiphoto_visible_ids(iiphoto_presentation* h,const char*const* ids,size_t count){return invoke(h,[&](auto c){c->model.setVisibleIds(list(ids,count));});}
int iiphoto_drop_files(iiphoto_presentation* h,const char*const* files,size_t count,int* accepted){
    return invoke(h,[&](auto c){if(!accepted)throw std::invalid_argument("missing drop result");*accepted=c->model.dropFiles(list(files,count));});
}
int iiphoto_select(iiphoto_presentation* h,const char* id,int information){
    return invoke(h,[&](auto c){if(!id)throw std::invalid_argument("missing photo ID");const bool info=boolean(information);
        if(!c->model.selectById(id,info))throw std::invalid_argument("unknown photo ID");});
}
int iiphoto_command(iiphoto_presentation* h,int command){return invoke(h,[&](auto c){auto& m=c->model;
    switch(command){case IIPHOTO_CONNECT:m.connectLibrary();break;case IIPHOTO_PICK_FILES:m.pickFiles();break;
    case IIPHOTO_REFRESH:m.refresh();break;case IIPHOTO_CLOSE_INFORMATION:m.closeInformation();break;
    case IIPHOTO_OPEN:m.openOriginal();break;case IIPHOTO_COPY:m.copy();break;case IIPHOTO_DUPLICATE:m.duplicate();break;
    case IIPHOTO_SHARE:m.share();break;case IIPHOTO_TRASH:m.trash();break;case IIPHOTO_HIDE:m.hide();break;
    case IIPHOTO_SHOW:m.show();break;default:throw std::invalid_argument("unknown photo command");}});}
int iiphoto_resize(iiphoto_presentation* h,double width,double height){return invoke(h,[&](auto c){c->model.setViewportSize(width,height);});}
int iiphoto_pinch(iiphoto_presentation* h,double scale,int* accepted){return invoke(h,[&](auto c){if(!accepted)throw std::invalid_argument("missing pinch result");*accepted=c->model.pinch(scale);});}
int iiphoto_count(iiphoto_presentation* h,size_t* value){return invoke(h,[&](auto c){if(!value)throw std::invalid_argument("missing row count");*value=c->model.rows.get().size();});}
int iiphoto_text(iiphoto_presentation* h,int property,char* buffer,size_t capacity,size_t* length){return invoke(h,[&](auto c){auto& m=c->model;std::string value;
    switch(property){case IIPHOTO_ACCESS:value=m.access.get();break;case IIPHOTO_STATUS:value=m.status.get();break;
    case IIPHOTO_SELECTED_ID:value=m.selectedId.get();break;case IIPHOTO_SELECTED_NAME:value=m.selectedName.get();break;
    case IIPHOTO_SELECTED_PREVIEW:value=m.selectedPreview.get();break;case IIPHOTO_SELECTED_CREATED:value=m.selectedCreated.get();break;
    case IIPHOTO_REVEAL_KEY:value=m.revealKey.get();break;default:throw std::invalid_argument("unknown photo text");}copyText(value,buffer,capacity,length);});}
int iiphoto_boolean(iiphoto_presentation* h,int property,int* value){return invoke(h,[&](auto c){if(!value)throw std::invalid_argument("missing Boolean result");auto& m=c->model;
    switch(property){case IIPHOTO_ACTIVE:*value=m.active.get();break;case IIPHOTO_BUSY:*value=m.busy.get();break;
    case IIPHOTO_CAN_SHARE:*value=m.canShare.get();break;case IIPHOTO_INFORMATION_OPEN:*value=m.informationOpen.get();break;
    case IIPHOTO_EMPTY:*value=m.empty.get();break;case IIPHOTO_MOTION_ENABLED:*value=m.motionEnabled.get();break;
    case IIPHOTO_COMPACT_LAYOUT:*value=m.compactLayout.get();break;case IIPHOTO_SELECTED_VIDEO:*value=m.selectedVideo.get();break;
    default:throw std::invalid_argument("unknown photo Boolean");}});}
int iiphoto_unsigned(iiphoto_presentation* h,int property,uint64_t* value){return invoke(h,[&](auto c){if(!value)throw std::invalid_argument("missing integer result");
    if(property==IIPHOTO_SELECTED_BYTES)*value=c->model.selectedBytes.get();else if(property==IIPHOTO_REVEAL_REVISION)*value=c->model.revealRevision.get();
    else throw std::invalid_argument("unknown photo integer");});}
int iiphoto_number(iiphoto_presentation* h,int property,double* value){return invoke(h,[&](auto c){if(!value)throw std::invalid_argument("missing number result");
    if(property==IIPHOTO_THUMBNAIL_SIZE)*value=c->model.thumbnailSize.get();else if(property==IIPHOTO_VIEWPORT_WIDTH)*value=c->model.viewportWidth.get();
    else if(property==IIPHOTO_VIEWPORT_HEIGHT)*value=c->model.viewportHeight.get();else throw std::invalid_argument("unknown photo number");});}
int iiphoto_row_text(iiphoto_presentation* h,size_t index,int property,char* buffer,size_t capacity,size_t* length){return invoke(h,[&](auto c){auto r=row(*c,index);std::string value;
    switch(property){case IIPHOTO_ROW_ID:value=r->id;break;case IIPHOTO_ROW_GALLERY_KEY:value=r->galleryKey.get();break;
    case IIPHOTO_ROW_NAME:value=r->name.get();break;case IIPHOTO_ROW_PREVIEW:value=r->preview.get();break;
    case IIPHOTO_ROW_PREVIEW_REVISION:value=r->previewRevision.get();break;case IIPHOTO_ROW_CREATED:value=r->created.get();break;
    default:throw std::invalid_argument("unknown photo row text");}copyText(value,buffer,capacity,length);});}
int iiphoto_row_boolean(iiphoto_presentation* h,size_t index,int property,int* value){return invoke(h,[&](auto c){if(!value)throw std::invalid_argument("missing row Boolean");auto r=row(*c,index);
    if(property==IIPHOTO_ROW_VIDEO)*value=r->video.get();else if(property==IIPHOTO_ROW_SELECTED)*value=r->selected.get();else throw std::invalid_argument("unknown photo row Boolean");});}
int iiphoto_row_unsigned(iiphoto_presentation* h,size_t index,int property,uint64_t* value){return invoke(h,[&](auto c){if(!value||property!=IIPHOTO_ROW_BYTES)throw std::invalid_argument("unknown photo row integer");*value=row(*c,index)->bytes.get();});}
int iiphoto_acquire_model(iiphoto_presentation* h,iiphoto_model_lease** lease){return invoke(h,[&](auto c){if(!lease)throw std::invalid_argument("missing lease result");*lease=new iiphoto_model_lease{c};});}
int iiphoto_lease_model(iiphoto_model_lease* lease,void** model){
    try{if(!lease||!lease->impl||!model)throw std::invalid_argument("invalid photo model lease");
        if(lease->impl->owner!=std::this_thread::get_id())throw std::logic_error("photo model lease requires its owner thread");
        *model=&lease->impl->model;return 0;}catch(const std::exception& e){error(e.what());}catch(...){error("cannot read photo model lease");}return -1;
}
int iiphoto_release_model(iiphoto_model_lease* lease){
    if(!lease)return 0;try{if(!lease->impl)throw std::invalid_argument("invalid photo model lease");
        if(lease->impl->owner!=std::this_thread::get_id())throw std::logic_error("photo model lease requires its owner thread");
        if(lease->impl->callbackDepth)throw std::logic_error("release photo lease after callbacks return");
        delete lease;return 0;}catch(const std::exception& e){error(e.what());}catch(...){error("cannot release photo model lease");}return -1;
}
}
