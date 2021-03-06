/*************************************************************************************
 *  Copyright (C) 2012 by Alejandro Fiestas Olivares <afiestas@kde.org>              *
 *  Copyright (C) 2012, 2013 by Daniel Vrátil <dvratil@redhat.com>                   *
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

#include "xrandroutput.h"
#include "xrandrmode.h"
#include "xrandrconfig.h"
#include "xrandr.h"
#include "output.h"
#include "config.h"

#include <QRect>

Q_DECLARE_METATYPE(QList<int>)

XRandROutput::XRandROutput(xcb_randr_output_t id, XRandRConfig *config)
    : QObject(config)
    , m_config(config)
    , m_id(id)
    , m_type(KScreen::Output::Unknown)
    , m_primary(0)
    , m_crtc(0)
{
    init();
}

XRandROutput::~XRandROutput()
{
}

xcb_randr_output_t XRandROutput::id() const
{
    return m_id;
}

bool XRandROutput::isConnected() const
{
    return m_connected == XCB_RANDR_CONNECTION_CONNECTED;
}

bool XRandROutput::isEnabled() const
{
    return m_crtc != Q_NULLPTR && m_crtc->mode() != XCB_NONE;
}

bool XRandROutput::isPrimary() const
{
    return m_primary;
}

QPoint XRandROutput::position() const
{
    return m_crtc ? m_crtc->geometry().topLeft() : QPoint();
}

QSize XRandROutput::size() const
{
    return m_crtc ? m_crtc->geometry().size() : QSize();
}

XRandRMode::Map XRandROutput::modes() const
{
    return m_modes;
}

QString XRandROutput::currentModeId() const
{
    return m_crtc ? QString::number(m_crtc->mode()) : QString();
}

XRandRMode* XRandROutput::currentMode() const
{
    if (!m_crtc) {
        return Q_NULLPTR;
    }
    int modeId = m_crtc->mode();
    if (!m_modes.contains(modeId)) {
        return 0;
    }

    return m_modes[modeId];
}

KScreen::Output::Rotation XRandROutput::rotation() const
{
    return static_cast<KScreen::Output::Rotation>(m_crtc ? m_crtc->rotation() : XCB_RANDR_ROTATION_ROTATE_0);
}

QByteArray XRandROutput::edid() const
{
    if (m_edid.isNull()) {
        size_t len;
        quint8 *data = XRandR::outputEdid(m_id, len);
        if (data) {
            m_edid = QByteArray((char *) data, len);
            delete[] data;
        } else {
            m_edid = QByteArray();
        }
    }

    return m_edid;
}

XRandRCrtc* XRandROutput::crtc() const
{
    return m_crtc;
}

void XRandROutput::update()
{
    init();
}

void XRandROutput::update(xcb_randr_crtc_t crtc, xcb_randr_mode_t mode, xcb_randr_connection_t conn, bool primary)
{
    qCDebug(KSCREEN_XRANDR) << "XRandROutput" << m_id << "update";
    qCDebug(KSCREEN_XRANDR) << "\tm_connected:" << m_connected;
    qCDebug(KSCREEN_XRANDR) << "\tm_crtc" << m_crtc;
    qCDebug(KSCREEN_XRANDR) << "\tCRTC:" << crtc;
    qCDebug(KSCREEN_XRANDR) << "\tMODE:" << mode;
    qCDebug(KSCREEN_XRANDR) << "\tConnection:" << conn;
    qCDebug(KSCREEN_XRANDR) << "\tPrimary:" << primary;

    // Connected or disconnected
    if (isConnected() != (conn == XCB_RANDR_CONNECTION_CONNECTED)) {
        if (conn == XCB_RANDR_CONNECTION_CONNECTED) {
            // New monitor has been connected, refresh everything
            init();
        } else {
            // Disconnected
            m_connected = conn;
            m_clones.clear();
            m_heightMm = 0;
            m_widthMm = 0;
            m_type = KScreen::Output::Unknown;
            qDeleteAll(m_modes);
            m_modes.clear();
            m_preferredModes.clear();
        }
    } else if (conn == XCB_RANDR_CONNECTION_CONNECTED) {
        // the output changed in some way, let's update the internal
        // list of modes, as it may have changed
        XCB::OutputInfo outputInfo(m_id, XCB_TIME_CURRENT_TIME);
        if (outputInfo) {
            updateModes(outputInfo);
        }

    }

    // A monitor has been enabled or disabled
    // We don't use isEnabled(), because it checks for crtc && crtc->mode(), however
    // crtc->mode may already be unset due to xcb_randr_crtc_tChangeNotify coming before
    // xcb_randr_output_tChangeNotify and reseting the CRTC mode

    if ((m_crtc == Q_NULLPTR) != (crtc == XCB_NONE)) {
        if (crtc == XCB_NONE && mode == XCB_NONE) {
            // Monitor has been disabled
            m_crtc->disconectOutput(m_id);
            m_crtc = 0;
        } else {
            m_crtc = m_config->crtc(crtc);
            m_crtc->connectOutput(m_id);
        }
    }

    // Primary has changed
    m_primary = primary;
}

void XRandROutput::setIsPrimary(bool primary)
{
    m_primary = primary;
}


void XRandROutput::init()
{
    XCB::OutputInfo outputInfo(m_id, XCB_TIME_CURRENT_TIME);
    Q_ASSERT(outputInfo);
    if (!outputInfo) {
        return;
    }

    XCB::PrimaryOutput primary(XRandR::rootWindow());

    m_name = QString::fromUtf8((const char *) xcb_randr_get_output_info_name(outputInfo.data()), outputInfo->name_len);
    m_type = fetchOutputType(m_id, m_name);
    m_icon = QString();
    m_connected = (xcb_randr_connection_t) outputInfo->connection;
    m_primary = (primary->output == m_id);
    xcb_randr_output_t *clones = xcb_randr_get_output_info_clones(outputInfo.data());
    for (int i = 0; i < outputInfo->num_clones; ++i) {
        m_clones.append(clones[i]);
    }
    m_widthMm = outputInfo->mm_width;
    m_heightMm = outputInfo->mm_height;
    m_crtc = m_config->crtc(outputInfo->crtc);
    if (m_crtc) {
        m_crtc->connectOutput(m_id);
    }

    updateModes(outputInfo);
}

void XRandROutput::updateModes(const XCB::OutputInfo &outputInfo)
{
    /* Init modes */
    XCB::ScopedPointer<xcb_randr_get_screen_resources_reply_t> screenResources(XRandR::screenResources());
    Q_ASSERT(screenResources);
    if (!screenResources) {
        return;
    }
    xcb_randr_mode_info_t *modes = xcb_randr_get_screen_resources_modes(screenResources.data());
    xcb_randr_mode_t *outputModes = xcb_randr_get_output_info_modes(outputInfo.data());

    m_preferredModes.clear();
    qDeleteAll(m_modes);
    m_modes.clear();
    for (int i = 0; i < outputInfo->num_modes; ++i) {
        /* Resources->modes contains all possible modes, we are only interested
         * in those listed in outputInfo->modes. */
        for (int j = 0; j < screenResources->num_modes; ++j) {
            if (modes[j].id != outputModes[i]) {
                continue;
            }

            XRandRMode *mode = new XRandRMode(modes[j], this);
            m_modes.insert(mode->id(), mode);

            if (i < outputInfo->num_preferred) {
                m_preferredModes.append(QString::number(mode->id()));
            }
            break;
        }
    }
}

KScreen::Output::Type XRandROutput::fetchOutputType(xcb_randr_output_t outputId, const QString &name)
{
    QByteArray type = typeFromProperty(outputId);
    if (type.isEmpty()) {
        type = name.toLocal8Bit();
    }

    static const QVector<QLatin1String> embedded{QLatin1String("LVDS"),
                                                 QLatin1String("IDP"),
                                                 QLatin1String("EDP"),
                                                 QLatin1String("LCD")};

    for (const QLatin1String &pre : embedded) {
        if (name.toUpper().startsWith(pre)) {
            return KScreen::Output::Panel;
        }
    }

    if (type.contains("VGA")) {
        return KScreen::Output::VGA;
    } else if (type.contains("DVI")) {
        return KScreen::Output::DVI;
    } else if (type.contains("DVI-I")) {
        return KScreen::Output::DVII;
    } else if (type.contains("DVI-A")) {
        return KScreen::Output::DVIA;
    } else if (type.contains("DVI-D")) {
        return KScreen::Output::DVID;
    } else if (type.contains("HDMI")) {
        return KScreen::Output::HDMI;
    } else if (type.contains("Panel")) {
        return KScreen::Output::Panel;
    } else if (type.contains("TV-Composite")) {
        return KScreen::Output::TVComposite;
    } else if (type.contains("TV-SVideo")) {
        return KScreen::Output::TVSVideo;
    } else if (type.contains("TV-Component")) {
        return KScreen::Output::TVComponent;
    } else if (type.contains("TV-SCART")) {
        return KScreen::Output::TVSCART;
    } else if (type.contains("TV-C4")) {
        return KScreen::Output::TVC4;
    } else if (type.contains("TV")) {
        return KScreen::Output::TV;
    } else if (type.contains("DisplayPort") || type.startsWith("DP")) {
        return KScreen::Output::DisplayPort;
    } else if (type.contains("unknown")) {
        return KScreen::Output::Unknown;
    } else {
//         qCDebug(KSCREEN_XRANDR) << "Output Type not translated:" << type;
    }

    return KScreen::Output::Unknown;

}

QByteArray XRandROutput::typeFromProperty(xcb_randr_output_t outputId)
{
    QByteArray type;

    XCB::InternAtom atomType(true, 13, "ConnectorType");
    if (!atomType) {
        return type;
    }

    char *connectorType;

    auto cookie = xcb_randr_get_output_property(XCB::connection(), outputId, atomType->atom,
                                                XCB_ATOM_ANY, 0, 100, false, false);
    XCB::ScopedPointer<xcb_randr_get_output_property_reply_t> reply(xcb_randr_get_output_property_reply(XCB::connection(), cookie, NULL));
    if (!reply) {
        return type;
    }

    if (!(reply->type == XCB_ATOM_ATOM && reply->format == 32 && reply->num_items == 1)) {
        return type;
    }

    const uint8_t *prop = xcb_randr_get_output_property_data(reply.data());
    XCB::AtomName atomName(*reinterpret_cast<const xcb_atom_t*>(prop));
    if (!atomName) {
        return type;
    }

    connectorType = xcb_get_atom_name_name(atomName);
    if (!connectorType) {
        return type;
    }

    type = connectorType;
    return type;
}

KScreen::OutputPtr XRandROutput::toKScreenOutput() const
{
    KScreen::OutputPtr kscreenOutput(new KScreen::Output);

    const bool signalsBlocked = kscreenOutput->signalsBlocked();
    kscreenOutput->blockSignals(true);
    kscreenOutput->setId(m_id);
    kscreenOutput->setType(m_type);
    kscreenOutput->setSizeMm(QSize(m_widthMm, m_heightMm));
    kscreenOutput->setName(m_name);
    kscreenOutput->setIcon(m_icon);

    kscreenOutput->setConnected(isConnected());
    if (isConnected()) {
        KScreen::ModeList kscreenModes;
        for (auto iter = m_modes.constBegin(), end = m_modes.constEnd(); iter != end; ++iter) {
            XRandRMode *mode = iter.value();
            kscreenModes.insert(QString::number(iter.key()), mode->toKScreenMode());
        }
        kscreenOutput->setModes(kscreenModes);
        kscreenOutput->setPreferredModes(m_preferredModes);
        kscreenOutput->setPrimary(m_primary);
        kscreenOutput->setClones([](const QList<xcb_randr_output_t> &clones) {
            QList<int> kclones;
            kclones.reserve(clones.size());
            for (xcb_randr_output_t o : clones) {
                kclones.append(static_cast<int>(o));
            }
            return kclones;
        }(m_clones));
        kscreenOutput->setEnabled(isEnabled());
        if (isEnabled()) {
            kscreenOutput->setSize(size());
            kscreenOutput->setPos(position());
            kscreenOutput->setRotation(rotation());
            kscreenOutput->setCurrentModeId(currentModeId());
        }
    }


    kscreenOutput->blockSignals(signalsBlocked);
    return kscreenOutput;
}
