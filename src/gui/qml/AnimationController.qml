import QtQuick

Item {
    id: root

    visible: false
    width: 0
    height: 0

    property string stateName: "IDLE"
    property real inputLevel: 0.0
    property bool overlayVisible: false
    property int uiState: mapUiState(stateName)

    property real time: 0.0
    property real targetInput: Math.max(0.0, Math.min(1.0, inputLevel * 9.0))
    property real smoothedInput: targetInput
    property real speakingTarget: executingAmount * (0.52 + 0.33 * Math.sin(time * 8.4) + 0.15 * Math.sin(time * 13.8 + 1.2))
    property real speakingSignal: speakingTarget
    property real jitterSignal: Math.sin(time * 33.0) * 0.5 + Math.sin(time * 51.0 + 0.7) * 0.5

    readonly property int stateIdle: 0
    readonly property int stateListening: 1
    readonly property int stateThinking: 2
    readonly property int stateExecuting: 3

    readonly property real idleAmount: uiState === stateIdle ? 1.0 : 0.0
    readonly property real listeningAmount: uiState === stateListening ? 1.0 : 0.0
    readonly property real thinkingAmount: uiState === stateThinking ? 1.0 : 0.0
    readonly property real executingAmount: uiState === stateExecuting ? 1.0 : 0.0
    readonly property real inputBoost: Math.min(1.0, smoothedInput * 8.5)
    readonly property real idleBreath: 0.5 + 0.5 * Math.sin(time * 1.05)
    readonly property real orbitalRotation: (time * (22 + thinkingAmount * 78 + executingAmount * 54)) % 360
    readonly property real orbScale: 0.96
        + idleAmount * (0.03 + idleBreath * 0.015)
        + listeningAmount * (0.05 + inputBoost * 0.1)
        + thinkingAmount * 0.08
        + executingAmount * (0.06 + speakingSignal * 0.08)
    readonly property real distortion: 0.08
        + idleAmount * 0.03
        + listeningAmount * (0.16 + inputBoost * 0.3)
        + thinkingAmount * 0.18
        + executingAmount * (0.12 + speakingSignal * 0.18)
    readonly property real glow: 0.24
        + idleAmount * 0.08
        + listeningAmount * (0.14 + inputBoost * 0.18)
        + thinkingAmount * 0.18
        + executingAmount * (0.24 + speakingSignal * 0.2)
    readonly property real auraPulse: 0.36 + idleBreath * 0.16 + speakingSignal * 0.22 + inputBoost * 0.16
    readonly property real listeningVibration: listeningAmount * (0.8 + inputBoost * 1.4) * jitterSignal

    function mapUiState(name) {
        const normalized = (name || "").toString().trim().toUpperCase()
        if (normalized === "LISTENING") {
            return stateListening
        }
        if (normalized === "PROCESSING" || normalized === "THINKING") {
            return stateThinking
        }
        if (normalized === "SPEAKING" || normalized === "EXECUTING") {
            return stateExecuting
        }
        return stateIdle
    }

    onStateNameChanged: {
        uiState = mapUiState(stateName)
    }

    NumberAnimation on time {
        from: 0
        to: 20000
        duration: 12000000
        loops: Animation.Infinite
        running: root.overlayVisible
    }

    Behavior on smoothedInput {
        NumberAnimation {
            duration: 130
            easing.type: Easing.OutCubic
        }
    }

    Behavior on speakingSignal {
        NumberAnimation {
            duration: 140
            easing.type: Easing.OutCubic
        }
    }

    onTargetInputChanged: {
        smoothedInput = targetInput
    }

    onSpeakingTargetChanged: {
        speakingSignal = Math.max(0.0, speakingTarget)
    }
}
