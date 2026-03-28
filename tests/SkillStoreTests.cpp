#include <QtTest>

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

#include "skills/SkillStore.h"

class SkillStoreTests : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void createSkillCreatesExpectedFiles();
    void createSkillNormalizesId();
    void createSkillRejectsEmptyId();
    void createSkillRejectsEmptyName();
    void createSkillRejectsDuplicate();
    void listSkillsReturnsCreatedSkill();
    void listSkillsEmptyWhenNoneCreated();
    void installSkillFailsOnNonWindows();

private:
    QTemporaryDir m_tempDir;
    QString m_savedCwd;
};

void SkillStoreTests::init()
{
    m_savedCwd = QDir::currentPath();
    QDir::setCurrent(m_tempDir.path());
}

void SkillStoreTests::cleanup()
{
    QDir(m_tempDir.path() + QStringLiteral("/skills")).removeRecursively();
    QDir::setCurrent(m_savedCwd);
}

void SkillStoreTests::createSkillCreatesExpectedFiles()
{
    SkillStore store;
    QVERIFY(store.createSkill(
        QStringLiteral("hello-world"),
        QStringLiteral("Hello World"),
        QStringLiteral("Greets the user")));

    const QString root = store.skillsRoot() + QStringLiteral("/hello-world");
    QVERIFY(QFileInfo::exists(root + QStringLiteral("/skill.json")));
    QVERIFY(QFileInfo::exists(root + QStringLiteral("/README.md")));
    QVERIFY(QFileInfo::exists(root + QStringLiteral("/prompt.txt")));
}

void SkillStoreTests::createSkillNormalizesId()
{
    SkillStore store;
    QVERIFY(store.createSkill(
        QStringLiteral("Hello World Skill"),
        QStringLiteral("Hello World"),
        QStringLiteral("Greets the user")));

    const QString normalizedRoot = store.skillsRoot() + QStringLiteral("/hello-world-skill");
    QVERIFY(QFileInfo::exists(normalizedRoot + QStringLiteral("/skill.json")));
}

void SkillStoreTests::createSkillRejectsEmptyId()
{
    SkillStore store;
    QString error;
    QVERIFY(!store.createSkill(
        QStringLiteral(""),
        QStringLiteral("Hello World"),
        QStringLiteral("Greets the user"),
        &error));
    QVERIFY(!error.isEmpty());
}

void SkillStoreTests::createSkillRejectsEmptyName()
{
    SkillStore store;
    QString error;
    QVERIFY(!store.createSkill(
        QStringLiteral("valid-id"),
        QStringLiteral(""),
        QStringLiteral("Description"),
        &error));
    QVERIFY(!error.isEmpty());
}

void SkillStoreTests::createSkillRejectsDuplicate()
{
    SkillStore store;
    QVERIFY(store.createSkill(
        QStringLiteral("my-skill"),
        QStringLiteral("My Skill"),
        QStringLiteral("A test skill")));

    QString error;
    QVERIFY(!store.createSkill(
        QStringLiteral("my-skill"),
        QStringLiteral("My Skill"),
        QStringLiteral("A test skill"),
        &error));
    QCOMPARE(error, QStringLiteral("Skill already exists."));
}

void SkillStoreTests::listSkillsReturnsCreatedSkill()
{
    SkillStore store;
    QVERIFY(store.createSkill(
        QStringLiteral("test-skill"),
        QStringLiteral("Test Skill"),
        QStringLiteral("A test skill")));

    const auto skills = store.listSkills();
    QCOMPARE(skills.size(), 1);
    QCOMPARE(skills.first().id, QStringLiteral("test-skill"));
    QCOMPARE(skills.first().name, QStringLiteral("Test Skill"));
    QCOMPARE(skills.first().version, QStringLiteral("1.0.0"));
    QCOMPARE(skills.first().description, QStringLiteral("A test skill"));
    QCOMPARE(skills.first().promptTemplatePath, QStringLiteral("prompt.txt"));
}

void SkillStoreTests::listSkillsEmptyWhenNoneCreated()
{
    SkillStore store;
    QCOMPARE(store.listSkills().size(), 0);
}

void SkillStoreTests::installSkillFailsOnNonWindows()
{
#ifndef Q_OS_WIN
    SkillStore store;
    QString error;
    QVERIFY(!store.installSkill(QStringLiteral("https://github.com/example/skill"), &error));
    QVERIFY(error.contains(QStringLiteral("Windows")));
#else
    QSKIP("Non-Windows only test");
#endif
}

QTEST_APPLESS_MAIN(SkillStoreTests)
#include "SkillStoreTests.moc"
