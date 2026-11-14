#include <iiPhotoLibrary/PhotoPresentationBridge.h>
#include <iiPhotoLibrary/PhotoViewModel.h>
#include <iiPhotoLibrary/PhotoController.h>
#include <QCoreApplication>
#include <QTest>
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
class PhotoPresentationBridgeTests final : public QObject {
    Q_OBJECT
private:
    static QVariant row(QString id,QString name,QString stamp){
        return QVariantMap{{"id",id},{"galleryKey",id},{"name",name},{"preview",QUrl("file:///image.jpg")},
                           {"previewStamp",stamp},{"media","photo"}};
    }
private slots:
    void eventsAndLifetime(){
        auto model=std::make_shared<PhotoViewModel>();auto library=std::make_shared<FakeLibrary>();
        auto controller=std::make_unique<PhotoController>(PhotoController::Sender{},nullptr,library);
        auto* gallery=static_cast<PhotoGalleryModel*>(controller->galleryModel());
        gallery->replace({row("a","First","1"),row("b","Second","1")});
        std::vector<std::string> invalidated;
        PhotoPresentationBridge bridge(controller.get(),model,{},[&](auto source){invalidated.emplace_back(source);});
        QCOMPARE(model->rows.get().size(),std::size_t(2));
        auto first=model->rows.get()[0],second=model->rows.get()[1];auto revision=model->rows.revision();
        gallery->replace({row("a","Updated","2"),row("b","Second","1")});
        QCOMPARE(model->rows.revision(),revision);QCOMPARE(first->name.get(),std::string("Updated"));
        QCOMPARE(invalidated.size(),std::size_t(1));
        gallery->replace({row("b","Second","1"),row("c","Third","1"),row("a","Updated","2")});
        QCOMPARE(model->rows.get().size(),std::size_t(3));
        QCOMPARE(model->rows.get()[0],second);QCOMPARE(model->rows.get()[2],first);
        QCOMPARE(model->rows.get()[1]->id,std::string("c"));
        gallery->replace({row("b","Second","1")});
        QCOMPARE(model->rows.get().size(),std::size_t(1));QCOMPARE(model->rows.get()[0],second);
        first->select();QVERIFY(model->selectedId.get().empty());
        controller.reset();QVERIFY(model->rows.get().empty());
        second->select();QVERIFY(model->selectedId.get().empty());
    }
    void bridgeCanBeRetiredDuringRowNotification(){
        auto model=std::make_shared<PhotoViewModel>();auto library=std::make_shared<FakeLibrary>();
        PhotoController controller({},nullptr,library);
        auto* gallery=static_cast<PhotoGalleryModel*>(controller.galleryModel());
        gallery->replace({row("a","First","1"),row("b","Second","1")});
        auto bridge=std::make_unique<PhotoPresentationBridge>(&controller,model);
        auto lease=model->rows.get().front()->name.observe([&](const auto&,std::uint64_t){bridge.reset();});
        gallery->replace({row("a","Changed","2"),row("b","Changed","2")});
        QVERIFY(!bridge);QCOMPARE(model->rows.get().front()->name.get(),std::string("Changed"));
    }
    void recoversWhenAChangeWasMissed(){
        auto model=std::make_shared<PhotoViewModel>();auto library=std::make_shared<FakeLibrary>();
        PhotoController controller({},nullptr,library);
        auto* gallery=static_cast<PhotoGalleryModel*>(controller.galleryModel());
        gallery->replace({row("a","First","1"),row("b","Second","1")});
        PhotoPresentationBridge bridge(&controller,model);
        const auto surviving=model->rows.get()[1];
        auto lost=model->change.send({.id="a"});QVERIFY(lost&&*lost);
        gallery->replace({row("b","Second","1")});
        QTRY_COMPARE(model->rows.get().size(),std::size_t(1));
        QCOMPARE(model->rows.get()[0],surviving);
        gallery->replace({row("a","First","1"),row("b","Second","1")});
        auto lostAgain=model->change.send({.id="a"});QVERIFY(lostAgain&&*lostAgain);
        gallery->replace({row("a","First","1"),row("b","Second","1"),row("c","Third","1")});
        QTRY_COMPARE(model->rows.get().size(),std::size_t(3));
        QCOMPARE(model->rows.get()[0]->id,std::string("a"));QCOMPARE(model->rows.get()[1],surviving);
    }
    void bridgeCanBeRetiredDuringStructuralChange(){
        auto model=std::make_shared<PhotoViewModel>();auto library=std::make_shared<FakeLibrary>();
        PhotoController controller({},nullptr,library);
        auto* gallery=static_cast<PhotoGalleryModel*>(controller.galleryModel());
        gallery->replace({row("a","First","1")});
        auto bridge=std::make_unique<PhotoPresentationBridge>(&controller,model);
        auto lease=model->rows.observe([&](const auto&,std::uint64_t){bridge.reset();});
        gallery->replace({row("a","First","1"),row("b","Second","1")});
        QVERIFY(!bridge);QCOMPARE(model->rows.get().size(),std::size_t(2));
    }
};
QTEST_GUILESS_MAIN(PhotoPresentationBridgeTests)
#include "photo_presentation_bridge.moc"
