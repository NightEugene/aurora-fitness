import QtQuick 2.0
import Sailfish.Silica 1.0
import Aurora.Controls 1.0

// Батарея браслета: текущий заряд, последняя зарядка, дни без зарядки,
// средний расход и история уровня. История копится с момента установки
// этой версии — до этого данных нет.
Page {
    id: page

    property var history: []
    property var stats: ({})

    readonly property color cardColor: Theme.rgba(Theme.primaryColor, 0.16)
    readonly property color accentBattery: "#4caf50"

    function reload() {
        history = storage.batteryHistory(14)
        stats = storage.batteryStats()
        chart.requestPaint()
    }

    function fmtDateTime(ts) {
        var d = new Date(ts * 1000)
        return ("0" + d.getDate()).slice(-2) + "." + ("0" + (d.getMonth() + 1)).slice(-2)
               + " " + ("0" + d.getHours()).slice(-2) + ":" + ("0" + d.getMinutes()).slice(-2)
    }

    Component.onCompleted: reload()

    Connections {
        target: storage
        onDataChanged: page.reload()
    }

    AppBar {
        id: appBar
        headerText: qsTr("Батарея")
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
            spacing: Theme.paddingMedium

            Item { width: 1; height: Theme.paddingSmall }

            // --- Текущий заряд ---
            Rectangle {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                height: currentCol.height + 2 * Theme.paddingLarge
                radius: Theme.dp(20)
                color: page.cardColor

                Column {
                    id: currentCol
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: Theme.paddingLarge
                    spacing: Theme.paddingSmall

                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: bluez.bandInfo.batteryLevel !== undefined
                              ? bluez.bandInfo.batteryLevel + " %" : "—"
                        color: page.accentBattery
                        font.pixelSize: Theme.fontSizeHuge
                        font.bold: true
                    }
                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: bluez.bandInfo.batteryState === 1
                        text: qsTr("заряжается")
                        color: Theme.secondaryColor
                        font.pixelSize: Theme.fontSizeSmall
                    }
                }
            }

            // --- Зарядка ---
            Rectangle {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                height: chargeCol.height + 2 * Theme.paddingMedium
                radius: Theme.dp(20)
                color: page.cardColor

                Column {
                    id: chargeCol
                    x: Theme.paddingLarge
                    y: Theme.paddingMedium
                    width: parent.width - 2 * x
                    spacing: Theme.paddingSmall

                    Item {
                        width: parent.width
                        height: lastChargeVal.implicitHeight
                        Label {
                            text: qsTr("Последняя зарядка")
                            color: Theme.secondaryColor
                            font.pixelSize: Theme.fontSizeSmall
                        }
                        Label {
                            id: lastChargeVal
                            anchors.right: parent.right
                            text: page.stats.lastChargeTs !== undefined
                                  ? page.fmtDateTime(page.stats.lastChargeTs)
                                  : qsTr("нет данных")
                            color: Theme.primaryColor
                            font.pixelSize: Theme.fontSizeSmall
                            font.bold: true
                        }
                    }
                    Item {
                        width: parent.width
                        height: daysVal.implicitHeight
                        Label {
                            text: qsTr("Без зарядки")
                            color: Theme.secondaryColor
                            font.pixelSize: Theme.fontSizeSmall
                        }
                        Label {
                            id: daysVal
                            anchors.right: parent.right
                            text: {
                                if (page.stats.daysSinceCharge === undefined)
                                    return "—"
                                var n = page.stats.daysSinceCharge
                                var w = qsTr("дней")
                                if (n % 10 === 1 && n % 100 !== 11)
                                    w = qsTr("день")
                                else if (n % 10 >= 2 && n % 10 <= 4
                                         && (n % 100 < 10 || n % 100 >= 20))
                                    w = qsTr("дня")
                                return n + " " + w
                            }
                            color: Theme.primaryColor
                            font.pixelSize: Theme.fontSizeSmall
                            font.bold: true
                        }
                    }
                    Item {
                        width: parent.width
                        height: drainVal.implicitHeight
                        visible: page.stats.drainPerDay !== undefined
                        Label {
                            text: qsTr("Средний расход")
                            color: Theme.secondaryColor
                            font.pixelSize: Theme.fontSizeSmall
                        }
                        Label {
                            id: drainVal
                            anchors.right: parent.right
                            text: page.stats.drainPerDay !== undefined
                                  ? "~" + page.stats.drainPerDay + " %/" + qsTr("день") : "—"
                            color: Theme.primaryColor
                            font.pixelSize: Theme.fontSizeSmall
                            font.bold: true
                        }
                    }
                }
            }

            // --- История уровня ---
            Rectangle {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                height: histCol.height + 2 * Theme.paddingMedium
                radius: Theme.dp(20)
                color: page.cardColor
                visible: page.history.length > 1

                Column {
                    id: histCol
                    x: Theme.paddingLarge
                    y: Theme.paddingMedium
                    width: parent.width - 2 * x
                    spacing: Theme.paddingSmall

                    Label {
                        text: qsTr("Уровень заряда")
                        color: Theme.primaryColor
                        font.pixelSize: Theme.fontSizeSmall
                    }

                    Canvas {
                        id: chart
                        width: parent.width
                        height: Theme.dp(220)

                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.clearRect(0, 0, width, height)
                            var pts = page.history
                            if (!pts || pts.length < 2)
                                return
                            var labelH = Theme.fontSizeExtraSmall
                            var chartH = height - labelH - Theme.paddingSmall
                            var labelW = Theme.fontSizeExtraSmall * 2.2
                            var chartW = width - labelW
                            var t0 = pts[0].ts
                            var t1 = pts[pts.length - 1].ts
                            if (t1 <= t0)
                                t1 = t0 + 1

                            // Ось Y: 0, 50, 100 %
                            ctx.fillStyle = Theme.secondaryColor
                            ctx.font = Theme.fontSizeExtraSmall + "px sans-serif"
                            ctx.textAlign = "left"
                            ctx.textBaseline = "middle"
                            ctx.fillText("100", 0, Theme.fontSizeExtraSmall / 2)
                            ctx.fillText("50", 0, chartH / 2)
                            ctx.fillText("0", 0, chartH - Theme.fontSizeExtraSmall / 2)
                            ctx.textBaseline = "alphabetic"

                            // Подписи дат по краям и середине
                            ctx.textAlign = "center"
                            var marks = [0, 0.5, 1]
                            for (var i = 0; i < marks.length; i++) {
                                var d = new Date((t0 + (t1 - t0) * marks[i]) * 1000)
                                var tx = labelW + chartW * marks[i]
                                tx = Math.max(labelW + 14, Math.min(width - 14, tx))
                                ctx.fillText(("0" + d.getDate()).slice(-2) + "."
                                             + ("0" + (d.getMonth() + 1)).slice(-2),
                                             tx, height)
                            }

                            // Линия уровня
                            ctx.strokeStyle = page.accentBattery
                            ctx.lineWidth = Theme.dp(2)
                            ctx.beginPath()
                            for (i = 0; i < pts.length; i++) {
                                var x = labelW + chartW * (pts[i].ts - t0) / (t1 - t0)
                                var y = chartH * (1 - pts[i].level / 100.0)
                                if (i === 0)
                                    ctx.moveTo(x, y)
                                else
                                    ctx.lineTo(x, y)
                            }
                            ctx.stroke()
                        }
                    }
                }
            }

            Item { width: 1; height: Theme.paddingMedium }
        }
    }
}
