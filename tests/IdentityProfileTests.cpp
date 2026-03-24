#include <QtTest>

#include "settings/IdentityProfileService.h"

class IdentityProfileTests : public QObject
{
    Q_OBJECT

private slots:
    void loadsIdentityAndUserProfile();
};

void IdentityProfileTests::loadsIdentityAndUserProfile()
{
    IdentityProfileService service;
    QVERIFY(service.initialize());

    const auto identity = service.identity();
    QVERIFY(!identity.assistantName.isEmpty());
    QVERIFY(!identity.tone.isEmpty());

    const auto profile = service.userProfile();
    QVERIFY(profile.preferences.is_object());
}

QTEST_MAIN(IdentityProfileTests)
#include "IdentityProfileTests.moc"
