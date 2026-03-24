import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import "." as JarvisUi

Window {
    id: root

    width: 1920
    height: 1080
    visible: false
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool | Qt.NoDropShadowWindowHint

    onClosing: function(close) {
        close.accepted = false
        hide()
    }

    function compactText(rawText, fallbackText) {
        let text = (rawText || "").toString().replace(/\s+/g, " ").trim()
        if (text.length === 0) {
            text = fallbackText || ""
        }
        if (text.length > 84) {
            text = text.slice(0, 81) + "..."
        }
        return text
    }

    function greetingLine() {
        const hour = new Date().getHours()
        const period = hour < 12 ? "Good morning" : hour >= 18 ? "Good evening" : "Good afternoon"
        return backend.userName.length > 0 ? period + ", " + backend.userName + "." : period + "."
    }

    function primaryLine() {
        if (backend.responseText.length > 0) {
            return compactText(backend.responseText, "")
        }
        if (backend.transcript.length > 0) {
            return compactText(backend.transcript, "")
        }
        if (backend.stateName === "IDLE") {
            return greetingLine()
        }
        return compactText(backend.statusText, "Ready.")
    }

    function secondaryLine() {
        if (backend.stateName === "LISTENING") {
            return "Listening for your next phrase."
        }
        if (backend.stateName === "PROCESSING") {
            return "Working through it."
        }
        if (backend.stateName === "SPEAKING") {
            return "Delivering the response."
        }
        return compactText(backend.statusText, "Standing by.")
    }

    JarvisUi.AnimationController {
        id: motion
        stateName: backend.stateName
        inputLevel: backend.audioLevel
        overlayVisible: backend.overlayVisible
    }

    Item {
        anchors.fill: parent

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 34
            width: 232
            height: 42
            radius: 21
            color: "#8a0a111d"
            border.width: 1
            border.color: "#314c71"
            opacity: 0.96

            Row {
                anchors.centerIn: parent
                spacing: 12

                Rectangle {
                    width: 8
                    height: 8
                    radius: 4
                    color: backend.stateName === "LISTENING" ? "#7feeff"
                        : backend.stateName === "PROCESSING" ? "#9ab3ff"
                        : backend.stateName === "SPEAKING" ? "#f2a0ff"
                        : "#8fa4c8"
                }

                Text {
                    text: backend.assistantName + "  ·  " + backend.stateName
                    color: "#dfefff"
                    font.pixelSize: 12
                    font.letterSpacing: 1.7
                }
            }
        }

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: -96
            width: 740
            height: 740
            radius: 370
            color: "#17304d"
            opacity: 0.06 + motion.glow * 0.04
            scale: 0.96 + motion.glow * 0.08
        }

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: -96
            width: 520
            height: 520
            radius: 260
            color: "#7bcfff"
            opacity: 0.035 + motion.glow * 0.03
            scale: 0.98 + motion.glow * 0.04
        }

        JarvisUi.OrbRenderer {
            id: orb
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: -96
            width: 460
            height: 460
            stateName: backend.stateName
            time: motion.time
            audioLevel: motion.inputBoost
            speakingLevel: motion.speakingSignal
            distortion: motion.distortion
            glow: motion.glow
            orbScale: motion.orbScale
            orbitalRotation: motion.orbitalRotation
        }

        MouseArea {
            anchors.fill: orb
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                if (backend.stateName === "LISTENING" || backend.stateName === "PROCESSING") {
                    backend.cancelRequest()
                } else {
                    backend.startListening()
                }
            }
        }

        Column {
            anchors.top: orb.bottom
            anchors.topMargin: 18
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(parent.width * 0.58, 760)
            spacing: 8

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: primaryLine()
                color: "#eef7ff"
                font.pixelSize: 42
                font.weight: Font.Normal
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: secondaryLine()
                color: "#8aa7cf"
                font.pixelSize: 16
                wrapMode: Text.Wrap
                maximumLineCount: 1
                elide: Text.ElideRight
            }
        }

        Rectangle {
            id: inputRibbon
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 58
            width: Math.min(parent.width * 0.42, 620)
            height: 78
            radius: 39
            color: "#7f08111a"
            border.width: 1
            border.color: backend.stateName === "LISTENING" ? "#4ea6ff" : "#29476b"
            opacity: 0.97

            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                color: "transparent"
                border.width: 1
                border.color: "#2292d8ff"
                opacity: 0.1 + motion.glow * 0.1
            }

            RowLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 12

                Rectangle {
                    Layout.preferredWidth: 56
                    Layout.preferredHeight: 56
                    radius: 28
                    color: backend.stateName === "LISTENING" ? "#123f69" : "#101c2e"
                    border.width: 1
                    border.color: backend.stateName === "LISTENING" ? "#8fe9ff" : "#29476b"

                    Text {
                        anchors.centerIn: parent
                        text: backend.stateName === "LISTENING" ? "■" : "•"
                        color: "#effbff"
                        font.pixelSize: backend.stateName === "LISTENING" ? 16 : 26
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (backend.stateName === "LISTENING" || backend.stateName === "PROCESSING") {
                                backend.cancelRequest()
                            } else {
                                backend.startListening()
                            }
                        }
                    }
                }

                TextField {
                    id: promptField
                    Layout.fillWidth: true
                    color: "#eff7ff"
                    font.pixelSize: 17
                    placeholderText: "Ask naturally"
                    placeholderTextColor: "#6d89ad"
                    selectByMouse: true
                    background: Item {}
                    onAccepted: {
                        const prompt = text.trim()
                        if (prompt.length === 0) {
                            return
                        }
                        backend.submitText(prompt)
                        text = ""
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 56
                    Layout.preferredHeight: 56
                    radius: 28
                    color: "#142a46"
                    border.width: 1
                    border.color: "#3a6fa4"

                    Text {
                        anchors.centerIn: parent
                        text: "↗"
                        color: "#effbff"
                        font.pixelSize: 18
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            const prompt = promptField.text.trim()
                            if (prompt.length === 0) {
                                return
                            }
                            backend.submitText(prompt)
                            promptField.text = ""
                        }
                    }
                }
            }
        }

        JarvisUi.ToastManager {
            id: toastManager
            anchors.right: parent.right
            anchors.rightMargin: 34
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 40
        }
    }

    Connections {
        target: backend

        function onStatusTextChanged() {
            if (backend.statusText.length === 0) {
                return
            }
            toastManager.pushToast(backend.statusText, backend.statusText.toLowerCase().indexOf("error") >= 0 ? "error" : "status")
        }

        function onResponseTextChanged() {
            if (backend.responseText.length === 0) {
                return
            }
            toastManager.pushToast(backend.responseText, "response")
        }
    }
}
