import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Qt5Compat.GraphicalEffects

Window {
    id: root
    width: 560
    height: 720
    visible: false
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool
    onClosing: function(close) {
        close.accepted = false
        hide()
    }

    Rectangle {
        anchors.fill: parent
        color: "transparent"

        Rectangle {
            anchors.centerIn: parent
            width: 440
            height: 620
            radius: 42
            color: "#07111dcc"
            border.color: "#44c8ff"
            border.width: 1

            RadialGradient {
                anchors.fill: parent
                horizontalRadius: width * 0.75
                verticalRadius: height * 0.75
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#183b56" }
                    GradientStop { position: 0.5; color: "#0d1f31" }
                    GradientStop { position: 1.0; color: "#04080d" }
                }
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 28
                spacing: 18

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 280

                    Rectangle {
                        id: outerRing
                        anchors.centerIn: parent
                        width: 220
                        height: 220
                        radius: width / 2
                        color: "transparent"
                        border.width: 2
                        border.color: "#4df0ff"
                        opacity: backend.stateName === "PROCESSING" ? 0.95 : 0.65
                        rotation: backend.stateName === "PROCESSING" ? 360 : 0

                        SequentialAnimation on rotation {
                            running: backend.stateName === "PROCESSING"
                            loops: Animation.Infinite
                            NumberAnimation { from: 0; to: 360; duration: 2200; easing.type: Easing.Linear }
                        }
                    }

                    Rectangle {
                        id: orb
                        anchors.centerIn: parent
                        width: backend.stateName === "LISTENING" ? 142 : backend.stateName === "SPEAKING" ? 152 : 132
                        height: width
                        radius: width / 2
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: "#8ef7ff" }
                            GradientStop { position: 0.45; color: "#2cc5ff" }
                            GradientStop { position: 1.0; color: "#0f4f89" }
                        }
                        scale: 1.0 + backend.audioLevel * 1.8
                        opacity: 0.92

                        Behavior on width { NumberAnimation { duration: 180; easing.type: Easing.OutQuad } }
                        Behavior on scale { NumberAnimation { duration: 80; easing.type: Easing.OutQuad } }

                        SequentialAnimation on opacity {
                            running: backend.stateName === "IDLE"
                            loops: Animation.Infinite
                            NumberAnimation { from: 0.72; to: 0.96; duration: 1800; easing.type: Easing.InOutSine }
                            NumberAnimation { from: 0.96; to: 0.72; duration: 1800; easing.type: Easing.InOutSine }
                        }
                    }

                    Glow {
                        anchors.fill: orb
                        source: orb
                        radius: 42
                        samples: 24
                        color: backend.stateName === "LISTENING" ? "#7cffd6" : "#33d0ff"
                        spread: 0.35
                    }

                    Repeater {
                        model: 28
                        delegate: Rectangle {
                            width: 5
                            height: backend.stateName === "LISTENING" ? 24 + Math.abs(Math.sin((index + 1) * 0.4 + backend.audioLevel * 14.0)) * 70 : 14
                            radius: width / 2
                            color: "#83e9ff"
                            opacity: backend.stateName === "LISTENING" ? 0.92 : 0.22
                            anchors.centerIn: parent
                            transform: [
                                Translate { y: -122 },
                                Rotation { angle: index * (360 / 28); origin.x: 2.5; origin.y: 122 }
                            ]
                        }
                    }
                }

                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: "JARVIS"
                    color: "#d9f7ff"
                    font.pixelSize: 34
                    font.letterSpacing: 5
                    font.bold: true
                }

                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: backend.stateName + "  •  " + backend.statusText
                    color: "#86b8cf"
                    font.pixelSize: 14
                    horizontalAlignment: Text.AlignHCenter
                }

                TextArea {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 84
                    readOnly: true
                    wrapMode: Text.Wrap
                    text: backend.responseText.length > 0 ? backend.responseText : backend.transcript
                    color: "#ecfbff"
                    placeholderText: "Say something or type below..."
                    background: Rectangle {
                        radius: 18
                        color: "#0c1926"
                        border.color: "#1e3d55"
                    }
                }

                TextField {
                    id: inputField
                    Layout.fillWidth: true
                    placeholderText: "Type a message"
                    color: "#ecfbff"
                    placeholderTextColor: "#6b90a6"
                    background: Rectangle {
                        radius: 18
                        color: "#08131d"
                        border.color: "#21516c"
                    }
                    onAccepted: {
                        backend.submitText(text)
                        text = ""
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Button {
                        Layout.fillWidth: true
                        text: "Listen"
                        onClicked: backend.startListening()
                    }

                    Button {
                        Layout.fillWidth: true
                        text: "Send"
                        onClicked: {
                            backend.submitText(inputField.text)
                            inputField.text = ""
                        }
                    }

                    Button {
                        Layout.fillWidth: true
                        text: "Cancel"
                        onClicked: backend.cancelRequest()
                    }
                }
            }
        }
    }
}
