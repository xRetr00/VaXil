import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import "." as JarvisUi

Window {
    id: wizard

    width: 860
    height: 720
    visible: false
    title: backend.assistantName + " Setup"
    color: "#050912"

    onClosing: function(close) {
        close.accepted = false
        hide()
    }

    property int stepIndex: 0

    function syncVoiceFieldsFromBackend() {
        whisperPathField.text = backend.whisperExecutable
        piperPathField.text = backend.piperExecutable
        voicePathField.text = backend.piperVoiceModel
        ffmpegPathField.text = backend.ffmpegExecutable
    }

    onVisibleChanged: {
        if (visible) {
            syncVoiceFieldsFromBackend()
        }
    }

    JarvisUi.AnimationController {
        id: setupMotion
        stateName: stepIndex === 0 ? "IDLE" : stepIndex === 1 ? "PROCESSING" : stepIndex === 2 ? "LISTENING" : "SPEAKING"
        inputLevel: 0.04
        overlayVisible: wizard.visible
    }

    Rectangle {
        anchors.fill: parent
        color: "#050912"
    }

    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 280
        color: "#091224"
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 26
        spacing: 22

        Rectangle {
            Layout.preferredWidth: 280
            Layout.fillHeight: true
            radius: 36
            color: "#9f08111d"
            border.width: 1
            border.color: "#20314e"

            Column {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 18

                JarvisUi.OrbRenderer {
                    width: 220
                    height: 220
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
                    color: "#eff7ff"
                    font.pixelSize: 30
                    font.weight: Font.Medium
                    horizontalAlignment: Text.AlignHCenter
                }

                Text {
                    width: parent.width
                    text: "Shape the local stack before the assistant enters the overlay."
                    color: "#8ca5c5"
                    font.pixelSize: 14
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                }

                Column {
                    width: parent.width
                    spacing: 10

                    Repeater {
                        model: 4

                        delegate: Rectangle {
                            required property int index

                            width: parent.width
                            height: 44
                            radius: 22
                            color: wizard.stepIndex === index ? "#17375d" : "#0d1829"
                            border.width: 1
                            border.color: wizard.stepIndex === index ? "#5ba5ff" : "#213754"

                            Text {
                                anchors.centerIn: parent
                                text: index === 0 ? "Profile"
                                    : index === 1 ? "AI Core"
                                    : index === 2 ? "Voice"
                                    : "Overlay"
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
            radius: 36
            color: "#9208111d"
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
                        : "Presence"
                    color: "#f0f8ff"
                    font.pixelSize: 28
                    font.weight: Font.Medium
                }

                Text {
                    text: wizard.stepIndex === 0 ? "Define how the assistant should address you."
                        : wizard.stepIndex === 1 ? "Point J.A.R.V.I.S at LM Studio and choose a model."
                        : wizard.stepIndex === 2 ? "Connect whisper.cpp, Piper, and the voice model."
                        : "Set the overlay’s starting behavior."
                    color: "#89a3c4"
                    font.pixelSize: 14
                    wrapMode: Text.Wrap
                }

                ProgressBar {
                    Layout.fillWidth: true
                    from: 0
                    to: 4
                    value: wizard.stepIndex + 1
                }

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: wizard.stepIndex

                    ColumnLayout {
                        spacing: 14
                        Text { text: "Your name"; color: "#d0e3f5"; font.pixelSize: 13 }
                        TextField {
                            id: userNameField
                            Layout.fillWidth: true
                            text: backend.userName
                            placeholderText: "How should " + backend.assistantName + " address you?"
                        }
                    }

                    ColumnLayout {
                        spacing: 14
                        Text { text: "LM Studio endpoint"; color: "#d0e3f5"; font.pixelSize: 13 }
                        TextField { id: endpointField; Layout.fillWidth: true; text: backend.lmStudioEndpoint }
                        RowLayout {
                            Layout.fillWidth: true

                            Rectangle {
                                Layout.preferredWidth: 150
                                Layout.preferredHeight: 48
                                radius: 24
                                color: "#15253c"
                                border.width: 1
                                border.color: "#2a4667"

                                Text {
                                    anchors.centerIn: parent
                                    text: "Refresh models"
                                    color: "#edf8ff"
                                    font.pixelSize: 14
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: backend.refreshModels()
                                }
                            }

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
                            }
                        }
                    }

                    ColumnLayout {
                        spacing: 14
                        Text { text: "whisper.cpp executable"; color: "#d0e3f5"; font.pixelSize: 13 }
                        TextField { id: whisperPathField; Layout.fillWidth: true; text: backend.whisperExecutable }
                        Text { text: "Piper executable"; color: "#d0e3f5"; font.pixelSize: 13 }
                        TextField { id: piperPathField; Layout.fillWidth: true; text: backend.piperExecutable }
                        Text { text: "Piper voice model"; color: "#d0e3f5"; font.pixelSize: 13 }
                        TextField { id: voicePathField; Layout.fillWidth: true; text: backend.piperVoiceModel }
                        Text { text: "ffmpeg executable"; color: "#d0e3f5"; font.pixelSize: 13 }
                        TextField { id: ffmpegPathField; Layout.fillWidth: true; text: backend.ffmpegExecutable }

                        RowLayout {
                            Layout.fillWidth: true

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 44
                                radius: 22
                                color: "#142338"
                                border.width: 1
                                border.color: "#2a4667"

                                Text {
                                    anchors.centerIn: parent
                                    text: "Auto-detect tools"
                                    color: "#edf8ff"
                                    font.pixelSize: 13
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        backend.autoDetectVoiceTools()
                                        wizard.syncVoiceFieldsFromBackend()
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 44
                                radius: 22
                                color: "#183657"
                                border.width: 1
                                border.color: "#4d8fd1"

                                Text {
                                    anchors.centerIn: parent
                                    text: "Install missing tools"
                                    color: "#f2fbff"
                                    font.pixelSize: 13
                                    font.weight: Font.Medium
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        backend.installAndDetectVoiceTools()
                                        wizard.syncVoiceFieldsFromBackend()
                                    }
                                }
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: backend.toolInstallStatus.length > 0 ? backend.toolInstallStatus : "Auto-detection checks PATH and local tool folders."
                            color: "#9ab0ca"
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                        }
                    }

                    ColumnLayout {
                        spacing: 14
                        CheckBox {
                            id: clickThroughCheck
                            text: "Enable click-through overlay by default"
                            checked: backend.clickThroughEnabled
                        }

                        Text {
                            text: "You can still change all of this later from the tray menu."
                            color: "#9ab0ca"
                            font.pixelSize: 14
                            wrapMode: Text.Wrap
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true

                    Rectangle {
                        Layout.preferredWidth: 130
                        Layout.preferredHeight: 50
                        radius: 25
                        color: wizard.stepIndex > 0 ? "#15253c" : "#0d1521"
                        border.width: 1
                        border.color: wizard.stepIndex > 0 ? "#2a4667" : "#1a293e"
                        opacity: wizard.stepIndex > 0 ? 1.0 : 0.45

                        Text {
                            anchors.centerIn: parent
                            text: "Back"
                            color: "#edf8ff"
                            font.pixelSize: 14
                        }

                        MouseArea {
                            anchors.fill: parent
                            enabled: wizard.stepIndex > 0
                            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                            onClicked: wizard.stepIndex--
                        }
                    }

                    Item { Layout.fillWidth: true }

                    Rectangle {
                        Layout.preferredWidth: 168
                        Layout.preferredHeight: 50
                        radius: 25
                        color: "#183657"
                        border.width: 1
                        border.color: "#4d8fd1"

                        Text {
                            anchors.centerIn: parent
                            text: wizard.stepIndex === 3 ? "Finish setup" : "Continue"
                            color: "#f2fbff"
                            font.pixelSize: 14
                            font.weight: Font.Medium
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (wizard.stepIndex < 3) {
                                    wizard.stepIndex++
                                    return
                                }

                                backend.completeInitialSetup(
                                    userNameField.text,
                                    endpointField.text,
                                    modelCombo.currentText,
                                    whisperPathField.text,
                                    piperPathField.text,
                                    voicePathField.text,
                                    ffmpegPathField.text,
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
