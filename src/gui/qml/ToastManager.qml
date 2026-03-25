import QtQuick
import QtQuick.Controls

Item {
    id: root

    property string lastMessage: ""
    signal toastClicked(int taskId)

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

    function pushToast(message, tone, taskId) {
        const cleaned = normalizeMessage(message)
        if (cleaned.length === 0 || cleaned === lastMessage) {
            return
        }

        lastMessage = cleaned
        toastModel.insert(0, {
            "message": cleaned,
            "tone": tone || "info",
            "taskId": taskId === undefined ? -1 : taskId
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

            delegate: ToastWidget {
                required property int index
                required property string message
                required property string tone
                property int modelTaskId: taskId

                width: 300
                toastMessage: message
                toastTone: tone
                taskId: modelTaskId
                onClicked: function(clickedTaskId) {
                    root.toastClicked(clickedTaskId)
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
