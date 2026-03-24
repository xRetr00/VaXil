import QtQuick

Item {
    id: root

    property string stateName: "IDLE"
    property real time: 0.0
    property real audioLevel: 0.0
    property real speakingLevel: 0.0
    property real distortion: 0.12
    property real glow: 0.3
    property real orbScale: 1.0
    property real orbitalRotation: 0.0

    readonly property real stateIndex: stateName === "LISTENING" ? 1.0
        : stateName === "PROCESSING" ? 2.0
        : stateName === "SPEAKING" ? 3.0
        : 0.0

    implicitWidth: 520
    implicitHeight: 520

    ShaderEffect {
        id: auraLayer
        anchors.centerIn: parent
        width: parent.width * 1.18
        height: width
        blending: true

        property real time: root.time * 0.68 + 8.0
        property real audioLevel: root.audioLevel * 0.72
        property real speaking: root.speakingLevel * 0.7
        property vector2d resolution: Qt.vector2d(width, height)

        fragmentShader: "qrc:/qt/qml/JARVIS/gui/shaders/src/gui/shaders/orb.frag.qsb"
        opacity: 0.18 + root.glow * 0.16
        scale: 1.02 + root.glow * 0.2
        rotation: root.orbitalRotation * 0.18
    }

    ShaderEffect {
        id: plasmaLayer
        anchors.centerIn: parent
        width: parent.width * 0.86
        height: width
        blending: true

        property real time: root.time * 0.97
        property real audioLevel: root.audioLevel * 0.92
        property real speaking: root.speakingLevel * 0.9
        property vector2d resolution: Qt.vector2d(width, height)

        fragmentShader: "qrc:/qt/qml/JARVIS/gui/shaders/src/gui/shaders/orb.frag.qsb"
        opacity: 0.92
        scale: root.orbScale
        rotation: root.orbitalRotation * -0.32
    }

    ShaderEffect {
        id: coreLayer
        anchors.centerIn: parent
        width: parent.width * 0.56
        height: width
        blending: true

        property real time: root.time * 1.34 + 17.0
        property real audioLevel: root.audioLevel * 0.62
        property real speaking: root.speakingLevel * 0.88
        property vector2d resolution: Qt.vector2d(width, height)

        fragmentShader: "qrc:/qt/qml/JARVIS/gui/shaders/src/gui/shaders/orb.frag.qsb"
        opacity: 0.98
        scale: 0.92 + root.orbScale * 0.08
        rotation: root.orbitalRotation * 0.46
    }
}
