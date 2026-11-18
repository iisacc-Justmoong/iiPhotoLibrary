#pragma once

#include <QObject>
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QEvent>
#include <QEventLoop>
#include <QList>
#include <QTimer>
#include <QVariant>

#include <algorithm>
#include <exception>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace photo_test {

class CTestRunner final {
public:
    CTestRunner() = default;
    CTestRunner(int argc, char** argv)
    {
        listing_ = argc == 2 && std::string_view(argv[1]) == "-functions";
        if (!listing_)
            for (int i = 1; i < argc; ++i) selection_.emplace_back(argv[i]);
    }

    template <typename Function>
    bool run(std::string_view name, Function&& function)
    {
        const auto separator = name.find(':');
        const auto functionName = name.substr(0, separator);
        if (listing_) {
            if (listed_.emplace(functionName).second) std::cout << functionName << "()\n";
            return true;
        }
        if (!selection_.empty() && std::none_of(selection_.begin(), selection_.end(),
                [&](const auto& selected) { return selected == name || selected == functionName; }))
            return true;
        ++cases_;
        try {
            std::forward<Function>(function)();
            return true;
        } catch (const std::exception& error) {
            ++failures_;
            std::cerr << name << ": " << error.what() << '\n';
        } catch (...) {
            ++failures_;
            std::cerr << name << ": unexpected non-standard exception\n";
        }
        return false;
    }

    [[nodiscard]] int result() const
    {
        if (listing_) return 0;
        if (cases_ == 0) {
            std::cerr << "No test cases were executed\n";
            return 1;
        }
        std::cout << cases_ << " test cases, " << failures_ << " failures\n";
        return failures_ == 0 ? 0 : 1;
    }

private:
    bool listing_ = false;
    std::vector<std::string> selection_;
    std::set<std::string> listed_;
    unsigned cases_ = 0;
    unsigned failures_ = 0;
};

[[noreturn]] inline void fail(const char* file, int line, const char* message)
{
    throw std::runtime_error(std::string(file) + ":" + std::to_string(line) + ": " + message);
}

template <typename Actual, typename Expected>
inline void compare(Actual&& actual, Expected&& expected,
                    const char* file, int line, const char* message)
{
    if (!(actual == expected))
        fail(file, line, message);
}

class ScopedDeferredDeletes final {
public:
    ~ScopedDeferredDeletes()
    {
        if (QCoreApplication::instance())
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
};

inline void processFor(int milliseconds)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

template <typename Predicate>
bool waitFor(Predicate&& predicate, int timeoutMs)
{
    QDeadlineTimer deadline(timeoutMs);
    while (!predicate()) {
        if (deadline.hasExpired()) return false;
        QEventLoop loop;
        QTimer::singleShot(10, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return true;
}

class SignalCounter final {
public:
    template <typename Sender, typename Signal>
    SignalCounter(Sender* sender, Signal signal)
    {
        const auto connection = QObject::connect(sender, signal, &receiver_, [this] { ++count_; });
        if (!connection)
            throw std::runtime_error("Failed to connect signal counter");
    }
    [[nodiscard]] int count() const { return count_; }
    [[nodiscard]] int size() const { return count_; }
    [[nodiscard]] bool isEmpty() const { return count_ == 0; }
private:
    int count_ = 0;
    QObject receiver_;
};

template <typename Signature> class SignalRecorder;

template <typename Sender, typename... Args>
class SignalRecorder<void (Sender::*)(Args...)> final {
public:
    template <typename Object>
    SignalRecorder(Object* sender, void (Sender::*signal)(Args...))
    {
        if (!QObject::connect(sender, signal, &receiver_, [this](Args... args) {
            rows_.append(QVariantList{QVariant::fromValue(std::decay_t<Args>(args))...});
        })) throw std::runtime_error("Failed to connect signal recorder");
    }
    [[nodiscard]] int size() const { return rows_.size(); }
    [[nodiscard]] int count() const { return rows_.size(); }
    [[nodiscard]] bool isEmpty() const { return rows_.isEmpty(); }
    [[nodiscard]] const QVariantList& operator[](int index) const { return rows_.at(index); }
private:
    QList<QVariantList> rows_;
    QObject receiver_;
};

template <typename Object, typename Sender, typename... Args>
SignalRecorder(Object*, void (Sender::*)(Args...)) -> SignalRecorder<void (Sender::*)(Args...)>;

} // namespace photo_test

#define CTEST_CHECK(expression) \
    do { if (!(expression)) ::photo_test::fail(__FILE__, __LINE__, #expression); } while (false)

#define CTEST_CHECK_MESSAGE(expression, message) \
    do { \
        if (!(expression)) \
            ::photo_test::fail(__FILE__, __LINE__, (message)); \
    } while (false)

#define CTEST_COMPARE(actual, expected) \
    ::photo_test::compare((actual), (expected), __FILE__, __LINE__, #actual " == " #expected)

#define CTEST_THROWS(exception_type, expression) \
    do { \
        bool ctest_thrown = false; \
        try { (void)(expression); } \
        catch (const exception_type&) { ctest_thrown = true; } \
        if (!ctest_thrown) \
            ::photo_test::fail(__FILE__, __LINE__, "expected " #exception_type " from " #expression); \
    } while (false)

#define CTEST_WAIT_VERIFY(expression, timeout_ms) \
    do { \
        if (!::photo_test::waitFor([&] { return bool(expression); }, (timeout_ms))) \
            ::photo_test::fail(__FILE__, __LINE__, "timed out waiting for " #expression); \
    } while (false)

#define CTEST_WAIT_DEFAULT(expression) CTEST_WAIT_VERIFY(expression, 5000)
#define CTEST_WAIT_COMPARE_TIMEOUT(actual, expected, timeout_ms) \
    CTEST_WAIT_VERIFY(((actual) == (expected)), (timeout_ms))
#define CTEST_WAIT_COMPARE(actual, expected) CTEST_WAIT_COMPARE_TIMEOUT(actual, expected, 5000)
#define CTEST_WAIT_MESSAGE_TIMEOUT(expression, message, timeout_ms) \
    do { \
        if (!::photo_test::waitFor([&] { return bool(expression); }, (timeout_ms))) \
            ::photo_test::fail(__FILE__, __LINE__, (message)); \
    } while (false)
#define CTEST_WAIT_MESSAGE_DEFAULT(expression, message) CTEST_WAIT_MESSAGE_TIMEOUT(expression, message, 5000)
