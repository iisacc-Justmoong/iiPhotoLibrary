#include "CTestSupport.h"

#include <QCoreApplication>
#include <QPointer>
#include <QTimer>

#include <sstream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    std::ostringstream errors;
    std::ostringstream output;
    auto* previous = std::cerr.rdbuf(errors.rdbuf());
    auto* previousOutput = std::cout.rdbuf(output.rdbuf());
    photo_test::CTestRunner empty;
    const int emptyResult = empty.result();
    photo_test::CTestRunner failure;
    bool continued = false;
    const bool badRowPassed = failure.run("bad-row", [] { CTEST_CHECK(false); });
    const bool nextRowPassed = failure.run("next-row", [&] { continued = true; });
    const int failureResult = failure.result();
    std::cerr.rdbuf(previous);
    std::cout.rdbuf(previousOutput);
    if (emptyResult == 0 || failureResult == 0 || badRowPassed || !nextRowPassed || !continued ||
        errors.str().find("No test cases") == std::string::npos ||
        errors.str().find("bad-row") == std::string::npos ||
        errors.str().find("ctest_support.cpp:") == std::string::npos)
        return 1;

    photo_test::CTestRunner success;
    success.run("checks", [] {
        int evaluations = 0;
        CTEST_CHECK(true);
        CTEST_COMPARE(++evaluations, 1);
        CTEST_COMPARE(evaluations, 1);
        CTEST_THROWS(std::runtime_error, throw std::runtime_error("expected"));
        QTimer timer;
        photo_test::SignalCounter counter(&timer, &QTimer::timeout);
        CTEST_CHECK(QMetaObject::invokeMethod(&timer, "timeout", Qt::DirectConnection));
        CTEST_COMPARE(counter.count(), 1);
        QTimer* missing = nullptr;
        CTEST_THROWS(std::runtime_error, photo_test::SignalCounter(missing, &QTimer::timeout));
        QObject sender;
        photo_test::SignalRecorder recorder(&sender, &QObject::objectNameChanged);
        sender.setObjectName(QStringLiteral("fixture"));
        CTEST_COMPARE(recorder.size(), 1);
        CTEST_COMPARE(recorder[0][0].toString(), QStringLiteral("fixture"));
        bool ready = false;
        QTimer::singleShot(0, [&] { ready = true; });
        CTEST_WAIT_VERIFY(ready, 1000);
        CTEST_CHECK(!photo_test::waitFor([] { return false; }, 0));
        QPointer<QObject> deferred = new QObject;
        {
            photo_test::ScopedDeferredDeletes cleanup;
            deferred->deleteLater();
        }
        CTEST_CHECK(deferred.isNull());
        char program[] = "test", first[] = "alpha", second[] = "row:desktop";
        char* selectedArgs[] = {program, first, second};
        photo_test::CTestRunner selected(3, selectedArgs);
        int selectedCases = 0;
        selected.run("alpha", [&] { ++selectedCases; });
        selected.run("beta", [&] { selectedCases += 100; });
        selected.run("row:mobile", [&] { selectedCases += 100; });
        selected.run("row:desktop", [&] { ++selectedCases; });
        CTEST_COMPARE(selected.result(), 0);
        CTEST_COMPARE(selectedCases, 2);
        char groupArg[] = "row";
        char* groupArgs[] = {program, groupArg};
        photo_test::CTestRunner group(2, groupArgs);
        int groupCases = 0;
        group.run("row:mobile", [&] { ++groupCases; });
        group.run("row:desktop", [&] { ++groupCases; });
        CTEST_COMPARE(group.result(), 0);
        CTEST_COMPARE(groupCases, 2);
        char listArg[] = "-functions";
        char* listArgs[] = {program, listArg};
        std::ostringstream listed;
        auto* oldOutput = std::cout.rdbuf(listed.rdbuf());
        photo_test::CTestRunner listing(2, listArgs);
        listing.run("row:mobile", [] { throw std::runtime_error("must not run"); });
        listing.run("row:desktop", [] { throw std::runtime_error("must not run"); });
        const int listResult = listing.result();
        std::cout.rdbuf(oldOutput);
        CTEST_COMPARE(listResult, 0);
        CTEST_COMPARE(listed.str(), std::string("row()\n"));
    });
    return success.result();
}
