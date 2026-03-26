import QtQuick

Item {
    id: root

    property string stateName: "IDLE"
    property int uiState: -1
    property real time: 0.0
    property real audioLevel: 0.0
    property real speakingLevel: 0.0
    property real distortion: 0.12
    property real glow: 0.3
    property real orbScale: 1.0
    property real orbitalRotation: 0.0
    property real auraPulse: 0.4
    property real flicker: 0.5
    property int quality: qualityHigh

    readonly property int qualityLow: 0
    readonly property int qualityMedium: 1
    readonly property int qualityHigh: 2
    readonly property real clampedAudio: Math.max(0.0, Math.min(1.0, audioLevel))
    readonly property real clampedSpeaking: Math.max(0.0, Math.min(1.0, speakingLevel))
    readonly property real effectiveUiState: uiState >= 0 ? uiState : stateIndex

    readonly property real stateIndex: stateName === "LISTENING" ? 1.0
        : stateName === "PROCESSING" || stateName === "THINKING" ? 2.0
        : stateName === "SPEAKING" || stateName === "EXECUTING" ? 3.0
        : 0.0

    implicitWidth: 250
    implicitHeight: 250

    ShaderEffect {
        id: auraLayer
        anchors.centerIn: parent
        width: parent.width * 1.22
        height: width
        blending: true

        property real time: root.time * 0.62 + 5.3
        property real audioLevel: root.clampedAudio * 0.52
        property real speaking: root.clampedSpeaking * 0.34
        property real uiState: root.effectiveUiState
        property real quality: root.quality
        property vector2d resolution: Qt.vector2d(width, height)
        property real layerRole: 2.0
        property real distortion: root.distortion * 1.18
        property real intensity: 0.82 + root.glow * 0.34 + root.auraPulse * 0.2
        property real pulseAmount: 0.52 + root.auraPulse * 0.42
        property real hueShift: 0.1 + root.flicker * 0.05

        fragmentShader: "qrc:/qt/qml/JARVIS/gui/shaders/src/gui/shaders/orb.frag.qsb"
        opacity: 0.18 + root.glow * 0.2
        scale: root.orbScale * (1.16 + root.auraPulse * 0.16 + root.clampedAudio * 0.04)
    }

    ShaderEffect {
        id: energyLayer
        anchors.centerIn: parent
        width: parent.width * 0.92
        height: width
        blending: true

        property real time: root.time * 1.16 + 11.0
        property real audioLevel: root.clampedAudio * 0.98
        property real speaking: root.clampedSpeaking * 0.78
        property real uiState: root.effectiveUiState
        property real quality: root.quality
        property vector2d resolution: Qt.vector2d(width, height)
        property real layerRole: 1.0
        property real distortion: root.distortion
        property real intensity: 1.0 + root.glow * 0.22 + root.flicker * 0.08
        property real pulseAmount: 0.7 + root.auraPulse * 0.3
        property real hueShift: 0.04

        fragmentShader: "qrc:/qt/qml/JARVIS/gui/shaders/src/gui/shaders/orb.frag.qsb"
        opacity: 0.66 + root.glow * 0.18
        scale: root.orbScale * (0.98 + root.clampedAudio * 0.08 + root.auraPulse * 0.04)
    }

    ShaderEffect {
        id: coreLayer
        anchors.centerIn: parent
        width: parent.width * 0.58
        height: width
        blending: true

        property real time: root.time * 1.58 + 22.0
        property real audioLevel: root.clampedAudio * 0.72
        property real speaking: root.clampedSpeaking * 1.06
        property real uiState: root.effectiveUiState
        property real quality: root.quality
        property vector2d resolution: Qt.vector2d(width, height)
        property real layerRole: 0.0
        property real distortion: root.distortion * 0.62
        property real intensity: 1.18 + root.glow * 0.14 + root.clampedSpeaking * 0.24
        property real pulseAmount: 0.84 + root.clampedSpeaking * 0.28
        property real hueShift: -0.02

        fragmentShader: "qrc:/qt/qml/JARVIS/gui/shaders/src/gui/shaders/orb.frag.qsb"
        opacity: 0.84 + root.clampedSpeaking * 0.18
        scale: root.orbScale * (0.8 + root.clampedAudio * 0.1 + root.clampedSpeaking * 0.08)
    }
}
