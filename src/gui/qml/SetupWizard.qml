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
    color: "#050912"

    property real dpiScale: Math.max(1.0, Screen.devicePixelRatio)

    property int stepIndex: 0

    function syncVoiceFieldsFromBackend() {
        whisperPathField.text = settingsVm.whisperExecutable
        whisperModelPathField.text = settingsVm.whisperModelPath
        intentModelPathField.text = settingsVm.intentModelPath
        piperPathField.text = settingsVm.piperExecutable
        voicePathField.text = settingsVm.piperVoiceModel
        ffmpegPathField.text = settingsVm.ffmpegExecutable

        const modelIndex = settingsVm.models.indexOf(settingsVm.selectedModel)
        modelCombo.currentIndex = modelIndex >= 0 ? modelIndex : 0

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
        color: "#050912"
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#091224" }
            GradientStop { position: 0.6; color: "#060b14" }
            GradientStop { position: 1.0; color: "#03060c" }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 20

        Rectangle {
            Layout.preferredWidth: 300
            Layout.fillHeight: true
            radius: 34
            color: "#88111c2c"
            border.width: 1
            border.color: "#213754"

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

                Text {
                    width: parent.width
                    text: settingsVm.assistantName + " Setup"
                    color: "#eef7ff"
                    font.pixelSize: 30
                    font.weight: Font.Medium
                    horizontalAlignment: Text.AlignHCenter
                }

                Text {
                    width: parent.width
                    text: "Prepare the local AI, voice stack, wake phrase flow, and overlay defaults before activation."
                    color: "#8da6c7"
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

                        delegate: Rectangle {
                            required property int index

                            width: parent.width
                            height: 46
                            radius: 23
                            color: wizard.stepIndex === index ? "#17375d" : "#0d1829"
                            border.width: 1
                            border.color: wizard.stepIndex === index ? "#5ba5ff" : "#213754"

                            Text {
                                anchors.centerIn: parent
                                text: index === 0 ? "Profile"
                                    : index === 1 ? "AI Core"
                                    : index === 2 ? "Voice"
                                    : index === 3 ? "Wake Word"
                                    : "Final Check"
                                color: "#e5f4ff"
                                font.pixelSize: 14
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 34
            color: "#7a08111d"
            border.width: 1
            border.color: "#20314e"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 28
                spacing: 18

                Text {
                    text: wizard.stepIndex === 0 ? "Identity"
                        : wizard.stepIndex === 1 ? "AI Core"
                        : wizard.stepIndex === 2 ? "Voice Pipeline"
                        : wizard.stepIndex === 3 ? "Wake Phrase"
                        : "Final Validation"
                    color: "#f0f8ff"
                    font.pixelSize: 28
                    font.weight: Font.Medium
                }

                Text {
                    text: wizard.stepIndex === 0 ? "Define how the assistant should address you."
                        : wizard.stepIndex === 1 ? "Point JARVIS at the local AI backend and choose the active model."
                        : wizard.stepIndex === 2 ? "Connect whisper.cpp, Piper, FFmpeg, and the local speech models you want."
                        : wizard.stepIndex === 3 ? "Review the local sherpa wake engine and microphone behavior."
                        : "Run final checks, trigger tests, and confirm the real startup behavior."
                    color: "#89a3c4"
                    font.pixelSize: 14
                    wrapMode: Text.Wrap
                }

                ProgressBar {
                    Layout.fillWidth: true
                    from: 0
                    to: 5
                    value: wizard.stepIndex + 1
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

                        Text { text: "Local AI backend endpoint"; color: "#d0e3f5"; font.pixelSize: 13 }
                        TextField {
                            id: endpointField
                            Layout.fillWidth: true
                            text: settingsVm.lmStudioEndpoint
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
                            }
                        }

                        Text {
                            text: "The selected model is stored in settings and used for all local AI backend requests."
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
                            Button { text: "Open Dir"; onClicked: settingsVm.openContainingDirectory(whisperPathField.text) }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Button { text: "Auto Detect"; onClicked: { settingsVm.autoDetectVoiceTools(); wizard.syncVoiceFieldsFromBackend() } }
                            Button { text: "Install Stack"; onClicked: settingsVm.installAllTools() }
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
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 58
                            radius: 18
                            color: "#0f1a2a"
                            border.width: 1
                            border.color: "#284666"

                            Text {
                                anchors.centerIn: parent
                                text: settingsVm.wakeWordPhrase
                                color: "#e7f7ff"
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

                        Text {
                            text: "Model status: Bundled sherpa runtime"
                            color: "#69d39a"
                            font.pixelSize: 14
                        }

                        Text {
                            text: "JARVIS uses the local sherpa-onnx wake stack. Microphone input stays gated while TTS is playing."
                            color: "#9ab0ca"
                            font.pixelSize: 14
                            wrapMode: Text.Wrap
                        }

                        Text {
                            text: "The wake engine is managed automatically from the installed local tool stack."
                            color: "#7f97b7"
                            font.pixelSize: 13
                            wrapMode: Text.Wrap
                        }

                        Text {
                            Layout.fillWidth: true
                            text: settingsVm.toolInstallStatus.length > 0
                                  ? settingsVm.toolInstallStatus
                                  : "Use Auto Detect after installing tools so the current local paths are populated."
                            color: "#9ab0ca"
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                        }

                        CheckBox {
                            id: clickThroughCheck
                            text: "Enable click-through overlay by default"
                            checked: settingsVm.clickThroughEnabled
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
                                text: "Test Jarvis"
                                onClicked: {
                                    if (!settingsVm.runSetupScenario(
                                            userNameField.text,
                                            endpointField.text,
                                            modelCombo.currentText,
                                            whisperPathField.text,
                                            whisperModelPathField.text,
                                            "",
                                            "",
                                            settingsVm.preciseTriggerThreshold,
                                            settingsVm.preciseTriggerCooldownMs,
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
                                            endpointField.text,
                                            modelCombo.currentText,
                                            whisperPathField.text,
                                            whisperModelPathField.text,
                                            "",
                                            "",
                                            settingsVm.preciseTriggerThreshold,
                                            settingsVm.preciseTriggerCooldownMs,
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
                            Button { text: "Install All"; onClicked: settingsVm.installAllTools() }
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
                            visible: settingsVm.toolDownloadPercent >= 0
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
                                height: 52
                                radius: 14
                                color: "#0f1a2a"
                                border.width: 1
                                border.color: modelData.installed ? "#2e7e4b" : "#284666"

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
                                    endpointField.text,
                                    modelCombo.currentText,
                                    whisperPathField.text,
                                    whisperModelPathField.text,
                                    "",
                                    "",
                                    settingsVm.preciseTriggerThreshold,
                                    settingsVm.preciseTriggerCooldownMs,
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

                            if (!settingsVm.completeInitialSetup(
                                    userNameField.text,
                                    endpointField.text,
                                    modelCombo.currentText,
                                    whisperPathField.text,
                                    whisperModelPathField.text,
                                    "",
                                    "",
                                    settingsVm.preciseTriggerThreshold,
                                    settingsVm.preciseTriggerCooldownMs,
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

