#include "CTestSupport.h"
#include "Legacy/TouchSequence.h"
#include <QCoreApplication>

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    photo_test::CTestRunner runner;
    runner.run("touch_sequence_preserves_state_and_coordinates", [] {
        photo_test::TouchSequence sequence(nullptr, nullptr, false);
        sequence.press(0, QPoint(10, 20));
        CTEST_CHECK(sequence.hasPoint(0));
        CTEST_COMPARE(sequence.pendingPoint(0).state(), QEventPoint::State::Pressed);
        CTEST_COMPARE(sequence.pendingPoint(0).globalPosition(), QPointF(10, 20));
        sequence.move(0, QPoint(10, 50));
        CTEST_COMPARE(sequence.pendingPoint(0).state(), QEventPoint::State::Updated);
        CTEST_COMPARE(sequence.pendingPoint(0).globalPosition(), QPointF(10, 50));
        sequence.release(0, QPoint(10, 210));
        CTEST_COMPARE(sequence.pendingPoint(0).state(), QEventPoint::State::Released);
        CTEST_COMPARE(sequence.pendingPoint(0).globalPosition(), QPointF(10, 210));
    });
    return runner.result();
}
