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

    Rectangle {
        anchors.centerIn: parent
        width: parent.width * 1.02
        height: width
        radius: width / 2
        color: stateName === "SPEAKING" ? "#52156f" : "#12253d"
        opacity: 0.03 + root.glow * 0.04
        scale: 0.92 + root.glow * 0.22
    }

    Rectangle {
        anchors.centerIn: parent
        width: parent.width * 0.78
        height: width
        radius: width / 2
        color: stateName === "SPEAKING" ? "#7630cc" : "#1f4f93"
        opacity: 0.05 + root.glow * 0.04
        scale: 1.0 + root.glow * 0.12
    }

    ShaderEffect {
        id: auraLayer
        anchors.centerIn: parent
        width: parent.width * 0.96
        height: width
        blending: true

        property real time: root.time * 0.82
        property real level: root.audioLevel * 0.7
        property real speaking: root.speakingLevel * 0.65
        property real mode: root.stateIndex
        property real distortion: root.distortion * 1.35
        property vector2d resolution: Qt.vector2d(width, height)
        property color colorA: root.stateName === "SPEAKING" ? "#d7d0ff" : "#b9fcff"
        property color colorB: root.stateName === "PROCESSING" ? "#8073ff" : "#51a7ff"
        property color colorC: root.stateName === "SPEAKING" ? "#7124d5" : "#11335d"

        fragmentShader: "qrc:/qt/qml/JARVIS/gui/shaders/src/gui/shaders/orb.frag.qsb"
        opacity: 0.28 + root.glow * 0.2
        scale: 1.05 + root.glow * 0.16
    }

    ShaderEffect {
        id: coreLayer
        anchors.centerIn: parent
        width: parent.width * 0.74
        height: width
        blending: true

        property real time: root.time
        property real level: root.audioLevel
        property real speaking: root.speakingLevel
        property real mode: root.stateIndex
        property real distortion: root.distortion
        property vector2d resolution: Qt.vector2d(width, height)
        property color colorA: root.stateName === "SPEAKING" ? "#f3e3ff" : "#cbffff"
        property color colorB: root.stateName === "PROCESSING" ? "#7a78ff" : "#39a5ff"
        property color colorC: root.stateName === "SPEAKING" ? "#a53fff" : "#16345c"

        fragmentShader: "qrc:/qt/qml/JARVIS/gui/shaders/src/gui/shaders/orb.frag.qsb"
        scale: root.orbScale
    }

    ShaderEffect {
        id: innerLayer
        anchors.centerIn: parent
        width: parent.width * 0.48
        height: width
        blending: true

        property real time: root.time * 1.3 + 4.0
        property real level: root.audioLevel * 0.6
        property real speaking: root.speakingLevel * 0.8
        property real mode: root.stateIndex
        property real distortion: root.distortion * 0.75
        property vector2d resolution: Qt.vector2d(width, height)
        property color colorA: "#e8ffff"
        property color colorB: root.stateName === "SPEAKING" ? "#d978ff" : "#7cd8ff"
        property color colorC: root.stateName === "SPEAKING" ? "#6c2db9" : "#173764"

        fragmentShader: "qrc:/qt/qml/JARVIS/gui/shaders/src/gui/shaders/orb.frag.qsb"
        opacity: 0.82
        scale: 0.96 + root.orbScale * 0.04
        rotation: root.orbitalRotation * 0.55
    }

    Rectangle {
        anchors.centerIn: parent
        width: parent.width * 0.18
        height: width
        radius: width / 2
        color: "#f5fcff"
        opacity: 0.16 + root.glow * 0.1 + root.speakingLevel * 0.08
        scale: 0.92 + root.orbScale * 0.08
    }

    Repeater {
        model: 4

        delegate: Item {
            required property int index

            width: root.width
            height: root.height
            rotation: root.orbitalRotation * (index % 2 === 0 ? 1.0 : -0.65) + index * 90
            opacity: root.stateName === "PROCESSING" || root.stateName === "SPEAKING" ? 0.65 : 0.22

            Rectangle {
                width: index % 2 === 0 ? 16 : 10
                height: width
                radius: width / 2
                color: index % 2 === 0 ? "#8ce7ff" : "#cc8cff"
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: -root.height * (0.24 + index * 0.015)
                opacity: 0.55 + root.glow * 0.2
            }
        }
    }
}
