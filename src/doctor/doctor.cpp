/*************************************************************************************
 *  Copyright 2014-2015 Sebastian Kügler <sebas@kde.org>                             *
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

#include "doctor.h"

#include <QCommandLineParser>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QLoggingCategory>
#include <QRect>
#include <QStandardPaths>

#include "../config.h"
#include "../configoperation.h"
#include "../getconfigoperation.h"
#include "../setconfigoperation.h"
#include "../edid.h"
#include "../output.h"

Q_LOGGING_CATEGORY(KSCREEN_DOCTOR, "kscreen.doctor");


static QTextStream cout(stdout);
static QTextStream cerr(stderr);

const static QString green = QStringLiteral("\033[01;32m");
const static QString red = QStringLiteral("\033[01;31m");
const static QString yellow = QStringLiteral("\033[01;33m");
const static QString blue = QStringLiteral("\033[01;34m");
const static QString bold = QStringLiteral("\033[01;39m");
const static QString cr = QStringLiteral("\033[0;0m");

namespace KScreen
{
namespace ConfigSerializer
{
// Exported private symbol in configserializer_p.h in KScreen
extern QJsonObject serializeConfig(const KScreen::ConfigPtr &config);
}
}


using namespace KScreen;

Doctor::Doctor(QObject *parent)
    : QObject(parent)
    , m_config(nullptr)
    , m_changed(false)
{
}

Doctor::~Doctor()
{
}

void Doctor::start(QCommandLineParser *parser)
{
    m_parser = parser;
    KScreen::GetConfigOperation *op = new KScreen::GetConfigOperation();
    QObject::connect(op, &KScreen::GetConfigOperation::finished, this,
                     [&](KScreen::ConfigOperation *op) {
                        configReceived(op);
                      });
}

void Doctor::setOptionList(const QStringList &positionalArgs)
{
    m_positionalArgs = positionalArgs;
}

void Doctor::parsePositionalArgs()
{
    Q_FOREACH(const QString &op, m_positionalArgs) {
        auto ops = op.split('.');
        if (ops.count() > 2) {
            bool ok;
            if (ops[0] == QStringLiteral("output")) {
                int output_id = parseInt(ops[1], ok);
                if (!ok) {
                    cerr << "Unable to parse output id" << ops[1] << endl;
                    qApp->exit(3);
                    return;
                }
                if (ops.count() == 3 &&  ops[2] == QStringLiteral("enable")) {
                    if (!setEnabled(output_id, true)) {
                        qApp->exit(1);
                        return;
                    };
                } else if (ops.count() == 3 &ops[2] == QStringLiteral("disable")) {
                    if (!setEnabled(output_id, false)) {
                        qApp->exit(1);
                        return;
                    };
                } else if (ops.count() == 4 && ops[2] == QStringLiteral("mode")) {
                    QString mode_id = ops[3];
                    // set mode
                    if (!setMode(output_id, mode_id)) {
                        qApp->exit(9);
                        return;
                    }
                    qCDebug(KSCREEN_DOCTOR) << "Output" << output_id << "set mode" << mode_id;

                } else if (ops.count() == 4 && ops[2] == QStringLiteral("position")) {
                    QStringList _pos = ops[3].split(',');
                    if (_pos.count() != 2) {
                        qCWarning(KSCREEN_DOCTOR) << "Invalid position:" << ops[3];
                        qApp->exit(5);
                        return;
                    }
                    int x = parseInt(_pos[0], ok);
                    int y = parseInt(_pos[1], ok);
                    if (!ok) {
                        cerr << "Unable to parse position" << ops[3] << endl;
                        qApp->exit(5);
                        return;
                    }

                    QPoint p(x, y);
                    qCDebug(KSCREEN_DOCTOR) << "Output position" << p;
                    if (!setPosition(output_id, p)) {
                        qApp->exit(1);
                        return;
                    }
                } else {
                    cerr << "Unable to parse arguments" << op << endl;
                    qApp->exit(2);
                    return;
                }
            }
        }
    }
    applyConfig();
}

int Doctor::parseInt(const QString &str, bool &ok) const
{
    int _id = str.toInt();
    if (QString::number(_id) == str) {
        ok = true;
        return _id;
    }
    ok = false;
    return 0;
}

void Doctor::configReceived(KScreen::ConfigOperation *op)
{
    m_config = op->config();

    if (m_parser->isSet("json")) {
        showJson();
        qApp->quit();
    }
    if (m_parser->isSet("outputs")) {
        showOutputs();
        qApp->quit();
    }

    parsePositionalArgs();
    // The following paths will have to call quits at some point,
    // otherwise the app just hangs there.
    if (m_parser->isSet("enable")) {
        setEnabled(m_parser->value("enable").toInt(), true);
    }
    if (m_parser->isSet("disable")) {
        setEnabled(m_parser->value("disable").toInt(), false);
    }

    if (m_changed) {
        applyConfig();
        m_changed = false;
    }
}

int Doctor::outputCount() const
{
    if (!m_config) {
        qWarning() << "Invalid config.";
        return 0;
    }
    return m_config->outputs().count();
}

void Doctor::showOutputs() const
{
    if (!m_config) {
        qWarning() << "Invalid config.";
        return;
    }

    Q_FOREACH (const auto &output, m_config->outputs()) {
        cout << green << "Output: " << cr << output->id() << " " << output->name();
        cout << " " << (output->isEnabled() ? green + "enabled" : red + "disabled");
//         QVERIFY(!output->name().isEmpty());
//         QVERIFY(output->id() > -1);
//         QVERIFY(output->isConnected());
//         QVERIFY(output->geometry() != QRectF(1,1,1,1));
//         QVERIFY(output->geometry() != QRectF());
//         QVERIFY(output->sizeMm() != QSize());
        cout << blue << " Modes: " << cr;
        Q_FOREACH (auto mode, output->modes()) {
            cout << mode->id() << ":" << mode->name() << " ";
//             QVERIFY(!mode->name().isEmpty());
//             QVERIFY(mode->refreshRate() > 0);
//             QVERIFY(mode->size().isValid());
        }
        const auto g = output->geometry();
        cout << yellow << "Geometry: " << cr << g.x() << "," << g.y() << " " << g.width() << "x" << g.height();
        cout << endl;
    }
}

void Doctor::showJson() const
{
    QJsonDocument doc(KScreen::ConfigSerializer::serializeConfig(m_config));
    cout << doc.toJson(QJsonDocument::Indented);
}

bool Doctor::setEnabled(int id, bool enabled = true)
{
    if (!m_config) {
        qWarning() << "Invalid config.";
        return false;
    }

    Q_FOREACH (const auto &output, m_config->outputs()) {
        if (output->id() == id) {
            cout << (enabled ? "Enabling " : "Disabling ") << "output " << id << endl;
            output->setEnabled(enabled);
            m_changed = true;
            return true;
        }
    }
    cerr << "Output with id " << id << " not found." << endl;
    qApp->exit(8);
    return false;
}

bool Doctor::setPosition(int id, const QPoint &pos)
{
    if (!m_config) {
        qWarning() << "Invalid config.";
        return false;
    }

    Q_FOREACH (const auto &output, m_config->outputs()) {
        if (output->id() == id) {
            output->setPos(pos);
            m_changed = true;
            return true;
        }
    }
    cout << "Output with id " << id << " not found." << endl;
    return false;
}

bool Doctor::setMode(int id, const QString &mode_id)
{
    if (!m_config) {
        qWarning() << "Invalid config.";
        return false;
    }

    Q_FOREACH (const auto &output, m_config->outputs()) {
        if (output->id() == id) {
            // find mode
            Q_FOREACH (const KScreen::ModePtr mode, output->modes()) {
               if (mode->id() == mode_id) {
                    qCDebug(KSCREEN_DOCTOR) << "Taddaaa! Found mode" << mode->id() << mode->name();
                    output->setCurrentModeId(mode->id());
                    m_changed = true;
                    return true;
               }
            }
        }
    }
    cout << "Output mode " << id << " not found." << endl;
    return false;
}

void Doctor::applyConfig()
{
    if (!m_changed) {
        return;
    }
    auto setop = new SetConfigOperation(m_config, this);
    setop->exec();

    qApp->exit(0);
}