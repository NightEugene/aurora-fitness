import QtQuick 2.0
import Sailfish.Silica 1.0
import Aurora.Controls 1.0

// Вид главного экрана: какие карточки метрик показывать и в каком порядке.
// Порядок меняется перетаскиванием за ручку «≡» справа.
Page {
    id: page

    // Карточки, поддерживаемые подключенным устройством, в сохранённом порядке
    function visibleOrder() {
        var order = bluez.cardOrder
        var out = []
        for (var i = 0; i < order.length; i++) {
            var id = order[i]
            if (id === "sleep" && !bluez.supportsSleep)
                continue
            if (id === "stress" && !bluez.supportsStress)
                continue
            if (id === "spo2" && !bluez.supportsSpO2)
                continue
            out.push(id)
        }
        return out
    }

    function nameOf(id) {
        switch (id) {
        case "steps":    return qsTr("Шаги")
        case "calories": return qsTr("Калории")
        case "activity": return qsTr("Активность")
        case "hr":       return qsTr("Пульс")
        case "sleep":    return qsTr("Сон")
        case "stress":   return qsTr("Стресс")
        case "spo2":     return "SpO2"
        case "battery":  return qsTr("Батарея")
        }
        return id
    }

    AppBar {
        id: appBar
        headerText: qsTr("Вид")
    }

    SilicaListView {
        id: list
        anchors {
            top: appBar.bottom
            bottom: parent.bottom
            left: parent.left
            right: parent.right
        }
        model: page.visibleOrder()

        delegate: Item {
            id: cell
            width: list.width
            height: Theme.itemSizeMedium

            property bool held: false
            property real originY: 0
            z: held ? 10 : 1

            Rectangle {
                anchors.fill: parent
                color: Theme.rgba(Theme.highlightColor, 0.2)
                visible: cell.held
            }

            TextSwitch {
                anchors {
                    left: parent.left
                    right: handle.left
                    verticalCenter: parent.verticalCenter
                }
                text: page.nameOf(modelData)
                checked: bluez.cardVisibility[modelData] !== false
                onClicked: bluez.setCardVisible(modelData, checked)
            }

            // Ручка перетаскивания
            Item {
                id: handle
                anchors {
                    right: parent.right
                    rightMargin: Theme.horizontalPageMargin
                }
                width: Theme.itemSizeMedium
                height: parent.height

                Label {
                    anchors.centerIn: parent
                    text: "≡"
                    color: Theme.secondaryColor
                    font.pixelSize: Theme.fontSizeLarge
                }

                MouseArea {
                    anchors.fill: parent
                    drag.target: cell.held ? cell : null
                    drag.axis: Drag.YAxis
                    onPressed: {
                        cell.held = true
                        cell.originY = cell.y
                    }
                    onReleased: {
                        cell.held = false
                        var shift = Math.round((cell.y - cell.originY) / cell.height)
                        if (shift !== 0) {
                            var order = page.visibleOrder()
                            var from = order.indexOf(modelData)
                            order.splice(from, 1)
                            var to = Math.max(0, Math.min(order.length, from + shift))
                            order.splice(to, 0, modelData)
                            bluez.setCardOrder(order)
                        } else {
                            cell.y = cell.originY
                        }
                    }
                    onCanceled: {
                        cell.held = false
                        cell.y = cell.originY
                    }
                }
            }
        }

        VerticalScrollDecorator {}
    }
}
