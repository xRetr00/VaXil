#pragma once

#include <QString>

#include "cognition/SuggestionProposalRanker.h"

namespace SuggestionProposalPolicyScoring {

[[nodiscard]] double compiledPolicyFocusAdjustment(const SuggestionProposalRanker::Input &input,
                                                   const ActionProposal &proposal,
                                                   QString *reasonCode);

[[nodiscard]] double compiledPolicySourceAdjustment(const SuggestionProposalRanker::Input &input,
                                                    const ActionProposal &proposal,
                                                    QString *reasonCode);
[[nodiscard]] double compiledLayeredAdjustment(const SuggestionProposalRanker::Input &input,
                                               const ActionProposal &proposal,
                                               QString *reasonCode);
[[nodiscard]] double compiledEvolutionAdjustment(const SuggestionProposalRanker::Input &input,
                                                 const ActionProposal &proposal,
                                                 QString *reasonCode);
[[nodiscard]] double compiledTuningAdjustment(const SuggestionProposalRanker::Input &input,
                                              const ActionProposal &proposal,
                                              QString *reasonCode);

} // namespace SuggestionProposalPolicyScoring
