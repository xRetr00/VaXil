#include "core/MemoryPolicyHandler.h"

#include "memory/MemoryStore.h"
#include "settings/IdentityProfileService.h"

MemoryPolicyHandler::MemoryPolicyHandler(IdentityProfileService *identityProfileService, MemoryStore *memoryStore)
    : m_identityProfileService(identityProfileService)
    , m_memoryStore(memoryStore)
{
}

void MemoryPolicyHandler::applyUserInput(const QString &input) const
{
    const QString lowered = input.trimmed().toLower();
    if (lowered.startsWith(QStringLiteral("my name is "))) {
        if (m_identityProfileService) {
            m_identityProfileService->setUserName(input.trimmed().mid(11).trimmed());
        }
        return;
    }

    if (lowered.startsWith(QStringLiteral("i prefer "))) {
        if (m_identityProfileService) {
            m_identityProfileService->setPreference(QStringLiteral("general"), input.trimmed().mid(9).trimmed());
        }
        return;
    }

    captureExplicitMemoryFromInput(input);
}

QList<MemoryRecord> MemoryPolicyHandler::requestMemory(const QString &query, const MemoryRecord &runtimeRecord) const
{
    QList<MemoryRecord> memory = m_memoryStore ? m_memoryStore->relevantMemory(query) : QList<MemoryRecord>{};
    if (!runtimeRecord.key.trimmed().isEmpty()) {
        memory.push_front(runtimeRecord);
    }
    return memory;
}

void MemoryPolicyHandler::captureExplicitMemoryFromInput(const QString &input) const
{
    const QString lowered = input.trimmed().toLower();
    if (!lowered.startsWith(QStringLiteral("remember "))
        && !lowered.startsWith(QStringLiteral("remember that "))
        && !lowered.startsWith(QStringLiteral("save this preference "))) {
        return;
    }

    if (m_memoryStore) {
        m_memoryStore->extractUserFacts(input);
    }
}
