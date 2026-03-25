import QtQuick
import QtQuick.Controls

Item {
    id: root

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

    function indexOfTaskType(taskType) {
        for (let i = 0; i < toastModel.count; ++i) {
            const toast = toastModel.get(i)
            if (toast.taskType === taskType) {
                return i
            }
        }
        return -1
    }

    function trimQueue() {
        while (toastModel.count > 3) {
            toastModel.remove(toastModel.count - 1, 1)
        }
    }

    function removeExpiredToasts() {
        const now = Date.now()
        for (let i = toastModel.count - 1; i >= 0; --i) {
            if (toastModel.get(i).expiresAt <= now) {
                toastModel.remove(i, 1)
            }
        }
    }

    function pushToast(message, tone, taskId, taskType) {
        const cleaned = normalizeMessage(message)
        if (cleaned.length === 0) {
            return
        }

        const typeKey = (taskType || "general").toString()
        const expiresAt = Date.now() + 5200
        const existingIndex = indexOfTaskType(typeKey)

        if (existingIndex >= 0) {
            toastModel.set(existingIndex, {
                "message": cleaned,
                "tone": tone || "info",
                "taskId": taskId === undefined ? -1 : taskId,
                "taskType": typeKey,
                "expiresAt": expiresAt
            })

            if (existingIndex > 0) {
                toastModel.move(existingIndex, 0, 1)
            }
        } else {
            toastModel.insert(0, {
                "message": cleaned,
                "tone": tone || "info",
                "taskId": taskId === undefined ? -1 : taskId,
                "taskType": typeKey,
                "expiresAt": expiresAt
            })
        }

        trimQueue()
    }

    Timer {
        interval: 250
        repeat: true
        running: toastModel.count > 0
        onTriggered: root.removeExpiredToasts()
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
                required property string taskType
                property int modelTaskId: taskId

                width: 300
                toastMessage: message
                toastTone: tone
                taskId: modelTaskId
                onClicked: function(clickedTaskId) {
                    root.toastClicked(clickedTaskId)
                }

                Behavior on y { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }
            }
        }
    }
}
