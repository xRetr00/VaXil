#pragma once

#include <QString>

namespace VaxilErrorCodes {

enum class Key {
    CrashUnhandledException,
    CrashTerminate,
    CrashSignal,
    CrashWindowsException,
    CrashQtFatal,
    CrashWakeHelperException,
    LogInitializationFailed,
    LogFlushFailure,
    StartupInitializationFailed,
    ToolTransport,
    ToolAuth,
    ToolCapability,
    ToolInvalid,
    ToolTimeout,
    ToolUnknown,
    Unknown
};

QString compose(const QString &module, int number);
QString forKey(Key key);
QString description(Key key);
QString fromToolErrorKindValue(int kindValue);
QString crashSignalCode(int signalNumber);

} // namespace VaxilErrorCodes
