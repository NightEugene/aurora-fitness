import QtQuick 2.0
import Sailfish.Silica 1.0
import Aurora.Controls 1.0

Page {
    id: page

    property var sessions: []
    property int selected: 0
    property var current: null
    property var stages: []
    property int tappedSeg: -1

    readonly property color cardColor: Theme.rgba(Theme.primaryColor, 0.16)
    readonly property color accentSleep: "#7c4dff"
    readonly property color phaseDeep: "#3f51b5"
    readonly property color phaseLight: "#7c9bff"
    readonly property color phaseRem: "#b388ff"
    readonly property color phaseAwake: "#ff8a65"

    // Сессии без фаз (например, дневная дрёмота) не показываем
    function hasPhases(s) {
        return (s.deepMin || 0) + (s.lightMin || 0) + (s.remMin || 0)
                + (s.awakeMin || 0) > 0
    }

    function reload() {
        var all = storage.sleepSessions(14)
        var ok = []
        for (var i = 0; i < all.length; i++) {
            if (hasPhases(all[i]))
                ok.push(all[i])
        }
        sessions = ok
        selectSession(Math.min(selected, Math.max(ok.length - 1, 0)))
    }

    function selectSession(i) {
        if (sessions.length === 0) {
            current = null
            stages = []
            tappedSeg = -1
            return
        }
        selected = i
        current = sessions[i]
        stages = storage.sleepStages(current.bedTime)
        tappedSeg = -1
        hypnoCanvas.requestPaint()
        phaseBar.requestPaint()
    }

    // Ночь датируется днём ПРОБУЖДЕНИЯ (как в Mi Fitness): тогда ночь
    // «лёг в 02:20» и «лёг в 23:27» не слипаются в одну дату отбоя.
    function nightTs(s) {
        return (s.wakeTime > 0) ? s.wakeTime : s.bedTime
    }

    function fmtHM(min) {
        return Math.floor(min / 60) + qsTr("ч") + " " + ("0" + (min % 60)).slice(-2) + qsTr("м")
    }

    readonly property var monthNames: [
        qsTr("января"), qsTr("февраля"), qsTr("марта"), qsTr("апреля"),
        qsTr("мая"), qsTr("июня"), qsTr("июля"), qsTr("августа"),
        qsTr("сентября"), qsTr("октября"), qsTr("ноября"), qsTr("декабря")]
    readonly property var weekdayLetters: [
        qsTr("П"), qsTr("В"), qsTr("С"), qsTr("Ч"), qsTr("П"), qsTr("С"), qsTr("В")]

    function fmtLongDate(d) {
        return d.getDate() + " " + monthNames[d.getMonth()] + " " + d.getFullYear() + " г."
    }

    function sameDay(a, b) {
        return a.getFullYear() === b.getFullYear() && a.getMonth() === b.getMonth()
                && a.getDate() === b.getDate()
    }

    // Сессия ночи, датированной днём пробуждения d
    function sessionForDate(d) {
        for (var i = 0; i < sessions.length; i++) {
            if (sameDay(new Date(nightTs(sessions[i]) * 1000), d))
                return i
        }
        return -1
    }

    function fmtDate(ts) {
        var d = new Date(ts * 1000)
        return ("0" + d.getDate()).slice(-2) + "." + ("0" + (d.getMonth() + 1)).slice(-2)
    }

    function fmtTime(ts) {
        var d = new Date(ts * 1000)
        return ("0" + d.getHours()).slice(-2) + ":" + ("0" + d.getMinutes()).slice(-2)
    }

    function phasesOf(s) {
        return [
            { name: qsTr("Глубокий"), color: phaseDeep, min: s.deepMin || 0 },
            { name: qsTr("Лёгкий"), color: phaseLight, min: s.lightMin || 0 },
            { name: "REM", color: phaseRem, min: s.remMin || 0 },
            { name: qsTr("Бодрствование"), color: phaseAwake, min: s.awakeMin || 0 }
        ]
    }

    function colorOfStage(stage) {
        switch (stage) {
        case "deep":  return phaseDeep
        case "light": return phaseLight
        case "rem":   return phaseRem
        case "awake": return phaseAwake
        }
        return "transparent"
    }

    function nameOfStage(stage) {
        switch (stage) {
        case "deep":  return qsTr("Глубокий сон")
        case "light": return qsTr("Лёгкий сон")
        case "rem":   return "REM"
        case "awake": return qsTr("Бодрствование")
        }
        return stage
    }

    // Границы шкалы гипнограммы [t0, t1] для текущей сессии
    function hypnoRange() {
        var s = current
        if (!s)
            return [0, 0]
        var t0 = s.bedTime
        var t1 = s.wakeTime > s.bedTime ? s.wakeTime
                                        : (stages.length > 0
                                           ? stages[stages.length - 1].ts + 3600 : t0 + 1)
        return [t0, Math.max(t1, t0 + 1)]
    }

    // Номер сегмента по координате x на гипнограмме, -1 если вне
    function hypnoSegmentAt(x, width) {
        var st = stages
        if (st.length < 2 || width <= 0)
            return -1
        var range = hypnoRange()
        var t = range[0] + (range[1] - range[0]) * x / width
        for (var i = 0; i < st.length; i++) {
            var a = st[i].ts
            var b = (i + 1 < st.length) ? st[i + 1].ts : range[1]
            if (t >= a && t < b)
                return i
        }
        return t >= range[1] ? st.length - 1 : -1
    }

    Component.onCompleted: reload()

    Connections {
        target: storage
        onDataChanged: page.reload()
    }

    AppBar {
        id: appBar
        headerText: qsTr("Сон")
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

            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: page.sessions.length === 0
                text: qsTr("Нет данных о сне")
                color: Theme.secondaryColor
            }

            // --- Выбранная ночь ---
            Rectangle {
                id: detailsCard
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                height: lastCol.height + 2 * Theme.paddingMedium
                radius: Theme.dp(20)
                color: page.cardColor
                visible: page.sessions.length > 0

                Column {
                    id: lastCol
                    x: Theme.paddingLarge
                    y: Theme.paddingMedium
                    width: parent.width - 2 * x
                    spacing: Theme.paddingSmall

                    // Выбор ночи: ‹ дата ›
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: Theme.paddingLarge

                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "‹"
                            color: page.selected < page.sessions.length - 1
                                   ? Theme.highlightColor : Theme.rgba(Theme.primaryColor, 0.3)
                            font.pixelSize: Theme.fontSizeLarge
                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -Theme.paddingMedium
                                enabled: page.selected < page.sessions.length - 1
                                onClicked: page.selectSession(page.selected + 1)
                            }
                        }
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: page.current
                                  ? page.fmtLongDate(new Date(page.nightTs(page.current) * 1000))
                                  : ""
                            color: Theme.primaryColor
                            font.pixelSize: Theme.fontSizeMedium
                            font.bold: true

                            // Тап по дате — календарь с выбором даты
                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -Theme.paddingSmall
                                onClicked: {
                                    if (!page.current)
                                        return
                                    var marked = []
                                    for (var i = 0; i < page.sessions.length; i++) {
                                        var d = new Date(page.nightTs(page.sessions[i]) * 1000)
                                        marked.push(d.getFullYear() + "-"
                                                    + ("0" + (d.getMonth() + 1)).slice(-2) + "-"
                                                    + ("0" + d.getDate()).slice(-2))
                                    }
                                    var dlg = pageStack.push(
                                                Qt.resolvedUrl("SleepDateDialog.qml"), {
                                                    date: new Date(page.nightTs(page.current) * 1000),
                                                    markedDates: marked
                                                })
                                    dlg.accepted.connect(function () {
                                        var want = ("0" + dlg.selectedDate.getDate()).slice(-2) + "."
                                                + ("0" + (dlg.selectedDate.getMonth() + 1)).slice(-2)
                                        // точное совпадение по дате пробуждения, иначе ближайшая
                                        var best = -1
                                        var bestDiff = -1
                                        for (var i = 0; i < page.sessions.length; i++) {
                                            var diff = Math.abs(page.nightTs(page.sessions[i])
                                                                - dlg.selectedDate.getTime() / 1000)
                                            if (page.fmtDate(page.nightTs(page.sessions[i])) === want) {
                                                best = i
                                                break
                                            }
                                            if (bestDiff < 0 || diff < bestDiff) {
                                                bestDiff = diff
                                                best = i
                                            }
                                        }
                                        if (best >= 0)
                                            page.selectSession(best)
                                    })
                                }
                            }
                        }
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "›"
                            color: page.selected > 0
                                   ? Theme.highlightColor : Theme.rgba(Theme.primaryColor, 0.3)
                            font.pixelSize: Theme.fontSizeLarge
                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -Theme.paddingMedium
                                enabled: page.selected > 0
                                onClicked: page.selectSession(page.selected - 1)
                            }
                        }
                    }

                    // --- Недельная лента (как в Статистике): мини-кольцо —
                    // длительность сна относительно 8 ч, тап выбирает ночь ---
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: Theme.paddingSmall

                        Repeater {
                            model: 7

                            delegate: Item {
                                width: Theme.dp(56)
                                height: Theme.dp(108)

                                // Понедельник недели текущей ночи + index
                                property date cellDate: {
                                    var base = page.current
                                            ? new Date(page.nightTs(page.current) * 1000)
                                            : new Date()
                                    base.setDate(base.getDate() - ((base.getDay() + 6) % 7) + index)
                                    return base
                                }
                                property int cellSession: page.sessionForDate(cellDate)
                                property bool isSelected: page.current
                                        && page.sameDay(cellDate,
                                                        new Date(page.nightTs(page.current) * 1000))
                                property bool isToday: page.sameDay(cellDate, new Date())

                                onCellSessionChanged: miniRing.requestPaint()

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
                                        var frac = cellSession >= 0
                                                ? Math.min(1.0, (page.sessions[cellSession].sleepMin || 0) / 480.0)
                                                : 0
                                        ctx.strokeStyle = Theme.rgba(page.accentSleep, 0.15)
                                        ctx.lineWidth = lw
                                        ctx.beginPath()
                                        ctx.arc(width / 2, height / 2, r, 0, Math.PI * 2)
                                        ctx.stroke()
                                        if (frac > 0) {
                                            ctx.strokeStyle = page.accentSleep
                                            ctx.lineCap = "round"
                                            ctx.beginPath()
                                            ctx.arc(width / 2, height / 2, r, -Math.PI / 2,
                                                    -Math.PI / 2 + frac * Math.PI * 2)
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
                                    enabled: cellSession >= 0
                                    onClicked: page.selectSession(cellSession)
                                }
                            }
                        }
                    }

                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: page.current ? page.fmtHM(page.current.sleepMin) : "—"
                        color: page.accentSleep
                        font.pixelSize: Theme.fontSizeHuge
                        font.bold: true
                    }

                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        color: Theme.primaryColor
                        font.pixelSize: Theme.fontSizeSmall
                        text: page.current
                              ? qsTr("Начало сна") + " " + page.fmtTime(page.current.bedTime)
                                + "  ·  " + qsTr("Пробуждение") + " "
                                + (page.current.wakeTime > 0
                                   ? page.fmtTime(page.current.wakeTime) : "—")
                              : ""
                    }

                    // Гипнограмма: ступенчатый график фаз во времени
                    // (бодрствование сверху, глубокий сон снизу)
                    Canvas {
                        id: hypnoCanvas
                        width: parent.width
                        height: Theme.dp(220)
                        visible: page.stages.length > 1

                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.clearRect(0, 0, width, height)
                            var s = page.current
                            var st = page.stages
                            if (!s || st.length < 2)
                                return
                            var range = page.hypnoRange()
                            var t0 = range[0]
                            var t1 = range[1]

                            var rows = 4 // awake, rem, light, deep — сверху вниз
                            var rowH = height / rows
                            function rowOf(stage) {
                                switch (stage) {
                                case "awake": return 0
                                case "rem":   return 1
                                case "light": return 2
                                case "deep":  return 3
                                }
                                return -1
                            }

                            // тонкая сетка между уровнями (setLineDash в Qt 5.6
                            // не поддерживается — только сплошные линии)
                            ctx.strokeStyle = Theme.rgba(Theme.secondaryColor, 0.35)
                            ctx.lineWidth = 1
                            for (var g = 1; g < rows; g++) {
                                ctx.beginPath()
                                ctx.moveTo(0, g * rowH + 0.5)
                                ctx.lineTo(width, g * rowH + 0.5)
                                ctx.stroke()
                            }

                            for (var i = 0; i < st.length; i++) {
                                var row = rowOf(st[i].stage)
                                if (row < 0)
                                    continue
                                var a = Math.max(st[i].ts, t0)
                                var b = (i + 1 < st.length) ? st[i + 1].ts : t1
                                if (b > t1)
                                    b = t1
                                if (b <= a)
                                    continue
                                var x0 = width * (a - t0) / (t1 - t0)
                                var x1 = width * (b - t0) / (t1 - t0)
                                ctx.fillStyle = page.colorOfStage(st[i].stage)
                                ctx.globalAlpha = (page.tappedSeg < 0 || page.tappedSeg === i)
                                        ? 1.0 : 0.35
                                ctx.fillRect(x0, row * rowH + 1,
                                             Math.max(x1 - x0, 1.5), rowH - 2)
                                if (page.tappedSeg === i) {
                                    ctx.globalAlpha = 1.0
                                    ctx.strokeStyle = "white"
                                    ctx.lineWidth = 2
                                    ctx.strokeRect(x0 + 1, row * rowH + 2,
                                                   Math.max(x1 - x0, 1.5) - 2, rowH - 4)
                                }
                            }
                            ctx.globalAlpha = 1.0
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                var idx = page.hypnoSegmentAt(mouse.x, width)
                                page.tappedSeg = (idx === page.tappedSeg) ? -1 : idx
                                parent.requestPaint()
                            }
                        }
                    }

                    // Подробности фазы по тапу
                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: page.tappedSeg >= 0 && page.tappedSeg < page.stages.length
                        color: Theme.primaryColor
                        font.pixelSize: Theme.fontSizeSmall
                        text: {
                            if (page.tappedSeg < 0 || page.tappedSeg >= page.stages.length)
                                return ""
                            var st = page.stages
                            var i = page.tappedSeg
                            var range = page.hypnoRange()
                            var a = st[i].ts
                            var b = (i + 1 < st.length) ? st[i + 1].ts : range[1]
                            return page.nameOfStage(st[i].stage) + "  ·  "
                                    + page.fmtTime(a) + "–" + page.fmtTime(b)
                                    + "  ·  " + page.fmtHM(Math.round((b - a) / 60))
                        }
                    }

                    // Подписи времени под гипнограммой
                    Row {
                        width: parent.width
                        visible: page.stages.length > 1
                        Label {
                            width: parent.width / 2
                            text: page.current ? page.fmtTime(page.current.bedTime) : ""
                            color: Theme.secondaryColor
                            font.pixelSize: Theme.fontSizeExtraSmall
                        }
                        Label {
                            width: parent.width / 2
                            horizontalAlignment: Text.AlignRight
                            text: page.current && page.current.wakeTime > 0
                                  ? page.fmtTime(page.current.wakeTime) : ""
                            color: Theme.secondaryColor
                            font.pixelSize: Theme.fontSizeExtraSmall
                        }
                    }

                    // Полоса фаз — для старых записей без пофазовой шкалы
                    Canvas {
                        id: phaseBar
                        width: parent.width
                        height: Theme.dp(32)
                        visible: page.stages.length < 2

                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.clearRect(0, 0, width, height)
                            if (!page.current)
                                return
                            var phases = page.phasesOf(page.current)
                            var total = 0
                            for (var i = 0; i < phases.length; i++)
                                total += phases[i].min
                            if (total <= 0)
                                return

                            var r = height / 2
                            ctx.save()
                            ctx.beginPath()
                            ctx.moveTo(r, 0)
                            ctx.lineTo(width - r, 0)
                            ctx.arc(width - r, r, r, -Math.PI / 2, Math.PI / 2)
                            ctx.lineTo(r, height)
                            ctx.arc(r, r, r, Math.PI / 2, Math.PI * 1.5)
                            ctx.closePath()
                            ctx.clip()

                            var x = 0
                            for (i = 0; i < phases.length; i++) {
                                var w = width * phases[i].min / total
                                if (w <= 0)
                                    continue
                                ctx.fillStyle = phases[i].color
                                ctx.fillRect(x, 0, w, height)
                                x += w
                            }
                            ctx.restore()
                        }
                    }

                    Repeater {
                        model: page.current ? page.phasesOf(page.current) : []

                        Row {
                            width: lastCol.width
                            spacing: Theme.paddingSmall

                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: Theme.dp(14); height: width; radius: width / 2
                                color: modelData.color
                            }
                            Label {
                                width: parent.width - Theme.dp(14) - 2 * Theme.paddingSmall - valueLbl.width
                                text: modelData.name
                                color: Theme.primaryColor
                                font.pixelSize: Theme.fontSizeSmall
                                truncationMode: TruncationMode.Fade
                            }
                            Label {
                                id: valueLbl
                                anchors.verticalCenter: parent.verticalCenter
                                text: {
                                    var total = 0
                                    var phases = page.phasesOf(page.current)
                                    for (var i = 0; i < phases.length; i++)
                                        total += phases[i].min
                                    var pct = total > 0
                                            ? Math.round(100 * modelData.min / total) : 0
                                    return page.fmtHM(modelData.min) + " · " + pct + "%"
                                }
                                color: Theme.secondaryColor
                                font.pixelSize: Theme.fontSizeSmall
                            }
                        }
                    }
                }

                // Тап вне гипнограммы сбрасывает выделение фазы;
                // остальные клики пропускаем вниз (гипнограмма, стрелки, дата)
                MouseArea {
                    anchors.fill: parent
                    propagateComposedEvents: true
                    onClicked: {
                        if (page.tappedSeg < 0 || !hypnoCanvas.visible) {
                            mouse.accepted = false
                            return
                        }
                        var p = hypnoCanvas.mapToItem(this, 0, 0)
                        if (mouse.x >= p.x && mouse.x <= p.x + hypnoCanvas.width
                                && mouse.y >= p.y && mouse.y <= p.y + hypnoCanvas.height) {
                            mouse.accepted = false
                            return
                        }
                        page.tappedSeg = -1
                        hypnoCanvas.requestPaint()
                    }
                }
            }

            // --- Предыдущие ночи (тап = выбрать) ---
            Rectangle {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                height: histCol.height + 2 * Theme.paddingMedium
                radius: Theme.dp(20)
                color: page.cardColor
                visible: page.sessions.length > 1

                Column {
                    id: histCol
                    x: Theme.paddingLarge
                    y: Theme.paddingMedium
                    width: parent.width - 2 * x
                    spacing: Theme.paddingSmall

                    Label {
                        text: qsTr("Предыдущие ночи")
                        color: Theme.primaryColor
                        font.pixelSize: Theme.fontSizeSmall
                    }

                    Repeater {
                        model: page.sessions.length > 1 ? page.sessions.slice(1) : []

                        Rectangle {
                            width: histCol.width
                            height: histRow.height + Theme.paddingSmall
                            radius: Theme.dp(10)
                            color: (page.selected === index + 1)
                                   ? Theme.rgba(Theme.highlightColor, 0.18) : "transparent"

                            Row {
                                id: histRow
                                x: Theme.paddingSmall
                                width: parent.width - 2 * x
                                anchors.verticalCenter: parent.verticalCenter

                                Label {
                                    width: parent.width - durLbl.width
                                    text: page.fmtDate(page.nightTs(modelData))
                                          + "  ·  " + page.fmtTime(modelData.bedTime)
                                          + "–" + (modelData.wakeTime > 0
                                                   ? page.fmtTime(modelData.wakeTime) : "—")
                                    color: Theme.secondaryColor
                                    font.pixelSize: Theme.fontSizeExtraSmall
                                }
                                Label {
                                    id: durLbl
                                    text: page.fmtHM(modelData.sleepMin)
                                    color: page.accentSleep
                                    font.pixelSize: Theme.fontSizeExtraSmall
                                    font.bold: true
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: page.selectSession(index + 1)
                            }
                        }
                    }
                }
            }

            Item { width: 1; height: Theme.paddingMedium }
        }

        VerticalScrollDecorator {}
    }
}
