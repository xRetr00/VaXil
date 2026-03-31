#include "python/PythonRuntimeManager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonValue>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>

#include "logging/LoggingService.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
constexpr int kStartupTimeoutMs = 8000;
constexpr int kDefaultRequestTimeoutMs = 30000;

QString normalizeCandidatePath(const QString &path)
{
    if (path.trimmed().isEmpty()) {
        return {};
    }

    const QFileInfo info(path);
    return info.exists() ? info.absoluteFilePath() : QString{};
}

QString normalizeExistingDirectoryPath(const QString &path)
{
    if (path.trimmed().isEmpty()) {
        return {};
    }

    const QFileInfo info(path);
    return info.exists() && info.isDir() ? info.absoluteFilePath() : QString{};
}
}

PythonRuntimeManager::PythonRuntimeManager(const QStringList &allowedRoots,
                                           LoggingService *loggingService,
                                           QObject *parent)
    : QObject(parent)
    , m_allowedRoots(allowedRoots)
    , m_loggingService(loggingService)
{
}

PythonRuntimeManager::~PythonRuntimeManager()
{
    shutdown();
}

bool PythonRuntimeManager::isAvailable(QString *error)
{
    return ensureStarted(error);
}

QJsonArray PythonRuntimeManager::listCatalog(QString *error)
{
    if (m_catalogLoaded) {
        return m_catalogCache;
    }

    const QJsonObject response = call(QStringLiteral("catalog.list"), {}, kDefaultRequestTimeoutMs, error);
    if (response.isEmpty()) {
        return {};
    }

    const QJsonArray catalog = response.value(QStringLiteral("actions")).toArray();
    m_catalogCache = catalog;
    m_catalogLoaded = true;
    return m_catalogCache;
}

bool PythonRuntimeManager::supportsAction(const QString &name, QString *error)
{
    const QJsonArray catalog = listCatalog(error);
    for (const QJsonValue &value : catalog) {
        const QJsonObject spec = value.toObject();
        if (spec.value(QStringLiteral("name")).toString() == name) {
            return true;
        }
    }
    return false;
}

QJsonObject PythonRuntimeManager::executeAction(const QString &name,
                                                const QJsonObject &args,
                                                const QJsonObject &context,
                                                QString *error)
{
    QJsonObject params;
    params.insert(QStringLiteral("name"), name);
    params.insert(QStringLiteral("args"), args);
    params.insert(QStringLiteral("context"), context);
    return call(QStringLiteral("action.execute"), params, kDefaultRequestTimeoutMs, error);
}

QJsonArray PythonRuntimeManager::listSkills(QString *error)
{
    const QJsonObject response = call(QStringLiteral("skill.list"), {}, kDefaultRequestTimeoutMs, error);
    return response.value(QStringLiteral("skills")).toArray();
}

QJsonObject PythonRuntimeManager::createSkill(const QString &id,
                                              const QString &name,
                                              const QString &description,
                                              QString *error)
{
    QJsonObject params;
    params.insert(QStringLiteral("id"), id);
    params.insert(QStringLiteral("name"), name);
    params.insert(QStringLiteral("description"), description);
    return call(QStringLiteral("skill.create"), params, kDefaultRequestTimeoutMs, error);
}

QJsonObject PythonRuntimeManager::installSkill(const QString &url, QString *error)
{
    QJsonObject params;
    params.insert(QStringLiteral("url"), url);
    return call(QStringLiteral("skill.install"), params, 180000, error);
}

void PythonRuntimeManager::shutdown()
{
    if (!m_process) {
        return;
    }

    if (m_process->state() != QProcess::NotRunning) {
        m_process->write(R"({"jsonrpc":"2.0","method":"shutdown"})" "\n");
        m_process->waitForBytesWritten(1000);
        m_process->closeWriteChannel();
        if (!m_process->waitForFinished(1500)) {
            m_process->kill();
            m_process->waitForFinished(1000);
        }
    }

    logStdErr();
    m_process->deleteLater();
    m_process = nullptr;
    m_initialized = false;
    invalidateCatalog();
}

QString PythonRuntimeManager::resolvePythonExecutable() const
{
    const QString envPath = normalizeCandidatePath(qEnvironmentVariable("VAXIL_PYTHON_EXECUTABLE"));
    if (!envPath.isEmpty()) {
        return envPath;
    }

#ifdef Q_OS_WIN
    const QString venvCandidate = normalizeCandidatePath(QStringLiteral(JARVIS_SOURCE_DIR) + QStringLiteral("/python_runtime/.venv/Scripts/python.exe"));
#else
    const QString venvCandidate = normalizeCandidatePath(QStringLiteral(JARVIS_SOURCE_DIR) + QStringLiteral("/python_runtime/.venv/bin/python"));
#endif
    if (!venvCandidate.isEmpty()) {
        return venvCandidate;
    }

    const QString python = QStandardPaths::findExecutable(QStringLiteral("python"));
    if (!python.isEmpty()) {
        return python;
    }

    return QStandardPaths::findExecutable(QStringLiteral("python3"));
}

QString PythonRuntimeManager::resolvePackagedRuntimeExecutable() const
{
    const QString envPath = normalizeCandidatePath(qEnvironmentVariable("VAXIL_PYTHON_RUNTIME_EXECUTABLE"));
    if (!envPath.isEmpty()) {
        return envPath;
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates{
        appDir + QStringLiteral("/python_runtime/vaxil_python_runtime.exe"),
        QStringLiteral(JARVIS_SOURCE_DIR) + QStringLiteral("/bin/python_runtime/vaxil_python_runtime.exe")
    };

    for (const QString &candidate : candidates) {
        const QString normalized = normalizeCandidatePath(candidate);
        if (!normalized.isEmpty()) {
            return normalized;
        }
    }

    return {};
}

QString PythonRuntimeManager::resolveRuntimeScriptPath() const
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates{
        QStringLiteral(JARVIS_SOURCE_DIR) + QStringLiteral("/python_runtime/vaxil_runtime/main.py"),
        appDir + QStringLiteral("/python_runtime/vaxil_runtime/main.py")
    };

    for (const QString &candidate : candidates) {
        const QString normalized = normalizeCandidatePath(candidate);
        if (!normalized.isEmpty()) {
            return normalized;
        }
    }

    return {};
}

QString PythonRuntimeManager::resolvePlaywrightBrowsersPath() const
{
    const QString overridePath = normalizeExistingDirectoryPath(qEnvironmentVariable("VAXIL_PLAYWRIGHT_BROWSERS_PATH"));
    if (!overridePath.isEmpty()) {
        return overridePath;
    }

    const QString envPath = normalizeExistingDirectoryPath(qEnvironmentVariable("PLAYWRIGHT_BROWSERS_PATH"));
    if (!envPath.isEmpty()) {
        return envPath;
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates{
        appDir + QStringLiteral("/python_runtime/ms-playwright"),
        QStringLiteral(JARVIS_SOURCE_DIR) + QStringLiteral("/bin/python_runtime/ms-playwright"),
        QStringLiteral(JARVIS_SOURCE_DIR) + QStringLiteral("/python_runtime/ms-playwright")
    };

    for (const QString &candidate : candidates) {
        const QString normalized = normalizeExistingDirectoryPath(candidate);
        if (!normalized.isEmpty()) {
            return normalized;
        }
    }

    return {};
}

QJsonObject PythonRuntimeManager::makeInitializeParams() const
{
    QJsonObject params;
    const QString sourceRoot = QDir(QStringLiteral(JARVIS_SOURCE_DIR)).exists()
                                   ? QStringLiteral(JARVIS_SOURCE_DIR)
                                   : QCoreApplication::applicationDirPath();
    params.insert(QStringLiteral("workspace_root"), QDir::currentPath());
    params.insert(QStringLiteral("source_root"), sourceRoot);
    params.insert(QStringLiteral("application_dir"), QCoreApplication::applicationDirPath());
    params.insert(QStringLiteral("skills_root"), QDir::cleanPath(QDir::currentPath() + QStringLiteral("/skills")));

    QJsonArray roots;
    for (const QString &root : m_allowedRoots) {
        roots.push_back(root);
    }
    params.insert(QStringLiteral("allowed_roots"), roots);
    return params;
}

bool PythonRuntimeManager::ensureStarted(QString *error)
{
    if (m_process && m_process->state() != QProcess::NotRunning && m_initialized) {
        return true;
    }

    const QString packagedExecutable = resolvePackagedRuntimeExecutable();
    const bool usePackagedRuntime = !packagedExecutable.isEmpty();

    QString program;
    QStringList arguments;
    QString workingDirectory;
    if (usePackagedRuntime) {
        program = packagedExecutable;
        workingDirectory = QFileInfo(packagedExecutable).absolutePath();
    } else {
        const QString pythonExecutable = resolvePythonExecutable();
        if (pythonExecutable.isEmpty()) {
            if (error) {
                *error = QStringLiteral("Python executable not found.");
            }
            return false;
        }

        const QString scriptPath = resolveRuntimeScriptPath();
        if (scriptPath.isEmpty()) {
            if (error) {
                *error = QStringLiteral("Python runtime entrypoint not found.");
            }
            return false;
        }

        program = pythonExecutable;
        arguments = {scriptPath};
        workingDirectory = QStringLiteral(JARVIS_SOURCE_DIR);
    }

    if (!m_process) {
        m_process = new QProcess(this);
        m_process->setProcessChannelMode(QProcess::SeparateChannels);
    } else if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }

    invalidateCatalog();
    m_initialized = false;
    m_process->setWorkingDirectory(workingDirectory);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString browsersPath = resolvePlaywrightBrowsersPath();
    if (!browsersPath.isEmpty()) {
        env.insert(QStringLiteral("PLAYWRIGHT_BROWSERS_PATH"), browsersPath);
    }
    m_process->setProcessEnvironment(env);

#ifdef Q_OS_WIN
    if (qEnvironmentVariableIntValue("JARVIS_DISABLE_PYTHON_RUNTIME_WINDOW_HIDE") == 0) {
        m_process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
            args->flags |= CREATE_NO_WINDOW;
        });
    } else {
        m_process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *) {});
    }
#endif

    m_process->start(program, arguments);
    if (!m_process->waitForStarted(kStartupTimeoutMs)) {
        if (error) {
            *error = QStringLiteral("Failed to start Python runtime.");
        }
        logStdErr();
        return false;
    }

    QString initError;
    const QJsonObject initResponse = call(QStringLiteral("initialize"), makeInitializeParams(), kStartupTimeoutMs, &initError);
    if (initResponse.isEmpty()) {
        if (error) {
            *error = initError.isEmpty() ? QStringLiteral("Failed to initialize Python runtime.") : initError;
        }
        shutdown();
        return false;
    }

    m_initialized = true;
    return true;
}

QJsonObject PythonRuntimeManager::call(const QString &method,
                                       const QJsonObject &params,
                                       int timeoutMs,
                                       QString *error)
{
    const bool isInitialize = method == QStringLiteral("initialize");
    if (!isInitialize && !ensureStarted(error)) {
        return {};
    }
    if (isInitialize && (!m_process || m_process->state() == QProcess::NotRunning)) {
        if (error) {
            *error = QStringLiteral("Python runtime is not running.");
        }
        return {};
    }

    const qint64 requestId = m_nextRequestId++;
    const QJsonObject request{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), QString::number(requestId)},
        {QStringLiteral("method"), method},
        {QStringLiteral("params"), params}
    };

    const QByteArray payload = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
    if (m_process->write(payload) != payload.size() || !m_process->waitForBytesWritten(2000)) {
        if (error) {
            *error = QStringLiteral("Failed to write to Python runtime.");
        }
        shutdown();
        return {};
    }

    QByteArray buffer;
    const qint64 deadlineMs = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
    while (QDateTime::currentMSecsSinceEpoch() < deadlineMs) {
        if (!m_process->waitForReadyRead(200)) {
            if (m_process->state() == QProcess::NotRunning) {
                if (error) {
                    *error = QStringLiteral("Python runtime exited unexpectedly.");
                }
                logStdErr();
                shutdown();
                return {};
            }
            continue;
        }

        buffer += m_process->readAllStandardOutput();
        QList<QByteArray> lines = buffer.split('\n');
        buffer = lines.takeLast();
        for (const QByteArray &line : lines) {
            const QByteArray trimmed = line.trimmed();
            if (trimmed.isEmpty()) {
                continue;
            }

            const QJsonDocument doc = QJsonDocument::fromJson(trimmed);
            if (!doc.isObject()) {
                continue;
            }

            const QJsonObject object = doc.object();
            const QString responseId = object.value(QStringLiteral("id")).toString();
            if (responseId != QString::number(requestId)) {
                continue;
            }

            if (object.contains(QStringLiteral("error"))) {
                const QJsonObject errorObject = object.value(QStringLiteral("error")).toObject();
                if (error) {
                    *error = errorObject.value(QStringLiteral("message")).toString(QStringLiteral("Unknown Python runtime error."));
                }
                return {};
            }

            return object.value(QStringLiteral("result")).toObject();
        }
    }

    if (error) {
        *error = QStringLiteral("Python runtime request timed out.");
    }
    logStdErr();
    return {};
}

void PythonRuntimeManager::invalidateCatalog()
{
    m_catalogCache = {};
    m_catalogLoaded = false;
}

void PythonRuntimeManager::logStdErr()
{
    if (!m_process) {
        return;
    }

    const QString stderrText = QString::fromUtf8(m_process->readAllStandardError()).trimmed();
    if (stderrText.isEmpty()) {
        return;
    }

    if (m_loggingService) {
        m_loggingService->warnFor(QStringLiteral("tools_mcp"),
                                  QStringLiteral("[PythonRuntime] %1").arg(stderrText.left(4000)));
    }
}
