import QtQuick 2.0
import Sailfish.Silica 1.0
import Aurora.Controls 1.0

Page {
    id: page
    property string address
    property string deviceName

    function serviceName(uuid) {
        var known = {
            "00001800-0000-1000-8000-00805f9b34fb": "Generic Access",
            "00001801-0000-1000-8000-00805f9b34fb": "Generic Attribute",
            "0000180a-0000-1000-8000-00805f9b34fb": "Device Information",
            "0000180f-0000-1000-8000-00805f9b34fb": "Battery Service",
            "0000fee0-0000-1000-8000-00805f9b34fb": "Xiaomi (FEE0)",
            "0000fee1-0000-1000-8000-00805f9b34fb": "Xiaomi (FEE1)",
            "0000fe95-0000-1000-8000-00805f9b34fb": "Xiaomi (FE95)"
        }
        return known[uuid] || uuid
    }

    AppBar {
        id: appBar

        headerText: deviceName
    }

    SilicaFlickable {
        anchors {
            top: appBar.bottom
            bottom: parent.bottom
            left: parent.left
            right: parent.right
        }
        contentHeight: column.height

        Column {
            id: column
            width: parent.width

            Item { width: 1; height: Theme.paddingLarge }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: bluez.connectedAddress === page.address
                      ? qsTr("Отключиться") : qsTr("Подключиться")
                onClicked: {
                    if (bluez.connectedAddress === page.address)
                        bluez.disconnectBand()
                    else
                        bluez.connectToBand(page.address)
                }
            }

            Item { width: 1; height: Theme.paddingMedium }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                text: bluez.userStatus
                visible: text.length > 0
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.Wrap
            }

            SectionHeader {
                text: qsTr("Mi Band 8")
                visible: bluez.requiresAuth
            }

            TextField {
                id: keyField
                width: parent.width
                visible: bluez.requiresAuth && bluez.connectedAddress.length > 0
                         && !bluez.ready
                placeholderText: qsTr("Auth key (32 hex-символа)")
                label: qsTr("Ключ из логов Mi Fitness")
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: keyField.visible
                text: qsTr("Авторизоваться")
                enabled: keyField.text.length === 32 || keyField.text.length === 0
                onClicked: {
                    if (keyField.text.length === 32)
                        bluez.setAuthKey(keyField.text)
                    bluez.startBandAuth()
                }
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: bluez.authStatus.length > 0
                text: bluez.authStatus
                color: bluez.ready
                       ? "#4caf50" : Theme.secondaryColor
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.Wrap
            }

            Item { width: 1; height: Theme.paddingLarge }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: bluez.ready
                text: qsTr("Синхронизировать данные")
                onClicked: bluez.syncActivity()
            }

            SectionHeader {
                text: qsTr("Уведомление на браслет")
                visible: bluez.ready
            }

            TextField {
                id: notifTitle
                width: parent.width
                visible: bluez.ready
                placeholderText: qsTr("Заголовок")
                label: qsTr("Заголовок")
            }

            TextField {
                id: notifBody
                width: parent.width
                visible: bluez.ready
                placeholderText: qsTr("Текст уведомления")
                label: qsTr("Текст уведомления")
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: bluez.ready
                enabled: notifTitle.text.length > 0 || notifBody.text.length > 0
                text: qsTr("Отправить уведомление")
                onClicked: bluez.sendTestNotification(notifTitle.text, notifBody.text,
                                                      "Аврора Фитнес",
                                                      "ru.nighteugene.aurorafitness")
            }

            SectionHeader {
                text: qsTr("Данные активности (%1)").arg(bluez.activityResults.length)
                visible: bluez.activityResults.length > 0
            }

            Repeater {
                model: bluez.activityResults

                delegate: Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * x
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.primaryColor
                    wrapMode: Text.Wrap
                    text: {
                        var m = modelData
                        var dt = m.timestamp
                            ? new Date(m.timestamp * 1000).toLocaleString(Qt.locale(), "dd.MM HH:mm")
                            : "?"
                        if (m.kind === "dailySummary")
                            return dt + " — шаги: " + (m.steps || "?")
                                    + ", ккал: " + (m.calories || "?")
                                    + ", пульс ср: " + (m.avgHr || "?")
                        if (m.kind === "sleep")
                            return dt + " — сон: " + (m.summary ? m.summary.sleepMin : "?") + " мин"
                        if (m.kind === "dailyDetails")
                            return dt + " — поминутные сэмплы: " + (m.samples ? m.samples.length : 0)
                        if (m.kind === "manualSamples")
                            return dt + " — точечные измерения: " + (m.samples ? m.samples.length : 0)
                        return dt + " — " + (m.kind || "unknown")
                    }
                }
            }

            SectionHeader {
                text: qsTr("Данные браслета")
                visible: Object.keys(bluez.bandInfo).length > 0
            }

            Repeater {
                model: {
                    var info = bluez.bandInfo
                    var labels = {
                        "deviceName": qsTr("Имя"),
                        "modelNumber": qsTr("Модель"),
                        "serialNumber": qsTr("Серийный номер"),
                        "firmwareRevision": qsTr("Прошивка"),
                        "manufacturer": qsTr("Производитель"),
                        "batteryLevel": qsTr("Батарея, %")
                    }
                    var rows = []
                    for (var key in labels)
                        if (info[key] !== undefined)
                            rows.push({ "label": labels[key], "value": info[key] })
                    return rows
                }

                delegate: DetailItem {
                    label: modelData.label
                    value: modelData.value
                }
            }

            SectionHeader {
                text: qsTr("GATT-сервисы (%1)").arg(bluez.services.length)
                visible: bluez.services.length > 0
            }

            Repeater {
                model: bluez.services

                delegate: Column {
                    width: parent.width

                    Label {
                        x: Theme.horizontalPageMargin
                        text: page.serviceName(modelData.uuid)
                        color: Theme.highlightColor
                        font.pixelSize: Theme.fontSizeSmall
                    }

                    Repeater {
                        model: modelData.characteristics

                        delegate: Label {
                            x: 2 * Theme.horizontalPageMargin
                            text: modelData.uuid + "  [" + modelData.flags.join(", ") + "]"
                            color: Theme.secondaryColor
                            font.pixelSize: Theme.fontSizeExtraSmall
                        }
                    }

                    Item { width: 1; height: Theme.paddingSmall }
                }
            }

            Item { width: 1; height: Theme.paddingLarge }
        }

        VerticalScrollDecorator {}
    }
}
