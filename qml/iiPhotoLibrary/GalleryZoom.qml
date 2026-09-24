pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root
    required property GridView view
    property real thumbnailSize: 128
    property bool pinchEnabled: true
    readonly property int columns: Math.max(1, Math.floor(view.width / thumbnailSize))
    readonly property bool interacting: drag.active || pinch.active
    objectName: "galleryZoom"

    function resizeTiles(size: real, focusY: real): void {
        if (!isFinite(size) || view.width <= 0 || view.cellHeight <= 0) return
        const oldRow = Math.max(0, (view.contentY - view.originY + focusY) / view.cellHeight)
        const anchor = Math.min(Math.max(0, view.count - 1), Math.floor(oldRow) * columns)
        thumbnailSize = Math.max(56, Math.min(400, size))
        view.forceLayout()
        const desired = (Math.floor(anchor / columns) + oldRow % 1) * view.cellHeight - focusY
        view.contentY = view.originY + Math.max(0, Math.min(desired, Math.max(0, view.contentHeight - view.height)))
    }

    // Attach handlers to the grid ancestor so delegates still receive clicks.
    DragHandler {
        parent: root.view
        enabled: root.enabled
        id: drag
        objectName: "galleryDragZoom"
        target: null
        minimumPointCount: 1
        maximumPointCount: 1
        yAxis.enabled: false
        property real startSize: 128
        onActiveChanged: if (active) { startSize = root.thumbnailSize; root.view.cancelFlick() }
        onActiveTranslationChanged: if (active)
            root.resizeTiles(startSize * Math.pow(2, activeTranslation.x / 180), centroid.pressPosition.y)
    }
    PinchHandler {
        parent: root.view
        enabled: root.enabled && root.pinchEnabled
        id: pinch
        objectName: "galleryPinchZoom"
        target: null
        rotationAxis.enabled: false
        xAxis.enabled: false
        yAxis.enabled: false
        property real startSize: 128
        onActiveChanged: if (active) { startSize = root.thumbnailSize; root.view.cancelFlick() }
        onActiveScaleChanged: if (active)
            root.resizeTiles(startSize * activeScale, centroid.position.y)
    }
}
