#include "PhotosView.generated.h"
#include <iiGUI/Image.h>
#include <iiGUI/WidgetState.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class T>T take(ii::core::Result<T> value){if(!value){if(value.error().cause)std::rethrow_exception(value.error().cause);throw std::runtime_error(value.error().message);}return std::move(*value);}
void good(ii::core::Result<void> value){if(!value){if(value.error().cause)std::rethrow_exception(value.error().cause);throw std::runtime_error(value.error().message);}}
ii::gui::Widget find(const std::shared_ptr<ii::gui::detail::WidgetState>& tree,std::string_view suffix){
    const auto nodes=take(tree->snapshots());auto item=std::ranges::find_if(nodes,[&](const auto& node){return node.id.ends_with(suffix);});
    if(item==nodes.end())throw std::runtime_error("Missing photo widget "+std::string(suffix));return take(tree->find(item->id));
}
struct Resolver final : lvui::native::ImageResolver<ii::gui::RasterImage> {
    std::shared_ptr<const ii::gui::RasterImage> raster;
    mutable int requests=0;
    ii::core::Observable<int> revision{0};
    std::shared_ptr<const ii::gui::RasterImage> image(std::string_view source,std::uint32_t,std::uint32_t)const override{
        (void)revision.get();check(source=="file:///fixture-preview.jpg","Unexpected preview address");++requests;return raster;
    }
    void invalidate(){revision.set(revision.get()+1);}
};
}
int main(){
    using namespace iiPhotoLibrary::presentation;
    using View=lvui_generated::PhotosView;
    auto model=std::make_shared<PhotoViewModel>();model->thumbnailSize.set(80);model->viewportWidth.set(320);model->viewportHeight.set(170);model->motionEnabled.set(false);
    std::vector<std::vector<std::string>> priorities;std::vector<std::string> actions,dropped;
    auto resolver=std::make_shared<Resolver>();resolver->raster=take(ii::gui::rgba_image(1,1,{255,0,0,255}));
    auto imageServices=std::make_shared<lvui::native::ImageServices<ii::gui::RasterImage>>();imageServices->resolver.set(resolver);
    PhotoActions handlers;handlers.prioritize=[&](const auto& keys){priorities.push_back(keys);};
    handlers.addFiles=[&](const auto& files){dropped=files;};
    handlers.use=[&](PhotoUse,std::string_view id){actions.emplace_back(id);};
    handlers.previewChanged=[&](std::string_view){resolver->invalidate();};model->setActions(std::move(handlers));
    auto strings=std::make_shared<lvui::native::AppStrings>();
    good(strings->replace_catalog("en",{{"photo.title","Photos"},{"photo.open","Open"},
        {"photo.import_library","Import photo library…"},{"photo.add","Add photos or videos…"},{"photo.refresh","Refresh"},{"photo.size","Size"}}));
    good(strings->replace_catalog("ko",{{"photo.title","사진"},{"photo.open","열기"},
        {"photo.import_library","사진 보관함 가져오기…"},{"photo.add","사진 또는 동영상 추가…"},{"photo.refresh","새로고침"},{"photo.size","크기"}}));
    auto factories=lvui::native::BackendFactories{}.with_app_strings(strings).with<PhotoBackend>([model]{return PhotoBackend(model);})
        .with<std::shared_ptr<lvui::native::ImageServices<ii::gui::RasterImage>>>([imageServices]{return imageServices;});
    ii::runtime::EventLoop eventLoop;ii::gui::detail::WidgetFrameHooks hooks;
    hooks.events=[&]()->ii::core::Result<ii::runtime::EventLoop*>{return &eventLoop;};
    auto tree=std::make_shared<ii::gui::detail::WidgetState>([](ii::runtime::Task task){task();},ii::gui::WidgetAppearance{},hooks);
    auto root=take(tree->content_host({},"host",{900,720}));std::shared_ptr<View::Scope> scope;
    good(root.set_content([&](auto& builder){return View::build(builder,{}, {},&scope,factories);}));
    PhotoState state{.active=true,.canShare=true,.access="authorized",.status="Ready"};
    for(int index=0;index<120;++index)state.entries.push_back({.id="id-"+std::to_string(index),.galleryKey="key-"+std::to_string(index),.name="Photo "+std::to_string(index)});
    state.entries.front().preview="file:///fixture-preview.jpg";state.entries.front().previewRevision="1";
    auto published=model->update.send(state);check(published&&*published,"Photo snapshot failed");
    (void)take(tree->drawing_snapshot({900,720}));
    check(take(find(tree,"/photosTitle").snapshot()).text=="Photos","English title was not localized");
    good(strings->set_locale("ko"));check(take(find(tree,"/photosTitle").snapshot()).text=="사진","Live locale change was not applied");
    auto grid=find(tree,"/photoGrid"),scroll=find(tree,"/photoGrid/viewport");
    check(take(scroll.snapshot()).scroll_offset>0&&bool(grid.keyed_content_item("id-119")),"Gallery did not initially show the newest photo");
    good(scroll.set_scroll_offset(0));
    const auto previewFrame=take(tree->drawing_snapshot({900,720}));
    auto surface=take(grid.keyed_content_item("id-0","photoTile.surface"));
    const auto bounds=take(surface.snapshot()).bounds;
    auto content=std::make_shared<ii::gui::ClipboardContent>();content->files={"/tmp/photo.jpg"};
    good(tree->input({.kind=ii::gui::WidgetInputKind::drop,.x=bounds.x+1,.y=bounds.y+1,.content=content}));
    check(dropped==content->files,"The photo view did not forward a dropped file");
    const auto hasRaster=[&](const auto& images){return std::ranges::any_of(images,[&](const auto& image){return image.image==resolver->raster;});};
    check(resolver->requests>0&&(hasRaster(previewFrame.images)||std::ranges::any_of(previewFrame.overlays,[&](const auto& layer){return hasRaster(layer.images);})),
          "The supplied preview did not reach the card painter");
    good(tree->input({.kind=ii::gui::WidgetInputKind::context_request,.x=bounds.x+4,.y=bounds.y+4}));
    auto menu=take(grid.keyed_content_item("id-0","photoMenu"));
    check(take(menu.snapshot()).opened&&model->selectedId.get()=="id-0"&&!model->informationOpen.get(),
          "The photo context menu did not select its own row");
    good(take(grid.keyed_content_item("id-0","photoCopy")).activate());
    check(actions==std::vector<std::string>{"id-0"}&&!take(menu.snapshot()).opened,
          "The context command did not target its row and close the menu");
    actions.clear();
    good(tree->input({.kind=ii::gui::WidgetInputKind::pointer_down,.x=bounds.x+4,.y=bounds.y+4,
                      .device=ii::gui::PointerDevice::touch,.device_id=3}));
    auto touchDeadline=eventLoop.next_deadline();check(bool(touchDeadline)&&eventLoop.stats().pending==1,"Photo touch did not request exactly one context deadline");
    std::this_thread::sleep_until(*touchDeadline+std::chrono::milliseconds(10));eventLoop.poll();
    if(!take(menu.snapshot()).opened||model->informationOpen.get())throw std::runtime_error(
        "Long touch did not open the photo context menu: pending="+std::to_string(eventLoop.stats().pending)+
        " selected="+model->selectedId.get()+" info="+std::to_string(model->informationOpen.get()));
    good(tree->input({.kind=ii::gui::WidgetInputKind::pointer_up,.x=bounds.x+4,.y=bounds.y+4,
                      .device=ii::gui::PointerDevice::touch,.device_id=3}));
    check(!model->informationOpen.get(),"Long touch also activated photo information");
    good(take(grid.keyed_content_item("id-0","photoCopy")).activate());
    check(actions==std::vector<std::string>{"id-0"},"Long-touch menu lost its photo identity");actions.clear();
    const auto requests=resolver->requests;auto revised=state.entries.front();revised.previewRevision="2";
    auto changed=model->change.send({.id="id-0",.entry=revised});check(changed&&*changed&&resolver->requests>requests,"Preview revision did not invalidate the image resolver");
    if(priorities.empty()||priorities.back().empty()||priorities.back().size()>=state.entries.size()){
        const auto viewport=take(scroll.snapshot());
        throw std::runtime_error("Visible priority was not bounded: events="+std::to_string(priorities.size())+
            " last="+std::to_string(priorities.empty()?0:priorities.back().size())+" mounted="+std::to_string(take(grid.keyed_content_keys()).size())+
            " width="+std::to_string(viewport.bounds.width)+" height="+std::to_string(viewport.bounds.height));
    }
    good(surface.activate());
    check(model->selectedId.get()=="id-0"&&model->informationOpen.get(),"Gallery selection did not open information");
    check(take(find(tree,"/photoName").snapshot()).text=="Photo 0","Selected photo information was not bound");
    good(find(tree,"/viewOriginal").activate());check(actions==std::vector<std::string>{"id-0"},"Original action used the wrong photo");
    model->closeInformation();
    auto zoom=find(tree,"/photoSizeSlider");const auto beforeSize=model->thumbnailSize.get();
    good(zoom.focus());good(tree->input({.kind=ii::gui::WidgetInputKind::key_down,.key=ii::gui::WidgetKey::right}));
    check(model->thumbnailSize.get()>beforeSize&&model->thumbnailSize.get()<=400,
          "The size control did not change the photo grid through its binding");
    good(scroll.set_scroll_offset(240));(void)take(tree->drawing_snapshot({900,720}));
    const auto dragSize=model->thumbnailSize.get();const auto dragCell=take(take(grid.keyed_content_item("id-13")).snapshot()).bounds;
    const auto dragX=dragCell.x+20,dragY=dragCell.y+20;
    const auto dragFraction=(dragY-dragCell.y)/(dragSize+2.);
    const auto previousSelection=model->selectedId.get();
    good(tree->input({.kind=ii::gui::WidgetInputKind::pointer_down,.x=dragX,.y=dragY,.buttons=1}));
    good(tree->input({.kind=ii::gui::WidgetInputKind::pointer_move,.x=dragX+18,.y=dragY,.buttons=1}));
    check(std::abs(model->thumbnailSize.get()-dragSize*std::exp2(.1))<.01,"Horizontal drag did not resize the gallery");
    const auto draggedCell=take(take(grid.keyed_content_item("id-13")).snapshot()).bounds;
    check(std::abs((dragY-draggedCell.y)/(model->thumbnailSize.get()+2.)-dragFraction)<.02,
          "Horizontal drag did not preserve the photo under its press");
    good(tree->input({.kind=ii::gui::WidgetInputKind::pointer_move,.x=dragX+540,.y=dragY,.buttons=1}));
    check(model->thumbnailSize.get()==400.,"Drag zoom did not respect its upper bound");
    good(tree->input({.kind=ii::gui::WidgetInputKind::pointer_move,.x=dragX+480,.y=dragY,.buttons=1}));
    check(model->thumbnailSize.get()==400.,"Reversing beyond the bound ignored the starting size");
    good(tree->input({.kind=ii::gui::WidgetInputKind::pointer_move,.x=dragX+180,.y=dragY,.buttons=1}));
    check(std::abs(model->thumbnailSize.get()-dragSize*2.)<.01,"Drag zoom did not use cumulative distance from the press");
    good(tree->input({.kind=ii::gui::WidgetInputKind::pointer_up,.x=dragX+180,.y=dragY}));
    check(model->selectedId.get()==previousSelection&&!model->informationOpen.get(),"Drag also activated a photo");
    model->thumbnailSize.set(dragSize);good(scroll.set_scroll_offset(240));(void)take(tree->drawing_snapshot({900,720}));
    const auto pinchSize=model->thumbnailSize.get();const auto gridBounds=take(scroll.snapshot()).bounds;
    const auto oldCell=take(take(grid.keyed_content_item("id-13")).snapshot()).bounds;
    const auto pinchX=oldCell.x+20,pinchY=oldCell.y+20;
    check(pinchX>=gridBounds.x&&pinchX<gridBounds.x+gridBounds.width&&pinchY>=gridBounds.y&&pinchY<gridBounds.y+gridBounds.height,
          "Pinch fixture is outside the viewport");
    const auto previousFraction=(pinchY-oldCell.y)/(pinchSize+2.);
    good(tree->input({.kind=ii::gui::WidgetInputKind::scroll,.x=pinchX,.y=pinchY,
        .scroll_y=std::log(1.5)*ii::gui::pinch_scroll_units,.control=true,.precise_scroll=true,.device=ii::gui::PointerDevice::touch}));
    check(std::abs(model->thumbnailSize.get()-pinchSize*1.5)<.01,"Touch pinch did not resize the gallery");
    const auto newCell=take(take(grid.keyed_content_item("id-13")).snapshot()).bounds;
    check(std::abs((pinchY-newCell.y)/(model->thumbnailSize.get()+2.)-previousFraction)<.02,
          "Touch pinch did not preserve the photo under the gesture center");
    good(scroll.set_scroll_offset(240));
    check(!priorities.empty()&&std::ranges::find(priorities.back(),"key-0")==priorities.back().end(),"Scroll did not change visible preview priorities");
    model->viewportWidth.set(0);model->viewportHeight.set(0);
    for(const auto locale:{"ko","en"}){
        good(strings->set_locale(locale));
        for(const double width:{320.,390.}){
            (void)take(tree->drawing_snapshot({width,844}));
            const auto narrow=take(scroll.snapshot());
            check(narrow.bounds.width>0&&narrow.bounds.width<=width&&narrow.bounds.height>0&&narrow.bounds.height<=844,
                  "The gallery did not adapt its viewport to a narrow host");
            for(const auto id:{"/connectLibrary","/addPhotos","/refresh"}){
                const auto button=take(find(tree,id).snapshot()).bounds;
                if(button.x<0||button.x+button.width>width){const auto host=take(find(tree,"/galleryDrop").snapshot()).bounds;
                    throw std::runtime_error(std::string("Photo action overflows a narrow host: ")+id+" "+locale+
                        " x="+std::to_string(button.x)+" w="+std::to_string(button.width)+
                        " listener="+std::to_string(host.width)+"x"+std::to_string(host.height));}
            }
            const auto sizeControl=take(find(tree,"/photoSizeSlider").snapshot()).bounds;
            check(sizeControl.x>=0&&sizeControl.x+sizeControl.width<=width,"Photo size control overflows a narrow host");
        }
    }
    (void)take(tree->drawing_snapshot({900,720}));(void)take(find(tree,"/galleryDrop").snapshot());
    check(!model->compactLayout.get(),"The gallery remained compact after widening its host");
    model->hide();check(priorities.back().empty(),"Hiding the view did not release visible priorities");
    const auto hiddenSize=model->thumbnailSize.get();const auto hiddenScroll=take(scroll.snapshot());
    good(tree->input({.kind=ii::gui::WidgetInputKind::scroll,.x=hiddenScroll.bounds.x+5,.y=hiddenScroll.bounds.y+5,
        .scroll_y=std::log(1.2)*ii::gui::pinch_scroll_units,.control=true,.precise_scroll=true,.device=ii::gui::PointerDevice::touch}));
    check(model->thumbnailSize.get()==hiddenSize&&take(scroll.snapshot()).scroll_offset==hiddenScroll.scroll_offset,
          "A declined pinch changed the hidden gallery or scrolled it");
    good(root.clear_content());scope.reset();tree->close();
}
