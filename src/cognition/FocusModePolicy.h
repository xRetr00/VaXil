#pragma once

#include <QString>

#include "companion/contracts/FocusModeState.h"

class FocusModePolicy
{
public:
    [[nodiscard]] bool allows(const FocusModeState &state, const QString &priority) const;
};
