import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import "." as JarvisUi

Window {
    id: root

    width: Screen.width
    height: Screen.height
    minimumWidth: 960
    minimumHeight: 540
    visible: false
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool | Qt.NoDropShadowWindowHint

    property real dpiScale: Math.max(1.0, Screen.devicePixelRatio)
    property real pageMargin: 24 * dpiScale
    property real orbBaseSize: Math.min(width * 0.18, 300 * dpiScale)

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
        return agentVm.userName.length > 0 ? period + ", " + agentVm.userName + "." : period + "."
    }

    function presenceLine() {
        if (agentVm.responseText.length > 0) {
            return compactText(agentVm.responseText, "")
        }
        if (agentVm.transcript.length > 0) {
            return compactText(agentVm.transcript, "")
        }
        if (agentVm.stateName === "IDLE") {
            return greetingLine()
        }
        return compactText(agentVm.statusText, "Ready.")
    }

    function microStatus() {
        if (agentVm.stateName === "LISTENING") {
            return "Listening"
        }
        if (agentVm.stateName === "PROCESSING") {
            return "Thinking"
        }
        if (agentVm.stateName === "SPEAKING") {
            return "Executing"
        }
        return "Idle"
    }

    JarvisUi.AnimationController {
        id: motion
        stateName: agentVm.stateName
        inputLevel: agentVm.audioLevel
        overlayVisible: agentVm.overlayVisible
        uiState: agentVm.uiState
    }

    Item {
        anchors.fill: parent

        ColumnLayout {
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.topMargin: pageMargin + 6 * dpiScale
            anchors.bottomMargin: pageMargin + 40 * dpiScale
            spacing: 12 * dpiScale

            Text {
                Layout.alignment: Qt.AlignHCenter
                text: agentVm.assistantName + " - " + microStatus()
                color: "#dbeeff"
                opacity: 0.76
                font.pixelSize: Math.round(12 * dpiScale)
                font.letterSpacing: 2.1
            }

            Item { Layout.fillHeight: true }

            ColumnLayout {
                Layout.alignment: Qt.AlignHCenter
                width: Math.min(root.width * 0.28, 420 * dpiScale)
                spacing: 6 * dpiScale
                x: agentVm.presenceOffsetX * 6
                y: -agentVm.presenceOffsetY * 6

                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: presenceLine()
                    color: "#edf6ff"
                    font.pixelSize: Math.round(20 * dpiScale)
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }

                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: compactText(agentVm.statusText, "")
                    visible: text.length > 0 && text !== presenceLine()
                    color: "#7f9fc7"
                    font.pixelSize: Math.round(12 * dpiScale)
                    wrapMode: Text.Wrap
                    maximumLineCount: 1
                    elide: Text.ElideRight
                }
            }

            JarvisUi.OrbRenderer {
                id: orb
                Layout.alignment: Qt.AlignHCenter
                width: orbBaseSize
                height: width
                x: agentVm.presenceOffsetX * 10 + motion.listeningVibration * 2
                y: -agentVm.presenceOffsetY * 10 + motion.listeningVibration * 2
                stateName: agentVm.stateName
                uiState: agentVm.uiState
                quality: width < 220 * dpiScale ? orb.qualityMedium : orb.qualityHigh
                time: motion.time
                audioLevel: motion.inputBoost
                speakingLevel: motion.speakingSignal
                distortion: motion.distortion
                glow: motion.glow
                orbScale: motion.orbScale
                orbitalRotation: motion.orbitalRotation
            }
        }

        JarvisUi.ToastManager {
            id: toastManager
            anchors.right: parent.right
            anchors.rightMargin: pageMargin
            anchors.bottom: parent.bottom
            anchors.bottomMargin: pageMargin
            onToastClicked: function(taskId) {
                taskVm.setBackgroundPanelVisible(true)
                taskVm.notifyTaskToastShown(taskId)
            }
        }

        Rectangle {
            id: taskPanel
            anchors.top: parent.top
            anchors.topMargin: 84 * dpiScale
            anchors.right: parent.right
            anchors.rightMargin: pageMargin
            width: Math.min(parent.width * 0.34, 460 * dpiScale)
            height: parent.height - (160 * dpiScale)
            visible: taskVm.backgroundPanelVisible && taskVm.backgroundTaskResults.length > 0
            radius: 28 * dpiScale
            color: "#c40b1320"
            border.width: 1
            border.color: "#284762"

            Column {
                anchors.fill: parent
                anchors.margins: 18 * dpiScale
                spacing: 12 * dpiScale

                Row {
                    width: parent.width
                    spacing: 10 * dpiScale

                    Text {
                        text: "Background Results"
                        color: "#eef7ff"
                        font.pixelSize: Math.round(20 * dpiScale)
                        font.weight: Font.Medium
                    }

                    Rectangle {
                        width: 86 * dpiScale
                        height: 30 * dpiScale
                        radius: 15 * dpiScale
                        color: "#13283d"
                        border.width: 1
                        border.color: "#325274"

                        Text {
                            anchors.centerIn: parent
                            text: "Hide"
                            color: "#d5e8ff"
                            font.pixelSize: Math.round(12 * dpiScale)
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: taskVm.setBackgroundPanelVisible(false)
                        }
                    }
                }

                ScrollView {
                    width: parent.width
                    height: parent.height - 48 * dpiScale
                    clip: true

                    Column {
                        width: taskPanel.width - 54 * dpiScale
                        spacing: 12 * dpiScale

                        Repeater {
                            model: taskVm.backgroundTaskResults

                            delegate: Rectangle {
                                required property var modelData
                                width: parent.width
                                radius: 18 * dpiScale
                                color: "#101d2b"
                                border.width: 1
                                border.color: modelData.success ? "#346b52" : "#7a4557"
                                height: resultColumn.implicitHeight + 20 * dpiScale

                                Column {
                                    id: resultColumn
                                    anchors.fill: parent
                                    anchors.margins: 12 * dpiScale
                                    spacing: 6 * dpiScale

                                    Text {
                                        text: modelData.finishedAt + "  " + modelData.title
                                        color: "#edf6ff"
                                        font.pixelSize: Math.round(13 * dpiScale)
                                        wrapMode: Text.Wrap
                                    }

                                    Text {
                                        text: modelData.summary
                                        color: modelData.success ? "#8fe1b0" : "#ffb5cc"
                                        font.pixelSize: Math.round(12 * dpiScale)
                                        wrapMode: Text.Wrap
                                    }

                                    Text {
                                        text: modelData.detail
                                        color: "#bfd3ea"
                                        font.pixelSize: Math.round(12 * dpiScale)
                                        wrapMode: Text.Wrap
                                    }

                                    TextArea {
                                        width: parent.width
                                        readOnly: true
                                        wrapMode: TextArea.Wrap
                                        text: modelData.payload
                                        color: "#dcecff"
                                        background: Rectangle {
                                            radius: 12 * dpiScale
                                            color: "#0b1520"
                                            border.width: 1
                                            border.color: "#20364b"
                                        }
                                        implicitHeight: Math.min(160 * dpiScale, Math.max(60 * dpiScale, contentHeight + 18 * dpiScale))
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
            anchors.topMargin: 90 * dpiScale
            anchors.right: parent.right
            anchors.rightMargin: pageMargin
            width: 92 * dpiScale
            height: 34 * dpiScale
            radius: 17 * dpiScale
            visible: !taskPanel.visible && taskVm.backgroundTaskResults.length > 0
            color: "#13283d"
            border.width: 1
            border.color: "#325274"

            Text {
                anchors.centerIn: parent
                text: "Results"
                color: "#d5e8ff"
                font.pixelSize: Math.round(12 * dpiScale)
            }

            MouseArea {
                anchors.fill: parent
                onClicked: {
                    taskVm.setBackgroundPanelVisible(true)
                    taskVm.notifyTaskPanelRendered()
                }
            }
        }
    }

    Connections {
        target: agentVm

        function onStatusTextChanged() {
            if (agentVm.statusText.length === 0) {
                return
            }
            toastManager.pushToast(
                agentVm.statusText,
                agentVm.statusText.toLowerCase().indexOf("error") >= 0 ? "error" : "status",
                -1,
                "status")
        }

        function onResponseTextChanged() {
            if (agentVm.responseText.length === 0) {
                return
            }
            toastManager.pushToast(agentVm.responseText, "response", -1, "response")
        }
    }

    Connections {
        target: taskVm

        function onLatestTaskToastChanged() {
            if (taskVm.latestTaskToast.length === 0) {
                return
            }
            toastManager.pushToast(
                taskVm.latestTaskToast,
                taskVm.latestTaskToastTone,
                taskVm.latestTaskToastTaskId,
                taskVm.latestTaskToastType)
        }

        function onBackgroundPanelVisibleChanged() {
            if (taskVm.backgroundPanelVisible) {
                taskVm.notifyTaskPanelRendered()
            }
        }
    }
}
