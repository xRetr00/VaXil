import QtQuick

Item {
    id: root

    property int surfaceState: 0
    property real dpiScale: 1.0

    readonly property string stateLabel: {
        switch (surfaceState) {
        case 1: return "Listening"
        case 2: return "Thinking"
        case 3: return "Speaking"
        case 4: return "Tool Running"
        case 5: return "Error"
        default: return "Ready"
        }
    }

    readonly property color dotColor: {
        switch (surfaceState) {
        case 1: return "#cde9ff"
        case 2: return "#d9d5ff"
        case 3: return "#c3f6de"
        case 4: return "#ffe2bd"
        case 5: return "#ffb9c6"
        default: return "#d7e8fb"
        }
    }

    readonly property color fillColor: {
        switch (surfaceState) {
        case 5: return "#1e281f24"
        case 4: return "#1a2d2418"
        default: return "#18242d38"
        }
    }

    implicitWidth: indicatorRow.implicitWidth + Math.round(20 * dpiScale)
    implicitHeight: Math.round(30 * dpiScale)

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: root.fillColor
        border.width: 1
        border.color: "#26ffffff"
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: height / 2
        color: "transparent"
        border.width: 1
        border.color: "#10ffffff"
    }

    Row {
        id: indicatorRow
        anchors.centerIn: parent
        spacing: Math.round(8 * root.dpiScale)

        Rectangle {
            id: pulseDot
            width: Math.round(7 * root.dpiScale)
            height: width
            radius: width / 2
            color: root.dotColor
            opacity: root.surfaceState === 0 ? 0.78 : 1.0

            SequentialAnimation on opacity {
                running: root.surfaceState !== 0 && root.surfaceState !== 5
                loops: Animation.Infinite
                NumberAnimation { to: 0.45; duration: 760; easing.type: Easing.InOutCubic }
                NumberAnimation { to: 1.0; duration: 920; easing.type: Easing.InOutCubic }
            }
        }

        Text {
            text: root.stateLabel
            color: "#edf5ff"
            opacity: 0.95
            font.pixelSize: Math.round(12 * root.dpiScale)
            font.weight: Font.Medium
            renderType: Text.NativeRendering
        }
    }
}
