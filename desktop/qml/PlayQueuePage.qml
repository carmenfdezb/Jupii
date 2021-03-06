/* Copyright (C) 2020 Michal Kosciesza <michal@mkiol.net>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

import QtQuick 2.4
import QtQuick.Controls 2.2 as Controls
import QtQuick.Layouts 1.2
import org.kde.kirigami 2.14 as Kirigami
import QtQuick.Dialogs 1.1

import harbour.jupii.AVTransport 1.0
import harbour.jupii.RenderingControl 1.0
import harbour.jupii.PlayListModel 1.0
import harbour.jupii.ContentServer 1.0

Kirigami.ScrollablePage {
    id: root

    property bool devless: av.deviceId.length === 0 && rc.deviceId.length === 0
    property int itemType: utils.itemTypeFromUrl(av.currentId)
    property bool inited: directory.inited && (av.inited || rc.busy)
    property bool busy: !directory.inited || playlist.busy || playlist.refreshing || av.busy || rc.busy
    property bool canCancel: directory.inited && !av.busy && !rc.busy && (playlist.busy || playlist.refreshing)
    property bool controlEnabled: true

    Kirigami.Theme.colorSet: Kirigami.Theme.View

    implicitWidth: Kirigami.Units.gridUnit * 20

    Component.onCompleted: {
        refreshing = Qt.binding(function() { return root.busy })
    }

    title: av.deviceFriendlyName.length > 0 ? av.deviceFriendlyName : qsTr("Play queue")

    actions {
        main: Kirigami.Action {
            id: addAction
            text: qsTr("Add items")
            checked: app.addMediaPageAction.checked
            iconName: "list-add"
            enabled: !root.busy && root.controlEnabled
            onTriggered: app.addMediaPageAction.trigger()
        }

        contextualActions: [
            Kirigami.Action {
                text: qsTr("Cancel")
                checked: addMediaPageAction.checked
                iconName: "dialog-cancel"
                enabled: root.canCancel && root.controlEnabled
                visible: enabled
                onTriggered: {
                    if (playlist.busy)
                        playlist.cancelAdd()
                    else if (playlist.refreshing)
                        playlist.cancelRefresh()
                }
            },
            Kirigami.Action {
                text: qsTr("Track info")
                checked: app.trackInfoAction.checked
                iconName: "documentinfo"
                enabled: av.controlable
                onTriggered: app.trackInfoAction.trigger()
            },
            Kirigami.Action {
                displayHint: Kirigami.Action.DisplayHint.AlwaysHide
                text: qsTr("Save queue")
                iconName: "document-save"
                enabled: root.controlEnabled
                visible: !playlist.refreshing && !playlist.busy && itemList.count !== 0
                onTriggered: saveDialog.open()
            },
            Kirigami.Action {
                displayHint: Kirigami.Action.DisplayHint.AlwaysHide
                text: qsTr("Clear queue")
                iconName: "edit-clear-all"
                enabled: root.controlEnabled
                visible: !root.busy && itemList.count !== 0
                onTriggered: clearDialog.open()
            },
            Kirigami.Action {
                text: qsTr("Refresh items")
                iconName: "view-refresh"
                enabled: root.controlEnabled
                visible: !root.busy && playlist.refreshable && itemList.count !== 0
                onTriggered: playlist.refresh()
            }
        ]
    }

    function showActiveItem() {
        if (playlist.activeItemIndex >= 0)
            itemList.positionViewAtIndex(playlist.activeItemIndex, ListView.Contain)
        else
            itemList.positionViewAtBeginning()
    }

    function showLastItem() {
        itemList.positionViewAtEnd();
    }

    Connections {
        target: playlist

        onItemsAdded: root.showLastItem()
        onItemsLoaded: root.showActiveItem()
        onActiveItemChanged: root.showActiveItem()

        onBusyChanged: {
            refreshing = playlist.busy
        }

        onError: {
            if (code === PlayListModel.E_FileExists)
                notifications.show(qsTr("Item is already in play queue"))
            else if (code === PlayListModel.E_ItemNotAdded)
                notifications.show(qsTr("Item cannot be added"))
            else if (code === PlayListModel.E_SomeItemsNotAdded)
                notifications.show(qsTr("Some items cannot be added"))
            else if (code === PlayListModel.E_AllItemsNotAdded)
                notifications.show(qsTr("Items cannot be added"))
            else
                notifications.show(qsTr("Unknown error"))
        }
    }

    FileDialog {
        id: saveDialog
        title: qsTr("Save items to playlist file")
        selectMultiple: false
        selectFolder: false
        selectExisting: false
        defaultSuffix: "pls"
        nameFilters: [ "Playlist (*.pls)" ]
        folder: shortcuts.music
        onAccepted: {
            if (playlist.saveToUrl(saveDialog.fileUrls[0]))
                showPassiveNotification(qsTr("Playlist has been saved"))
        }
    }

    MessageDialog {
        id: clearDialog
        title: qsTr("Clear queue")
        icon: StandardIcon.Question
        text: qsTr("Remove all items from play queue?")
        standardButtons: StandardButton.Ok | StandardButton.Cancel
        onAccepted: {
            playlist.clear()
        }
    }

    Controls.Label {
        anchors.top: parent.top
        anchors.topMargin: Kirigami.Units.smallSpacing
        anchors.horizontalCenter: parent.horizontalCenter
        font: Kirigami.Theme.smallFont
        color: Kirigami.Theme.disabledTextColor
        visible: playlist.busy || playlist.refreshing
        wrapMode: Text.WordWrap
        text: playlist.refreshing ?
                  playlist.progressTotal > 1 ?
                      qsTr("Refreshing item %1 of %2...").arg(playlist.progressValue + 1).arg(playlist.progressTotal) :
                      qsTr("Refreshing item...") :
                  playlist.progressTotal > 1 ?
                      qsTr("Adding item %1 of %2...").arg(playlist.progressValue + 1).arg(playlist.progressTotal) :
                      qsTr("Adding item...")
    }

    Component {
        id: listItemComponent
        DoubleListItem {
            id: listItem

            property bool isImage: model.type === AVTransport.T_Image
            property bool playing: model.id == av.currentId &&
                                   av.transportState === AVTransport.Playing
            enabled: !root.busy
            label: model.name
            busy: model.toBeActive
            subtitle: model.artist.length > 0 ? model.artist : ""
            defaultIconSource: {
                if (model.itemType === ContentServer.ItemType_Mic)
                    return "audio-input-microphone"
                else if (model.itemType === ContentServer.ItemType_AudioCapture)
                    return "player-volume"
                else if (model.itemType === ContentServer.ItemType_ScreenCapture)
                    return "computer"
                else
                    switch (model.type) {
                    case AVTransport.T_Image:
                        return "image-x-generic"
                    case AVTransport.T_Audio:
                        return "audio-x-generic"
                    case AVTransport.T_Video:
                        return "video-x-generic"
                    default:
                        return "unknown"
                    }
            }
            attachedIconName: model.itemType === ContentServer.ItemType_Url ?
                                  "folder-remote" :
                              model.itemType === ContentServer.ItemType_Upnp ?
                                  "network-server" : ""
            iconSource: {
                if (model.itemType === ContentServer.ItemType_Mic ||
                        model.itemType === ContentServer.ItemType_AudioCapture ||
                        model.itemType === ContentServer.ItemType_ScreenCapture) {
                    return ""
                }
                return model.icon
            }
            iconSize: Kirigami.Units.iconSizes.medium

            onClicked: {
                if (!listItem.enabled)
                    return;
                if (!root.inited)
                    return;

                if (model.active)
                    playlist.togglePlay()
                else
                    playlist.play(model.id)
            }

            active: model.active

            actions: [
                Kirigami.Action {
                    iconName: listItem.playing ?
                                  "media-playback-pause" : "media-playback-start"
                    text: listItem.playing ?
                              qsTr("Pause") : listItem.isImage ? qsTr("Show") : qsTr("Play")
                    visible: root.inited
                    onTriggered: {
                        if (!model.active)
                            playlist.play(model.id)
                        else
                            playlist.togglePlay()
                    }
                },
                Kirigami.Action {
                    iconName: "delete"
                    text: qsTr("Remove")
                    onTriggered: {
                        playlist.remove(model.id)
                    }
                }
            ]
        }
    }

    ListView {
        id: itemList
        model: playlist
        enabled: root.controlEnabled

        delegate: Kirigami.DelegateRecycler {
            width: parent.width
            sourceComponent: listItemComponent
        }

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - (Kirigami.Units.largeSpacing * 4)
            visible: !root.busy && itemList.count === 0
            text: qsTr("No items")
            helpfulAction: Kirigami.Action {
                iconName: "list-add"
                text: qsTr("Add items")
                onTriggered: addAction.trigger()
            }
        }
    }

    footer: PlayerPanel {
        id: ppanel

        enabled: root.controlEnabled

        width: root.width

        open: root.inited && av.controlable
        inited: root.inited

        title: av.currentTitle.length === 0 ? qsTr("Unknown") : av.currentTitle
        subtitle: app.streamTitle.length === 0 ?
                      (root.itemType !== ContentServer.ItemType_Mic &&
                       root.itemType !== ContentServer.ItemType_AudioCapture &&
                       root.itemType !== ContentServer.ItemType_ScreenCapture ?
                           av.currentAuthor : "") : app.streamTitle
        itemType: root.itemType

        prevEnabled: playlist.prevSupported &&
                     !playlist.refreshing
        nextEnabled: playlist.nextSupported &&
                     !playlist.refreshing

        forwardEnabled: av.seekSupported &&
                        av.transportState === AVTransport.Playing &&
                        av.currentType !== AVTransport.T_Image
        backwardEnabled: forwardEnabled
        recordEnabled: app.streamRecordable
        recordActive: app.streamToRecord

        playMode: playlist.playMode

        controlable: av.controlable

        onNextClicked: playlist.next()
        onPrevClicked: playlist.prev()
        onTogglePlayClicked: playlist.togglePlay()

        onForwardClicked: {
            var pos = av.relativeTimePosition + settings.forwardTime
            var max = av.currentTrackDuration
            av.seek(pos > max ? max : pos)
        }

        onBackwardClicked: {
            var pos = av.relativeTimePosition - settings.forwardTime
            av.seek(pos < 0 ? 0 : pos)
        }

        onRepeatClicked: {
            playlist.togglePlayMode()
        }

        onRecordClicked: {
            cserver.setStreamToRecord(av.currentId, !app.streamToRecord)
        }

        onClicked: {
            full = !full
        }

        onIconClicked: {
            app.trackInfoAction.trigger()
        }
    }
}
