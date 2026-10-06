import QtQuick 2.0
import Sailfish.Silica 1.0

CoverBackground {
    id: cover

    property var today: ({})

    readonly property color accentKcal: "#ff9800"
    readonly property color accentActivity: "#8bc34a"

    function reload() {
        today = storage.todaySummary()
        stepsChev.requestPaint()
        kcalChev.requestPaint()
        actChev.requestPaint()
    }

    // Переполнение цели: полоска остаётся полной и своего цвета, число
    // полных «кругов» — чёрными шевронами ">" на позиции текущего круга.
    function lapInfo(value, goal) {
        if (value === undefined || goal <= 0)
            return { fill: 0.0, laps: 0, over: 0.0 }
        var ov = value / goal
        var laps = Math.floor(ov)
        if (laps < 1)
            return { fill: ov, laps: 0, over: 0.0 }
        return { fill: 1.0, laps: laps, over: ov - laps }
    }

    // Шевроны ">" по направлению полоски (вправо), cx — позиция текущего круга
    function drawBarChevrons(ctx, w, h, laps, overFrac) {
        var cx = overFrac * w
        var cy = h / 2
        var s = h * 0.35
        ctx.strokeStyle = "black"
        ctx.lineWidth = Math.max(1.5, h * 0.18)
        ctx.lineCap = "round"
        for (var k = 0; k < laps; k++) {
            var bx = cx - k * s * 1.6
            ctx.beginPath()
            ctx.moveTo(bx - s, cy - s)
            ctx.lineTo(bx + s, cy)
            ctx.lineTo(bx - s, cy + s)
            ctx.stroke()
        }
    }

    Component.onCompleted: reload()
    onStatusChanged: {
        if (status === Cover.Active)
            reload()
    }
    Timer {
        interval: 60000
        repeat: true
        running: cover.status === Cover.Active
        onTriggered: cover.reload()
    }
    Connections {
        target: bluez
        onStepsGoalChanged: reload()
        onCaloriesGoalChanged: reload()
        onActivityGoalChanged: reload()
    }

    Connections {
        target: storage
        onDataChanged: reload()
    }

    Label {
        anchors.centerIn: parent
        visible: bluez.connectedAddress.length === 0
        text: qsTr("Не подключено")
        color: Theme.secondaryColor
        font.pixelSize: Theme.fontSizeMedium
    }

    Column {
        anchors {
            centerIn: parent
            verticalCenterOffset: -Theme.paddingMedium
        }
        width: parent.width - 2 * Theme.paddingLarge
        spacing: Theme.paddingMedium
        visible: bluez.connectedAddress.length > 0

        // Шаги
        Column {
            width: parent.width
            spacing: Theme.paddingSmall
            property var lap: cover.lapInfo(cover.today.steps, bluez.stepsGoal)

            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: today.steps !== undefined ? today.steps : "—"
                color: Theme.highlightColor
                font.pixelSize: Theme.fontSizeHuge
            }
            Rectangle {
                width: parent.width
                height: Math.round(Theme.paddingSmall * 1.5)
                radius: height / 2
                color: Theme.rgba(Theme.highlightColor, 0.2)
                Rectangle {
                    width: parent.width * parent.parent.lap.fill
                    height: parent.height
                    radius: parent.radius
                    color: Theme.highlightColor
                }
                Canvas {
                    id: stepsChev
                    anchors.fill: parent
                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        var lap = parent.parent.lap
                        if (lap.laps >= 1)
                            cover.drawBarChevrons(ctx, width, height, lap.laps, lap.over)
                    }
                }
            }
        }

        // Калории
        Column {
            width: parent.width
            spacing: Theme.paddingSmall
            property var lap: cover.lapInfo(cover.today.calories, bluez.caloriesGoal)

            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: today.calories !== undefined ? today.calories : "—"
                color: cover.accentKcal
                font.pixelSize: Theme.fontSizeLarge
            }
            Rectangle {
                width: parent.width
                height: Math.round(Theme.paddingSmall * 1.5)
                radius: height / 2
                color: Theme.rgba(cover.accentKcal, 0.2)
                Rectangle {
                    width: parent.width * parent.parent.lap.fill
                    height: parent.height
                    radius: parent.radius
                    color: cover.accentKcal
                }
                Canvas {
                    id: kcalChev
                    anchors.fill: parent
                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        var lap = parent.parent.lap
                        if (lap.laps >= 1)
                            cover.drawBarChevrons(ctx, width, height, lap.laps, lap.over)
                    }
                }
            }
        }

        // Активность
        Column {
            width: parent.width
            spacing: Theme.paddingSmall
            property var lap: cover.lapInfo(cover.today.activityMin, bluez.activityGoal)

            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: today.activityMin !== undefined ? today.activityMin : "—"
                color: cover.accentActivity
                font.pixelSize: Theme.fontSizeLarge
            }
            Rectangle {
                width: parent.width
                height: Math.round(Theme.paddingSmall * 1.5)
                radius: height / 2
                color: Theme.rgba(cover.accentActivity, 0.2)
                Rectangle {
                    width: parent.width * parent.parent.lap.fill
                    height: parent.height
                    radius: parent.radius
                    color: cover.accentActivity
                }
                Canvas {
                    id: actChev
                    anchors.fill: parent
                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        var lap = parent.parent.lap
                        if (lap.laps >= 1)
                            cover.drawBarChevrons(ctx, width, height, lap.laps, lap.over)
                    }
                }
            }
        }
    }
}
