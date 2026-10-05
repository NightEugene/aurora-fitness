import QtQuick 2.0
import Sailfish.Silica 1.0
import Aurora.Controls 1.0

Page {
    id: page

    AppBar {
        id: appBar
        headerText: qsTr("О приложении")
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
            spacing: Theme.paddingLarge

            Item { width: 1; height: Theme.paddingLarge }

            Image {
                anchors.horizontalCenter: parent.horizontalCenter
                source: "/usr/share/icons/hicolor/172x172/apps/ru.nighteugene.aurorafitness.png"
                width: Theme.dp(172)
                height: width
                sourceSize.width: 172
                sourceSize.height: 172
            }

            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Аврора Фитнес")
                color: Theme.primaryColor
                font.pixelSize: Theme.fontSizeLarge
                font.bold: true
            }

            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Версия 1.1.0")
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeSmall
            }

            Item { width: 1; height: Theme.paddingSmall }

            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Поддерживаемые устройства")
                color: Theme.primaryColor
                font.pixelSize: Theme.fontSizeSmall
                font.bold: true
            }

            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Mi Band 8 — " + qsTr("полная поддержка")
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeSmall
            }

            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "PineTime (InfiniTime) — " + qsTr("базовая поддержка")
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeSmall
            }

            Item { width: 1; height: Theme.paddingSmall }

            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Разработчики")
                color: Theme.primaryColor
                font.pixelSize: Theme.fontSizeSmall
                font.bold: true
            }

            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Eugene Todoruk (nighteugene)"
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeSmall
            }

            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Pavel Bibichenko (erhoof)"
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeSmall
            }

            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "github.com/nighteugene/aurora-fitness"
                color: Theme.highlightColor
                font.pixelSize: Theme.fontSizeSmall
                font.underline: true

                MouseArea {
                    anchors.fill: parent
                    onClicked: Qt.openUrlExternally(
                                   "https://github.com/nighteugene/aurora-fitness")
                }
            }

            Item { width: 1; height: Theme.paddingSmall }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                horizontalAlignment: Text.AlignHCenter
                color: Theme.primaryColor
                font.pixelSize: Theme.fontSizeSmall
                font.bold: true
                text: qsTr("Как получить auth key браслета")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeSmall
                text: qsTr("Для подключения Mi Band 8 нужен ключ аутентификации "
                           + "(32 hex-символа). Спарьте браслет с приложением "
                           + "Mi Fitness на Android и найдите ключ в его логах "
                           + "по строке «encryptKey»: "
                           + "/sdcard/Android/data/com.xiaomi.wearable/"
                           + "files/log/XiaomiFit.device.log "
                           + "(root не требуется).")
            }

            Item { width: 1; height: Theme.paddingSmall }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeSmall
                text: qsTr("Приложение создано с помощью ИИ и открыто для доработки: "
                           + "по исходному коду и логам Mi Fitness можно адаптировать "
                           + "его под свои часы — попросите об этом вашу любимую "
                           + "ИИ-модель, она разберётся.")
            }

            Item { width: 1; height: Theme.paddingLarge }
        }
    }
}
