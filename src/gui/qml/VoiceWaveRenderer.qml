import QtQuick

Item {
    id: root

    implicitWidth: 320
    implicitHeight: 72

    readonly property int stateIdle: 0
    readonly property int stateListening: 1
    readonly property int stateThinking: 2
    readonly property int stateSpeaking: 3

    property string stateName: "IDLE"
    property real time: 0.0
    property real audioLevel: 0.0
    property real speakingLevel: 0.0
    property real glow: 0.0
    property int uiState: -1
    property string diagnosticsTag: ""
    property bool geometryReadyLogged: false

    readonly property real effectiveUiState: uiState >= 0 ? uiState : mapState(stateName)
    readonly property real reactiveAudio: clamp(audioLevel, 0.0, 1.0)
    readonly property real reactiveSpeaking: clamp(speakingLevel, 0.0, 1.0)
    readonly property real reactiveGlow: clamp(glow, 0.0, 1.0)
    readonly property real reactiveInput: clamp(Math.max(reactiveAudio, reactiveSpeaking * 0.45), 0.0, 1.0)
    readonly property real idleAmount: stateWeight(stateIdle)
    readonly property real listeningAmount: stateWeight(stateListening)
    readonly property real thinkingAmount: stateWeight(stateThinking)
    readonly property real speakingAmount: stateWeight(stateSpeaking)
    readonly property real idleCarrier: wave(time * 0.7)
    readonly property real listeningCarrier: wave(time * (3.0 + reactiveInput * 4.8))
    readonly property real thinkingCarrier: 0.55 * wave(time * 1.4 + 0.4) + 0.45 * wave(time * 2.7 + 1.1)
    readonly property real speakingCarrier: 0.45 * wave(time * (5.2 + reactiveSpeaking * 2.8))
        + 0.55 * wave(time * (8.8 + reactiveInput * 3.6) + 0.8)
    readonly property real amplitudeUniform: clamp(
        idleAmount * (0.16 + idleCarrier * 0.06)
        + listeningAmount * (0.34 + reactiveInput * 0.78 + listeningCarrier * 0.18)
        + thinkingAmount * (0.28 + thinkingCarrier * 0.16 + reactiveGlow * 0.06)
        + speakingAmount * (0.46 + reactiveSpeaking * 0.5 + reactiveInput * 0.22 + speakingCarrier * 0.18),
        0.12, 1.35)
    readonly property real timeScaleUniform: clamp(
        idleAmount * (0.82 + idleCarrier * 0.06)
        + listeningAmount * (1.02 + reactiveInput * 0.94 + listeningCarrier * 0.16)
        + thinkingAmount * (1.18 + thinkingCarrier * 0.3)
        + speakingAmount * (1.42 + reactiveSpeaking * 0.66 + reactiveInput * 0.2 + speakingCarrier * 0.12),
        0.72, 2.45)
    readonly property real yScaleUniform: clamp(
        idleAmount * 0.98
        + listeningAmount * (0.88 - reactiveInput * 0.08)
        + thinkingAmount * 0.9
        + speakingAmount * (0.8 - reactiveSpeaking * 0.08),
        0.68, 1.0)
    readonly property real exposureUniform: clamp(
        idleAmount * (0.72 + reactiveGlow * 0.1)
        + listeningAmount * (0.95 + reactiveInput * 0.34 + reactiveGlow * 0.1)
        + thinkingAmount * (1.0 + thinkingCarrier * 0.18 + reactiveGlow * 0.12)
        + speakingAmount * (1.12 + reactiveSpeaking * 0.4 + reactiveGlow * 0.12),
        0.68, 1.7)
    readonly property string resolvedDiagnosticsTag: diagnosticsTag.length > 0
        ? diagnosticsTag
        : "voice_wave_" + Math.round(width) + "x" + Math.round(height)

    function clamp(value, minValue, maxValue) {
        return Math.max(minValue, Math.min(maxValue, value))
    }

    function wave(value) {
        return 0.5 + 0.5 * Math.sin(value)
    }

    function stateWeight(targetIndex) {
        return clamp(1.0 - Math.abs(effectiveUiState - targetIndex), 0.0, 1.0)
    }

    function mapState(name) {
        const normalized = (name || "").toString().trim().toUpperCase()
        if (normalized === "LISTENING") {
            return stateListening
        }
        if (normalized === "PROCESSING" || normalized === "THINKING") {
            return stateThinking
        }
        if (normalized === "SPEAKING" || normalized === "EXECUTING") {
            return stateSpeaking
        }
        return stateIdle
    }

    function diagnosticsPayload(extra) {
        const payload = {
            tag: resolvedDiagnosticsTag,
            stateName: stateName,
            uiState: effectiveUiState,
            time: Number(time.toFixed(3)),
            audioLevel: Number(reactiveAudio.toFixed(3)),
            speakingLevel: Number(reactiveSpeaking.toFixed(3)),
            glow: Number(reactiveGlow.toFixed(3)),
            timeScale: Number(timeScaleUniform.toFixed(3)),
            amplitude: Number(amplitudeUniform.toFixed(3)),
            yScale: Number(yScaleUniform.toFixed(3)),
            exposure: Number(exposureUniform.toFixed(3)),
            width: Math.round(width),
            height: Math.round(height),
            visible: root.visible
        }
        for (const key in extra) {
            payload[key] = extra[key]
        }
        return payload
    }

    Component.onCompleted: {
        backend.logOrbRendererStatus("voice_wave_component_completed", diagnosticsPayload({
            shader: "qrc:/qt/qml/VAXIL/gui/shaders/src/gui/shaders/voice_wave.frag.qsb"
        }))
        logGeometryReady()
    }

    Timer {
        interval: 12000
        running: root.visible && root.width > 0 && root.height > 0
        repeat: true
        onTriggered: backend.logOrbRendererStatus("voice_wave_heartbeat", root.diagnosticsPayload({}))
    }

    onWidthChanged: logGeometryReady()
    onHeightChanged: logGeometryReady()

    function logGeometryReady() {
        if (geometryReadyLogged || width <= 0 || height <= 0) {
            return
        }
        geometryReadyLogged = true
        backend.logOrbRendererStatus("voice_wave_geometry_ready", diagnosticsPayload({}))
    }

    ShaderEffect {
        anchors.fill: parent
        blending: true
        fragmentShader: "qrc:/qt/qml/VAXIL/gui/shaders/src/gui/shaders/voice_wave.frag.qsb"
        property real time: root.time
        property vector2d resolution: Qt.vector2d(width, height)
        property real timeScale: root.timeScaleUniform
        property real amplitude: root.amplitudeUniform
        property real yScale: root.yScaleUniform
        property real exposure: root.exposureUniform
    }
}
