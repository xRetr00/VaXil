import QtQuick

Item {
    id: root

    visible: false
    width: 0
    height: 0

    property string stateName: "IDLE"
    property real inputLevel: 0.0
    property bool overlayVisible: false
    property int uiState: -1

    property real time: 0.0
    property real stateMorph: effectiveUiState
    property real targetInput: clamp(inputLevel * 9.5, 0.0, 1.0)
    property real smoothedInput: targetInput
    property real speakingTarget: clamp(
        executingAmount * (0.44
                           + 0.24 * wave(time * 7.8)
                           + 0.18 * wave(time * 12.6 + 1.2)
                           + smoothedInput * 0.2),
        0.0,
        1.0)
    property real speakingSignal: speakingTarget

    readonly property int stateIdle: 0
    readonly property int stateListening: 1
    readonly property int stateThinking: 2
    readonly property int stateExecuting: 3

    readonly property int effectiveUiState: uiState >= 0 ? uiState : mapUiState(stateName)
    readonly property real idleAmount: stateWeight(stateIdle)
    readonly property real listeningAmount: stateWeight(stateListening)
    readonly property real thinkingAmount: stateWeight(stateThinking)
    readonly property real executingAmount: stateWeight(stateExecuting)
    readonly property real inputBoost: clamp(smoothedInput * 1.04, 0.0, 1.0)
    readonly property real breathing: wave(time * (0.72 + thinkingAmount * 0.42 + executingAmount * 0.24))
    readonly property real thinkingDrift: wave(time * 1.36 + 0.7)
    readonly property real executionDrive: wave(time * 6.4 + smoothedInput * 4.2)
    readonly property real orbitalRotation: (time * (10 + thinkingAmount * 28 + executingAmount * 18)) % 360
    readonly property real orbScale: 0.94
        + idleAmount * (0.03 + breathing * 0.02)
        + listeningAmount * (0.05 + inputBoost * 0.08)
        + thinkingAmount * (0.08 + thinkingDrift * 0.03)
        + executingAmount * (0.09 + speakingSignal * 0.07)
    readonly property real distortion: 0.06
        + idleAmount * 0.02
        + listeningAmount * (0.18 + inputBoost * 0.24)
        + thinkingAmount * 0.2
        + executingAmount * (0.12 + speakingSignal * 0.2)
    readonly property real glow: 0.24
        + idleAmount * 0.1
        + listeningAmount * (0.14 + inputBoost * 0.18)
        + thinkingAmount * 0.22
        + executingAmount * (0.24 + speakingSignal * 0.18)
    readonly property real auraPulse: 0.28
        + breathing * 0.16
        + thinkingDrift * 0.12
        + speakingSignal * 0.24
        + inputBoost * 0.18
    readonly property real flicker: wave(time * 14.0 + inputBoost * 6.5 + executingAmount * 2.0)
    readonly property real listeningVibrationX: listeningAmount
        * (0.45 + inputBoost * 1.3)
        * (Math.sin(time * 46.0) + Math.sin(time * 73.0 + 0.9)) * 0.5
    readonly property real listeningVibrationY: listeningAmount
        * (0.4 + inputBoost * 1.1)
        * (Math.cos(time * 39.0 + 0.4) + Math.sin(time * 61.0 + 1.3)) * 0.5

    function clamp(value, minValue, maxValue) {
        return Math.max(minValue, Math.min(maxValue, value))
    }

    function wave(value) {
        return 0.5 + 0.5 * Math.sin(value)
    }

    function stateWeight(targetIndex) {
        return clamp(1.0 - Math.abs(stateMorph - targetIndex), 0.0, 1.0)
    }

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

    onEffectiveUiStateChanged: {
        stateMorph = effectiveUiState
    }

    NumberAnimation on time {
        from: 0
        to: 20000
        duration: 12000000
        loops: Animation.Infinite
        running: root.overlayVisible
    }

    Behavior on stateMorph {
        NumberAnimation {
            duration: 360
            easing.type: Easing.OutCubic
        }
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
