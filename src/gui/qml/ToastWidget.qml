import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property string toastMessage
    required property string toastTone
    required property string timestampLabel
    property int taskId: -1
    signal clicked(int taskId)
    signal dismissed(int taskId)

    width: parent ? parent.width : 420
    implicitWidth: width
    implicitHeight: Math.min(320, Math.max(88, contentColumn.implicitHeight + 26))
    radius: 18
    color: "#d90b1522"
    border.width: 1
    border.color: toastTone === "error" ? "#a44b68" : toastTone === "response" ? "#3a80c4" : "#2f566f"
    opacity: 0.98

    readonly property color accentColor: toastTone === "error" ? "#ff789f" : toastTone === "response" ? "#8ae6ff" : "#8da7ff"
    readonly property string toneLabel: toastTone === "error" ? "Error"
        : toastTone === "response" ? "Model"
        : "Status"

    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        color: "transparent"
        border.width: 1
        border.color: root.accentColor
        opacity: root.toastTone === "response" ? 0.28 : 0.14
    }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 4
        radius: 2
        color: root.accentColor
        opacity: 0.85
    }

    Column {
        id: contentColumn
        z: 1
        anchors.fill: parent
        anchors.margins: 14
        anchors.leftMargin: 16
        spacing: 10

        Row {
            width: parent.width
            spacing: 8

            Text {
                text: root.toneLabel
                color: root.accentColor
                font.pixelSize: 11
                font.bold: true
                elide: Text.ElideRight
            }

            Text {
                text: root.timestampLabel
                color: "#9ab7ce"
                font.pixelSize: 10
                elide: Text.ElideRight
            }

            Item { width: 1; height: 1; Layout.fillWidth: true }

            Button {
                text: "Dismiss"
                padding: 4
                font.pixelSize: 10
                onClicked: root.dismissed(root.taskId)
            }
        }

        ScrollView {
            width: parent.width
            height: Math.min(220, Math.max(36, toastText.implicitHeight))
            clip: true

            Text {
                id: toastText
                width: Math.max(220, root.width - 64)
                text: root.toastMessage
                color: "#ecf7ff"
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        z: 0
        cursorShape: Qt.PointingHandCursor
        onClicked: function() {
            root.clicked(root.taskId)
        }
    }
}
