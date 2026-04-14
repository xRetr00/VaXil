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
        if (family === "selection_context") {
            return "#8ff0c8"
        }
        if (family === "action_proposal") {
            return "#f3d78a"
        }
        if (family === "ui_presentation") {
            return "#ffb6d9"
        }
        return "#dfe9f7"
    }

    function detailText(entry) {
        const parts = []
        const reasonCode = (entry.reasonCode || "").toString()
        const threadId = (entry.threadId || "").toString()
        const capabilityId = (entry.capabilityId || "").toString()
        const purpose = (entry.purpose || "").toString()
        const action = (entry.action || "").toString()
        const desktopTaskId = (entry.desktopTaskId || "").toString()
        const metadataQuality = (entry.metadataQuality || "").toString()
        const metadataRedacted = !!entry.metadataRedacted
        const gateReasonCode = (entry.gateReasonCode || "").toString()
        const cooldownReasonCode = (entry.cooldownReasonCode || "").toString()
        const confidenceScore = entry.confidenceScore
        const noveltyScore = entry.noveltyScore
        if (reasonCode.length > 0) {
            parts.push(reasonCode)
        }
        if (cooldownReasonCode.length > 0 && cooldownReasonCode !== reasonCode) {
            parts.push("cooldown=" + cooldownReasonCode)
        }
        if (gateReasonCode.length > 0) {
            parts.push("gate=" + gateReasonCode)
        }
        if (purpose.length > 0) {
            parts.push(purpose)
        }
        if (action.length > 0) {
            parts.push(action)
        }
        if (desktopTaskId.length > 0) {
            parts.push(desktopTaskId)
        }
        if (metadataQuality.length > 0) {
            parts.push("quality=" + metadataQuality)
        }
        if (metadataRedacted) {
            parts.push("redacted")
        }
        if (confidenceScore !== undefined && confidenceScore !== null) {
            parts.push("confidence=" + Number(confidenceScore).toFixed(2))
        }
        if (noveltyScore !== undefined && noveltyScore !== null) {
            parts.push("novelty=" + Number(noveltyScore).toFixed(2))
        }
        if (threadId.length > 0) {
            parts.push(threadId)
        }
        if (capabilityId.length > 0) {
            parts.push(capabilityId)
        }
        return parts.join("  |  ")
    }

    function summaryText(entry) {
        const family = (entry.family || "").toString()
        if (family === "selection_context") {
            const stage = (entry.stage || "").toString()
            if (stage === "memory_context") {
                const activeKeys = entry.activeCommitmentKeys || []
                const profileKeys = entry.profileKeys || []
                const episodicKeys = entry.episodicKeys || []
                return "Memory context built: active [" + activeKeys.join(", ") + "] profile [" + profileKeys.join(", ") + "] episodic [" + episodicKeys.join(", ") + "]"
            }
            if (stage === "tool_plan") {
                const tools = entry.orderedToolNames || []
                return "Tool plan: " + tools.join(", ") + ((entry.rationale || "").toString().length > 0 ? " | " + entry.rationale : "")
            }
            if (stage === "tool_exposure") {
                const selected = entry.selectedToolNames || []
                return "Tools exposed: " + selected.join(", ")
            }
            const taskId = (entry.desktopTaskId || "").toString()
            const topic = (entry.desktopTopic || "").toString()
            return "Desktop context affected selection" + (taskId ? " for " + taskId : "") + (topic ? " (" + topic + ")" : "")
        }
        if (family === "action_proposal") {
            const stage = (entry.stage || "").toString()
            const title = (entry.title || "").toString()
            const action = (entry.action || "").toString()
            const priority = (entry.priority || "").toString()
            const reasonCode = (entry.reasonCode || "").toString()
            if (stage === "generated") {
                const proposalCount = entry.proposalCount || 0
                const proposalTitles = entry.proposalTitles || []
                return "Generated " + proposalCount + " suggestion proposals: " + proposalTitles.join(", ")
            }
            if (stage === "ranked") {
                const rankedTitles = entry.rankedTitles || []
                let text = "Ranked suggestion proposals"
                if (rankedTitles.length > 0) {
                    text += ": " + rankedTitles.join(" > ")
                }
                if (reasonCode.length > 0) {
                    text += " because " + reasonCode
                }
                return text
            }
            if (stage === "gated") {
                const confidenceScore = entry.confidenceScore
                const noveltyScore = entry.noveltyScore
                let text = "Proposal " + (action.length > 0 ? action : "decision")
                if (title.length > 0) {
                    text += ": " + title
                }
                if (priority.length > 0) {
                    text += " [" + priority + "]"
                }
                if (confidenceScore !== undefined && confidenceScore !== null) {
                    text += " confidence " + Number(confidenceScore).toFixed(2)
                }
                if (noveltyScore !== undefined && noveltyScore !== null) {
                    text += " novelty " + Number(noveltyScore).toFixed(2)
                }
                if (reasonCode.length > 0) {
                    text += " because " + reasonCode
                }
                return text
            }
            let text = "Proposal " + (action.length > 0 ? action : "decision")
            if (title.length > 0) {
                text += ": " + title
            }
            if (priority.length > 0) {
                text += " [" + priority + "]"
            }
            if (reasonCode.length > 0) {
                text += " because " + reasonCode
            }
            return text
        }
        if (family === "ui_presentation") {
            const taskType = (entry.taskType || "").toString()
            const action = (entry.action || "").toString()
            const surfaceKind = (entry.surfaceKind || "").toString()
            const reasonCode = (entry.reasonCode || "").toString()
            let text = "UI presentation " + (action || "decision") + (taskType ? " for " + taskType : "")
            if (surfaceKind.length > 0) {
                text += " [" + surfaceKind + "]"
            }
            if (reasonCode.length > 0) {
                text += " because " + reasonCode
            }
            return text
        }
        if (family === "context_thread") {
            const taskId = (entry.taskId || "").toString()
            const topic = (entry.topic || "").toString()
            const metadataQuality = (entry.metadataQuality || "").toString()
            const confidence = entry.confidence
            const redactionReason = (entry.redactionReason || "").toString()
            let text = "Context thread updated" + (taskId ? " to " + taskId : "") + (topic ? " (" + topic + ")" : "")
            if (metadataQuality.length > 0) {
                text += " [" + metadataQuality + " confidence]"
            } else if (confidence !== undefined && confidence !== null) {
                text += " [confidence " + Number(confidence).toFixed(2) + "]"
            }
            if (!!entry.metadataRedacted) {
                text += redactionReason.length > 0 ? " redacted: " + redactionReason : " redacted"
            }
            return text
        }
        if (family === "perception") {
            const metadataQuality = (entry.metadataQuality || "").toString()
            const confidence = entry.confidence
            const novelty = entry.novelty
            let text = "Perception observed"
            if (metadataQuality.length > 0) {
                text += " [" + metadataQuality + " confidence]"
            } else if (confidence !== undefined && confidence !== null) {
                text += " [confidence " + Number(confidence).toFixed(2) + "]"
            }
            if (novelty !== undefined && novelty !== null) {
                text += " novelty " + Number(novelty).toFixed(2)
            }
            if (!!entry.metadataRedacted) {
                const redactionReason = (entry.redactionReason || "").toString()
                text += redactionReason.length > 0 ? " redacted: " + redactionReason : " redacted"
            }
            return text
        }
        return JSON.stringify(entry)
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
            text: "Perception, context-thread, cooldown, selection-context, action-proposal, and UI-presentation events from the behavioral ledger."
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
                        text: root.summaryText(modelData)
                        color: "#8ea8c8"
                        font.pixelSize: 11
                        wrapMode: Text.WrapAnywhere
                    }
                }
            }
        }
    }
}
