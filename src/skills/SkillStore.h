#pragma once

#include <QObject>

#include "core/AssistantTypes.h"

class SkillStore : public QObject
{
    Q_OBJECT

public:
    explicit SkillStore(QObject *parent = nullptr);

    QString skillsRoot() const;
    QList<SkillManifest> listSkills() const;
    bool createSkill(const QString &id, const QString &name, const QString &description, QString *error = nullptr) const;
    bool installSkill(const QString &sourceUrl, QString *error = nullptr) const;

private:
    SkillManifest loadManifest(const QString &manifestPath) const;
    bool validateManifest(const SkillManifest &manifest, QString *error) const;
    bool copyDirectory(const QString &sourcePath, const QString &destinationPath, QString *error) const;

    mutable class PythonRuntimeManager *m_pythonRuntime = nullptr;
};
