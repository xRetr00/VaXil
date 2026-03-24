import QtQuick

Item {
    id: root

    visible: false
    width: 0
    height: 0

    property string stateName: "IDLE"
    property real inputLevel: 0.0
    property bool overlayVisible: false

    property real time: 0.0
    property real smoothedInput: 0.0
    property real speakingSignal: 0.0
    property real jitterSignal: 0.0

    readonly property real idleAmount: stateName === "IDLE" ? 1.0 : 0.0
    readonly property real listeningAmount: stateName === "LISTENING" ? 1.0 : 0.0
    readonly property real processingAmount: stateName === "PROCESSING" ? 1.0 : 0.0
    readonly property real speakingAmount: stateName === "SPEAKING" ? 1.0 : 0.0
    readonly property real inputBoost: Math.min(1.0, smoothedInput * 8.5)
    readonly property real idleBreath: 0.5 + 0.5 * Math.sin(time * 1.05)
    readonly property real orbitalRotation: (time * (26 + processingAmount * 70)) % 360
    readonly property real orbScale: 0.96
        + idleAmount * (0.03 + idleBreath * 0.015)
        + listeningAmount * (0.05 + inputBoost * 0.1)
        + processingAmount * 0.08
        + speakingAmount * (0.06 + speakingSignal * 0.08)
    readonly property real distortion: 0.08
        + idleAmount * 0.03
        + listeningAmount * (0.16 + inputBoost * 0.3)
        + processingAmount * 0.18
        + speakingAmount * (0.12 + speakingSignal * 0.18)
    readonly property real glow: 0.24
        + idleAmount * 0.08
        + listeningAmount * (0.14 + inputBoost * 0.18)
        + processingAmount * 0.18
        + speakingAmount * (0.24 + speakingSignal * 0.2)
    readonly property real auraPulse: 0.36 + idleBreath * 0.16 + speakingSignal * 0.22 + inputBoost * 0.16
    readonly property real listeningVibration: listeningAmount * (0.8 + inputBoost * 1.4) * jitterSignal

    Timer {
        id: frameDriver
        running: root.overlayVisible
        repeat: true
        interval: root.stateName === "IDLE" ? 33 : 16

        property double lastTick: 0

        onTriggered: {
            const now = Date.now()
            if (lastTick === 0) {
                lastTick = now
                return
            }

            const dt = Math.min(0.05, (now - lastTick) / 1000.0)
            lastTick = now

            root.time += dt

            const targetInput = Math.max(0.0, Math.min(1.0, root.inputLevel * 9.0))
            root.smoothedInput += (targetInput - root.smoothedInput) * Math.min(1.0, dt * 10.0)

            const waveform = 0.46
                + 0.34 * Math.sin(root.time * 7.2)
                + 0.2 * Math.sin(root.time * 12.6 + 1.7)
            const targetSpeech = root.stateName === "SPEAKING" ? Math.max(0.0, waveform) : 0.0
            root.speakingSignal += (targetSpeech - root.speakingSignal) * Math.min(1.0, dt * 8.5)
            root.jitterSignal = Math.sin(root.time * 33.0) * 0.5 + Math.sin(root.time * 51.0 + 0.7) * 0.5
        }

        onRunningChanged: {
            if (!running) {
                lastTick = 0
            }
        }
    }
}
