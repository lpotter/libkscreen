/*************************************************************************************
 *  Copyright 2015 by Sebastian Kügler <sebas@kde.org>                           *
 *                                                                                   *
 *  This library is free software; you can redistribute it and/or                    *
 *  modify it under the terms of the GNU Lesser General Public                       *
 *  License as published by the Free Software Foundation; either                     *
 *  version 2.1 of the License, or (at your option) any later version.               *
 *                                                                                   *
 *  This library is distributed in the hope that it will be useful,                  *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of                   *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU                *
 *  Lesser General Public License for more details.                                  *
 *                                                                                   *
 *  You should have received a copy of the GNU Lesser General Public                 *
 *  License along with this library; if not, write to the Free Software              *
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA       *
 *************************************************************************************/

#include <QCoreApplication>
#include <QtTest>
#include <QObject>
#include <QSignalSpy>

#include "backendmanager_p.h"
#include "getconfigoperation.h"
#include "setconfigoperation.h"
#include "config.h"
#include "configmonitor.h"
#include "output.h"
#include "mode.h"
#include "edid.h"

#include "waylandtestserver.h"

Q_LOGGING_CATEGORY(KSCREEN_WAYLAND, "kscreen.wayland");

using namespace KScreen;

class TestKWaylandConfig : public QObject
{
    Q_OBJECT

public:
    explicit TestKWaylandConfig(QObject *parent = nullptr);

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    void changeConfig();
    void testPositionChange();
    void testRotationChange();
    void testModeChange();

private:

    WaylandTestServer *m_server;

};

TestKWaylandConfig::TestKWaylandConfig(QObject *parent)
    : QObject(parent)
    , m_server(nullptr)
{
}

void TestKWaylandConfig::initTestCase()
{
    setenv("KSCREEN_BACKEND", "kwayland", 1);
    KScreen::BackendManager::instance()->shutdownBackend();

    // This is how KWayland will pick up the right socket,
    // and thus connect to our internal test server.
    setenv("WAYLAND_DISPLAY", s_socketName.toLocal8Bit(), 1);

    m_server = new WaylandTestServer(this);
    m_server->start();
}

void TestKWaylandConfig::cleanupTestCase()
{
    qDebug() << "Shutting down";
    KScreen::BackendManager::instance()->shutdownBackend();
    delete m_server;
}

void TestKWaylandConfig::changeConfig()
{
    auto op = new GetConfigOperation();
    QVERIFY(op->exec());
    auto config = op->config();
    QVERIFY(config);

    // Prepare monitor & spy
    KScreen::ConfigMonitor *monitor = KScreen::ConfigMonitor::instance();
    monitor->addConfig(config);
    QSignalSpy configSpy(monitor, &KScreen::ConfigMonitor::configurationChanged);


    // The first output is currently disabled, let's try to enable it
    auto output = config->outputs().first();
    output->setEnabled(true);
    output->setCurrentModeId("76");

    auto output2 = config->outputs()[2]; // is this id stable enough?
    output2->setPos(QPoint(4000, 1080));
    output2->setRotation(KScreen::Output::Left);

    QSignalSpy serverSpy(m_server, &WaylandTestServer::configChanged);
    auto sop = new SetConfigOperation(config, this);
    sop->exec(); // fire and forget...

    QVERIFY(configSpy.wait(2000));
    // check if the server changed
    QCOMPARE(serverSpy.count(), 1);

    QVERIFY(configSpy.count() > 3);

    monitor->removeConfig(config);
    m_server->showOutputs();
}

void TestKWaylandConfig::testPositionChange()
{
    auto op = new GetConfigOperation();
    QVERIFY(op->exec());
    auto config = op->config();
    QVERIFY(config);

    // Prepare monitor & spy
    KScreen::ConfigMonitor *monitor = KScreen::ConfigMonitor::instance();
    monitor->addConfig(config);
    QSignalSpy configSpy(monitor, &KScreen::ConfigMonitor::configurationChanged);

    auto output = config->outputs()[2]; // is this id stable enough?
    auto new_pos = QPoint(3840, 1080);
    output->setPos(new_pos);

    QSignalSpy serverSpy(m_server, &WaylandTestServer::configChanged);
    auto sop = new SetConfigOperation(config, this);
    sop->exec(); // fire and forget...

    QVERIFY(configSpy.wait(2000));
    // check if the server changed
    QCOMPARE(serverSpy.count(), 1);

    QVERIFY(configSpy.count() > 0);
}

void TestKWaylandConfig::testRotationChange()
{
    auto op = new GetConfigOperation();
    QVERIFY(op->exec());
    auto config = op->config();
    QVERIFY(config);

    // Prepare monitor & spy
    KScreen::ConfigMonitor *monitor = KScreen::ConfigMonitor::instance();
    monitor->addConfig(config);
    QSignalSpy configSpy(monitor, &KScreen::ConfigMonitor::configurationChanged);

    auto output = config->outputs()[2]; // is this id stable enough?
    output->setRotation(KScreen::Output::Inverted);

    QSignalSpy serverSpy(m_server, &WaylandTestServer::configChanged);
    auto sop = new SetConfigOperation(config, this);
    sop->exec(); // fire and forget...

    QVERIFY(configSpy.wait(2000));
    // check if the server changed
    QCOMPARE(serverSpy.count(), 1);

    QVERIFY(configSpy.count() > 0);
}


void TestKWaylandConfig::testModeChange()
{
    auto op = new GetConfigOperation();
    QVERIFY(op->exec());
    auto config = op->config();
    QVERIFY(config);

    KScreen::ConfigMonitor *monitor = KScreen::ConfigMonitor::instance();
    monitor->addConfig(config);
    QSignalSpy configSpy(monitor, &KScreen::ConfigMonitor::configurationChanged);

    auto output = config->outputs()[1]; // is this id stable enough?

    QString new_mode = QStringLiteral("74");
    output->setCurrentModeId(new_mode);

    QSignalSpy serverSpy(m_server, &WaylandTestServer::configChanged);
    auto sop = new SetConfigOperation(config, this);
    sop->exec();

    QVERIFY(configSpy.wait(200));
    // check if the server changed
    QCOMPARE(serverSpy.count(), 1);

    QVERIFY(configSpy.count() > 0);
}


QTEST_GUILESS_MAIN(TestKWaylandConfig)

#include "testkwaylandconfig.moc"