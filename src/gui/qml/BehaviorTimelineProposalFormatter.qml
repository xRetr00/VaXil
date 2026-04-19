import QtQml

QtObject {
    function finiteNumberOrNull(value) {
        const numberValue = Number(value)
        return Number.isFinite(numberValue) ? numberValue : null
    }

    function scoreDeltaText(label, previousValue, currentValue) {
        const current = finiteNumberOrNull(currentValue)
        const previous = finiteNumberOrNull(previousValue)
        if (current === null || previous === null) {
            return ""
        }
        const delta = current - previous
        if (Math.abs(delta) < 0.01) {
            return ""
        }
        const direction = delta > 0 ? "up" : "down"
        const signedDelta = (delta > 0 ? "+" : "") + delta.toFixed(2)
        return label + " " + previous.toFixed(2) + " -> " + current.toFixed(2) + " (" + direction + " " + signedDelta + ")"
    }

    function noveltySetDeltaText(previousEntry, currentEntry) {
        const previousTags = noveltyReasonTags(previousEntry)
        const currentTags = noveltyReasonTags(currentEntry)
        const added = []
        const removed = []
        for (let i = 0; i < currentTags.length; ++i) {
            const tag = currentTags[i]
            if (previousTags.indexOf(tag) < 0) {
                added.push(tag)
            }
        }
        for (let i = 0; i < previousTags.length; ++i) {
            const tag = previousTags[i]
            if (currentTags.indexOf(tag) < 0) {
                removed.push(tag)
            }
        }
        const parts = []
        if (added.length > 0) {
            parts.push("novelty + " + added.join(", "))
        }
        if (removed.length > 0) {
            parts.push("novelty - " + removed.join(", "))
        }
        return parts.join(" ; ")
    }

    function toStringList(value) {
        const rows = value || []
        const parts = []
        for (let i = 0; i < rows.length; ++i) {
            const text = (rows[i] || "").toString().trim()
            if (text.length > 0) {
                parts.push(text)
            }
        }
        return parts
    }

    function cooldownDetail(entry, key) {
        const cooldownDecision = entry.cooldownDecision || {}
        const directValue = entry[key]
        if (directValue !== undefined && directValue !== null && directValue !== "") {
            return directValue
        }
        return cooldownDecision[key]
    }

    function noveltyReasonTags(entry) {
        const directTags = toStringList(cooldownDetail(entry, "noveltyReasonCodes"))
        const tags = []
        for (let i = 0; i < directTags.length; ++i) {
            const tag = directTags[i]
            if (tag.startsWith("novelty.")) {
                tags.push(tag)
            }
        }
        return tags
    }

    function burstCountEvidence(entry) {
        const recentSeenCount = Number(cooldownDetail(entry, "connectorKindRecentSeenCount") || 0)
        const recentPresentedCount = Number(cooldownDetail(entry, "connectorKindRecentPresentedCount") || 0)
        const historySeenCount = Number(cooldownDetail(entry, "historySeenCount") || 0)
        const parts = []
        if (recentSeenCount > 0 || recentPresentedCount > 0) {
            parts.push("burst seen/presented " + recentSeenCount + "/" + recentPresentedCount)
        }
        if (historySeenCount > 0) {
            parts.push("history seen " + historySeenCount)
        }
        return parts.join(" | ")
    }

    function urgencyBurstEvidence(entry) {
        const urgencyBand = (cooldownDetail(entry, "urgencyBand") || "").toString()
        const burstPressureBand = (cooldownDetail(entry, "burstPressureBand") || "").toString()
        const parts = []
        if (urgencyBand.length > 0) {
            parts.push("urgency " + urgencyBand)
        }
        if (burstPressureBand.length > 0) {
            parts.push("burst " + burstPressureBand)
        }
        return parts.join(" | ")
    }

    function proposalEvidence(entry) {
        const sourceLabel = (entry.sourceLabel || "").toString()
        const keyHint = (entry.presentationKeyHint || "").toString()
        const proposalReason = (entry.proposalReasonCode || "").toString()
        const urgencyBurst = urgencyBurstEvidence(entry)
        const burstCounts = burstCountEvidence(entry)
        const noveltyTags = noveltyReasonTags(entry)
        const parts = []
        if (sourceLabel.length > 0) {
            parts.push("source " + sourceLabel)
        }
        if (keyHint.length > 0) {
            parts.push("key " + keyHint)
        }
        if (proposalReason.length > 0) {
            parts.push("reason " + proposalReason)
        }
        if (urgencyBurst.length > 0) {
            parts.push(urgencyBurst)
        }
        if (burstCounts.length > 0) {
            parts.push(burstCounts)
        }
        if (noveltyTags.length > 0) {
            parts.push("novelty " + noveltyTags.join(", "))
        }
        return parts.join(" | ")
    }

    function eventIndex(entry, behaviorEntries) {
        const rows = behaviorEntries || []
        const eventId = (entry.eventId || "").toString()
        if (eventId.length > 0) {
            for (let i = 0; i < rows.length; ++i) {
                const row = rows[i] || {}
                if ((row.eventId || "").toString() === eventId) {
                    return i
                }
            }
        }

        const timestampUtc = (entry.timestampUtc || "").toString()
        const threadId = (entry.threadId || "").toString()
        for (let i = 0; i < rows.length; ++i) {
            const row = rows[i] || {}
            if ((row.timestampUtc || "").toString() === timestampUtc
                    && (row.threadId || "").toString() === threadId
                    && (row.family || "").toString() === (entry.family || "").toString()
                    && (row.stage || "").toString() === (entry.stage || "").toString()) {
                return i
            }
        }
        return -1
    }

    function proposalGateDeltaText(entry, behaviorEntries) {
        if ((entry.stage || "").toString() !== "gated") {
            return ""
        }
        const rows = behaviorEntries || []
        const index = eventIndex(entry, rows)
        if (index < 0) {
            return ""
        }

        const threadId = (entry.threadId || "").toString().trim()
        if (threadId.length === 0) {
            return ""
        }

        const currentAction = (entry.action || "").toString().trim()
        const currentReason = (entry.reasonCode || "").toString().trim()
        const currentConfidence = finiteNumberOrNull(entry.confidenceScore)
        const currentNovelty = finiteNumberOrNull(entry.noveltyScore)
        for (let i = index + 1; i < rows.length; ++i) {
            const previous = rows[i] || {}
            if ((previous.family || "").toString() !== "action_proposal"
                    || (previous.stage || "").toString() !== "gated"
                    || (previous.threadId || "").toString().trim() !== threadId) {
                continue
            }

            const previousAction = (previous.action || "").toString().trim()
            const previousReason = (previous.reasonCode || "").toString().trim()
            const parts = []
            if (previousAction.length > 0 && currentAction.length > 0 && previousAction !== currentAction) {
                parts.push("action " + previousAction + " -> " + currentAction)
            }
            if (previousReason.length > 0 && currentReason.length > 0 && previousReason !== currentReason) {
                parts.push("reason " + previousReason + " -> " + currentReason)
            }
            const confidenceDelta = scoreDeltaText("confidence", previous.confidenceScore, currentConfidence)
            if (confidenceDelta.length > 0) {
                parts.push(confidenceDelta)
            }
            const noveltyDelta = scoreDeltaText("novelty", previous.noveltyScore, currentNovelty)
            if (noveltyDelta.length > 0) {
                parts.push(noveltyDelta)
            }
            const noveltySetDelta = noveltySetDeltaText(previous, entry)
            if (noveltySetDelta.length > 0) {
                parts.push(noveltySetDelta)
            }
            if (parts.length > 0) {
                return " | delta " + parts.join(" ; ")
            }
            return ""
        }

        return ""
    }

    function summaryText(entry, behaviorEntries) {
        const stage = (entry.stage || "").toString()
        const title = (entry.title || "").toString()
        const action = (entry.action || "").toString()
        const priority = (entry.priority || "").toString()
        const reasonCode = (entry.reasonCode || "").toString()
        const evidence = proposalEvidence(entry)
        const deltaText = proposalGateDeltaText(entry, behaviorEntries)

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
            if (evidence.length > 0) {
                text += " | " + evidence
            }
            if (reasonCode.length > 0) {
                text += " because " + reasonCode
            }
            if (deltaText.length > 0) {
                text += deltaText
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
            if (evidence.length > 0) {
                text += " | " + evidence
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
        if (evidence.length > 0) {
            text += " | " + evidence
        }
        if (reasonCode.length > 0) {
            text += " because " + reasonCode
        }
        if (deltaText.length > 0) {
            text += deltaText
        }
        return text
    }
}
