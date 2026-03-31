#include <QtTest>

#include <QDir>
#include <QFileInfo>
#include <QScopeGuard>
#include <QTemporaryDir>

#include "python/PythonRuntimeManager.h"

class PythonRuntimeTests : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void listsRuntimeCatalog();
    void executesFileRoundTrip();
    void executesShellCommand();
    void executesBrowserFetchText();
    void prefersPackagedRuntimeOverPythonFallback();

private:
    QTemporaryDir m_tempDir;
    QString m_savedCwd;
    QByteArray m_savedWindowHideSetting;
};

void PythonRuntimeTests::init()
{
    m_savedCwd = QDir::currentPath();
    QVERIFY(m_tempDir.isValid());
    m_savedWindowHideSetting = qgetenv("JARVIS_DISABLE_PYTHON_RUNTIME_WINDOW_HIDE");
    qputenv("JARVIS_DISABLE_PYTHON_RUNTIME_WINDOW_HIDE", QByteArrayLiteral("1"));
    QDir::setCurrent(m_tempDir.path());
}

void PythonRuntimeTests::cleanup()
{
    if (m_savedWindowHideSetting.isEmpty()) {
        qunsetenv("JARVIS_DISABLE_PYTHON_RUNTIME_WINDOW_HIDE");
    } else {
        qputenv("JARVIS_DISABLE_PYTHON_RUNTIME_WINDOW_HIDE", m_savedWindowHideSetting);
    }
    QDir::setCurrent(m_savedCwd);
}

void PythonRuntimeTests::listsRuntimeCatalog()
{
    PythonRuntimeManager runtime({m_tempDir.path()});
    QString error;
    QVERIFY2(runtime.isAvailable(&error), qPrintable(error));

    const QJsonArray catalog = runtime.listCatalog(&error);
    QVERIFY2(!catalog.isEmpty(), qPrintable(error));

    QStringList names;
    for (const QJsonValue &value : catalog) {
        names.push_back(value.toObject().value(QStringLiteral("name")).toString());
    }

    QVERIFY(names.contains(QStringLiteral("file_read")));
    QVERIFY(names.contains(QStringLiteral("shell_run")));
}

void PythonRuntimeTests::executesFileRoundTrip()
{
    PythonRuntimeManager runtime({m_tempDir.path()});
    QString error;

    const QJsonObject writeResult = runtime.executeAction(
        QStringLiteral("file_write"),
        QJsonObject{
            {QStringLiteral("path"), QStringLiteral("note.txt")},
            {QStringLiteral("content"), QStringLiteral("hello runtime")}
        },
        {},
        &error);
    QVERIFY2(!writeResult.isEmpty(), qPrintable(error));
    QVERIFY(writeResult.value(QStringLiteral("ok")).toBool());

    const QJsonObject readResult = runtime.executeAction(
        QStringLiteral("file_read"),
        QJsonObject{{QStringLiteral("path"), QStringLiteral("note.txt")}},
        {},
        &error);
    QVERIFY2(!readResult.isEmpty(), qPrintable(error));
    QVERIFY(readResult.value(QStringLiteral("ok")).toBool());
    QCOMPARE(readResult.value(QStringLiteral("payload")).toObject().value(QStringLiteral("text")).toString(),
             QStringLiteral("hello runtime"));
}

void PythonRuntimeTests::executesShellCommand()
{
    PythonRuntimeManager runtime({m_tempDir.path()});
    QString error;

#if defined(Q_OS_WIN)
    const QString command = QStringLiteral("Write-Output 'runtime ok'");
#else
    const QString command = QStringLiteral("printf 'runtime ok'");
#endif

    const QJsonObject result = runtime.executeAction(
        QStringLiteral("shell_run"),
        QJsonObject{{QStringLiteral("command"), command}},
        {},
        &error);
    QVERIFY2(!result.isEmpty(), qPrintable(error));
    QVERIFY(result.value(QStringLiteral("ok")).toBool());

    const QJsonObject payload = result.value(QStringLiteral("payload")).toObject();
    QVERIFY(payload.value(QStringLiteral("stdout")).toString().contains(QStringLiteral("runtime ok")));
}

void PythonRuntimeTests::executesBrowserFetchText()
{
    const QString sourceBrowsers = QStringLiteral(JARVIS_SOURCE_DIR) + QStringLiteral("/python_runtime/ms-playwright");
    const QString packagedBrowsers = QStringLiteral(JARVIS_SOURCE_DIR) + QStringLiteral("/bin/python_runtime/ms-playwright");
    if (!QFileInfo::exists(sourceBrowsers) && !QFileInfo::exists(packagedBrowsers)) {
        QSKIP("Playwright browser assets are not available.");
    }

    PythonRuntimeManager runtime({m_tempDir.path()});
    QString error;

    const QJsonObject result = runtime.executeAction(
        QStringLiteral("browser_fetch_text"),
        QJsonObject{{QStringLiteral("url"), QStringLiteral("data:text/html,<html><body><h1>runtime browser ok</h1></body></html>")}},
        {},
        &error);
    QVERIFY2(!result.isEmpty(), qPrintable(error));
    QVERIFY(result.value(QStringLiteral("ok")).toBool());

    const QJsonObject payload = result.value(QStringLiteral("payload")).toObject();
    QVERIFY(payload.value(QStringLiteral("text")).toString().contains(QStringLiteral("runtime browser ok")));
}

void PythonRuntimeTests::prefersPackagedRuntimeOverPythonFallback()
{
#if defined(Q_OS_WIN)
    const QString packagedRuntime = QStringLiteral(JARVIS_SOURCE_DIR) + QStringLiteral("/bin/python_runtime/vaxil_python_runtime.exe");
    if (!QFileInfo::exists(packagedRuntime)) {
        QSKIP("Packaged Python runtime sidecar is not available.");
    }

    const QByteArray previousPythonExecutable = qgetenv("VAXIL_PYTHON_EXECUTABLE");
    const auto restorePythonExecutable = qScopeGuard([&previousPythonExecutable]() {
        if (previousPythonExecutable.isEmpty()) {
            qunsetenv("VAXIL_PYTHON_EXECUTABLE");
        } else {
            qputenv("VAXIL_PYTHON_EXECUTABLE", previousPythonExecutable);
        }
    });
    qputenv("VAXIL_PYTHON_EXECUTABLE", QByteArrayLiteral("Z:/missing/python.exe"));

    PythonRuntimeManager runtime({m_tempDir.path()});
    QString error;
    QVERIFY2(runtime.isAvailable(&error), qPrintable(error));

    const QJsonObject result = runtime.executeAction(
        QStringLiteral("shell_run"),
        QJsonObject{{QStringLiteral("command"), QStringLiteral("Write-Output 'packaged runtime ok'")}},
        {},
        &error);
    QVERIFY2(!result.isEmpty(), qPrintable(error));
    QVERIFY(result.value(QStringLiteral("ok")).toBool());
#else
    QSKIP("Packaged runtime preference is only relevant on Windows.");
#endif
}

QTEST_GUILESS_MAIN(PythonRuntimeTests)
#include "PythonRuntimeTests.moc"
