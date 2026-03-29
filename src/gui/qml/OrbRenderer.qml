import QtQuick

Item {
    id: root

    implicitWidth: 320
    implicitHeight: 320

    readonly property int qualityLow: 0
    readonly property int qualityMedium: 1
    readonly property int qualityHigh: 2

    property string stateName: "IDLE"
    property int uiState: -1
    property real time: 0.0
    property real audioLevel: 0.0
    property real speakingLevel: 0.0
    readonly property real orbState: uiState >= 0 ? uiState : mapState(stateName)
    property int quality: qualityMedium

    property real distortion: 0.0
    property real glow: 0.0
    property real orbScale: 1.0
    property real orbitalRotation: 0.0
    property real auraPulse: 0.0
    property real flicker: 0.0

    readonly property real reactiveAudio: clamp(Math.max(audioLevel, speakingLevel * 0.92), 0.0, 1.0)
    readonly property real reactiveGlow: clamp(glow, 0.0, 1.0)
    readonly property real reactiveDistortion: clamp(distortion, 0.0, 1.0)
    readonly property real reactivePulse: clamp(auraPulse, 0.0, 1.0)
    readonly property real reactiveSpeaking: clamp(speakingLevel, 0.0, 1.0)
    readonly property real reactiveFlicker: clamp(flicker, 0.0, 1.0)
    readonly property real effectScale: Math.max(0.72, orbScale * (0.98 + reactivePulse * 0.05 + reactiveGlow * 0.02))
    readonly property real effectOpacity: clamp(0.86 + reactiveGlow * 0.16, 0.0, 1.0)

    function clamp(value, minValue, maxValue) {
        return Math.max(minValue, Math.min(maxValue, value))
    }

    function mapState(name) {
        const normalized = (name || "").toString().trim().toUpperCase()
        if (normalized === "LISTENING") {
            return 1
        }
        if (normalized === "PROCESSING" || normalized === "THINKING") {
            return 2
        }
        if (normalized === "SPEAKING" || normalized === "EXECUTING") {
            return 3
        }
        return 0
    }

    ShaderEffect {
        anchors.fill: parent
        blending: true
        fragmentShader: "qrc:/qt/qml/VAXIL/gui/shaders/src/gui/shaders/orb.frag.qsb"
        property real time: root.time
        property vector2d resolution: Qt.vector2d(width, height)
        property real audioLevel: root.reactiveAudio
        property real orbState: root.orbState
        property real quality: root.quality
        property real speaking: root.reactiveSpeaking
        property real distortion: root.reactiveDistortion
        property real glow: root.reactiveGlow
        property real pulseAmount: root.reactivePulse
        property real flicker: root.reactiveFlicker
        scale: root.effectScale
        opacity: root.effectOpacity
    }
}
