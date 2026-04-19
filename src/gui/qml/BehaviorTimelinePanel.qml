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

    JarvisUi.BehaviorTimelineProposalFormatter {
        id: proposalFormatter
    }
    JarvisUi.BehaviorTimelineContextDeltaFormatter {
        id: contextDeltaFormatter
    }

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
        if (family === "connector_event") {
            return "#8ac7ff"
        }
        if (family === "action_proposal") {
            return "#f3d78a"
        }
        if (family === "ui_presentation") {
            return "#ffb6d9"
        }
        if (family === "risk_check") {
            return "#ff9f8a"
        }
        if (family === "permission") {
            return "#a8e0a1"
        }
        if (family === "confirmation") {
            return "#d7b6ff"
        }
        return "#dfe9f7"
    }

    function permissionSummary(permissions) {
        const rows = permissions || []
        const parts = []
        for (let i = 0; i < rows.length; ++i) {
            const grant = rows[i] || {}
            const capabilityId = (grant.capabilityId || "").toString()
            const granted = !!grant.granted
            const scope = (grant.scope || "").toString()
            if (capabilityId.length === 0) {
                continue
            }
            parts.push(capabilityId + "=" + (granted ? "allowed" : "blocked") + (scope.length > 0 ? " (" + scope + ")" : ""))
        }
        return parts.join(", ")
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
        const riskLevel = (entry.level || entry.riskLevel || "").toString()
        const confirmationRequired = entry.confirmationRequired
        const permissionCount = entry.permissionCount
        const registryVersion = (entry.permissionRegistryVersion || "").toString()
        const proposalReasonCode = (entry.proposalReasonCode || "").toString()
        const sourceLabel = (entry.sourceLabel || "").toString()
        const presentationKeyHint = (entry.presentationKeyHint || "").toString()
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
        if (riskLevel.length > 0) {
            parts.push("risk=" + riskLevel)
        }
        if (confirmationRequired !== undefined && confirmationRequired !== null) {
            parts.push("confirm=" + (!!confirmationRequired ? "yes" : "no"))
        }
        if (permissionCount !== undefined && permissionCount !== null) {
            parts.push("permissions=" + Number(permissionCount))
        }
        if (registryVersion.length > 0) {
            parts.push(registryVersion)
        }
        if (proposalReasonCode.length > 0) {
            parts.push(proposalReasonCode)
        }
        if (sourceLabel.length > 0) {
            parts.push("source=" + sourceLabel)
        }
        if (presentationKeyHint.length > 0) {
            parts.push("key=" + presentationKeyHint)
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
                let text = "Memory context built: active [" + activeKeys.join(", ") + "] profile [" + profileKeys.join(", ") + "] episodic [" + episodicKeys.join(", ") + "]"
                text += contextDeltaFormatter.selectionDeltaText(entry, behaviorEntries)
                return text
            }
            if (stage === "prompt_context") {
                const promptKeys = entry.promptContextKeys || []
                const promptReasons = entry.promptContextReasons || []
                const suppressedKeys = entry.suppressedPromptContextKeys || []
                const stablePromptCycles = entry.stablePromptCycles || 0
                let text = "Prompt context compiled"
                if (promptKeys.length > 0) {
                    text += ": " + promptKeys.join(", ")
                }
                if (promptReasons.length > 0) {
                    text += " | " + promptReasons.join(", ")
                }
                if (suppressedKeys.length > 0) {
                    text += " | suppressed " + suppressedKeys.join(", ")
                }
                if (Number(stablePromptCycles) > 0) {
                    text += " | stable cycles " + Number(stablePromptCycles)
                }
                text += contextDeltaFormatter.selectionDeltaText(entry, behaviorEntries)
                return text
            }
            if (stage === "compiled_context_delta") {
                const addedKeys = entry.addedKeys || []
                const removedKeys = entry.removedKeys || []
                let text = "Compiled context delta"
                if (addedKeys.length > 0) {
                    text += ": +" + addedKeys.join(", +")
                }
                if (removedKeys.length > 0) {
                    text += (addedKeys.length > 0 ? " " : ": ") + "-" + removedKeys.join(", -")
                }
                if (!!entry.summaryChanged) {
                    text += " summary changed"
                }
                text += contextDeltaFormatter.selectionDeltaText(entry, behaviorEntries)
                return text
            }
            if (stage === "compiled_context_stability") {
                const stableKeys = entry.stableKeys || []
                const stableCycles = entry.stableCycles || 0
                const stableDurationMs = entry.stableDurationMs || 0
                let text = !!entry.stableContext ? "Compiled context stable" : "Compiled context fresh"
                if (stableKeys.length > 0) {
                    text += ": " + stableKeys.join(", ")
                }
                if (Number(stableCycles) > 0) {
                    text += " | cycles " + Number(stableCycles)
                }
                if (Number(stableDurationMs) > 0) {
                    text += " | " + Number(stableDurationMs) + " ms"
                }
                return text
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
            let text = "Desktop context affected selection" + (taskId ? " for " + taskId : "") + (topic ? " (" + topic + ")" : "")
            text += contextDeltaFormatter.selectionDeltaText(entry, behaviorEntries)
            return text
        }
        if (family === "risk_check") {
            const level = (entry.level || "").toString()
            const toolNames = entry.toolNames || []
            const desktopWorkMode = (entry.desktopWorkMode || "").toString()
            const permissionCount = entry.permissionCount || 0
            let text = "Risk check"
            if (level.length > 0) {
                text += ": " + level
            }
            if (!!entry.confirmationRequired) {
                text += " | confirmation required"
            }
            if (toolNames.length > 0) {
                text += " | tools " + toolNames.join(", ")
            }
            if (desktopWorkMode.length > 0) {
                text += " | desktop " + desktopWorkMode
            }
            if (Number(permissionCount) > 0) {
                text += " | " + Number(permissionCount) + " permission checks"
            }
            return text
        }
        if (family === "permission") {
            const permissions = permissionSummary(entry.permissions)
            const riskLevel = (entry.riskLevel || "").toString()
            let text = "Permission decision"
            if (permissions.length > 0) {
                text += ": " + permissions
            } else {
                text += ": no special capability required"
            }
            if (riskLevel.length > 0) {
                text += " | risk " + riskLevel
            }
            if (!!entry.confirmationRequired) {
                text += " | waiting for confirmation"
            }
            return text
        }
        if (family === "confirmation") {
            const stage = (entry.stage || "").toString()
            const permissions = permissionSummary(entry.permissions)
            const executionWillContinue = !!entry.executionWillContinue
            let text = "Confirmation " + (stage.length > 0 ? stage : "outcome")
            if (permissions.length > 0) {
                text += ": " + permissions
            }
            text += executionWillContinue ? " | execution continues" : " | execution stopped"
            return text
        }
        if (family === "action_proposal") {
            return proposalFormatter.summaryText(entry, behaviorEntries)
        }
        if (family === "connector_event") {
            const connectorKind = (entry.connectorKind || "").toString()
            const taskType = (entry.taskType || "").toString()
            const itemCount = entry.itemCount
            let text = "Connector event ingested"
            if (connectorKind.length > 0) {
                text += " from " + connectorKind
            }
            if (taskType.length > 0) {
                text += " for " + taskType
            }
            if (itemCount !== undefined && itemCount !== null && Number(itemCount) > 0) {
                text += " (" + Number(itemCount) + " items)"
            }
            return text
        }
        if (family === "ui_presentation") {
            const taskType = (entry.taskType || "").toString()
            const action = (entry.action || "").toString()
            const surfaceKind = (entry.surfaceKind || "").toString()
            const reasonCode = (entry.reasonCode || "").toString()
            const urgencyBand = (entry.urgencyBand || "").toString()
            const burstPressureBand = (entry.burstPressureBand || "").toString()
            let text = "UI presentation " + (action || "decision") + (taskType ? " for " + taskType : "")
            if (surfaceKind.length > 0) {
                text += " [" + surfaceKind + "]"
            }
            if (urgencyBand.length > 0 || burstPressureBand.length > 0) {
                text += " |"
                if (urgencyBand.length > 0) {
                    text += " urgency " + urgencyBand
                }
                if (burstPressureBand.length > 0) {
                    text += (urgencyBand.length > 0 ? " |" : "") + " burst " + burstPressureBand
                }
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
            text += contextDeltaFormatter.contextConfidenceDeltaText(entry, behaviorEntries)
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
            text += contextDeltaFormatter.contextConfidenceDeltaText(entry, behaviorEntries)
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
            text: "Perception, context, cooldown, selection, proposal, risk, permission, confirmation, and UI events from the behavioral ledger."
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
