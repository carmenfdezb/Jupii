/* Copyright (C) 2017 Michal Kosciesza <michal@mkiol.net>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <QThreadPool>
#include <QDebug>
#include <QList>
#include <QMap>
#include <QStringList>
#include <QByteArray>
#include <QFile>
#include <QCoreApplication>
#include <QNetworkConfiguration>
#include <QNetworkInterface>
#include <string>

#include <libupnpp/control/description.hxx>

#include "settings.h"
#include "utils.h"
#include "directory.h"
#include "device.h"

Directory* Directory::m_instance = nullptr;

Directory::Directory(QObject *parent) :
    QObject(parent),
    TaskExecutor(parent),
    ncm(new QNetworkConfigurationManager())
{
    connect(ncm, &QNetworkConfigurationManager::configurationAdded,
            this, &Directory::handleNetworkConfChanged);
    connect(ncm, &QNetworkConfigurationManager::configurationRemoved,
            this, &Directory::handleNetworkConfChanged);
    connect(ncm, &QNetworkConfigurationManager::configurationChanged,
            this, &Directory::handleNetworkConfChanged);

    connect(this, &Directory::busyChanged, this, &Directory::handleBusyChanged);
    connect(this, &Directory::networkStateChanged, this, &Directory::init);
    connect(this, &Directory::initedChanged, this, &Directory::handleInitedChanged, Qt::QueuedConnection);

    updateNetworkConf();
}

void Directory::handleNetworkConfChanged(const QNetworkConfiguration &conf)
{
    if (conf.state() & QNetworkConfiguration::Defined &&
        (conf.bearerType() == QNetworkConfiguration::BearerWLAN ||
        conf.bearerType() == QNetworkConfiguration::BearerEthernet)) {
        updateNetworkConf();
    }
}

void Directory::updateNetworkConf()
{
    QStringList eth_candidates;
    QStringList wlan_candidates;

    for (const auto& interface : QNetworkInterface::allInterfaces()) {
        if (interface.flags().testFlag(QNetworkInterface::IsRunning)) {
            if (Utils::ethNetworkInf(interface)) {
                //qDebug() << "eth interface:" << interface.name();
                eth_candidates << interface.name();
            } else if (Utils::wlanNetworkInf(interface)) {
                //qDebug() << "wlan interface:" << interface.name();
                wlan_candidates << interface.name();
            }
        }
    }

    QString new_ifname;

    if (eth_candidates.isEmpty() && wlan_candidates.isEmpty()) {
        qWarning() << "No connected network interface found";
    } else {
        QString pref_ifname = Settings::instance()->getPrefNetInf();
        //qDebug() << "Preferred network interface:" << pref_ifname;

        if (!pref_ifname.isEmpty() && (eth_candidates.contains(pref_ifname) ||
                                       wlan_candidates.contains(pref_ifname))) {
            qDebug() << "Preferred network interface found";
            new_ifname = pref_ifname;
        } else {
#ifdef SAILFISH
            // preferred WLAN
            if (!wlan_candidates.isEmpty()) {
                new_ifname = wlan_candidates.first();
            } else {
                new_ifname = eth_candidates.first();
            }
#else
            // preferred Ethernet
            if (!eth_candidates.isEmpty()) {
                new_ifname = eth_candidates.first();
            } else {
                new_ifname = wlan_candidates.first();
            }
#endif
        }
    }

    if (m_ifname != new_ifname) {
        qDebug() << "Connected network interface changed:" << new_ifname;
        m_ifname = new_ifname;
        emit networkStateChanged();
    }
}

bool Directory::isNetworkConnected()
{
    return !m_ifname.isEmpty();
}

bool Directory::getNetworkIf(QString &ifname, QString &address)
{
    if (isNetworkConnected()) {
        auto interface = QNetworkInterface::interfaceFromName(m_ifname);
        if (interface.isValid() &&
            interface.flags().testFlag(QNetworkInterface::IsUp) &&
            interface.flags().testFlag(QNetworkInterface::IsRunning)) {

            ifname = m_ifname;

            auto addra = interface.addressEntries();
            for (const auto &a : addra) {
                auto ha = a.ip();
                if (ha.protocol() == QAbstractSocket::IPv4Protocol ||
                    ha.protocol() == QAbstractSocket::IPv6Protocol) {
                    address = ha.toString();
                    //qDebug() << "Net interface:" << ifname << address;
                    return true;
                }
            }

            qWarning() << "Cannot find valid ip addr for interface:" << m_ifname;
        }
    }

    return false;
}

void Directory::handleBusyChanged()
{
    if (!m_busy) {
        qDebug() << "Refreshing status for XC devices";
        QHash<QString,YamahaXC*>::iterator i = m_xcs.begin();
        while (i != m_xcs.end()) {
            i.value()->getStatus();
            ++i;
        }

        qDebug() << "Sending SSDP advertisement";
        if (msdev)
            msdev->sendAdvertisement();
    }
}

void Directory::handleInitedChanged()
{
    if (m_inited) {
        msdev = std::unique_ptr<MediaServerDevice>(new MediaServerDevice());
    } else {
        clearLists(true);
        msdev.reset(nullptr);
    }
}

void Directory::init()
{
    qDebug() << "Directory init";

    QString ifname, addr;
    if (!getNetworkIf(ifname, addr)) {
        qWarning() << "Cannot find valid network interface";
        setInited(false);
        emit error(1);
        return;
    }

    qDebug() << "LibUPnP init:" << ifname << addr;

    m_lib = UPnPP::LibUPnP::getLibUPnP(
        false, nullptr, ifname.toStdString(), addr.toStdString(), 0
    );

    if (!m_lib) {
        qWarning() << "Cannot initialize UPnPP lib (lib == nullptr)";
        setInited(false);
        emit error(2);
        return;
    }

    if (!m_lib->ok()) {
        qWarning() << "Cannot initialize UPnPP lib (lib != ok)";
        setInited(false);
        emit error(2);
        return;
    }

#ifdef QT_DEBUG
    m_lib->setLogFileName("", UPnPP::LibUPnP::LogLevelDebug);
#else
    m_lib->setLogFileName("", UPnPP::LibUPnP::LogLevelError);
#endif
    m_directory = UPnPClient::UPnPDeviceDirectory::getTheDir(5);

    if (!m_directory) {
        qWarning() << "Cannot initialize UPnPP directory (dir == nullptr)";
        setInited(false);
        emit error(3);
        return;
    }

    if (!m_directory->ok()) {
        qWarning() << "Cannot initialize UPnPP directory (dir != ok)";
        setInited(false);
        m_directory = nullptr;
        emit error(2);
        return;
    }

    setInited(true);

    discover();
}

Directory* Directory::instance(QObject *parent)
{
    if (Directory::m_instance == nullptr) {
        Directory::m_instance = new Directory(parent);
    }

    return Directory::m_instance;
}

void Directory::clearLists(bool all)
{
    m_devsdesc.clear();
    m_servsdesc.clear();
    if (all)
        m_last_devsdesc.clear();
}

void Directory::discover()
{
    qDebug() << "discover";

    if (!m_inited) {
        qWarning() << "Directory not inited.";
        return;
    }

    if (!isNetworkConnected()) {
        qWarning() << "Cannot find valid network interface";
        setInited(false);
        return;
    }

    if (taskActive()) {
        qWarning() << "Task is active. Skipping adding new task";
        return;
    }

    if (!m_directory) {
        qWarning() << "Directory not initialized";
        setInited(false);
        emit error(3);
        return;
    }

    setBusy(true);

    // last devices
    auto s = Settings::instance();
    auto last = s->getLastDevices();
    //qDebug() << "Adding last devices:" << last.size();

    for (auto it = last.begin(); it != last.end(); ++it) {
        qDebug() << it.key() << it.value().toString();
        QString id = it.key();
        QString url = it.value().toString();
        QByteArray xml;
        if (!Settings::readDeviceXML(id, xml))
            continue;
        UPnPClient::UPnPDeviceDesc ddesc(url.toStdString(), xml.toStdString());
        auto did = QString::fromStdString(ddesc.UDN);
        for (auto& sdesc : ddesc.services) {
            auto sid = QString::fromStdString(sdesc.serviceId);
            this->m_servsdesc.insert(did + sid, sdesc);
        }
        this->m_last_devsdesc.insert(did, ddesc);
    }

    emit discoveryLastReady();

    startTask([this](){
        clearLists(false);
        QHash<QString,bool> xcs;
        auto s = Settings::instance();

        // favs

        auto favs = s->getFavDevices();
        //qDebug() << "Adding fav devices:" << favs.size();

        for (auto it = favs.begin(); it != favs.end(); ++it) {
            qDebug() << it.key() << it.value().toString();
            QString id = it.key();
            QString url = it.value().toString();
            QByteArray xml;
            if (!Settings::readDeviceXML(id, xml))
                continue;
            UPnPClient::UPnPDeviceDesc ddesc(url.toStdString(), xml.toStdString());
            auto did = QString::fromStdString(ddesc.UDN);
            for (auto& sdesc : ddesc.services) {
                auto sid = QString::fromStdString(sdesc.serviceId);
                this->m_servsdesc.insert(did + sid, sdesc);
            }
            this->m_devsdesc.insert(did, ddesc);
            if (!xcs.contains(did)) {
                xcs[did] = true;
                auto xc = new YamahaXC(did, xml);
                if (xc->valid()) {
                    qDebug() << "XCS is valid for" << did;
                    xc->moveToThread(QCoreApplication::instance()->thread());
                    this->m_xcs.insert(did, xc);
                } else {
                    //qWarning() << "XCS is invalid for" << did;
                    delete xc;
                }
            }
        }

        emit discoveryFavReady();

        // discovery

        bool found = false;
        auto traverseFun = [this, &found, &xcs](const UPnPClient::UPnPDeviceDesc &ddesc,
                const UPnPClient::UPnPServiceDesc &sdesc) {
            qDebug() << "==> Visitor";
            qDebug() << " Device";
            qDebug() << "  friendlyName:" << QString::fromStdString(ddesc.friendlyName);
            qDebug() << "  deviceType:" << QString::fromStdString(ddesc.deviceType);
            qDebug() << "  UDN:" << QString::fromStdString(ddesc.UDN);
            qDebug() << "  modelName:" << QString::fromStdString(ddesc.modelName);
            qDebug() << "  URLBase:" << QString::fromStdString(ddesc.URLBase);
            qDebug() << " Service";
            qDebug() << "  controlURL:" << QString::fromStdString(sdesc.controlURL);
            qDebug() << "  eventSubURL:" << QString::fromStdString(sdesc.eventSubURL);
            qDebug() << "  SCPDURL:" << QString::fromStdString(sdesc.SCPDURL);
            qDebug() << "  serviceId:" << QString::fromStdString(sdesc.serviceId);
            qDebug() << "  serviceType:" << QString::fromStdString(sdesc.serviceType);
            //qDebug() << "  ddesc.XMLText:" << QString::fromStdString(ddesc.XMLText);

            auto did = QString::fromStdString(ddesc.UDN);
            auto sid = QString::fromStdString(sdesc.serviceId);

            this->m_devsdesc.insert(did, ddesc);
            this->m_servsdesc.insert(did + sid, sdesc);

            if (!xcs.contains(did)) {
                xcs[did] = true;
                auto xc = new YamahaXC(did, QString::fromStdString(ddesc.XMLText));
                if (xc->valid()) {
                    //qDebug() << "XCS is valid for" << did;
                    xc->moveToThread(QCoreApplication::instance()->thread());
                    this->m_xcs.insert(did, xc);
                } else {
                    //qWarning() << "XCS is invalid for" << did;
                    delete xc;
                }
            }

            found = true;

            return true;
        };

        for (int i = 0; i < 5; ++i) {
            if (!m_directory) {
                qWarning() << "Directory not initialized";
                setInited(false);
                emit error(3);
                return;
            }
            //qDebug() << "traverse:" << i;
            m_directory->traverse(traverseFun);
            if (found)
                break;
        }

        //qDebug() << "traverse end";

        emit discoveryReady();

        setBusy(false);
    });
}

const QHash<QString,UPnPClient::UPnPDeviceDesc>& Directory::getDeviceDescs()
{
    return m_devsdesc;
}

bool Directory::getServiceDesc(const QString& deviceId, const QString& serviceId,
                                   UPnPClient::UPnPServiceDesc& sdesc)
{
    const auto it = m_servsdesc.find(deviceId + serviceId);
    if (it != m_servsdesc.end()) {
        sdesc = it.value();
        return true;
    }

    qWarning() << "Cannot find device" << deviceId << "with service" << serviceId;

    return false;
}

bool Directory::getDeviceDesc(const QString& deviceId, UPnPClient::UPnPDeviceDesc& ddesc)
{
    const auto it = m_devsdesc.find(deviceId);
    if (it != m_devsdesc.end()) {
        ddesc = it.value();
        return true;
    }

    const auto lit = m_last_devsdesc.find(deviceId);
    if (lit != m_last_devsdesc.end()) {
        ddesc = lit.value();
        //qDebug() << "Found last device:" << deviceId;
        return true;
    }

    qWarning() << "Cannot find device:" << deviceId;

    return false;
}

QString Directory::deviceNameFromId(const QString& deviceId)
{
    const auto it = m_devsdesc.find(deviceId);
    if (it != m_devsdesc.end()) {
        return QString::fromStdString(it.value().friendlyName);
    }

    const auto lit = m_last_devsdesc.find(deviceId);
    if (lit != m_last_devsdesc.end()) {
        return QString::fromStdString(lit.value().friendlyName);
    }

    qWarning() << "Cannot find device name:" << deviceId;

    return QString();
}

YamahaXC* Directory::deviceXC(const QString& deviceId)
{
    auto it = m_xcs.find(deviceId);
    if (it != m_xcs.end())
        return it.value();
    return nullptr;
}

bool Directory::xcExists(const QString& deviceId)
{
    return m_xcs.contains(deviceId);
}

bool Directory::getBusy()
{
    return m_busy;
}

void Directory::xcTogglePower(const QString& deviceId)
{
    auto xc = deviceXC(deviceId);
    if (xc) {
        xc->powerToggle();
    } else {
        qWarning() << "Device doesn't have XC API";
    }
}

void Directory::xcGetStatus(const QString& deviceId)
{
    auto xc = deviceXC(deviceId);
    if (xc) {
        xc->getStatus();
    } else {
        qWarning() << "Device doesn't have XC API";
    }
}


bool Directory::getInited()
{
    return m_inited;
}

void Directory::setBusy(bool busy)
{
    if (busy != m_busy) {
        m_busy = busy;
        emit busyChanged();
    }
}

void Directory::setInited(bool inited)
{
    if (inited != m_inited) {
        m_inited = inited;
        qDebug() << "Directory inited:" << m_inited;
        emit initedChanged();
    }
}

QUrl Directory::getDeviceIconUrl(const UPnPClient::UPnPDeviceDesc& ddesc)
{
    if (ddesc.iconList.empty())
        return QUrl();

    // Finding largest icon
    int max_size = 0; std::string max_url;
    for (const auto &icon : ddesc.iconList) {
        if (icon.mimeType == "image/jpeg" || icon.mimeType == "image/png") {
            int size = icon.width + icon.height;
            if (size > max_size) {
                max_size = size;
                max_url = icon.url;
            }
        }
    }

    QUrl url(QString::fromStdString(ddesc.URLBase));
    return url.resolved(QUrl(QString::fromStdString(max_url)));
}
