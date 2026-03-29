#include "core/agent/IntentEngine.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>
#include <array>
#include <cmath>

#if JARVIS_HAS_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif

#include "logging/LoggingService.h"
#include "settings/AppSettings.h"

namespace {
constexpr qint64 kIntentCacheMs = 1500;
constexpr int kIntentClassCount = 5;

QStringList featureVocabulary()
{
    return {
        QStringLiteral("list"),
        QStringLiteral("files"),
        QStringLiteral("file"),
        QStringLiteral("directory"),
        QStringLiteral("folder"),
        QStringLiteral("workspace"),
        QStringLiteral("show"),
        QStringLiteral("read"),
        QStringLiteral("open"),
        QStringLiteral("view"),
        QStringLiteral("log"),
        QStringLiteral("write"),
        QStringLiteral("create"),
        QStringLiteral("save"),
        QStringLiteral("make"),
        QStringLiteral("remember"),
        QStringLiteral("preference"),
        QStringLiteral("prefer"),
        QStringLiteral("like"),
        QStringLiteral("fact"),
        QStringLiteral("path"),
        QStringLiteral("content"),
        QStringLiteral("short"),
        QStringLiteral("answers"),
        QStringLiteral("please"),
        QStringLiteral("can"),
        QStringLiteral("you"),
        QStringLiteral("read file"),
        QStringLiteral("list files"),
        QStringLiteral("write file"),
        QStringLiteral("remember that")
    };
}

IntentType intentTypeFromIndex(int index)
{
    switch (index) {
    case 0:
        return IntentType::LIST_FILES;
    case 1:
        return IntentType::READ_FILE;
    case 2:
        return IntentType::WRITE_FILE;
    case 3:
        return IntentType::MEMORY_WRITE;
    default:
        return IntentType::GENERAL_CHAT;
    }
}

std::array<float, kIntentClassCount> softmax(const float *scores, int count)
{
    std::array<float, kIntentClassCount> normalized{};
    if (scores == nullptr || count <= 0) {
        normalized[4] = 1.0f;
        return normalized;
    }

    const float maxScore = *std::max_element(scores, scores + std::min(count, kIntentClassCount));
    float sum = 0.0f;
    for (int i = 0; i < std::min(count, kIntentClassCount); ++i) {
        normalized[i] = std::exp(scores[i] - maxScore);
        sum += normalized[i];
    }

    if (sum <= 0.0f) {
        normalized[4] = 1.0f;
        return normalized;
    }

    for (float &value : normalized) {
        value /= sum;
    }

    return normalized;
}
}

struct IntentEngine::Impl
{
#if JARVIS_HAS_ONNXRUNTIME
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "vaxil-intent"};
    Ort::SessionOptions sessionOptions;
    std::unique_ptr<Ort::Session> session;
    Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::vector<float> inputBuffer;
    std::vector<int64_t> inputShape;
    std::string inputName = "input";
    std::string outputName = "probabilities";
#endif
    bool ready = false;
};

IntentEngine::IntentEngine(AppSettings *settings, LoggingService *loggingService, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_loggingService(loggingService)
    , m_impl(std::make_unique<Impl>())
{
    if (m_settings != nullptr) {
        connect(m_settings, &AppSettings::settingsChanged, this, &IntentEngine::reloadModel);
    }

    reloadModel();
}

IntentEngine::~IntentEngine() = default;

IntentResult IntentEngine::classify(const QString &text)
{
    const QString normalized = normalizeText(text);
    if (normalized.isEmpty()) {
        return {};
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const auto cached = m_cache.constFind(normalized);
    if (cached != m_cache.cend() && nowMs - cached->createdAtMs <= kIntentCacheMs) {
        return cached->result;
    }

    const IntentResult heuristicResult = classifyWithHeuristics(normalized);
    IntentResult result = heuristicResult;
    if (m_impl->ready) {
        const IntentResult modelResult = classifyWithModel(normalized);
        if (modelResult.type != IntentType::GENERAL_CHAT && modelResult.confidence >= 0.80f) {
            result = modelResult;
        } else if (heuristicResult.type != IntentType::GENERAL_CHAT && heuristicResult.confidence >= 0.55f) {
            result = heuristicResult;
        } else if (modelResult.confidence > heuristicResult.confidence) {
            result = modelResult;
        }
    }
    m_cache.insert(normalized, {result, nowMs});
    return result;
}

bool IntentEngine::isReady() const
{
    return m_impl->ready;
}

QString IntentEngine::modelPath() const
{
    return m_modelPath;
}

void IntentEngine::reloadModel()
{
    const QString resolvedPath = resolveModelPath();
#if JARVIS_HAS_ONNXRUNTIME
    const bool hasLoadedSession = m_impl->session != nullptr;
#else
    const bool hasLoadedSession = false;
#endif
    if (resolvedPath == m_modelPath
        && ((resolvedPath.isEmpty() && !m_impl->ready)
            || (!resolvedPath.isEmpty() && m_impl->ready && hasLoadedSession))) {
        return;
    }

    m_cache.clear();
    m_modelPath = resolvedPath;
    m_impl->ready = false;

#if JARVIS_HAS_ONNXRUNTIME
    m_impl->session.reset();
    if (!m_modelPath.isEmpty()) {
        try {
            m_impl->sessionOptions.SetIntraOpNumThreads(1);
            m_impl->sessionOptions.SetInterOpNumThreads(1);
            m_impl->sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_BASIC);
            m_impl->session = std::make_unique<Ort::Session>(
                m_impl->env,
                m_modelPath.toStdWString().c_str(),
                m_impl->sessionOptions);
            m_impl->inputBuffer.assign(featureVocabulary().size(), 0.0f);
            m_impl->inputShape = {1, static_cast<int64_t>(m_impl->inputBuffer.size())};
            m_impl->ready = true;
        } catch (...) {
            m_impl->session.reset();
            m_impl->ready = false;
        }
    }
#endif

    if (m_loggingService) {
        m_loggingService->info(QStringLiteral("IntentEngine initialized. onnxReady=%1 model=\"%2\"")
            .arg(m_impl->ready ? QStringLiteral("true") : QStringLiteral("false"))
            .arg(m_modelPath));
    }
}

IntentResult IntentEngine::classifyWithModel(const QString &text)
{
#if !JARVIS_HAS_ONNXRUNTIME
    Q_UNUSED(text)
    return classifyWithHeuristics(text);
#else
    if (!m_impl->session) {
        return classifyWithHeuristics(text);
    }

    std::fill(m_impl->inputBuffer.begin(), m_impl->inputBuffer.end(), 0.0f);
    encodeFeatures(text, m_impl->inputBuffer);

    try {
        std::array<const char *, 1> inputNames{m_impl->inputName.c_str()};
        std::array<const char *, 1> outputNames{m_impl->outputName.c_str()};
        std::array<Ort::Value, 1> inputs{
            Ort::Value::CreateTensor<float>(
                m_impl->memoryInfo,
                m_impl->inputBuffer.data(),
                m_impl->inputBuffer.size(),
                m_impl->inputShape.data(),
                m_impl->inputShape.size())
        };

        // Minimal ONNX Runtime classification path: encode -> tensor -> session->Run -> max probability.
        auto outputs = m_impl->session->Run(
            Ort::RunOptions{nullptr},
            inputNames.data(),
            inputs.data(),
            inputs.size(),
            outputNames.data(),
            outputNames.size());

        if (outputs.empty()) {
            return classifyWithHeuristics(text);
        }

        const float *scores = outputs[0].GetTensorData<float>();
        const auto probabilities = softmax(scores, kIntentClassCount);
        int bestIndex = 4;
        float bestConfidence = probabilities[4];
        for (int index = 0; index < kIntentClassCount; ++index) {
            if (probabilities[index] > bestConfidence) {
                bestConfidence = probabilities[index];
                bestIndex = index;
            }
        }

        return {
            .type = intentTypeFromIndex(bestIndex),
            .confidence = bestConfidence
        };
    } catch (...) {
        return classifyWithHeuristics(text);
    }
#endif
}

IntentResult IntentEngine::classifyWithHeuristics(const QString &text) const
{
    float listScore = 0.0f;
    float readScore = 0.0f;
    float writeScore = 0.0f;
    float memoryScore = 0.0f;
    float chatScore = 0.35f;

    auto contains = [&](const QString &needle) {
        return text.contains(needle);
    };

    if (contains(QStringLiteral("list files")) || contains(QStringLiteral("show files")) || contains(QStringLiteral("directory")) || contains(QStringLiteral("current folder")) || contains(QStringLiteral("current directory")) || contains(QStringLiteral("current dictionary"))) {
        listScore += 0.92f;
    }
    if (contains(QStringLiteral("workspace")) && contains(QStringLiteral("files"))) {
        listScore += 0.18f;
    }

    if (contains(QStringLiteral("read file")) || contains(QStringLiteral("open file")) || contains(QStringLiteral("read the log")) || contains(QStringLiteral("read logs")) || contains(QStringLiteral("read your own logs")) || contains(QStringLiteral("startup log")) || contains(QStringLiteral("vaxil log")) || contains(QStringLiteral("jarvis log"))) {
        readScore += 0.94f;
    }
    if ((contains(QStringLiteral("read")) && contains(QStringLiteral("file"))) || (contains(QStringLiteral("read")) && contains(QStringLiteral("log")))) {
        readScore += 0.28f;
    }

    if (contains(QStringLiteral("write file")) || contains(QStringLiteral("create file")) || contains(QStringLiteral("save file"))) {
        writeScore += 0.95f;
    }
    if (contains(QStringLiteral("write")) && contains(QStringLiteral("content"))) {
        writeScore += 0.24f;
    }

    if (contains(QStringLiteral("remember that")) || contains(QStringLiteral("save this preference"))) {
        memoryScore += 0.96f;
    }
    if (contains(QStringLiteral("i prefer")) || contains(QStringLiteral("i like")) || contains(QStringLiteral("short answers"))) {
        memoryScore += 0.32f;
    }

    const std::array<float, kIntentClassCount> scores{
        listScore,
        readScore,
        writeScore,
        memoryScore,
        chatScore
    };

    int bestIndex = 4;
    float bestScore = scores[4];
    for (int index = 0; index < kIntentClassCount; ++index) {
        if (scores[index] > bestScore) {
            bestScore = scores[index];
            bestIndex = index;
        }
    }

    const float confidence = std::clamp(bestScore, 0.0f, 0.99f);
    return {
        .type = intentTypeFromIndex(bestIndex),
        .confidence = confidence
    };
}

QString IntentEngine::normalizeText(const QString &text) const
{
    QString normalized = text.trimmed().toLower();
    normalized.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return normalized;
}

void IntentEngine::encodeFeatures(const QString &text, std::vector<float> &buffer) const
{
    static const QStringList vocabulary = featureVocabulary();
    const QString normalized = normalizeText(text);

    for (int index = 0; index < vocabulary.size() && index < static_cast<int>(buffer.size()); ++index) {
        if (normalized.contains(vocabulary.at(index))) {
            buffer[static_cast<size_t>(index)] = 1.0f;
        }
    }
}

QString IntentEngine::resolveModelPath() const
{
    if (m_settings != nullptr && !m_settings->intentModelPath().trimmed().isEmpty()) {
        const QString configuredPath = QDir::cleanPath(m_settings->intentModelPath());
        if (QFileInfo::exists(configuredPath)) {
            return configuredPath;
        }
    }

    const QStringList candidates = {
        QDir::currentPath() + QStringLiteral("/models/intent/intent_classifier.onnx"),
        QDir::currentPath() + QStringLiteral("/third_party/models/intent/intent_classifier.onnx"),
        QStringLiteral(JARVIS_SOURCE_DIR) + QStringLiteral("/third_party/models/intent/intent_classifier.onnx"),
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/models/intent/intent_classifier.onnx")
    };

    for (const QString &candidate : candidates) {
        if (!candidate.isEmpty() && QFileInfo::exists(candidate)) {
            return QDir::cleanPath(candidate);
        }
    }

    return {};
}
