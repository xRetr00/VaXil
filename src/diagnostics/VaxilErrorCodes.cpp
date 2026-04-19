#include "VaxilErrorCodes.h"

#include <QRegularExpression>

namespace {
struct ErrorCodeDefinition {
    VaxilErrorCodes::Key key;
    const char *module;
    int number;
    const char *description;
};

constexpr ErrorCodeDefinition kDefinitions[] = {
    {VaxilErrorCodes::Key::CrashUnhandledException, "CRASH", 1, "Unhandled C++ exception reached process boundary."},
    {VaxilErrorCodes::Key::CrashTerminate, "CRASH", 2, "std::terminate invoked."},
    {VaxilErrorCodes::Key::CrashSignal, "CRASH", 3, "Fatal runtime signal intercepted."},
    {VaxilErrorCodes::Key::CrashWindowsException, "CRASH", 4, "Windows structured exception intercepted."},
    {VaxilErrorCodes::Key::CrashQtFatal, "CRASH", 5, "Qt fatal message received."},
    {VaxilErrorCodes::Key::CrashWakeHelperException, "WAKE", 9, "Wake helper terminated due to fatal exception."},
    {VaxilErrorCodes::Key::LogInitializationFailed, "LOG", 1, "Logging service failed to initialize."},
    {VaxilErrorCodes::Key::LogFlushFailure, "LOG", 2, "Logging flush operation failed."},
    {VaxilErrorCodes::Key::StartupInitializationFailed, "CORE", 1, "Application startup initialization failed."},
    {VaxilErrorCodes::Key::ToolTransport, "TOOL", 1, "Tool execution transport/network failure."},
    {VaxilErrorCodes::Key::ToolAuth, "TOOL", 2, "Tool execution authorization failure."},
    {VaxilErrorCodes::Key::ToolCapability, "TOOL", 3, "Tool execution capability failure."},
    {VaxilErrorCodes::Key::ToolInvalid, "TOOL", 4, "Tool execution invalid input failure."},
    {VaxilErrorCodes::Key::ToolTimeout, "TOOL", 5, "Tool execution timeout."},
    {VaxilErrorCodes::Key::ToolUnknown, "TOOL", 9, "Tool execution unknown failure."},
    {VaxilErrorCodes::Key::Unknown, "CORE", 9999, "Unknown error."},
};

const ErrorCodeDefinition *definitionForKey(VaxilErrorCodes::Key key)
{
    for (const ErrorCodeDefinition &definition : kDefinitions) {
        if (definition.key == key) {
            return &definition;
        }
    }
    return nullptr;
}

QString sanitizeModule(QString module)
{
    module = module.trimmed().toUpper();
    module.remove(QRegularExpression(QStringLiteral("[^A-Z0-9]")));
    if (module.isEmpty()) {
        return QStringLiteral("CORE");
    }
    return module;
}
}

namespace VaxilErrorCodes {

QString compose(const QString &module, int number)
{
    const QString normalizedModule = sanitizeModule(module);
    const int normalizedNumber = number < 0 ? 0 : number;
    return QStringLiteral("VAXIL-%1-%2")
        .arg(normalizedModule)
        .arg(normalizedNumber, 4, 10, QLatin1Char('0'));
}

QString forKey(Key key)
{
    const ErrorCodeDefinition *definition = definitionForKey(key);
    if (definition == nullptr) {
        definition = definitionForKey(Key::Unknown);
    }
    return compose(QString::fromLatin1(definition->module), definition->number);
}

QString description(Key key)
{
    const ErrorCodeDefinition *definition = definitionForKey(key);
    if (definition == nullptr) {
        definition = definitionForKey(Key::Unknown);
    }
    return QString::fromLatin1(definition->description);
}

QString fromToolErrorKindValue(int kindValue)
{
    switch (kindValue) {
    case 0:
        return compose(QStringLiteral("TOOL"), 0);
    case 1:
        return forKey(Key::ToolTransport);
    case 2:
        return forKey(Key::ToolAuth);
    case 3:
        return forKey(Key::ToolCapability);
    case 4:
        return forKey(Key::ToolInvalid);
    case 5:
        return forKey(Key::ToolTimeout);
    case 6:
    default:
        return forKey(Key::ToolUnknown);
    }
}

QString crashSignalCode(int)
{
    return forKey(Key::CrashSignal);
}

} // namespace VaxilErrorCodes
