#include "smart_home/DesktopBleIdentityAdapter.h"

#include <QtBluetooth/QBluetoothDeviceInfo>
#include <QtBluetooth/QBluetoothUuid>
#include <QtTest/QtTest>

class DesktopBleIdentityAdapterTests : public QObject
{
    Q_OBJECT

private slots:
    void matchesServiceUuid();
    void matchesIBeaconManufacturerUuid();
    void matchesManufacturerPayloadContainingUuidBytes();
    void rejectsMismatchedUuid();
    void rejectsRssiBelowThreshold();
};

void DesktopBleIdentityAdapterTests::matchesServiceUuid()
{
    QBluetoothDeviceInfo info;
    info.setServiceUuids({QBluetoothUuid(QStringLiteral("74278bda-b644-4520-8f0c-720eaf059935"))});
    info.setRssi(-60);

    QVERIFY(DesktopBleIdentityAdapter::advertisementMatchesBeacon(
        info,
        QStringLiteral("74278bda-b644-4520-8f0c-720eaf059935"),
        -85));
}

void DesktopBleIdentityAdapterTests::matchesIBeaconManufacturerUuid()
{
    QByteArray data;
    data.append(char(0x02));
    data.append(char(0x15));
    data.append(QByteArray::fromHex("74278bdab64445208f0c720eaf059935"));
    data.append(QByteArray::fromHex("00010002c5"));

    QBluetoothDeviceInfo info;
    info.setManufacturerData(0x004c, data);
    info.setRssi(-62);

    QVERIFY(DesktopBleIdentityAdapter::advertisementMatchesBeacon(
        info,
        QStringLiteral("74278bda-b644-4520-8f0c-720eaf059935"),
        -85));
}

void DesktopBleIdentityAdapterTests::matchesManufacturerPayloadContainingUuidBytes()
{
    QByteArray data = QByteArray::fromHex("0102030474278bdab64445208f0c720eaf05993505060708");

    QBluetoothDeviceInfo info;
    info.setManufacturerData(0xffff, data);
    info.setRssi(-62);

    QVERIFY(DesktopBleIdentityAdapter::advertisementMatchesBeacon(
        info,
        QStringLiteral("74278bda-b644-4520-8f0c-720eaf059935"),
        -85));
}

void DesktopBleIdentityAdapterTests::rejectsMismatchedUuid()
{
    QBluetoothDeviceInfo info;
    info.setServiceUuids({QBluetoothUuid(QStringLiteral("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"))});
    info.setRssi(-55);

    QVERIFY(!DesktopBleIdentityAdapter::advertisementMatchesBeacon(
        info,
        QStringLiteral("74278bda-b644-4520-8f0c-720eaf059935"),
        -85));
}

void DesktopBleIdentityAdapterTests::rejectsRssiBelowThreshold()
{
    QBluetoothDeviceInfo info;
    info.setServiceUuids({QBluetoothUuid(QStringLiteral("74278bda-b644-4520-8f0c-720eaf059935"))});
    info.setRssi(-95);

    QVERIFY(!DesktopBleIdentityAdapter::advertisementMatchesBeacon(
        info,
        QStringLiteral("74278bda-b644-4520-8f0c-720eaf059935"),
        -85));
}

QTEST_APPLESS_MAIN(DesktopBleIdentityAdapterTests)
#include "DesktopBleIdentityAdapterTests.moc"
