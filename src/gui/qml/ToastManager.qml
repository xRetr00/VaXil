import QtQuick
import QtQuick.Controls

Item {
    id: root

    property string lastMessage: ""

    width: 300
    height: 320

    ListModel {
        id: toastModel
    }

    function normalizeMessage(message) {
        if (!message) {
            return ""
        }

        let cleaned = message.toString().replace(/\s+/g, " ").trim()
        if (cleaned.length > 90) {
            cleaned = cleaned.slice(0, 87) + "..."
        }
        return cleaned
    }

    function pushToast(message, tone) {
        const cleaned = normalizeMessage(message)
        if (cleaned.length === 0 || cleaned === lastMessage) {
            return
        }

        lastMessage = cleaned
        toastModel.insert(0, {
            "message": cleaned,
            "tone": tone || "info"
        })

        while (toastModel.count > 4) {
            toastModel.remove(toastModel.count - 1, 1)
        }
    }

    Column {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        spacing: 12

        Repeater {
            model: toastModel

            delegate: Rectangle {
                required property int index
                required property string message
                required property string tone

                width: 268
                height: 50
                radius: 25
                color: "#8c06101a"
                border.width: 1
                border.color: tone === "error" ? "#74475f" : tone === "response" ? "#345d92" : "#263e58"
                opacity: 0.96

                Rectangle {
                    anchors.fill: parent
                    radius: parent.radius
                    color: "transparent"
                    border.width: 1
                    border.color: tone === "response" ? "#41d5ff" : "#8db6ff"
                    opacity: tone === "response" ? 0.16 : 0.06
                }

                Rectangle {
                    x: 14
                    y: parent.height / 2 - height / 2
                    width: 10
                    height: 10
                    radius: 5
                    color: tone === "error" ? "#ff8fb5" : tone === "response" ? "#8ae6ff" : "#8da7ff"
                }

                Label {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 36
                    anchors.rightMargin: 20
                    text: message
                    color: "#ecf7ff"
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }

                SequentialAnimation on opacity {
                    running: true
                    loops: 1
                    PauseAnimation { duration: 3800 }
                    NumberAnimation { to: 0.0; duration: 260; easing.type: Easing.InCubic }
                    onFinished: {
                        if (index < toastModel.count) {
                            toastModel.remove(index, 1)
                        }
                    }
                }

                Behavior on y { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }
            }
        }
    }
}
