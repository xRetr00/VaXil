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

    readonly property int stateIdle: 0
    readonly property int stateListening: 1
    readonly property int stateThinking: 2
    readonly property int stateExecuting: 3

    property real dpiScale: Math.max(1.0, Math.max(Screen.devicePixelRatio, Screen.pixelDensity / 3.78))
    property real shortEdge: Math.min(width, height)
    property real pageMargin: Math.max(22 * dpiScale, Math.min(shortEdge * 0.045, 56 * dpiScale))
    property real sectionSpacing: Math.max(12 * dpiScale, shortEdge * 0.012)
    property real contentWidth: Math.min(width * 0.4, 520 * dpiScale)
    property real orbBaseSize: Math.max(220 * dpiScale, Math.min(shortEdge * 0.31, 420 * dpiScale))
    property bool showTaskPanel: taskVm.backgroundPanelVisible && taskVm.backgroundTaskResults.length > 0
    property real sideLaneWidth: taskVm.backgroundTaskResults.length > 0
        ? Math.min(width * (width >= 1480 * dpiScale ? 0.3 : 0.2), 470 * dpiScale)
        : 0

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
        if (agentVm.uiState === stateIdle) {
            return greetingLine()
        }
        return compactText(agentVm.statusText, "Ready.")
    }

    function microStatus() {
        if (agentVm.uiState === stateListening) {
            return "Listening"
        }
        if (agentVm.uiState === stateThinking) {
            return "Thinking"
        }
        if (agentVm.uiState === stateExecuting) {
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

        RowLayout {
            anchors.fill: parent
            anchors.margins: pageMargin
            spacing: pageMargin

            Item {
                Layout.preferredWidth: sideLaneWidth
                Layout.fillHeight: true
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: sectionSpacing

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: agentVm.assistantName + "  |  " + microStatus()
                    color: "#dbeeff"
                    opacity: 0.78
                    font.pixelSize: Math.round(12 * dpiScale)
                    font.letterSpacing: 1.8
                }

                Item { Layout.fillHeight: true }

                Item {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: contentWidth
                    Layout.maximumWidth: contentWidth
                    implicitHeight: presenceStack.implicitHeight
                    transform: Translate {
                        x: agentVm.presenceOffsetX * 16 * dpiScale
                        y: -agentVm.presenceOffsetY * 12 * dpiScale
                    }

                    ColumnLayout {
                        id: presenceStack
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: parent.width
                        spacing: 8 * dpiScale

                        Text {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            text: presenceLine()
                            color: "#edf6ff"
                            font.pixelSize: Math.round(Math.max(18 * dpiScale, Math.min(root.shortEdge * 0.03, 28 * dpiScale)))
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
                }

                Item {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: orbBaseSize
                    Layout.preferredHeight: orbBaseSize
                    transform: Translate {
                        x: agentVm.presenceOffsetX * 22 * dpiScale + motion.listeningVibrationX * 6 * dpiScale
                        y: -agentVm.presenceOffsetY * 18 * dpiScale + motion.listeningVibrationY * 6 * dpiScale
                    }

                    JarvisUi.OrbRenderer {
                        id: orb
                        anchors.fill: parent
                        stateName: agentVm.stateName
                        uiState: agentVm.uiState
                        quality: root.shortEdge < 760 * dpiScale ? orb.qualityLow
                            : root.shortEdge < 1100 * dpiScale ? orb.qualityMedium
                            : orb.qualityHigh
                        time: motion.time
                        audioLevel: motion.inputBoost
                        speakingLevel: motion.speakingSignal
                        distortion: motion.distortion
                        glow: motion.glow
                        orbScale: motion.orbScale
                        orbitalRotation: motion.orbitalRotation
                        auraPulse: motion.auraPulse
                        flicker: motion.flicker
                    }
                }

                Item { Layout.fillHeight: true }
            }

            Item {
                Layout.preferredWidth: sideLaneWidth
                Layout.fillHeight: true
                visible: taskVm.backgroundTaskResults.length > 0

                ColumnLayout {
                    anchors.fill: parent
                    spacing: sectionSpacing

                    Item { Layout.fillHeight: true }

                    Rectangle {
                        id: taskPanel
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        Layout.preferredHeight: Math.min(parent.height * 0.7, 640 * dpiScale)
                        visible: root.showTaskPanel
                        radius: 28 * dpiScale
                        color: "#c40b1320"
                        border.width: 1
                        border.color: "#284762"

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 18 * dpiScale
                            spacing: 12 * dpiScale

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10 * dpiScale

                                Text {
                                    Layout.fillWidth: true
                                    text: "Background Results"
                                    color: "#eef7ff"
                                    font.pixelSize: Math.round(20 * dpiScale)
                                    font.weight: Font.Medium
                                }

                                Button {
                                    text: "Hide"
                                    onClicked: taskVm.setBackgroundPanelVisible(false)
                                }
                            }

                            ScrollView {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true

                                ColumnLayout {
                                    width: taskPanel.width - 54 * dpiScale
                                    spacing: 12 * dpiScale

                                    Repeater {
                                        model: taskVm.backgroundTaskResults

                                        delegate: Rectangle {
                                            required property var modelData
                                            Layout.fillWidth: true
                                            width: parent.width
                                            radius: 18 * dpiScale
                                            color: "#101d2b"
                                            border.width: 1
                                            border.color: modelData.success ? "#346b52" : "#7a4557"
                                            implicitHeight: resultColumn.implicitHeight + 20 * dpiScale

                                            ColumnLayout {
                                                id: resultColumn
                                                anchors.fill: parent
                                                anchors.margins: 12 * dpiScale
                                                spacing: 6 * dpiScale

                                                Text {
                                                    Layout.fillWidth: true
                                                    text: modelData.finishedAt + "  " + modelData.title
                                                    color: "#edf6ff"
                                                    font.pixelSize: Math.round(13 * dpiScale)
                                                    wrapMode: Text.Wrap
                                                }

                                                Text {
                                                    Layout.fillWidth: true
                                                    text: modelData.summary
                                                    color: modelData.success ? "#8fe1b0" : "#ffb5cc"
                                                    font.pixelSize: Math.round(12 * dpiScale)
                                                    wrapMode: Text.Wrap
                                                }

                                                Text {
                                                    Layout.fillWidth: true
                                                    text: modelData.detail
                                                    color: "#bfd3ea"
                                                    font.pixelSize: Math.round(12 * dpiScale)
                                                    wrapMode: Text.Wrap
                                                }

                                                TextArea {
                                                    Layout.fillWidth: true
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

                    Item { Layout.fillHeight: true }

                    Button {
                        Layout.alignment: Qt.AlignRight
                        visible: !root.showTaskPanel && taskVm.backgroundTaskResults.length > 0
                        text: "Results"
                        onClicked: {
                            taskVm.setBackgroundPanelVisible(true)
                            taskVm.notifyTaskPanelRendered()
                        }
                    }
                }
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
