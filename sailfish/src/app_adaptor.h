/*
 * This file was generated by qdbusxml2cpp version 0.8
 * Command line was: qdbusxml2cpp dbus/org.jupii.xml -a src/app_adaptor -p src/app_interface
 *
 * qdbusxml2cpp is Copyright (C) 2016 The Qt Company Ltd.
 *
 * This is an auto-generated file.
 * This file may have been hand-edited. Look for HAND-EDIT comments
 * before re-generating it.
 */

#ifndef APP_ADAPTOR_H
#define APP_ADAPTOR_H

#include <QtCore/QObject>
#include <QtDBus/QtDBus>
QT_BEGIN_NAMESPACE
class QByteArray;
template<class T> class QList;
template<class Key, class Value> class QMap;
class QString;
class QStringList;
class QVariant;
QT_END_NAMESPACE

/*
 * Adaptor class for interface org.jupii.Player
 */
class PlayerAdaptor: public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.jupii.Player")
    Q_CLASSINFO("D-Bus Introspection", ""
"  <interface name=\"org.jupii.Player\">\n"
"    <property access=\"read\" type=\"b\" name=\"canControl\"/>\n"
"    <method name=\"appendPath\">\n"
"      <arg direction=\"in\" type=\"s\" name=\"path\"/>\n"
"    </method>\n"
"    <method name=\"clearPlaylist\"/>\n"
"  </interface>\n"
        "")
public:
    PlayerAdaptor(QObject *parent);
    virtual ~PlayerAdaptor();

public: // PROPERTIES
    Q_PROPERTY(bool canControl READ canControl)
    bool canControl() const;

public Q_SLOTS: // METHODS
    void appendPath(const QString &path);
    void clearPlaylist();
Q_SIGNALS: // SIGNALS
};

#endif
