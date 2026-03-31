import QtQuick

Item {
    id: root

    property alias radius: shell.radius
    property color panelColor: "#18171d23"
    property color innerColor: "#1f1b222a"
    property color outlineColor: "#20ffffff"
    property color innerOutlineColor: "#0effffff"
    property color highlightColor: "#14ffffff"
    property color shadowColor: "#0f000000"
    property real shadowOpacity: 0.42
    property real highlightHeightRatio: 0.46
    property bool clipContent: false

    default property alias contentData: contentLayer.data

    Rectangle {
        anchors.fill: parent
        anchors.topMargin: 10
        radius: shell.radius
        color: root.shadowColor
        opacity: root.shadowOpacity
    }

    Rectangle {
        id: shell
        anchors.fill: parent
        radius: 28
        color: root.panelColor
        border.width: 1
        border.color: root.outlineColor
    }

    Rectangle {
        anchors.fill: shell
        anchors.margins: 1
        radius: shell.radius - 1
        color: root.innerColor
        border.width: 1
        border.color: root.innerOutlineColor
    }

    Rectangle {
        anchors.left: shell.left
        anchors.right: shell.right
        anchors.top: shell.top
        anchors.margins: 1
        height: Math.max(24, shell.height * root.highlightHeightRatio)
        radius: shell.radius - 1
        color: root.highlightColor
    }

    Item {
        id: contentLayer
        anchors.fill: parent
        clip: root.clipContent
        z: 2
    }
}
