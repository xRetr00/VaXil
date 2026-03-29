#include <QtTest>

#include "wakeword/WakeWordDetector.h"

class WakeWordDetectorTests : public QObject
{
    Q_OBJECT

private slots:
    void detectsPrimaryPhrase();
    void detectsVariantPhrase();
    void rejectsNonWakeGreeting();
    void rejectsRandomText();
};

void WakeWordDetectorTests::detectsPrimaryPhrase()
{
    QVERIFY(WakeWordDetector::isWakeWordDetected(std::string("hey vaxil")));
}

void WakeWordDetectorTests::detectsVariantPhrase()
{
    QVERIFY(WakeWordDetector::isWakeWordDetected(std::string("Hey Vaxel")));
}

void WakeWordDetectorTests::rejectsNonWakeGreeting()
{
    QVERIFY(!WakeWordDetector::isWakeWordDetected(std::string("hello vaxil")));
}

void WakeWordDetectorTests::rejectsRandomText()
{
    QVERIFY(!WakeWordDetector::isWakeWordDetected(std::string("random text")));
}

QTEST_APPLESS_MAIN(WakeWordDetectorTests)
#include "WakeWordDetectorTests.moc"
