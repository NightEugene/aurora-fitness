import QtQuick 2.0
import Sailfish.Silica 1.0
import Aurora.Controls 1.0

Page {
    id: page

    function isBand(name) {
        return name.indexOf("Smart Band") !== -1 || name.indexOf("Mi Band") !== -1 || name.toLowerCase().indexOf("pinetime") !== -1 || name.toLowerCase().indexOf("infinitime") !== -1
    }

    AppBar {
        id: appBar

        headerText: qsTr("Настройки")
    }

    BusyIndicator {
        anchors.top: appBar.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        size: BusyIndicatorSize.Medium
        running: bluez.scanning
    }

    SilicaListView {
        id: listView
        anchors {
            top: appBar.bottom
            bottom: parent.bottom
            left: parent.left
            right: parent.right
        }
        model: bluez.devices

        header: Column {
            width: parent.width

            SectionHeader {
                text: qsTr("Фоновый режим")
            }

            TextSwitch {
                text: qsTr("Пересылать уведомления")
                description: qsTr("Фоновый демон пересылает системные уведомления на браслет")
                checked: bluez.daemonEnabled()
                onClicked: bluez.setDaemonEnabled(checked)
            }

            TextSwitch {
                text: qsTr("Автосинхронизация каждые 30 мин")
                description: qsTr("Демон периодически синхронизирует данные активности")
                checked: bluez.daemonSyncEnabled()
                onClicked: bluez.setDaemonSyncEnabled(checked)
            }

            SectionHeader {
                text: qsTr("Подключено")
                visible: bluez.connectedAddress.length > 0
            }

            BackgroundItem {
                width: parent.width
                height: Theme.itemSizeMedium
                visible: bluez.connectedAddress.length > 0
                onClicked: pageStack.push(Qt.resolvedUrl("DevicePage.qml"), {
                                              "address": bluez.connectedAddress,
                                              "deviceName": bluez.connectedDeviceName
                                          })

                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * x

                    Label {
                        width: parent.width
                        truncationMode: TruncationMode.Fade
                        color: Theme.highlightColor
                        text: bluez.connectedDeviceName
                    }
                    Label {
                        width: parent.width
                        truncationMode: TruncationMode.Fade
                        color: Theme.secondaryColor
                        font.pixelSize: Theme.fontSizeExtraSmall
                        text: bluez.connectedAddress
                              + (bluez.bandInfo.batteryLevel !== undefined
                                 && bluez.bandInfo.batteryLevel > 0
                                 ? "  ·  " + bluez.bandInfo.batteryLevel + "%" : "")
                    }
                }
            }

            SectionHeader {
                text: qsTr("Устройства")
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: bluez.scanning ? qsTr("Остановить сканирование") : qsTr("Сканировать")
                onClicked: bluez.scanning ? bluez.stopScan() : bluez.startScan()
            }

            Item { width: 1; height: Theme.paddingMedium }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: listView.count === 0
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryColor
                wrapMode: Text.Wrap
                text: bluez.scanning
                      ? qsTr("Ищем устройства…")
                      : (bluez.adapterPowered
                         ? qsTr("Устройства не найдены. Нажмите «Сканировать».")
                         : qsTr("Включите Bluetooth"))
            }

            Item { width: 1; height: Theme.paddingMedium }
        }

        delegate: ListItem {
            id: delegate
            contentHeight: Theme.itemSizeMedium

            Rectangle {
                anchors.fill: parent
                color: Theme.rgba(Theme.highlightBackgroundColor,
                                  page.isBand(modelData.name) ? 0.15 : 0.0)
            }

            Column {
                anchors.verticalCenter: parent.verticalCenter
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x

                Label {
                    text: modelData.name + (page.isBand(modelData.name) ? "  ⌚" : "")
                    color: delegate.highlighted ? Theme.highlightColor : Theme.primaryColor
                    truncationMode: TruncationMode.Fade
                    width: parent.width
                }
                Label {
                    text: modelData.address + "   RSSI " + modelData.rssi
                          + (modelData.connected ? "   ●" : "")
                    color: Theme.secondaryColor
                    font.pixelSize: Theme.fontSizeExtraSmall
                }
            }

            onClicked: {
                bluez.stopScan()
                bluez.connectToBand(modelData.address)
                pageStack.push(Qt.resolvedUrl("DevicePage.qml"),
                               { "address": modelData.address, "deviceName": modelData.name })
            }
        }

        VerticalScrollDecorator {}
    }
}
