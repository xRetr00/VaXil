import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import "." as JarvisUi

Window {
    id: settingsWindow

    width: 680
    height: 860
    visible: false
    title: backend.assistantName + " Control Surface"
    color: "#050912"

    onClosing: function(close) {
        close.accepted = false
        hide()
    }

    property var requirementStatus: ({})

    function statusColor(ok) {
        return ok ? "#1ecb6b" : "#f04d5d"
    }

    function statusText(ok) {
        return ok ? "OK" : "Missing"
    }

    function refreshRequirementStatus() {
        requirementStatus = backend.evaluateSetupRequirements(
            endpointField.text,
            modelCombo.currentText,
            whisperPathField.text,
            whisperModelPathField.text,
            "",
            "",
            piperPathField.text,
            voicePathField.text,
            ffmpegPathField.text
        )
    }

    function syncFieldsFromBackend() {
        endpointField.text = backend.lmStudioEndpoint
        whisperPathField.text = backend.whisperExecutable
        whisperModelPathField.text = backend.whisperModelPath
        piperPathField.text = backend.piperExecutable
        voicePathField.text = backend.piperVoiceModel
        ffmpegPathField.text = backend.ffmpegExecutable
        speedSlider.value = backend.voiceSpeed
        pitchSlider.value = backend.voicePitch
        vadSlider.value = backend.vadSensitivity
        micSlider.value = backend.micSensitivity
        aecCheck.checked = backend.aecEnabled
        rnnoiseCheck.checked = backend.rnnoiseEnabled
        autoRoutingCheck.checked = backend.autoRoutingEnabled
        streamCheck.checked = backend.streamingEnabled
        timeoutSpin.value = backend.requestTimeoutMs
        clickThroughCheck.checked = backend.clickThroughEnabled
        agentEnabledCheck.checked = backend.agentEnabled
        agentProviderField.text = backend.agentProviderMode
        conversationTempSlider.value = backend.conversationTemperature
        conversationTopPField.text = backend.conversationTopP > 0 ? backend.conversationTopP.toFixed(2) : ""
        toolTempSlider.value = backend.toolUseTemperature
        providerTopKField.text = backend.providerTopK > 0 ? String(backend.providerTopK) : ""
        maxOutputSpin.value = backend.maxOutputTokens
        memoryAutoWriteCheck.checked = backend.memoryAutoWrite
        webProviderField.text = backend.webSearchProvider
        tracePanelCheck.checked = backend.tracePanelEnabled

        const modelIndex = backend.models.indexOf(backend.selectedModel)
        modelCombo.currentIndex = modelIndex >= 0 ? modelIndex : 0

        const modeIndex = backend.defaultReasoningMode
        modeCombo.currentIndex = modeIndex >= 0 ? modeIndex : 0

        const ttsIndex = ["piper"].indexOf(backend.ttsEngineKind)
        ttsEngineCombo.currentIndex = ttsIndex >= 0 ? ttsIndex : 0

        const wakeIndex = ["sherpa-onnx"].indexOf(backend.wakeEngineKind)
        wakeEngineCombo.currentIndex = wakeIndex >= 0 ? wakeIndex : 0

        const whisperModelIndex = backend.whisperModelPresetIds.indexOf(backend.selectedWhisperModelPresetId)
        whisperModelPresetCombo.currentIndex = whisperModelIndex >= 0 ? whisperModelIndex : 1
    }

    onVisibleChanged: {
        if (!visible) {
            return
        }

        backend.refreshAudioDevices()
        syncFieldsFromBackend()
        const inputIndex = backend.audioInputDeviceIds.indexOf(backend.selectedAudioInputDeviceId)
        inputDeviceCombo.currentIndex = inputIndex >= 0 ? inputIndex : 0
        const outputIndex = backend.audioOutputDeviceIds.indexOf(backend.selectedAudioOutputDeviceId)
        outputDeviceCombo.currentIndex = outputIndex >= 0 ? outputIndex : 0
        refreshRequirementStatus()
    }

    JarvisUi.AnimationController {
        id: heroMotion
        stateName: "PROCESSING"
        inputLevel: 0.03
        overlayVisible: settingsWindow.visible
    }

    Rectangle {
        anchors.fill: parent
        color: "#050912"
    }

    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 220
        color: "#0a1324"
    }

    Rectangle {
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: -90
        width: 360
        height: 360
        radius: 180
        color: "#1c2f5e"
        opacity: 0.14
    }

    ScrollView {
        anchors.fill: parent
        anchors.margins: 22
        clip: true

        Column {
            width: parent.width
            spacing: 18

            Rectangle {
                width: parent.width
                height: 220
                radius: 34
                color: "#9e08111d"
                border.width: 1
                border.color: "#20314e"

                JarvisUi.OrbRenderer {
                    anchors.left: parent.left
                    anchors.leftMargin: 22
                    anchors.verticalCenter: parent.verticalCenter
                    width: 170
                    height: 170
                    stateName: "PROCESSING"
                    time: heroMotion.time
                    audioLevel: heroMotion.inputBoost
                    speakingLevel: heroMotion.speakingSignal
                    distortion: heroMotion.distortion
                    glow: heroMotion.glow
                    orbScale: 0.92 + heroMotion.orbScale * 0.08
                    orbitalRotation: heroMotion.orbitalRotation
                }

                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: 220
                    anchors.right: parent.right
                    anchors.rightMargin: 28
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 10

                    Text {
                        text: backend.assistantName
                        color: "#eff7ff"
                        font.pixelSize: 34
                        font.weight: Font.Medium
                    }

                    Text {
                        text: "Control surface for the local core, voice pipeline, and overlay behavior."
                        color: "#8ea8c8"
                        font.pixelSize: 15
                        wrapMode: Text.Wrap
                    }

                    Row {
                        spacing: 10

                        Rectangle {
                            width: 132
                            height: 32
                            radius: 16
                            color: "#0f1d31"
                            border.width: 1
                            border.color: "#284564"

                            Text {
                                anchors.centerIn: parent
                                text: backend.selectedModel.length > 0 ? backend.selectedModel : "No model"
                                color: "#d9ecff"
                                font.pixelSize: 12
                            }
                        }

                        Rectangle {
                            width: 120
                            height: 32
                            radius: 16
                            color: "#0f1d31"
                            border.width: 1
                            border.color: "#284564"

                            Text {
                                anchors.centerIn: parent
                                text: backend.autoRoutingEnabled ? "Auto routing" : "Manual routing"
                                color: "#d9ecff"
                                font.pixelSize: 12
                            }
                        }
                    }
                }
            }

            Rectangle {
                width: parent.width
                radius: 30
                color: "#9208111d"
                border.width: 1
                border.color: "#1d2f4d"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 22
                    spacing: 14

                    Text {
                        text: "AI Core"
                        color: "#eef7ff"
                        font.pixelSize: 22
                        font.weight: Font.Medium
                    }

                    Text {
                        text: "Model discovery, routing defaults, and request timing."
                        color: "#8099b8"
                        font.pixelSize: 14
                    }

                    Text { text: "Local AI backend endpoint"; color: "#c9def3"; font.pixelSize: 13 }
                    TextField { id: endpointField; Layout.fillWidth: true; text: backend.lmStudioEndpoint }
                    RowLayout {
                        Layout.fillWidth: true
                        Rectangle { width: 10; height: 10; radius: 5; color: settingsWindow.statusColor(requirementStatus.endpointOk === true) }
                        Text { text: "Endpoint: " + settingsWindow.statusText(requirementStatus.endpointOk === true); color: "#9ab0ca"; font.pixelSize: 12 }
                    }

                    Text { text: "Selected model"; color: "#c9def3"; font.pixelSize: 13 }
                    ComboBox {
                        id: modelCombo
                        Layout.fillWidth: true
                        model: backend.models
                        Component.onCompleted: {
                            const index = backend.models.indexOf(backend.selectedModel)
                            if (index >= 0) {
                                currentIndex = index
                            }
                        }
                        onActivated: backend.setSelectedModel(currentText)
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Rectangle { width: 10; height: 10; radius: 5; color: settingsWindow.statusColor(requirementStatus.modelOk === true) }
                        Text { text: "Model: " + settingsWindow.statusText(requirementStatus.modelOk === true); color: "#9ab0ca"; font.pixelSize: 12 }
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        ColumnLayout {
                            Layout.fillWidth: true
                            Text { text: "Default reasoning"; color: "#c9def3"; font.pixelSize: 13 }
                            ComboBox {
                                id: modeCombo
                                Layout.fillWidth: true
                                model: ["Fast", "Balanced", "Deep"]
                                currentIndex: backend.defaultReasoningMode
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Text { text: "Timeout"; color: "#c9def3"; font.pixelSize: 13 }
                            SpinBox {
                                id: timeoutSpin
                                Layout.fillWidth: true
                                from: 10000
                                to: 15000
                                value: backend.requestTimeoutMs
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        CheckBox { id: autoRoutingCheck; text: "Enable auto routing"; checked: backend.autoRoutingEnabled }
                        CheckBox { id: streamCheck; text: "Enable streaming"; checked: backend.streamingEnabled }
                    }
                }
            }

            Rectangle {
                width: parent.width
                radius: 30
                color: "#9208111d"
                border.width: 1
                border.color: "#1d2f4d"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 22
                    spacing: 14

                    Text {
                        text: "Agent Runtime"
                        color: "#eef7ff"
                        font.pixelSize: 22
                        font.weight: Font.Medium
                    }

                    Text {
                        text: backend.agentStatus
                        color: backend.agentAvailable ? "#87d7a2" : "#d8a17a"
                        font.pixelSize: 13
                        wrapMode: Text.Wrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        CheckBox { id: agentEnabledCheck; text: "Enable agent mode"; checked: backend.agentEnabled }
                        CheckBox { id: memoryAutoWriteCheck; text: "Auto-write memory"; checked: backend.memoryAutoWrite }
                        CheckBox { id: tracePanelCheck; text: "Trace panel"; checked: backend.tracePanelEnabled }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            Text { text: "Provider mode"; color: "#c9def3"; font.pixelSize: 13 }
                            TextField { id: agentProviderField; Layout.fillWidth: true; text: backend.agentProviderMode }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Text { text: "Web provider"; color: "#c9def3"; font.pixelSize: 13 }
                            TextField { id: webProviderField; Layout.fillWidth: true; text: backend.webSearchProvider }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            Text { text: "Conversation temperature"; color: "#c9def3"; font.pixelSize: 13 }
                            Slider { id: conversationTempSlider; Layout.fillWidth: true; from: 0.0; to: 1.5; value: backend.conversationTemperature }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Text { text: "Tool temperature"; color: "#c9def3"; font.pixelSize: 13 }
                            Slider { id: toolTempSlider; Layout.fillWidth: true; from: 0.0; to: 1.0; value: backend.toolUseTemperature }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            Text { text: "Top P"; color: "#c9def3"; font.pixelSize: 13 }
                            TextField { id: conversationTopPField; Layout.fillWidth: true; text: backend.conversationTopP > 0 ? backend.conversationTopP.toFixed(2) : "" }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Text { text: "Provider Top K"; color: "#c9def3"; font.pixelSize: 13 }
                            TextField { id: providerTopKField; Layout.fillWidth: true; text: backend.providerTopK > 0 ? String(backend.providerTopK) : "" }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Text { text: "Max output tokens"; color: "#c9def3"; font.pixelSize: 13 }
                            SpinBox { id: maxOutputSpin; Layout.fillWidth: true; from: 64; to: 8192; value: backend.maxOutputTokens }
                        }
                    }

                    Button {
                        text: "Save agent settings"
                        onClicked: backend.saveAgentSettings(
                            agentEnabledCheck.checked,
                            agentProviderField.text,
                            conversationTempSlider.value,
                            parseFloat(conversationTopPField.text || "0"),
                            toolTempSlider.value,
                            parseInt(providerTopKField.text || "0"),
                            maxOutputSpin.value,
                            memoryAutoWriteCheck.checked,
                            webProviderField.text,
                            tracePanelCheck.checked
                        )
                    }
                }
            }

            Rectangle {
                width: parent.width
                radius: 30
                color: "#9208111d"
                border.width: 1
                border.color: "#1d2f4d"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 22
                    spacing: 14

                    Text {
                        text: "Voice Pipeline"
                        color: "#eef7ff"
                        font.pixelSize: 22
                        font.weight: Font.Medium
                    }

                    Text {
                        text: "Local binaries, voice model, and speech tuning."
                        color: "#8099b8"
                        font.pixelSize: 14
                    }

                    Text { text: "TTS engine"; color: "#c9def3"; font.pixelSize: 13 }
                    ComboBox {
                        id: ttsEngineCombo
                        Layout.fillWidth: true
                        model: ["piper"]
                        currentIndex: Math.max(0, model.indexOf(backend.ttsEngineKind))
                        onActivated: backend.setTtsEngineKind(currentText)
                    }

                    Text { text: "whisper.cpp executable"; color: "#c9def3"; font.pixelSize: 13 }
                    TextField { id: whisperPathField; Layout.fillWidth: true; text: backend.whisperExecutable }
                    RowLayout {
                        Layout.fillWidth: true
                        Rectangle { width: 10; height: 10; radius: 5; color: settingsWindow.statusColor(requirementStatus.whisperOk === true) }
                        Text {
                            text: "Whisper: " + settingsWindow.statusText(requirementStatus.whisperOk === true)
                                + (requirementStatus.whisperVersion ? " (" + requirementStatus.whisperVersion + ")" : "")
                            color: "#9ab0ca"
                            font.pixelSize: 12
                        }
                    }

                    Text { text: "Whisper model"; color: "#c9def3"; font.pixelSize: 13 }
                    TextField { id: whisperModelPathField; Layout.fillWidth: true; text: backend.whisperModelPath }
                    RowLayout {
                        Layout.fillWidth: true
                        Rectangle { width: 10; height: 10; radius: 5; color: settingsWindow.statusColor(requirementStatus.whisperModelOk === true) }
                        Text {
                            text: "Whisper model: " + settingsWindow.statusText(requirementStatus.whisperModelOk === true)
                            color: "#9ab0ca"
                            font.pixelSize: 12
                        }
                    }
                    Text { text: "Official Whisper model"; color: "#c9def3"; font.pixelSize: 13 }
                    RowLayout {
                        Layout.fillWidth: true
                        ComboBox {
                            id: whisperModelPresetCombo
                            Layout.fillWidth: true
                            model: backend.whisperModelPresetNames
                            Component.onCompleted: {
                                const index = backend.whisperModelPresetIds.indexOf(backend.selectedWhisperModelPresetId)
                                currentIndex = index >= 0 ? index : 1
                            }
                        }
                        Button {
                            text: "Download"
                            onClicked: {
                                backend.downloadWhisperModel(backend.whisperModelPresetIds[whisperModelPresetCombo.currentIndex])
                                settingsWindow.syncFieldsFromBackend()
                                settingsWindow.refreshRequirementStatus()
                            }
                        }
                        Button {
                            text: "Auto Detect"
                            onClicked: {
                                backend.autoDetectVoiceTools()
                                settingsWindow.syncFieldsFromBackend()
                                settingsWindow.refreshRequirementStatus()
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Rectangle { width: 10; height: 10; radius: 5; color: settingsWindow.statusColor(requirementStatus.whisperLatestOk === true) }
                        Text {
                            text: "Whisper latest: " + (requirementStatus.whisperLatestOk === true ? "Up to date" : "Check update")
                                + (requirementStatus.whisperLatestTag ? " [" + requirementStatus.whisperLatestTag + "]" : "")
                            color: "#9ab0ca"
                            font.pixelSize: 12
                        }
                    }

                    Text { text: "Piper executable"; color: "#c9def3"; font.pixelSize: 13 }
                    TextField { id: piperPathField; Layout.fillWidth: true; text: backend.piperExecutable }
                    RowLayout {
                        Layout.fillWidth: true
                        Rectangle { width: 10; height: 10; radius: 5; color: settingsWindow.statusColor(requirementStatus.piperOk === true) }
                        Text {
                            text: "Piper: " + settingsWindow.statusText(requirementStatus.piperOk === true)
                                + (requirementStatus.piperVersion ? " (" + requirementStatus.piperVersion + ")" : "")
                            color: "#9ab0ca"
                            font.pixelSize: 12
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Rectangle { width: 10; height: 10; radius: 5; color: settingsWindow.statusColor(requirementStatus.piperLatestOk === true) }
                        Text {
                            text: "Piper latest: " + (requirementStatus.piperLatestOk === true ? "Up to date" : "Check update")
                                + (requirementStatus.piperLatestTag ? " [" + requirementStatus.piperLatestTag + "]" : "")
                            color: "#9ab0ca"
                            font.pixelSize: 12
                        }
                    }

                    Text { text: "Piper voice model"; color: "#c9def3"; font.pixelSize: 13 }
                    TextField { id: voicePathField; Layout.fillWidth: true; text: backend.piperVoiceModel }
                    RowLayout {
                        Layout.fillWidth: true
                        Rectangle { width: 10; height: 10; radius: 5; color: settingsWindow.statusColor(requirementStatus.voiceOk === true) }
                        Text { text: "Voice model: " + settingsWindow.statusText(requirementStatus.voiceOk === true); color: "#9ab0ca"; font.pixelSize: 12 }
                    }

                    Text { text: "ffmpeg executable"; color: "#c9def3"; font.pixelSize: 13 }
                    TextField { id: ffmpegPathField; Layout.fillWidth: true; text: backend.ffmpegExecutable }
                    RowLayout {
                        Layout.fillWidth: true
                        Rectangle { width: 10; height: 10; radius: 5; color: settingsWindow.statusColor(requirementStatus.ffmpegOk === true) }
                        Text {
                            text: "FFmpeg: " + settingsWindow.statusText(requirementStatus.ffmpegOk === true)
                                + (requirementStatus.ffmpegVersion ? " (" + requirementStatus.ffmpegVersion + ")" : "")
                            color: "#9ab0ca"
                            font.pixelSize: 12
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Rectangle { width: 10; height: 10; radius: 5; color: settingsWindow.statusColor(requirementStatus.ffmpegLatestOk === true) }
                        Text {
                            text: "FFmpeg latest: " + (requirementStatus.ffmpegLatestOk === true ? "Up to date" : "Check update")
                                + (requirementStatus.ffmpegLatestTag ? " [" + requirementStatus.ffmpegLatestTag + "]" : "")
                            color: "#9ab0ca"
                            font.pixelSize: 12
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        ColumnLayout {
                            Layout.fillWidth: true
                            Text { text: "Voice speed"; color: "#c9def3"; font.pixelSize: 13 }
                            Slider { id: speedSlider; Layout.fillWidth: true; from: 0.7; to: 1.5; value: backend.voiceSpeed }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Text { text: "Voice pitch"; color: "#c9def3"; font.pixelSize: 13 }
                            Slider { id: pitchSlider; Layout.fillWidth: true; from: 0.8; to: 1.2; value: backend.voicePitch }
                        }
                    }
                }
            }

            Rectangle {
                width: parent.width
                radius: 30
                color: "#9208111d"
                border.width: 1
                border.color: "#1d2f4d"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 22
                    spacing: 14

                    Text {
                        text: "Presence"
                        color: "#eef7ff"
                        font.pixelSize: 22
                        font.weight: Font.Medium
                    }

                    Text {
                        text: "Sensitivity, overlay behavior, and local wake model settings."
                        color: "#8099b8"
                        font.pixelSize: 14
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        CheckBox {
                            id: aecCheck
                            text: "AEC enabled"
                            checked: backend.aecEnabled
                            onToggled: backend.saveAudioProcessing(checked, rnnoiseCheck.checked, vadSlider.value)
                        }
                        CheckBox {
                            id: rnnoiseCheck
                            text: "RNNoise enabled"
                            checked: backend.rnnoiseEnabled
                            onToggled: backend.saveAudioProcessing(aecCheck.checked, checked, vadSlider.value)
                        }
                    }

                    Text { text: "VAD sensitivity"; color: "#c9def3"; font.pixelSize: 13 }
                    Slider {
                        id: vadSlider
                        Layout.fillWidth: true
                        from: 0.05
                        to: 0.95
                        value: backend.vadSensitivity
                        onPressedChanged: if (!pressed) backend.saveAudioProcessing(aecCheck.checked, rnnoiseCheck.checked, value)
                    }

                    Text { text: "Mic sensitivity"; color: "#c9def3"; font.pixelSize: 13 }
                    Slider { id: micSlider; Layout.fillWidth: true; from: 0.01; to: 0.10; value: backend.micSensitivity }

                    Text { text: "Wake engine"; color: "#c9def3"; font.pixelSize: 13 }
                    ComboBox {
                        id: wakeEngineCombo
                        Layout.fillWidth: true
                        model: ["sherpa-onnx"]
                        currentIndex: Math.max(0, model.indexOf(backend.wakeEngineKind))
                        onActivated: backend.setWakeEngineKind(currentText)
                    }

                    Text {
                        text: "The app now uses sherpa-onnx for wake detection. Precise has been removed from the active setup flow."
                        color: "#9ab0ca"
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }

                    Text { text: "Input device (microphone)"; color: "#c9def3"; font.pixelSize: 13 }
                    ComboBox {
                        id: inputDeviceCombo
                        Layout.fillWidth: true
                        model: backend.audioInputDeviceNames
                        Component.onCompleted: {
                            const index = backend.audioInputDeviceIds.indexOf(backend.selectedAudioInputDeviceId)
                            currentIndex = index >= 0 ? index : 0
                        }
                    }

                    Text { text: "Output device (speaker/headset)"; color: "#c9def3"; font.pixelSize: 13 }
                    ComboBox {
                        id: outputDeviceCombo
                        Layout.fillWidth: true
                        model: backend.audioOutputDeviceNames
                        Component.onCompleted: {
                            const index = backend.audioOutputDeviceIds.indexOf(backend.selectedAudioOutputDeviceId)
                            currentIndex = index >= 0 ? index : 0
                        }
                    }

                    CheckBox { id: clickThroughCheck; text: "Click-through overlay"; checked: backend.clickThroughEnabled }

                    RowLayout {
                        Layout.fillWidth: true

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 50
                            radius: 25
                            color: "#15253c"
                            border.width: 1
                            border.color: "#2a4667"

                            Text {
                                anchors.centerIn: parent
                                text: "Refresh models"
                                color: "#e8f5ff"
                                font.pixelSize: 14
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    backend.refreshModels()
                                    backend.refreshAudioDevices()
                                    settingsWindow.refreshRequirementStatus()
                                    const inputIndex = backend.audioInputDeviceIds.indexOf(backend.selectedAudioInputDeviceId)
                                    inputDeviceCombo.currentIndex = inputIndex >= 0 ? inputIndex : 0
                                    const outputIndex = backend.audioOutputDeviceIds.indexOf(backend.selectedAudioOutputDeviceId)
                                    outputDeviceCombo.currentIndex = outputIndex >= 0 ? outputIndex : 0
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 50
                            radius: 25
                            color: "#15331f"
                            border.width: 1
                            border.color: "#2e7e4b"

                            Text {
                                anchors.centerIn: parent
                                text: "Auto Detect"
                                color: "#eafdf2"
                                font.pixelSize: 14
                                font.weight: Font.Medium
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    backend.autoDetectVoiceTools()
                                    settingsWindow.syncFieldsFromBackend()
                                    settingsWindow.refreshRequirementStatus()
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 50
                            radius: 25
                            color: "#183657"
                            border.width: 1
                            border.color: "#4d8fd1"

                            Text {
                                anchors.centerIn: parent
                                text: "Save changes"
                                color: "#f2fbff"
                                font.pixelSize: 14
                                font.weight: Font.Medium
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    backend.saveSettings(
                                        endpointField.text,
                                        modelCombo.currentText,
                                        modeCombo.currentIndex,
                                        autoRoutingCheck.checked,
                                        streamCheck.checked,
                                        timeoutSpin.value,
                                        aecCheck.checked,
                                        rnnoiseCheck.checked,
                                        vadSlider.value,
                                        wakeEngineCombo.currentText,
                                        whisperPathField.text,
                                        whisperModelPathField.text,
                                        "",
                                        "",
                                        backend.preciseTriggerThreshold,
                                        backend.preciseTriggerCooldownMs,
                                        ttsEngineCombo.currentText,
                                        piperPathField.text,
                                        voicePathField.text,
                                        ffmpegPathField.text,
                                        speedSlider.value,
                                        pitchSlider.value,
                                        micSlider.value,
                                        inputDeviceCombo.currentIndex >= 0 ? backend.audioInputDeviceIds[inputDeviceCombo.currentIndex] : "",
                                        outputDeviceCombo.currentIndex >= 0 ? backend.audioOutputDeviceIds[outputDeviceCombo.currentIndex] : "",
                                        clickThroughCheck.checked
                                    )
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                width: parent.width
                radius: 30
                color: "#9208111d"
                border.width: 1
                border.color: "#1d2f4d"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 22
                    spacing: 14

                    Text {
                        text: "Tools Status"
                        color: "#eef7ff"
                        font.pixelSize: 22
                        font.weight: Font.Medium
                    }

                    Text {
                        text: "Auto-detected runtimes, models, and downloadable assets."
                        color: "#8099b8"
                        font.pixelSize: 14
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Button { text: "Rescan"; onClicked: backend.rescanTools() }
                        Button {
                            text: "Auto Detect"
                            onClicked: {
                                backend.autoDetectVoiceTools()
                                settingsWindow.syncFieldsFromBackend()
                                settingsWindow.refreshRequirementStatus()
                            }
                        }
                        Button { text: "Install All"; onClicked: backend.installAllTools() }
                        Item { Layout.fillWidth: true }
                        Text { text: backend.toolsRoot; color: "#7f97b7"; font.pixelSize: 11; Layout.fillWidth: true; horizontalAlignment: Text.AlignRight }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: backend.toolInstallStatus.length > 0 ? backend.toolInstallStatus : "No active download."
                        color: "#9ab0ca"
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }

                    ProgressBar {
                        Layout.fillWidth: true
                        visible: backend.toolDownloadPercent >= 0
                        from: 0
                        to: 100
                        value: backend.toolDownloadPercent >= 0 ? backend.toolDownloadPercent : 0
                    }

                    Repeater {
                        model: backend.toolStatuses

                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            width: parent.width
                            radius: 18
                            color: "#102034"
                            border.width: 1
                            border.color: modelData.installed ? "#2e7e4b" : "#3b506b"
                            height: 58

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 14
                                spacing: 12

                                Rectangle { width: 10; height: 10; radius: 5; color: modelData.installed ? "#1ecb6b" : "#f04d5d" }
                                Text { text: modelData.name; color: "#eaf4ff"; font.pixelSize: 14; Layout.preferredWidth: 150 }
                                Text { text: modelData.category; color: "#8ea8c8"; font.pixelSize: 12; Layout.preferredWidth: 80 }
                                Text { text: modelData.version && modelData.version.length > 0 ? modelData.version : (modelData.installed ? "Installed" : "Missing"); color: "#c9def3"; font.pixelSize: 12; Layout.fillWidth: true }
                                Button {
                                    visible: modelData.downloadable === true && modelData.installed !== true
                                    text: "Download"
                                    onClicked: backend.downloadTool(modelData.name)
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                width: parent.width
                radius: 30
                color: "#9208111d"
                border.width: 1
                border.color: "#1d2f4d"
                visible: backend.tracePanelEnabled

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 22
                    spacing: 14

                    Text {
                        text: "Agent Trace"
                        color: "#eef7ff"
                        font.pixelSize: 22
                        font.weight: Font.Medium
                    }

                    Text {
                        text: "Tool calls, memory writes, skill installs, and model loop events."
                        color: "#8099b8"
                        font.pixelSize: 14
                    }

                    Repeater {
                        model: backend.agentTraceEntries

                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            width: parent.width
                            radius: 16
                            color: "#102034"
                            border.width: 1
                            border.color: modelData.success ? "#2e7e4b" : "#7e3b3b"
                            height: traceColumn.implicitHeight + 20

                            Column {
                                id: traceColumn
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 4

                                Text {
                                    text: modelData.timestamp + "  " + modelData.kind + "  " + modelData.title
                                    color: "#eaf4ff"
                                    font.pixelSize: 12
                                    wrapMode: Text.Wrap
                                }

                                Text {
                                    text: modelData.detail
                                    color: "#9ab0ca"
                                    font.pixelSize: 12
                                    wrapMode: Text.Wrap
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
