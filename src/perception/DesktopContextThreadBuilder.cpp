#include "perception/DesktopContextThreadBuilder.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include "companion/contracts/ContextThreadId.h"

namespace {
QString firstMeaningfulChunk(const QString &text)
{
    static const QRegularExpression separators(QStringLiteral("\\s*[\\-|:|•|»|/]+\\s*"));
    const QStringList chunks = text.split(separators, Qt::SkipEmptyParts);
    for (QString chunk : chunks) {
        chunk = chunk.simplified();
        if (chunk.size() >= 3) {
            return chunk;
        }
    }
    return text.simplified();
}

bool isBrowserApp(const QString &appId)
{
    static const QSet<QString> browserApps{
        QStringLiteral("chrome"),
        QStringLiteral("edge"),
        QStringLiteral("firefox"),
        QStringLiteral("brave"),
        QStringLiteral("opera")
    };
    return browserApps.contains(appId);
}

bool isEditorApp(const QString &appId)
{
    static const QSet<QString> editorApps{
        QStringLiteral("vscode"),
        QStringLiteral("cursor"),
        QStringLiteral("windsurf"),
        QStringLiteral("notepad"),
        QStringLiteral("notepad_plus_plus"),
        QStringLiteral("pycharm"),
        QStringLiteral("idea"),
        QStringLiteral("sublime_text")
    };
    return editorApps.contains(appId);
}

QString languageForExtension(QString extension)
{
    extension = extension.trimmed().toLower();
    if (extension == QStringLiteral("cpp") || extension == QStringLiteral("cc") || extension == QStringLiteral("cxx")) {
        return QStringLiteral("cpp");
    }
    if (extension == QStringLiteral("h") || extension == QStringLiteral("hpp")) {
        return QStringLiteral("cpp_header");
    }
    if (extension == QStringLiteral("py")) {
        return QStringLiteral("python");
    }
    if (extension == QStringLiteral("qml")) {
        return QStringLiteral("qml");
    }
    if (extension == QStringLiteral("ts") || extension == QStringLiteral("tsx")) {
        return QStringLiteral("typescript");
    }
    if (extension == QStringLiteral("js") || extension == QStringLiteral("jsx")) {
        return QStringLiteral("javascript");
    }
    if (extension == QStringLiteral("md")) {
        return QStringLiteral("markdown");
    }
    if (extension == QStringLiteral("json")) {
        return QStringLiteral("json");
    }
    return {};
}

QString fileExtension(const QString &document)
{
    const QRegularExpressionMatch match =
        QRegularExpression(QStringLiteral("\\.([A-Za-z0-9_]+)$")).match(document.trimmed());
    return match.hasMatch() ? match.captured(1).toLower() : QString{};
}

QString cleanedChunk(QString value)
{
    value = value.simplified();
    value.remove(QRegularExpression(QStringLiteral("^[\\-|:|•|»/\\s]+")));
    value.remove(QRegularExpression(QStringLiteral("[\\-|:|•|»/\\s]+$")));
    return value.simplified();
}

QStringList splitTitle(QString title)
{
    title = title.simplified();
    if (title.isEmpty()) {
        return {};
    }

    QStringList parts = title.split(QRegularExpression(QStringLiteral("\\s+-\\s+")), Qt::SkipEmptyParts);
    for (QString &part : parts) {
        part = cleanedChunk(part);
    }
    return parts;
}
}

CompanionContextSnapshot DesktopContextThreadBuilder::fromActiveWindow(const QString &appId,
                                                                       const QString &windowTitle,
                                                                       const QVariantMap &externalMetadata)
{
    CompanionContextSnapshot snapshot;
    snapshot.appId = normalizedAppFamily(appId);
    snapshot.taskId = inferredTaskType(snapshot.appId);
    snapshot.metadata = activeWindowMetadata(snapshot.appId, windowTitle);
    for (auto it = externalMetadata.constBegin(); it != externalMetadata.constEnd(); ++it) {
        if (it.value().metaType().id() == QMetaType::QString) {
            const QString value = it.value().toString().trimmed();
            if (!value.isEmpty()) {
                snapshot.metadata.insert(it.key(), value);
            }
            continue;
        }
        snapshot.metadata.insert(it.key(), it.value());
    }
    const bool redacted = snapshot.metadata.value(QStringLiteral("metadataRedacted")).toBool()
        || snapshot.metadata.value(QStringLiteral("metadataClass")).toString() == QStringLiteral("private_app_only");
    const QString topicSource = snapshot.metadata.value(QStringLiteral("documentContext")).toString().trimmed();
    const QString secondarySource = snapshot.metadata.value(QStringLiteral("siteContext")).toString().trimmed().isEmpty()
        ? snapshot.metadata.value(QStringLiteral("workspaceContext")).toString().trimmed()
        : snapshot.metadata.value(QStringLiteral("siteContext")).toString().trimmed();
    snapshot.topic = redacted
        ? QStringLiteral("private_mode")
        : inferTopic(topicSource.isEmpty() ? windowTitle : topicSource, secondarySource);
    snapshot.recentIntent = snapshot.taskId == QStringLiteral("browser_tab")
        ? QStringLiteral("reference current tab")
        : snapshot.taskId == QStringLiteral("editor_document")
            ? QStringLiteral("reference current file")
            : QStringLiteral("reference current window");
    const double metadataConfidence = snapshot.metadata.value(QStringLiteral("metadataConfidence")).toDouble();
    snapshot.confidence = metadataConfidence > 0.0
        ? metadataConfidence
        : (snapshot.metadata.value(QStringLiteral("automationSource")).toString() == QStringLiteral("uia")
            ? 0.86
            : 0.78);
    snapshot.threadId = ContextThreadId::fromParts(
        {QStringLiteral("desktop"), snapshot.taskId, snapshot.appId, snapshot.topic});
    return snapshot;
}

CompanionContextSnapshot DesktopContextThreadBuilder::fromClipboard(const QString &appId,
                                                                    const QString &windowTitle,
                                                                    const QString &clipboardPreview)
{
    CompanionContextSnapshot snapshot;
    snapshot.appId = normalizedAppFamily(appId);
    snapshot.taskId = QStringLiteral("clipboard");
    snapshot.topic = inferTopic(clipboardPreview, windowTitle);
    snapshot.confidence = 0.72;
    snapshot.threadId = ContextThreadId::fromParts(
        {QStringLiteral("desktop"), QStringLiteral("clipboard"), snapshot.appId, snapshot.topic});
    snapshot.metadata.insert(QStringLiteral("windowTitle"), windowTitle.simplified());
    snapshot.metadata.insert(QStringLiteral("clipboardPreview"), clipboardPreview);
    return snapshot;
}

CompanionContextSnapshot DesktopContextThreadBuilder::fromNotification(const QString &title,
                                                                       const QString &message,
                                                                       const QString &priority,
                                                                       const QString &sourceAppId)
{
    CompanionContextSnapshot snapshot;
    snapshot.appId = normalizedAppFamily(sourceAppId);
    snapshot.taskId = QStringLiteral("notification");
    snapshot.topic = inferTopic(title, message);
    snapshot.confidence = priority.trimmed().compare(QStringLiteral("high"), Qt::CaseInsensitive) == 0 ? 0.84 : 0.68;
    snapshot.threadId = ContextThreadId::fromParts(
        {QStringLiteral("desktop"), QStringLiteral("notification"), snapshot.appId, snapshot.topic});
    snapshot.metadata.insert(QStringLiteral("title"), title.simplified());
    snapshot.metadata.insert(QStringLiteral("message"), message.simplified());
    snapshot.metadata.insert(QStringLiteral("priority"), priority.trimmed().toLower());
    return snapshot;
}

QString DesktopContextThreadBuilder::describeContext(const CompanionContextSnapshot &context)
{
    const QString appLabel = context.appId.trimmed().isEmpty() ? QStringLiteral("unknown app") : context.appId.trimmed();
    const QString documentContext = context.metadata.value(QStringLiteral("documentContext")).toString().trimmed();
    const QString workspaceContext = context.metadata.value(QStringLiteral("workspaceContext")).toString().trimmed();
    const QString siteContext = context.metadata.value(QStringLiteral("siteContext")).toString().trimmed();
    const QString clipboardPreview = context.metadata.value(QStringLiteral("clipboardPreview")).toString().trimmed();
    const QString title = context.metadata.value(QStringLiteral("title")).toString().trimmed();

    if (context.taskId == QStringLiteral("browser_tab")) {
        const QString subject = documentContext.isEmpty() ? context.topic : documentContext;
        return siteContext.isEmpty()
            ? QStringLiteral("Desktop context: browser tab \"%1\" in %2.").arg(subject, appLabel)
            : QStringLiteral("Desktop context: browser tab \"%1\" on %2 in %3.").arg(subject, siteContext, appLabel);
    }
    if (context.taskId == QStringLiteral("editor_document")) {
        const QString subject = documentContext.isEmpty() ? context.topic : documentContext;
        return workspaceContext.isEmpty()
            ? QStringLiteral("Desktop context: editor file \"%1\" in %2.").arg(subject, appLabel)
            : QStringLiteral("Desktop context: editor file \"%1\" in workspace \"%2\" via %3.").arg(subject, workspaceContext, appLabel);
    }
    if (context.taskId == QStringLiteral("clipboard")) {
        return QStringLiteral("Desktop context: clipboard from %1 with preview \"%2\".")
            .arg(appLabel, clipboardPreview);
    }
    if (context.taskId == QStringLiteral("notification")) {
        return QStringLiteral("Desktop context: notification \"%1\" from %2.")
            .arg(title.isEmpty() ? context.topic : title, appLabel);
    }
    return QStringLiteral("Desktop context: focused window \"%1\" in %2.")
        .arg(documentContext.isEmpty() ? context.topic : documentContext, appLabel);
}

QVariantMap DesktopContextThreadBuilder::activeWindowMetadata(const QString &normalizedAppId,
                                                              const QString &windowTitle)
{
    QVariantMap metadata;
    metadata.insert(QStringLiteral("windowTitle"), windowTitle.simplified());

    const QStringList parts = splitTitle(windowTitle);
    const QString firstPart = parts.value(0);
    const QString secondPart = parts.value(1);

    if (isBrowserApp(normalizedAppId)) {
        const QStringList browserParts = firstPart.split(QRegularExpression(QStringLiteral("\\s+\\|\\s+")), Qt::SkipEmptyParts);
        metadata.insert(QStringLiteral("documentContext"), cleanedChunk(browserParts.value(0, firstPart)));
        metadata.insert(QStringLiteral("documentKind"), QStringLiteral("browser_page"));
        metadata.insert(QStringLiteral("metadataClass"), QStringLiteral("browser_document"));
        metadata.insert(QStringLiteral("metadataSource"), QStringLiteral("window_title"));
        if (browserParts.size() > 1) {
            metadata.insert(QStringLiteral("siteContext"), cleanedChunk(browserParts.last()));
        } else if (!secondPart.isEmpty()) {
            metadata.insert(QStringLiteral("siteContext"), secondPart);
        }
        return metadata;
    }

    if (isEditorApp(normalizedAppId)) {
        const QString document = cleanedChunk(firstPart.isEmpty() ? firstMeaningfulChunk(windowTitle) : firstPart);
        metadata.insert(QStringLiteral("documentContext"), document);
        metadata.insert(QStringLiteral("documentKind"), QStringLiteral("editor_file"));
        metadata.insert(QStringLiteral("metadataClass"), QStringLiteral("editor_document"));
        metadata.insert(QStringLiteral("metadataSource"), QStringLiteral("window_title"));
        const QString extension = fileExtension(document);
        if (!extension.isEmpty()) {
            metadata.insert(QStringLiteral("fileExtension"), extension);
            const QString language = languageForExtension(extension);
            if (!language.isEmpty()) {
                metadata.insert(QStringLiteral("languageHint"), language);
            }
        }
        if (parts.size() > 2) {
            metadata.insert(QStringLiteral("workspaceContext"), cleanedChunk(parts.at(1)));
        } else if (!secondPart.isEmpty() && !secondPart.contains(QStringLiteral("code"), Qt::CaseInsensitive)) {
            metadata.insert(QStringLiteral("workspaceContext"), cleanedChunk(secondPart));
        }
        return metadata;
    }

    metadata.insert(QStringLiteral("documentContext"), cleanedChunk(firstPart.isEmpty() ? firstMeaningfulChunk(windowTitle) : firstPart));
    metadata.insert(QStringLiteral("documentKind"), QStringLiteral("window_title"));
    metadata.insert(QStringLiteral("metadataClass"), QStringLiteral("active_window"));
    metadata.insert(QStringLiteral("metadataSource"), QStringLiteral("window_title"));
    return metadata;
}

QString DesktopContextThreadBuilder::inferredTaskType(const QString &normalizedAppId)
{
    if (isBrowserApp(normalizedAppId)) {
        return QStringLiteral("browser_tab");
    }
    if (isEditorApp(normalizedAppId)) {
        return QStringLiteral("editor_document");
    }
    return QStringLiteral("active_window");
}

QString DesktopContextThreadBuilder::normalizedAppFamily(const QString &appId)
{
    const QString baseName = QFileInfo(appId.trimmed()).completeBaseName();
    QString normalized = normalizeSegment(baseName.isEmpty() ? appId : baseName);
    if (normalized == QStringLiteral("code")) {
        return QStringLiteral("vscode");
    }
    if (normalized == QStringLiteral("msedge")) {
        return QStringLiteral("edge");
    }
    if (normalized == QStringLiteral("notepad")) {
        return QStringLiteral("notepad");
    }
    if (normalized.isEmpty()) {
        return QStringLiteral("unknown_app");
    }
    return normalized;
}

QString DesktopContextThreadBuilder::inferTopic(const QString &primaryText, const QString &secondaryText)
{
    QString topic = normalizeSegment(firstMeaningfulChunk(primaryText));
    if (!topic.isEmpty() && topic != QStringLiteral("unknown_topic")) {
        return topic;
    }

    topic = normalizeSegment(firstMeaningfulChunk(secondaryText));
    if (!topic.isEmpty()) {
        return topic;
    }

    return QStringLiteral("unknown_topic");
}

QString DesktopContextThreadBuilder::normalizeSegment(QString value)
{
    value = value.trimmed().toLower();
    if (value.isEmpty()) {
        return {};
    }

    value.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("_"));
    value.replace(QRegularExpression(QStringLiteral("_+")), QStringLiteral("_"));
    while (value.startsWith(QLatin1Char('_'))) {
        value.remove(0, 1);
    }
    while (value.endsWith(QLatin1Char('_'))) {
        value.chop(1);
    }
    if (value.size() > 40) {
        value = value.left(40);
        while (value.endsWith(QLatin1Char('_'))) {
            value.chop(1);
        }
    }
    return value;
}
