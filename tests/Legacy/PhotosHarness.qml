pragma ComponentBehavior: Bound
import QtQuick
import LVRS 1.0 as LV
import iiPhotoLibrary 1.0 as Photos

LV.ApplicationWindow {
    id: window
    required property var controller
    width: 960
    height: 720
    desktopMinWidth: 360
    desktopMinHeight: 320
    visible: true
    useInternalPageStack: false
    content: Photos.PhotosView { anchors.fill: parent; controller: window.controller }
}
