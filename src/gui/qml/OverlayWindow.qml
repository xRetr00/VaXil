import QtQuick
import QtQuick.Controls
import QtQuick.Window
import "." as JarvisUi

Window {
    id: root

    width: 1920
    height: 1080
    visible: false
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool | Qt.NoDropShadowWindowHint

    onClosing: function(close) {
        close.accepted = false
        hide()
    }

    function compactText(rawText, fallbackText) {
        let text = (rawText || "").toString().replace(/\s+/g, " ").trim()
        if (text.length === 0) {
            text = fallbackText || ""
        }
        if (text.length > 72) {
            text = text.slice(0, 69) + "..."
        }
        return text
    }

    function greetingLine() {
        const hour = new Date().getHours()
        const period = hour < 12 ? "Good morning" : hour >= 18 ? "Good evening" : "Good afternoon"
        return backend.userName.length > 0 ? period + ", " + backend.userName + "." : period + "."
    }

    function presenceLine() {
        if (backend.responseText.length > 0) {
            return compactText(backend.responseText, "")
        }
        if (backend.transcript.length > 0) {
            return compactText(backend.transcript, "")
        }
        if (backend.stateName === "IDLE") {
            return greetingLine()
        }
        return compactText(backend.statusText, "Ready.")
    }

    function microStatus() {
        if (backend.stateName === "LISTENING") {
            return "Listening"
        }
        if (backend.stateName === "PROCESSING") {
            return "Processing"
        }
        if (backend.stateName === "SPEAKING") {
            return "Speaking"
        }
        return "Idle"
    }

    JarvisUi.AnimationController {
        id: motion
        stateName: backend.stateName
        inputLevel: backend.audioLevel
        overlayVisible: backend.overlayVisible
    }

    Item {
        anchors.fill: parent

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 32
            text: backend.assistantName + " · " + microStatus()
            color: "#dbeeff"
            opacity: 0.76
            font.pixelSize: 12
            font.letterSpacing: 2.1
        }

        JarvisUi.OrbRenderer {
            id: orb
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 82
            width: Math.min(parent.width * 0.16, 250)
            height: width
            x: backend.presenceOffsetX * 10 + motion.listeningVibration * 2
            y: -backend.presenceOffsetY * 10 + motion.listeningVibration * 2
            stateName: backend.stateName
            time: motion.time
            audioLevel: motion.inputBoost
            speakingLevel: motion.speakingSignal
            distortion: motion.distortion
            glow: motion.glow
            orbScale: motion.orbScale
            orbitalRotation: motion.orbitalRotation
        }

        Column {
            anchors.horizontalCenter: orb.horizontalCenter
            anchors.bottom: orb.top
            anchors.bottomMargin: 22
            width: Math.min(parent.width * 0.22, 320)
            spacing: 6
            x: backend.presenceOffsetX * 6
            y: -backend.presenceOffsetY * 6

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: presenceLine()
                color: "#edf6ff"
                font.pixelSize: 20
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: compactText(backend.statusText, "")
                visible: text.length > 0 && text !== presenceLine()
                color: "#7f9fc7"
                font.pixelSize: 12
                wrapMode: Text.Wrap
                maximumLineCount: 1
                elide: Text.ElideRight
            }
        }

        JarvisUi.ToastManager {
            id: toastManager
            anchors.right: parent.right
            anchors.rightMargin: 28
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 30
            onToastClicked: function(taskId) {
                backend.setBackgroundPanelVisible(true)
                backend.notifyTaskToastShown(taskId)
            }
        }

        Rectangle {
            id: taskPanel
            anchors.top: parent.top
            anchors.topMargin: 84
            anchors.right: parent.right
            anchors.rightMargin: 26
            width: 420
            height: parent.height - 160
            visible: backend.backgroundPanelVisible && backend.backgroundTaskResults.length > 0
            radius: 28
            color: "#c40b1320"
            border.width: 1
            border.color: "#284762"

            Column {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 12

                Row {
                    width: parent.width
                    spacing: 10

                    Text {
                        text: "Background Results"
                        color: "#eef7ff"
                        font.pixelSize: 20
                        font.weight: Font.Medium
                    }

                    Rectangle {
                        width: 86
                        height: 30
                        radius: 15
                        color: "#13283d"
                        border.width: 1
                        border.color: "#325274"

                        Text {
                            anchors.centerIn: parent
                            text: "Hide"
                            color: "#d5e8ff"
                            font.pixelSize: 12
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: backend.setBackgroundPanelVisible(false)
                        }
                    }
                }

                ScrollView {
                    width: parent.width
                    height: parent.height - 48
                    clip: true

                    Column {
                        width: taskPanel.width - 54
                        spacing: 12

                        Repeater {
                            model: backend.backgroundTaskResults

                            delegate: Rectangle {
                                required property var modelData
                                width: parent.width
                                radius: 18
                                color: "#101d2b"
                                border.width: 1
                                border.color: modelData.success ? "#346b52" : "#7a4557"
                                height: resultColumn.implicitHeight + 20

                                Column {
                                    id: resultColumn
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    spacing: 6

                                    Text {
                                        text: modelData.finishedAt + "  " + modelData.title
                                        color: "#edf6ff"
                                        font.pixelSize: 13
                                        wrapMode: Text.Wrap
                                    }

                                    Text {
                                        text: modelData.summary
                                        color: modelData.success ? "#8fe1b0" : "#ffb5cc"
                                        font.pixelSize: 12
                                        wrapMode: Text.Wrap
                                    }

                                    Text {
                                        text: modelData.detail
                                        color: "#bfd3ea"
                                        font.pixelSize: 12
                                        wrapMode: Text.Wrap
                                    }

                                    TextArea {
                                        width: parent.width
                                        readOnly: true
                                        wrapMode: TextArea.Wrap
                                        text: modelData.payload
                                        color: "#dcecff"
                                        background: Rectangle {
                                            radius: 12
                                            color: "#0b1520"
                                            border.width: 1
                                            border.color: "#20364b"
                                        }
                                        implicitHeight: Math.min(160, Math.max(60, contentHeight + 18))
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            anchors.top: parent.top
            anchors.topMargin: 90
            anchors.right: parent.right
            anchors.rightMargin: 26
            width: 92
            height: 34
            radius: 17
            visible: !taskPanel.visible && backend.backgroundTaskResults.length > 0
            color: "#13283d"
            border.width: 1
            border.color: "#325274"

            Text {
                anchors.centerIn: parent
                text: "Results"
                color: "#d5e8ff"
                font.pixelSize: 12
            }

            MouseArea {
                anchors.fill: parent
                onClicked: {
                    backend.setBackgroundPanelVisible(true)
                    backend.notifyTaskPanelRendered()
                }
            }
        }
    }

    Connections {
        target: backend

        function onStatusTextChanged() {
            if (backend.statusText.length === 0) {
                return
            }
            toastManager.pushToast(
                backend.statusText,
                backend.statusText.toLowerCase().indexOf("error") >= 0 ? "error" : "status",
                -1,
                "status")
        }

        function onResponseTextChanged() {
            if (backend.responseText.length === 0) {
                return
            }
            toastManager.pushToast(backend.responseText, "response", -1, "response")
        }

        function onLatestTaskToastChanged() {
            if (backend.latestTaskToast.length === 0) {
                return
            }
            toastManager.pushToast(
                backend.latestTaskToast,
                backend.latestTaskToastTone,
                backend.latestTaskToastTaskId,
                backend.latestTaskToastType)
        }

        function onBackgroundPanelVisibleChanged() {
            if (backend.backgroundPanelVisible) {
                backend.notifyTaskPanelRendered()
            }
        }
    }
}
