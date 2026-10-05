import QtQuick 2.0
import Sailfish.Silica 1.0
import Aurora.Controls 1.0

// Универсальная страница метрики: Калории / Активность / Пульс / Стресс / SpO2.
// Параметры: metricTitle, accent, unit, field (поле дневной сводки),
// goal (0 — без цели), intraday (true — поминутный график пульса за день,
// false — недельные столбики по полю field).
Page {
    id: page

    property string metricTitle: ""
    property color accent: Theme.highlightColor
    property string unit: ""
    property string field: ""
    property int goal: 0
    property bool intraday: false
    // Почасовой график из поминутных сэмплов: имя поля minute_samples
    // ("active", "actKcal", "stress", "spo2") и режим агрегации
    property string hourlyField: ""
    property string hourlyMode: "sum"   // "sum" | "avg"

    property date currentDate: new Date()
    property var day: ({})
    property var summaries: []
    property var samples: []
    property var hourly: []

    readonly property color cardColor: Theme.rgba(Theme.primaryColor, 0.16)

    readonly property var monthNames: [
        qsTr("января"), qsTr("февраля"), qsTr("марта"), qsTr("апреля"),
        qsTr("мая"), qsTr("июня"), qsTr("июля"), qsTr("августа"),
        qsTr("сентября"), qsTr("октября"), qsTr("ноября"), qsTr("декабря")]
    readonly property var weekdayLetters: [
        qsTr("П"), qsTr("В"), qsTr("С"), qsTr("Ч"), qsTr("П"), qsTr("С"), qsTr("В")]

    function sameDay(a, b) {
        return a.getFullYear() === b.getFullYear() && a.getMonth() === b.getMonth()
                && a.getDate() === b.getDate()
    }

    // Значение поля за день d из сводок; 0/undefined если нет данных
    function summaryFor(d) {
        var ts = dayStartTs(d)
        for (var i = 0; i < summaries.length; i++) {
            if (summaries[i].ts === ts)
                return summaries[i]
        }
        return null
    }

    // Доля заполнения мини-кольца дня: к цели, а без цели — «было/не было»
    function dayFrac(d) {
        var s = summaryFor(d)
        if (!s)
            return 0
        var v = s[field]
        if (v === undefined || v === 0)
            return 0
        if (goal > 0)
            return Math.min(1.0, v / goal)
        return 1.0
    }

    function dayStartTs(d) {
        return new Date(d.getFullYear(), d.getMonth(), d.getDate()).getTime() / 1000
    }

    function fmtLongDate(d) {
        return d.getDate() + " " + monthNames[d.getMonth()] + " " + d.getFullYear() + " г."
    }

    function fmtShort(d) {
        return ("0" + d.getDate()).slice(-2) + "." + ("0" + (d.getMonth() + 1)).slice(-2)
    }

    function multiplierText(value, goalValue) {
        if (!goalValue || value < goalValue)
            return ""
        return " ×" + (Math.round(value * 10.0 / goalValue) / 10)
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

    // Семь дней [currentDate-6 .. currentDate] со значением поля (0 если нет)
    function weekBars() {
        var out = []
        var byTs = {}
        for (var i = 0; i < summaries.length; i++)
            byTs[summaries[i].ts] = summaries[i]
        for (var k = 6; k >= 0; k--) {
            var d = new Date(currentDate)
            d.setDate(d.getDate() - k)
            var s = byTs[dayStartTs(d)]
            out.push({ date: d, value: s && s[field] !== undefined ? s[field] : 0,
                       has: !!s })
        }
        return out
    }

    function reload() {
        summaries = storage.dailySummaries(90)
        var t0 = dayStartTs(currentDate)
        day = storage.daySummary(t0)
        if (intraday || hourlyField !== "") {
            var raw = storage.minuteSamples(t0, t0 + 86399)
            if (intraday) {
                var hr = []
                for (var i = 0; i < raw.length; i++) {
                    if (raw[i].hr > 0)
                        hr.push({ ts: raw[i].ts, hr: raw[i].hr })
                }
                samples = hr
            }
            if (hourlyField !== "") {
                var sum = [], cnt = []
                for (i = 0; i < 24; i++) {
                    sum.push(0)
                    cnt.push(0)
                }
                for (i = 0; i < raw.length; i++) {
                    var v = raw[i][hourlyField]
                    if (v === undefined || v === 0)
                        continue
                    var hh = new Date(raw[i].ts * 1000).getHours()
                    sum[hh] += v
                    cnt[hh]++
                }
                var h = []
                for (i = 0; i < 24; i++)
                    h.push(hourlyMode === "avg" ? (cnt[i] > 0 ? Math.round(sum[i] / cnt[i]) : 0)
                                                : sum[i])
                hourly = h
            }
        }
        weekCanvas.requestPaint()
        dayCanvas.requestPaint()
        goalBarChev.requestPaint()
        hourlyCanvas.requestPaint()
    }

    Component.onCompleted: reload()

    Connections {
        target: storage
        onDataChanged: page.reload()
    }

    AppBar {
        id: appBar
        headerText: page.metricTitle
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

            // --- Выбор дня: ‹ дата ›, тап по дате — календарь ---
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

            // --- Недельная лента (как в Статистике/Сне) ---
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
                        property real cellFrac: page.dayFrac(cellDate)
                        property bool isSelected: page.sameDay(cellDate, page.currentDate)
                        property bool isToday: page.sameDay(cellDate, new Date())

                        onCellFracChanged: miniRing.requestPaint()

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
                                var lw = Theme.dp(4)
                                var r = (width - lw) / 2
                                ctx.strokeStyle = Theme.rgba(page.accent, 0.15)
                                ctx.lineWidth = lw
                                ctx.beginPath()
                                ctx.arc(width / 2, height / 2, r, 0, Math.PI * 2)
                                ctx.stroke()
                                if (cellFrac > 0) {
                                    ctx.strokeStyle = page.accent
                                    ctx.lineCap = "round"
                                    ctx.beginPath()
                                    ctx.arc(width / 2, height / 2, r, -Math.PI / 2,
                                            -Math.PI / 2 + cellFrac * Math.PI * 2)
                                    ctx.stroke()
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

            // --- Значение за день ---
            Rectangle {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                height: valueCol.height + 2 * Theme.paddingLarge
                radius: Theme.dp(20)
                color: page.cardColor

                Column {
                    id: valueCol
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: Theme.paddingLarge
                    spacing: Theme.paddingSmall

                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: {
                            var v = page.day[page.field]
                            if (v === undefined || v === 0)
                                return "—"
                            return v + (page.unit ? " " + page.unit : "")
                        }
                        color: page.accent
                        font.pixelSize: Theme.fontSizeHuge
                        font.bold: true
                    }
                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: page.goal > 0
                        text: qsTr("из") + " " + page.goal + " " + page.unit
                              + page.multiplierText(page.day[page.field] || 0, page.goal)
                        color: Theme.secondaryColor
                        font.pixelSize: Theme.fontSizeSmall
                    }
                    // Прогресс к цели: полная полоска + чёрные шевроны
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: page.goal > 0
                        width: page.width - 6 * Theme.horizontalPageMargin
                        height: Math.round(Theme.paddingSmall * 1.5)
                        radius: height / 2
                        color: Theme.rgba(page.accent, 0.2)

                        property var lap: {
                            var v = page.day[page.field] || 0
                            if (page.goal <= 0)
                                return { fill: 0.0, laps: 0, over: 0.0 }
                            var ov = v / page.goal
                            var laps = Math.floor(ov)
                            if (laps < 1)
                                return { fill: ov, laps: 0, over: 0.0 }
                            return { fill: 1.0, laps: laps, over: ov - laps }
                        }

                        Rectangle {
                            width: parent.width * parent.lap.fill
                            height: parent.height
                            radius: parent.radius
                            color: page.accent
                        }
                        Canvas {
                            id: goalBarChev
                            anchors.fill: parent
                            onPaint: {
                                var ctx = getContext("2d")
                                ctx.clearRect(0, 0, width, height)
                                var lap = parent.lap
                                if (lap.laps < 1)
                                    return
                                var cx = lap.over * width
                                var cy = height / 2
                                var s = height * 0.35
                                ctx.strokeStyle = "black"
                                ctx.lineWidth = Math.max(1.5, height * 0.18)
                                ctx.lineCap = "round"
                                for (var k = 0; k < lap.laps; k++) {
                                    var bx = cx - k * s * 1.6
                                    ctx.beginPath()
                                    ctx.moveTo(bx - s, cy - s)
                                    ctx.lineTo(bx + s, cy)
                                    ctx.lineTo(bx - s, cy + s)
                                    ctx.stroke()
                                }
                            }
                            Component.onCompleted: requestPaint()
                        }
                    }
                }
            }

            // --- График: пульс за день (intraday) ---
            Rectangle {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                height: dayChartCol.height + 2 * Theme.paddingMedium
                radius: Theme.dp(20)
                color: page.cardColor
                visible: page.intraday

                Column {
                    id: dayChartCol
                    x: Theme.paddingLarge
                    y: Theme.paddingMedium
                    width: parent.width - 2 * x
                    spacing: Theme.paddingSmall

                    Label {
                        text: qsTr("Пульс за день")
                        color: Theme.primaryColor
                        font.pixelSize: Theme.fontSizeSmall
                    }

                    Canvas {
                        id: dayCanvas
                        width: parent.width
                        height: Theme.dp(220)

                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.clearRect(0, 0, width, height)
                            var labelH = Theme.fontSizeExtraSmall
                            var chartH = height - labelH - Theme.paddingSmall
                            var t0 = page.dayStartTs(page.currentDate)

                            var pts = page.samples
                            var havePts = pts && pts.length >= 2
                            var lo = 30, hi = 100
                            if (havePts) {
                                lo = 1000; hi = 0
                                for (var i = 0; i < pts.length; i++) {
                                    if (pts[i].hr < lo) lo = pts[i].hr
                                    if (pts[i].hr > hi) hi = pts[i].hr
                                }
                                lo = Math.max(30, lo - 5)
                                hi += 5
                            }

                            // Ось Y: мин / середина / макс слева
                            var labelW = Theme.fontSizeExtraSmall * 2.2
                            ctx.fillStyle = Theme.secondaryColor
                            ctx.font = Theme.fontSizeExtraSmall + "px sans-serif"
                            ctx.textAlign = "left"
                            ctx.textBaseline = "middle"
                            ctx.fillText(hi, 0, Theme.fontSizeExtraSmall / 2)
                            ctx.fillText(Math.round((hi + lo) / 2), 0, chartH / 2)
                            ctx.fillText(lo, 0, chartH - Theme.fontSizeExtraSmall / 2)
                            ctx.textBaseline = "alphabetic"

                            // Ось часов: 0, 6, 12, 18
                            ctx.textAlign = "center"
                            var marks = [0, 6, 12, 18]
                            for (i = 0; i < marks.length; i++)
                                ctx.fillText(marks[i], labelW + marks[i] * (width - labelW) / 24
                                             + (width - labelW) / 48, height)

                            if (!havePts)
                                return

                            ctx.strokeStyle = page.accent
                            ctx.lineWidth = Theme.dp(2)
                            ctx.beginPath()
                            var started = false
                            for (i = 0; i < pts.length; i++) {
                                var x = labelW + (width - labelW) * (pts[i].ts - t0) / 86400
                                var y = chartH * (1 - (pts[i].hr - lo) / (hi - lo))
                                if (!started) {
                                    ctx.moveTo(x, y)
                                    started = true
                                } else {
                                    // разрыв, если между сэмплами больше 15 минут
                                    if (pts[i].ts - pts[i - 1].ts > 900)
                                        ctx.moveTo(x, y)
                                    else
                                        ctx.lineTo(x, y)
                                }
                            }
                            ctx.stroke()
                        }
                    }

                    Row {
                        width: parent.width
                        Label {
                            text: qsTr("мин") + " " + (page.day.minHr !== undefined ? page.day.minHr : "—")
                                  + "  ·  " + qsTr("макс") + " "
                                  + (page.day.maxHr !== undefined ? page.day.maxHr : "—")
                                  + "  ·  " + qsTr("покой") + " "
                                  + (page.day.restingHr !== undefined ? page.day.restingHr : "—")
                            color: Theme.secondaryColor
                            font.pixelSize: Theme.fontSizeExtraSmall
                        }
                    }
                }
            }

            // --- График: неделя столбиками (не intraday) ---
            Rectangle {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                height: weekChartCol.height + 2 * Theme.paddingMedium
                radius: Theme.dp(20)
                color: page.cardColor
                visible: !page.intraday

                Column {
                    id: weekChartCol
                    x: Theme.paddingLarge
                    y: Theme.paddingMedium
                    width: parent.width - 2 * x
                    spacing: Theme.paddingSmall

                    Label {
                        text: qsTr("За 7 дней")
                        color: Theme.primaryColor
                        font.pixelSize: Theme.fontSizeSmall
                    }

                    Canvas {
                        id: weekCanvas
                        width: parent.width
                        height: Theme.dp(180)

                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.clearRect(0, 0, width, height)
                            var bars = page.weekBars()
                            var max = page.goal
                            for (var i = 0; i < bars.length; i++)
                                if (bars[i].value > max)
                                    max = bars[i].value
                            if (max <= 0)
                                max = 1
                            var labelH = Theme.fontSizeExtraSmall
                            var chartH = height - labelH - Theme.paddingSmall
                            var labelW = Theme.fontSizeExtraSmall * 2.2
                            var chartW = width - labelW
                            var bw = chartW / 7
                            var barW = Math.max(2, bw * 0.55)

                            // Ось Y: максимум и середина шкалы
                            ctx.fillStyle = Theme.secondaryColor
                            ctx.font = Theme.fontSizeExtraSmall + "px sans-serif"
                            ctx.textAlign = "left"
                            ctx.textBaseline = "middle"
                            ctx.fillText(max, 0, Theme.fontSizeExtraSmall / 2)
                            ctx.fillText(Math.round(max / 2), 0, chartH / 2)
                            ctx.textBaseline = "alphabetic"

                            // Линия цели
                            if (page.goal > 0) {
                                var gy = chartH * (1 - page.goal / max)
                                ctx.strokeStyle = Theme.rgba(page.accent, 0.5)
                                ctx.lineWidth = 1
                                ctx.beginPath()
                                ctx.moveTo(labelW, gy)
                                ctx.lineTo(width, gy)
                                ctx.stroke()
                            }

                            for (i = 0; i < bars.length; i++) {
                                var b = bars[i]
                                var isSel = (i === bars.length - 1)
                                if (b.value > 0) {
                                    var bh = Math.max(2, chartH * b.value / max)
                                    ctx.fillStyle = isSel
                                            ? page.accent
                                            : Theme.rgba(page.accent, 0.45)
                                    ctx.fillRect(labelW + i * bw + (bw - barW) / 2, chartH - bh,
                                                 barW, bh)
                                }
                                ctx.fillStyle = isSel ? Theme.highlightColor
                                                      : Theme.secondaryColor
                                ctx.font = Theme.fontSizeExtraSmall + "px sans-serif"
                                ctx.textAlign = "center"
                                ctx.fillText(b.date.getDate(), labelW + i * bw + bw / 2, height)
                            }
                        }
                    }
                }
            }

            // --- По часам (из поминутных сэмплов) ---
            Rectangle {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                height: hourlyCol.height + 2 * Theme.paddingMedium
                radius: Theme.dp(20)
                color: page.cardColor
                visible: page.hourlyField !== ""

                Column {
                    id: hourlyCol
                    x: Theme.paddingLarge
                    y: Theme.paddingMedium
                    width: parent.width - 2 * x
                    spacing: Theme.paddingSmall

                    Label {
                        text: qsTr("По часам")
                              + (page.hourlyField === "actKcal"
                                 ? " · " + qsTr("активные") : "")
                              + (page.hourlyMode === "avg"
                                 ? " · " + qsTr("среднее") : "")
                        color: Theme.primaryColor
                        font.pixelSize: Theme.fontSizeSmall
                    }

                    Canvas {
                        id: hourlyCanvas
                        width: parent.width
                        height: Theme.dp(140)

                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.clearRect(0, 0, width, height)
                            var h = page.hourly
                            if (!h || h.length !== 24)
                                return
                            var max = 0
                            for (var i = 0; i < 24; i++)
                                if (h[i] > max)
                                    max = h[i]
                            var labelH = Theme.fontSizeExtraSmall
                            var chartH = height - labelH - Theme.paddingSmall
                            var labelW = Theme.fontSizeExtraSmall * 2.2
                            var chartW = width - labelW
                            var bw = chartW / 24
                            var barW = Math.max(2, bw * 0.6)

                            // Ось Y: максимум и середина шкалы
                            if (max > 0) {
                                ctx.fillStyle = Theme.secondaryColor
                                ctx.font = Theme.fontSizeExtraSmall + "px sans-serif"
                                ctx.textAlign = "left"
                                ctx.textBaseline = "middle"
                                ctx.fillText(max, 0, Theme.fontSizeExtraSmall / 2)
                                ctx.fillText(Math.round(max / 2), 0, chartH / 2)
                                ctx.textBaseline = "alphabetic"
                            }

                            // Подписи часов: 0, 6, 12, 18
                            ctx.fillStyle = Theme.secondaryColor
                            ctx.font = Theme.fontSizeExtraSmall + "px sans-serif"
                            ctx.textAlign = "center"
                            var marks = [0, 6, 12, 18]
                            for (i = 0; i < marks.length; i++)
                                ctx.fillText(marks[i], labelW + marks[i] * bw + bw / 2, height)

                            if (max <= 0)
                                return
                            for (i = 0; i < 24; i++) {
                                if (h[i] <= 0)
                                    continue
                                var bh = Math.max(2, chartH * h[i] / max)
                                ctx.fillStyle = page.accent
                                ctx.fillRect(labelW + i * bw + (bw - barW) / 2, chartH - bh,
                                             barW, bh)
                            }
                        }
                    }
                }
            }

            Item { width: 1; height: Theme.paddingMedium }
        }
    }
}
