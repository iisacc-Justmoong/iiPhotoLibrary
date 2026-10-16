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
AlbumPhoto photo(std::string id, std::string name = "photo.jpg") {
    AlbumPhoto value;
    value.sourcePhotoId = std::move(id); value.name = std::move(name);
    return value;
}
const AlbumPhoto &findPhoto(const AlbumData &data, const std::string &id) {
    const auto found = std::find_if(data.contents.photos.begin(), data.contents.photos.end(),
        [&](const AlbumPhoto &value) { return value.sourcePhotoId == id; });
    if (found == data.contents.photos.end()) throw std::runtime_error("photo not found");
    return *found;
}
class Source final : public AlbumSource {
  public:
    std::string provider = "remote";
    AlbumCoverage scope = AlbumCoverage::External;
    AlbumSnapshot result{{photo("network")}, true, "one"};
    std::optional<AlbumErrorCode> error;
    std::function<AlbumSnapshot(std::stop_token)> onRead;
    int reads = 0;
    std::string providerId() const override { return provider; }
    AlbumCoverage coverage() const override { return scope; }
    AlbumSnapshot read(std::stop_token stop) override {
        ++reads;
        if (error) throw AlbumError(*error, "fixture error");
        return onRead ? onRead(stop) : result;
    }
};
void localAlbums() {
    AlbumManager manager;
    auto all = manager.allLibrary();
    const auto empty = all->snapshot();
    CHECK(all && manager.allLibrary() == all && manager.album(all->id()) == all);
    CHECK(empty.connection.title == "All Library");
    CHECK(empty.connection.origin == AlbumOrigin::AllLibrary && empty.coverage == AlbumCoverage::AllLibrary);
    CHECK(empty.state == AlbumState::Ready && empty.contents.complete && empty.contents.photos.empty());
    CHECK(manager.albums().empty());
    auto first = manager.createLocal("a", "Same title");
    auto second = manager.createLocal("b", "Same title");
    auto shared = photo("shared", "first.jpg");
    shared.properties["allLibrary.providerId"] = "untrusted";
    manager.setLocalPhotos(first, {shared, photo("first-only")});
    manager.setLocalPhotos(second, {photo("shared", "second.jpg"), photo("second-only")});
    const auto before = all->snapshot();
    CHECK(before.contents.photos.size() == 3 && before.contents.complete);
    CHECK(manager.albums().size() == 2 && manager.allLibrary() == all);
    const auto &unique = findPhoto(before, "shared");
    CHECK(unique.name == "first.jpg");
    CHECK(unique.id != first->snapshot().contents.photos.back().id);
    CHECK(unique.properties.at("allLibrary.providerId") == "local");
    CHECK(unique.properties.at("allLibrary.accountId").empty());
    CHECK(unique.properties.at("allLibrary.albumId") == first->id());
    CHECK(unique.properties.at("allLibrary.photoId") == first->snapshot().contents.photos.back().id);
    manager.setLocalPhotos(first, {photo("updated")});
    const auto after = all->snapshot();
    CHECK(after.contents.photos.size() == 3 && findPhoto(after, "shared").name == "second.jpg");
    CHECK(findPhoto(after, "shared").id == unique.id);
    CHECK(findPhoto(after, "shared").properties.at("allLibrary.albumId") == second->id());
    CHECK(before.contents.photos.size() == 3 && findPhoto(before, "shared").name == "first.jpg");
    CHECK(manager.disconnect(second));
    CHECK(all->snapshot().contents.photos.size() == 1);
    CHECK(manager.disconnect(first));
    CHECK(all->snapshot().contents.photos.empty() && all->snapshot().contents.complete);
}
void identitiesAndReconnection() {
    AlbumManager manager;
    auto all = manager.allLibrary();
    const AlbumConnection connection{"remote", "account-a", "Same title", "a"};
    auto first = manager.connect(connection);
    CHECK(!all->snapshot().contents.complete);
    manager.publish(first, {{photo("same"), photo("first-only")}, true, "one"});
    auto second = manager.connect({"remote", "account-a", "Same title", "b"});
    manager.publish(second, {{photo("same")}, true, "two"});
    CHECK(all->snapshot().contents.photos.size() == 2 && all->snapshot().contents.complete);
    auto account = manager.connect({"remote", "account-b", "Same title", "a"});
    manager.publish(account, {{photo("same")}, true, "three"});
    auto provider = manager.connect({"another", "account-a", "Same title", "a"});
    manager.publish(provider, {{photo("same")}, true, "four"});
    auto colonOne = manager.connect({"remote:a", "b", "Colon", "a"});
    auto colonTwo = manager.connect({"remote", "a:b", "Colon", "a"});
    manager.publish(colonOne, {{photo("same")}, true, "five"});
    manager.publish(colonTwo, {{photo("same")}, true, "six"});
    const auto data = all->snapshot();
    CHECK(data.contents.photos.size() == 6 && data.contents.complete);
    std::vector<std::string> ids;
    for (const auto &value : data.contents.photos) ids.push_back(value.id);
    std::sort(ids.begin(), ids.end());
    CHECK(std::adjacent_find(ids.begin(), ids.end()) == ids.end());
    auto replacement = manager.connect(connection);
    CHECK(first->snapshot().state == AlbumState::Disconnected);
    CHECK(manager.allLibrary() == all && all->snapshot().contents.photos.size() == 5);
    manager.publish(replacement, {{photo("new")}, false, "partial"});
    CHECK(all->snapshot().contents.photos.size() == 6 && !all->snapshot().contents.complete);
    manager.publish(replacement, {{photo("new")}, true, "complete"});
    CHECK(all->snapshot().contents.complete);
}
void sourceReadsAndErrors() {
    AlbumManager manager;
    auto all = manager.allLibrary();
    auto local = manager.createLocal("camera", "Camera");
    manager.setLocalPhotos(local, {photo("local")});
    auto source = std::make_shared<Source>();
    auto remote = manager.connect({"remote", "account", "Remote"}, source);
    CHECK(all->snapshot().contents.photos.size() == 1 && source->reads == 0);
    CHECK(!all->snapshot().contents.complete);
    manager.refresh(remote);
    CHECK(all->snapshot().contents.photos.size() == 2 && all->snapshot().contents.complete && source->reads == 1);
    source->error = AlbumErrorCode::Transport;
    fails(*source->error, [&] { manager.refresh(remote); });
    CHECK(all->snapshot().contents.photos.size() == 2 && !all->snapshot().contents.complete);
    source->error = AlbumErrorCode::PermissionDenied;
    fails(*source->error, [&] { manager.refresh(remote); });
    CHECK(all->snapshot().contents.photos.size() == 1 && findPhoto(all->snapshot(), "local").sourcePhotoId == "local");
    CHECK(all->snapshot().state == AlbumState::Ready && !all->snapshot().error && !all->snapshot().contents.complete);
    source->error.reset();
    manager.refresh(remote);
    CHECK(all->snapshot().contents.complete);
    manager.disconnect(remote);
    CHECK(all->snapshot().contents.photos.size() == 1 && all->snapshot().contents.complete);
}
void loadingAndCancellation() {
    AlbumManager manager;
    auto all = manager.allLibrary();
    auto source = std::make_shared<Source>();
    auto remote = manager.connect({"remote", "account", "Remote"}, source);
    manager.publish(remote, {{photo("cached")}, true, "cached"});
    std::promise<void> entered, release;
    auto enteredFuture = entered.get_future();
    auto released = release.get_future().share();
    source->onRead = [&](std::stop_token) {
        entered.set_value(); released.wait(); return source->result;
    };
    auto pending = std::async(std::launch::async, [&] {
        try { manager.refresh(remote); } catch (const AlbumError &error) { return error.code(); }
        return AlbumErrorCode::Unsupported;
    });
    enteredFuture.wait();
    const auto loading = all->snapshot();
    manager.disconnect(remote);
    const auto disconnected = all->snapshot();
    release.set_value();
    const auto result = pending.get();
    CHECK(loading.contents.photos.size() == 1 && !loading.contents.complete);
    CHECK(disconnected.contents.photos.empty() && disconnected.contents.complete);
    CHECK(result == AlbumErrorCode::StaleConnection);
    CHECK(all->snapshot().contents.photos.empty() && source->reads == 1);
}
void readOnlyAndLimits() {
    AlbumManager manager({2, 1});
    auto all = manager.allLibrary();
    fails(AlbumErrorCode::ReadOnly, [&] { manager.publish(all, {}); });
    fails(AlbumErrorCode::ReadOnly, [&] { manager.setLocalPhotos(all, {}); });
    fails(AlbumErrorCode::ReadOnly, [&] { manager.refresh(all); });
    CHECK(!manager.disconnect(all) && manager.allLibrary() == all);
    AlbumManager foreign;
    fails(AlbumErrorCode::StaleConnection, [&] { foreign.publish(all, {}); });
    invalid([&] { manager.connect({"all-library", "account", "Spoof"}); });
    invalid([&] { manager.connect({"remote", "account", "Spoof", "library", AlbumOrigin::AllLibrary}); });
    invalid([&] { manager.connect({"remote", "account", "Spoof"}, {}, AlbumCoverage::AllLibrary); });
    auto source = std::make_shared<Source>(); source->scope = AlbumCoverage::AllLibrary;
    invalid([&] { manager.connect({"remote", "account", "Spoof"}, source); });
    auto first = manager.createLocal("all-library", "First");
    auto second = manager.createLocal("second", "Second");
    manager.setLocalPhotos(first, {photo("one")});
    manager.setLocalPhotos(second, {photo("two")});
    CHECK(all->snapshot().contents.photos.size() == 2);
    CHECK(manager.albums().size() == 2 && first->id() != all->id());
    fails(AlbumErrorCode::LimitExceeded, [&] { manager.createLocal("third", "Third"); });
    CHECK(all->snapshot().contents.photos.size() == 2 && all->snapshot().state == AlbumState::Ready);
}
void lifetime() {
    std::shared_ptr<Album> surviving;
    AlbumData before;
    {
        AlbumManager manager;
        surviving = manager.allLibrary();
        auto local = manager.createLocal("local", "Local");
        manager.setLocalPhotos(local, {photo("one")});
        before = surviving->snapshot();
    }
    const auto after = surviving->snapshot();
    CHECK(before.contents.photos.size() == 1 && before.contents.complete);
    CHECK(after.id == before.id && after.connection.title == "All Library");
    CHECK(after.state == AlbumState::Disconnected && after.contents.photos.empty() && !after.contents.complete);
    CHECK(surviving->snapshot().contents.photos.empty());
}
void concurrentDestruction() {
    auto manager = std::make_unique<AlbumManager>();
    auto all = manager->allLibrary();
    auto local = manager->createLocal("local", "Local");
    manager->setLocalPhotos(local, {photo("one")});
    std::atomic<bool> reading = true;
    std::promise<void> entered;
    auto enteredFuture = entered.get_future();
    auto reader = std::async(std::launch::async, [&] {
        bool valid = true;
        const auto first = all->snapshot();
        valid = first.state == AlbumState::Ready && first.contents.photos.size() == 1;
        entered.set_value();
        while (reading.load()) {
            const auto data = all->snapshot();
            valid = valid && ((data.state == AlbumState::Ready && data.contents.photos.size() == 1) ||
                (data.state == AlbumState::Disconnected && data.contents.photos.empty()));
        }
        return valid && all->snapshot().state == AlbumState::Disconnected;
    });
    enteredFuture.wait();
    manager.reset();
    reading.store(false);
    CHECK(reader.get());
    CHECK(all->snapshot().contents.photos.empty());
}
}
int main() {
    try {
        localAlbums(); identitiesAndReconnection(); sourceReadsAndErrors();
        loadingAndCancellation(); readOnlyAndLimits(); lifetime(); concurrentDestruction();
        std::cout << "all library: " << checks << " checks passed\n";
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
