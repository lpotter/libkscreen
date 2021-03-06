/*************************************************************************************
 *  Copyright (C) 2012 by Alejandro Fiestas Olivares <afiestas@kde.org>              *
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

#include "parser.h"
#include "fake.h"

#include "config.h"
#include "output.h"

#include <QtCore/QFile>
#include <QMetaObject>
#include <QMetaProperty>
#include <QLoggingCategory>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

using namespace KScreen;

ConfigPtr Parser::fromJson(const QByteArray& data)
{
    ConfigPtr config(new Config);

    const QJsonObject json = QJsonDocument::fromJson(data).object();

    ScreenPtr screen = Parser::screenFromJson(json["screen"].toObject().toVariantMap());
    config->setScreen(screen);

    const QVariantList outputs = json["outputs"].toArray().toVariantList();
    if (outputs.isEmpty()) {
        return config;
    }

    OutputList outputList;
    Q_FOREACH(const QVariant &value, outputs) {
        const OutputPtr output = Parser::outputFromJson(value.toMap());
        outputList.insert(output->id(), output);
    }

    config->setOutputs(outputList);
    return config;
}

ConfigPtr Parser::fromJson(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << file.errorString();
        qWarning() << "File: " << path;
        return ConfigPtr();
    }

    return Parser::fromJson(file.readAll());
}

ScreenPtr Parser::screenFromJson(const QVariantMap &data)
{
    ScreenPtr screen(new Screen);
    screen->setId(data["id"].toInt());
    screen->setMinSize(Parser::sizeFromJson(data["minSize"].toMap()));
    screen->setMaxSize(Parser::sizeFromJson(data["maxSize"].toMap()));
    screen->setCurrentSize(Parser::sizeFromJson(data["currentSize"].toMap()));
    screen->setMaxActiveOutputsCount(data["maxActiveOutputsCount"].toInt());

    return screen;
}

void Parser::qvariant2qobject(const QVariantMap &variant, QObject *object)
{
    const QMetaObject *metaObject = object->metaObject();
    for (QVariantMap::const_iterator iter = variant.begin(); iter != variant.end(); ++iter) {
        const int propertyIndex = metaObject->indexOfProperty(qPrintable(iter.key()));
        if (propertyIndex == -1) {
            //qWarning() << "Skipping non-existent property" << iter.key();
            continue;
        }
        const QMetaProperty metaProperty = metaObject->property(propertyIndex);
        if (!metaProperty.isWritable()) {
            //qWarning() << "Skipping read-only property" << iter.key();
            continue;
        }

        const QVariant property = object->property(iter.key().toLatin1());
        Q_ASSERT(property.isValid());
        if (property.isValid()) {
            QVariant value = iter.value();
            if (value.canConvert(property.type())) {
                value.convert(property.type());
                object->setProperty(iter.key().toLatin1(), value);
            } else if (QString(QLatin1String("QVariant")).compare(QLatin1String(property.typeName())) == 0) {
                object->setProperty(iter.key().toLatin1(), value);
            }
        }
    }
}

OutputPtr Parser::outputFromJson(QMap< QString, QVariant > map)
{
    OutputPtr output(new Output);
    output->setId(map["id"].toInt());

    QStringList preferredModes;
    const QVariantList prefModes = map["preferredModes"].toList();
    Q_FOREACH(const QVariant &mode, prefModes) {
        preferredModes.append(mode.toString());
    }
    output->setPreferredModes(preferredModes);
    map.remove(QLatin1Literal("preferredModes"));

    ModeList modelist;
    const QVariantList modes = map["modes"].toList();
    Q_FOREACH(const QVariant &modeValue, modes) {
        const ModePtr mode = Parser::modeFromJson(modeValue);
        modelist.insert(mode->id(), mode);
    }
    output->setModes(modelist);
    map.remove(QLatin1Literal("modes"));

    if(map.contains("clones")) {
        QList<int> clones;
        Q_FOREACH(const QVariant &id, map["clones"].toList()) {
            clones.append(id.toInt());
        }

        output->setClones(clones);
        map.remove(QLatin1Literal("clones"));
    }

    const QString type = map["type"].toByteArray().toUpper();
    if (type.contains("LVDS") || type.contains("EDP") || type.contains("IDP") || type.contains("7")) {
        output->setType(Output::Panel);
    } else if (type.contains("VGA")) {
        output->setType(Output::VGA);
    } else if (type.contains("DVI")) {
        output->setType(Output::DVI);
    } else if (type.contains("DVI-I")) {
        output->setType(Output::DVII);
    } else if (type.contains("DVI-A")) {
        output->setType(Output::DVIA);
    } else if (type.contains("DVI-D")) {
        output->setType(Output::DVID);
    } else if (type.contains("HDMI") || type.contains("6")) {
        output->setType(Output::HDMI);
    } else if (type.contains("Panel")) {
        output->setType(Output::Panel);
    } else if (type.contains("TV")) {
        output->setType(Output::TV);
    } else if (type.contains("TV-Composite")) {
        output->setType(Output::TVComposite);
    } else if (type.contains("TV-SVideo")) {
        output->setType(Output::TVSVideo);
    } else if (type.contains("TV-Component")) {
        output->setType(Output::TVComponent);
    } else if (type.contains("TV-SCART")) {
        output->setType(Output::TVSCART);
    } else if (type.contains("TV-C4")) {
        output->setType(Output::TVC4);
    } else if (type.contains("DisplayPort") || type.contains("14")) {
        output->setType(Output::DisplayPort);
    } else if (type.contains("Unknown")) {
        output->setType(Output::Unknown);
    } else {
        qCWarning(KSCREEN_FAKE) << "Output Type not translated:" << type;
    }
    map.remove(QLatin1Literal("type"));

    if (map.contains("pos")) {
        output->setPos(Parser::pointFromJson(map["pos"].toMap()));
        map.remove(QLatin1Literal("pos"));
    }

    if (map.contains("size")) {
        output->setSize(Parser::sizeFromJson(map["size"].toMap()));
        map.remove(QLatin1Literal("size"));
    }

    if (map.contains("scale")) {
        qDebug() << "Scale found:" << map["scale"].toReal();
        output->setScale(map["scale"].toReal());
        map.remove(QLatin1Literal("size"));
    }

    //Remove some extra properties that we do not want or need special treatment
    map.remove(QLatin1Literal("edid"));

    Parser::qvariant2qobject(map, output.data());
    return output;
}

ModePtr Parser::modeFromJson(const QVariant& data)
{
    const QVariantMap map = data.toMap();
    ModePtr mode(new Mode);
    Parser::qvariant2qobject(map, mode.data());

    mode->setSize(Parser::sizeFromJson(map["size"].toMap()));

    return mode;
}

QSize Parser::sizeFromJson(const QVariant& data)
{
    const QVariantMap map = data.toMap();

    QSize size;
    size.setWidth(map["width"].toInt());
    size.setHeight(map["height"].toInt());

    return size;
}

QPoint Parser::pointFromJson(const QVariant& data)
{
    const QVariantMap map = data.toMap();

    QPoint point;
    point.setX(map["x"].toInt());
    point.setY(map["y"].toInt());

    return point;
}

QRect Parser::rectFromJson(const QVariant& data)
{
    QRect rect;
    rect.setSize(Parser::sizeFromJson(data));
    rect.setBottomLeft(Parser::pointFromJson(data));

    return rect;
}

bool Parser::validate(const QByteArray& data)
{
    Q_UNUSED(data);
    return true;
}

bool Parser::validate(const QString& data)
{
    Q_UNUSED(data);
    return true;
}
