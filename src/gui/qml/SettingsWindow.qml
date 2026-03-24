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

                    Text { text: "LM Studio endpoint"; color: "#c9def3"; font.pixelSize: 13 }
                    TextField { id: endpointField; Layout.fillWidth: true; text: backend.lmStudioEndpoint }

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

                    Text { text: "whisper.cpp executable"; color: "#c9def3"; font.pixelSize: 13 }
                    TextField { id: whisperPathField; Layout.fillWidth: true; text: backend.whisperExecutable }

                    Text { text: "Piper executable"; color: "#c9def3"; font.pixelSize: 13 }
                    TextField { id: piperPathField; Layout.fillWidth: true; text: backend.piperExecutable }

                    Text { text: "Piper voice model"; color: "#c9def3"; font.pixelSize: 13 }
                    TextField { id: voicePathField; Layout.fillWidth: true; text: backend.piperVoiceModel }

                    Text { text: "ffmpeg executable"; color: "#c9def3"; font.pixelSize: 13 }
                    TextField { id: ffmpegPathField; Layout.fillWidth: true; text: backend.ffmpegExecutable }

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
                        text: "Sensitivity and overlay interaction settings."
                        color: "#8099b8"
                        font.pixelSize: 14
                    }

                    Text { text: "Mic sensitivity"; color: "#c9def3"; font.pixelSize: 13 }
                    Slider { id: micSlider; Layout.fillWidth: true; from: 0.01; to: 0.10; value: backend.micSensitivity }

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
                                        whisperPathField.text,
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
        }
    }
}
