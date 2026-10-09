import QtQuick 2.0
import Sailfish.Silica 1.0
import Aurora.Controls 1.0

Page {
    AppBar {
        id: appBar
        headerText: qsTr("Профиль")
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

            TextField {
                id: weightField
                width: parent.width
                label: qsTr("Вес, кг")
                inputMethodHints: Qt.ImhDigitsOnly
                validator: IntValidator { bottom: 20; top: 300 }
                Component.onCompleted: text = Math.round(bluez.weightKg)
                function apply() {
                    if (acceptableInput)
                        bluez.weightKg = parseInt(text)
                    text = Math.round(bluez.weightKg)
                }
                EnterKey.onClicked: { apply(); focus = false }
                onActiveFocusChanged: if (!activeFocus) apply()
            }

            TextField {
                id: heightField
                width: parent.width
                label: qsTr("Рост, см")
                inputMethodHints: Qt.ImhDigitsOnly
                validator: IntValidator { bottom: 80; top: 250 }
                Component.onCompleted: text = bluez.heightCm
                function apply() {
                    if (acceptableInput)
                        bluez.heightCm = parseInt(text)
                    text = bluez.heightCm
                }
                EnterKey.onClicked: { apply(); focus = false }
                onActiveFocusChanged: if (!activeFocus) apply()
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                text: qsTr("Рост и вес используются для оценки калорий ходьбы, если устройство не передаёт калории.")
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
            }

        }
        VerticalScrollDecorator {}
    }
}
