import QtQuick
import QtQuick.Controls

Item {
    id: root

    signal toastClicked(int taskId, string taskType)
    signal toastDismissed(int taskId, string taskType, string reason)
    z: 9999

    width: Math.max(320, Math.min(parent ? parent.width * 0.36 : 460, 540))
    height: Math.max(240, Math.min(parent ? parent.height * 0.6 : 440, 520))

    property string latestUserPrompt: ""
    property int displayDurationMs: 180000

    ListModel {
        id: toastModel
    }

    function normalizeMessage(message) {
        if (!message) {
            return ""
        }

        return message.toString().replace(/\r/g, "").trim()
    }

    function normalizedForComparison(message) {
        return normalizeMessage(message).replace(/\s+/g, " ").toLowerCase()
    }

    function shouldIgnoreToast(message, tone) {
        const normalized = normalizedForComparison(message)
        if (normalized.length === 0) {
            return true
        }

        const prompt = normalizedForComparison(root.latestUserPrompt)
        if (tone !== "response" && prompt.length > 0 && normalized === prompt) {
            return true
        }

        if (tone === "status") {
            const lowValue = normalized
            if (lowValue === "processing request"
                    || lowValue === "listening"
                    || lowValue === "thinking"
                    || lowValue === "executing"
                    || lowValue === "idle") {
                return true
            }
        }

        return false
    }

    function timestampLabel() {
        const now = new Date()
        return Qt.formatDateTime(now, "hh:mm:ss")
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
        while (toastModel.count > 4) {
            const toast = toastModel.get(toastModel.count - 1)
            root.toastDismissed(toast.taskId, toast.taskType, "deferred")
            toastModel.remove(toastModel.count - 1, 1)
        }
    }

    function dismissAt(index) {
        if (index >= 0 && index < toastModel.count) {
            toastModel.remove(index, 1)
        }
    }

    function removeExpiredToasts() {
        const now = Date.now()
        for (let i = toastModel.count - 1; i >= 0; --i) {
            if (toastModel.get(i).expiresAt <= now) {
                const toast = toastModel.get(i)
                root.toastDismissed(toast.taskId, toast.taskType, "ignored")
                toastModel.remove(i, 1)
            }
        }
    }

    function pushToast(message, tone, taskId, taskType) {
        const cleaned = normalizeMessage(message)
        const effectiveTone = tone || "info"
        if (shouldIgnoreToast(cleaned, effectiveTone)) {
            return
        }

        const typeKey = (taskType || "general").toString()
        const expiresAt = Date.now() + displayDurationMs
        const createdAt = timestampLabel()
        const existingIndex = indexOfTaskType(typeKey)

        if (existingIndex >= 0) {
            toastModel.set(existingIndex, {
                "message": cleaned,
                "tone": effectiveTone,
                "taskId": taskId === undefined ? -1 : taskId,
                "taskType": typeKey,
                "expiresAt": expiresAt,
                "createdAt": createdAt
            })

            if (existingIndex > 0) {
                toastModel.move(existingIndex, 0, 1)
            }
        } else {
            toastModel.insert(0, {
                "message": cleaned,
                "tone": effectiveTone,
                "taskId": taskId === undefined ? -1 : taskId,
                "taskType": typeKey,
                "expiresAt": expiresAt,
                "createdAt": createdAt
            })
        }

        trimQueue()
    }

    Timer {
        interval: 600
        repeat: true
        running: toastModel.count > 0
        onTriggered: root.removeExpiredToasts()
    }

    Column {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: root.width
        height: implicitHeight
        spacing: 12

        Repeater {
            model: toastModel

            delegate: ToastWidget {
                required property int index
                required property string message
                required property string tone
                required property string taskType
                required property string createdAt
                property int modelTaskId: taskId

                width: root.width
                toastMessage: message
                toastTone: tone
                timestampLabel: createdAt
                taskId: modelTaskId
                onClicked: function(clickedTaskId) {
                    root.toastClicked(clickedTaskId, taskType)
                    root.dismissAt(index)
                }
                onDismissed: function() {
                    root.toastDismissed(modelTaskId, taskType, "dismissed")
                    root.dismissAt(index)
                }

                Behavior on y { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }
            }
        }
    }
}
