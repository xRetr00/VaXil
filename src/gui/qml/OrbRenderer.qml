import QtQuick

Item {
    id: root

    property string stateName: "IDLE"
    property int uiState: stateIndex
    property real time: 0.0
    property real audioLevel: 0.0
    property real speakingLevel: 0.0
    property real distortion: 0.12
    property real glow: 0.3
    property real orbScale: 1.0
    property real orbitalRotation: 0.0
    property int quality: qualityHigh

    readonly property int qualityLow: 0
    readonly property int qualityMedium: 1
    readonly property int qualityHigh: 2

    readonly property real stateIndex: stateName === "LISTENING" ? 1.0
        : stateName === "PROCESSING" ? 2.0
        : stateName === "SPEAKING" ? 3.0
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
        property real audioLevel: root.audioLevel * 0.54
        property real speaking: root.speakingLevel * 0.42
        property real uiState: root.uiState
        property real quality: root.quality
        property vector2d resolution: Qt.vector2d(width, height)

        fragmentShader: "qrc:/qt/qml/JARVIS/gui/shaders/src/gui/shaders/orb.frag.qsb"
        opacity: 0.10 + root.glow * 0.18
        scale: root.orbScale * (1.10 + root.glow * 0.20)
        rotation: root.orbitalRotation * 0.07
    }

    ShaderEffect {
        id: energyLayer
        anchors.centerIn: parent
        width: parent.width * 0.92
        height: width
        blending: true

        property real time: root.time * 1.16 + 11.0
        property real audioLevel: root.audioLevel * 0.96
        property real speaking: root.speakingLevel * 0.82
        property real uiState: root.uiState
        property real quality: root.quality
        property vector2d resolution: Qt.vector2d(width, height)

        fragmentShader: "qrc:/qt/qml/JARVIS/gui/shaders/src/gui/shaders/orb.frag.qsb"
        opacity: 0.62 + root.glow * 0.24
        scale: root.orbScale * (0.98 + root.audioLevel * 0.08)
        rotation: root.orbitalRotation * -0.23
    }

    ShaderEffect {
        id: coreLayer
        anchors.centerIn: parent
        width: parent.width * 0.58
        height: width
        blending: true

        property real time: root.time * 1.58 + 22.0
        property real audioLevel: root.audioLevel * 0.70
        property real speaking: root.speakingLevel * 1.05
        property real uiState: root.uiState
        property real quality: root.quality
        property vector2d resolution: Qt.vector2d(width, height)

        fragmentShader: "qrc:/qt/qml/JARVIS/gui/shaders/src/gui/shaders/orb.frag.qsb"
        opacity: 0.86 + root.speakingLevel * 0.24
        scale: root.orbScale * (0.80 + root.audioLevel * 0.10)
        rotation: root.orbitalRotation * 0.14
    }
}
