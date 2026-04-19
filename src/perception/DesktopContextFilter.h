#pragma once

#include <QVariantMap>
#include <QString>

struct DesktopContextFilterInput
{
    QString sourceKind;
    QString appId;
    QString windowTitle;
    QString notificationTitle;
    QString notificationMessage;
    QVariantMap metadata;
};

struct DesktopContextFilterDecision
{
    bool accepted = true;
    bool diagnosticOnly = false;
    QString reasonCode = QStringLiteral("desktop_context.accepted");
};

class DesktopContextFilter
{
public:
    [[nodiscard]] static DesktopContextFilterDecision evaluate(const DesktopContextFilterInput &input);
};
