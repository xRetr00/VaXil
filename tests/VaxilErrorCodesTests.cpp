#include <QtTest>

#include "diagnostics/VaxilErrorCodes.h"

class VaxilErrorCodesTests : public QObject
{
    Q_OBJECT

private slots:
    void composeFormatsExpectedCode();
    void keyLookupReturnsStableCodes();
    void toolErrorKindMapsToToolCodes();
};

void VaxilErrorCodesTests::composeFormatsExpectedCode()
{
    QCOMPARE(VaxilErrorCodes::compose(QStringLiteral("core"), 12), QStringLiteral("VAXIL-CORE-0012"));
    QCOMPARE(VaxilErrorCodes::compose(QStringLiteral(" wake "), 9), QStringLiteral("VAXIL-WAKE-0009"));
}

void VaxilErrorCodesTests::keyLookupReturnsStableCodes()
{
    QCOMPARE(VaxilErrorCodes::forKey(VaxilErrorCodes::Key::CrashTerminate), QStringLiteral("VAXIL-CRASH-0002"));
    QCOMPARE(VaxilErrorCodes::forKey(VaxilErrorCodes::Key::CrashQtFatal), QStringLiteral("VAXIL-CRASH-0005"));
    QVERIFY(!VaxilErrorCodes::description(VaxilErrorCodes::Key::CrashQtFatal).isEmpty());
}

void VaxilErrorCodesTests::toolErrorKindMapsToToolCodes()
{
    QCOMPARE(VaxilErrorCodes::fromToolErrorKindValue(1), QStringLiteral("VAXIL-TOOL-0001"));
    QCOMPARE(VaxilErrorCodes::fromToolErrorKindValue(2), QStringLiteral("VAXIL-TOOL-0002"));
    QCOMPARE(VaxilErrorCodes::fromToolErrorKindValue(3), QStringLiteral("VAXIL-TOOL-0003"));
    QCOMPARE(VaxilErrorCodes::fromToolErrorKindValue(4), QStringLiteral("VAXIL-TOOL-0004"));
    QCOMPARE(VaxilErrorCodes::fromToolErrorKindValue(5), QStringLiteral("VAXIL-TOOL-0005"));
    QCOMPARE(VaxilErrorCodes::fromToolErrorKindValue(6), QStringLiteral("VAXIL-TOOL-0009"));
}

QTEST_APPLESS_MAIN(VaxilErrorCodesTests)
#include "VaxilErrorCodesTests.moc"
