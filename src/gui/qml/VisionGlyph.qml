import QtQuick
import "." as JarvisUi

JarvisUi.VisionGlassPanel {
    id: root

    property string source: ""
    property real iconSize: 18
    property real padding: 10
    property real iconOpacity: 0.95

    width: Math.round(iconSize + padding * 2)
    height: width
    radius: width / 2
    panelColor: "#181b2228"
    innerColor: "#1e222a32"
    outlineColor: "#18ffffff"
    highlightColor: "#0dffffff"
    shadowOpacity: 0.16

    Image {
        anchors.centerIn: parent
        width: root.iconSize
        height: root.iconSize
        source: root.source
        fillMode: Image.PreserveAspectFit
        sourceSize.width: Math.round(root.iconSize * 2)
        sourceSize.height: Math.round(root.iconSize * 2)
        smooth: true
        mipmap: true
        opacity: root.iconOpacity
    }
}
