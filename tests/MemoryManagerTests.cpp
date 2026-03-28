#include <QtTest>

#include <QTemporaryDir>

#include "memory/MemoryManager.h"

class MemoryManagerTests : public QObject
{
    Q_OBJECT

private slots:
    void writesAndRetrievesEntry();
    void rejectsSecretFlaggedEntry();
    void rejectsEntryWithSecretMarkerInKey();
    void rejectsEntryWithSecretMarkerInValue();
    void rejectsEntryWithOversizedValue();
    void rejectsEntryWithTooManyNewlines();
    void rejectsEntryWithEmptyKey();
    void rejectsEntryWithEmptyValue();
    void upsertsExistingEntryById();
    void upsertsExistingEntryByKeyAndType();
    void removesEntryById();
    void removesEntryByKey();
    void removesEntryByTitle();
    void removeFalseWhenNotFound();
    void searchFiltersResults();
    void searchEmptyQueryReturnsAll();
    void returnsUserName();
    void returnsEmptyUserNameWhenNotStored();
};

static MemoryEntry makeEntry(const QString &key, const QString &value, MemoryType type = MemoryType::Fact)
{
    MemoryEntry entry;
    entry.type = type;
    entry.key = key;
    entry.title = key;
    entry.value = value;
    entry.content = value;
    return entry;
}

void MemoryManagerTests::writesAndRetrievesEntry()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MemoryManager manager(dir.path() + QStringLiteral("/memory.json"));

    QVERIFY(manager.write(makeEntry(QStringLiteral("language"), QStringLiteral("C++"))));
    const auto entries = manager.entries();
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().key, QStringLiteral("language"));
    QCOMPARE(entries.first().value, QStringLiteral("C++"));
}

void MemoryManagerTests::rejectsSecretFlaggedEntry()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MemoryManager manager(dir.path() + QStringLiteral("/memory.json"));

    MemoryEntry entry = makeEntry(QStringLiteral("credential"), QStringLiteral("abc123"));
    entry.secret = true;
    QVERIFY(!manager.write(entry));
    QCOMPARE(manager.entries().size(), 0);
}

void MemoryManagerTests::rejectsEntryWithSecretMarkerInKey()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MemoryManager manager(dir.path() + QStringLiteral("/memory.json"));

    QVERIFY(!manager.write(makeEntry(QStringLiteral("api key"), QStringLiteral("some value"))));
    QCOMPARE(manager.entries().size(), 0);
}

void MemoryManagerTests::rejectsEntryWithSecretMarkerInValue()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MemoryManager manager(dir.path() + QStringLiteral("/memory.json"));

    QVERIFY(!manager.write(makeEntry(QStringLiteral("config"), QStringLiteral("password: secret123"))));
    QCOMPARE(manager.entries().size(), 0);
}

void MemoryManagerTests::rejectsEntryWithOversizedValue()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MemoryManager manager(dir.path() + QStringLiteral("/memory.json"));

    const QString hugeValue = QString(2001, QChar::fromLatin1('x'));
    QVERIFY(!manager.write(makeEntry(QStringLiteral("data"), hugeValue)));
    QCOMPARE(manager.entries().size(), 0);
}

void MemoryManagerTests::rejectsEntryWithTooManyNewlines()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MemoryManager manager(dir.path() + QStringLiteral("/memory.json"));

    QString multilineValue;
    for (int i = 0; i < 21; ++i) {
        multilineValue += QStringLiteral("line\n");
    }
    QVERIFY(!manager.write(makeEntry(QStringLiteral("data"), multilineValue)));
    QCOMPARE(manager.entries().size(), 0);
}

void MemoryManagerTests::rejectsEntryWithEmptyKey()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MemoryManager manager(dir.path() + QStringLiteral("/memory.json"));

    QVERIFY(!manager.write(makeEntry(QStringLiteral(""), QStringLiteral("some value"))));
    QCOMPARE(manager.entries().size(), 0);
}

void MemoryManagerTests::rejectsEntryWithEmptyValue()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MemoryManager manager(dir.path() + QStringLiteral("/memory.json"));

    QVERIFY(!manager.write(makeEntry(QStringLiteral("key"), QStringLiteral(""))));
    QCOMPARE(manager.entries().size(), 0);
}

void MemoryManagerTests::upsertsExistingEntryById()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MemoryManager manager(dir.path() + QStringLiteral("/memory.json"));

    QVERIFY(manager.write(makeEntry(QStringLiteral("name"), QStringLiteral("Alice"))));
    QCOMPARE(manager.entries().size(), 1);

    QVERIFY(manager.write(makeEntry(QStringLiteral("name"), QStringLiteral("Bob"))));
    QCOMPARE(manager.entries().size(), 1);
    QCOMPARE(manager.entries().first().value, QStringLiteral("Bob"));
}

void MemoryManagerTests::upsertsExistingEntryByKeyAndType()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MemoryManager manager(dir.path() + QStringLiteral("/memory.json"));

    MemoryEntry a = makeEntry(QStringLiteral("theme"), QStringLiteral("dark"), MemoryType::Preference);
    MemoryEntry b = makeEntry(QStringLiteral("theme"), QStringLiteral("light"), MemoryType::Preference);

    QVERIFY(manager.write(a));
    QVERIFY(manager.write(b));
    QCOMPARE(manager.entries().size(), 1);
    QCOMPARE(manager.entries().first().value, QStringLiteral("light"));
}

void MemoryManagerTests::removesEntryById()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MemoryManager manager(dir.path() + QStringLiteral("/memory.json"));

    QVERIFY(manager.write(makeEntry(QStringLiteral("language"), QStringLiteral("C++"))));
    QCOMPARE(manager.entries().size(), 1);

    const QString id = manager.entries().first().id;
    QVERIFY(manager.remove(id));
    QCOMPARE(manager.entries().size(), 0);
}

void MemoryManagerTests::removesEntryByKey()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MemoryManager manager(dir.path() + QStringLiteral("/memory.json"));

    QVERIFY(manager.write(makeEntry(QStringLiteral("language"), QStringLiteral("C++"))));
    QVERIFY(manager.remove(QStringLiteral("language")));
    QCOMPARE(manager.entries().size(), 0);
}

void MemoryManagerTests::removesEntryByTitle()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MemoryManager manager(dir.path() + QStringLiteral("/memory.json"));

    MemoryEntry entry = makeEntry(QStringLiteral("language"), QStringLiteral("C++"));
    entry.title = QStringLiteral("programming language");
    QVERIFY(manager.write(entry));
    QVERIFY(manager.remove(QStringLiteral("programming language")));
    QCOMPARE(manager.entries().size(), 0);
}

void MemoryManagerTests::removeFalseWhenNotFound()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MemoryManager manager(dir.path() + QStringLiteral("/memory.json"));

    QVERIFY(!manager.remove(QStringLiteral("nonexistent")));
}

void MemoryManagerTests::searchFiltersResults()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MemoryManager manager(dir.path() + QStringLiteral("/memory.json"));

    QVERIFY(manager.write(makeEntry(QStringLiteral("language"), QStringLiteral("C++"))));
    QVERIFY(manager.write(makeEntry(QStringLiteral("os"), QStringLiteral("Linux"))));

    const auto results = manager.search(QStringLiteral("linux"));
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().value, QStringLiteral("Linux"));
}

void MemoryManagerTests::searchEmptyQueryReturnsAll()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MemoryManager manager(dir.path() + QStringLiteral("/memory.json"));

    QVERIFY(manager.write(makeEntry(QStringLiteral("language"), QStringLiteral("C++"))));
    QVERIFY(manager.write(makeEntry(QStringLiteral("os"), QStringLiteral("Linux"))));

    const auto results = manager.search(QStringLiteral(""));
    QCOMPARE(results.size(), 2);
}

void MemoryManagerTests::returnsUserName()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MemoryManager manager(dir.path() + QStringLiteral("/memory.json"));

    MemoryEntry entry = makeEntry(QStringLiteral("name"), QStringLiteral("Alice"), MemoryType::Fact);
    QVERIFY(manager.write(entry));
    QCOMPARE(manager.userName(), QStringLiteral("Alice"));
}

void MemoryManagerTests::returnsEmptyUserNameWhenNotStored()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MemoryManager manager(dir.path() + QStringLiteral("/memory.json"));

    QVERIFY(manager.userName().isEmpty());
}

QTEST_APPLESS_MAIN(MemoryManagerTests)
#include "MemoryManagerTests.moc"
