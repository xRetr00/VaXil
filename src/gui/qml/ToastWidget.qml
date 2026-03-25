import QtQuick
import QtQuick.Controls

Rectangle {
    id: root

    required property string toastMessage
    required property string toastTone
    property int taskId: -1
    signal clicked(int taskId)

    width: 320
    height: Math.min(124, Math.max(64, contentColumn.implicitHeight + 24))
    radius: 22
    color: "#8c06101a"
    border.width: 1
    border.color: toastTone === "error" ? "#74475f" : toastTone === "response" ? "#345d92" : "#263e58"
    opacity: 0.96

    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        color: "transparent"
        border.width: 1
        border.color: root.toastTone === "response" ? "#41d5ff" : "#8db6ff"
        opacity: root.toastTone === "response" ? 0.16 : 0.06
    }

    Column {
        id: contentColumn
        anchors.fill: parent
        anchors.margins: 14
        spacing: 8

        Rectangle {
            width: 10
            height: 10
            radius: 5
            color: root.toastTone === "error" ? "#ff8fb5" : root.toastTone === "response" ? "#8ae6ff" : "#8da7ff"
        }

        ScrollView {
            width: parent.width
            height: Math.min(72, toastText.implicitHeight)
            clip: true

            Text {
                id: toastText
                width: root.width - 56
                text: root.toastMessage
                color: "#ecf7ff"
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked(root.taskId)
    }
}
