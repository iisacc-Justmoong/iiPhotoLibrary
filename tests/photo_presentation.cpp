#include <iiPhotoLibrary/PhotoViewModel.h>

#include <cmath>
#include <ctime>
#include <array>
#include <atomic>
#include <future>
#include <memory>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace iiPhotoLibrary::presentation;
#define CHECK(expression) do { if (!(expression)) throw std::runtime_error(#expression); } while (false)

int main() {
    {
        PhotoViewModel guarded;
        PhotoViewModel visible;
        bool originalAction=false, foreignAction=false;
        std::vector<std::vector<std::string>> guardedPriorities;
        PhotoActions callbacks;
        callbacks.connect=[&]{originalAction=true;};
        callbacks.prioritize=[&](const auto& keys){guardedPriorities.push_back(keys);};
        guarded.setActions(std::move(callbacks));
        auto initial=guarded.update.send({.active=true,.access="authorized",
            .entries={{.id="owner",.galleryKey="owner-key"}}});
        CHECK(initial&&*initial);
        guarded.setVisibleIds({"owner"});
        auto row=guarded.rows.get().front();
        std::array<bool,22> rejected{};
        std::thread worker([&]{
            const auto direct=[](auto&& action){
                try{action();return false;}catch(const std::logic_error&){return true;}
            };
            rejected[0]=direct([&]{PhotoActions replacement;replacement.connect=[&]{foreignAction=true;};
                guarded.setActions(std::move(replacement));});
            rejected[1]=!guarded.update.send({.entries={{.id="",.galleryKey="invalid"}}});
            rejected[2]=!guarded.change.send({.id="missing"});
            rejected[3]=direct([&]{guarded.setVisibleIds({});});
            rejected[4]=direct([&]{guarded.setVisibleKeys({});});
            rejected[5]=direct([&]{guarded.setViewportSize(0,0);});
            rejected[6]=direct([&]{(void)guarded.dropFiles({});});
            rejected[7]=direct([&]{row->selectForMenu();});
            rejected[8]=direct([&]{row->copy();});
            rejected[9]=direct([&]{guarded.connectLibrary();});
            rejected[10]=direct([&]{visible.show();});
            rejected[11]=direct([&]{guarded.hide();});
            rejected[12]=!guarded.updateFlags.send({});
            rejected[13]=!guarded.splice.send({});
            rejected[14]=!guarded.reorder.send({"owner"});
            rejected[15]=direct([&]{guarded.pickFiles();});
            rejected[16]=direct([&]{guarded.refresh();});
            rejected[17]=direct([&]{guarded.closeInformation();});
            rejected[18]=direct([&]{(void)guarded.pinch((std::numeric_limits<double>::quiet_NaN)());});
            rejected[19]=direct([&]{(void)guarded.dragZoom(0,false);});
            rejected[20]=direct([&]{(void)guarded.selectById("owner",true);});
            rejected[21]=direct([&]{(void)guarded.findById("owner");});
        });
        worker.join();
        for(std::size_t i=0;i<rejected.size();++i)
            if(!rejected[i])throw std::runtime_error("Foreign-thread photo operation accepted: "+std::to_string(i));
        guarded.connectLibrary();
        CHECK(originalAction&&!foreignAction);
        CHECK(guardedPriorities.size()==1&&guardedPriorities.front()==std::vector<std::string>{"owner-key"});
        CHECK(!guarded.selectById("missing",true));
        CHECK(!guarded.findById("missing")&&guarded.findById("owner")==row);
        CHECK(guarded.selectById("owner",true));
        CHECK(guarded.informationOpen.get()&&guarded.selectedId.get()=="owner");
    }
    {
        PhotoViewModel concurrent;
        std::promise<void> start;
        const auto ready=start.get_future().share();
        std::atomic<int> admitted{0};
        std::thread worker([&]{
            ready.wait();
            for(int i=0;i<500;++i){
                try{concurrent.setActions({});++admitted;}catch(const std::logic_error&){}
                try{concurrent.hide();++admitted;}catch(const std::logic_error&){}
                if(concurrent.change.send({.id="missing"}))++admitted;
            }
        });
        start.set_value();
        for(int i=0;i<500;++i){
            auto changed=concurrent.updateFlags.send({.active=(i%2)!=0});
            CHECK(changed&&*changed);
            concurrent.hide();concurrent.show();
        }
        worker.join();
        CHECK(admitted.load()==0);
    }
    {
        ii::runtime::EventLoop loop;
        bool completed=false;
        auto pending=std::make_unique<PhotoViewModel>();
        auto submitted=pending->change.post(loop,PhotoChange{.id="pending"},
            [&](auto){completed=true;});
        CHECK(submitted);
        pending.reset();
        (void)loop.poll();
        CHECK(!completed);
    }
    {
        auto dying=std::make_unique<PhotoViewModel>();
        auto state=dying->update.send({.active=true,.entries={{.id="row",.galleryKey="key"}}});
        CHECK(state&&*state);
        auto row=dying->rows.get().front();
        auto observer=dying->selectedId.observe([&](const auto&,auto){dying.reset();});
        row->select();
        CHECK(!dying);
        row->copy();
    }
    {
        PhotoViewModel previews;
        std::vector<std::string> invalidations;
        PhotoActions actions;
        actions.previewChanged=[&](std::string_view source){invalidations.emplace_back(source);};
        previews.setActions(std::move(actions));
        bool structuralOrder=true;
        auto structural=previews.rows.observe([&](const auto& rows,std::uint64_t){
            if(!rows.empty()&&invalidations.empty())structuralOrder=false;
        });
        PhotoState state{.entries={
            {.id="a",.galleryKey="ka",.preview="file:///same.jpg",.previewRevision="1"},
            {.id="b",.galleryKey="kb",.preview="file:///same.jpg",.previewRevision="1"}}};
        auto result=previews.update.send(state);
        CHECK(result&&*result&&structuralOrder&&invalidations==std::vector<std::string>{"file:///same.jpg"});
        invalidations.clear();
        result=previews.update.send(state);CHECK(result&&*result&&invalidations.empty());
        auto first=previews.rows.get().front();
        bool sourceOrder=true,revisionOrder=true;
        auto sourceObserver=first->preview.observe([&](const auto& source,std::uint64_t){
            if(!source.empty()&&(invalidations.empty()||invalidations.back()!=source))sourceOrder=false;
        });
        auto revisionObserver=first->previewRevision.observe([&](const auto&,std::uint64_t){
            if(invalidations.empty()||invalidations.back()!=first->preview.get())revisionOrder=false;
        });
        auto changed=state.entries.front();changed.preview="file:///other.jpg";
        result=previews.change.send({.id="a",.entry=changed});
        CHECK(result&&*result&&sourceOrder&&invalidations==std::vector<std::string>{"file:///other.jpg"});
        invalidations.clear();changed.previewRevision="2";
        result=previews.change.send({.id="a",.entry=changed});
        CHECK(result&&*result&&revisionOrder&&invalidations==std::vector<std::string>{"file:///other.jpg"});
        invalidations.clear();changed.name="Only metadata";
        result=previews.change.send({.id="a",.entry=changed});CHECK(result&&*result&&invalidations.empty());
        result=previews.change.send({.id="a"});CHECK(result&&*result);
        bool insertionOrder=false;
        auto reinsertion=previews.rows.observe([&](const auto& rows,std::uint64_t){
            if(rows.size()==2&&rows.back()->id=="a")
                insertionOrder=invalidations==std::vector<std::string>{"file:///other.jpg"};
        });
        changed.previewRevision="3";
        result=previews.change.send({.id="a",.entry=changed});
        CHECK(result&&*result&&insertionOrder);
        invalidations.clear();
        auto invalid=previews.splice.send({.index=0,.inserted={{.id="c",.galleryKey="kb",.preview="file:///dup.jpg"}}});
        CHECK(invalid&&!*invalid&&invalidations.empty());
        bool spliceOrder=false;
        auto spliceObserver=previews.rows.observe([&](const auto& rows,std::uint64_t){
            if(rows.size()==4)spliceOrder=invalidations==std::vector<std::string>{"file:///dup.jpg"};
        });
        auto spliced=previews.splice.send({.index=0,.inserted={
            {.id="c",.galleryKey="kc",.preview="file:///dup.jpg"},
            {.id="d",.galleryKey="kd",.preview="file:///dup.jpg"}}});
        CHECK(spliced&&*spliced&&spliceOrder);
        invalidations.clear();
        bool snapshotOrder=true;
        auto revision=previews.rows.get().front()->previewRevision.observe([&](const auto&,std::uint64_t){
            snapshotOrder=invalidations==std::vector<std::string>{"file:///dup.jpg"};
        });
        PhotoState refreshed{.entries={
            {.id="c",.galleryKey="kc",.preview="file:///dup.jpg",.previewRevision="2"},
            {.id="d",.galleryKey="kd",.preview="file:///dup.jpg",.previewRevision="2"},
            state.entries.back(),changed}};
        result=previews.update.send(refreshed);
        CHECK(result&&*result&&snapshotOrder&&invalidations==std::vector<std::string>{"file:///dup.jpg"});
    }
    {
        PhotoViewModel previews;
        PhotoState state{.entries={{.id="a",.galleryKey="ka",.preview="file:///a.jpg",.previewRevision="1"}}};
        auto initial=previews.update.send(state);CHECK(initial&&*initial);
        int attempts=0;PhotoActions actions;
        actions.previewChanged=[&](std::string_view){if(++attempts==1)throw std::runtime_error("retry invalidation");};
        previews.setActions(std::move(actions));state.entries.front().previewRevision="2";
        auto failed=previews.update.send(state);
        CHECK(!failed&&previews.rows.get().front()->previewRevision.get()=="1");
        auto retried=previews.update.send(state);
        CHECK(retried&&*retried&&attempts==2&&previews.rows.get().front()->previewRevision.get()=="2");
    }
    auto model=std::make_shared<PhotoViewModel>();
    {
        PhotoBackend backend(model);
        CHECK(backend.formatField("Type: {value}","Photo")=="Type: Photo");
        CHECK(backend.formatSize(1023,"{value} bytes","{value} KB","{value} MB","{value} GB")=="1023 bytes");
        CHECK(backend.formatSize(1536,"{value} bytes","{value} KB","{value} MB","{value} GB")=="1.5 KB");
        CHECK(backend.formatSize(1572864,"{value} bytes","{value} KB","{value} MB","{value} GB")=="1.5 MB");
        CHECK(backend.formatSize(1610612736,"{value} bytes","{value} KB","{value} MB","{value} GB")=="1.5 GB");
        const auto utc=backend.formatTimestamp("2026-11-01T05:30:00Z");
        CHECK(utc.size()==16&&utc[10]==' '&&utc[13]==':');
        const std::time_t instant=1793511000;
        const auto local=std::localtime(&instant);CHECK(local);
        char expected[32]{};CHECK(std::strftime(expected,sizeof(expected),"%Y-%m-%d %H:%M",local)>0);
        CHECK(utc==expected);
        CHECK(utc==backend.formatTimestamp("2026-11-01T01:30:00-04:00"));
        CHECK(utc==backend.formatTimestamp("2026-11-01T07:30:00.123+02:00"));
        CHECK(backend.formatTimestamp("2026-02-30T05:30:00Z")=="2026-02-30T05:30:00Z");
        CHECK(backend.formatTimestamp("2026-10-02T12:34:00+25:00")=="2026-10-02T12:34:00+25:00");
        CHECK(backend.formatTimestamp("unrecognized")=="unrecognized");
        CHECK(backend.formatTimestamp("2026-10-02T12:34:00")=="2026-10-02 12:34");
    }
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
    CHECK(invalidated==std::vector<std::string>{"file:///one.jpg"});invalidated.clear();
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
