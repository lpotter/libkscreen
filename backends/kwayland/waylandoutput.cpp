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

#include "waylandoutput.h"
#include "waylandbackend.h"
#include "waylandconfig.h"

#include <mode.h>
#include <edid.h>

#include <QRect>
#include <QGuiApplication>


using namespace KScreen;

WaylandOutput::WaylandOutput(quint32 id, WaylandConfig *parent)
    : QObject(parent)
    , m_id(id)
    , m_output(nullptr)
{
    m_rotationMap = {
        {KWayland::Client::OutputDevice::Transform::Normal, KScreen::Output::None},
        {KWayland::Client::OutputDevice::Transform::Rotated90, KScreen::Output::Right},
        {KWayland::Client::OutputDevice::Transform::Rotated180, KScreen::Output::Inverted},
        {KWayland::Client::OutputDevice::Transform::Rotated270, KScreen::Output::Left},
        {KWayland::Client::OutputDevice::Transform::Flipped, KScreen::Output::None},
        {KWayland::Client::OutputDevice::Transform::Flipped90, KScreen::Output::Right},
        {KWayland::Client::OutputDevice::Transform::Flipped180, KScreen::Output::Inverted},
        {KWayland::Client::OutputDevice::Transform::Flipped270, KScreen::Output::Left}
    };
}

KScreen::Output::Rotation WaylandOutput::toKScreenRotation(const KWayland::Client::OutputDevice::Transform  transform) const
{
    auto it = m_rotationMap.constFind(transform);
    return it.value();
}

KWayland::Client::OutputDevice::Transform WaylandOutput::toKWaylandTransform(const KScreen::Output::Rotation rotation) const
{
    return m_rotationMap.key(rotation);
}

QString WaylandOutput::toKScreenModeId(int kwaylandmodeid) const
{
    if (!m_modeIdMap.values().contains(kwaylandmodeid)) {
        qCWarning(KSCREEN_WAYLAND) << "Invalid kwayland mode id:" << kwaylandmodeid << m_modeIdMap;
    }
    return m_modeIdMap.key(kwaylandmodeid, QStringLiteral("invalid_mode_id"));
}

int WaylandOutput::toKWaylandModeId(const QString &kscreenmodeid) const
{
    if (!m_modeIdMap.contains(kscreenmodeid)) {
        qCWarning(KSCREEN_WAYLAND) << "Invalid kscreen mode id:" << kscreenmodeid << m_modeIdMap;
    }
    return m_modeIdMap.value(kscreenmodeid, -1);
}

WaylandOutput::~WaylandOutput()
{
}

quint32 WaylandOutput::id() const
{
    Q_ASSERT(m_output);
    return m_id;
}

bool WaylandOutput::enabled() const
{
    return m_output != nullptr;
}

KWayland::Client::OutputDevice* WaylandOutput::outputDevice() const
{
    return m_output;
}

void WaylandOutput::bindOutputDevice(KWayland::Client::Registry* registry, KWayland::Client::OutputDevice* op, quint32 name, quint32 version)
{
    if (m_output == op) {
        return;
    }
    m_output = op;

    connect(m_output, &KWayland::Client::OutputDevice::done, this, [this]() {
                Q_EMIT complete();
                connect(m_output, &KWayland::Client::OutputDevice::changed,
                        this, &WaylandOutput::changed);

    });

    m_output->setup(registry->bindOutputDevice(name, version));
}

KScreen::OutputPtr WaylandOutput::toKScreenOutput()
{
    KScreen::OutputPtr output(new KScreen::Output());
    output->setId(m_id);
    updateKScreenOutput(output);
    return output;
}

void WaylandOutput::updateKScreenOutput(KScreen::OutputPtr &output)
{
    // Initialize primary output
    output->setId(m_id);
    output->setEnabled(m_output->enabled() == KWayland::Client::OutputDevice::Enablement::Enabled);
    output->setConnected(true);
    output->setPrimary(true); // FIXME: wayland doesn't have the concept of a primary display
    output->setName(m_output->model());
    // Physical size
    output->setSizeMm(m_output->physicalSize());
    output->setPos(m_output->globalPosition());
    output->setRotation(m_rotationMap[m_output->transform()]);
    KScreen::ModeList modeList;
    m_modeIdMap.clear();
    QString currentModeId = QStringLiteral("-1");
    Q_FOREACH (const KWayland::Client::OutputDevice::Mode &m, m_output->modes()) {
        KScreen::ModePtr mode(new KScreen::Mode());
        const QString modename = modeName(m);
        QString modeid = QString::number(m.id);
        if (modeid.isEmpty()) {
            qCDebug(KSCREEN_WAYLAND) << "Could not create mode id from" << m.id << ", using" << modename << "instead.";
            modeid = modename;
        }
        if (m_modeIdMap.keys().contains(modeid)) {
            qCWarning(KSCREEN_WAYLAND) << "Mode id already in use:" << modeid;
        }

        mode->setId(modeid);
        mode->setRefreshRate(m.refreshRate);
        mode->setSize(m.size);
        mode->setName(modename);
        if (m.flags.testFlag(KWayland::Client::OutputDevice::Mode::Flag::Current)) {
            currentModeId = modeid;
        }
        // Update the kscreen => kwayland mode id translation map
        m_modeIdMap.insert(modeid, m.id);
        // Add to the modelist which gets set on the output
        modeList[modeid] = mode;
    }
    if (currentModeId == QLatin1String("-1")) {
        qCWarning(KSCREEN_WAYLAND) << "Could not find the current mode id" << modeList;
    }
    output->setCurrentModeId(currentModeId);

    output->setModes(modeList);
    output->setScale(m_output->scale());
}

QString WaylandOutput::modeName(const KWayland::Client::OutputDevice::Mode &m) const
{
    return QString::number(m.size.width()) + QLatin1Char('x') +
           QString::number(m.size.height()) + QLatin1Char('@') +
           QString::number(qRound(m.refreshRate/1000.0));
}

QString WaylandOutput::name() const
{
    Q_ASSERT(m_output);
    return QStringLiteral("%1 %2").arg(m_output->manufacturer(), m_output->model());
}

QDebug operator<<(QDebug dbg, const WaylandOutput *output)
{
    dbg << "WaylandOutput(Id:" << output->id() <<", Name:" << \
        QString(output->outputDevice()->manufacturer() + QStringLiteral(" ") + \
        output->outputDevice()->model())  << ")";
    return dbg;
}
