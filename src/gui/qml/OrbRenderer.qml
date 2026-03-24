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

    implicitWidth: 460
    implicitHeight: 460

    Rectangle {
        anchors.centerIn: parent
        width: parent.width * 0.94
        height: width
        radius: width / 2
        color: "#060911"
        opacity: 0.08 + root.glow * 0.08
        scale: 1.0 + root.glow * 0.12
    }

    Rectangle {
        anchors.centerIn: parent
        width: parent.width * 0.8
        height: width
        radius: width / 2
        color: root.stateName === "SPEAKING" ? "#5b31c9" : "#1f58b2"
        opacity: 0.14 + root.glow * 0.08
        scale: 1.02 + root.glow * 0.08
    }

    Rectangle {
        id: shell
        anchors.centerIn: parent
        width: parent.width * 0.72
        height: width
        radius: width / 2
        color: "#1b234033"
        border.width: 1
        border.color: root.stateName === "SPEAKING" ? "#d286ff" : "#9cc9ff"
        opacity: 0.98
        clip: true
        scale: root.orbScale

        gradient: Gradient {
            GradientStop { position: 0.0; color: "#60dbf3ff" }
            GradientStop { position: 0.16; color: "#25376777" }
            GradientStop { position: 0.72; color: "#12193144" }
            GradientStop { position: 1.0; color: "#06091210" }
        }

        Behavior on scale { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }

        Repeater {
            model: 5

            delegate: Rectangle {
                required property int index

                width: shell.width * (0.18 + index * 0.02)
                height: shell.height * (0.82 - index * 0.06)
                radius: width / 2
                anchors.centerIn: parent
                x: Math.sin(root.time * 0.55 + index * 1.2) * shell.width * 0.05
                y: Math.cos(root.time * 0.75 + index * 0.9) * shell.height * 0.04
                rotation: root.orbitalRotation * 0.35 + index * 34 + Math.sin(root.time * 0.8 + index) * 14
                opacity: 0.18 + index * 0.08 + root.glow * 0.08

                gradient: Gradient {
                    GradientStop { position: 0.0; color: index % 2 === 0 ? "#ffd1ff" : "#d5fdff" }
                    GradientStop { position: 0.28; color: index % 2 === 0 ? "#7e6fff" : "#3f95ff" }
                    GradientStop { position: 1.0; color: "#05142a" }
                }
            }
        }

        Rectangle {
            width: shell.width * 0.92
            height: width
            anchors.centerIn: parent
            radius: width / 2
            color: "transparent"
            border.width: 1
            border.color: "#5ea9ff"
            opacity: 0.16
        }

        Rectangle {
            width: shell.width * 0.58
            height: shell.height * 0.22
            radius: height / 2
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: shell.height * 0.08
            color: "#f4fbff"
            opacity: 0.18
            rotation: -12
        }

        ShaderEffect {
            id: orbShader
            anchors.centerIn: parent
            width: shell.width * 0.64
            height: width
            blending: true
            mesh: Qt.size(1, 1)

            property real time: root.time
            property real level: root.audioLevel
            property real speaking: root.speakingLevel
            property real mode: root.stateIndex
            property real distortion: root.distortion
            property vector2d resolution: Qt.vector2d(width, height)
            property color colorA: root.stateName === "SPEAKING" ? "#bdf8ff" : "#b1fbff"
            property color colorB: root.stateName === "PROCESSING" ? "#6d6fff" : "#3ba8ff"
            property color colorC: root.stateName === "SPEAKING" ? "#bb56ff" : "#143160"

            fragmentShader: "qrc:/qt/qml/JARVIS/gui/shaders/src/gui/shaders/orb.frag.qsb"
            scale: 0.94 + root.orbScale * 0.06
        }

        Rectangle {
            anchors.centerIn: orbShader
            width: orbShader.width * 0.24
            height: width
            radius: width / 2
            color: "#f1fbff"
            opacity: 0.2 + root.glow * 0.12 + root.speakingLevel * 0.06
        }
    }

    Repeater {
        model: 3

        delegate: Item {
            width: root.width
            height: root.height
            opacity: root.stateName === "PROCESSING" ? 0.7 : 0.0
            rotation: root.orbitalRotation + index * 120

            Behavior on opacity { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

            Rectangle {
                width: 14
                height: 14
                radius: 7
                color: index === 0 ? "#89f4ff" : index === 1 ? "#7c91ff" : "#c06cff"
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: -root.height * 0.26
                opacity: 0.78
            }
        }
    }

    Rectangle {
        anchors.centerIn: parent
        width: parent.width * 0.78
        height: width
        radius: width / 2
        color: "transparent"
        border.width: 1
        border.color: root.stateName === "SPEAKING" ? "#ff9cff" : "#66b5ff"
        opacity: 0.1 + root.glow * 0.12
        scale: 1.0 + root.glow * 0.04

        Behavior on opacity { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
        Behavior on scale { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }
    }
}
