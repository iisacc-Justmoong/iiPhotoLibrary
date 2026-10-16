#include <iiFileProviderDevice.h>
#include <iiPhotoLibrary/PhotoLibrary.h>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QFileInfo>
#include <QDir>
#include <QHash>
#include <QCryptographicHash>
#include <QUuid>
#include <QJsonArray>
#include <QMetaObject>
#include <QTimeZone>
#include <QMutex>
#include <QElapsedTimer>
#include <QUrl>
#include <atomic>
#import <Foundation/Foundation.h>
#import <Photos/Photos.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#if TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
#else
#import <AppKit/AppKit.h>
#endif

namespace iiPhotoLibrary {
namespace {
#if TARGET_OS_IPHONE
using PlatformImage = UIImage;
#else
using PlatformImage = NSImage;
#endif
bool failure(QString *error, NSString *message) {
    if (error) *error = QString::fromNSString(message ?: @"The photo library operation failed."); return false;
}
PHAsset *findAsset(const QString &identifier) {
    return [PHAsset fetchAssetsWithLocalIdentifiers:@[identifier.toNSString()] options:nil].firstObject;
}
NSString *resourceName(PHAssetResource *resource) {
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 270000 || __IPHONE_OS_VERSION_MAX_ALLOWED >= 270000
    if (@available(macOS 27.0, iOS 27.0, *)) return resource.filename ?: @"photo";
#endif
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    return resource.originalFilename;
#pragma clang diagnostic pop
}
NSString *cloudString(PHCloudIdentifier *identifier) {
    if (@available(macOS 15.2, iOS 18.2, *)) return identifier.archivalStringValue;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    return identifier.stringValue;
#pragma clang diagnostic pop
}
PHCloudIdentifier *cloudIdentifier(NSString *string) {
    if (@available(macOS 15.2, iOS 18.2, *)) return [[PHCloudIdentifier alloc] initWithArchivalStringValue:string];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    return [[PHCloudIdentifier alloc] initWithStringValue:string];
#pragma clang diagnostic pop
}
struct NativeReply {
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    NSError *__strong error = nil;
    NSString *__strong identifier = nil;
    NSData *__strong image = nil;
    bool success = false;
};
QString role(PHAssetResourceType type) {
    switch (type) {
    case PHAssetResourceTypePhoto: return "photo";
    case PHAssetResourceTypeVideo: return "video";
    case PHAssetResourceTypePairedVideo: return "pairedVideo";
    case PHAssetResourceTypeAlternatePhoto: return "alternatePhoto";
    default: return {};
    }
}
PHAssetResourceType type(const QString &role) {
    if (role == "video") return PHAssetResourceTypeVideo;
    if (role == "pairedVideo") return PHAssetResourceTypePairedVideo;
    if (role == "alternatePhoto") return PHAssetResourceTypeAlternatePhoto;
    return PHAssetResourceTypePhoto;
}
class AppleLibrary final : public PhotoLibrary {
    std::atomic_bool stopping{false};
    QHash<QString, QString> importedIdentifiers;
    bool indexed = false;
    bool wait(const std::shared_ptr<NativeReply> &reply, int timeoutMs = 0) const {
        QElapsedTimer elapsed; elapsed.start();
        while (dispatch_semaphore_wait(reply->done, dispatch_time(DISPATCH_TIME_NOW, 250 * NSEC_PER_MSEC)))
            if (stopping || (timeoutMs > 0 && elapsed.elapsed() >= timeoutMs)) return false;
        return !stopping;
    }
public:
    void cancel() override { stopping = true; }
    QString name() const override { return "Apple Photos"; }
    Access access() const override {
        switch ([PHPhotoLibrary authorizationStatusForAccessLevel:PHAccessLevelReadWrite]) {
        case PHAuthorizationStatusAuthorized: return Access::Full;
        case PHAuthorizationStatusLimited: return Access::Limited;
        case PHAuthorizationStatusNotDetermined: return Access::NotDetermined;
        default: return Access::Denied;
        }
    }
    void authorize(std::function<void()> finished) override {
        if (access() == Access::Denied || access() == Access::Limited) {
#if TARGET_OS_IPHONE
            [[UIApplication sharedApplication] openURL:[NSURL URLWithString:UIApplicationOpenSettingsURLString] options:@{} completionHandler:nil];
#else
            QDesktopServices::openUrl(QUrl("x-apple.systempreferences:com.apple.preference.security?Privacy_Photos"));
#endif
            finished(); return;
        }
        [PHPhotoLibrary requestAuthorizationForAccessLevel:PHAccessLevelReadWrite handler:^(PHAuthorizationStatus) {
            QMetaObject::invokeMethod(qApp, std::move(finished), Qt::QueuedConnection);
        }];
    }
    Snapshot scan(QString *error) override {
        @autoreleasepool {
            Snapshot result;
            const auto permission = access();
            if (permission != Access::Full && permission != Access::Limited) { failure(error, @"Allow Society to access Photos in system settings."); return result; }
            PHFetchOptions *options = [PHFetchOptions new];
            options.includeHiddenAssets = YES; options.includeAllBurstAssets = YES;
            options.predicate = [NSPredicate predicateWithFormat:@"mediaType == %d OR mediaType == %d", PHAssetMediaTypeImage, PHAssetMediaTypeVideo];
            PHFetchResult<PHAsset *> *assets = [PHAsset fetchAssetsWithOptions:options];
            importedIdentifiers.clear(); indexed = false;
            for (NSUInteger start = 0; start < assets.count; start += 100) {
                if (stopping) { failure(error, @"The photo session ended."); return {}; }
                for (NSUInteger i = start; i < MIN(assets.count, start + 100); ++i) {
                    PHAsset *native = assets[i]; Asset asset;
                    asset.identifier = QString::fromNSString(native.localIdentifier);
                    asset.media = native.mediaType == PHAssetMediaTypeVideo ? "video" : "photo";
                    asset.created = QDateTime::fromMSecsSinceEpoch(qRound64(native.creationDate.timeIntervalSince1970 * 1000), QTimeZone::UTC);
                    asset.stamp = QString::number(qRound64(native.modificationDate.timeIntervalSince1970 * 1000))
                        + ':' + QString::number(native.pixelWidth) + ':' + QString::number(native.pixelHeight);
                    asset.favorite = native.favorite;
                    asset.screenshot = (native.mediaSubtypes & PHAssetMediaSubtypePhotoScreenshot) != 0;
                    asset.hidden = native.hidden;
                    NSArray<PHAssetResource *> *resources = [PHAssetResource assetResourcesForAsset:native];
                    for (NSUInteger index = 0; index < resources.count; ++index) {
                        PHAssetResource *resource = resources[index]; const auto resourceRole = role(resource.type);
                        if (resourceRole.isEmpty()) continue;
                        asset.resources.append({QString::number(index), QString::fromNSString(resourceName(resource)), resourceRole});
                        const auto imported = importedPhotoId(asset.resources.last().name);
                        if (!imported.isEmpty()) { importedIdentifiers[imported] = asset.identifier; asset.imported = true; }
                    }
                    if (!asset.resources.isEmpty()) { asset.name = asset.resources.first().name; result.assets.append(asset); }
                }
            }
            result.complete = permission == Access::Full && access() == Access::Full;
            indexed = true;
            return result;
        }
    }
    bool resolveIdentifiers(Snapshot &snapshot, QString *error) override {
        @autoreleasepool {
            if (@available(macOS 12.0, iOS 15.0, *)) {
                for (qsizetype start = 0; start < snapshot.assets.size(); start += 100) {
                    if (stopping) return failure(error, @"The photo session ended.");
                    const auto end = std::min(snapshot.assets.size(), start + 100);
                    NSMutableArray<NSString *> *identifiers = [NSMutableArray array];
                    for (auto i = start; i < end; ++i) [identifiers addObject:snapshot.assets[i].identifier.toNSString()];
                    NSDictionary<NSString *, PHCloudIdentifierMapping *> *cloud = [[PHPhotoLibrary sharedPhotoLibrary]
                        cloudIdentifierMappingsForLocalIdentifiers:identifiers];
                    for (auto i = start; i < end; ++i) {
                        auto &asset = snapshot.assets[i];
                        asset.cloudIdentifier = QString::fromNSString(cloudString(cloud[asset.identifier.toNSString()].cloudIdentifier));
                    }
                }
            }
            return !stopping;
        }
    }
    bool preview(const QString &identifier, const QString &destination, QString *error) override {
        @autoreleasepool {
            PHAsset *asset = findAsset(identifier); if (!asset) return failure(error, @"The photo is no longer accessible.");
            PHImageRequestOptions *options = [PHImageRequestOptions new];
            options.networkAccessAllowed = YES; options.synchronous = NO;
            options.deliveryMode = PHImageRequestOptionsDeliveryModeHighQualityFormat;
            options.resizeMode = PHImageRequestOptionsResizeModeExact;
            const auto reply = std::make_shared<NativeReply>();
            const auto request = [[PHImageManager defaultManager] requestImageForAsset:asset targetSize:CGSizeMake(1024, 1024)
                contentMode:PHImageContentModeAspectFit options:options resultHandler:^(PlatformImage *image, NSDictionary *info) {
                    if ([info[PHImageResultIsDegradedKey] boolValue]) return;
                    reply->error = info[PHImageErrorKey];
#if TARGET_OS_IPHONE
                    if (image) reply->image = UIImageJPEGRepresentation(image, 0.85);
#else
                    if (image) {
                        NSBitmapImageRep *bitmap = [NSBitmapImageRep imageRepWithData:image.TIFFRepresentation];
                        reply->image = [bitmap representationUsingType:NSBitmapImageFileTypeJPEG properties:@{NSImageCompressionFactor: @0.85}];
                    }
#endif
                    dispatch_semaphore_signal(reply->done);
                }];
            if (!wait(reply, 5000)) {
                [[PHImageManager defaultManager] cancelImageRequest:request];
                return failure(error, stopping ? @"The photo session ended." : @"The photo preview will retry on the next scan.");
            }
            if (!reply->image) return failure(error, reply->error.localizedDescription ?: @"The photo preview is not available yet.");
            NSError *writeError = nil;
            if (![reply->image writeToFile:destination.toNSString() options:NSDataWritingAtomic error:&writeError]) return failure(error, writeError.localizedDescription);
            return true;
        }
    }
    bool exportResource(const QString &identifier, const QString &token, const QString &destination, QString *error) override {
        @autoreleasepool {
            PHAsset *asset = findAsset(identifier); if (!asset) return failure(error, @"The photo original is no longer accessible.");
            NSArray<PHAssetResource *> *resources = [PHAssetResource assetResourcesForAsset:asset];
            bool valid; const int index = token.toInt(&valid);
            if (!valid || index < 0 || NSUInteger(index) >= resources.count) return failure(error, @"The photo resource changed.");
            PHAssetResourceRequestOptions *options = [PHAssetResourceRequestOptions new];
            options.networkAccessAllowed = YES;
            struct Output { QMutex mutex; iiFileProvider::FileDevice file; bool cancelled = false, failed = false; explicit Output(const QString &path) : file(path) {} };
            const auto output = std::make_shared<Output>(destination); const auto reply = std::make_shared<NativeReply>();
            if (!output->file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return failure(error, @"Could not stage the photo original.");
            const auto request = [[PHAssetResourceManager defaultManager] requestDataForAssetResource:resources[index] options:options
                dataReceivedHandler:^(NSData *data) {
                    QMutexLocker lock(&output->mutex);
                    if (!output->cancelled && !output->failed)
                        output->failed = output->file.write(static_cast<const char *>(data.bytes), data.length) != qint64(data.length);
                } completionHandler:^(NSError *e) { reply->error = e; dispatch_semaphore_signal(reply->done); }];
            const bool complete = wait(reply);
            if (!complete) [[PHAssetResourceManager defaultManager] cancelDataRequest:request];
            QMutexLocker lock(&output->mutex); output->cancelled = true; output->file.close();
            if (!complete || output->failed) return failure(error, @"The photo transfer was interrupted.");
            return reply->error ? failure(error, reply->error.localizedDescription) : true;
        }
    }
    QString import(const QJsonObject &record, const QStringList &paths, QString *error) override {
        @autoreleasepool {
            const auto id = record.value("id").toString(), cloud = record.value("cloud").toString();
            const auto resources = record.value("resources").toArray();
            if (paths.isEmpty() || paths.size() != resources.size()) { failure(error, @"The photo original is incomplete."); return {}; }
            const auto matchingOriginal = [&](const QString &identifier) -> QString {
                PHAsset *asset = findAsset(identifier);
                if (!asset) return {};
                NSArray<PHAssetResource *> *native = [PHAssetResource assetResourcesForAsset:asset]; int index = 0;
                int count = 0; for (PHAssetResource *resource in native) if (!role(resource.type).isEmpty()) ++count;
                if (count != resources.size()) { failure(error, @"This native photo has a different resource set. Its existing original was preserved."); return {}; }
                for (NSUInteger i = 0; i < native.count; ++i) {
                    const auto nativeRole = role(native[i].type); if (nativeRole.isEmpty()) continue;
                    if (index >= resources.size() || resources[index].toObject().value("role") != nativeRole) break;
                    const auto path = QFileInfo(paths.first()).dir().filePath("verify-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
                    if (!exportResource(identifier, QString::number(i), path, error)) { iiFileProvider::FileDevice::remove(path); return {}; }
                    iiFileProvider::FileDevice file(path); QCryptographicHash digest(QCryptographicHash::Sha256);
                    const bool read = file.open(QIODevice::ReadOnly) && digest.addData(&file); file.close(); iiFileProvider::FileDevice::remove(path);
                    if (!read || QString::fromLatin1(digest.result().toHex()) != resources[index].toObject().value("hash").toString()) break;
                    ++index;
                }
                if (index == resources.size()) return identifier;
                failure(error, @"This photo has a different native original. Its existing original was preserved."); return {};
            };
            if (!cloud.isEmpty()) {
                if (@available(macOS 12.0, iOS 15.0, *)) {
                    PHCloudIdentifier *identifier = cloudIdentifier(cloud.toNSString());
                    PHLocalIdentifierMapping *mapping = identifier ? [[PHPhotoLibrary sharedPhotoLibrary] localIdentifierMappingsForCloudIdentifiers:@[identifier]][identifier] : nil;
                    if (mapping.localIdentifier && findAsset(QString::fromNSString(mapping.localIdentifier))) return matchingOriginal(QString::fromNSString(mapping.localIdentifier));
                }
            }
            // Recover a completed PhotoKit import after an app crash, including
            // assets that have since arrived through iCloud on another device.
            if (!indexed) { scan(error); if (error && !error->isEmpty()) return {}; }
            const auto existing = importedIdentifiers.value(id);
            if (!existing.isEmpty() && findAsset(existing)) return matchingOriginal(existing);
            NSMutableArray<NSNumber *> *types = [NSMutableArray array];
            for (const auto &value : resources) [types addObject:@(type(value.toObject().value("role").toString()))];
            if (![PHAssetCreationRequest supportsAssetResourceTypes:types]) { failure(error, @"Apple Photos cannot import this resource combination."); return {}; }
            const auto reply = std::make_shared<NativeReply>();
            [[PHPhotoLibrary sharedPhotoLibrary] performChanges:^{
                PHAssetCreationRequest *request = [PHAssetCreationRequest creationRequestForAsset];
                const auto date = QDateTime::fromString(record.value("created").toString(), Qt::ISODateWithMs);
                if (date.isValid()) request.creationDate = [NSDate dateWithTimeIntervalSince1970:date.toMSecsSinceEpoch() / 1000.0];
                for (int i = 0; i < resources.size(); ++i) {
                    const auto resource = resources[i].toObject(); PHAssetResourceCreationOptions *creation = [PHAssetResourceCreationOptions new];
                    creation.shouldMoveFile = NO;
                    creation.originalFilename = importedResourceName(id, i, resource.value("name").toString()).toNSString();
                    const auto extension = resource.value("name").toString().section('.', -1);
                    creation.uniformTypeIdentifier = [UTType typeWithFilenameExtension:extension.toNSString()].identifier;
                    [request addResourceWithType:type(resource.value("role").toString()) fileURL:[NSURL fileURLWithPath:paths[i].toNSString()] options:creation];
                }
                reply->identifier = request.placeholderForCreatedAsset.localIdentifier;
            } completionHandler:^(BOOL success, NSError *error) { reply->success = success; reply->error = error; dispatch_semaphore_signal(reply->done); }];
            if (!wait(reply)) { failure(error, @"The photo import was interrupted."); return {}; }
            if (!reply->success) { failure(error, reply->error.localizedDescription); return {}; }
            const auto identifier = QString::fromNSString(reply->identifier);
            importedIdentifiers[id] = identifier; return identifier;
        }
    }
    bool trash(const QString &identifier, QString *error) override {
        @autoreleasepool {
            PHAsset *asset = findAsset(identifier); if (!asset) return true;
            const auto reply = std::make_shared<NativeReply>();
            [[PHPhotoLibrary sharedPhotoLibrary] performChanges:^{ [PHAssetChangeRequest deleteAssets:@[asset]]; }
                completionHandler:^(BOOL success, NSError *error) { reply->success = success; reply->error = error; dispatch_semaphore_signal(reply->done); }];
            if (!wait(reply)) return failure(error, @"The photo trash operation was interrupted.");
            return reply->success || failure(error, reply->error.localizedDescription);
        }
    }
};
}
std::shared_ptr<PhotoLibrary> nativePhotoLibrary() { return std::make_shared<AppleLibrary>(); }
}
