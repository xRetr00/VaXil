#pragma once

#include <QString>

class WebSearchQueryBuilder
{
public:
    [[nodiscard]] static QString build(const QString &input, int currentYear);
};
