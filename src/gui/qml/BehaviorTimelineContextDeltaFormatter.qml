import QtQml

QtObject {
    function toStringList(value) {
        const rows = value || []
        const parts = []
        for (let i = 0; i < rows.length; ++i) {
            const text = (rows[i] || "").toString().trim()
            if (text.length > 0) parts.push(text)
        }
        return parts
    }

    function finiteNumberOrNull(value) {
        const numberValue = Number(value)
        return Number.isFinite(numberValue) ? numberValue : null
    }

    function eventIndex(entry, behaviorEntries) {
        const rows = behaviorEntries || []
        const eventId = (entry.eventId || "").toString()
        if (eventId.length > 0) {
            for (let i = 0; i < rows.length; ++i) {
                const row = rows[i] || {}
                if ((row.eventId || "").toString() === eventId) return i
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

    function previousMatching(entry, behaviorEntries) {
        const rows = behaviorEntries || []
        const index = eventIndex(entry, rows)
        if (index < 0) return null
        const family = (entry.family || "").toString()
        const stage = (entry.stage || "").toString()
        const threadId = (entry.threadId || "").toString().trim()
        for (let i = index + 1; i < rows.length; ++i) {
            const previous = rows[i] || {}
            if ((previous.family || "").toString() === family
                    && (previous.stage || "").toString() === stage
                    && (previous.threadId || "").toString().trim() === threadId) {
                return previous
            }
        }
        return null
    }

    function confidenceDeltaText(label, previousValue, currentValue) {
        const previous = finiteNumberOrNull(previousValue)
        const current = finiteNumberOrNull(currentValue)
        if (previous === null || current === null) return ""
        const delta = current - previous
        if (Math.abs(delta) < 0.01) return ""
        const signed = (delta > 0 ? "+" : "") + delta.toFixed(2)
        return label + " " + previous.toFixed(2) + " -> " + current.toFixed(2) + " (" + signed + ")"
    }

    function selectionDeltaText(entry, behaviorEntries) {
        if ((entry.family || "").toString() !== "selection_context") return ""
        const previous = previousMatching(entry, behaviorEntries)
        if (!previous) return ""
        const stage = (entry.stage || "").toString()
        const parts = []
        if (stage === "prompt_context") {
            const prevKeys = toStringList(previous.promptContextKeys)
            const currKeys = toStringList(entry.promptContextKeys)
            const added = []
            const removed = []
            for (let i = 0; i < currKeys.length; ++i) if (prevKeys.indexOf(currKeys[i]) < 0) added.push(currKeys[i])
            for (let i = 0; i < prevKeys.length; ++i) if (currKeys.indexOf(prevKeys[i]) < 0) removed.push(prevKeys[i])
            if (added.length > 0) parts.push("blocks + " + added.join(", "))
            if (removed.length > 0) parts.push("blocks - " + removed.join(", "))
            const stableDelta = Number(entry.stablePromptCycles || 0) - Number(previous.stablePromptCycles || 0)
            if (stableDelta !== 0) parts.push("stable cycles " + Number(previous.stablePromptCycles || 0) + " -> " + Number(entry.stablePromptCycles || 0))
        } else if (stage === "compiled_context_delta") {
            const prevAdded = toStringList(previous.addedKeys).length
            const prevRemoved = toStringList(previous.removedKeys).length
            const currAdded = toStringList(entry.addedKeys).length
            const currRemoved = toStringList(entry.removedKeys).length
            if (prevAdded !== currAdded) parts.push("added keys " + prevAdded + " -> " + currAdded)
            if (prevRemoved !== currRemoved) parts.push("removed keys " + prevRemoved + " -> " + currRemoved)
        } else if (stage === "memory_context") {
            const prevCount = toStringList(previous.activeCommitmentKeys).length
            const currCount = toStringList(entry.activeCommitmentKeys).length
            if (prevCount !== currCount) parts.push("active commitments " + prevCount + " -> " + currCount)
        }
        return parts.length > 0 ? " | delta " + parts.join(" ; ") : ""
    }

    function contextConfidenceDeltaText(entry, behaviorEntries) {
        const family = (entry.family || "").toString()
        if (family !== "perception" && family !== "context_thread") return ""
        const previous = previousMatching(entry, behaviorEntries)
        if (!previous) return ""
        const text = confidenceDeltaText("confidence", previous.confidence, entry.confidence)
        return text.length > 0 ? " | delta " + text : ""
    }
}
