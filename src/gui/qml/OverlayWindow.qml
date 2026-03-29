import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import "." as JarvisUi

Window {
    id: root

    x: Screen.virtualX
    y: Screen.virtualY
    width: Screen.width
    height: Screen.height
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
    property real presenceDriftX: agentVm.presenceOffsetX * Math.min(width * 0.04, 34 * dpiScale)
    property real presenceDriftY: agentVm.presenceOffsetY * Math.min(height * 0.03, 24 * dpiScale)
    property real bgLuma: (0.2126 * palette.window.r) + (0.7152 * palette.window.g) + (0.0722 * palette.window.b)
    property bool useDarkText: bgLuma > 0.55
    property color textPrimaryColor: useDarkText ? "#14202c" : "#edf6ff"
    property color textSecondaryColor: useDarkText ? "#2b4056" : "#bfd3ea"
    property color textMutedColor: useDarkText ? "#3f5871" : "#7f9fc7"
    property color textOutlineColor: useDarkText ? "#80ffffff" : "#90060d14"
    property bool showingModelResponse: agentVm.responseText.trim().length > 0

    onClosing: function(close) {
        close.accepted = false
        hide()
    }

    onVisibleChanged: {
        if (visible) {
            requestActivate()
            keyCapture.forceActiveFocus()
        }
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
        if (root.showingModelResponse) {
            return compactText(agentVm.responseText, "")
        }
        if (agentVm.uiState === stateIdle) {
            return greetingLine()
        }
        return compactText(agentVm.statusText, "Ready.")
    }

    function shouldShowStatusToast(statusText) {
        const value = compactText(statusText, "").toLowerCase()
        if (value.length === 0) {
            return false
        }
        return value !== "processing request"
            && value !== "listening"
            && value !== "thinking"
            && value !== "executing"
            && value !== "idle"
            && value !== "response ready"
    }

    function shouldShowModelToast(responseText) {
        const normalizedResponse = compactText(responseText, "").toLowerCase()
        return normalizedResponse.length > 0
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
        wakeTriggerToken: agentVm.wakeTriggerToken
    }

    Shortcut {
        sequence: "M"
        context: Qt.ApplicationShortcut
        enabled: root.visible
        onActivated: agentVm.interruptSpeechAndListen()
    }

    Item {
        id: keyCapture
        anchors.fill: parent
        focus: root.visible
        z: -1
        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_M) {
                agentVm.interruptSpeechAndListen()
                event.accepted = true
            }
        }
    }

    Item {
        anchors.fill: parent

        Column {
            anchors.top: parent.top
            anchors.topMargin: root.pageMargin
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 4 * root.dpiScale

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "VAXIL ONLINE"
                color: root.textPrimaryColor
                opacity: 0.82
                style: Text.Outline
                styleColor: root.textOutlineColor
                font.pixelSize: Math.round(11 * root.dpiScale)
                font.letterSpacing: 2.4
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: microStatus()
                color: root.textMutedColor
                opacity: 0.88
                style: Text.Outline
                styleColor: root.textOutlineColor
                font.pixelSize: Math.round(12 * root.dpiScale)
                font.letterSpacing: 1.6
            }
        }

        Item {
            id: centerFrame
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: root.height * 0.06 + root.presenceDriftY
            width: Math.max(root.contentWidth, root.orbBaseSize)
            height: centerStack.implicitHeight
            transform: Translate {
                x: root.presenceDriftX
                y: 0
            }

            ColumnLayout {
                id: centerStack
                anchors.fill: parent
                spacing: root.sectionSpacing

                Item {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: root.contentWidth
                    Layout.maximumWidth: root.contentWidth
                    implicitHeight: presenceStack.implicitHeight

                    ColumnLayout {
                        id: presenceStack
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: parent.width
                        spacing: 8 * root.dpiScale

                        Text {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            text: presenceLine()
                            color: root.textPrimaryColor
                            font.pixelSize: Math.round(Math.max(18 * root.dpiScale, Math.min(root.shortEdge * 0.03, 28 * root.dpiScale)))
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                            style: Text.Outline
                            styleColor: root.textOutlineColor
                            layer.enabled: false
                        }

                        Text {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            text: compactText(agentVm.statusText, "")
                            visible: text.length > 0 && text !== presenceLine()
                            color: root.textMutedColor
                            font.pixelSize: Math.round(12 * root.dpiScale)
                            wrapMode: Text.Wrap
                            maximumLineCount: 1
                            elide: Text.ElideRight
                            style: Text.Outline
                            styleColor: root.textOutlineColor
                        }
                    }
                }

                Item {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: root.orbBaseSize
                    Layout.preferredHeight: root.orbBaseSize
                    transform: Translate {
                        x: motion.listeningVibrationX * 4 * root.dpiScale
                        y: motion.listeningVibrationY * 4 * root.dpiScale
                    }

                    JarvisUi.OrbRenderer {
                        id: orb
                        anchors.fill: parent
                        stateName: agentVm.stateName
                        uiState: agentVm.uiState
                        quality: root.shortEdge < 760 * root.dpiScale ? orb.qualityLow
                            : root.shortEdge < 1100 * root.dpiScale ? orb.qualityMedium
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
            }
        }

        Rectangle {
            id: taskPanel
            anchors.right: parent.right
            anchors.rightMargin: root.pageMargin
            anchors.verticalCenter: parent.verticalCenter
            width: root.sideLaneWidth
            height: Math.min(parent.height * 0.7, 640 * root.dpiScale)
            visible: root.showTaskPanel
            radius: 28 * root.dpiScale
            color: "#c40b1320"
            border.width: 1
            border.color: "#284762"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18 * root.dpiScale
                spacing: 12 * root.dpiScale

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10 * root.dpiScale

                    Text {
                        Layout.fillWidth: true
                        text: "Background Results"
                        color: root.textPrimaryColor
                        style: Text.Outline
                        styleColor: root.textOutlineColor
                        font.pixelSize: Math.round(20 * root.dpiScale)
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
                        width: taskPanel.width - 54 * root.dpiScale
                        spacing: 12 * root.dpiScale

                        Repeater {
                            model: taskVm.backgroundTaskResults

                            delegate: Rectangle {
                                id: resultCard
                                required property var modelData
                                Layout.fillWidth: true
                                width: parent.width
                                radius: 18 * root.dpiScale
                                color: "#101d2b"
                                border.width: 1
                                border.color: modelData.success ? "#346b52" : "#7a4557"
                                implicitHeight: resultColumn.implicitHeight + 20 * root.dpiScale

                                ColumnLayout {
                                    id: resultColumn
                                    anchors.fill: parent
                                    anchors.margins: 12 * root.dpiScale
                                    spacing: 6 * root.dpiScale

                                    Text {
                                        Layout.fillWidth: true
                                        text: resultCard.modelData.finishedAt + "  " + resultCard.modelData.title
                                        color: root.textPrimaryColor
                                        font.pixelSize: Math.round(13 * root.dpiScale)
                                        wrapMode: Text.Wrap
                                        style: Text.Outline
                                        styleColor: root.textOutlineColor
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: resultCard.modelData.summary
                                        color: resultCard.modelData.success ? "#8fe1b0" : "#ffb5cc"
                                        font.pixelSize: Math.round(12 * root.dpiScale)
                                        wrapMode: Text.Wrap
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: resultCard.modelData.detail
                                        color: root.textSecondaryColor
                                        font.pixelSize: Math.round(12 * root.dpiScale)
                                        wrapMode: Text.Wrap
                                        style: Text.Outline
                                        styleColor: root.textOutlineColor
                                    }

                                    TextArea {
                                        Layout.fillWidth: true
                                        readOnly: true
                                        wrapMode: TextArea.Wrap
                                        text: resultCard.modelData.payload
                                        color: root.textPrimaryColor
                                        background: Rectangle {
                                            radius: 12 * root.dpiScale
                                            color: "#0b1520"
                                            border.width: 1
                                            border.color: "#20364b"
                                        }
                                        implicitHeight: Math.min(160 * root.dpiScale, Math.max(60 * root.dpiScale, contentHeight + 18 * root.dpiScale))
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        Button {
            anchors.right: parent.right
            anchors.rightMargin: root.pageMargin
            anchors.bottom: parent.bottom
            anchors.bottomMargin: root.pageMargin
            visible: !root.showTaskPanel && taskVm.backgroundTaskResults.length > 0
            text: "Results"
            onClicked: {
                taskVm.setBackgroundPanelVisible(true)
                taskVm.notifyTaskPanelRendered()
            }
        }

        Button {
            anchors.left: parent.left
            anchors.leftMargin: root.pageMargin
            anchors.bottom: parent.bottom
            anchors.bottomMargin: root.pageMargin
            visible: agentVm.uiState === root.stateExecuting || agentVm.uiState === root.stateThinking
            text: "M"
            ToolTip.visible: hovered
            ToolTip.text: "Mute/interrupt and listen"
            onClicked: agentVm.interruptSpeechAndListen()
        }

        JarvisUi.ToastManager {
            id: toastManager
            anchors.right: parent.right
            anchors.rightMargin: pageMargin
            anchors.bottom: parent.bottom
            anchors.bottomMargin: pageMargin
            latestUserPrompt: agentVm.transcript
            onToastClicked: function(taskId) {
                if (taskId < 0) {
                    return
                }
                taskVm.setBackgroundPanelVisible(true)
                taskVm.notifyTaskToastShown(taskId)
            }
        }
    }

    Connections {
        target: agentVm

        function onStatusTextChanged() {
            if (!root.shouldShowStatusToast(agentVm.statusText)) {
                return
            }
            toastManager.pushToast(
                agentVm.statusText,
                agentVm.statusText.toLowerCase().indexOf("error") >= 0 ? "error" : "status",
                -1,
                "status")
        }

        function onResponseTextChanged() {
            if (!root.shouldShowModelToast(agentVm.responseText)) {
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
