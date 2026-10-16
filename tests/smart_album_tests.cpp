#include <iiPhotoLibrary/Album.h>
#include <algorithm>
#include <atomic>
#include <functional>
#include <future>
#include <iostream>

using namespace iiPhotoLibrary;
namespace {
int checks = 0;
#define CHECK(...) do { ++checks; if (!(__VA_ARGS__)) throw std::runtime_error( \
    std::string(#__VA_ARGS__) + " at " + std::to_string(__LINE__)); } while (false)
template<class F> void fails(AlbumErrorCode code, F call) {
    try { call(); } catch (const AlbumError &error) { CHECK(error.code() == code); return; }
    throw std::runtime_error("expected album error");
}
template<class F> void invalid(F call) {
    try { call(); } catch (const std::invalid_argument &) { ++checks; return; }
    throw std::runtime_error("expected invalid argument");
}
const std::string hashA(64, 'a'), hashB(64, 'b');
AlbumPhoto photo(std::string id, AlbumPhotoTraits traits = {}) {
    AlbumPhoto value; value.sourcePhotoId = std::move(id); value.name = "photo.jpg"; value.traits = std::move(traits);
    return value;
}
AlbumPhoto find(const std::shared_ptr<Album> &album, const std::string &sourceId) {
    const auto data = album->snapshot();
    const auto item = std::find_if(data.contents.photos.begin(), data.contents.photos.end(),
        [&](const auto &value) { return value.sourcePhotoId == sourceId; });
    if (item == data.contents.photos.end()) throw std::runtime_error("photo not found");
    return *item;
}
class Source final : public AlbumSource {
  public:
    AlbumSnapshot result{{photo("remote")}, true, "one"};
    std::optional<AlbumErrorCode> error;
    std::function<void()> onRead;
    int reads = 0;
    std::string providerId() const override { return "remote"; }
    AlbumCoverage coverage() const override { return AlbumCoverage::External; }
    AlbumSnapshot read(std::stop_token) override {
        ++reads; if (onRead) onRead();
        if (error) throw AlbumError(*error, "fixture error");
        return result;
    }
};
void objectsAndReadOnly() {
    AlbumManager manager({1, 1});
    const auto views = manager.smartAlbums();
    CHECK(views.size() == 8 && manager.albums().empty());
    const std::vector<std::string> titles{"Screenshots", "Imported Photos", "Duplicates", "Hidden", "Images", "Videos", "Favorite", "Deleted"};
    for (std::size_t index = 0; index < views.size(); ++index) {
        const auto kind = static_cast<SmartAlbumKind>(index);
        auto view = manager.smartAlbum(kind);
        const auto data = view->snapshot();
        CHECK(view == views[index] && view == manager.smartAlbum(kind) && manager.album(view->id()) == view);
        CHECK(data.connection.title == titles[index] && data.smartKind == kind);
        CHECK(data.connection.origin == AlbumOrigin::Smart && data.coverage == AlbumCoverage::SmartLibrary);
        CHECK(data.state == AlbumState::Ready && data.contents.complete && data.contents.photos.empty());
        fails(AlbumErrorCode::ReadOnly, [&] { manager.publish(view, {}); });
        fails(AlbumErrorCode::ReadOnly, [&] { manager.setLocalPhotos(view, {}); });
        fails(AlbumErrorCode::ReadOnly, [&] { manager.refresh(view); });
        CHECK(!manager.disconnect(view));
    }
    invalid([&] { manager.smartAlbum(static_cast<SmartAlbumKind>(100)); });
    invalid([&] { manager.connect({"smart-album", "account", "Spoof"}); });
    invalid([&] { manager.connect({"remote", "account", "Spoof", "library", AlbumOrigin::Smart}); });
    invalid([&] { manager.connect({"remote", "account", "Spoof"}, {}, AlbumCoverage::SmartLibrary); });
    auto local = manager.createLocal("local", "Local");
    manager.setLocalPhotos(local, {photo("one")});
    CHECK(manager.albums().size() == 1 && manager.smartAlbums().size() == 8);
}
void classificationsAndVisibility() {
    AlbumManager manager;
    auto local = manager.createLocal("local", "Local");
    auto screenshots = manager.smartAlbum(SmartAlbumKind::Screenshots);
    auto imported = manager.smartAlbum(SmartAlbumKind::Imported);
    auto duplicates = manager.smartAlbum(SmartAlbumKind::Duplicates);
    auto hidden = manager.smartAlbum(SmartAlbumKind::Hidden);
    manager.setLocalPhotos(local, {photo("plain"), photo("shot", {.screenshot = true}),
        photo("import", {.imported = true}), photo("hidden", {.screenshot = true, .imported = true, .hidden = true}),
        photo("copy-a", {.contentHash = hashA}), photo("copy-b", {.contentHash = hashA})});
    CHECK(manager.allLibrary()->snapshot().contents.photos.size() == 5);
    CHECK(screenshots->snapshot().contents.photos.size() == 1 && imported->snapshot().contents.photos.size() == 1);
    CHECK(duplicates->snapshot().contents.photos.size() == 2 && hidden->snapshot().contents.photos.size() == 1);
    CHECK(!duplicates->snapshot().contents.complete);
    const auto groups = manager.duplicateGroups();
    CHECK(groups.size() == 1 && groups[0].contentHash == hashA && groups[0].photos.size() == 2);
    CHECK(groups[0].photos[0].id != groups[0].photos[1].id);
    const auto plain = find(local, "plain");
    manager.setScreenshot(plain.id, true); manager.setImported(plain.id, true);
    CHECK(screenshots->snapshot().contents.photos.size() == 2 && imported->snapshot().contents.photos.size() == 2);
    CHECK(find(screenshots, "plain").id == find(manager.allLibrary(), "plain").id);
    manager.setHidden(find(imported, "plain").id, true);
    CHECK(manager.allLibrary()->snapshot().contents.photos.size() == 4 && hidden->snapshot().contents.photos.size() == 2);
    CHECK(screenshots->snapshot().contents.photos.size() == 1 && imported->snapshot().contents.photos.size() == 1);
    CHECK(manager.photoTraits(plain.id).hidden && find(local, "plain").traits.hidden);
    manager.setHidden(find(hidden, "plain").id, false);
    CHECK(manager.allLibrary()->snapshot().contents.photos.size() == 5);
    manager.setScreenshot(plain.id, false); manager.setImported(plain.id, false);
    CHECK(screenshots->snapshot().contents.photos.size() == 1 && imported->snapshot().contents.photos.size() == 1);
    const auto copy = find(duplicates, "copy-b");
    manager.setContentHash(copy.id, "");
    CHECK(duplicates->snapshot().contents.photos.empty() && manager.duplicateGroups().empty());
    manager.setContentHash(copy.id, hashB);
    CHECK(duplicates->snapshot().contents.photos.empty());
    manager.setContentHash(copy.id, std::string(64, 'A'));
    CHECK(manager.photoTraits(copy.id).contentHash == hashA && manager.duplicateGroups().size() == 1);
    manager.setHidden(find(duplicates, "copy-a").id, true);
    CHECK(duplicates->snapshot().contents.photos.empty() && manager.duplicateGroups().empty());
    manager.setHidden(find(hidden, "copy-a").id, false);
    for (const auto &id : {"plain", "shot", "import"}) manager.setContentHash(find(local, id).id, hashB);
    CHECK(duplicates->snapshot().contents.complete && manager.duplicateGroups().size() == 2);
    const auto before = manager.photoTraits(plain.id);
    fails(AlbumErrorCode::InvalidSnapshot, [&] { manager.setContentHash(plain.id, "not-a-hash"); });
    CHECK(manager.photoTraits(plain.id) == before);
    fails(AlbumErrorCode::NotFound, [&] { manager.setHidden("unknown", true); });
    fails(AlbumErrorCode::NotFound, [&] { manager.photoTraits("unknown"); });
    auto video = photo("video"); video.mediaType = "video";
    manager.setLocalPhotos(local, {video});
    fails(AlbumErrorCode::InvalidSnapshot, [&] { manager.setScreenshot(find(local, "video").id, true); });
    CHECK(screenshots->snapshot().contents.photos.empty() && manager.allLibrary()->snapshot().contents.photos.size() == 1);
}
void identitiesAndConflictingHashes() {
    AlbumManager manager;
    auto first = manager.connect({"remote", "account", "First", "a"});
    auto second = manager.connect({"remote", "account", "Second", "b"});
    manager.publish(first, {{photo("same", {.screenshot = true, .contentHash = hashA})}, true, "one"});
    manager.publish(second, {{photo("same", {.imported = true, .contentHash = hashA})}, true, "two"});
    CHECK(manager.allLibrary()->snapshot().contents.photos.size() == 1 && manager.duplicateGroups().empty());
    CHECK(manager.smartAlbum(SmartAlbumKind::Screenshots)->snapshot().contents.photos.size() == 1);
    CHECK(manager.smartAlbum(SmartAlbumKind::Imported)->snapshot().contents.photos.size() == 1);
    const auto id = find(first, "same").id;
    manager.setHidden(id, true);
    CHECK(find(second, "same").traits.hidden && manager.allLibrary()->snapshot().contents.photos.empty());
    manager.setHidden(find(manager.smartAlbum(SmartAlbumKind::Hidden), "same").id, false);
    auto account = manager.connect({"remote", "another-account", "Another"});
    manager.publish(account, {{photo("same", {.contentHash = hashA})}, true, "three"});
    CHECK(manager.duplicateGroups().size() == 1 && manager.duplicateGroups()[0].photos.size() == 2);
    manager.publish(second, {{photo("same", {.contentHash = hashB})}, true, "conflict"});
    CHECK(manager.duplicateGroups().empty() && !manager.smartAlbum(SmartAlbumKind::Duplicates)->snapshot().contents.complete);
    CHECK(manager.photoTraits(id).contentHash.empty());
    manager.setContentHash(id, hashA);
    CHECK(manager.duplicateGroups().size() == 1 && find(second, "same").traits.contentHash == hashA);
    manager.disconnect(first);
    CHECK(manager.duplicateGroups().size() == 1);
    manager.disconnect(second);
    CHECK(manager.duplicateGroups().empty());
    const auto before = account->snapshot().contents;
    fails(AlbumErrorCode::InvalidSnapshot, [&] { manager.publish(account, {{photo("bad", {.contentHash = "bad"})}, true, "invalid"}); });
    CHECK(account->snapshot().contents.photos == before.photos);
}
void refreshAndOverrideLifetime() {
    AlbumManager manager;
    auto source = std::make_shared<Source>();
    auto remote = manager.connect({"remote", "account", "Remote"}, source);
    auto hidden = manager.smartAlbum(SmartAlbumKind::Hidden);
    CHECK(hidden->snapshot().contents.photos.empty() && !hidden->snapshot().contents.complete && source->reads == 0);
    manager.refresh(remote);
    const auto id = find(remote, "remote").id;
    manager.setPhotoTraits(id, {.screenshot = true, .imported = true, .hidden = true, .contentHash = hashA});
    manager.refresh(remote);
    CHECK(manager.photoTraits(id).screenshot && manager.photoTraits(id).imported && manager.photoTraits(id).hidden);
    CHECK(hidden->snapshot().contents.photos.size() == 1 && manager.allLibrary()->snapshot().contents.photos.empty());
    source->onRead = [&] { manager.setHidden(id, false); };
    manager.refresh(remote); source->onRead = {};
    CHECK(!manager.photoTraits(id).hidden && manager.smartAlbum(SmartAlbumKind::Screenshots)->snapshot().contents.photos.size() == 1);
    source->error = AlbumErrorCode::Transport;
    fails(*source->error, [&] { manager.refresh(remote); });
    CHECK(manager.smartAlbum(SmartAlbumKind::Imported)->snapshot().contents.photos.size() == 1);
    CHECK(!manager.smartAlbum(SmartAlbumKind::Imported)->snapshot().contents.complete);
    source->error = AlbumErrorCode::PermissionDenied;
    fails(*source->error, [&] { manager.refresh(remote); });
    CHECK(hidden->snapshot().contents.photos.empty() && manager.smartAlbum(SmartAlbumKind::Imported)->snapshot().contents.photos.empty());
    source->error.reset(); manager.refresh(remote);
    CHECK(manager.photoTraits(id) == AlbumPhotoTraits{});
    manager.setHidden(id, true);
    source->result = {{}, false, "partial"}; manager.refresh(remote);
    CHECK(hidden->snapshot().contents.photos.size() == 1 && !hidden->snapshot().contents.complete);
    source->result.complete = true; manager.refresh(remote);
    CHECK(hidden->snapshot().contents.photos.empty());
    source->result = {{photo("remote")}, true, "again"}; manager.refresh(remote);
    CHECK(!manager.photoTraits(id).hidden);
    manager.setImported(id, true); manager.disconnect(remote);
    auto replacement = manager.connect({"remote", "account", "Remote"}, source); manager.refresh(replacement);
    CHECK(!manager.photoTraits(find(replacement, "remote").id).imported);
}
void mediaFavoriteAndDeleted() {
    AlbumManager manager;
    auto local = manager.createLocal("local", "Local");
    auto images = manager.smartAlbum(SmartAlbumKind::Images);
    auto videos = manager.smartAlbum(SmartAlbumKind::Videos);
    auto favorite = manager.smartAlbum(SmartAlbumKind::Favorite);
    auto deleted = manager.smartAlbum(SmartAlbumKind::Deleted);
    auto hidden = manager.smartAlbum(SmartAlbumKind::Hidden);
    auto image = photo("image");
    auto video = photo("video"); video.mediaType = "video"; video.favorite = true;
    auto hiddenImage = photo("hidden", {.hidden = true}); hiddenImage.favorite = true;
    auto deletedShot = photo("deleted-shot", {.screenshot = true, .imported = true, .deleted = true}); deletedShot.favorite = true;
    manager.setLocalPhotos(local, {image, video, hiddenImage, deletedShot});
    const auto before = manager.allLibrary()->snapshot();
    CHECK(before.contents.photos.size() == 2);
    CHECK(images->snapshot().contents.photos.size() == 1 && videos->snapshot().contents.photos.size() == 1);
    CHECK(favorite->snapshot().contents.photos.size() == 1 && deleted->snapshot().contents.photos.size() == 1);
    CHECK(hidden->snapshot().contents.photos.size() == 1);
    CHECK(manager.smartAlbum(SmartAlbumKind::Screenshots)->snapshot().contents.photos.empty());
    CHECK(manager.smartAlbum(SmartAlbumKind::Imported)->snapshot().contents.photos.empty());
    const auto imageId = find(images, "image").id;
    const auto videoId = find(videos, "video").id;
    manager.setFavorite(imageId, true);
    CHECK(manager.isFavorite(imageId) && find(local, "image").favorite && favorite->snapshot().contents.photos.size() == 2);
    manager.setDeleted(videoId, true);
    CHECK(videos->snapshot().contents.photos.empty() && favorite->snapshot().contents.photos.size() == 1);
    CHECK(deleted->snapshot().contents.photos.size() == 2 && manager.allLibrary()->snapshot().contents.photos.size() == 1);
    CHECK(manager.photoTraits(videoId).deleted && find(local, "video").traits.deleted);
    manager.setFavorite(videoId, false);
    CHECK(!manager.isFavorite(videoId) && !find(deleted, "video").favorite);
    manager.setDeleted(find(deleted, "video").id, false);
    CHECK(videos->snapshot().contents.photos.size() == 1 && favorite->snapshot().contents.photos.size() == 1);
    manager.setDeleted(imageId, true); manager.setDeleted(imageId, false);
    CHECK(find(images, "image").favorite && favorite->snapshot().contents.photos.size() == 1);
    manager.setDeleted(find(deleted, "deleted-shot").id, false);
    CHECK(manager.smartAlbum(SmartAlbumKind::Screenshots)->snapshot().contents.photos.size() == 1);
    CHECK(manager.smartAlbum(SmartAlbumKind::Imported)->snapshot().contents.photos.size() == 1);
    CHECK(images->snapshot().contents.photos.size() == 2 && favorite->snapshot().contents.photos.size() == 2);
    const auto hiddenId = find(hidden, "hidden").id;
    manager.setDeleted(hiddenId, true);
    CHECK(hidden->snapshot().contents.photos.empty() && find(deleted, "hidden").traits.hidden);
    manager.setDeleted(hiddenId, false);
    CHECK(hidden->snapshot().contents.photos.size() == 1 && manager.allLibrary()->snapshot().contents.photos.size() == 3);
    CHECK(before.contents.photos.size() == 2 && !before.contents.photos.front().favorite);
    fails(AlbumErrorCode::NotFound, [&] { manager.setFavorite("unknown", true); });
    fails(AlbumErrorCode::NotFound, [&] { manager.isFavorite("unknown"); });
    fails(AlbumErrorCode::NotFound, [&] { manager.setDeleted("unknown", true); });
    fails(AlbumErrorCode::InvalidSnapshot, [&] {
        manager.setPhotoTraits(imageId, {.contentHash = "invalid", .favorite = false, .deleted = true});
    });
    CHECK(manager.isFavorite(imageId) && !manager.photoTraits(imageId).deleted);
    image.mediaType = "video";
    manager.setLocalPhotos(local, {image});
    CHECK(images->snapshot().contents.photos.empty() && videos->snapshot().contents.photos.size() == 1);
}
void favoriteDeletionAcrossMembershipsAndRefresh() {
    AlbumManager manager;
    auto first = manager.connect({"remote", "account", "First", "a"});
    auto second = manager.connect({"remote", "account", "Second", "b"});
    auto plain = photo("same", {.contentHash = hashA});
    auto marked = plain; marked.favorite = true;
    manager.publish(first, {{plain}, true, "one"});
    manager.publish(second, {{marked}, true, "two"});
    const auto id = find(first, "same").id;
    auto favorite = manager.smartAlbum(SmartAlbumKind::Favorite);
    auto deleted = manager.smartAlbum(SmartAlbumKind::Deleted);
    CHECK(favorite->snapshot().contents.photos.size() == 1 && manager.isFavorite(id));
    manager.setFavorite(id, false);
    CHECK(!find(first, "same").favorite && !find(second, "same").favorite && favorite->snapshot().contents.photos.empty());
    manager.setDeleted(id, true);
    CHECK(find(second, "same").traits.deleted && manager.allLibrary()->snapshot().contents.photos.empty());
    CHECK(deleted->snapshot().contents.photos.size() == 1);
    manager.publish(second, {{marked}, true, "refresh"});
    CHECK(!manager.isFavorite(id) && manager.photoTraits(id).deleted);
    manager.setPhotoTraits(find(deleted, "same").id, {.favorite = true, .deleted = false});
    CHECK(manager.isFavorite(id) && manager.allLibrary()->snapshot().contents.photos.size() == 1);
    manager.setLocalPhotos(manager.createLocal("copy", "Copy"), {photo("copy", {.contentHash = hashA})});
    CHECK(manager.duplicateGroups().size() == 1);
    manager.setDeleted(id, true);
    CHECK(manager.duplicateGroups().empty() && manager.smartAlbum(SmartAlbumKind::Duplicates)->snapshot().contents.complete);
    auto source = std::make_shared<Source>(); source->result.photos[0].favorite = true;
    auto remote = manager.connect({"remote", "another-account", "Remote"}, source);
    manager.refresh(remote);
    const auto remoteId = find(remote, "remote").id;
    manager.setPhotoTraits(remoteId, {.favorite = false, .deleted = true});
    manager.refresh(remote);
    CHECK(!manager.isFavorite(remoteId) && manager.photoTraits(remoteId).deleted);
    source->onRead = [&] { manager.setDeleted(remoteId, false); };
    manager.refresh(remote); source->onRead = {};
    CHECK(!manager.photoTraits(remoteId).deleted && !manager.isFavorite(remoteId));
    manager.setDeleted(remoteId, true);
    source->error = AlbumErrorCode::Transport;
    fails(*source->error, [&] { manager.refresh(remote); });
    CHECK(find(deleted, "remote").traits.deleted && !deleted->snapshot().contents.complete);
    source->error = AlbumErrorCode::PermissionDenied;
    fails(*source->error, [&] { manager.refresh(remote); });
    fails(AlbumErrorCode::NotFound, [&] { manager.isFavorite(remoteId); });
    source->error.reset(); manager.refresh(remote);
    CHECK(manager.isFavorite(remoteId) && !manager.photoTraits(remoteId).deleted);
    manager.setDeleted(remoteId, true);
    source->result = {{}, false, "partial"}; manager.refresh(remote);
    CHECK(find(deleted, "remote").traits.deleted);
    source->result.complete = true; manager.refresh(remote);
    source->result = {{photo("remote")}, true, "again"}; manager.refresh(remote);
    CHECK(!manager.photoTraits(remoteId).deleted);
}
void concurrentUpdatesAndLifetime() {
    auto manager = std::make_unique<AlbumManager>();
    const auto views = manager->smartAlbums();
    auto local = manager->createLocal("local", "Local");
    manager->setLocalPhotos(local, {photo("one")});
    const auto id = find(local, "one").id;
    std::atomic<bool> reading = true;
    std::promise<void> entered;
    auto enteredFuture = entered.get_future();
    auto reader = std::async(std::launch::async, [&] {
        bool valid = true;
        entered.set_value();
        while (reading.load())
            for (const auto &value : views[0]->snapshot().contents.photos)
                valid = valid && value.traits.screenshot == value.traits.imported && value.traits.screenshot == value.favorite;
        return valid;
    });
    enteredFuture.wait();
    for (int index = 0; index < 100; ++index) {
        const bool marked = index % 2 != 0;
        manager->setPhotoTraits(id, {.screenshot = marked, .imported = marked, .favorite = marked, .deleted = false});
    }
    reading.store(false);
    CHECK(reader.get());
    manager.reset();
    for (const auto &view : views) {
        const auto data = view->snapshot();
        CHECK(data.state == AlbumState::Disconnected && data.contents.photos.empty() && !data.contents.complete);
    }
}
}
int main() {
    try {
        objectsAndReadOnly(); classificationsAndVisibility(); identitiesAndConflictingHashes();
        refreshAndOverrideLifetime(); mediaFavoriteAndDeleted(); favoriteDeletionAcrossMembershipsAndRefresh();
        concurrentUpdatesAndLifetime();
        std::cout << "smart albums: " << checks << " checks passed\n";
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
