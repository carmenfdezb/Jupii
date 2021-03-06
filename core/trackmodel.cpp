/* Copyright (C) 2018-2020 Michal Kosciesza <michal@mkiol.net>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <QDebug>
#include <QHash>
#include <QUrl>
#include <QFileInfo>

#include "tracker.h"
#include "trackmodel.h"
#include "trackercursor.h"
#include "utils.h"

const QString TrackModel::queryByAlbumTemplate =
      "SELECT ?song " \
      "nie:title(?song) " \
      "nmm:artistName(nmm:performer(?song)) " \
      "nie:title(nmm:musicAlbum(?song)) "
      "nie:url(?song) " \
      "nmm:trackNumber(?song) AS ?trackNumber " \
      "nfo:duration(?song) " \
      "nie:mimeType(?song) " \
      "WHERE { " \
      "?song a nmm:MusicPiece; " \
      "nmm:musicAlbum \"%1\" . " \
      "FILTER regex(nie:title(?song), \"%2\", \"i\") " \
      "} " \
      "ORDER BY ?trackNumber " \
      "LIMIT 500";

const QString TrackModel::queryByArtistTemplate =
      "SELECT ?song " \
      "nie:title(?song) " \
      "nmm:artistName(nmm:performer(?song)) " \
      "nie:title(nmm:musicAlbum(?song)) AS ?albumTitle "
      "nie:url(?song) " \
      "nmm:trackNumber(?song) AS ?trackNumber " \
      "nfo:duration(?song) " \
      "nie:mimeType(?song) " \
      "WHERE { " \
      "?song a nmm:MusicPiece; " \
      "nmm:performer \"%1\" . " \
      "FILTER regex(nie:title(?song), \"%2\", \"i\") " \
      "} " \
      "ORDER BY ?albumTitle ?trackNumber " \
      "LIMIT 500";

const QString TrackModel::queryByPlaylistTemplate =
        "SELECT nfo:hasMediaFileListEntry(?list) " \
        "WHERE { ?list a nmm:Playlist . " \
        "FILTER ( ?list in ( <%1> ) ) " \
        "} " \
        "LIMIT 1";

const QString TrackModel::queryByEntriesTemplate =
        "SELECT nfo:entryUrl(?song) " \
        "WHERE { ?song a nfo:MediaFileListEntry . " \
        "FILTER ( ?song in ( %1 )) } " \
        "LIMIT 500";

const QString TrackModel::queryByUrlsTemplate =
        "SELECT nie:url(?item) " \
        "nie:mimeType(?item) " \
        "nie:title(?item) " \
        "nmm:artistName(nmm:performer(?item)) " \
        "nie:title(nmm:musicAlbum(?item)) "
        "WHERE { ?item a nmm:MusicPiece . " \
        "FILTER ( nie:url(?item) in ( %1 ) ) }";

TrackModel::TrackModel(QObject *parent) :
    SelectableItemModel(new TrackItem, parent)
{
}

QList<ListItem*> TrackModel::makeItems()
{
    auto tracker = Tracker::instance();

    QString query;
    TrackerTasks task = TaskUnknown;

    if (!m_albumId.isEmpty()) {
        query = queryByAlbumTemplate.arg(m_albumId, getFilter());
        task = TaskAlbum;
    } else if (!m_artistId.isEmpty()) {
        query = queryByArtistTemplate.arg(m_artistId, getFilter());
        task = TaskArtist;
    } else if (!m_playlistId.isEmpty()) {
        query = queryByPlaylistTemplate.arg(m_playlistId);
        task = TaskPlaylist;
    } else {
        qWarning() << "Id not defined";
    }

    if (!query.isEmpty()) {
        if (tracker->query(query, false)) {
            auto result = tracker->getResult();
            return processTrackerReply(task, result.first, result.second);
        } else {
            qWarning() << "Tracker query error";
        }
    }

    return {};
}

QList<ListItem*> TrackModel::processTrackerReplyForAlbumArtist(TrackerCursor& cursor)
{
    QList<ListItem*> items;

    while(cursor.next()) {
        auto type = ContentServer::typeFromMime(cursor.value(7).toString());
        items << new TrackItem(
                     cursor.value(0).toString(), // id
                     cursor.value(1).toString(),
                     cursor.value(2).toString(),
                     cursor.value(3).toString(),
                     QUrl(cursor.value(4).toString()), // url
                     QUrl(), // icon
                     type,
                     cursor.value(5).toInt(),
                     cursor.value(6).toInt()
                     );
    }

    return items;
}

TrackModel::TrackData TrackModel::makeTrackDataFromId(const QUrl& id) const
{
    TrackData data;
    QString name;
    int t = 0;

    Utils::pathTypeNameCookieIconFromId(
                id,
                nullptr,
                &t,
                &name,
                nullptr,
                &data.icon,
                nullptr,
                &data.author);

    data.type = t == 0 ?
                ContentServer::getContentTypeByExtension(id) :
                static_cast<ContentServer::Type>(t);

    if (name.isEmpty()) {
        auto itemType = ContentServer::itemTypeFromUrl(id);
        if (itemType == ContentServer::ItemType_Mic)
            data.title = tr("Microphone");
        else if (itemType == ContentServer::ItemType_AudioCapture)
            data.title = tr("Audio capture");
        else if (itemType == ContentServer::ItemType_ScreenCapture)
            data.title = tr("Screen capture");
    } else {
        data.title = name;
    }

    return data;
}

QList<ListItem*> TrackModel::processTrackerReplyForPlaylist(TrackerCursor& cursor)
{
    QString entries;
    while(cursor.next()) {
        auto list = cursor.value(0).toString().split(',');
        list = list.mid(0, 50);
        list.replaceInStrings(QRegExp("^(.*)$"), "<\\1>");
        entries = list.join(',');
        break;
    }

    if (!entries.isEmpty()) {
        auto tracker = Tracker::instance();
        auto query = queryByEntriesTemplate.arg(entries);
        if (tracker->query(query, false)) {
            auto result = tracker->getResult();
            return processTrackerReply(TaskEntries, result.first, result.second);
        } else {
            qWarning() << "Tracker query error";
        }
    }

    return {};
}

QList<ListItem*> TrackModel::processTrackerReplyForEntries(TrackerCursor& cursor)
{
    QStringList fileUrls;

    auto filter = getFilter();

    while(cursor.next()) {
        auto url = QUrl(cursor.value(0).toString());
        if (!url.isEmpty()) {
            m_ids.append(url);
            if (url.isLocalFile()) {
                fileUrls.append('"' + url.toString(QUrl::EncodeUnicode|QUrl::EncodeSpaces) + '"');
            }
        }
    }

    if (!fileUrls.isEmpty()) {
        auto tracker = Tracker::instance();
        auto query = queryByUrlsTemplate.arg(fileUrls.join(","));
        if (tracker->query(query, false)) {
            auto result = tracker->getResult();
            return processTrackerReply(TaskUrls, result.first, result.second);
        } else {
            qWarning() << "Tracker query error";
        }
    }

    return makeTrackItemsFromTrackData();
}

QList<ListItem*> TrackModel::makeTrackItemsFromTrackData()
{
    QList<ListItem*> items;

    auto filter = getFilter();

    for (const auto& id : m_ids) {
        auto id_data = makeTrackDataFromId(id);
        if (m_trackdata_by_id.contains(id)) {
            auto& data = m_trackdata_by_id[id];
            if (!id_data.title.isEmpty())
                data.title = id_data.title;
            if (!id_data.author.isEmpty())
                data.author = id_data.author;
            if (!id_data.icon.isEmpty())
                data.icon = id_data.icon;
            if (id_data.type != ContentServer::TypeUnknown)
                data.type = id_data.type;
        } else {
            m_trackdata_by_id.insert(id, id_data);
        }
    }

    for (const auto& id : m_ids) {
        const auto& data = m_trackdata_by_id.value(id);
        auto title = data.title.isEmpty() ?
                    id.fileName().isEmpty() ? id.path().isEmpty() ?
                    id.toString() : id.path() : id.fileName() : data.title;
        if (filter.isEmpty() ||
                title.contains(filter, Qt::CaseInsensitive) ||
                data.author.contains(filter, Qt::CaseInsensitive)) {
            items << new TrackItem(
                         id.toString(), // id
                         title, // title-
                         data.author, // artist
                         QString(), // album
                         id, // url
                         data.icon, // icon
                         data.type, // type
                         0, // number
                         0, // length
                         ContentServer::itemTypeFromUrl(id)
                         );
        }
    }

    m_ids.clear();
    m_trackdata_by_id.clear();

    return items;
}

QList<ListItem*> TrackModel::processTrackerReplyForUrls(TrackerCursor& cursor)
{
    while(cursor.next()) {
        auto id = QUrl(cursor.value(0).toString());

        TrackData data;
        data.title = cursor.value(2).toString();
        data.author = cursor.value(3).toString();
        data.icon = QUrl::fromLocalFile(Tracker::genAlbumArtFile(cursor.value(4).toString(), data.author));
        data.type = ContentServer::typeFromMime(cursor.value(1).toString());

        m_trackdata_by_id.insert(id, data);
    }

    return makeTrackItemsFromTrackData();
}

QList<ListItem*> TrackModel::processTrackerReply(
        TrackerTasks task,
        const QStringList& varNames,
        const QByteArray& data)
{
    TrackerCursor cursor(varNames, data);
    int n = cursor.columnCount();

    if (task == TaskPlaylist && n == 1) {
        return processTrackerReplyForPlaylist(cursor);
    } else if (task == TaskEntries && n == 1) {
        return processTrackerReplyForEntries(cursor);
    } else if (task == TaskUrls && n == 5) {
        return processTrackerReplyForUrls(cursor);
    } else if ((task == TaskAlbum || task == TaskArtist) && n == 8) {
        return processTrackerReplyForAlbumArtist(cursor);
    } else {
        qWarning() << "Tracker reply is incorrect";
    }

    return {};
}

QVariantList TrackModel::selectedItems()
{
    QVariantList list;

    for (auto item : m_list) {
        auto track = dynamic_cast<TrackItem*>(item);
        if (track->selected()) {
            QVariantMap map;
            map.insert("url", QVariant(track->url()));
            list << map;
        }
    }

    return list;
}

void TrackModel::setAlbumId(const QString &id)
{
    if (!m_artistId.isEmpty()) {
        m_artistId.clear();
        emit artistIdChanged();
    }

    if (m_albumId != id) {
        m_albumId = id;
        emit albumIdChanged();

        updateModel();
    }
}

void TrackModel::setArtistId(const QString &id)
{
    if (!m_albumId.isEmpty()) {
        m_albumId.clear();
        emit albumIdChanged();
    }

    if (m_artistId != id) {
        m_artistId = id;
        emit artistIdChanged();

        updateModel();
    }
}

void TrackModel::setPlaylistId(const QString &id)
{
    if (!m_playlistId.isEmpty()) {
        m_playlistId.clear();
        emit playlistIdChanged();
    }

    if (m_playlistId != id) {
        m_playlistId = id;
        emit playlistIdChanged();

        updateModel();
    }
}

QString TrackModel::getAlbumId()
{
    return m_albumId;
}

QString TrackModel::getArtistId()
{
    return m_artistId;
}

QString TrackModel::getPlaylistId()
{
    return m_playlistId;
}

TrackItem::TrackItem(const QString &id,
                   const QString &title,
                   const QString &artist,
                   const QString &album,
                   const QUrl &url,
                   const QUrl &icon,
                   ContentServer::Type type,
                   int number,
                   int length,
                   ContentServer::ItemType itemType,
                   QObject *parent) :
    SelectableItem(parent),
    m_id(id),
    m_title(title),
    m_artist(artist),
    m_album(album),
    m_url(url),
    m_icon(icon),
    m_type(type),
    m_number(number),
    m_length(length),
    m_item_type(itemType)
{
}

QHash<int, QByteArray> TrackItem::roleNames() const
{
    QHash<int, QByteArray> names;
    names[IdRole] = "id";
    names[TitleRole] = "title";
    names[ArtistRole] = "artist";
    names[AlbumRole] = "album";
    names[UrlRole] = "url";
    names[IconRole] = "icon";
    names[NumberRole] = "number";
    names[LengthRole] = "length";
    names[TypeRole] = "type";
    names[ItemTypeRole] = "itemType";
    names[SelectedRole] = "selected";
    return names;
}

QVariant TrackItem::data(int role) const
{
    switch(role) {
    case IdRole:
        return id();
    case TitleRole:
        return title();
    case ArtistRole:
        return artist();
    case AlbumRole:
        return album();
    case UrlRole:
        return url();
    case IconRole:
        return icon();
    case NumberRole:
        return number();
    case LengthRole:
        return length();
    case TypeRole:
        return type();
    case ItemTypeRole:
        return itemType();
    case SelectedRole:
        return selected();
    default:
        return {};
    }
}

