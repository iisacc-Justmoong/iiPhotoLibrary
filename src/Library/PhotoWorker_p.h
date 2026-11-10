#pragma once

#include <QFuture>
#include <QJsonObject>
#include <QMetaObject>
#include <QObject>
#include <QPromise>
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace iiPhotoLibrary::detail {

// Posting and owner destruction serialize; queued callbacks recheck the gate.
class CallbackGate final : public std::enable_shared_from_this<CallbackGate> {
public:
    explicit CallbackGate(QObject *owner) : owner_(owner) {}
    void close() { std::lock_guard lock(mutex_); owner_ = nullptr; }
    template<class F> void post(F callback) {
        std::lock_guard lock(mutex_);
        if (!owner_) return;
        auto gate = shared_from_this();
        QMetaObject::invokeMethod(owner_, [gate, callback = std::move(callback)]() mutable {
            { std::lock_guard guard(gate->mutex_); if (!gate->owner_) return; }
            callback();
        }, Qt::QueuedConnection);
    }
private:
    std::mutex mutex_;
    QObject *owner_;
};

// A store has one serial writer. During final shutdown only the active task
// may outlive the owner; it holds its own store and callback gate by value.
class PhotoWorker final {
    struct State {
        std::mutex mutex;
        std::condition_variable changed, idle;
        std::deque<std::function<void()>> tasks;
        bool active = false, stopping = false;
    };
public:
    PhotoWorker() : state_(std::make_shared<State>()), thread_([state = state_] { run(state); }) {}
    ~PhotoWorker() { shutdown(false); }
    QFuture<QJsonObject> submit(std::function<QJsonObject()> operation) {
        auto promise = std::make_shared<QPromise<QJsonObject>>();
        promise->start();
        auto future = promise->future();
        {
            std::lock_guard lock(state_->mutex);
            if (state_->stopping) { promise->finish(); return future; }
            state_->tasks.push_back([promise, operation = std::move(operation)] {
                try { promise->addResult(operation()); }
                catch (const std::exception &error) {
                    promise->addResult(QJsonObject{{"ok", false}, {"error", QString::fromUtf8(error.what())}});
                } catch (...) {
                    promise->addResult(QJsonObject{{"ok", false}, {"error", QStringLiteral("photo_worker_failed")}});
                }
                promise->finish();
            });
        }
        state_->changed.notify_one();
        return future;
    }
    void waitForDone() {
        std::unique_lock lock(state_->mutex);
        state_->idle.wait(lock, [state = state_] { return !state->active && state->tasks.empty(); });
    }
    void shutdown(bool detachActive) {
        if (!thread_.joinable()) return;
        {
            std::lock_guard lock(state_->mutex);
            state_->stopping = true;
            if (detachActive) state_->tasks.clear();
        }
        state_->changed.notify_one();
        if (detachActive) thread_.detach(); else thread_.join();
    }
private:
    static void run(const std::shared_ptr<State> &state) {
        std::unique_lock lock(state->mutex);
        for (;;) {
            state->changed.wait(lock, [&] { return state->stopping || !state->tasks.empty(); });
            if (state->tasks.empty() && state->stopping) break;
            auto task = std::move(state->tasks.front());
            state->tasks.pop_front(); state->active = true;
            lock.unlock(); task(); lock.lock();
            state->active = false;
            state->idle.notify_all();
        }
        state->idle.notify_all();
    }
    std::shared_ptr<State> state_;
    std::thread thread_;
};
} // namespace iiPhotoLibrary::detail
