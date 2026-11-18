#include <iiPhotoLibrary/PhotoPresentationBridge.h>
#include <iiPhotoLibrary/PhotoViewModel.h>
#include <iiPhotoLibrary/PhotoController.h>
#include <QCoreApplication>
#include "CTestSupport.h"
#include <memory>

using namespace iiPhotoLibrary;
using namespace iiPhotoLibrary::presentation;

class FakeLibrary final : public PhotoLibrary {
public:
    QString name()const override{return "Fake";}
    Access access()const override{return Access::Full;}
    void authorize(std::function<void()> finished)override{finished();}
    Snapshot scan(QString*)override{return {};}
    bool preview(const QString&,const QString&,QString*)override{return false;}
    bool exportResource(const QString&,const QString&,const QString&,QString*)override{return false;}
    QString import(const QJsonObject&,const QStringList&,QString*)override{return {};}
    bool trash(const QString&,QString*)override{return false;}
};
class PhotoPresentationBridgeTests final {
private:
    static QVariant row(QString id,QString name,QString stamp){
        return QVariantMap{{"id",id},{"galleryKey",id},{"name",name},{"preview",QUrl("file:///image.jpg")},
                           {"previewStamp",stamp},{"media","photo"}};
    }
public:
    void eventsAndLifetime(){
        auto model=std::make_shared<PhotoViewModel>();auto library=std::make_shared<FakeLibrary>();
        auto controller=std::make_unique<PhotoController>(PhotoController::Sender{},nullptr,library);
        auto* gallery=static_cast<PhotoGalleryModel*>(controller->galleryModel());
        gallery->replace({row("a","First","1"),row("b","Second","1")});
        std::vector<std::string> invalidated;
        PhotoPresentationBridge bridge(controller.get(),model,{},[&](auto source){invalidated.emplace_back(source);});
        CTEST_COMPARE(model->rows.get().size(),std::size_t(2));
        CTEST_COMPARE(invalidated.size(),std::size_t(1));
        invalidated.clear();
        auto first=model->rows.get()[0],second=model->rows.get()[1];auto revision=model->rows.revision();
        gallery->replace({row("a","Updated","2"),row("b","Second","1")});
        CTEST_COMPARE(model->rows.revision(),revision);CTEST_COMPARE(first->name.get(),std::string("Updated"));
        CTEST_COMPARE(invalidated.size(),std::size_t(1));
        gallery->replace({row("b","Second","1"),row("c","Third","1"),row("a","Updated","2")});
        CTEST_COMPARE(model->rows.get().size(),std::size_t(3));
        CTEST_COMPARE(model->rows.get()[0],second);CTEST_COMPARE(model->rows.get()[2],first);
        CTEST_COMPARE(model->rows.get()[1]->id,std::string("c"));
        gallery->replace({row("b","Second","1")});
        CTEST_COMPARE(model->rows.get().size(),std::size_t(1));CTEST_COMPARE(model->rows.get()[0],second);
        first->select();CTEST_CHECK(model->selectedId.get().empty());
        controller.reset();CTEST_CHECK(model->rows.get().empty());
        second->select();CTEST_CHECK(model->selectedId.get().empty());
    }
    void bridgeCanBeRetiredDuringRowNotification(){
        auto model=std::make_shared<PhotoViewModel>();auto library=std::make_shared<FakeLibrary>();
        PhotoController controller({},nullptr,library);
        auto* gallery=static_cast<PhotoGalleryModel*>(controller.galleryModel());
        gallery->replace({row("a","First","1"),row("b","Second","1")});
        auto bridge=std::make_unique<PhotoPresentationBridge>(&controller,model);
        auto lease=model->rows.get().front()->name.observe([&](const auto&,std::uint64_t){bridge.reset();});
        gallery->replace({row("a","Changed","2"),row("b","Changed","2")});
        CTEST_CHECK(!bridge);CTEST_COMPARE(model->rows.get().front()->name.get(),std::string("Changed"));
    }
    void recoversWhenAChangeWasMissed(){
        auto model=std::make_shared<PhotoViewModel>();auto library=std::make_shared<FakeLibrary>();
        PhotoController controller({},nullptr,library);
        auto* gallery=static_cast<PhotoGalleryModel*>(controller.galleryModel());
        gallery->replace({row("a","First","1"),row("b","Second","1")});
        PhotoPresentationBridge bridge(&controller,model);
        const auto surviving=model->rows.get()[1];
        auto lost=model->change.send({.id="a"});CTEST_CHECK(lost&&*lost);
        gallery->replace({row("b","Second","1")});
        CTEST_WAIT_VERIFY(model->rows.get().size() == std::size_t(1), 5000);
        CTEST_COMPARE(model->rows.get()[0],surviving);
        gallery->replace({row("a","First","1"),row("b","Second","1")});
        auto lostAgain=model->change.send({.id="a"});CTEST_CHECK(lostAgain&&*lostAgain);
        gallery->replace({row("a","First","1"),row("b","Second","1"),row("c","Third","1")});
        CTEST_WAIT_VERIFY(model->rows.get().size() == std::size_t(3), 5000);
        CTEST_COMPARE(model->rows.get()[0]->id,std::string("a"));CTEST_COMPARE(model->rows.get()[1],surviving);
    }
    void bridgeCanBeRetiredDuringStructuralChange(){
        auto model=std::make_shared<PhotoViewModel>();auto library=std::make_shared<FakeLibrary>();
        PhotoController controller({},nullptr,library);
        auto* gallery=static_cast<PhotoGalleryModel*>(controller.galleryModel());
        gallery->replace({row("a","First","1")});
        auto bridge=std::make_unique<PhotoPresentationBridge>(&controller,model);
        auto lease=model->rows.observe([&](const auto&,std::uint64_t){bridge.reset();});
        gallery->replace({row("a","First","1"),row("b","Second","1")});
        CTEST_CHECK(!bridge);CTEST_COMPARE(model->rows.get().size(),std::size_t(2));
    }
};
int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    photo_test::CTestRunner runner;
    PhotoPresentationBridgeTests tests;
    runner.run("eventsAndLifetime", [&] { tests.eventsAndLifetime(); });
    runner.run("bridgeCanBeRetiredDuringRowNotification", [&] { tests.bridgeCanBeRetiredDuringRowNotification(); });
    runner.run("recoversWhenAChangeWasMissed", [&] { tests.recoversWhenAChangeWasMissed(); });
    runner.run("bridgeCanBeRetiredDuringStructuralChange", [&] { tests.bridgeCanBeRetiredDuringStructuralChange(); });
    return runner.result();
}
