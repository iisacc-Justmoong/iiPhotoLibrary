#include "PhotoPresentationBridge.h"
#include "PhotoViewModel.h"
#include <iiPhotoLibrary/PhotoController.h>
#include <QAbstractItemModel>
#include <QDir>
#include <QMetaObject>
#include <QThread>
#include <QUrl>
#include <limits>
#include <stdexcept>
#include <utility>

namespace iiPhotoLibrary::presentation {
namespace {
std::string utf8(const QString& value){const auto bytes=value.toUtf8();return {bytes.constData(),std::size_t(bytes.size())};}
QString qstring(std::string_view value){return QString::fromUtf8(value.data(),qsizetype(value.size()));}
PhotoEntry entry(const QVariant& data){
    const auto values=data.toMap();PhotoEntry result;
    result.id=utf8(values.value("id").toString());result.galleryKey=utf8(values.value("galleryKey").toString());
    result.name=utf8(values.value("name").toString());result.created=utf8(values.value("created").toString());
    result.previewRevision=utf8(values.value("previewStamp").toString());
    if(!result.previewRevision.empty())result.preview=utf8(values.value("preview").toUrl().toString(QUrl::FullyEncoded));
    result.video=values.value("media").toString()=="video";
    for(const auto& resource:values.value("resources").toList()){
        const auto bytes=resource.toMap().value("size").toLongLong();
        if(bytes>0){const auto amount=std::uint64_t(bytes);result.bytes=amount>UINT64_MAX-result.bytes?UINT64_MAX:result.bytes+amount;}
    }
    return result;
}
PhotoFlags flags(PhotoController& controller){
    return {.active=controller.active(),.busy=controller.busy(),.canShare=controller.canShare(),
            .access=utf8(controller.access()),.status=utf8(controller.status())};
}
}
PhotoPresentationBridge::PhotoPresentationBridge(PhotoController* controller,std::shared_ptr<PhotoViewModel> model,
        std::function<void()> picker,std::function<void(std::string_view)> invalidate,QObject* parent)
    :QObject(parent),controller_(controller),model_(model),pickFiles_(std::move(picker)),invalidatePreview_(std::move(invalidate)){
    if(!controller||!model||controller->thread()!=QThread::currentThread()||thread()!=QThread::currentThread())
        throw std::invalid_argument("Photo presentation requires an owner-thread controller and model");
    (void)model->rows.revision();const QPointer<PhotoPresentationBridge> self(this);
    PhotoActions actions;
    actions.connect=[self]{if(self&&self->controller_)self->controller_->connectLibrary();};
    actions.pickFiles=[self]{if(self){auto action=self->pickFiles_;if(action)action();}};
    actions.refresh=[self]{if(self&&self->controller_)self->controller_->refresh();};
    actions.addFiles=[self](const auto& files){if(self)self->addFiles(files);};
    actions.use=[self](PhotoUse operation,std::string_view id){
        if(!self||!self->controller_)return;auto* controller=self->controller_.data();const auto value=qstring(id);
        switch(operation){case PhotoUse::open:controller->openPhoto(value);break;case PhotoUse::copy:controller->copyPhoto(value);break;
        case PhotoUse::duplicate:controller->duplicatePhoto(value);break;case PhotoUse::share:controller->sharePhoto(value);break;
        case PhotoUse::trash:controller->trashPhoto(value);break;}
    };
    actions.prioritize=[self](const auto& keys){
        if(!self||!self->controller_)return;QStringList values;values.reserve(qsizetype(keys.size()));
        for(const auto& key:keys)values.push_back(qstring(key));self->controller_->prioritizeVisible(values);
    };
    actions.previewChanged=[self](std::string_view source){if(self){auto action=self->invalidatePreview_;if(action)action(source);}};
    model->setActions(std::move(actions));
    connect(controller,&PhotoController::changed,this,[this]{scheduleFlags();});
    const auto weak=model_;
    connect(controller,&QObject::destroyed,this,[weak]{if(auto owner=weak.lock())(void)owner->update.send({});});
    auto* gallery=controller->galleryModel();
    connect(gallery,&QAbstractItemModel::modelReset,this,[this]{pendingRemoval_.reset();scheduleReconcile();});
    connect(gallery,&QAbstractItemModel::rowsAboutToBeRemoved,this,[this,gallery](const QModelIndex& parent,int first,int last){
        if(scheduled_)return;
        auto owner=model_.lock();if(!owner)return;
        const auto& rows=owner->rows.get();
        if(parent.isValid()||first<0||last<first||std::size_t(last)>=rows.size()||
           rows.size()!=std::size_t(gallery->rowCount())||pendingRemoval_){
            pendingRemoval_.reset();scheduleReconcile();return;
        }
        PendingRemoval pending;pending.first=first;pending.ids.reserve(std::size_t(last-first+1));
        for(int index=first;index<=last;++index){
            const auto id=utf8(gallery->data(gallery->index(index,0),Qt::UserRole).toMap().value("id").toString());
            if(id!=rows[std::size_t(index)]->id){pendingRemoval_.reset();scheduleReconcile();return;}
            pending.ids.push_back(id);
        }
        pendingRemoval_=std::move(pending);
    });
    connect(gallery,&QAbstractItemModel::rowsRemoved,this,[this](const QModelIndex& parent,int first,int last){
        if(scheduled_){pendingRemoval_.reset();return;}
        auto owner=model_.lock();if(!owner)return;
        if(parent.isValid()||!pendingRemoval_||pendingRemoval_->first!=first||last<first||
           pendingRemoval_->ids.size()!=std::size_t(last-first+1)){
            pendingRemoval_.reset();scheduleReconcile();return;
        }
        auto removed=std::move(pendingRemoval_->ids);pendingRemoval_.reset();
        const QPointer<PhotoPresentationBridge> self(this);
        const auto result=owner->splice.send({.index=std::size_t(first),.removedIds=std::move(removed)});
        if(self&&(!result||!*result))self->scheduleReconcile();
    });
    connect(gallery,&QAbstractItemModel::rowsInserted,this,[this,gallery](const QModelIndex& parent,int first,int last){
        if(scheduled_)return;auto owner=model_.lock();if(!owner)return;
        const auto& rows=owner->rows.get();
        if(parent.isValid()||first<0||last<first||last>=gallery->rowCount()||pendingRemoval_||
           std::size_t(first)>rows.size()||rows.size()+std::size_t(last-first+1)!=std::size_t(gallery->rowCount())){
            scheduleReconcile();return;
        }
        const auto idAt=[gallery](int index){return utf8(gallery->data(gallery->index(index,0),Qt::UserRole).toMap().value("id").toString());};
        if((first>0&&idAt(first-1)!=rows[std::size_t(first-1)]->id)||
           (std::size_t(first)<rows.size()&&idAt(last+1)!=rows[std::size_t(first)]->id)){
            scheduleReconcile();return;
        }
        PhotoSplice changes{.index=std::size_t(first)};changes.inserted.reserve(std::size_t(last-first+1));
        for(int index=first;index<=last;++index)changes.inserted.push_back(entry(gallery->data(gallery->index(index,0),Qt::UserRole)));
        const QPointer<PhotoPresentationBridge> self(this);
        const auto result=owner->splice.send(changes);
        if(self&&(!result||!*result))self->scheduleReconcile();
    });
    connect(gallery,&QAbstractItemModel::layoutChanged,this,[this,gallery]{
        if(scheduled_)return;auto owner=model_.lock();if(!owner)return;
        if(pendingRemoval_){pendingRemoval_.reset();scheduleReconcile();return;}
        std::vector<std::string> ids;ids.reserve(std::size_t(gallery->rowCount()));
        for(int index=0;index<gallery->rowCount();++index)
            ids.push_back(utf8(gallery->data(gallery->index(index,0),Qt::UserRole).toMap().value("id").toString()));
        const QPointer<PhotoPresentationBridge> self(this);
        const auto result=owner->reorder.send(ids);
        if(self&&(!result||!*result))self->scheduleReconcile();
    });
    connect(gallery,&QAbstractItemModel::rowsMoved,this,[this]{pendingRemoval_.reset();scheduleReconcile();});
    connect(gallery,&QAbstractItemModel::dataChanged,this,[this,gallery](const QModelIndex& first,const QModelIndex& last){
        if(scheduled_)return;auto owner=model_.lock();if(!owner)return;
        const QPointer<PhotoPresentationBridge> self(this);
        const QPointer<QAbstractItemModel> guardedGallery(gallery);
        for(int index=first.row();index<=last.row();++index){
            if(!self||!guardedGallery)return;
            auto current=entry(guardedGallery->data(guardedGallery->index(index,0),Qt::UserRole));
            const auto result=owner->change.send({.id=current.id,.entry=current});
            if(!self)return;
            if(!result||!*result){self->scheduleReconcile();return;}
        }
    });
    if(!reconcile())throw std::invalid_argument("Could not reconcile photo presentation");
}
bool PhotoPresentationBridge::reconcile(){
    auto model=model_.lock();auto* controller=controller_.data();if(!model||!controller)return false;
    const auto current=flags(*controller);PhotoState state{.active=current.active,.busy=current.busy,.canShare=current.canShare,
        .access=current.access,.status=current.status};
    auto* gallery=controller->galleryModel();state.entries.reserve(std::size_t(gallery->rowCount()));
    for(int row=0;row<gallery->rowCount();++row)state.entries.push_back(entry(gallery->data(gallery->index(row,0),Qt::UserRole)));
    const auto result=model->update.send(state);return result&&*result;
}
bool PhotoPresentationBridge::publishFlags(){
    auto model=model_.lock();auto* controller=controller_.data();if(!model||!controller)return false;
    const auto result=model->updateFlags.send(flags(*controller));return result&&*result;
}
void PhotoPresentationBridge::scheduleReconcile(){
    if(scheduled_)return;scheduled_=true;
    if(!QMetaObject::invokeMethod(this,[this]{scheduled_=false;(void)reconcile();},Qt::QueuedConnection)){
        scheduled_=false;(void)reconcile();
    }
}
void PhotoPresentationBridge::scheduleFlags(){
    if(flagsScheduled_)return;flagsScheduled_=true;
    if(!QMetaObject::invokeMethod(this,[this]{flagsScheduled_=false;(void)publishFlags();},Qt::QueuedConnection)){
        flagsScheduled_=false;(void)publishFlags();
    }
}
void PhotoPresentationBridge::addFiles(const std::vector<std::string>& urls){
    auto* controller=controller_.data();if(!controller||!controller->active()||controller->busy())return;
    QList<QUrl> values;values.reserve(qsizetype(urls.size()));
    for(const auto& value:urls){
        const auto text=qstring(value);const auto url=QUrl::fromEncoded(QByteArray(value.data(),qsizetype(value.size())));
        if(url.isLocalFile())values.push_back(url);
        else if(QDir::isAbsolutePath(text))values.push_back(QUrl::fromLocalFile(text));
    }
    if(!values.isEmpty())controller->addFiles(values);
}
} // namespace iiPhotoLibrary::presentation
