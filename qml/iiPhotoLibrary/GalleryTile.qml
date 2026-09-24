pragma ComponentBehavior: Bound
import QtQuick
import LVRS 1.0 as LV

LV.AbstractButton {
    id: tile
    required property string name
    property url preview: ""
    property string previewObjectName: "galleryPreview"
    property bool cachePreview: false
    property string previewRevision: ""
    property bool selected: false
    property bool video: false
    objectName: "galleryTile"
    height: width
    horizontalPadding: 0
    verticalPadding: 0
    cornerRadius: 0
    motionEnabled: false
    text: ""
    Accessible.name: name
    Accessible.role: Accessible.ListItem
    Accessible.selected: selected
    background: Rectangle { color: LV.Theme.panelBackground06 }
    contentItem: Item {
        clip: true
        Image {
            id: image
            objectName: tile.previewObjectName
            anchors.fill: parent
            source: tile.preview.toString().length > 0 && tile.previewRevision.length > 0
                ? tile.preview.toString() + "?revision=" + encodeURIComponent(tile.previewRevision) : tile.preview
            asynchronous: true
            autoTransform: true
            cache: tile.cachePreview
            retainWhileLoading: tile.cachePreview
            sourceSize: Qt.size(768, 768)
            fillMode: Image.PreserveAspectCrop
        }
        Image {
            anchors.centerIn: parent
            width: Math.min(32, parent.width / 3)
            height: width
            visible: image.status !== Image.Ready
            source: LV.Theme.iconPath("fileTypesimage")
        }
        Rectangle {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 6
            width: 22; height: 22; radius: 11
            visible: tile.video
            color: "#99000000"
            LV.Label { anchors.centerIn: parent; text: "▶"; color: "white"; style: caption }
        }
        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.width: tile.selected ? 3 : 0
            border.color: LV.Theme.accent
        }
    }
}
