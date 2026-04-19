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
    function burstCountDeltaText(previousEntry, currentEntry) {
        const previousSeen = Number(cooldownDetail(previousEntry, "connectorKindRecentSeenCount") || 0)
        const previousPresented = Number(cooldownDetail(previousEntry, "connectorKindRecentPresentedCount") || 0)
        const previousHistory = Number(cooldownDetail(previousEntry, "historySeenCount") || 0)
        const currentSeen = Number(cooldownDetail(currentEntry, "connectorKindRecentSeenCount") || 0)
        const currentPresented = Number(cooldownDetail(currentEntry, "connectorKindRecentPresentedCount") || 0)
        const currentHistory = Number(cooldownDetail(currentEntry, "historySeenCount") || 0)
        const parts = []
        if (previousSeen !== currentSeen) {
            parts.push("burst seen " + previousSeen + " -> " + currentSeen)
        }
        if (previousPresented !== currentPresented) {
            parts.push("burst presented " + previousPresented + " -> " + currentPresented)
        }
        if (previousHistory !== currentHistory) {
            parts.push("history seen " + previousHistory + " -> " + currentHistory)
        }
        return parts.join(" ; ")
    }
    function urgencyBurstBandDeltaText(previousEntry, currentEntry) {
        const previousUrgency = (cooldownDetail(previousEntry, "urgencyBand") || "").toString().trim()
        const previousBurstBand = (cooldownDetail(previousEntry, "burstPressureBand") || "").toString().trim()
        const currentUrgency = (cooldownDetail(currentEntry, "urgencyBand") || "").toString().trim()
        const currentBurstBand = (cooldownDetail(currentEntry, "burstPressureBand") || "").toString().trim()
        const parts = []
        if (previousUrgency.length > 0 && currentUrgency.length > 0 && previousUrgency !== currentUrgency) {
            parts.push("urgency band " + previousUrgency + " -> " + currentUrgency)
        }
        if (previousBurstBand.length > 0 && currentBurstBand.length > 0 && previousBurstBand !== currentBurstBand) {
            parts.push("burst band " + previousBurstBand + " -> " + currentBurstBand)
        }
        return parts.join(" ; ")
    }
    function proposalEvidenceDeltaText(previousEntry, currentEntry) {
        const previousSource = (previousEntry.sourceLabel || "").toString().trim()
        const currentSource = (currentEntry.sourceLabel || "").toString().trim()
        const previousKey = (previousEntry.presentationKeyHint || "").toString().trim()
        const currentKey = (currentEntry.presentationKeyHint || "").toString().trim()
        const previousProposalReason = (previousEntry.proposalReasonCode || "").toString().trim()
        const currentProposalReason = (currentEntry.proposalReasonCode || "").toString().trim()
        const parts = []
        if (previousSource.length > 0 && currentSource.length > 0 && previousSource !== currentSource) {
            parts.push("source " + previousSource + " -> " + currentSource)
        }
        if (previousKey.length > 0 && currentKey.length > 0 && previousKey !== currentKey) {
            parts.push("key " + previousKey + " -> " + currentKey)
        }
        if (previousProposalReason.length > 0
                && currentProposalReason.length > 0
                && previousProposalReason !== currentProposalReason) {
            parts.push("proposal reason " + previousProposalReason + " -> " + currentProposalReason)
        }
        return parts.join(" ; ")
    }
    function rankedTitles(entry) {
        return toStringList(entry.rankedTitles)
    }
    function rankedOrderPreview(titles) {
        if (!titles || titles.length === 0) {
            return ""
        }
        const head = titles.slice(0, 3).join(" > ")
        return titles.length > 3 ? head + " ..." : head
    }
    function rankedOrderingDeltaText(entry, behaviorEntries) {
        if ((entry.stage || "").toString() !== "ranked") {
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
        const currentRankedTitles = rankedTitles(entry)
        for (let i = index + 1; i < rows.length; ++i) {
            const previous = rows[i] || {}
            if ((previous.family || "").toString() !== "action_proposal"
                    || (previous.stage || "").toString() !== "ranked"
                    || (previous.threadId || "").toString().trim() !== threadId) {
                continue
            }
            const previousRankedTitles = rankedTitles(previous)
            if (previousRankedTitles.length === 0 || currentRankedTitles.length === 0) {
                return ""
            }
            if (previousRankedTitles.join("|") === currentRankedTitles.join("|")) {
                return ""
            }
            const previousTop = previousRankedTitles[0]
            const currentTop = currentRankedTitles[0]
            const parts = []
            if (previousTop && currentTop && previousTop !== currentTop) {
                parts.push("top " + previousTop + " -> " + currentTop)
            }
            parts.push("order " + rankedOrderPreview(previousRankedTitles) + " -> " + rankedOrderPreview(currentRankedTitles))
            return " | delta " + parts.join(" ; ")
        }
        return ""
    }
    function rankedScores(entry) {
        const rows = entry.rankedScores || []
        const scores = []
        for (let i = 0; i < rows.length; ++i) {
            const value = finiteNumberOrNull(rows[i])
            if (value !== null) scores.push(value)
        }
        return scores
    }
    function rankedScorePreview(scores) {
        if (!scores || scores.length === 0) return ""
        const values = []
        for (let i = 0; i < Math.min(scores.length, 3); ++i) values.push(scores[i].toFixed(2))
        const head = values.join(" > ")
        return scores.length > 3 ? head + " ..." : head
    }
    function rankedScoreDeltaText(entry, behaviorEntries) {
        if ((entry.stage || "").toString() !== "ranked") return ""
        const rows = behaviorEntries || []
        const index = eventIndex(entry, rows)
        if (index < 0) return ""
        const threadId = (entry.threadId || "").toString().trim()
        if (threadId.length === 0) return ""
        const currentScores = rankedScores(entry)
        for (let i = index + 1; i < rows.length; ++i) {
            const previous = rows[i] || {}
            if ((previous.family || "").toString() !== "action_proposal"
                    || (previous.stage || "").toString() !== "ranked"
                    || (previous.threadId || "").toString().trim() !== threadId) {
                continue
            }
            const previousScores = rankedScores(previous)
            if (previousScores.length === 0 || currentScores.length === 0) return ""
            const previousJoined = previousScores.map((v) => v.toFixed(2)).join("|")
            const currentJoined = currentScores.map((v) => v.toFixed(2)).join("|")
            if (previousJoined === currentJoined) return ""
            const parts = []
            const topDelta = scoreDeltaText("top score", previousScores[0], currentScores[0])
            if (topDelta.length > 0) parts.push(topDelta)
            const previousSpread = previousScores.length > 1 ? previousScores[0] - previousScores[1] : null
            const currentSpread = currentScores.length > 1 ? currentScores[0] - currentScores[1] : null
            const spreadDelta = scoreDeltaText("top gap", previousSpread, currentSpread)
            if (spreadDelta.length > 0) parts.push(spreadDelta)
            parts.push("scores " + rankedScorePreview(previousScores) + " -> " + rankedScorePreview(currentScores))
            return " | delta " + parts.join(" ; ")
        }
        return ""
    }
    function generatedProposalDeltaText(entry, behaviorEntries) {
        if ((entry.stage || "").toString() !== "generated") return ""
        const rows = behaviorEntries || []
        const index = eventIndex(entry, rows)
        if (index < 0) return ""
        const threadId = (entry.threadId || "").toString().trim()
        if (threadId.length === 0) return ""
        const currentCount = Number(entry.proposalCount || 0)
        const currentTitles = toStringList(entry.proposalTitles)
        for (let i = index + 1; i < rows.length; ++i) {
            const previous = rows[i] || {}
            if ((previous.family || "").toString() !== "action_proposal"
                    || (previous.stage || "").toString() !== "generated"
                    || (previous.threadId || "").toString().trim() !== threadId) {
                continue
            }
            const previousCount = Number(previous.proposalCount || 0)
            const previousTitles = toStringList(previous.proposalTitles)
            const parts = []
            if (previousCount !== currentCount) {
                parts.push("count " + previousCount + " -> " + currentCount)
            }
            if (previousTitles.join("|") !== currentTitles.join("|")) {
                const prevPreview = previousTitles.slice(0, 3).join(", ")
                const currPreview = currentTitles.slice(0, 3).join(", ")
                parts.push("titles [" + prevPreview + "] -> [" + currPreview + "]")
            }
            return parts.length > 0 ? " | delta " + parts.join(" ; ") : ""
        }
        return ""
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
            const burstCountDelta = burstCountDeltaText(previous, entry)
            if (burstCountDelta.length > 0) {
                parts.push(burstCountDelta)
            }
            const urgencyBurstBandDelta = urgencyBurstBandDeltaText(previous, entry)
            if (urgencyBurstBandDelta.length > 0) {
                parts.push(urgencyBurstBandDelta)
            }
            const proposalEvidenceDelta = proposalEvidenceDeltaText(previous, entry)
            if (proposalEvidenceDelta.length > 0) {
                parts.push(proposalEvidenceDelta)
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
        const gateDeltaText = proposalGateDeltaText(entry, behaviorEntries)
        const rankedDeltaText = rankedOrderingDeltaText(entry, behaviorEntries)
        const rankedScoreDelta = rankedScoreDeltaText(entry, behaviorEntries)
        if (stage === "generated") {
            const proposalCount = entry.proposalCount || 0
            const proposalTitles = entry.proposalTitles || []
            let text = "Generated " + proposalCount + " suggestion proposals: " + proposalTitles.join(", ")
            const generatedDelta = generatedProposalDeltaText(entry, behaviorEntries)
            if (generatedDelta.length > 0) {
                text += generatedDelta
            }
            return text
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
            if (rankedDeltaText.length > 0) {
                text += rankedDeltaText
            }
            if (rankedScoreDelta.length > 0) {
                text += rankedScoreDelta
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
            if (gateDeltaText.length > 0) {
                text += gateDeltaText
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
        if (gateDeltaText.length > 0) {
            text += gateDeltaText
        }
        return text
    }
}
