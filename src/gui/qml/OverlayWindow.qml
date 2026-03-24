import QtQuick
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
        if (text.length > 72) {
            text = text.slice(0, 69) + "..."
        }
        return text
    }

    function greetingLine() {
        const hour = new Date().getHours()
        const period = hour < 12 ? "Good morning" : hour >= 18 ? "Good evening" : "Good afternoon"
        return backend.userName.length > 0 ? period + ", " + backend.userName + "." : period + "."
    }

    function presenceLine() {
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

    function microStatus() {
        if (backend.stateName === "LISTENING") {
            return "Listening"
        }
        if (backend.stateName === "PROCESSING") {
            return "Processing"
        }
        if (backend.stateName === "SPEAKING") {
            return "Speaking"
        }
        return "Idle"
    }

    JarvisUi.AnimationController {
        id: motion
        stateName: backend.stateName
        inputLevel: backend.audioLevel
        overlayVisible: backend.overlayVisible
    }

    Item {
        anchors.fill: parent

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 32
            text: backend.assistantName + " · " + microStatus()
            color: "#dbeeff"
            opacity: 0.76
            font.pixelSize: 12
            font.letterSpacing: 2.1
        }

        JarvisUi.OrbRenderer {
            id: orb
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: -52
            width: Math.min(parent.width * 0.28, 560)
            height: width
            x: backend.presenceOffsetX * 12 + motion.listeningVibration * 3
            y: backend.presenceOffsetY * 12 + motion.listeningVibration * 2
            stateName: backend.stateName
            time: motion.time
            audioLevel: motion.inputBoost
            speakingLevel: motion.speakingSignal
            distortion: motion.distortion
            glow: motion.glow
            orbScale: motion.orbScale
            orbitalRotation: motion.orbitalRotation
        }

        Column {
            anchors.top: orb.bottom
            anchors.topMargin: 20
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(parent.width * 0.36, 520)
            spacing: 6
            x: backend.presenceOffsetX * 8
            y: backend.presenceOffsetY * 8

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: presenceLine()
                color: "#edf6ff"
                font.pixelSize: 28
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: compactText(backend.statusText, "")
                visible: text.length > 0 && text !== presenceLine()
                color: "#7f9fc7"
                font.pixelSize: 14
                wrapMode: Text.Wrap
                maximumLineCount: 1
                elide: Text.ElideRight
            }
        }

        JarvisUi.ToastManager {
            id: toastManager
            anchors.right: parent.right
            anchors.rightMargin: 28
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 30
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
