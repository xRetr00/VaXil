#pragma once

#include <QObject>
#include <QPointer>
#include <QVariantList>

class QNetworkAccessManager;
class QNetworkReply;

struct ToolInfo
{
    QString name;
    QString category;
    bool installed = false;
    QString version;
    QString path;
    bool downloadable = false;
    bool critical = false;
};

Q_DECLARE_METATYPE(ToolInfo)

class ToolManager : public QObject
{
    Q_OBJECT

public:
    explicit ToolManager(QObject *parent = nullptr);

    QList<ToolInfo> scan();
    QVariantList toolStatusList() const;
    QString toolsRoot() const;
    QString activeDownloadName() const;
    int activeDownloadPercent() const;

public slots:
    void rescan();
    void downloadTool(const QString &name);
    void downloadModel(const QString &name);
    void installAll();

signals:
    void toolsUpdated();
    void downloadProgress(const QString &name, qint64 received, qint64 total);
    void downloadFinished(const QString &name, bool success, const QString &message);

private:
    struct DownloadDescriptor {
        QString name;
        QString category;
        QString url;
        QString relativeTargetPath;
        bool extractArchive = false;
        QString sha256;
    };

    ToolInfo probeTool(const QString &name, const QString &category, const QStringList &candidateFiles, bool critical, bool downloadable) const;
    QString resolveExistingPath(const QStringList &candidateFiles) const;
    QString probeVersion(const QString &path, const QStringList &args) const;
    DownloadDescriptor descriptorForName(const QString &name) const;
    void beginDownload(const DownloadDescriptor &descriptor);
    void finalizeDownload(QNetworkReply *reply);

    QList<ToolInfo> m_tools;
    QNetworkAccessManager *m_network = nullptr;
    QString m_activeDownloadName;
    int m_activeDownloadPercent = -1;
};
