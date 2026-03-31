import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import "." as JarvisUi

Window {
    id: fullUi

    width: Math.min(Math.max(960, Screen.width * 0.78), 1400)
    height: Math.min(Math.max(820, Screen.height * 0.86), Screen.height)
    minimumWidth: 920
    minimumHeight: 720
    visible: false
    title: settingsVm.assistantName + " Command Deck"
    color: "#03060c"

    property real dpiScale: Math.max(1.0, Screen.devicePixelRatio)
    property int maxLogEntries: 5
    property bool blinkOn: true
    property string lastTranscript: ""
    property string lastResponse: ""
    property string liveResponseText: ""
    property int liveTypingIndex: 0
    property real slowPulse: 0.5 + 0.5 * Math.sin(motion.time * 0.35)
    property real fastPulse: 0.5 + 0.5 * Math.sin(motion.time * 1.15)

    function appendLog(role, text) {
        const trimmed = (text || "").toString().trim()
        if (trimmed.length === 0) {
            return
        }
        if (logModel.count >= maxLogEntries) {
            logModel.remove(0, 1)
        }
        logModel.append({ role: role, text: trimmed })
    }

    function finalizeLiveResponse() {
        if (liveResponseText.trim().length === 0) {
            return
        }
        appendLog("AI", liveResponseText)
        liveResponseText = ""
        liveTypingIndex = 0
    }

    function statusLine() {
        const state = agentVm.stateName
        if (state === "LISTENING") {
            return (blinkOn ? "*" : " ") + " LISTENING"
        }
        if (state === "PROCESSING" || state === "THINKING") {
            return (blinkOn ? "*" : " ") + " THINKING"
        }
        if (state === "SPEAKING" || state === "EXECUTING") {
            return (blinkOn ? "*" : " ") + " SPEAKING"
        }
        return (blinkOn ? "*" : " ") + " IDLE"
    }

    onVisibleChanged: {
        if (!visible) {
            return
        }
        requestActivate()
        inputField.forceActiveFocus()
    }

    Timer {
        interval: 650
        running: fullUi.visible
        repeat: true
        onTriggered: fullUi.blinkOn = !fullUi.blinkOn
    }

    Timer {
        interval: 8
        running: fullUi.visible && fullUi.liveResponseText.length > fullUi.liveTypingIndex
        repeat: true
        onTriggered: {
            if (fullUi.liveTypingIndex < fullUi.liveResponseText.length) {
                fullUi.liveTypingIndex += 1
            }
        }
    }

    JarvisUi.AnimationController {
        id: motion
        stateName: agentVm.stateName
        inputLevel: agentVm.audioLevel
        overlayVisible: fullUi.visible
        uiState: agentVm.uiState
        wakeTriggerToken: agentVm.wakeTriggerToken
    }

    Connections {
        target: agentVm

        function onTranscriptChanged() {
            if (agentVm.transcript.trim().length === 0) {
                return
            }
            if (agentVm.transcript === fullUi.lastTranscript) {
                return
            }
            fullUi.appendLog("YOU", agentVm.transcript)
            fullUi.lastTranscript = agentVm.transcript
        }

        function onResponseTextChanged() {
            const text = agentVm.responseText || ""
            if (text.length === 0) {
                return
            }
            if (text.length < fullUi.lastResponse.length) {
                fullUi.finalizeLiveResponse()
                fullUi.liveTypingIndex = 0
            }
            fullUi.liveResponseText = text
            if (fullUi.liveTypingIndex > text.length) {
                fullUi.liveTypingIndex = 0
            }
            fullUi.lastResponse = text
        }
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#060b14" }
            GradientStop { position: 0.55; color: "#03060c" }
            GradientStop { position: 1.0; color: "#020408" }
        }
    }

    Item {
        anchors.fill: parent
        opacity: 0.35

        Repeater {
            model: Math.floor(fullUi.width / 44)
            delegate: Rectangle {
                width: 1
                height: fullUi.height
                color: "#0a2230"
                x: index * 44
            }
        }

        Repeater {
            model: Math.floor(fullUi.height / 44)
            delegate: Rectangle {
                width: fullUi.width
                height: 1
                color: "#0a2230"
                y: index * 44
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 26
        spacing: 18

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 74
            radius: 16
            color: "#08111e"
            border.width: 1
            border.color: "#1d3554"

            RowLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 14

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        text: settingsVm.assistantName.toUpperCase() + " COMMAND DECK"
                        color: "#e8f5ff"
                        font.pixelSize: 20
                        font.weight: Font.DemiBold
                        font.letterSpacing: 1.2
                    }
                }

                Text {
                    text: statusLine()
                    color: agentVm.uiState === 3 ? "#ff9846" : agentVm.uiState === 1 ? "#7af0a4" : "#8ad5ff"
                    font.pixelSize: 14
                    font.letterSpacing: 1.4
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 2
                color: "#1a6aa6"
                opacity: 0.2 + 0.35 * fullUi.slowPulse
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 18

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 430
                Layout.preferredWidth: Math.max(460, fullUi.width * 0.48)
                radius: 24
                color: "#06101b"
                border.width: 1
                border.color: "#223a5a"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 16

                    Item {
                        id: visualHero
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 380
                        clip: true

                        readonly property real orbDiameter: Math.min(width * 0.84, height * 0.88)

                        Rectangle {
                            anchors.centerIn: parent
                            width: visualHero.orbDiameter * (1.1 + 0.05 * fullUi.slowPulse)
                            height: width
                            radius: width / 2
                            color: "transparent"
                            border.width: 2
                            border.color: "#1b6fa8"
                            opacity: 0.18 + 0.22 * fullUi.fastPulse
                        }

                        JarvisUi.OrbRenderer {
                            anchors.centerIn: parent
                            width: visualHero.orbDiameter
                            height: visualHero.orbDiameter
                            stateName: agentVm.stateName
                            uiState: agentVm.uiState
                            time: motion.time
                            audioLevel: motion.inputBoost
                            speakingLevel: motion.speakingSignal
                            distortion: motion.distortion
                            glow: motion.glow
                            orbScale: motion.orbScale
                            orbitalRotation: motion.orbitalRotation
                            auraPulse: motion.auraPulse
                            flicker: motion.flicker
                            quality: fullUi.width < 1200 ? qualityMedium : qualityHigh
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.minimumHeight: 96
                        Layout.preferredHeight: 110
                        radius: 16
                        color: "#081422"
                        border.width: 1
                        border.color: "#1c3452"

                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: 8
                            radius: 12
                            color: "#040c16"
                            border.width: 1
                            border.color: "#1a3551"

                            JarvisUi.VoiceWaveRenderer {
                                anchors.fill: parent
                                anchors.margins: 6
                                stateName: agentVm.stateName
                                time: motion.time
                                audioLevel: motion.inputBoost
                                speakingLevel: motion.speakingSignal
                                glow: motion.glow
                                uiState: agentVm.uiState
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 58
                        radius: 14
                        color: "#0b1626"
                        border.width: 1
                        border.color: "#223a5a"

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 10

                            Text {
                                text: agentVm.statusText.length > 0 ? agentVm.statusText : "Ready for command."
                                color: "#b8d4ef"
                                font.pixelSize: 14
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }

                            Button {
                                text: "MIC"
                                onClicked: agentVm.startListening()
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                Layout.preferredWidth: Math.max(280, Math.min(360, fullUi.width * 0.28))
                Layout.maximumWidth: 380
                Layout.fillHeight: true
                spacing: 14

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(260, fullUi.height * 0.36)
                    Layout.maximumHeight: Math.max(320, fullUi.height * 0.48)
                    radius: 22
                    color: "#07111c"
                    border.width: 1
                    border.color: "#223a5a"
                    clip: true

                    Rectangle {
                        id: logSheen
                        width: 140
                        height: parent.height
                        y: 0
                        x: -width
                        opacity: 0.12 + 0.08 * fullUi.fastPulse
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: "#00d4ff00" }
                            GradientStop { position: 0.5; color: "#1a6aa644" }
                            GradientStop { position: 1.0; color: "#00d4ff00" }
                        }

                        NumberAnimation on x {
                            from: -logSheen.width
                            to: logSheen.parent.width
                            duration: 4200
                            loops: Animation.Infinite
                            running: fullUi.visible
                        }
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 12

                        Text {
                            text: "MISSION LOG"
                            color: "#8bd6ff"
                            font.pixelSize: 12
                            font.letterSpacing: 2.4
                        }

                        ListView {
                            id: logView
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            model: logModel
                            clip: true
                            spacing: 8

                            delegate: Text {
                                width: logView.width
                                text: (model.role === "YOU" ? "YOU: " : "AI: ") + model.text
                                color: model.role === "YOU" ? "#e8e8e8" : "#00d4ff"
                                font.pixelSize: 12
                                wrapMode: Text.Wrap
                            }
                        }

                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 132
                    radius: 18
                    color: "#081422"
                    border.width: 1
                    border.color: "#1c3452"

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 8

                        Text {
                            text: "LIVE RESPONSE"
                            color: "#8bd6ff"
                            font.pixelSize: 11
                            font.letterSpacing: 2.0
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: 12
                            color: "#06101b"
                            border.width: 1
                            border.color: "#1c3452"

                            Text {
                                anchors.fill: parent
                                anchors.margins: 10
                                text: "AI: " + (fullUi.liveTypingIndex > 0
                                       ? fullUi.liveResponseText.slice(0, fullUi.liveTypingIndex)
                                       : "") + (fullUi.liveResponseText.length > fullUi.liveTypingIndex ? "_" : "")
                                color: "#00d4ff"
                                font.pixelSize: 12
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }

                Item {
                    Layout.fillHeight: true
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            radius: 18
            color: "#0b1626"
            border.width: 1
            border.color: "#223a5a"

            RowLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10

                TextField {
                    id: inputField
                    Layout.fillWidth: true
                    placeholderText: "Type a command..."
                    onAccepted: {
                        if (text.trim().length === 0) {
                            return
                        }
                        agentVm.submitText(text)
                        text = ""
                    }
                }

                Button {
                    text: "SEND"
                    onClicked: {
                        if (inputField.text.trim().length === 0) {
                            return
                        }
                        agentVm.submitText(inputField.text)
                        inputField.text = ""
                    }
                }
            }
        }
    }

    ListModel { id: logModel }
}
