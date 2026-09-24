pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import QtQuick.Dialogs
import LVRS 1.0 as LV


Item {
    id: root
    required property var controller
    property bool touchNavigation: Qt.platform.os === "ios" || Qt.platform.os === "android"
    property string selectedId: ""
    property var pendingViewState: null
    property bool initialPositionPending: true
    objectName: "photosView"

    function prioritizeViewport(): void {
        if (!root.controller || !root.visible || root.initialPositionPending || grid.cellHeight <= 0) return
        const firstRow = Math.max(0, Math.floor((grid.contentY - grid.originY) / grid.cellHeight))
        const lastRow = Math.ceil((grid.contentY - grid.originY + grid.height) / grid.cellHeight)
        const first = firstRow * zoom.columns
        const end = Math.min(grid.count, lastRow * zoom.columns)
        const keys = []
        for (let index = first; index < end; ++index)
            keys.push(root.controller.entries[index].galleryKey)
        root.controller.prioritizeVisible(keys)
    }
    Timer {
        id: viewportUpdate
        interval: 16
        onTriggered: root.prioritizeViewport()
    }

    function resetView(): void {
        pendingViewState = null
        initialPositionPending = true
        Qt.callLater(root.restoreView)
    }

    function rememberView(): void {
        // Coalesce partial catalog updates before the grid has restored its layout.
        if (initialPositionPending || pendingViewState || grid.count === 0) return
        pendingViewState = { contentY: grid.contentY }
    }

    function restoreView(): void {
        if (!root.visible || !root.controller || grid.width <= 0 || grid.height <= 0
                || grid.count !== root.controller.entries.length) return
        if (grid.count === 0) {
            pendingViewState = null
            initialPositionPending = true
            return
        }
        grid.forceLayout()
        if (initialPositionPending) {
            grid.positionViewAtEnd()
            initialPositionPending = false
            pendingViewState = null
            viewportUpdate.restart()
            return
        }
        viewportUpdate.restart()
        const state = pendingViewState
        pendingViewState = null
        if (!state) return
        grid.contentY = Math.max(grid.originY, Math.min(state.contentY,
            grid.originY + Math.max(0, grid.contentHeight - grid.height)))
    }

    function showInformation(entry): void {
        selectedId = entry.id
        info.fileName = entry.name
        info.preview = entry.previewStamp.length > 0 ? entry.preview : ""
        let bytes = 0
        for (const resource of entry.resources) bytes += Number(resource.size)
        info.fields = [
            { label: qsTr("Type"), value: entry.media === "video" ? qsTr("Video") : qsTr("Photo") },
            { label: qsTr("Size"), value: info.formatSize(bytes) },
            { label: qsTr("Created"), value: Qt.formatDateTime(new Date(entry.created), "yyyy-MM-dd HH:mm") }
        ]
        info.open()
    }
    function openMenu(entry, item): void {
        selectedId = entry.id
        photoMenu.photoId = entry.id
        photoMenu.openFor(item, 0, item.height)
    }
    onVisibleChanged: {
        if (visible) resetView()
        else { info.close(); photoMenu.close(); if (controller) controller.prioritizeVisible([]) }
    }
    onControllerChanged: resetView()
    Component.onCompleted: resetView()
    onSelectedIdChanged: if (selectedId.length === 0) info.close()
    Connections {
        target: root.controller
        function onEntriesAboutToChange() { root.rememberView() }
        function onEntriesChanged() {
            if (root.selectedId.length > 0 && !root.controller.entries.some(function(entry) { return entry.id === root.selectedId }))
                root.selectedId = ""
            Qt.callLater(root.restoreView)
        }
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 12
        LV.Label { text: qsTr("Photos"); style: title2; Layout.fillWidth: true; Layout.leftMargin: 16; Layout.topMargin: 16 }
        Flow {
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            spacing: 8
            LV.PushButton {
                objectName: "connectPhotoLibrary"
                height: Math.max(implicitHeight, root.touchNavigation ? 44 : 0)
                visible: root.controller && root.controller.access !== "full" && root.controller.access !== "unsupported"
                text: root.controller && root.controller.access === "limited" ? qsTr("Manage access…")
                    : root.width < 600 ? qsTr("Connect…") : qsTr("Connect photo library…")
                enabled: root.controller && !root.controller.busy
                onClicked: root.controller.connectLibrary()
            }
            LV.PushButton {
                objectName: "addPhotos"
                height: Math.max(implicitHeight, root.touchNavigation ? 44 : 0)
                text: root.width < 600 ? qsTr("Add…") : qsTr("Add photos or videos…")
                enabled: root.controller && root.controller.active && !root.controller.busy
                onClicked: picker.open()
            }
            LV.PushButton {
                height: Math.max(implicitHeight, root.touchNavigation ? 44 : 0)
                text: qsTr("Refresh")
                enabled: root.controller && root.controller.active && !root.controller.busy
                onClicked: root.controller.refresh()
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            GridView {
                id: grid
                objectName: "photoGrid"
                anchors.fill: parent
                clip: true
                cellWidth: width / zoom.columns
                cellHeight: cellWidth
                interactive: !zoom.interacting
                model: root.controller ? root.controller.galleryModel : null
                currentIndex: -1
                boundsBehavior: Flickable.StopAtBounds
                cacheBuffer: height * 2
                onContentYChanged: viewportUpdate.restart()
                onCellHeightChanged: viewportUpdate.restart()
                onCountChanged: Qt.callLater(root.restoreView)
                onHeightChanged: { viewportUpdate.restart(); if (root.initialPositionPending) Qt.callLater(root.restoreView) }
                onWidthChanged: { viewportUpdate.restart(); if (root.initialPositionPending) Qt.callLater(root.restoreView) }
                Controls.ScrollBar.vertical: Controls.ScrollBar {}
                delegate: GalleryTile {
                    id: tile
                    required property var entry
                    width: grid.cellWidth - 2
                    height: width
                    name: entry.name
                    preview: entry.previewStamp.length > 0 ? entry.preview : ""
                    cachePreview: true
                    previewRevision: entry.previewStamp
                    previewObjectName: "photoPreview-" + entry.id
                    video: entry.media === "video"
                    selected: root.selectedId === entry.id
                    onClicked: if (!photoMenu.visible) root.showInformation(entry)
                    TapHandler {
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                        acceptedButtons: Qt.RightButton
                        onTapped: root.openMenu(tile.entry, tile)
                    }
                    TapHandler {
                        acceptedDevices: PointerDevice.TouchScreen
                        onLongPressed: root.openMenu(tile.entry, tile)
                    }
                }
                LV.Label {
                    anchors.centerIn: parent
                    visible: grid.count === 0
                    width: Math.min(380, parent.width)
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: root.controller && root.controller.busy ? qsTr("Reading your photo library…")
                        : qsTr("Connect your photo library or add photos and videos here. New items also appear in your device’s photo library.")
                    style: description
                }
            }
            GalleryZoom { id: zoom; view: grid; enabled: root.visible; pinchEnabled: false }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 12
            LV.Label {
                objectName: "photoStatus"
                Layout.fillWidth: true
                style: description
                text: root.controller ? root.controller.status : ""
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
            }
        }
    }
    GalleryInfo {
        id: info
        canTrash: true
        busy: root.controller ? root.controller.busy : false
        onOpenRequested: if (root.selectedId.length > 0) root.controller.openPhoto(root.selectedId)
        onTrashRequested: if (root.selectedId.length > 0) {
            root.controller.trashPhoto(root.selectedId)
            info.close()
        }
    }
    LV.ContextMenu {
        id: photoMenu
        objectName: "photoContextMenu"
        property string photoId: ""
        showIconSlot: false
        itemWidth: 180
        items: [
            { label: qsTr("Open"), enabled: root.controller && !root.controller.busy },
            { label: qsTr("Copy"), enabled: root.controller && !root.controller.busy },
            { label: qsTr("Duplicate"), enabled: root.controller && !root.controller.busy },
            { label: qsTr("Share…"), enabled: root.controller && root.controller.canShare === true && !root.controller.busy },
            { label: qsTr("Delete"), enabled: root.controller && !root.controller.busy }
        ]
        onItemTriggered: function(index) {
            if (index === 0) root.controller.openPhoto(photoId)
            else if (index === 1) root.controller.copyPhoto(photoId)
            else if (index === 2) root.controller.duplicatePhoto(photoId)
            else if (index === 3) root.controller.sharePhoto(photoId)
            else if (index === 4) root.controller.trashPhoto(photoId)
        }
    }
    DropArea {
        anchors.fill: parent
        onDropped: function(drop) {
            if (drop.hasUrls && root.controller && !root.controller.busy) {
                root.controller.addFiles(drop.urls)
                drop.acceptProposedAction()
            }
        }
    }
    FileDialog {
        id: picker
        title: qsTr("Add photos and videos")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Photos and videos (*.jpg *.jpeg *.png *.heic *.heif *.webp *.gif *.tif *.tiff *.dng *.mov *.mp4 *.m4v *.avi *.mkv *.webm)"), qsTr("All files (*)")]
        onAccepted: root.controller.addFiles(selectedFiles)
    }
}
