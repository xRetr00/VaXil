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
    color: "#04070c"
    flags: Qt.Window

    property real dpiScale: Math.max(1.0, Screen.devicePixelRatio)
    property int maxLogEntries: 5
    property bool blinkOn: true
    property string lastTranscript: ""
    property string lastResponse: ""
    property string liveResponseText: ""
    property int liveTypingIndex: 0
    property real slowPulse: 0.5 + 0.5 * Math.sin(motion.time * 0.35)
    property real fastPulse: 0.5 + 0.5 * Math.sin(motion.time * 1.15)
    readonly property string iconRoot: "qrc:/qt/qml/VAXIL/gui/assets/Icons/"
    readonly property string liveResponsePreview: "AI: " + (liveTypingIndex > 0
        ? liveResponseText.slice(0, liveTypingIndex)
        : "") + (liveResponseText.length > liveTypingIndex ? "_" : "")

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

    function hasText(value) {
        return value !== undefined && value !== null && value.toString().trim().length > 0
    }

    function installedToolCount() {
        const statuses = settingsVm.toolStatuses || []
        let count = 0
        for (let i = 0; i < statuses.length; ++i) {
            if (statuses[i].installed === true) {
                count += 1
            }
        }
        return count
    }

    function missingToolCount() {
        const statuses = settingsVm.toolStatuses || []
        let count = 0
        for (let i = 0; i < statuses.length; ++i) {
            if (statuses[i].installed !== true) {
                count += 1
            }
        }
        return count
    }

    readonly property bool providerReady: settingsVm.chatProviderKind === "openrouter"
                                         ? hasText(settingsVm.chatProviderApiKey)
                                         : hasText(settingsVm.lmStudioEndpoint)
    readonly property bool modelReady: hasText(settingsVm.selectedModel)
    readonly property bool sttReady: hasText(settingsVm.whisperExecutable) && hasText(settingsVm.whisperModelPath)
    readonly property bool ttsReady: hasText(settingsVm.piperExecutable) && hasText(settingsVm.piperVoiceModel)
    readonly property bool ffmpegReady: hasText(settingsVm.ffmpegExecutable)
    readonly property bool micReady: hasText(settingsVm.selectedAudioInputDeviceId) || settingsVm.audioInputDeviceIds.length > 0
    readonly property bool visionReady: !settingsVm.visionEnabled || hasText(settingsVm.visionEndpoint)
    readonly property bool mcpReady: !settingsVm.mcpEnabled || hasText(settingsVm.mcpServerUrl)
    readonly property bool toolingReady: missingToolCount() === 0
    readonly property bool agentReady: settingsVm.agentEnabled && settingsVm.agentAvailable

    readonly property string providerLabel: settingsVm.chatProviderKind === "openrouter"
                                            ? "OpenRouter"
                                            : settingsVm.chatProviderKind === "ollama"
                                              ? "Ollama"
                                              : "Local OpenAI-Compatible"

    onClosing: function(close) {
        close.accepted = false
        hide()
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
            GradientStop { position: 0.0; color: "#182230" }
            GradientStop { position: 0.32; color: "#0e141d" }
            GradientStop { position: 0.72; color: "#060a10" }
            GradientStop { position: 1.0; color: "#030509" }
        }
    }

    Rectangle {
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: -parent.height * 0.18
        width: Math.min(parent.width * 0.92, 1100)
        height: width
        radius: width / 2
        color: "#54d9f3ff"
        opacity: 0.13
    }

    Rectangle {
        anchors.right: parent.right
        anchors.rightMargin: -parent.width * 0.08
        anchors.top: parent.top
        anchors.topMargin: parent.height * 0.10
        width: Math.min(parent.width * 0.46, 520)
        height: width
        radius: width / 2
        color: "#24ffffff"
        opacity: 0.08
    }

    Item {
        anchors.fill: parent
        opacity: 0.08

        Repeater {
            model: Math.floor(fullUi.width / 44)
            delegate: Rectangle {
                width: 1
                height: fullUi.height
                color: "#24ffffff"
                x: index * 44
            }
        }

        Repeater {
            model: Math.floor(fullUi.height / 44)
            delegate: Rectangle {
                width: fullUi.width
                height: 1
                color: "#18ffffff"
                y: index * 44
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 26
        spacing: 18

        JarvisUi.VisionGlassPanel {
            Layout.fillWidth: true
            Layout.preferredHeight: 74
            radius: 22
            panelColor: "#16161d22"
            innerColor: "#1f1b222c"
            outlineColor: "#24ffffff"
            highlightColor: "#18ffffff"
            shadowOpacity: 0.28

            RowLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 14

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    RowLayout {
                        spacing: 10

                        JarvisUi.VisionGlyph {
                            iconSize: 18
                            source: fullUi.iconRoot + "icons8-ai-50.png"
                        }

                        Text {
                            text: settingsVm.assistantName.toUpperCase() + " COMMAND DECK"
                            color: "#f3f7ff"
                            font.pixelSize: 20
                            font.weight: Font.DemiBold
                            font.letterSpacing: 0.8
                        }
                    }
                }

                Text {
                    text: statusLine()
                    color: agentVm.uiState === 3 ? "#ffd2a3" : agentVm.uiState === 1 ? "#d8f1ff" : "#d6def8"
                    font.pixelSize: 14
                    font.letterSpacing: 1.0
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                anchors.bottomMargin: 10
                height: 1
                radius: height / 2
                color: "#48ffffff"
                opacity: 0.12 + 0.14 * fullUi.slowPulse
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 18

            JarvisUi.VisionGlassPanel {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 430
                Layout.preferredWidth: Math.max(460, fullUi.width * 0.48)
                radius: 30
                panelColor: "#151a2122"
                innerColor: "#1a1f272e"
                outlineColor: "#20ffffff"
                highlightColor: "#14ffffff"
                shadowOpacity: 0.30

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 16

                    Item {
                        id: runtimeHero
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 380
                        clip: true

                        JarvisUi.VisionGlassPanel {
                            anchors.fill: parent
                            radius: 24
                            panelColor: "#13182126"
                            innerColor: "#1a202a31"
                            outlineColor: "#1dffffff"
                            highlightColor: "#10ffffff"
                            shadowOpacity: 0.24

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 16
                                spacing: 12

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8

                                    JarvisUi.VisionGlyph {
                                        iconSize: 14
                                        source: fullUi.iconRoot + "icons8-connect-50.png"
                                    }

                                    Text {
                                        text: "RUNTIME STATUS"
                                        color: "#deebff"
                                        font.pixelSize: 12
                                        font.letterSpacing: 1.6
                                    }

                                    Item { Layout.fillWidth: true }

                                    Rectangle {
                                        width: 9
                                        height: 9
                                        radius: 5
                                        color: fullUi.agentReady ? "#1ecb6b" : "#f04d5d"
                                    }

                                    Text {
                                        text: fullUi.agentReady ? "Agent online" : "Agent offline"
                                        color: fullUi.agentReady ? "#98e5b0" : "#ffb8c0"
                                        font.pixelSize: 12
                                    }
                                }

                                JarvisUi.VisionGlassPanel {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 78
                                    radius: 16
                                    panelColor: "#130f1520"
                                    innerColor: "#17112028"
                                    outlineColor: "#16ffffff"
                                    highlightColor: "#0affffff"
                                    shadowOpacity: 0.14

                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 12
                                        spacing: 4

                                        Text {
                                            text: "AI Model: " + (settingsVm.selectedModel.length > 0 ? settingsVm.selectedModel : "Not selected")
                                            color: "#eef5ff"
                                            font.pixelSize: 13
                                            elide: Text.ElideRight
                                        }

                                        Text {
                                            text: "Provider: " + fullUi.providerLabel
                                            color: "#bfd0e8"
                                            font.pixelSize: 12
                                            elide: Text.ElideRight
                                        }

                                        Text {
                                            text: settingsVm.agentStatus.length > 0 ? settingsVm.agentStatus : "No agent status available."
                                            color: "#9eb3cf"
                                            font.pixelSize: 11
                                            elide: Text.ElideRight
                                        }
                                    }
                                }

                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: 2
                                    columnSpacing: 10
                                    rowSpacing: 8

                                    RowLayout {
                                        spacing: 6
                                        Rectangle { width: 8; height: 8; radius: 4; color: fullUi.providerReady ? "#1ecb6b" : "#f04d5d" }
                                        Text { text: "Provider"; color: "#dbe7fb"; font.pixelSize: 12 }
                                    }

                                    RowLayout {
                                        spacing: 6
                                        Rectangle { width: 8; height: 8; radius: 4; color: fullUi.modelReady ? "#1ecb6b" : "#f04d5d" }
                                        Text { text: "Model"; color: "#dbe7fb"; font.pixelSize: 12 }
                                    }

                                    RowLayout {
                                        spacing: 6
                                        Rectangle { width: 8; height: 8; radius: 4; color: fullUi.sttReady ? "#1ecb6b" : "#f04d5d" }
                                        Text { text: "Whisper STT"; color: "#dbe7fb"; font.pixelSize: 12 }
                                    }

                                    RowLayout {
                                        spacing: 6
                                        Rectangle { width: 8; height: 8; radius: 4; color: fullUi.ttsReady ? "#1ecb6b" : "#f04d5d" }
                                        Text { text: "Piper TTS"; color: "#dbe7fb"; font.pixelSize: 12 }
                                    }

                                    RowLayout {
                                        spacing: 6
                                        Rectangle { width: 8; height: 8; radius: 4; color: fullUi.ffmpegReady ? "#1ecb6b" : "#f04d5d" }
                                        Text { text: "FFmpeg"; color: "#dbe7fb"; font.pixelSize: 12 }
                                    }

                                    RowLayout {
                                        spacing: 6
                                        Rectangle { width: 8; height: 8; radius: 4; color: fullUi.micReady ? "#1ecb6b" : "#f04d5d" }
                                        Text { text: "Microphone"; color: "#dbe7fb"; font.pixelSize: 12 }
                                    }

                                    RowLayout {
                                        spacing: 6
                                        Rectangle { width: 8; height: 8; radius: 4; color: fullUi.visionReady ? "#1ecb6b" : "#f04d5d" }
                                        Text { text: "Vision"; color: "#dbe7fb"; font.pixelSize: 12 }
                                    }

                                    RowLayout {
                                        spacing: 6
                                        Rectangle { width: 8; height: 8; radius: 4; color: fullUi.mcpReady ? "#1ecb6b" : "#f04d5d" }
                                        Text { text: "MCP"; color: "#dbe7fb"; font.pixelSize: 12 }
                                    }
                                }

                                Item { Layout.fillHeight: true }

                                Text {
                                    Layout.fillWidth: true
                                    text: "Installed tools: " + fullUi.installedToolCount() + " / " + (settingsVm.toolStatuses || []).length
                                    color: fullUi.toolingReady ? "#93e1ab" : "#ffd09a"
                                    font.pixelSize: 12
                                }
                            }
                        }
                    }

                    JarvisUi.VisionGlassPanel {
                        Layout.fillWidth: true
                        Layout.minimumHeight: 96
                        Layout.preferredHeight: 124
                        radius: 20
                        panelColor: "#151b2324"
                        innerColor: "#171d262d"
                        outlineColor: "#1effffff"
                        highlightColor: "#10ffffff"
                        shadowOpacity: 0.20

                        JarvisUi.VisionGlassPanel {
                            anchors.fill: parent
                            anchors.margins: 8
                            radius: 16
                            panelColor: "#120f151e"
                            innerColor: "#180f1526"
                            outlineColor: "#16ffffff"
                            highlightColor: "#0affffff"
                            shadowOpacity: 0.14

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 6

                                RowLayout {
                                    spacing: 8

                                    Text {
                                        text: "TOOLING HEALTH"
                                        color: "#dceaff"
                                        font.pixelSize: 11
                                        font.letterSpacing: 1.4
                                    }

                                    Item { Layout.fillWidth: true }

                                    Rectangle {
                                        width: 8
                                        height: 8
                                        radius: 4
                                        color: fullUi.toolingReady ? "#1ecb6b" : "#f09a3e"
                                    }

                                    Text {
                                        text: fullUi.toolingReady ? "All installed" : (fullUi.missingToolCount() + " missing")
                                        color: fullUi.toolingReady ? "#98e5b0" : "#ffd29a"
                                        font.pixelSize: 11
                                    }
                                }

                                ListView {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    model: settingsVm.toolStatuses
                                    spacing: 4

                                    delegate: RowLayout {
                                        required property var modelData
                                        width: parent.width
                                        spacing: 6

                                        Rectangle {
                                            width: 7
                                            height: 7
                                            radius: 3.5
                                            color: modelData.installed ? "#1ecb6b" : "#f04d5d"
                                        }

                                        Text {
                                            text: modelData.name
                                            color: "#e6f0ff"
                                            font.pixelSize: 11
                                            Layout.preferredWidth: 120
                                            elide: Text.ElideRight
                                        }

                                        Text {
                                            text: modelData.installed ? "OK" : "Missing"
                                            color: modelData.installed ? "#99e4b0" : "#ffb8c0"
                                            font.pixelSize: 11
                                            Layout.fillWidth: true
                                            elide: Text.ElideRight
                                        }
                                    }
                                }
                            }
                        }
                    }

                    JarvisUi.VisionGlassPanel {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 58
                        radius: 18
                        panelColor: "#171b2326"
                        innerColor: "#1c21292e"
                        outlineColor: "#1effffff"
                        highlightColor: "#0dffffff"
                        shadowOpacity: 0.18

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 10

                            Text {
                                text: agentVm.statusText.length > 0 ? agentVm.statusText : "Ready for command."
                                color: "#e1eaf7"
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

                JarvisUi.VisionGlassPanel {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: Math.max(240, fullUi.height * 0.30)
                    radius: 28
                    panelColor: "#14191f24"
                    innerColor: "#191e2630"
                    outlineColor: "#1effffff"
                    highlightColor: "#14ffffff"
                    shadowOpacity: 0.28
                    clip: true

                    Rectangle {
                        id: logSheen
                        width: 140
                        height: parent.height
                        y: 0
                        x: -width
                        opacity: 0.12 + 0.08 * fullUi.fastPulse
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: "#00ffffff" }
                            GradientStop { position: 0.5; color: "#2cffffff" }
                            GradientStop { position: 1.0; color: "#00ffffff" }
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

                        RowLayout {
                            spacing: 8

                            JarvisUi.VisionGlyph {
                                iconSize: 14
                                source: fullUi.iconRoot + "icons8-flow-chart-50.png"
                            }

                            Text {
                                text: "MISSION LOG"
                                color: "#dceaff"
                                font.pixelSize: 12
                                font.letterSpacing: 1.8
                            }
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
                                color: model.role === "YOU" ? "#eef2f8" : "#dce9ff"
                                font.pixelSize: 12
                                wrapMode: Text.Wrap
                            }
                        }

                    }
                }

                JarvisUi.VisionGlassPanel {
                    id: liveResponseCard
                    Layout.fillWidth: true
                    Layout.minimumHeight: 136
                    Layout.maximumHeight: Math.max(260, fullUi.height * 0.42)
                    Layout.preferredHeight: Math.min(Layout.maximumHeight,
                                                     Math.max(Layout.minimumHeight,
                                                              liveResponseMeasure.implicitHeight + 64))
                    radius: 22
                    panelColor: "#151b2426"
                    innerColor: "#1a202930"
                    outlineColor: "#1effffff"
                    highlightColor: "#12ffffff"
                    shadowOpacity: 0.22

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 8

                        RowLayout {
                            spacing: 8

                            JarvisUi.VisionGlyph {
                                iconSize: 14
                                source: fullUi.iconRoot + "icons8-radio-tower-50.png"
                            }

                            Text {
                                text: "LIVE RESPONSE"
                                color: "#dceaff"
                                font.pixelSize: 11
                                font.letterSpacing: 1.6
                            }
                        }

                        JarvisUi.VisionGlassPanel {
                            id: liveResponseBody
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: 16
                            panelColor: "#130f151e"
                            innerColor: "#1a0f1626"
                            outlineColor: "#16ffffff"
                            highlightColor: "#0affffff"
                            shadowOpacity: 0.12

                            Text {
                                id: liveResponseMeasure
                                visible: false
                                width: Math.max(120, liveResponseBody.width - 20)
                                text: fullUi.liveResponsePreview
                                font.pixelSize: 12
                                wrapMode: Text.Wrap
                            }

                            ScrollView {
                                anchors.fill: parent
                                anchors.margins: 10
                                clip: true

                                Text {
                                    width: Math.max(120, liveResponseBody.width - 20)
                                    text: fullUi.liveResponsePreview
                                    color: "#eff5ff"
                                    font.pixelSize: 12
                                    wrapMode: Text.Wrap
                                }
                            }
                        }
                    }
                }
            }
        }

        JarvisUi.VisionGlassPanel {
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            radius: 22
            panelColor: "#181c2428"
            innerColor: "#1c212a32"
            outlineColor: "#22ffffff"
            highlightColor: "#12ffffff"
            shadowOpacity: 0.24

            RowLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    TextField {
                        id: inputField
                        anchors.fill: parent
                        placeholderText: "Type a command..."
                        leftPadding: 46
                        onAccepted: {
                            if (text.trim().length === 0) {
                                return
                            }
                            agentVm.submitText(text)
                            text = ""
                        }
                    }

                    JarvisUi.VisionGlyph {
                        anchors.left: parent.left
                        anchors.leftMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        iconSize: 16
                        source: fullUi.iconRoot + "icons8-search-50.png"
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
