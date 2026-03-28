import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import "." as JarvisUi

Window {
    id: settingsWindow

    width: Math.min(Math.max(760, Screen.width * 0.5), 1120)
    height: Math.min(Math.max(760, Screen.height * 0.86), Screen.height)
    minimumWidth: 760
    minimumHeight: 720
    visible: false
    title: settingsVm.assistantName + " Control Surface"
    color: "#050912"

    property real dpiScale: Math.max(1.0, Screen.devicePixelRatio)
    property string braveValidationMessage: ""
    property bool braveValidationOk: false
    property bool openRouterSelected: providerCombo.currentText === "openrouter"

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

    function effectiveModelText() {
        const typed = (modelCombo.editText || "").trim()
        return typed.length > 0 ? typed : (modelCombo.currentText || "").trim()
    }

    function effectiveEndpointText() {
        return openRouterSelected ? "https://openrouter.ai/api" : endpointField.text
    }

    function refreshRequirementStatus() {
        requirementStatus = settingsVm.evaluateSetupRequirements(
            effectiveEndpointText(),
            effectiveModelText(),
            whisperPathField.text,
            whisperModelPathField.text,
            piperPathField.text,
            voicePathField.text,
            ffmpegPathField.text
        )
    }

    function syncFieldsFromBackend() {
        if (userNameField) {
            userNameField.text = settingsVm.userName
        }
        endpointField.text = settingsVm.lmStudioEndpoint
        const providerIndex = ["openai_compatible_local", "openrouter", "ollama"].indexOf(settingsVm.chatProviderKind)
        providerCombo.currentIndex = providerIndex >= 0 ? providerIndex : 0
        providerApiKeyField.text = settingsVm.chatProviderApiKey
        whisperPathField.text = settingsVm.whisperExecutable
        whisperModelPathField.text = settingsVm.whisperModelPath
        intentModelPathField.text = settingsVm.intentModelPath
        piperPathField.text = settingsVm.piperExecutable
        voicePathField.text = settingsVm.piperVoiceModel
        ffmpegPathField.text = settingsVm.ffmpegExecutable
        speedSlider.value = settingsVm.voiceSpeed
        pitchSlider.value = settingsVm.voicePitch
        vadSlider.value = settingsVm.vadSensitivity
        micSlider.value = settingsVm.micSensitivity
        aecCheck.checked = settingsVm.aecEnabled
        rnnoiseCheck.checked = settingsVm.rnnoiseEnabled
        autoRoutingCheck.checked = settingsVm.autoRoutingEnabled
        streamCheck.checked = settingsVm.streamingEnabled
        timeoutSpin.value = settingsVm.requestTimeoutMs
        visionEnabledCheck.checked = settingsVm.visionEnabled
        visionEndpointField.text = settingsVm.visionEndpoint
        visionTimeoutSpin.value = settingsVm.visionTimeoutMs
        visionStaleThresholdSpin.value = settingsVm.visionStaleThresholdMs
        visionContextAlwaysOnCheck.checked = settingsVm.visionContextAlwaysOn
        visionObjectsMinConfidenceSlider.value = settingsVm.visionObjectsMinConfidence
        visionGesturesMinConfidenceSlider.value = settingsVm.visionGesturesMinConfidence
        clickThroughCheck.checked = settingsVm.clickThroughEnabled
        agentEnabledCheck.checked = settingsVm.agentEnabled
        agentProviderField.text = settingsVm.agentProviderMode
        conversationTempSlider.value = settingsVm.conversationTemperature
        conversationTopPField.text = settingsVm.conversationTopP > 0 ? settingsVm.conversationTopP.toFixed(2) : ""
        toolTempSlider.value = settingsVm.toolUseTemperature
        providerTopKField.text = settingsVm.providerTopK > 0 ? String(settingsVm.providerTopK) : ""
        maxOutputSpin.value = settingsVm.maxOutputTokens
        memoryAutoWriteCheck.checked = settingsVm.memoryAutoWrite
        webProviderField.text = settingsVm.webSearchProvider
        braveApiKeyField.text = settingsVm.braveSearchApiKey
        braveValidationMessage = ""
        tracePanelCheck.checked = settingsVm.tracePanelEnabled

        const modelIndex = settingsVm.models.indexOf(settingsVm.selectedModel)
        if (modelIndex >= 0) {
            modelCombo.currentIndex = modelIndex
        } else {
            modelCombo.currentIndex = -1
            modelCombo.editText = settingsVm.selectedModel
        }

        const modeIndex = settingsVm.defaultReasoningMode
        modeCombo.currentIndex = modeIndex >= 0 ? modeIndex : 0

        const ttsIndex = ["piper"].indexOf(settingsVm.ttsEngineKind)
        ttsEngineCombo.currentIndex = ttsIndex >= 0 ? ttsIndex : 0

        const wakeIndex = ["sherpa-onnx"].indexOf(settingsVm.wakeEngineKind)
        wakeEngineCombo.currentIndex = wakeIndex >= 0 ? wakeIndex : 0

        const whisperModelIndex = settingsVm.whisperModelPresetIds.indexOf(settingsVm.selectedWhisperModelPresetId)
        whisperModelPresetCombo.currentIndex = whisperModelIndex >= 0 ? whisperModelIndex : 1

        const voicePresetIndex = settingsVm.voicePresetIds.indexOf(settingsVm.selectedVoicePresetId)
        voicePresetCombo.currentIndex = voicePresetIndex >= 0 ? voicePresetIndex : 0

        const intentModelIndex = settingsVm.intentModelPresetIds.indexOf(settingsVm.selectedIntentModelId)
        intentModelCombo.currentIndex = intentModelIndex >= 0 ? intentModelIndex : 0
    }

    onVisibleChanged: {
        if (!visible) {
            return
        }

        settingsVm.refreshAudioDevices()
        syncFieldsFromBackend()
        const inputIndex = settingsVm.audioInputDeviceIds.indexOf(settingsVm.selectedAudioInputDeviceId)
        inputDeviceCombo.currentIndex = inputIndex >= 0 ? inputIndex : 0
        const outputIndex = settingsVm.audioOutputDeviceIds.indexOf(settingsVm.selectedAudioOutputDeviceId)
        outputDeviceCombo.currentIndex = outputIndex >= 0 ? outputIndex : 0
        refreshRequirementStatus()
    }

    Connections {
        target: settingsVm

        function onSettingsChanged() {
            if (!settingsWindow.visible) {
                return
            }
            settingsWindow.syncFieldsFromBackend()
            settingsWindow.refreshRequirementStatus()
        }

        function onProfileChanged() {
            if (!settingsWindow.visible) {
                return
            }
            settingsWindow.syncFieldsFromBackend()
        }
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
                        text: settingsVm.assistantName
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
                                text: settingsVm.selectedModel.length > 0 ? settingsVm.selectedModel : "No model"
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
                                text: settingsVm.autoRoutingEnabled ? "Auto routing" : "Manual routing"
                                color: "#d9ecff"
                                font.pixelSize: 12
                            }
                        }

                        Button {
                            text: "Tools & Stores"
                            onClicked: settingsVm.openToolsHub()
                        }
                    }
                }
            }

            Rectangle {
                width: parent.width
                implicitHeight: identityColumn.implicitHeight + 44
                radius: 30
                color: "#9208111d"
                border.width: 1
                border.color: "#1d2f4d"

                ColumnLayout {
                    id: identityColumn
                    anchors.fill: parent
                    anchors.margins: 22
                    spacing: 14

                    Text {
                        text: "Identity"
                        color: "#eef7ff"
                        font.pixelSize: 22
                        font.weight: Font.Medium
                    }

                    Text {
                        text: "Username used everywhere in the app and spoken replies."
                        color: "#8099b8"
                        font.pixelSize: 14
                    }

                    Text { text: "Username"; color: "#c9def3"; font.pixelSize: 13 }
                    TextField {
                        id: userNameField
                        Layout.fillWidth: true
                        text: settingsVm.userName
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Item { Layout.fillWidth: true }
                        Button {
                            text: "Save username"
                            onClicked: settingsVm.setUserName(userNameField.text)
                        }
                    }
                }
            }

            Rectangle {
                width: parent.width
                implicitHeight: aiCoreColumn.implicitHeight + 44
                radius: 30
                color: "#9208111d"
                border.width: 1
                border.color: "#1d2f4d"

                ColumnLayout {
                    id: aiCoreColumn
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

                    Text { text: "Provider"; color: "#c9def3"; font.pixelSize: 13 }
                    ComboBox {
                        id: providerCombo
                        Layout.fillWidth: true
                        model: ["openai_compatible_local", "openrouter", "ollama"]
                    }

                    Text { text: "Provider API key (optional for local backends)"; color: "#c9def3"; font.pixelSize: 13 }
                    TextField {
                        id: providerApiKeyField
                        Layout.fillWidth: true
                        echoMode: TextInput.Password
                    }

                    Text {
                        visible: openRouterSelected
                        text: "OpenRouter endpoint (preview)"
                        color: "#c9def3"
                        font.pixelSize: 13
                    }
                    TextField {
                        visible: openRouterSelected
                        Layout.fillWidth: true
                        readOnly: true
                        text: "https://openrouter.ai/api"
                    }
                    Text { visible: !openRouterSelected; text: "Local AI backend endpoint"; color: "#c9def3"; font.pixelSize: 13 }
                    TextField { id: endpointField; visible: !openRouterSelected; Layout.fillWidth: true; text: settingsVm.lmStudioEndpoint }
                    RowLayout {
                        Layout.fillWidth: true
                        Rectangle { width: 10; height: 10; radius: 5; color: settingsWindow.statusColor(requirementStatus.endpointOk === true) }
                        Text { text: "Endpoint: " + settingsWindow.statusText(requirementStatus.endpointOk === true); color: "#9ab0ca"; font.pixelSize: 12 }
                    }

                    Text { text: "Selected model"; color: "#c9def3"; font.pixelSize: 13 }
                    ComboBox {
                        id: modelCombo
                        Layout.fillWidth: true
                        model: settingsVm.models
                        editable: true
                        currentIndex: -1
                        editText: settingsVm.selectedModel
                        Component.onCompleted: {
                            const index = settingsVm.models.indexOf(settingsVm.selectedModel)
                            if (index >= 0) {
                                currentIndex = index
                            } else {
                                currentIndex = -1
                                editText = settingsVm.selectedModel
                            }
                        }
                        onActivated: settingsVm.setSelectedModel(effectiveModelText())
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Rectangle { width: 10; height: 10; radius: 5; color: settingsWindow.statusColor(requirementStatus.modelOk === true) }
                        Text { text: "Model: " + settingsWindow.statusText(requirementStatus.modelOk === true); color: "#9ab0ca"; font.pixelSize: 12 }
                    }

                    Text { text: "Intent model"; color: "#c9def3"; font.pixelSize: 13 }
                    RowLayout {
                        Layout.fillWidth: true

                        ComboBox {
                            id: intentModelCombo
                            Layout.fillWidth: true
                            model: settingsVm.intentModelPresetNames
                            onActivated: {
                                settingsVm.setSelectedIntentModelId(settingsVm.intentModelPresetIds[currentIndex])
                                intentModelPathField.text = settingsVm.intentModelPath
                            }
                        }

                        Button {
                            text: "Download"
                            visible: settingsVm.supportsAutoToolInstall
                            onClicked: settingsVm.downloadModel(settingsVm.intentModelPresetIds[intentModelCombo.currentIndex])
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "Recommended: " + settingsVm.recommendedIntentModelLabel + "\n" + settingsVm.intentHardwareSummary
                        color: "#9ab0ca"
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Rectangle { width: 10; height: 10; radius: 5; color: settingsWindow.statusColor(requirementStatus.intentModelOk === true) }
                        Text {
                            text: "Intent model: " + settingsWindow.statusText(requirementStatus.intentModelOk === true)
                            color: "#9ab0ca"
                            font.pixelSize: 12
                        }
                    }

                    Text { text: "Intent model path"; color: "#c9def3"; font.pixelSize: 13 }
                    RowLayout {
                        Layout.fillWidth: true
                        TextField { id: intentModelPathField; Layout.fillWidth: true; text: settingsVm.intentModelPath; readOnly: true }
                        Button {
                            text: "Open Dir"
                            enabled: intentModelPathField.text.length > 0
                            onClicked: settingsVm.openContainingDirectory(intentModelPathField.text)
                        }
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
                                currentIndex: settingsVm.defaultReasoningMode
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
                                value: settingsVm.requestTimeoutMs
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        CheckBox { id: autoRoutingCheck; text: "Enable auto routing"; checked: settingsVm.autoRoutingEnabled }
                        CheckBox { id: streamCheck; text: "Enable streaming"; checked: settingsVm.streamingEnabled }
                    }
                }
            }

            Rectangle {
                width: parent.width
                implicitHeight: agentRuntimeColumn.implicitHeight + 44
                radius: 30
                color: "#9208111d"
                border.width: 1
                border.color: "#1d2f4d"

                ColumnLayout {
                    id: agentRuntimeColumn
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
                        text: settingsVm.agentStatus
                        color: settingsVm.agentAvailable ? "#87d7a2" : "#d8a17a"
                        font.pixelSize: 13
                        wrapMode: Text.Wrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        CheckBox { id: agentEnabledCheck; text: "Enable agent mode"; checked: settingsVm.agentEnabled }
                        CheckBox { id: memoryAutoWriteCheck; text: "Auto-write memory"; checked: settingsVm.memoryAutoWrite }
                        CheckBox { id: tracePanelCheck; text: "Trace panel"; checked: settingsVm.tracePanelEnabled }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            Text { text: "Provider mode"; color: "#c9def3"; font.pixelSize: 13 }
                            TextField { id: agentProviderField; Layout.fillWidth: true; text: settingsVm.agentProviderMode }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Text { text: "Web provider"; color: "#c9def3"; font.pixelSize: 13 }
                            TextField { id: webProviderField; Layout.fillWidth: true; text: settingsVm.webSearchProvider }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Text { text: "Brave Search API key (X-Subscription-Token)"; color: "#c9def3"; font.pixelSize: 13 }
                        TextField {
                            id: braveApiKeyField
                            Layout.fillWidth: true
                            text: settingsVm.braveSearchApiKey
                            placeholderText: "Optional if BRAVE_SEARCH_API_KEY env var is set"
                            echoMode: TextInput.Password
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Button {
                            text: "Validate Brave Key"
                            onClicked: {
                                const result = settingsVm.validateBraveSearchConnection(braveApiKeyField.text)
                                braveValidationOk = !!result.ok
                                braveValidationMessage = result.message ? String(result.message) : (braveValidationOk ? "Connected." : "Validation failed.")
                            }
                        }

                        Rectangle {
                            width: 10
                            height: 10
                            radius: 5
                            color: braveValidationMessage.length === 0 ? "#6c7f99" : (braveValidationOk ? "#1ecb6b" : "#f04d5d")
                        }

                        Text {
                            Layout.fillWidth: true
                            text: braveValidationMessage.length === 0
                                  ? "Run validation to confirm Brave key and connectivity."
                                  : braveValidationMessage
                            color: braveValidationMessage.length === 0 ? "#9ab0ca" : (braveValidationOk ? "#87d7a2" : "#f3a1a1")
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            Text { text: "Conversation temperature"; color: "#c9def3"; font.pixelSize: 13 }
                            Slider { id: conversationTempSlider; Layout.fillWidth: true; from: 0.0; to: 1.5; value: settingsVm.conversationTemperature }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Text { text: "Tool temperature"; color: "#c9def3"; font.pixelSize: 13 }
                            Slider { id: toolTempSlider; Layout.fillWidth: true; from: 0.0; to: 1.0; value: settingsVm.toolUseTemperature }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            Text { text: "Top P"; color: "#c9def3"; font.pixelSize: 13 }
                            TextField { id: conversationTopPField; Layout.fillWidth: true; text: settingsVm.conversationTopP > 0 ? settingsVm.conversationTopP.toFixed(2) : "" }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Text { text: "Provider Top K"; color: "#c9def3"; font.pixelSize: 13 }
                            TextField { id: providerTopKField; Layout.fillWidth: true; text: settingsVm.providerTopK > 0 ? String(settingsVm.providerTopK) : "" }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Text { text: "Max output tokens"; color: "#c9def3"; font.pixelSize: 13 }
                            SpinBox { id: maxOutputSpin; Layout.fillWidth: true; from: 64; to: 8192; value: settingsVm.maxOutputTokens }
                        }
                    }

                    Button {
                        text: "Save agent settings"
                        onClicked: settingsVm.saveAgentSettings(
                            agentEnabledCheck.checked,
                            agentProviderField.text,
                            conversationTempSlider.value,
                            parseFloat(conversationTopPField.text || "0"),
                            toolTempSlider.value,
                            parseInt(providerTopKField.text || "0"),
                            maxOutputSpin.value,
                            memoryAutoWriteCheck.checked,
                            webProviderField.text,
                            braveApiKeyField.text,
                            tracePanelCheck.checked
                        )
                    }
                }
            }

            Rectangle {
                width: parent.width
                implicitHeight: voicePipelineColumn.implicitHeight + 44
                radius: 30
                color: "#9208111d"
                border.width: 1
                border.color: "#1d2f4d"

                ColumnLayout {
                    id: voicePipelineColumn
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
                        text: settingsVm.supportsAutoToolInstall
                              ? "Local binaries, voice model, and speech tuning."
                              : "Local binaries, voice model, and speech tuning. Linux uses manual tool and model selection."
                        color: "#8099b8"
                        font.pixelSize: 14
                    }

                    Text { text: "TTS engine"; color: "#c9def3"; font.pixelSize: 13 }
                    ComboBox {
                        id: ttsEngineCombo
                        Layout.fillWidth: true
                        model: ["piper"]
                        currentIndex: Math.max(0, model.indexOf(settingsVm.ttsEngineKind))
                        onActivated: settingsVm.setTtsEngineKind(currentText)
                    }

                    Text { text: "whisper.cpp executable"; color: "#c9def3"; font.pixelSize: 13 }
                    TextField { id: whisperPathField; Layout.fillWidth: true; text: settingsVm.whisperExecutable }
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
                    TextField { id: whisperModelPathField; Layout.fillWidth: true; text: settingsVm.whisperModelPath }
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
                            model: settingsVm.whisperModelPresetNames
                            Component.onCompleted: {
                                const index = settingsVm.whisperModelPresetIds.indexOf(settingsVm.selectedWhisperModelPresetId)
                                currentIndex = index >= 0 ? index : 1
                            }
                        }
                        Button {
                            text: "Download"
                            visible: settingsVm.supportsAutoToolInstall
                            onClicked: {
                                settingsVm.downloadWhisperModel(settingsVm.whisperModelPresetIds[whisperModelPresetCombo.currentIndex])
                                settingsWindow.syncFieldsFromBackend()
                                settingsWindow.refreshRequirementStatus()
                            }
                        }
                        Button {
                            text: "Auto Detect"
                            onClicked: {
                                settingsVm.autoDetectVoiceTools()
                                settingsWindow.syncFieldsFromBackend()
                                settingsWindow.refreshRequirementStatus()
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: settingsVm.supportsAutoToolInstall
                        Rectangle { width: 10; height: 10; radius: 5; color: settingsWindow.statusColor(requirementStatus.whisperLatestOk === true) }
                        Text {
                            text: "Whisper latest: " + (requirementStatus.whisperLatestOk === true ? "Up to date" : "Check update")
                                + (requirementStatus.whisperLatestTag ? " [" + requirementStatus.whisperLatestTag + "]" : "")
                            color: "#9ab0ca"
                            font.pixelSize: 12
                        }
                    }

                    Text { text: "Piper executable"; color: "#c9def3"; font.pixelSize: 13 }
                    TextField { id: piperPathField; Layout.fillWidth: true; text: settingsVm.piperExecutable }
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
                        visible: settingsVm.supportsAutoToolInstall
                        Rectangle { width: 10; height: 10; radius: 5; color: settingsWindow.statusColor(requirementStatus.piperLatestOk === true) }
                        Text {
                            text: "Piper latest: " + (requirementStatus.piperLatestOk === true ? "Up to date" : "Check update")
                                + (requirementStatus.piperLatestTag ? " [" + requirementStatus.piperLatestTag + "]" : "")
                            color: "#9ab0ca"
                            font.pixelSize: 12
                        }
                    }

                    Text { text: "Piper voice model"; color: "#c9def3"; font.pixelSize: 13 }
                    TextField { id: voicePathField; Layout.fillWidth: true; text: settingsVm.piperVoiceModel }
                    RowLayout {
                        Layout.fillWidth: true
                        Rectangle { width: 10; height: 10; radius: 5; color: settingsWindow.statusColor(requirementStatus.voiceOk === true) }
                        Text { text: "Voice model: " + settingsWindow.statusText(requirementStatus.voiceOk === true); color: "#9ab0ca"; font.pixelSize: 12 }
                    }

                    Text { text: "Official Piper voice"; color: "#c9def3"; font.pixelSize: 13 }
                    RowLayout {
                        Layout.fillWidth: true

                        ComboBox {
                            id: voicePresetCombo
                            Layout.fillWidth: true
                            model: settingsVm.voicePresetNames
                            onActivated: {
                                if (currentIndex >= 0 && currentIndex < settingsVm.voicePresetIds.length) {
                                    settingsVm.setSelectedVoicePresetId(settingsVm.voicePresetIds[currentIndex])
                                    settingsWindow.syncFieldsFromBackend()
                                    settingsWindow.refreshRequirementStatus()
                                }
                            }
                        }

                        Button {
                            text: "Download"
                            visible: settingsVm.supportsAutoToolInstall
                            onClicked: {
                                if (voicePresetCombo.currentIndex >= 0 && voicePresetCombo.currentIndex < settingsVm.voicePresetIds.length) {
                                    const voiceId = settingsVm.voicePresetIds[voicePresetCombo.currentIndex]
                                    settingsVm.setSelectedVoicePresetId(voiceId)
                                    settingsVm.downloadVoiceModel(voiceId)
                                    settingsWindow.syncFieldsFromBackend()
                                    settingsWindow.refreshRequirementStatus()
                                }
                            }
                        }
                    }

                    Text {
                        text: settingsVm.supportsAutoToolInstall
                              ? "Select from official Piper voices, then download to switch instantly."
                              : "Select any compatible Piper voice model already installed on this system."
                        color: "#9ab0ca"
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }

                    Text { text: "ffmpeg executable"; color: "#c9def3"; font.pixelSize: 13 }
                    TextField { id: ffmpegPathField; Layout.fillWidth: true; text: settingsVm.ffmpegExecutable }
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
                        visible: settingsVm.supportsAutoToolInstall
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
                            Slider { id: speedSlider; Layout.fillWidth: true; from: 0.7; to: 1.5; value: settingsVm.voiceSpeed }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Text { text: "Voice pitch"; color: "#c9def3"; font.pixelSize: 13 }
                            Slider { id: pitchSlider; Layout.fillWidth: true; from: 0.8; to: 1.2; value: settingsVm.voicePitch }
                        }
                    }
                }
            }

            Rectangle {
                width: parent.width
                implicitHeight: presenceColumn.implicitHeight + 44
                radius: 30
                color: "#9208111d"
                border.width: 1
                border.color: "#1d2f4d"

                ColumnLayout {
                    id: presenceColumn
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
                            checked: settingsVm.aecEnabled
                            onToggled: settingsVm.saveAudioProcessing(checked, rnnoiseCheck.checked, vadSlider.value)
                        }
                        CheckBox {
                            id: rnnoiseCheck
                            text: "RNNoise enabled"
                            checked: settingsVm.rnnoiseEnabled
                            onToggled: settingsVm.saveAudioProcessing(aecCheck.checked, checked, vadSlider.value)
                        }
                    }

                    Text { text: "VAD sensitivity"; color: "#c9def3"; font.pixelSize: 13 }
                    Slider {
                        id: vadSlider
                        Layout.fillWidth: true
                        from: 0.05
                        to: 0.95
                        value: settingsVm.vadSensitivity
                        onPressedChanged: if (!pressed) settingsVm.saveAudioProcessing(aecCheck.checked, rnnoiseCheck.checked, value)
                    }

                    Text { text: "Mic sensitivity"; color: "#c9def3"; font.pixelSize: 13 }
                    Slider { id: micSlider; Layout.fillWidth: true; from: 0.01; to: 0.10; value: settingsVm.micSensitivity }

                    Text { text: "Wake engine"; color: "#c9def3"; font.pixelSize: 13 }
                    ComboBox {
                        id: wakeEngineCombo
                        Layout.fillWidth: true
                        model: ["sherpa-onnx"]
                        currentIndex: Math.max(0, model.indexOf(settingsVm.wakeEngineKind))
                        onActivated: settingsVm.setWakeEngineKind(currentText)
                    }

                    Text {
                        text: settingsVm.supportsAutoToolInstall
                              ? "The app uses sherpa-onnx only for wake detection."
                              : "The app uses sherpa-onnx only for wake detection. On Linux, configure wake assets manually if you want wake-word support."
                        color: "#9ab0ca"
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }

                    Text { text: "Input device (microphone)"; color: "#c9def3"; font.pixelSize: 13 }
                    ComboBox {
                        id: inputDeviceCombo
                        Layout.fillWidth: true
                        model: settingsVm.audioInputDeviceNames
                        Component.onCompleted: {
                            const index = settingsVm.audioInputDeviceIds.indexOf(settingsVm.selectedAudioInputDeviceId)
                            currentIndex = index >= 0 ? index : 0
                        }
                    }

                    Text { text: "Output device (speaker/headset)"; color: "#c9def3"; font.pixelSize: 13 }
                    ComboBox {
                        id: outputDeviceCombo
                        Layout.fillWidth: true
                        model: settingsVm.audioOutputDeviceNames
                        Component.onCompleted: {
                            const index = settingsVm.audioOutputDeviceIds.indexOf(settingsVm.selectedAudioOutputDeviceId)
                            currentIndex = index >= 0 ? index : 0
                        }
                    }

                    CheckBox { id: clickThroughCheck; text: "Click-through overlay"; checked: settingsVm.clickThroughEnabled }

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
                                    settingsVm.refreshModels()
                                    settingsVm.refreshAudioDevices()
                                    settingsWindow.refreshRequirementStatus()
                                    const inputIndex = settingsVm.audioInputDeviceIds.indexOf(settingsVm.selectedAudioInputDeviceId)
                                    inputDeviceCombo.currentIndex = inputIndex >= 0 ? inputIndex : 0
                                    const outputIndex = settingsVm.audioOutputDeviceIds.indexOf(settingsVm.selectedAudioOutputDeviceId)
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
                                    settingsVm.autoDetectVoiceTools()
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
                                    settingsVm.saveSettings(
                                        effectiveEndpointText(),
                                        providerCombo.currentText,
                                        providerApiKeyField.text,
                                        effectiveModelText(),
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
                                        settingsVm.wakeTriggerThreshold,
                                        settingsVm.wakeTriggerCooldownMs,
                                        ttsEngineCombo.currentText,
                                        piperPathField.text,
                                        voicePathField.text,
                                        ffmpegPathField.text,
                                        speedSlider.value,
                                        pitchSlider.value,
                                        micSlider.value,
                                        inputDeviceCombo.currentIndex >= 0 ? settingsVm.audioInputDeviceIds[inputDeviceCombo.currentIndex] : "",
                                        outputDeviceCombo.currentIndex >= 0 ? settingsVm.audioOutputDeviceIds[outputDeviceCombo.currentIndex] : "",
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
                implicitHeight: visionColumn.implicitHeight + 44
                radius: 30
                color: "#9208111d"
                border.width: 1
                border.color: "#1d2f4d"

                ColumnLayout {
                    id: visionColumn
                    anchors.fill: parent
                    anchors.margins: 22
                    spacing: 14

                    Text {
                        text: "Distributed Vision"
                        color: "#eef7ff"
                        font.pixelSize: 22
                        font.weight: Font.Medium
                    }

                    Text {
                        text: "Optional semantic vision ingest. The main PC hosts a WebSocket endpoint and the laptop vision node connects to it."
                        color: "#8099b8"
                        font.pixelSize: 14
                        wrapMode: Text.Wrap
                    }

                    CheckBox {
                        id: visionEnabledCheck
                        text: "Enable vision ingest"
                        checked: settingsVm.visionEnabled
                    }

                    CheckBox {
                        id: visionContextAlwaysOnCheck
                        text: "Always allow vision context in prompts"
                        checked: settingsVm.visionContextAlwaysOn
                    }

                    Text {
                        text: "WebSocket endpoint"
                        color: "#c9def3"
                        font.pixelSize: 13
                    }
                    TextField {
                        id: visionEndpointField
                        Layout.fillWidth: true
                        text: settingsVm.visionEndpoint
                        placeholderText: "ws://0.0.0.0:8765/vision"
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        ColumnLayout {
                            Layout.fillWidth: true
                            Text { text: "Transport timeout"; color: "#c9def3"; font.pixelSize: 13 }
                            SpinBox {
                                id: visionTimeoutSpin
                                Layout.fillWidth: true
                                from: 1000
                                to: 60000
                                stepSize: 500
                                value: settingsVm.visionTimeoutMs
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Text { text: "Stale drop threshold"; color: "#c9def3"; font.pixelSize: 13 }
                            SpinBox {
                                id: visionStaleThresholdSpin
                                Layout.fillWidth: true
                                from: 100
                                to: 10000
                                stepSize: 100
                                value: settingsVm.visionStaleThresholdMs
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        ColumnLayout {
                            Layout.fillWidth: true
                            Text { text: "Objects min confidence"; color: "#c9def3"; font.pixelSize: 13 }
                            Slider {
                                id: visionObjectsMinConfidenceSlider
                                Layout.fillWidth: true
                                from: 0.10
                                to: 0.95
                                value: settingsVm.visionObjectsMinConfidence
                            }
                            Text {
                                text: visionObjectsMinConfidenceSlider.value.toFixed(2)
                                color: "#9ab0ca"
                                font.pixelSize: 12
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Text { text: "Gestures min confidence"; color: "#c9def3"; font.pixelSize: 13 }
                            Slider {
                                id: visionGesturesMinConfidenceSlider
                                Layout.fillWidth: true
                                from: 0.10
                                to: 0.95
                                value: settingsVm.visionGesturesMinConfidence
                            }
                            Text {
                                text: visionGesturesMinConfidenceSlider.value.toFixed(2)
                                color: "#9ab0ca"
                                font.pixelSize: 12
                            }
                        }
                    }

                    Text {
                        text: "Vision snapshots older than the stale threshold are dropped before they reach the assistant. When disabled, the voice pipeline and assistant behavior remain unchanged."
                        color: "#9ab0ca"
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Item { Layout.fillWidth: true }
                        Button {
                            text: "Save vision settings"
                            onClicked: {
                                settingsVm.saveVisionSettings(
                                    visionEnabledCheck.checked,
                                    visionEndpointField.text,
                                    visionTimeoutSpin.value,
                                    visionStaleThresholdSpin.value,
                                    visionContextAlwaysOnCheck.checked,
                                    visionObjectsMinConfidenceSlider.value,
                                    visionGesturesMinConfidenceSlider.value
                                )
                            }
                        }
                    }
                }
            }

            Rectangle {
                width: parent.width
                implicitHeight: toolsStatusColumn.implicitHeight + 44
                radius: 30
                color: "#9208111d"
                border.width: 1
                border.color: "#1d2f4d"

                ColumnLayout {
                    id: toolsStatusColumn
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
                        text: settingsVm.supportsAutoToolInstall
                              ? "Auto-detected runtimes, models, and downloadable assets."
                              : "Detected runtimes and models. Automatic downloads stay Windows-only in this release."
                        color: "#8099b8"
                        font.pixelSize: 14
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Button { text: "Rescan"; onClicked: settingsVm.rescanTools() }
                        Button {
                            text: "Auto Detect"
                            onClicked: {
                                settingsVm.autoDetectVoiceTools()
                                settingsWindow.syncFieldsFromBackend()
                                settingsWindow.refreshRequirementStatus()
                            }
                        }
                        Button {
                            text: "Install All"
                            visible: settingsVm.supportsAutoToolInstall
                            onClicked: settingsVm.installAllTools()
                        }
                        Item { Layout.fillWidth: true }
                        Text { text: settingsVm.toolsRoot; color: "#7f97b7"; font.pixelSize: 11; Layout.fillWidth: true; horizontalAlignment: Text.AlignRight }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: settingsVm.toolInstallStatus.length > 0 ? settingsVm.toolInstallStatus : "No active download."
                        color: "#9ab0ca"
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }

                    ProgressBar {
                        Layout.fillWidth: true
                        visible: settingsVm.supportsAutoToolInstall && settingsVm.toolDownloadPercent >= 0
                        from: 0
                        to: 100
                        value: settingsVm.toolDownloadPercent >= 0 ? settingsVm.toolDownloadPercent : 0
                    }

                    Repeater {
                        model: settingsVm.toolStatuses

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
                                    onClicked: settingsVm.downloadTool(modelData.name)
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                width: parent.width
                implicitHeight: agentTraceColumn.implicitHeight + 44
                radius: 30
                color: "#9208111d"
                border.width: 1
                border.color: "#1d2f4d"
                visible: settingsVm.tracePanelEnabled

                ColumnLayout {
                    id: agentTraceColumn
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
                        model: settingsVm.agentTraceEntries

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
