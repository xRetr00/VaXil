import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Window {
    id: settingsWindow
    width: 520
    height: 720
    visible: true
    title: "JARVIS Settings"
    color: "#0a1118"
    onClosing: function(close) {
        close.accepted = false
        hide()
    }

    ScrollView {
        anchors.fill: parent
        anchors.margins: 16

        ColumnLayout {
            width: parent.width
            spacing: 12

            Label { text: "LM Studio Endpoint"; color: "#d8f4ff" }
            TextField { id: endpointField; text: backend.lmStudioEndpoint }

            Label { text: "Selected Model"; color: "#d8f4ff" }
            ComboBox {
                id: modelCombo
                model: backend.models
                Component.onCompleted: {
                    const index = backend.models.indexOf(backend.selectedModel)
                    if (index >= 0) {
                        currentIndex = index
                    }
                }
                onActivated: backend.setSelectedModel(currentText)
            }

            Label { text: "Default Reasoning Mode"; color: "#d8f4ff" }
            ComboBox {
                id: modeCombo
                model: ["Fast", "Balanced", "Deep"]
                currentIndex: backend.defaultReasoningMode
            }

            CheckBox { id: autoRoutingCheck; text: "Enable auto-routing"; checked: backend.autoRoutingEnabled }
            CheckBox { id: streamCheck; text: "Enable streaming"; checked: backend.streamingEnabled }

            Label { text: "Request Timeout (ms)"; color: "#d8f4ff" }
            SpinBox { id: timeoutSpin; from: 10000; to: 15000; value: backend.requestTimeoutMs }

            Label { text: "whisper.cpp executable"; color: "#d8f4ff" }
            TextField { id: whisperPathField; text: backend.whisperExecutable }

            Label { text: "Piper executable"; color: "#d8f4ff" }
            TextField { id: piperPathField; text: backend.piperExecutable }

            Label { text: "Piper voice model"; color: "#d8f4ff" }
            TextField { id: voicePathField; text: backend.piperVoiceModel }

            Label { text: "ffmpeg executable"; color: "#d8f4ff" }
            TextField { id: ffmpegPathField; text: backend.ffmpegExecutable }

            Label { text: "Voice Speed"; color: "#d8f4ff" }
            Slider { id: speedSlider; from: 0.7; to: 1.5; value: backend.voiceSpeed }

            Label { text: "Voice Pitch"; color: "#d8f4ff" }
            Slider { id: pitchSlider; from: 0.8; to: 1.2; value: backend.voicePitch }

            Label { text: "Mic Sensitivity"; color: "#d8f4ff" }
            Slider { id: micSlider; from: 0.01; to: 0.10; value: backend.micSensitivity }

            CheckBox { id: clickThroughCheck; text: "Enable click-through overlay"; checked: backend.clickThroughEnabled }

            RowLayout {
                Layout.fillWidth: true

                Button {
                    text: "Refresh Models"
                    onClicked: backend.refreshModels()
                }

                Button {
                    text: "Save"
                    onClicked: {
                        backend.saveSettings(
                            endpointField.text,
                            modelCombo.currentText,
                            modeCombo.currentIndex,
                            autoRoutingCheck.checked,
                            streamCheck.checked,
                            timeoutSpin.value,
                            whisperPathField.text,
                            piperPathField.text,
                            voicePathField.text,
                            ffmpegPathField.text,
                            speedSlider.value,
                            pitchSlider.value,
                            micSlider.value,
                            clickThroughCheck.checked
                        )
                    }
                }
            }
        }
    }
}
