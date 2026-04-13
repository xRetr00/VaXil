import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "." as JarvisUi

JarvisUi.VisionGlassPanel {
    id: root

    property var viewModel: null
    property var behaviorEntries: []
    property string sqlitePath: ""
    property string ndjsonPath: ""
    property int maxEntries: 12

    width: parent ? parent.width : implicitWidth
    implicitHeight: timelineColumn.implicitHeight + 44
    radius: 30
    panelColor: "#16192022"
    innerColor: "#1b20292f"
    outlineColor: "#20ffffff"
    highlightColor: "#16ffffff"
    shadowOpacity: 0.22

    function refreshTimeline() {
        if (!viewModel) {
            return
        }
        behaviorEntries = viewModel.recentBehaviorEvents(maxEntries)
        sqlitePath = viewModel.behaviorLedgerDatabasePath()
        ndjsonPath = viewModel.behaviorLedgerNdjsonPath()
    }

    function accentColor(entry) {
        const family = (entry.family || "").toString()
        if (family === "perception") {
            return "#8ed4ff"
        }
        if (family === "cooldown") {
            return "#ffd19a"
        }
        if (family === "context_thread") {
            return "#b7c0ff"
        }
        return "#dfe9f7"
    }

    function detailText(entry) {
        const parts = []
        const reasonCode = (entry.reasonCode || "").toString()
        const threadId = (entry.threadId || "").toString()
        const capabilityId = (entry.capabilityId || "").toString()
        if (reasonCode.length > 0) {
            parts.push(reasonCode)
        }
        if (threadId.length > 0) {
            parts.push(threadId)
        }
        if (capabilityId.length > 0) {
            parts.push(capabilityId)
        }
        return parts.join("  |  ")
    }

    Component.onCompleted: refreshTimeline()
    onVisibleChanged: if (visible) refreshTimeline()

    Timer {
        interval: 2000
        repeat: true
        running: root.visible
        onTriggered: root.refreshTimeline()
    }

    ColumnLayout {
        id: timelineColumn
        anchors.fill: parent
        anchors.margins: 22
        spacing: 14

        RowLayout {
            spacing: 10

            JarvisUi.VisionGlyph {
                iconSize: 16
                source: "qrc:/qt/qml/VAXIL/gui/assets/Icons/icons8-flow-chart-50.png"
            }

            Text {
                text: "Behavior Timeline"
                color: "#eef7ff"
                font.pixelSize: 22
                font.weight: Font.Medium
            }
        }

        Text {
            text: "Perception, context-thread, and cooldown events from the behavioral ledger."
            color: "#8099b8"
            font.pixelSize: 14
            wrapMode: Text.Wrap
        }

        Text {
            visible: sqlitePath.length > 0 || ndjsonPath.length > 0
            text: "SQLite: " + sqlitePath + "\nNDJSON: " + ndjsonPath
            color: "#6f88a5"
            font.pixelSize: 11
            wrapMode: Text.WrapAnywhere
        }

        Text {
            visible: behaviorEntries.length === 0
            text: "No behavior events recorded yet."
            color: "#9ab0ca"
            font.pixelSize: 13
        }

        Repeater {
            model: root.behaviorEntries

            delegate: JarvisUi.VisionGlassPanel {
                required property var modelData
                width: parent.width
                radius: 16
                panelColor: "#17202a28"
                innerColor: "#1d253030"
                outlineColor: "#22ffffff"
                highlightColor: "#10ffffff"
                shadowOpacity: 0.14
                height: eventColumn.implicitHeight + 20

                Column {
                    id: eventColumn
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 4

                    Text {
                        text: (modelData.timestampUtc || "") + "  " + (modelData.family || "") + "  " + (modelData.stage || "")
                        color: root.accentColor(modelData)
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }

                    Text {
                        text: root.detailText(modelData)
                        color: "#d7e4f5"
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }

                    Text {
                        text: JSON.stringify(modelData)
                        color: "#8ea8c8"
                        font.pixelSize: 11
                        wrapMode: Text.WrapAnywhere
                    }
                }
            }
        }
    }
}
