import QtQuick

Item {
    id: root

    property int surfaceState: 0
    property string primaryText: ""
    property string secondaryText: ""
    property string preferredMode: "compact"
    property real dpiScale: 1.0
    property real maxWidth: 300
    property real maxHeight: 320
    property string displayMode: preferredMode

    readonly property bool isCompact: displayMode === "compact"
    readonly property bool showSecondary: !isCompact && secondaryText.length > 0
    readonly property real compactWidth: Math.min(maxWidth, Math.round(248 * dpiScale))
    readonly property real expandedWidth: Math.min(maxWidth, Math.round(340 * dpiScale))
    readonly property real activeWidth: isCompact ? compactWidth : (displayMode === "expanded" ? expandedWidth : maxWidth)
    readonly property real verticalPadding: Math.round(9 * dpiScale)
    readonly property real horizontalPadding: Math.round(15 * dpiScale)
    readonly property real contentMaxHeight: Math.max(Math.round(28 * dpiScale), maxHeight - Math.round(18 * dpiScale))

    implicitWidth: Math.round(activeWidth)
    implicitHeight: height
    width: implicitWidth
    height: card.height + Math.round(10 * dpiScale)
    opacity: primaryText.length > 0 ? 1 : 0
    y: isCompact ? 0 : Math.round(2 * dpiScale)

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
        anchors.topMargin: Math.round(6 * root.dpiScale)
        width: root.width - Math.round(2 * root.dpiScale)
        height: card.height
        radius: card.radius
        color: "#0c000000"
        opacity: 0.45
        y: Math.round(8 * root.dpiScale)
    }

    Rectangle {
        id: card
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        width: parent.width
        height: contentViewport.height + Math.round(18 * root.dpiScale)
        radius: Math.round(20 * root.dpiScale)
        color: "#11161b20"
        border.width: 1
        border.color: "#16ffffff"

        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: parent.radius - 1
            color: "#10151b26"
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 1
            height: parent.height * 0.42
            radius: parent.radius - 1
            color: "#0effffff"
        }

        Item {
            id: contentViewport
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: root.horizontalPadding
            anchors.rightMargin: root.horizontalPadding
            height: root.isCompact
                ? compactPrimary.implicitHeight
                : Math.min(contentColumn.implicitHeight, root.contentMaxHeight)
            clip: true

            Text {
                id: compactPrimary
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                visible: root.isCompact
                text: root.primaryText
                color: "#f2f7ff"
                font.pixelSize: Math.round(15 * root.dpiScale)
                font.weight: Font.Medium
                wrapMode: Text.NoWrap
                elide: Text.ElideRight
                renderType: Text.NativeRendering
            }

            Flickable {
                anchors.fill: parent
                visible: !root.isCompact
                contentWidth: width
                contentHeight: contentColumn.implicitHeight
                interactive: contentHeight > height
                boundsBehavior: Flickable.StopAtBounds
                clip: true

                Column {
                    id: contentColumn
                    width: contentViewport.width
                    spacing: Math.round(4 * root.dpiScale)

                    Text {
                        width: parent.width
                        text: root.primaryText
                        color: "#f2f7ff"
                        font.pixelSize: Math.round(15 * root.dpiScale)
                        font.weight: Font.Medium
                        wrapMode: Text.Wrap
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
                        renderType: Text.NativeRendering
                    }
                }
            }
        }
    }
}
