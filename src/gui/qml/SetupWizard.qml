import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import "." as JarvisUi

Window {
    id: wizard

    width: Math.min(Math.max(920, Screen.width * 0.72), 1400)
    height: Math.min(Math.max(760, Screen.height * 0.82), Screen.height)
    minimumWidth: 900
    minimumHeight: 720
    visible: false
    title: settingsVm.assistantName + " Setup"
    color: "#04070d"

    property real dpiScale: Math.max(1.0, Screen.devicePixelRatio)

    property int stepIndex: 0
    readonly property var providerOptions: [
        { label: "LM Studio", value: "openai_compatible_local" },
        { label: "OpenRouter", value: "openrouter" },
        { label: "Ollama", value: "ollama" }
    ]
    property bool openRouterSelected: providerCombo.currentValue === "openrouter"
    readonly property string iconRoot: "qrc:/qt/qml/VAXIL/gui/assets/Icons/"

    function stepIconSource(index) {
        switch (index) {
        case 0: return iconRoot + "icons8-member-50.png"
        case 1: return iconRoot + "icons8-processor-50.png"
        case 2: return iconRoot + "icons8-microphone-50.png"
        case 3: return iconRoot + "icons8-radio-tower-50.png"
        default: return iconRoot + "icons8-approval-50.png"
        }
    }

    function effectiveModelText() {
        const typed = (modelCombo.editText || "").trim()
        return typed.length > 0 ? typed : (modelCombo.currentText || "").trim()
    }

    function autoApplyProviderSelection() {
        settingsVm.saveProviderSettings(
            providerCombo.currentValue,
            providerApiKeyField.text,
            wizard.openRouterSelected ? "https://openrouter.ai/api" : endpointField.text
        )
    }

    function syncVoiceFieldsFromBackend() {
        const providerKind = settingsVm.chatProviderKind === "lmstudio"
            ? "openai_compatible_local"
            : settingsVm.chatProviderKind
        const providerValues = providerOptions.map(function(option) { return option.value })
        const providerIndex = providerValues.indexOf(providerKind)
        providerCombo.currentIndex = providerIndex >= 0 ? providerIndex : 0
        providerApiKeyField.text = settingsVm.chatProviderApiKey
        whisperPathField.text = settingsVm.whisperExecutable
        whisperModelPathField.text = settingsVm.whisperModelPath
        intentModelPathField.text = settingsVm.intentModelPath
        piperPathField.text = settingsVm.piperExecutable
        voicePathField.text = settingsVm.piperVoiceModel
        ffmpegPathField.text = settingsVm.ffmpegExecutable
        endpointField.text = settingsVm.lmStudioEndpoint

        const modelIndex = settingsVm.models.indexOf(settingsVm.selectedModel)
        if (modelIndex >= 0) {
            modelCombo.currentIndex = modelIndex
        } else {
            modelCombo.currentIndex = -1
            modelCombo.editText = settingsVm.selectedModel
        }

        const voiceIndex = settingsVm.voicePresetIds.indexOf(settingsVm.selectedVoicePresetId)
        voicePresetCombo.currentIndex = voiceIndex >= 0 ? voiceIndex : 0

        const whisperModelIndex = settingsVm.whisperModelPresetIds.indexOf(settingsVm.selectedWhisperModelPresetId)
        whisperModelPresetCombo.currentIndex = whisperModelIndex >= 0 ? whisperModelIndex : 1

        const intentModelIndex = settingsVm.intentModelPresetIds.indexOf(settingsVm.selectedIntentModelId)
        intentModelCombo.currentIndex = intentModelIndex >= 0 ? intentModelIndex : 0

        const inputIndex = settingsVm.audioInputDeviceIds.indexOf(settingsVm.selectedAudioInputDeviceId)
        inputDeviceCombo.currentIndex = inputIndex >= 0 ? inputIndex : 0

        const outputIndex = settingsVm.audioOutputDeviceIds.indexOf(settingsVm.selectedAudioOutputDeviceId)
        outputDeviceCombo.currentIndex = outputIndex >= 0 ? outputIndex : 0
        if (uiModeCombo) {
            uiModeCombo.currentIndex = settingsVm.uiMode === "overlay" ? 1 : 0
        }
    }

    onClosing: function(close) {
        close.accepted = false
        hide()
    }

    onVisibleChanged: {
        if (visible) {
            settingsVm.refreshAudioDevices()
            settingsVm.refreshModels()
            settingsVm.rescanTools()
            syncVoiceFieldsFromBackend()
        }
    }

    Connections {
        target: settingsVm
        function onModelsChanged() {
            wizard.syncVoiceFieldsFromBackend()
        }
        function onSettingsChanged() {
            if (wizard.visible) {
                wizard.syncVoiceFieldsFromBackend()
            }
        }
    }

    JarvisUi.AnimationController {
        id: setupMotion
        stateName: stepIndex === 0 ? "IDLE"
            : stepIndex === 1 ? "PROCESSING"
            : stepIndex === 2 ? "LISTENING"
            : stepIndex === 3 ? "PROCESSING"
            : "SPEAKING"
        inputLevel: 0.05
        overlayVisible: wizard.visible
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#17212d" }
            GradientStop { position: 0.35; color: "#0e141d" }
            GradientStop { position: 0.72; color: "#070b11" }
            GradientStop { position: 1.0; color: "#04070c" }
        }
    }

    Rectangle {
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: -160
        width: 520
        height: 520
        radius: 260
        color: "#4cddf4ff"
        opacity: 0.10
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 20

        JarvisUi.VisionGlassPanel {
            Layout.preferredWidth: 300
            Layout.fillHeight: true
            radius: 34
            panelColor: "#161a2124"
            innerColor: "#1d212a31"
            outlineColor: "#22ffffff"
            highlightColor: "#16ffffff"
            shadowOpacity: 0.26

            Column {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 18

                JarvisUi.OrbRenderer {
                    width: 228
                    height: 228
                    anchors.horizontalCenter: parent.horizontalCenter
                    stateName: setupMotion.stateName
                    time: setupMotion.time
                    audioLevel: setupMotion.inputBoost
                    speakingLevel: setupMotion.speakingSignal
                    distortion: setupMotion.distortion
                    glow: setupMotion.glow
                    orbScale: setupMotion.orbScale
                    orbitalRotation: setupMotion.orbitalRotation
                }

                Row {
                    width: parent.width
                    spacing: 12

                    JarvisUi.VisionGlyph {
                        anchors.verticalCenter: parent.verticalCenter
                        iconSize: 20
                        source: wizard.iconRoot + "icons8-approval-50.png"
                    }

                    Text {
                        width: parent.width - 52
                        text: settingsVm.assistantName + " Setup"
                        color: "#f3f7ff"
                        font.pixelSize: 30
                        font.weight: Font.Medium
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                Text {
                    width: parent.width
                    text: "Prepare the local AI, voice stack, wake phrase flow, and overlay defaults before activation."
                    color: "#ced9e8"
                    font.pixelSize: 14
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                }

                Button {
                    width: parent.width
                    text: "Open Tools & Stores"
                    onClicked: settingsVm.openToolsHub()
                }

                Column {
                    width: parent.width
                    spacing: 10

                    Repeater {
                        model: 5

                        delegate: JarvisUi.VisionGlassPanel {
                            required property int index

                            width: parent.width
                            height: 46
                            radius: 23
                            panelColor: wizard.stepIndex === index ? "#1a24312e" : "#151a2125"
                            innerColor: wizard.stepIndex === index ? "#212d3b38" : "#1c212931"
                            outlineColor: wizard.stepIndex === index ? "#28ffffff" : "#18ffffff"
                            highlightColor: wizard.stepIndex === index ? "#16ffffff" : "#0cffffff"
                            shadowOpacity: 0.14

                            Row {
                                anchors.centerIn: parent
                                spacing: 8

                                Image {
                                    width: 16
                                    height: 16
                                    source: wizard.stepIconSource(index)
                                    fillMode: Image.PreserveAspectFit
                                    smooth: true
                                    mipmap: true
                                    opacity: 0.96
                                }

                                Text {
                                    text: index === 0 ? "Profile"
                                        : index === 1 ? "AI Core"
                                        : index === 2 ? "Voice"
                                        : index === 3 ? "Wake Word"
                                        : "Final Check"
                                    color: wizard.stepIndex === index ? "#f4f7ff" : "#e6eef9"
                                    font.pixelSize: 14
                                }
                            }
                        }
                    }
                }
            }
        }

        JarvisUi.VisionGlassPanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 34
            panelColor: "#16192022"
            innerColor: "#1d212b30"
            outlineColor: "#22ffffff"
            highlightColor: "#18ffffff"
            shadowOpacity: 0.26

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 28
                spacing: 18

                RowLayout {
                    spacing: 12

                    JarvisUi.VisionGlyph {
                        iconSize: 18
                        source: wizard.stepIconSource(wizard.stepIndex)
                    }

                    Text {
                        text: wizard.stepIndex === 0 ? "Identity"
                            : wizard.stepIndex === 1 ? "AI Core"
                            : wizard.stepIndex === 2 ? "Voice Pipeline"
                            : wizard.stepIndex === 3 ? "Wake Phrase"
                            : "Final Validation"
                        color: "#f4f7ff"
                        font.pixelSize: 28
                        font.weight: Font.Medium
                    }
                }

                Text {
                    text: wizard.stepIndex === 0 ? "Define how the assistant should address you."
                        : wizard.stepIndex === 1 ? "Choose provider settings and set the active model."
                        : wizard.stepIndex === 2 ? "Connect whisper.cpp, Piper, FFmpeg, and the local speech models you want."
                        : wizard.stepIndex === 3 ? "Review the local sherpa wake engine and microphone behavior."
                        : "Run final checks, trigger tests, and confirm the real startup behavior."
                    color: "#c9d5e5"
                    font.pixelSize: 14
                    wrapMode: Text.Wrap
                }

                JarvisUi.VisionGlassPanel {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 10
                    radius: 5
                    panelColor: "#171c2228"
                    innerColor: "#1d222930"
                    outlineColor: "#18ffffff"
                    highlightColor: "#08ffffff"
                    shadowOpacity: 0.08

                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: parent.width * Math.max(0, Math.min(1, (wizard.stepIndex + 1) / 5.0))
                        radius: 5
                        color: "#d8e7ff"
                        opacity: 0.8
                    }
                }

                ScrollView {
                    id: stepScroll
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    StackLayout {
                        id: stepStack
                        width: stepScroll.availableWidth
                        implicitHeight: wizard.stepIndex === 0 ? profileStep.implicitHeight
                            : wizard.stepIndex === 1 ? aiCoreStep.implicitHeight
                            : wizard.stepIndex === 2 ? voiceStep.implicitHeight
                            : wizard.stepIndex === 3 ? wakeStep.implicitHeight
                            : finalStep.implicitHeight
                        height: implicitHeight
                        currentIndex: wizard.stepIndex

                        ColumnLayout {
                        id: profileStep
                        width: stepStack.width
                        spacing: 14

                        Text { text: "Username"; color: "#d0e3f5"; font.pixelSize: 13 }
                        TextField {
                            id: userNameField
                            Layout.fillWidth: true
                            text: settingsVm.userName
                            placeholderText: "Name used across interface and voice replies"
                        }
                        }

                        ColumnLayout {
                        id: aiCoreStep
                        width: stepStack.width
                        spacing: 14

                        Text { text: "Provider"; color: "#d0e3f5"; font.pixelSize: 13 }
                        ComboBox {
                            id: providerCombo
                            Layout.fillWidth: true
                            model: providerOptions
                            textRole: "label"
                            valueRole: "value"
                            onActivated: wizard.autoApplyProviderSelection()
                        }

                        Text { text: "Provider API key"; color: "#d0e3f5"; font.pixelSize: 13 }
                        TextField {
                            id: providerApiKeyField
                            Layout.fillWidth: true
                            echoMode: TextInput.Password
                            placeholderText: "Required for OpenRouter"
                            onEditingFinished: wizard.autoApplyProviderSelection()
                        }

                        Text {
                            visible: wizard.openRouterSelected
                            text: "OpenRouter endpoint (preview)"
                            color: "#d0e3f5"
                            font.pixelSize: 13
                        }
                        TextField {
                            visible: wizard.openRouterSelected
                            Layout.fillWidth: true
                            readOnly: true
                            text: "https://openrouter.ai/api"
                        }

                        Text {
                            visible: !wizard.openRouterSelected
                            text: "Local AI backend endpoint (LM Studio / compatible)"
                            color: "#d0e3f5"
                            font.pixelSize: 13
                        }
                        TextField {
                            id: endpointField
                            visible: !wizard.openRouterSelected
                            Layout.fillWidth: true
                            text: settingsVm.lmStudioEndpoint
                            onEditingFinished: wizard.autoApplyProviderSelection()
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            Button {
                                text: "Refresh models"
                                onClicked: settingsVm.refreshModels()
                            }

                            ComboBox {
                                id: modelCombo
                                Layout.fillWidth: true
                                model: settingsVm.models
                                editable: true
                                currentIndex: -1
                                editText: settingsVm.selectedModel
                                onActivated: settingsVm.setSelectedModel(effectiveModelText())
                            }
                        }

                        Text {
                            text: "You can type any model name manually; it does not need to be in the dropdown list."
                            color: "#9ab0ca"
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                        }

                        Text { text: "Intent model"; color: "#d0e3f5"; font.pixelSize: 13 }
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

                        Text { text: "Intent model path"; color: "#d0e3f5"; font.pixelSize: 13 }
                        RowLayout {
                            Layout.fillWidth: true
                            TextField { id: intentModelPathField; Layout.fillWidth: true; text: settingsVm.intentModelPath; readOnly: true }
                            Button {
                                text: "Open Dir"
                                enabled: intentModelPathField.text.length > 0
                                onClicked: settingsVm.openContainingDirectory(intentModelPathField.text)
                            }
                        }
                        }

                        ColumnLayout {
                        id: voiceStep
                        width: stepStack.width
                        spacing: 14

                        Text { text: "TTS engine"; color: "#d0e3f5"; font.pixelSize: 13 }
                        TextField { Layout.fillWidth: true; readOnly: true; text: "piper" }

                        Text { text: "whisper.cpp executable"; color: "#d0e3f5"; font.pixelSize: 13 }
                        RowLayout {
                            Layout.fillWidth: true
                            TextField { id: whisperPathField; Layout.fillWidth: true; text: settingsVm.whisperExecutable }
                            Button {
                                text: "Download"
                                visible: settingsVm.supportsAutoToolInstall
                                onClicked: {
                                    settingsVm.downloadTool("whisper.cpp")
                                    wizard.syncVoiceFieldsFromBackend()
                                }
                            }
                            Button { text: "Open Dir"; onClicked: settingsVm.openContainingDirectory(whisperPathField.text) }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Button { text: "Auto Detect"; onClicked: { settingsVm.autoDetectVoiceTools(); wizard.syncVoiceFieldsFromBackend() } }
                            Button {
                                text: "Install Stack"
                                visible: settingsVm.supportsAutoToolInstall
                                onClicked: settingsVm.installAllTools()
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: !settingsVm.supportsAutoToolInstall
                            text: "Linux setup uses manual dependency selection. Point Vaxil to installed `whisper`, `piper`, `ffmpeg`, and model files."
                            color: "#9ab0ca"
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                        }

                        Text { text: "Whisper model"; color: "#d0e3f5"; font.pixelSize: 13 }
                        RowLayout {
                            Layout.fillWidth: true
                            TextField { id: whisperModelPathField; Layout.fillWidth: true; text: settingsVm.whisperModelPath }
                            Button { text: "Open Dir"; onClicked: settingsVm.openContainingDirectory(whisperModelPathField.text) }
                        }
                        Text { text: "Official Whisper model"; color: "#d0e3f5"; font.pixelSize: 13 }
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
                                    wizard.syncVoiceFieldsFromBackend()
                                }
                            }
                        }
                        Text {
                            text: "Use a valid whisper.cpp ggml model such as ggml-base.en.bin."
                            color: "#9ab0ca"
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                        }

                        Text { text: "Piper executable"; color: "#d0e3f5"; font.pixelSize: 13 }
                        RowLayout {
                            Layout.fillWidth: true
                            TextField { id: piperPathField; Layout.fillWidth: true; text: settingsVm.piperExecutable }
                            Button {
                                text: "Download"
                                visible: settingsVm.supportsAutoToolInstall
                                onClicked: {
                                    settingsVm.downloadTool("piper")
                                    wizard.syncVoiceFieldsFromBackend()
                                }
                            }
                            Button { text: "Open Dir"; onClicked: settingsVm.openContainingDirectory(piperPathField.text) }
                        }

                        Text { text: "Piper voice model"; color: "#d0e3f5"; font.pixelSize: 13 }
                        RowLayout {
                            Layout.fillWidth: true
                            TextField { id: voicePathField; Layout.fillWidth: true; text: settingsVm.piperVoiceModel }
                            Button { text: "Open Dir"; onClicked: settingsVm.openContainingDirectory(voicePathField.text) }
                        }

                        Text { text: "Official Piper voice"; color: "#d0e3f5"; font.pixelSize: 13 }
                        RowLayout {
                            Layout.fillWidth: true

                            ComboBox {
                                id: voicePresetCombo
                                Layout.fillWidth: true
                                model: settingsVm.voicePresetNames
                                onActivated: settingsVm.setSelectedVoicePresetId(settingsVm.voicePresetIds[currentIndex])
                            }

                            Button {
                                text: "Download"
                                visible: settingsVm.supportsAutoToolInstall
                                onClicked: {
                                    settingsVm.setSelectedVoicePresetId(settingsVm.voicePresetIds[voicePresetCombo.currentIndex])
                                    settingsVm.downloadVoiceModel(settingsVm.voicePresetIds[voicePresetCombo.currentIndex])
                                    wizard.syncVoiceFieldsFromBackend()
                                }
                            }
                        }

                        Text {
                            text: "Use one of the curated official Piper voices, then keep the resolved .onnx model path active."
                            color: "#9ab0ca"
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                        }

                        Text { text: "ffmpeg executable"; color: "#d0e3f5"; font.pixelSize: 13 }
                        RowLayout {
                            Layout.fillWidth: true
                            TextField { id: ffmpegPathField; Layout.fillWidth: true; text: settingsVm.ffmpegExecutable }
                            Button { text: "Open Dir"; onClicked: settingsVm.openContainingDirectory(ffmpegPathField.text) }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            CheckBox {
                                id: wizardAecCheck
                                text: "AEC enabled"
                                checked: settingsVm.aecEnabled
                                onToggled: settingsVm.saveAudioProcessing(checked, wizardRnnoiseCheck.checked, wizardVadSlider.value)
                            }
                            CheckBox {
                                id: wizardRnnoiseCheck
                                text: "RNNoise enabled"
                                checked: settingsVm.rnnoiseEnabled
                                onToggled: settingsVm.saveAudioProcessing(wizardAecCheck.checked, checked, wizardVadSlider.value)
                            }
                        }

                        Text { text: "VAD sensitivity"; color: "#d0e3f5"; font.pixelSize: 13 }
                        Slider {
                            id: wizardVadSlider
                            Layout.fillWidth: true
                            from: 0.05
                            to: 0.95
                            value: settingsVm.vadSensitivity
                            onPressedChanged: if (!pressed) settingsVm.saveAudioProcessing(wizardAecCheck.checked, wizardRnnoiseCheck.checked, value)
                        }

                        Text { text: "Input device (microphone)"; color: "#d0e3f5"; font.pixelSize: 13 }
                        ComboBox {
                            id: inputDeviceCombo
                            Layout.fillWidth: true
                            model: settingsVm.audioInputDeviceNames
                        }

                        Text { text: "Output device (speaker/headset)"; color: "#d0e3f5"; font.pixelSize: 13 }
                        ComboBox {
                            id: outputDeviceCombo
                            Layout.fillWidth: true
                            model: settingsVm.audioOutputDeviceNames
                        }

                        }

                        ColumnLayout {
                        id: wakeStep
                        width: stepStack.width
                        spacing: 14

                        Text { text: "Wake phrase"; color: "#d0e3f5"; font.pixelSize: 13 }
                        JarvisUi.VisionGlassPanel {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 58
                            radius: 18
                            panelColor: "#181c2428"
                            innerColor: "#1d222b32"
                            outlineColor: "#20ffffff"
                            highlightColor: "#10ffffff"
                            shadowOpacity: 0.14

                            Text {
                                anchors.centerIn: parent
                                text: settingsVm.wakeWordPhrase
                                color: "#f3f7ff"
                                font.pixelSize: 22
                                font.weight: Font.Medium
                            }
                        }

                        Text {
                            text: "Wake engine"
                            color: "#eef7ff"
                            font.pixelSize: 18
                            font.weight: Font.Medium
                        }

                        TextField { Layout.fillWidth: true; readOnly: true; text: "sherpa-onnx" }

                        RowLayout {
                            Layout.fillWidth: true

                            Button {
                                text: "Download Wake Runtime"
                                visible: settingsVm.supportsAutoToolInstall
                                onClicked: {
                                    settingsVm.downloadTool("sherpa-onnx")
                                    wizard.syncVoiceFieldsFromBackend()
                                }
                            }

                            Button {
                                text: "Download Wake Model"
                                visible: settingsVm.supportsAutoToolInstall
                                onClicked: {
                                    settingsVm.downloadTool("sherpa-kws-model")
                                    wizard.syncVoiceFieldsFromBackend()
                                }
                            }
                        }

                        Text {
                            text: settingsVm.supportsAutoToolInstall
                                  ? "Model status: Managed runtime flow"
                                  : "Model status: Optional manual runtime"
                            color: settingsVm.supportsAutoToolInstall ? "#69d39a" : "#f0c56a"
                            font.pixelSize: 14
                        }

                        Text {
                            text: "Vaxil uses the local sherpa-onnx wake stack. Microphone input stays gated while TTS is playing."
                            color: "#9ab0ca"
                            font.pixelSize: 14
                            wrapMode: Text.Wrap
                        }

                        Text {
                            text: settingsVm.supportsAutoToolInstall
                                  ? "Install the sherpa runtime and wake model from here, then run Auto Detect so wake checks can pass."
                                  : "Install sherpa-onnx and the wake model manually, then run Auto Detect."
                            color: "#7f97b7"
                            font.pixelSize: 13
                            wrapMode: Text.Wrap
                        }

                        Text {
                            Layout.fillWidth: true
                            text: settingsVm.toolInstallStatus.length > 0
                                  ? settingsVm.toolInstallStatus
                                  : (settingsVm.supportsAutoToolInstall
                                      ? "Use Auto Detect after installing tools so the current local paths are populated."
                                      : "Use Auto Detect after installing tools so Vaxil can resolve the current local paths.")
                            color: "#9ab0ca"
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                        }

                        CheckBox {
                            id: clickThroughCheck
                            text: "Enable click-through overlay by default"
                            checked: settingsVm.clickThroughEnabled
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "UI mode"; color: "#d0e3f5"; font.pixelSize: 13 }
                            ComboBox {
                                id: uiModeCombo
                                Layout.fillWidth: true
                                model: ["Full UI", "Overlay UI"]
                                onActivated: settingsVm.setUiMode(currentIndex === 1 ? "overlay" : "full")
                            }
                        }
                        }

                        ColumnLayout {
                        id: finalStep
                        width: stepStack.width
                        spacing: 14

                        Text {
                            text: "Test phrases"
                            color: "#d0e3f5"
                            font.pixelSize: 13
                        }

                        Text {
                            text: "1. Say: \"" + settingsVm.wakeWordPhrase + "\""
                            color: "#eef7ff"
                            font.pixelSize: 18
                            wrapMode: Text.Wrap
                        }

                        Text {
                            text: "2. Say: \"" + settingsVm.wakeWordPhrase + ", what's the time now?\""
                            color: "#eef7ff"
                            font.pixelSize: 18
                            wrapMode: Text.Wrap
                        }

                        Text {
                            text: "The second test confirms that the assistant uses the real local clock instead of guessing."
                            color: "#9ab0ca"
                            font.pixelSize: 14
                            wrapMode: Text.Wrap
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            Button {
                                text: "Start mic test"
                                onClicked: settingsVm.startListening()
                            }

                            Button {
                                text: "Test Vaxil"
                                onClicked: {
                                    if (!settingsVm.runSetupScenario(
                                            userNameField.text,
                                            providerCombo.currentValue,
                                            providerApiKeyField.text,
                                            wizard.openRouterSelected ? "https://openrouter.ai/api" : endpointField.text,
                                            effectiveModelText(),
                                            whisperPathField.text,
                                            whisperModelPathField.text,
                                            settingsVm.wakeTriggerThreshold,
                                            settingsVm.wakeTriggerCooldownMs,
                                            piperPathField.text,
                                            voicePathField.text,
                                            ffmpegPathField.text,
                                            inputDeviceCombo.currentIndex >= 0 ? settingsVm.audioInputDeviceIds[inputDeviceCombo.currentIndex] : "",
                                            outputDeviceCombo.currentIndex >= 0 ? settingsVm.audioOutputDeviceIds[outputDeviceCombo.currentIndex] : "",
                                            clickThroughCheck.checked,
                                            "wakeword_ready")) {
                                        wizard.stepIndex = 3
                                    }
                                }
                            }

                            Button {
                                text: "Test time query"
                                onClicked: {
                                    if (!settingsVm.runSetupScenario(
                                            userNameField.text,
                                            providerCombo.currentValue,
                                            providerApiKeyField.text,
                                            wizard.openRouterSelected ? "https://openrouter.ai/api" : endpointField.text,
                                            effectiveModelText(),
                                            whisperPathField.text,
                                            whisperModelPathField.text,
                                            settingsVm.wakeTriggerThreshold,
                                            settingsVm.wakeTriggerCooldownMs,
                                            piperPathField.text,
                                            voicePathField.text,
                                            ffmpegPathField.text,
                                            inputDeviceCombo.currentIndex >= 0 ? settingsVm.audioInputDeviceIds[inputDeviceCombo.currentIndex] : "",
                                            outputDeviceCombo.currentIndex >= 0 ? settingsVm.audioOutputDeviceIds[outputDeviceCombo.currentIndex] : "",
                                            clickThroughCheck.checked,
                                            "wakeword_time")) {
                                        wizard.stepIndex = 3
                                    }
                                }
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: settingsVm.toolInstallStatus.length > 0 ? settingsVm.toolInstallStatus : "Run the tests before finishing setup."
                            color: "#9ab0ca"
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                        }

                        Text {
                            text: "Tools status"
                            color: "#d0e3f5"
                            font.pixelSize: 13
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Button { text: "Rescan"; onClicked: settingsVm.rescanTools() }
                            Button { text: "Auto Detect"; onClicked: { settingsVm.autoDetectVoiceTools(); wizard.syncVoiceFieldsFromBackend() } }
                            Button {
                                text: "Install All"
                                visible: settingsVm.supportsAutoToolInstall
                                onClicked: settingsVm.installAllTools()
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: settingsVm.toolInstallStatus.length > 0 ? settingsVm.toolInstallStatus : "No active download."
                            color: "#9ab0ca"
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                        }

                        JarvisUi.VisionGlassPanel {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 10
                            visible: settingsVm.supportsAutoToolInstall && settingsVm.toolDownloadPercent >= 0
                            radius: 5
                            panelColor: "#171c2228"
                            innerColor: "#1d222930"
                            outlineColor: "#18ffffff"
                            highlightColor: "#08ffffff"
                            shadowOpacity: 0.08

                            Rectangle {
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                width: parent.width * Math.max(0, Math.min(1, settingsVm.toolDownloadPercent / 100.0))
                                radius: 5
                                color: "#d8e7ff"
                                opacity: 0.8
                            }
                        }

                        Repeater {
                            model: settingsVm.toolStatuses

                            delegate: JarvisUi.VisionGlassPanel {
                                required property var modelData
                                Layout.fillWidth: true
                                width: parent.width
                                height: 52
                                radius: 14
                                panelColor: modelData.installed ? "#17221f26" : "#171c2326"
                                innerColor: modelData.installed ? "#1d2a2530" : "#1d232c32"
                                outlineColor: modelData.installed ? "#24ffffff" : "#18ffffff"
                                highlightColor: "#10ffffff"
                                shadowOpacity: 0.14

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    Rectangle { width: 10; height: 10; radius: 5; color: modelData.installed ? "#1ecb6b" : "#f04d5d" }
                                    Text { text: modelData.name; color: "#eef7ff"; font.pixelSize: 13; Layout.preferredWidth: 150 }
                                    Text { text: modelData.installed ? "Installed" : "Missing"; color: "#9ab0ca"; font.pixelSize: 12; Layout.fillWidth: true }
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
                }

                RowLayout {
                    Layout.fillWidth: true

                    Button {
                        text: "Back"
                        enabled: wizard.stepIndex > 0
                        onClicked: wizard.stepIndex--
                    }

                    Item { Layout.fillWidth: true }

                    Button {
                        visible: wizard.stepIndex === 4
                        text: "Quick test"
                        onClicked: {
                            if (!settingsVm.runSetupScenario(
                                    userNameField.text,
                                    providerCombo.currentValue,
                                    providerApiKeyField.text,
                                    wizard.openRouterSelected ? "https://openrouter.ai/api" : endpointField.text,
                                    effectiveModelText(),
                                    whisperPathField.text,
                                    whisperModelPathField.text,
                                    settingsVm.wakeTriggerThreshold,
                                    settingsVm.wakeTriggerCooldownMs,
                                    piperPathField.text,
                                    voicePathField.text,
                                    ffmpegPathField.text,
                                    inputDeviceCombo.currentIndex >= 0 ? settingsVm.audioInputDeviceIds[inputDeviceCombo.currentIndex] : "",
                                    outputDeviceCombo.currentIndex >= 0 ? settingsVm.audioOutputDeviceIds[outputDeviceCombo.currentIndex] : "",
                                    clickThroughCheck.checked,
                                    "wakeword_ready")) {
                                wizard.stepIndex = 3
                            }
                        }
                    }

                    Button {
                        text: wizard.stepIndex === 4 ? "Finish setup" : "Continue"
                        onClicked: {
                            if (wizard.stepIndex < 4) {
                                wizard.stepIndex++
                                return
                            }

                            settingsVm.setUiMode(uiModeCombo.currentIndex === 1 ? "overlay" : "full")
                            if (!settingsVm.completeInitialSetup(
                                    userNameField.text,
                                    providerCombo.currentValue,
                                    providerApiKeyField.text,
                                    wizard.openRouterSelected ? "https://openrouter.ai/api" : endpointField.text,
                                    effectiveModelText(),
                                    whisperPathField.text,
                                    whisperModelPathField.text,
                                    settingsVm.wakeTriggerThreshold,
                                    settingsVm.wakeTriggerCooldownMs,
                                    piperPathField.text,
                                    voicePathField.text,
                                    ffmpegPathField.text,
                                    inputDeviceCombo.currentIndex >= 0 ? settingsVm.audioInputDeviceIds[inputDeviceCombo.currentIndex] : "",
                                    outputDeviceCombo.currentIndex >= 0 ? settingsVm.audioOutputDeviceIds[outputDeviceCombo.currentIndex] : "",
                                    clickThroughCheck.checked)) {
                                wizard.stepIndex = 2
                            }
                        }
                    }
                }
            }
        }
    }
}
