#include <iiPhotoLibrary/PhotoViewModel.h>

#include <cmath>
#include <memory>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace iiPhotoLibrary::presentation;
#define CHECK(expression) do { if (!(expression)) throw std::runtime_error(#expression); } while (false)

int main() {
    auto model=std::make_shared<PhotoViewModel>();
    CHECK(model->viewportWidth.get()==0&&model->viewportHeight.get()==0);
    model->setViewportSize(320,844);CHECK(model->compactLayout.get());
    model->setViewportSize(900,720);CHECK(!model->compactLayout.get());
    std::vector<std::string> actions;
    std::vector<std::vector<std::string>> priorities;
    std::vector<std::string> dropped;
    std::vector<std::string> invalidated;
    PhotoActions handlers;
    handlers.connect=[&]{actions.emplace_back("connect");};
    handlers.pickFiles=[&]{actions.emplace_back("pick");};
    handlers.refresh=[&]{actions.emplace_back("refresh");};
    handlers.addFiles=[&](const auto& paths){dropped=paths;};
    handlers.use=[&](PhotoUse use,std::string_view id){actions.emplace_back(std::to_string(int(use))+":"+std::string(id));};
    handlers.prioritize=[&](const auto& keys){priorities.push_back(keys);};
    handlers.previewChanged=[&](std::string_view source){invalidated.emplace_back(source);};
    model->setActions(std::move(handlers));
    PhotoState state{.active=true,.canShare=true,.access="authorized",.status="Ready",
        .entries={{.id="one",.galleryKey="key1",.name="One",.preview="file:///one.jpg",.previewRevision="1"},
                  {.id="two",.galleryKey="key2",.name="Two",.video=true}}};
    auto accepted=model->update.send(state); CHECK(accepted&&*accepted&&model->rows.get().size()==2);
    CHECK(model->pinch(2.)&&model->thumbnailSize.get()==256.);
    CHECK(model->pinch(10.)&&model->thumbnailSize.get()==400.);
    CHECK(model->pinch(.01)&&model->thumbnailSize.get()==56.);
    CHECK(!model->pinch((std::numeric_limits<double>::quiet_NaN)())&&model->thumbnailSize.get()==56.);
    CHECK(!model->dragZoom(2.,false)&&model->thumbnailSize.get()==56.);
    model->thumbnailSize.set(390.);
    CHECK(model->dragZoom(2.,true)&&model->thumbnailSize.get()==400.);
    CHECK(model->dragZoom(std::exp2(80./180.),false)&&model->thumbnailSize.get()==400.);
    CHECK(model->dragZoom(std::exp2(-.1),false)&&std::abs(model->thumbnailSize.get()-390.*std::exp2(-.1))<.001);
    model->thumbnailSize.set(128.);
    CHECK(model->revealKey.get()=="two");
    CHECK(model->dropFiles({"/tmp/photo.jpg"})&&dropped==std::vector<std::string>{"/tmp/photo.jpg"});
    { PhotoBackend backend(model);CHECK(backend.rows.get().size()==2&&!backend.empty.get()); }
    auto first=model->rows.get()[0];auto second=model->rows.get()[1];
    const auto rowsRevision=model->rows.revision();
    second->selectForMenu();
    CHECK(model->selectedId.get()=="two"&&second->selected.get()&&!model->informationOpen.get());
    second->copy();CHECK(actions==std::vector<std::string>{"1:two"});actions.clear();
    first->select(); CHECK(model->selectedId.get()=="one"&&model->informationOpen.get()&&first->selected.get());
    CHECK(model->selectedName.get()=="One"&&model->selectedPreview.get()=="file:///one.jpg");
    auto changed=state.entries[0];changed.previewRevision="2";changed.name="Renamed";
    auto updated=model->change.send({.id="one",.entry=changed});CHECK(updated&&*updated);
    CHECK(model->rows.revision()==rowsRevision&&model->rows.get()[0]==first&&model->rows.get()[1]==second);
    CHECK(first->name.get()=="Renamed"&&first->previewRevision.get()=="2");
    CHECK(model->selectedName.get()=="Renamed"&&invalidated==std::vector<std::string>{"file:///one.jpg"});
    model->setVisibleKeys({"key1","key2"});model->setVisibleKeys({"key1","key2"});
    CHECK(priorities.size()==1&&priorities[0].size()==2);
    PhotoBackend(model).visibleItems({"two"});CHECK(priorities.size()==2&&priorities.back()==std::vector<std::string>{"key2"});
    auto movedKey=state.entries[1];movedKey.galleryKey="key2-new";
    auto keyChange=model->change.send({.id="two",.entry=movedKey});CHECK(keyChange&&*keyChange);
    CHECK(priorities.size()==3&&priorities.back()==std::vector<std::string>{"key2-new"});
    state.entries[1]=movedKey;
    model->openOriginal();model->copy();model->duplicate();model->share();model->trash();
    CHECK(actions.size()==5&&actions[0]=="0:one"&&actions[4]=="4:one");
    auto flags=model->updateFlags.send({.active=true,.busy=true,.canShare=true,.access="authorized",.status="Busy"});
    CHECK(flags&&*flags&&model->busy.get()&&model->rows.revision()==rowsRevision);
    CHECK(!model->dropFiles({"/tmp/other.jpg"})&&dropped==std::vector<std::string>{"/tmp/photo.jpg"});
    state.busy=true;state.entries[0]=changed;accepted=model->update.send(state);CHECK(accepted&&*accepted);
    model->copy();CHECK(actions.size()==5);
    state.busy=false;state.canShare=false;accepted=model->update.send(state);CHECK(accepted&&*accepted);
    model->share();second->share();CHECK(actions.size()==5);
    auto removed=model->change.send({.id="one"});CHECK(removed&&*removed);
    CHECK(model->selectedId.get().empty()&&!model->informationOpen.get());
    first->select();CHECK(model->selectedId.get().empty());
    first->copy();CHECK(actions.size()==5);
    auto bad=state;bad.entries.push_back(bad.entries[0]);accepted=model->update.send(bad);CHECK(accepted&&!*accepted);
    CHECK(model->rows.get().size()==1&&model->rows.get()[0]==second);
    const auto reveal=model->revealRevision.get();model->hide();CHECK(priorities.size()==4&&priorities.back().empty());
    const auto actionsBeforeHidden=actions.size();second->select();second->copy();
    CHECK(!model->pinch(2.)&&model->thumbnailSize.get()==128.);
    CHECK(!model->dragZoom(2.,true)&&model->thumbnailSize.get()==128.);
    PhotoBackend(model).visibleItems({"two"});
    CHECK(model->selectedId.get().empty()&&actions.size()==actionsBeforeHidden&&priorities.back().empty());
    model->show();CHECK(model->revealKey.get()=="two"&&model->revealRevision.get()==reveal+1);
    model.reset();second->select();
    PhotoViewModel retry;int deliveries=0;PhotoActions retryActions;
    retryActions.prioritize=[&](const auto&){if(++deliveries==1)throw std::runtime_error("temporary");};
    retry.setActions(std::move(retryActions));
    try{retry.setVisibleKeys({"same"});CHECK(false);}catch(const std::runtime_error&){}
    retry.setVisibleKeys({"same"});CHECK(deliveries==2);
    PhotoViewModel reentry;std::vector<std::vector<std::string>> reentryPriorities;
    PhotoActions reentryActions;reentryActions.prioritize=[&](const auto& keys){reentryPriorities.push_back(keys);};
    reentry.setActions(std::move(reentryActions));
    auto reentryState=reentry.update.send({.active=true,.entries={{.id="live",.galleryKey="live-key"}}});
    CHECK(reentryState&&*reentryState);reentry.setVisibleIds({"live"});reentry.rows.get().front()->select();
    auto reopen=reentry.informationOpen.observe([&](const bool& open,std::uint64_t){if(!open)reentry.show();});
    reentry.hide();
    CHECK(reentryPriorities.size()==1&&reentryPriorities.back()==std::vector<std::string>{"live-key"});
    reentry.rows.get().front()->select();CHECK(reentry.informationOpen.get());

    PhotoViewModel partial;
    auto initial=partial.update.send({.entries={{.id="a",.galleryKey="ka"},{.id="b",.galleryKey="kb"}}});
    CHECK(initial&&*initial);
    const auto a=partial.rows.get()[0],b=partial.rows.get()[1];
    const auto beforeInsert=partial.rows.revision();
    auto inserted=partial.splice.send({.index=1,.inserted={{.id="c",.galleryKey="kc"},{.id="d",.galleryKey="kd"}}});
    CHECK(inserted&&*inserted&&partial.rows.revision()==beforeInsert+1);
    CHECK(partial.rows.get().size()==4&&partial.rows.get()[0]==a&&partial.rows.get()[3]==b);
    auto c=partial.rows.get()[1];c->select();CHECK(partial.informationOpen.get());
    auto invalid=partial.splice.send({.index=1,.removedIds={"d","c"}});
    CHECK(invalid&&!*invalid&&partial.rows.get().size()==4&&partial.rows.get()[1]==c);
    auto duplicateKey=partial.splice.send({.index=1,.inserted={{.id="new",.galleryKey="ka"}}});
    CHECK(duplicateKey&&!*duplicateKey&&partial.rows.get().size()==4&&partial.rows.get()[1]==c);
    const auto beforeRemove=partial.rows.revision();
    auto erased=partial.splice.send({.index=1,.removedIds={"c","d"}});
    CHECK(erased&&*erased&&partial.rows.revision()==beforeRemove+1&&partial.rows.get().size()==2);
    CHECK(partial.selectedId.get().empty()&&!partial.informationOpen.get());
    c->select();CHECK(partial.selectedId.get().empty());
    auto duplicate=partial.reorder.send({"a","a"});CHECK(duplicate&&!*duplicate&&partial.rows.get()[0]==a);
    const auto beforeSameOrder=partial.rows.revision();
    auto sameOrder=partial.reorder.send({"a","b"});CHECK(sameOrder&&*sameOrder&&partial.rows.revision()==beforeSameOrder);
    auto reordered=partial.reorder.send({"b","a"});CHECK(reordered&&*reordered&&partial.rows.get()[0]==b&&partial.rows.get()[1]==a);
}
