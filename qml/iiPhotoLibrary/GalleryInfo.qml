pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import LVRS 1.0 as LV

LV.Sheet {
    id: panel
    property string fileName: ""
    property url preview: ""
    property var fields: []
    property bool canTrash: false
    property bool busy: false
    signal openRequested()
    signal trashRequested()
    objectName: "galleryInfo"
    title: qsTr("File information")
    showDescription: false
    preferredWidth: 520
    preferredHeight: 640
    contentPadding: 20
    function formatSize(bytes: real): string {
        if (!isFinite(bytes) || bytes < 0) return ""
        if (bytes < 1024) return qsTr("%1 bytes").arg(bytes)
        if (bytes < 1048576) return qsTr("%1 KB").arg((bytes / 1024).toFixed(1))
        if (bytes < 1073741824) return qsTr("%1 MB").arg((bytes / 1048576).toFixed(1))
        return qsTr("%1 GB").arg((bytes / 1073741824).toFixed(1))
    }
    ColumnLayout {
        width: panel.availableContentWidth
        spacing: 16
        Image {
            objectName: "galleryInfoPreview"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(260, panel.availableContentWidth)
            source: panel.preview
            autoTransform: true
            asynchronous: true
            sourceSize: Qt.size(1024, 1024)
            fillMode: Image.PreserveAspectFit
        }
        LV.Label {
            objectName: "galleryInfoName"
            Layout.fillWidth: true
            text: panel.fileName
            textFormat: Text.PlainText
            style: header
            wrapMode: Text.WrapAnywhere
            sizeToContentHeight: true
        }
        Repeater {
            model: panel.fields
            LV.Label {
                required property var modelData
                Layout.fillWidth: true
                text: modelData.label + ": " + modelData.value
                textFormat: Text.PlainText
                style: description
                wrapMode: Text.WrapAnywhere
                sizeToContentHeight: true
            }
        }
        Flow {
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            spacing: 8
            LV.PushButton {
                objectName: "galleryOpenOriginal"
                text: qsTr("View original")
                enabled: !panel.busy
                onClicked: panel.openRequested()
            }
            LV.PushButton {
                objectName: "trashPhoto"
                visible: panel.canTrash
                text: qsTr("Move to trash")
                enabled: !panel.busy
                onClicked: panel.trashRequested()
            }
        }
    }
}
