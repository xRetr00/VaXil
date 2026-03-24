import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Window {
    id: wizard
    width: 760
    height: 560
    visible: false
    title: backend.assistantName + " Setup"
    color: "#09111a"
    onClosing: function(close) {
        close.accepted = false
        hide()
    }

    property int stepIndex: 0

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#122437" }
            GradientStop { position: 1.0; color: "#07111a" }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 18

        Label {
            text: backend.assistantName + " Initial Setup"
            color: "#e5f8ff"
            font.pixelSize: 28
            font.bold: true
        }

        Label {
            text: "Complete the essentials before the overlay starts."
            color: "#8db4ca"
            font.pixelSize: 14
        }

        ProgressBar {
            Layout.fillWidth: true
            from: 0
            to: 3
            value: stepIndex + 1
        }

        StackLayout {
            id: steps
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: wizard.stepIndex

            ColumnLayout {
                spacing: 12
                Label { text: "Step 1 of 4"; color: "#6da1bb" }
                Label { text: "User Profile"; color: "#e5f8ff"; font.pixelSize: 22; font.bold: true }
                Label {
                    text: "Set the basic identity your assistant will use immediately."
                    color: "#9bb8c7"
                    wrapMode: Text.Wrap
                }
                Label { text: "Your name"; color: "#d8f1ff" }
                TextField {
                    id: userNameField
                    text: backend.userName
                    placeholderText: "How should " + backend.assistantName + " address you?"
                }
            }

            ColumnLayout {
                spacing: 12
                Label { text: "Step 2 of 4"; color: "#6da1bb" }
                Label { text: "AI Core"; color: "#e5f8ff"; font.pixelSize: 22; font.bold: true }
                Label { text: "Confirm the LM Studio endpoint and preferred model."; color: "#9bb8c7"; wrapMode: Text.Wrap }
                Label { text: "LM Studio endpoint"; color: "#d8f1ff" }
                TextField { id: endpointField; text: backend.lmStudioEndpoint }
                RowLayout {
                    Button { text: "Refresh Models"; onClicked: backend.refreshModels() }
                    ComboBox {
                        id: modelCombo
                        Layout.fillWidth: true
                        model: backend.models
                        Component.onCompleted: {
                            const index = backend.models.indexOf(backend.selectedModel)
                            if (index >= 0) currentIndex = index
                        }
                    }
                }
            }

            ColumnLayout {
                spacing: 12
                Label { text: "Step 3 of 4"; color: "#6da1bb" }
                Label { text: "Voice Tooling"; color: "#e5f8ff"; font.pixelSize: 22; font.bold: true }
                Label { text: "Point JARVIS to the local speech binaries and voice model."; color: "#9bb8c7"; wrapMode: Text.Wrap }
                Label { text: "whisper.cpp executable"; color: "#d8f1ff" }
                TextField { id: whisperPathField; text: backend.whisperExecutable }
                Label { text: "Piper executable"; color: "#d8f1ff" }
                TextField { id: piperPathField; text: backend.piperExecutable }
                Label { text: "Piper voice model"; color: "#d8f1ff" }
                TextField { id: voicePathField; text: backend.piperVoiceModel }
                Label { text: "ffmpeg executable"; color: "#d8f1ff" }
                TextField { id: ffmpegPathField; text: backend.ffmpegExecutable }
            }

            ColumnLayout {
                spacing: 12
                Label { text: "Step 4 of 4"; color: "#6da1bb" }
                Label { text: "Overlay Behavior"; color: "#e5f8ff"; font.pixelSize: 22; font.bold: true }
                Label { text: "Finalize the desktop overlay defaults."; color: "#9bb8c7"; wrapMode: Text.Wrap }
                CheckBox {
                    id: clickThroughCheck
                    text: "Enable click-through overlay by default"
                    checked: backend.clickThroughEnabled
                }
                Label {
                    text: "Finish setup to save your base configuration and unlock the assistant."
                    color: "#d8f1ff"
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
                text: wizard.stepIndex === 3 ? "Finish Setup" : "Next"
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
