import QtQuick

Item {
    id: root

    property int surfaceState: 0
    property string primaryText: ""
    property string secondaryText: ""
    property string preferredMode: "compact"
    property real dpiScale: 1.0
    property real maxWidth: 420
    property string displayMode: preferredMode

    readonly property bool showSecondary: displayMode !== "compact" && secondaryText.length > 0
    readonly property int primaryLineLimit: displayMode === "compact" ? 1 : 2
    readonly property int secondaryLineLimit: displayMode === "extended" ? 1 : 1

    implicitWidth: Math.round(maxWidth)
    implicitHeight: height
    width: implicitWidth
    height: card.implicitHeight + Math.round(14 * dpiScale)
    opacity: primaryText.length > 0 ? 1 : 0
    y: displayMode === "compact" ? 0 : Math.round(2 * dpiScale)

    Behavior on height {
        NumberAnimation { duration: 220; easing.type: Easing.InOutCubic }
    }

    Behavior on opacity {
        NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
    }

    Behavior on y {
        NumberAnimation { duration: 200; easing.type: Easing.OutCubic }
    }

    onPreferredModeChanged: updateDisplayMode()
    onSurfaceStateChanged: updateDisplayMode()
    Component.onCompleted: updateDisplayMode()

    function updateDisplayMode() {
        if (preferredMode !== "compact" || surfaceState !== 0) {
            collapseTimer.stop()
            displayMode = preferredMode
            return
        }

        if (displayMode === "compact") {
            return
        }

        collapseTimer.restart()
    }

    Timer {
        id: collapseTimer
        interval: 4500
        repeat: false
        onTriggered: root.displayMode = "compact"
    }

    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Math.round(8 * root.dpiScale)
        width: root.width - Math.round(2 * root.dpiScale)
        height: card.height
        radius: card.radius
        color: "#14000000"
        opacity: 0.55
        y: Math.round(10 * root.dpiScale)
    }

    Rectangle {
        id: card
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        width: parent.width
        implicitHeight: contentColumn.implicitHeight + Math.round(22 * root.dpiScale)
        radius: Math.round(22 * root.dpiScale)
        color: "#17192025"
        border.width: 1
        border.color: "#22ffffff"

        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: parent.radius - 1
            color: "#12161d2d"
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 1
            height: parent.height * 0.48
            radius: parent.radius - 1
            color: "#12ffffff"
        }

        Column {
            id: contentColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: Math.round(18 * root.dpiScale)
            anchors.rightMargin: Math.round(18 * root.dpiScale)
            spacing: Math.round(5 * root.dpiScale)

            Text {
                width: parent.width
                text: root.primaryText
                color: "#f2f7ff"
                font.pixelSize: Math.round(15 * root.dpiScale)
                font.weight: Font.Medium
                wrapMode: Text.Wrap
                maximumLineCount: root.primaryLineLimit
                elide: Text.ElideRight
                renderType: Text.NativeRendering
            }

            Text {
                width: parent.width
                visible: root.showSecondary
                text: root.secondaryText
                color: "#c1d1e5"
                opacity: 0.92
                font.pixelSize: Math.round(12 * root.dpiScale)
                wrapMode: Text.Wrap
                maximumLineCount: root.secondaryLineLimit
                elide: Text.ElideRight
                renderType: Text.NativeRendering
            }
        }
    }
}
