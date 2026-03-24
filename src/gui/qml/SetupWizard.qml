import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import "." as JarvisUi

Window {
    id: wizard

    width: 920
    height: 760
    visible: false
    title: backend.assistantName + " Setup"
    color: "#050912"

    property int stepIndex: 0

    function syncVoiceFieldsFromBackend() {
        whisperPathField.text = backend.whisperExecutable
        whisperModelPathField.text = backend.whisperModelPath
        porcupineAccessKeyField.text = backend.porcupineAccessKey
        porcupineLibraryPathField.text = backend.porcupineLibraryPath
        porcupineModelPathField.text = backend.porcupineModelPath
        porcupineKeywordPathField.text = backend.porcupineKeywordPath
        porcupineSensitivitySlider.value = backend.porcupineSensitivity
        piperPathField.text = backend.piperExecutable
        voicePathField.text = backend.piperVoiceModel
        ffmpegPathField.text = backend.ffmpegExecutable

        const modelIndex = backend.models.indexOf(backend.selectedModel)
        modelCombo.currentIndex = modelIndex >= 0 ? modelIndex : 0

        const voiceIndex = backend.voicePresetIds.indexOf(backend.selectedVoicePresetId)
        voicePresetCombo.currentIndex = voiceIndex >= 0 ? voiceIndex : 0

        const inputIndex = backend.audioInputDeviceIds.indexOf(backend.selectedAudioInputDeviceId)
        inputDeviceCombo.currentIndex = inputIndex >= 0 ? inputIndex : 0

        const outputIndex = backend.audioOutputDeviceIds.indexOf(backend.selectedAudioOutputDeviceId)
        outputDeviceCombo.currentIndex = outputIndex >= 0 ? outputIndex : 0
    }

    onClosing: function(close) {
        close.accepted = false
        hide()
    }

    onVisibleChanged: {
        if (visible) {
            backend.refreshAudioDevices()
            backend.refreshModels()
            syncVoiceFieldsFromBackend()
        }
    }

    Connections {
        target: backend
        function onModelsChanged() {
            wizard.syncVoiceFieldsFromBackend()
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
                    text: backend.assistantName + " Setup"
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
                        : wizard.stepIndex === 1 ? "Point JARVIS at LM Studio and choose the active model."
                        : wizard.stepIndex === 2 ? "Connect whisper.cpp, Piper, FFmpeg, and the voice model you want."
                        : wizard.stepIndex === 3 ? "Configure the dedicated Porcupine wake word engine for always-listening detection."
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

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: wizard.stepIndex

                    ColumnLayout {
                        spacing: 14

                        Text { text: "Display name"; color: "#d0e3f5"; font.pixelSize: 13 }
                        TextField {
                            id: displayNameField
                            Layout.fillWidth: true
                            text: backend.userName
                            placeholderText: "Name shown in the interface"
                        }

                        Text { text: "Spoken name"; color: "#d0e3f5"; font.pixelSize: 13 }
                        TextField {
                            id: spokenNameField
                            Layout.fillWidth: true
                            text: backend.spokenUserName
                            placeholderText: "Pronunciation for voice replies (optional)"
                        }
                    }

                    ColumnLayout {
                        spacing: 14

                        Text { text: "LM Studio endpoint"; color: "#d0e3f5"; font.pixelSize: 13 }
                        TextField {
                            id: endpointField
                            Layout.fillWidth: true
                            text: backend.lmStudioEndpoint
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            Button {
                                text: "Refresh models"
                                onClicked: backend.refreshModels()
                            }

                            ComboBox {
                                id: modelCombo
                                Layout.fillWidth: true
                                model: backend.models
                            }
                        }

                        Text {
                            text: "The selected model is stored in settings and used for all LM Studio requests."
                            color: "#9ab0ca"
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                        }
                    }

                    ColumnLayout {
                        spacing: 14

                        Text { text: "whisper.cpp executable"; color: "#d0e3f5"; font.pixelSize: 13 }
                        RowLayout {
                            Layout.fillWidth: true
                            TextField { id: whisperPathField; Layout.fillWidth: true; text: backend.whisperExecutable }
                            Button { text: "Open Dir"; onClicked: backend.openContainingDirectory(whisperPathField.text) }
                        }

                        Text { text: "Whisper model"; color: "#d0e3f5"; font.pixelSize: 13 }
                        RowLayout {
                            Layout.fillWidth: true
                            TextField { id: whisperModelPathField; Layout.fillWidth: true; text: backend.whisperModelPath }
                            Button { text: "Open Dir"; onClicked: backend.openContainingDirectory(whisperModelPathField.text) }
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
                            TextField { id: piperPathField; Layout.fillWidth: true; text: backend.piperExecutable }
                            Button { text: "Open Dir"; onClicked: backend.openContainingDirectory(piperPathField.text) }
                        }

                        Text { text: "Piper voice model"; color: "#d0e3f5"; font.pixelSize: 13 }
                        RowLayout {
                            Layout.fillWidth: true
                            TextField { id: voicePathField; Layout.fillWidth: true; text: backend.piperVoiceModel }
                            Button { text: "Open Dir"; onClicked: backend.openContainingDirectory(voicePathField.text) }
                        }

                        Text { text: "Official Piper voice"; color: "#d0e3f5"; font.pixelSize: 13 }
                        RowLayout {
                            Layout.fillWidth: true

                            ComboBox {
                                id: voicePresetCombo
                                Layout.fillWidth: true
                                model: backend.voicePresetNames
                                onActivated: backend.setSelectedVoicePresetId(backend.voicePresetIds[currentIndex])
                            }

                            Button {
                                text: "Download"
                                onClicked: {
                                    backend.setSelectedVoicePresetId(backend.voicePresetIds[voicePresetCombo.currentIndex])
                                    backend.downloadVoiceModel(backend.voicePresetIds[voicePresetCombo.currentIndex])
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
                            TextField { id: ffmpegPathField; Layout.fillWidth: true; text: backend.ffmpegExecutable }
                            Button { text: "Open Dir"; onClicked: backend.openContainingDirectory(ffmpegPathField.text) }
                        }

                        Text { text: "Input device (microphone)"; color: "#d0e3f5"; font.pixelSize: 13 }
                        ComboBox {
                            id: inputDeviceCombo
                            Layout.fillWidth: true
                            model: backend.audioInputDeviceNames
                        }

                        Text { text: "Output device (speaker/headset)"; color: "#d0e3f5"; font.pixelSize: 13 }
                        ComboBox {
                            id: outputDeviceCombo
                            Layout.fillWidth: true
                            model: backend.audioOutputDeviceNames
                        }

                    }

                    ColumnLayout {
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
                                text: backend.wakeWordPhrase
                                color: "#e7f7ff"
                                font.pixelSize: 22
                                font.weight: Font.Medium
                            }
                        }

                        Text { text: "Picovoice AccessKey"; color: "#d0e3f5"; font.pixelSize: 13 }
                        TextField {
                            id: porcupineAccessKeyField
                            Layout.fillWidth: true
                            text: backend.porcupineAccessKey
                            echoMode: TextInput.PasswordEchoOnEdit
                            placeholderText: "Required for always-listening wake word detection"
                        }

                        Text { text: "Porcupine library"; color: "#d0e3f5"; font.pixelSize: 13 }
                        RowLayout {
                            Layout.fillWidth: true
                            TextField { id: porcupineLibraryPathField; Layout.fillWidth: true; text: backend.porcupineLibraryPath }
                            Button { text: "Open Dir"; onClicked: backend.openContainingDirectory(porcupineLibraryPathField.text) }
                        }

                        Text { text: "Porcupine model"; color: "#d0e3f5"; font.pixelSize: 13 }
                        RowLayout {
                            Layout.fillWidth: true
                            TextField { id: porcupineModelPathField; Layout.fillWidth: true; text: backend.porcupineModelPath }
                            Button { text: "Open Dir"; onClicked: backend.openContainingDirectory(porcupineModelPathField.text) }
                        }

                        Text { text: "Jarvis keyword file"; color: "#d0e3f5"; font.pixelSize: 13 }
                        RowLayout {
                            Layout.fillWidth: true
                            TextField { id: porcupineKeywordPathField; Layout.fillWidth: true; text: backend.porcupineKeywordPath }
                            Button { text: "Open Dir"; onClicked: backend.openContainingDirectory(porcupineKeywordPathField.text) }
                        }

                        Text { text: "Wake sensitivity"; color: "#d0e3f5"; font.pixelSize: 13 }
                        Slider {
                            id: porcupineSensitivitySlider
                            Layout.fillWidth: true
                            from: 0.3
                            to: 0.9
                            value: backend.porcupineSensitivity
                        }

                        Text {
                            text: "JARVIS uses Picovoice Porcupine for dedicated wake detection. Whisper is only used after the wake word has been detected."
                            color: "#9ab0ca"
                            font.pixelSize: 14
                            wrapMode: Text.Wrap
                        }

                        Text {
                            text: "The official Windows Jarvis keyword file can be auto-downloaded here. Enter a Picovoice Console AccessKey to enable always-listening wake detection."
                            color: "#7f97b7"
                            font.pixelSize: 13
                            wrapMode: Text.Wrap
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            Button {
                                Layout.fillWidth: true
                                text: "Auto-detect wake assets"
                                onClicked: {
                                    backend.refreshAudioDevices()
                                    backend.autoDetectVoiceTools()
                                    wizard.syncVoiceFieldsFromBackend()
                                }
                            }

                            Button {
                                Layout.fillWidth: true
                                text: "Download official assets"
                                onClicked: {
                                    backend.setSelectedVoicePresetId(backend.voicePresetIds[voicePresetCombo.currentIndex])
                                    backend.installAndDetectVoiceTools()
                                    backend.refreshAudioDevices()
                                    wizard.syncVoiceFieldsFromBackend()
                                }
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: backend.toolInstallStatus.length > 0
                                  ? backend.toolInstallStatus
                                  : "Auto-detection checks the local Porcupine DLL, model, keyword file, and audio devices from this wake-word step only."
                            color: "#9ab0ca"
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                        }

                        CheckBox {
                            id: clickThroughCheck
                            text: "Enable click-through overlay by default"
                            checked: backend.clickThroughEnabled
                        }
                    }

                    ColumnLayout {
                        spacing: 14

                        Text {
                            text: "Test phrases"
                            color: "#d0e3f5"
                            font.pixelSize: 13
                        }

                        Text {
                            text: "1. Say: \"" + backend.wakeWordPhrase + "\""
                            color: "#eef7ff"
                            font.pixelSize: 18
                        }

                        Text {
                            text: "2. Say: \"" + backend.wakeWordPhrase + ", what's the time now?\""
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
                                onClicked: backend.startListening()
                            }

                            Button {
                                text: "Test Jarvis"
                                onClicked: {
                                    if (!backend.runSetupScenario(
                                            displayNameField.text,
                                            spokenNameField.text,
                                            endpointField.text,
                                            modelCombo.currentText,
                                            whisperPathField.text,
                                            whisperModelPathField.text,
                                            porcupineAccessKeyField.text,
                                            porcupineLibraryPathField.text,
                                            porcupineModelPathField.text,
                                            porcupineKeywordPathField.text,
                                            porcupineSensitivitySlider.value,
                                            piperPathField.text,
                                            voicePathField.text,
                                            ffmpegPathField.text,
                                            inputDeviceCombo.currentIndex >= 0 ? backend.audioInputDeviceIds[inputDeviceCombo.currentIndex] : "",
                                            outputDeviceCombo.currentIndex >= 0 ? backend.audioOutputDeviceIds[outputDeviceCombo.currentIndex] : "",
                                            clickThroughCheck.checked,
                                            "wakeword_ready")) {
                                        wizard.stepIndex = 2
                                    }
                                }
                            }

                            Button {
                                text: "Test time query"
                                onClicked: {
                                    if (!backend.runSetupScenario(
                                            displayNameField.text,
                                            spokenNameField.text,
                                            endpointField.text,
                                            modelCombo.currentText,
                                            whisperPathField.text,
                                            whisperModelPathField.text,
                                            porcupineAccessKeyField.text,
                                            porcupineLibraryPathField.text,
                                            porcupineModelPathField.text,
                                            porcupineKeywordPathField.text,
                                            porcupineSensitivitySlider.value,
                                            piperPathField.text,
                                            voicePathField.text,
                                            ffmpegPathField.text,
                                            inputDeviceCombo.currentIndex >= 0 ? backend.audioInputDeviceIds[inputDeviceCombo.currentIndex] : "",
                                            outputDeviceCombo.currentIndex >= 0 ? backend.audioOutputDeviceIds[outputDeviceCombo.currentIndex] : "",
                                            clickThroughCheck.checked,
                                            "wakeword_time")) {
                                        wizard.stepIndex = 2
                                    }
                                }
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: backend.toolInstallStatus.length > 0 ? backend.toolInstallStatus : "Run the tests before finishing setup."
                            color: "#9ab0ca"
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
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
                            if (!backend.runSetupScenario(
                                    displayNameField.text,
                                    spokenNameField.text,
                                    endpointField.text,
                                    modelCombo.currentText,
                                    whisperPathField.text,
                                    whisperModelPathField.text,
                                    porcupineAccessKeyField.text,
                                    porcupineLibraryPathField.text,
                                    porcupineModelPathField.text,
                                    porcupineKeywordPathField.text,
                                    porcupineSensitivitySlider.value,
                                    piperPathField.text,
                                    voicePathField.text,
                                    ffmpegPathField.text,
                                    inputDeviceCombo.currentIndex >= 0 ? backend.audioInputDeviceIds[inputDeviceCombo.currentIndex] : "",
                                    outputDeviceCombo.currentIndex >= 0 ? backend.audioOutputDeviceIds[outputDeviceCombo.currentIndex] : "",
                                    clickThroughCheck.checked,
                                    "wakeword_ready")) {
                                wizard.stepIndex = 2
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

                            if (!backend.completeInitialSetup(
                                    displayNameField.text,
                                    spokenNameField.text,
                                    endpointField.text,
                                    modelCombo.currentText,
                                    whisperPathField.text,
                                    whisperModelPathField.text,
                                    porcupineAccessKeyField.text,
                                    porcupineLibraryPathField.text,
                                    porcupineModelPathField.text,
                                    porcupineKeywordPathField.text,
                                    porcupineSensitivitySlider.value,
                                    piperPathField.text,
                                    voicePathField.text,
                                    ffmpegPathField.text,
                                    inputDeviceCombo.currentIndex >= 0 ? backend.audioInputDeviceIds[inputDeviceCombo.currentIndex] : "",
                                    outputDeviceCombo.currentIndex >= 0 ? backend.audioOutputDeviceIds[outputDeviceCombo.currentIndex] : "",
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
