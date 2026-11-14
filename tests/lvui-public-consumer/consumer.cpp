#include "PhotosView.generated.h"
#include <iiGUI/PaintView.h>
#include <chrono>
#include <cmath>
#include <memory>
#include <stdexcept>

using ii::core::Result;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void good(Result<void> value){if(!value){if(value.error().cause)std::rethrow_exception(value.error().cause);throw std::runtime_error(value.error().message);}}
template<class T>T take(Result<T> value){if(!value){if(value.error().cause)std::rethrow_exception(value.error().cause);throw std::runtime_error(value.error().message);}return std::move(*value);}
struct Device final : ii::gui::PaintDevice {
    ii::gui::PaintCapabilities capabilities()const noexcept override{return {.api=ii::gui::GraphicsApi::vulkan,.hardware=true,.native_api=true,.backdrop=true,.shadow=true,.content_transform=true,.scene_sources=true};}
    Result<void> begin_frame(const ii::gui::PaintFrameInfo&)override{return {};}
    Result<void> begin_scene(const ii::gui::PaintSceneInfo&)override{return {};}
    Result<void> end_scene()override{return {};}
    Result<void> begin_layer(const ii::gui::PaintLayerInfo&)override{return {};}
    Result<void> draw_panels(std::span<const ii::gui::Panel>)override{return {};}
    Result<void> draw_text(std::span<const ii::gui::TextRun>)override{return {};}
    Result<void> draw_images(std::span<const ii::gui::ImageRun>)override{return {};}
    Result<void> end_layer()override{return {};}
    Result<void> present()override{return {};}
    void abort_frame()noexcept override{}
};
}
int main(){
    using namespace iiPhotoLibrary::presentation;
    using View=lvui_generated::PhotosView;
    auto model=std::make_shared<PhotoViewModel>();
    model->thumbnailSize.set(80);model->viewportWidth.set(320);model->viewportHeight.set(170);model->motionEnabled.set(false);
    PhotoState state{.active=true,.canShare=true,.access="authorized"};
    for(int i=0;i<120;++i)state.entries.push_back({.id="id-"+std::to_string(i),.galleryKey="key-"+std::to_string(i),.name="Photo "+std::to_string(i)});
    auto published=model->update.send(state);check(published&&*published,"Photo snapshot was rejected");
    auto strings=std::make_shared<lvui::native::AppStrings>();
    auto images=std::make_shared<lvui::native::ImageServices<ii::gui::RasterImage>>();
    auto factories=lvui::native::BackendFactories{}.with_app_strings(strings).with<PhotoBackend>([model]{return PhotoBackend(model);})
        .with<std::shared_ptr<lvui::native::ImageServices<ii::gui::RasterImage>>>([images]{return images;});
    ii::runtime::EventLoop loop;ii::gui::Widget host;std::shared_ptr<View::Scope> scope;
    auto view=take(ii::gui::PaintView::create(std::make_shared<Device>(),[&](ii::gui::WidgetBuilder& b)->Result<void>{
        host=take(b.content_host({},"host",{900,720}));
        return host.set_content([&](ii::gui::WidgetBuilder& inner){return View::build(inner,{}, {},&scope,factories);});
    },{},ii::gui::PaintViewServices{.events=&loop}));
    good(view->paint({900,720}));
    auto grid=take(host.content_item("photoGrid")),scroll=take(host.content_item("photoGrid/viewport"));
    good(scroll.set_scroll_offset(240));good(view->paint({900,720}));
    const auto cell=take(take(grid.keyed_content_item("id-13")).snapshot()).bounds;
    const auto x=cell.x+20,y=cell.y+20;
    const auto initial=model->thumbnailSize.get();
    good(view->input({.kind=ii::gui::WidgetInputKind::pointer_down,.x=x,.y=y,.buttons=1}));
    good(view->input({.kind=ii::gui::WidgetInputKind::pointer_move,.x=x+18,.y=y,.buttons=1}));
    check(std::abs(model->thumbnailSize.get()-initial*std::exp2(.1))<.01,"Installed shared iiGUI did not deliver drag zoom");
    good(view->input({.kind=ii::gui::WidgetInputKind::pointer_up,.x=x+18,.y=y}));
    check(!model->informationOpen.get()&&model->selectedId.get().empty(),"Drag also selected a photo");
    good(view->close());check(!host.snapshot(),"Closing the public host retained its view");
}
