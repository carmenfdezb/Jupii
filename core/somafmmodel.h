/* Copyright (C) 2018 Michal Kosciesza <michal@mkiol.net>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef SOMAFMMODEL_H
#define SOMAFMMODEL_H

#include <QString>
#include <QHash>
#include <QDebug>
#include <QByteArray>
#include <QVariant>
#include <QUrl>
#include <QPair>
#include <QDir>
#include <QDomNodeList>
#include <QDomElement>
#include <QNetworkReply>

#include "listmodel.h"
#include "itemmodel.h"

class SomafmItem : public SelectableItem
{
    Q_OBJECT
public:
    enum Roles {
        NameRole = Qt::DisplayRole,
        IconRole = Qt::DecorationRole,
        IdRole = Qt::UserRole,
        UrlRole,
        DescriptionRole,
        SelectedRole
    };

public:
    SomafmItem(QObject *parent = nullptr): SelectableItem(parent) {}
    explicit SomafmItem(const QString &id,
                      const QString &name,
                      const QString &description,
                      const QUrl &url,
                      const QUrl &icon,
                      QObject *parent = nullptr);
    QVariant data(int role) const;
    QHash<int, QByteArray> roleNames() const;
    inline QString id() const { return m_id; }
    inline QString name() const { return m_name; }
    inline QString description() const { return m_description; }
    inline QUrl url() const { return m_url; }
    inline QUrl icon() const { return m_icon; }

private:
    QString m_id;
    QString m_name;
    QString m_description;
    QUrl m_url;
    QUrl m_icon;
};

class SomafmModel : public SelectableItemModel
{
    Q_OBJECT
    Q_PROPERTY (bool refreshing READ isRefreshing NOTIFY refreshingChanged)
public:
    explicit SomafmModel(QObject *parent = nullptr);
    bool isRefreshing();
    Q_INVOKABLE QVariantList selectedItems();

public slots:
    void refresh();

signals:
    void refreshingChanged();
    void error();

private slots:
    void handleDataDownloadFinished();
    void handleIconDownloadFinished();

private:
    static const QString m_dirUrl;
    static const QString m_dirFilename;
    static const QString m_imageFilename;
    static const int httpTimeout = 100000;

    QDomNodeList m_entries;
    QList<QPair<QString,QString>> m_imagesToDownload;  // <id, image URL>
    QHash<QNetworkReply*,QString> m_replyToId;
    bool m_refreshing = false;

    QList<ListItem*> makeItems();
    bool parseData();
    void downloadImages();
    void downloadImage();
    QString bestImage(const QDomElement& entry);
};

#endif // SOMAFMMODEL_H
