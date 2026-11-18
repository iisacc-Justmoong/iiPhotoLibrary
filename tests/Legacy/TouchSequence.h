#pragma once

// QtGui implements the platform touch-injection path used by Qt Quick.
// This adapter does not link the QtTest library.
#include <QtGui/qtestsupport_gui.h>

namespace photo_test {
inline QPointingDevice* createTouchDevice(QInputDevice::DeviceType type = QInputDevice::DeviceType::TouchScreen)
{
    return QTest::createTouchDevice(type);
}

class TouchSequence final : public QTest::QTouchEventSequence {
public:
    explicit TouchSequence(QWindow* window, QPointingDevice* device, bool autoCommit = true)
        : QTest::QTouchEventSequence(window, device, autoCommit) {}

    [[nodiscard]] bool hasPoint(int id) const { return points.contains(id); }
    [[nodiscard]] QEventPoint pendingPoint(int id) const { return points.value(id); }
};
} // namespace photo_test
