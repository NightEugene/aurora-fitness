import QtQuick 2.0
import Sailfish.Silica 1.0
import Aurora.Controls 1.0
import "../ProgressMarkers.js" as ProgressMarkers

// Дневная статистика: кольца целей, неделя, почасовые шаги, метрики дня.
// Открывается тапом по карточке шагов на главной. Дата выбирается стрелками,
// недельной лентой или календарём (тап по дате).
Page {
    id: page

    property date currentDate: new Date()
    property var day: ({})
    property var summaries: []

    readonly property color cardColor: Theme.rgba(Theme.primaryColor, 0.16)
    readonly property color accentKcal: "#ff9800"
    readonly property color accentActivity: "#8bc34a"
    readonly property color accentHr: "#ff5252"
    readonly property color accentSpo2: "#03a9f4"
    readonly property color accentStress: "#26a69a"

    readonly property var monthNames: [
        qsTr("января"), qsTr("февраля"), qsTr("марта"), qsTr("апреля"),
        qsTr("мая"), qsTr("июня"), qsTr("июля"), qsTr("августа"),
        qsTr("сентября"), qsTr("октября"), qsTr("ноября"), qsTr("декабря")]
    readonly property var weekdayLetters: [
        qsTr("П"), qsTr("В"), qsTr("С"), qsTr("Ч"), qsTr("П"), qsTr("С"), qsTr("В")]

    function dayStartTs(d) {
        return new Date(d.getFullYear(), d.getMonth(), d.getDate()).getTime() / 1000
    }

    function sameDay(a, b) {
        return a.getFullYear() === b.getFullYear() && a.getMonth() === b.getMonth()
                && a.getDate() === b.getDate()
    }

    function fmtLongDate(d) {
        return d.getDate() + " " + monthNames[d.getMonth()] + " " + d.getFullYear() + " г."
    }

    function fmtShort(d) {
        return ("0" + d.getDate()).slice(-2) + "." + ("0" + (d.getMonth() + 1)).slice(-2)
    }

    function summaryFor(d) {
        var ts = dayStartTs(d)
        for (var i = 0; i < summaries.length; i++) {
            if (summaries[i].ts === ts)
                return summaries[i]
        }
        return null
    }


    function multiplierText(value, goal) {
        if (!goal || value < goal)
            return ""
        return " ×" + (Math.round(value * 10.0 / goal) / 10)
    }

    function shiftDay(delta) {
        var d = new Date(currentDate)
        d.setDate(d.getDate() + delta)
        var now = new Date()
        if (dayStartTs(d) > dayStartTs(now))
            return
        currentDate = d
        reload()
    }

    function reload() {
        summaries = storage.dailySummaries(0)
        var t0 = dayStartTs(currentDate)
        day = storage.daySummary(t0)

        ringsCanvas.requestPaint()
        weekRepeater.model = 0 // пересоздать мини-кольца
        weekRepeater.model = 7
    }

    // Векторные иконки для колец (как на главной): ботинок, огонёк, часы
    function drawRingIcon(ctx, icon, x, y, size, color) {
        var s = size / 2
        ctx.save()
        ctx.translate(x, y)
        ctx.fillStyle = color
        ctx.beginPath()
        ctx.arc(0, 0, s, 0, Math.PI * 2)
        ctx.fill()
        var gs = s * 0.62
        ctx.fillStyle = "rgba(0, 0, 0, 0.55)"
        ctx.strokeStyle = "rgba(0, 0, 0, 0.55)"
        if (icon === "clock") {
            ctx.lineWidth = Math.max(1.5, gs * 0.22)
            ctx.beginPath()
            ctx.arc(0, 0, gs * 0.85, 0, Math.PI * 2)
            ctx.stroke()
            ctx.lineCap = "round"
            ctx.beginPath()
            ctx.moveTo(0, 0)
            ctx.lineTo(0, -gs * 0.5)
            ctx.moveTo(0, 0)
            ctx.lineTo(gs * 0.42, 0)
            ctx.stroke()
        } else if (icon === "flame") {
            ctx.beginPath()
            ctx.moveTo(0, gs)
            ctx.bezierCurveTo(-0.9 * gs, 0.55 * gs, -0.75 * gs, -0.1 * gs, -0.25 * gs, -0.45 * gs)
            ctx.bezierCurveTo(-0.35 * gs, -0.75 * gs, -0.1 * gs, -0.55 * gs, 0, -gs)
            ctx.bezierCurveTo(0.45 * gs, -0.45 * gs, 0.85 * gs, 0, 0.55 * gs, 0.55 * gs)
            ctx.bezierCurveTo(0.4 * gs, 0.85 * gs, 0.2 * gs, gs, 0, gs)
            ctx.fill()
        } else { // shoe — след: подушечка + пятка
            ctx.rotate(-0.18)
            ctx.save()
            ctx.translate(0, -0.3 * gs)
            ctx.scale(1, 1.3)
            ctx.beginPath()
            ctx.arc(0, 0, 0.42 * gs, 0, Math.PI * 2)
            ctx.fill()
            ctx.restore()
            ctx.beginPath()
            ctx.arc(0.05 * gs, 0.62 * gs, 0.3 * gs, 0, Math.PI * 2)
            ctx.fill()
        }
        ctx.restore()
    }

    Component.onCompleted: reload()

    Connections {
        target: storage
        onDataChanged: page.reload()
    }

    AppBar {
        id: appBar
        headerText: qsTr("Статистика")
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

            // --- Выбор дня: ‹ дата › ---
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Theme.paddingLarge

                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "‹"
                    color: Theme.highlightColor
                    font.pixelSize: Theme.fontSizeLarge
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -Theme.paddingMedium
                        onClicked: page.shiftDay(-1)
                    }
                }
                Label {
                    id: dateLabel
                    anchors.verticalCenter: parent.verticalCenter
                    text: page.fmtLongDate(page.currentDate)
                    color: Theme.primaryColor
                    font.pixelSize: Theme.fontSizeMedium
                    font.bold: true

                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -Theme.paddingSmall
                        onClicked: {
                            var marked = []
                            for (var i = 0; i < page.summaries.length; i++) {
                                var d = new Date(page.summaries[i].ts * 1000)
                                marked.push(d.getFullYear() + "-"
                                            + ("0" + (d.getMonth() + 1)).slice(-2) + "-"
                                            + ("0" + d.getDate()).slice(-2))
                            }
                            var dlg = pageStack.push(
                                        Qt.resolvedUrl("SleepDateDialog.qml"), {
                                            date: page.currentDate,
                                            markedDates: marked
                                        })
                            dlg.accepted.connect(function () {
                                var sel = dlg.selectedDate
                                var now = new Date()
                                if (page.dayStartTs(sel) > page.dayStartTs(now))
                                    sel = now
                                page.currentDate = new Date(sel.getFullYear(),
                                                            sel.getMonth(), sel.getDate())
                                page.reload()
                            })
                        }
                    }
                }
                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "›"
                    color: page.dayStartTs(page.currentDate) < page.dayStartTs(new Date())
                           ? Theme.highlightColor : Theme.rgba(Theme.primaryColor, 0.3)
                    font.pixelSize: Theme.fontSizeLarge
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -Theme.paddingMedium
                        enabled: page.dayStartTs(page.currentDate) < page.dayStartTs(new Date())
                        onClicked: page.shiftDay(1)
                    }
                }
            }

            // --- Недельная лента ---
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Theme.paddingSmall

                Repeater {
                    id: weekRepeater
                    model: 7

                    delegate: Item {
                        width: Theme.dp(56)
                        height: Theme.dp(108)

                        // Понедельник недели текущей даты + index
                        property date cellDate: {
                            var d = new Date(page.currentDate)
                            d.setDate(d.getDate() - ((d.getDay() + 6) % 7) + index)
                            return d
                        }
                        property var cellSummary: page.summaryFor(cellDate)
                        property bool isSelected: page.sameDay(cellDate, page.currentDate)
                        property bool isToday: page.sameDay(cellDate, new Date())

                        Label {
                            id: wdLabel
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: page.weekdayLetters[index]
                            color: Theme.secondaryColor
                            font.pixelSize: Theme.fontSizeExtraSmall
                        }
                        Canvas {
                            id: miniRing
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: wdLabel.height + Theme.paddingSmall
                            width: Theme.dp(40)
                            height: width
                            onPaint: {
                                var ctx = getContext("2d")
                                ctx.clearRect(0, 0, width, height)
                                var lw = Theme.dp(3)
                                var gap = Theme.dp(1.5)
                                var cx = width / 2, cy = height / 2
                                var rOuter = (width - lw) / 2
                                var s = cellSummary
                                // Три мини-кольца: шаги, ккал, активность
                                var rings = [
                                    { ov: (s ? s.steps || 0 : 0) / bluez.stepsGoal,
                                      color: Theme.highlightColor },
                                    { ov: (s ? s.calories || 0 : 0) / bluez.caloriesGoal,
                                      color: page.accentKcal },
                                    { ov: (s ? s.activityMin || 0 : 0) / bluez.activityGoal,
                                      color: page.accentActivity }
                                ]
                                for (var i = 0; i < rings.length; i++) {
                                    var r = rOuter - i * (lw + gap)
                                    if (r <= lw)
                                        break
                                    var col = rings[i].color
                                    ctx.strokeStyle = Theme.rgba(col, 0.15)
                                    ctx.lineWidth = lw
                                    ctx.beginPath()
                                    ctx.arc(cx, cy, r, 0, Math.PI * 2)
                                    ctx.stroke()
                                    var frac = Math.min(1.0, rings[i].ov)
                                    if (frac > 0) {
                                        ctx.strokeStyle = col
                                        ctx.lineCap = "round"
                                        ctx.beginPath()
                                        ctx.arc(cx, cy, r, -Math.PI / 2,
                                                -Math.PI / 2 + frac * Math.PI * 2)
                                        ctx.stroke()
                                    }
                                }
                            }
                            Component.onCompleted: requestPaint()
                        }
                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: miniRing.y + miniRing.height + Theme.paddingSmall
                            text: cellDate.getDate()
                            color: isSelected ? Theme.highlightColor
                                              : (isToday ? Theme.primaryColor : Theme.secondaryColor)
                            font.pixelSize: Theme.fontSizeSmall
                            font.bold: isSelected || isToday
                        }
                        Rectangle {
                            // Подчёркивание выбранного дня
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.bottom: parent.bottom
                            width: Theme.dp(24)
                            height: Theme.dp(3)
                            radius: height / 2
                            color: Theme.highlightColor
                            visible: isSelected
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                page.currentDate = cellDate
                                page.reload()
                            }
                        }
                    }
                }
            }

            // --- Кольца целей ---
            Rectangle {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                height: ringsCol.height + 2 * Theme.paddingLarge
                radius: Theme.dp(20)
                color: page.cardColor

                Column {
                    id: ringsCol
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: Theme.paddingLarge
                    width: parent.width - 2 * Theme.paddingLarge
                    spacing: Theme.paddingMedium

                    Canvas {
                        id: ringsCanvas
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: Math.min(page.width - 8 * Theme.horizontalPageMargin,
                                        Theme.dp(360))
                        height: width

                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.clearRect(0, 0, width, height)
                            var lw = Theme.dp(10)
                            var gap = lw * 1.25
                            var cx = width / 2, cy = height / 2
                            var rOuter = (width - lw) / 2 - lw * 1.1
                            var steps = page.day.steps || 0
                            var kcal = page.day.calories || 0
                            var act = page.day.activityMin || 0

                            // Как на обложке: цвет кольца не меняется; при переполнении
                            // кольцо остаётся полным, а на позиции текущего «круга»
                            // рисуются шевроны ">" по ходу движения — по одному на круг.
                            var rings = [
                                { ov: steps / bluez.stepsGoal,
                                  color: Theme.highlightColor, icon: "shoe" },
                                { ov: kcal / bluez.caloriesGoal,
                                  color: page.accentKcal, icon: "flame" },
                                { ov: act / bluez.activityGoal,
                                  color: page.accentActivity, icon: "clock" }
                            ]

                            ctx.lineCap = "round"
                            for (var i = 0; i < rings.length; i++) {
                                var r = rOuter - i * (lw + gap)
                                var col = rings[i].color
                                var ov = rings[i].ov
                                var laps = Math.floor(ov)
                                var frac = laps >= 1 ? 1.0 : Math.min(1.0, ov)

                                ctx.strokeStyle = Theme.rgba(col, 0.15)
                                ctx.lineWidth = lw
                                ctx.beginPath()
                                ctx.arc(cx, cy, r, 0, Math.PI * 2)
                                ctx.stroke()

                                if (frac > 0) {
                                    ctx.strokeStyle = col
                                    ctx.beginPath()
                                    ctx.arc(cx, cy, r, -Math.PI / 2,
                                            -Math.PI / 2 + frac * Math.PI * 2)
                                    ctx.stroke()
                                }

                                page.drawRingIcon(ctx, rings[i].icon,
                                                  cx, cy - r, lw * 2.1, col)

                                if (laps >= 1)
                                    ProgressMarkers.drawRing(ctx, cx, cy, r, laps, ov - laps, lw)
                            }
                        }
                    }

                    // Легенда: значение / цель
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: Theme.paddingLarge

                        Repeater {
                            model: [
                                { name: qsTr("Шаги"), value: page.day.steps || 0,
                                  goal: bluez.stepsGoal, color: Theme.highlightColor },
                                { name: qsTr("Ккал"), value: page.day.calories || 0,
                                  goal: bluez.caloriesGoal, color: page.accentKcal },
                                { name: qsTr("Активность"), value: page.day.activityMin || 0,
                                  goal: bluez.activityGoal, color: page.accentActivity }
                            ]

                            delegate: Column {
                                spacing: 0
                                Label {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: modelData.name
                                    color: Theme.secondaryColor
                                    font.pixelSize: Theme.fontSizeExtraSmall
                                }
                                Label {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: modelData.value + "/" + modelData.goal
                                          + page.multiplierText(modelData.value, modelData.goal)
                                    color: modelData.color
                                    font.pixelSize: Theme.fontSizeSmall
                                    font.bold: true
                                }
                            }
                        }
                    }
                }
            }

            // --- Метрики дня ---
            Rectangle {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                height: metricsCol.height + 2 * Theme.paddingMedium
                radius: Theme.dp(20)
                color: page.cardColor
                visible: metricsRepeater.count > 0

                Column {
                    id: metricsCol
                    x: Theme.paddingLarge
                    y: Theme.paddingMedium
                    width: parent.width - 2 * x
                    spacing: Theme.paddingSmall

                    Repeater {
                        id: metricsRepeater
                        model: {
                            var rows = []
                            if (page.day.avgHr !== undefined)
                                rows.push({ name: qsTr("Пульс средний"),
                                            value: page.day.avgHr + " " + qsTr("уд/мин"),
                                            color: page.accentHr })
                            if (page.day.minHr !== undefined && page.day.maxHr !== undefined)
                                rows.push({ name: qsTr("Пульс (мин–макс)"),
                                            value: page.day.minHr + "–" + page.day.maxHr,
                                            color: page.accentHr })
                            if (page.day.restingHr !== undefined && page.day.restingHr > 0)
                                rows.push({ name: qsTr("Пульс покоя"),
                                            value: page.day.restingHr + " " + qsTr("уд/мин"),
                                            color: page.accentHr })
                            if (page.day.stressAvg !== undefined && page.day.stressAvg > 0)
                                rows.push({ name: qsTr("Стресс средний"),
                                            value: page.day.stressAvg,
                                            color: page.accentStress })
                            if (page.day.spo2Avg !== undefined && page.day.spo2Avg > 0)
                                rows.push({ name: qsTr("Кислород (SpO2)"),
                                            value: page.day.spo2Avg + " %",
                                            color: page.accentSpo2 })
                            return rows
                        }

                        delegate: Item {
                            width: metricsCol.width
                            height: metricValue.height

                            Row {
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: Theme.paddingSmall
                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: Theme.dp(10)
                                    height: width
                                    radius: width / 2
                                    color: modelData.color
                                }
                                Label {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData.name
                                    color: Theme.secondaryColor
                                    font.pixelSize: Theme.fontSizeSmall
                                }
                            }
                            Label {
                                id: metricValue
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.right: parent.right
                                text: modelData.value
                                color: Theme.primaryColor
                                font.pixelSize: Theme.fontSizeSmall
                                font.bold: true
                            }
                        }
                    }
                }
            }

            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: page.summaries.length === 0
                text: qsTr("Нет данных — синхронизируйте на главном экране")
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeExtraSmall
            }

            Item { width: 1; height: Theme.paddingMedium }
        }
    }
}
