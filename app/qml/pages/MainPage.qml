import QtQuick 2.0
import Sailfish.Silica 1.0
import Aurora.Controls 1.0
import "../ProgressMarkers.js" as ProgressMarkers

Page {
    id: page

    property var today: ({})
    property var sleep: []
    property var hourly: []
    property string lastSync: ""

    readonly property color cardColor: Theme.rgba(Theme.primaryColor, 0.16)
    readonly property color accentKcal: "#ff9800"
    readonly property color accentActivity: "#8bc34a"
    readonly property color accentHr: "#ff5252"
    readonly property color accentSleep: "#7c4dff"
    readonly property color accentSpo2: "#03a9f4"
    readonly property color accentStress: "#26a69a"
    readonly property color accentBattery: "#4caf50"
    readonly property color goalDone: "#4caf50"
    // Яркие варианты — при превышении цели
    readonly property color accentKcalBright: "#ffca28"
    readonly property color accentActivityBright: "#c6ff00"
    readonly property color goalBright: "#ffd740"

    // " ×1.5" при превышении цели, иначе ""
    function multiplierText(value, goal) {
        if (!goal || value < goal)
            return ""
        return " ×" + (Math.round(value * 10.0 / goal) / 10)
    }

    // Векторные иконки для колец: ботинок (шаги), огонёк (ккал), часы (активность)
    function drawRingIcon(ctx, icon, x, y, size, color) {
        var s = size / 2
        ctx.save()
        ctx.translate(x, y)
        // Цветной диск-подложка, глиф — выемкой тёмным
        ctx.fillStyle = color
        ctx.beginPath()
        ctx.arc(0, 0, s, 0, Math.PI * 2)
        ctx.fill()
        var gs = s * 0.62 // глиф чуть меньше диска
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
            // подушечка
            ctx.save()
            ctx.translate(0, -0.3 * gs)
            ctx.scale(1, 1.3)
            ctx.beginPath()
            ctx.arc(0, 0, 0.42 * gs, 0, Math.PI * 2)
            ctx.fill()
            ctx.restore()
            // пятка
            ctx.beginPath()
            ctx.arc(0.05 * gs, 0.62 * gs, 0.3 * gs, 0, Math.PI * 2)
            ctx.fill()
        }
        ctx.restore()
    }


    function reload() {
        today = storage.todaySummary()
        // Сессии без фаз (например, дневная дрёмота) не показываем
        var all = storage.sleepSessions(7)
        var nn = []
        for (var i = 0; i < all.length; i++) {
            var s = all[i]
            if ((s.deepMin || 0) + (s.lightMin || 0) + (s.remMin || 0)
                    + (s.awakeMin || 0) > 0)
                nn.push(s)
        }
        sleep = nn
        hourly = storage.hourlyActivity()
        lastSync = bluez.lastSyncTimeText()
        ringCanvas.requestPaint()
        hourlyCanvas.requestPaint()
        cardsRepeater.model = cardModel()
    }

    function refreshGoals() {
        ringCanvas.requestPaint()
        cardsRepeater.model = cardModel()
    }

    function fmtDate(ts) {
        var d = new Date(ts * 1000)
        return ("0" + d.getDate()).slice(-2) + "." + ("0" + (d.getMonth() + 1)).slice(-2)
    }

    function fmtHM(min) {
        return Math.floor(min / 60) + qsTr("ч") + " " + ("0" + (min % 60)).slice(-2) + qsTr("м")
    }

    function val(v) {
        return v !== undefined && v !== null ? v : "—"
    }

    // Модель карточек сетки: порядок и видимость из настроек «Вид»,
    // значения — из текущих данных. frac < 0 — без полосы прогресса.
    function cardModel() {
        var cv = bluez.cardVisibility
        var order = bluez.cardOrder
        var out = []
        for (var i = 0; i < order.length; i++) {
            var id = order[i]
            if (cv[id] === false)
                continue
            var c = cardDef(id)
            if (c)
                out.push(c)
        }
        return out
    }

    function cardDef(id) {
        switch (id) {
        case "steps":
            return { name: qsTr("Шаги"), color: Theme.highlightColor,
                value: val(today.steps),
                frac: (today.steps || 0) / bluez.stepsGoal,
                sub: qsTr("из") + " " + bluez.stepsGoal
                     + multiplierText(today.steps || 0, bluez.stepsGoal),
                page: "MetricPage.qml",
                props: { metricTitle: qsTr("Шаги"), accent: Theme.highlightColor,
                         unit: qsTr("шагов"), field: "steps",
                         goal: bluez.stepsGoal, intraday: false,
                         hourlyField: "steps", hourlyMode: "sum" } }
        case "calories":
            return { name: qsTr("Ккал"), color: accentKcal,
                value: val(today.calories),
                frac: (today.calories || 0) / bluez.caloriesGoal,
                sub: qsTr("из") + " " + bluez.caloriesGoal + " " + qsTr("ккал")
                     + multiplierText(today.calories || 0, bluez.caloriesGoal),
                page: "MetricPage.qml",
                props: { metricTitle: qsTr("Калории"), accent: accentKcal,
                         unit: qsTr("ккал"), field: "calories",
                         goal: bluez.caloriesGoal, intraday: false,
                         hourlyField: "actKcal", hourlyMode: "sum" } }
        case "activity":
            return { name: qsTr("Активность"), color: accentActivity,
                value: val(today.activityMin),
                frac: (today.activityMin || 0) / bluez.activityGoal,
                sub: qsTr("из") + " " + bluez.activityGoal + " " + qsTr("мин")
                     + multiplierText(today.activityMin || 0, bluez.activityGoal),
                page: "MetricPage.qml",
                props: { metricTitle: qsTr("Активность"), accent: accentActivity,
                         unit: qsTr("мин"), field: "activityMin",
                         goal: bluez.activityGoal, intraday: false,
                         hourlyField: "active", hourlyMode: "sum" } }
        case "hr":
            return { name: qsTr("Пульс"), color: accentHr,
                value: val(bluez.heartRate > 0 ? bluez.heartRate : today.avgHr),
                frac: -1,
                sub: qsTr("мин") + " " + val(today.minHr)
                     + " · " + qsTr("макс") + " " + val(today.maxHr),
                page: "MetricPage.qml",
                props: { metricTitle: qsTr("Пульс"), accent: accentHr,
                         unit: qsTr("уд/мин"), field: "avgHr",
                         goal: 0, intraday: true } }
        case "sleep":
            if (!bluez.supportsSleep)
                return null
            return { name: qsTr("Сон"), color: accentSleep,
                value: sleep.length > 0 && sleep[0].sleepMin > 0
                       ? fmtHM(sleep[0].sleepMin) : "—",
                frac: -1,
                sub: sleep.length > 0 ? fmtDate(sleep[0].bedTime) : qsTr("нет данных"),
                page: "SleepPage.qml", props: {} }
        case "stress":
            if (!bluez.supportsStress)
                return null
            return { name: qsTr("Стресс"), color: accentStress,
                value: val(today.stressAvg), frac: -1, sub: qsTr("средний"),
                page: "MetricPage.qml",
                props: { metricTitle: qsTr("Стресс"), accent: accentStress,
                         unit: "", field: "stressAvg",
                         goal: 0, intraday: false,
                         hourlyField: "stress", hourlyMode: "avg" } }
        case "spo2":
            if (!bluez.supportsSpO2)
                return null
            return { name: "SpO2", color: accentSpo2,
                value: val(today.spo2Avg), frac: -1, sub: qsTr("средний, %"),
                page: "MetricPage.qml",
                props: { metricTitle: "SpO2", accent: accentSpo2,
                         unit: "%", field: "spo2Avg",
                         goal: 0, intraday: false,
                         hourlyField: "spo2", hourlyMode: "avg" } }
        case "battery":
            var lvl = bluez.bandInfo.batteryLevel
            return { name: qsTr("Батарея"), color: accentBattery,
                value: lvl !== undefined ? lvl + " %" : "—",
                frac: lvl !== undefined ? lvl / 100.0 : -1,
                sub: bluez.bandInfo.batteryState === 1 ? qsTr("заряжается") : "",
                page: "BatteryPage.qml", props: {} }
        }
        return null
    }

    Component.onCompleted: reload()

    Connections {
        target: bluez
        onBandInfoChanged: cardsRepeater.model = page.cardModel()
        onViewConfigChanged: cardsRepeater.model = page.cardModel()
        onCapabilitiesChanged: cardsRepeater.model = page.cardModel()
    }

    // Автообновление раз в минуту, пока страница активна (подхватывает
    // данные демона-автосинка), плюс reload при возвращении на страницу
    Timer {
        interval: 60000
        repeat: true
        running: page.status === PageStatus.Active
        onTriggered: page.reload()
    }

    onStatusChanged: {
        if (status === PageStatus.Active)
            reload()
    }

    Connections {
        target: storage
        onDataChanged: reload()
    }

    Connections {
        target: bluez
        onStepsGoalChanged: page.refreshGoals()
        onCaloriesGoalChanged: page.refreshGoals()
        onActivityGoalChanged: page.refreshGoals()
    }

    AppBar {
        id: appBar

        headerText: qsTr("Аврора Фитнес")
        subHeaderText: {
            var us = bluez.userStatus
            // Статусы с «…» — идущие процессы, показываем их в любом случае
            if (us.indexOf("…") !== -1 || bluez.connectedAddress.length === 0)
                return us.length > 0 ? us : qsTr("Браслет не подключён")
            var s = bluez.connectedDeviceName.length > 0
                    ? bluez.connectedDeviceName : qsTr("Подключён")
            if (bluez.bandInfo.batteryLevel !== undefined && bluez.bandInfo.batteryLevel > 0)
                s += " · " + bluez.bandInfo.batteryLevel + "%"
            if (page.lastSync.length > 0)
                s += " · " + qsTr("обновлено") + " " + page.lastSync
            return s
        }

        AppBarSpacer {}

        AppBarButton {
            icon.source: "image://theme/icon-splus-more"
            onClicked: mainMenu.open()

            PopupMenu {
                id: mainMenu

                PopupMenuItem {
                    text: qsTr("Настройки")
                    icon.source: "image://theme/icon-m-setting"
                    onClicked: pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
                }
                PopupMenuItem {
                    text: qsTr("Профиль")
                    icon.source: "image://theme/icon-m-contact"
                    onClicked: pageStack.push(Qt.resolvedUrl("ProfilePage.qml"))
                }
                PopupMenuItem {
                    text: qsTr("Цели")
                    icon.source: "image://theme/icon-m-administrator"
                    onClicked: pageStack.push(Qt.resolvedUrl("GoalsPage.qml"))
                }
                PopupMenuItem {
                    text: qsTr("Вид")
                    icon.source: "image://theme/icon-m-display"
                    onClicked: pageStack.push(Qt.resolvedUrl("ViewPage.qml"))
                }
                PopupMenuDividerItem {}
                PopupMenuItem {
                    text: qsTr("О приложении")
                    icon.source: "image://theme/icon-m-about"
                    onClicked: pageStack.push(Qt.resolvedUrl("AboutPage.qml"))
                }
            }
        }
    }

    SilicaFlickable {
        id: flick
        anchors {
            top: appBar.bottom
            bottom: parent.bottom
            left: parent.left
            right: parent.right
        }
        contentHeight: Math.max(column.height, height + 1)
        // overshoot за верхний край нужен всегда, даже когда контент короче вьюпорта
        boundsBehavior: Flickable.DragAndOvershootBounds

        // --- pull-to-refresh: нативный жест Aurora.Controls ---
        property bool syncRunning: false    // синк уже идёт — повторы блокируем

        PullToRefresh.refreshHandler: startPullSync

        function startPullSync() {
            if (!bluez.ready || syncRunning) {
                // скрыть индикатор без статуса
                flick.PullToRefresh.refreshCompletedCustom("", "")
                return
            }
            syncRunning = true
            bluez.syncActivity()
        }

        Connections {
            target: bluez
            // Индикатор крутится при ЛЮБОМ синке: кнопка, pull-to-refresh,
            // автосинк после подключения при старте приложения
            onActivitySyncStarted: flick.syncRunning = true
            onActivitySyncFinished: {
                if (flick.syncRunning) {
                    flick.syncRunning = false
                    flick.PullToRefresh.refreshCompleted()
                }
            }
            onDeviceError: {
                if (flick.syncRunning) {
                    flick.syncRunning = false
                    flick.PullToRefresh.refreshCompleted(false)
                }
            }
            onBandReadyChanged: if (!bluez.ready) flick.syncRunning = false
        }

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingLarge

            Item { width: 1; height: Theme.paddingSmall }

            // --- Hero-карточка: кольцо прогресса шагов ---
            Rectangle {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                height: heroContent.height + 2 * Theme.paddingLarge
                radius: Theme.dp(20)
                color: page.cardColor

                // Тап по карточке шагов — дневная статистика
                MouseArea {
                    z: 1
                    anchors.fill: parent
                    onClicked: pageStack.push(Qt.resolvedUrl("StatsPage.qml"))
                }

                Column {
                    id: heroContent
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: Theme.paddingLarge
                    width: parent.width
                    spacing: Theme.paddingMedium

                    Item {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: ringCanvas.width
                        height: ringCanvas.height

                        Canvas {
                            id: ringCanvas
                            width: Math.min(page.width - 6 * Theme.horizontalPageMargin,
                                            Theme.dp(400))
                            height: width

                            onPaint: {
                                var ctx = getContext("2d")
                                ctx.clearRect(0, 0, width, height)
                                var lw = Theme.dp(10)
                                // Зазор больше радиуса диска иконки (lw*1.05),
                                // чтобы соседние иконки не наезжали друг на друга
                                var gap = lw * 1.25
                                var cx = width / 2, cy = height / 2
                                // Радиус внешнего кольца с запасом под диск иконки
                                var rOuter = (width - lw) / 2 - lw * 1.1

                                // Три кольца: шаги, калории, активность.
                                // Цвет базовый всегда; при переполнении кольцо
                                // полное + шевроны ">" на позиции текущего круга.
                                var rings = [
                                    { ov: (page.today.steps || 0) / bluez.stepsGoal,
                                      color: Theme.highlightColor,
                                      icon: "shoe" },
                                    { ov: (page.today.calories || 0) / bluez.caloriesGoal,
                                      color: page.accentKcal,
                                      icon: "flame" },
                                    { ov: (page.today.activityMin || 0) / bluez.activityGoal,
                                      color: page.accentActivity,
                                      icon: "clock" }
                                ]

                                ctx.lineCap = "round"
                                for (var i = 0; i < rings.length; i++) {
                                    var r = rOuter - i * (lw + gap)
                                    var col = rings[i].color
                                    var laps = Math.floor(rings[i].ov)
                                    var frac = laps >= 1 ? 1.0 : Math.min(1.0, rings[i].ov)

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
                                        ProgressMarkers.drawRing(ctx, cx, cy, r, laps,
                                                             rings[i].ov - laps, lw)
                                }
                            }
                        }

                        Column {
                            anchors.centerIn: parent
                            spacing: 0

                            Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: page.today.steps !== undefined ? page.today.steps : "0"
                                color: Theme.highlightColor
                                font.pixelSize: Theme.fontSizeHuge
                                font.bold: true
                            }
                            Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: qsTr("из") + " " + bluez.stepsGoal + " " + qsTr("шагов")
                                      + page.multiplierText(page.today.steps || 0, bluez.stepsGoal)
                                color: Theme.secondaryColor
                                font.pixelSize: Theme.fontSizeSmall
                            }
                            Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                visible: (page.today.steps || 0) > 0
                                text: {
                                    var s = page.today.steps || 0
                                    if (s >= bluez.stepsGoal)
                                        return page.multiplierText(s, bluez.stepsGoal).substring(1)
                                    return Math.round(100.0 * s / bluez.stepsGoal) + "%"
                                }
                                color: (page.today.steps || 0) >= bluez.stepsGoal
                                       ? page.goalBright : Theme.secondaryHighlightColor
                                font.pixelSize: Theme.fontSizeMedium
                            }
                        }
                    }
                }
            }

            // --- Карточки метрик 2xN: состав и порядок — из настроек «Вид» ---
            Grid {
                id: metricsGrid
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                columns: 2
                spacing: Theme.paddingMedium

                Repeater {
                    id: cardsRepeater
                    model: []

                    delegate: Rectangle {
                        width: (metricsGrid.width - Theme.paddingMedium) / 2
                        height: cardCol.height + 2 * Theme.paddingLarge
                        radius: Theme.dp(20)
                        color: cardMouse.pressed ? Theme.rgba(Theme.highlightColor, 0.3)
                                                 : page.cardColor

                        Column {
                            id: cardCol
                            x: Theme.paddingLarge
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 2 * x
                            spacing: Theme.paddingSmall

                            Rectangle {
                                width: Theme.dp(14); height: width; radius: width / 2
                                color: modelData.color
                            }
                            Label {
                                width: parent.width
                                height: Math.round(Theme.fontSizeExtraSmall * 1.4)
                                text: modelData.name
                                color: Theme.secondaryColor
                                font.pixelSize: Theme.fontSizeExtraSmall
                                elide: Text.ElideRight
                            }
                            Label {
                                width: parent.width
                                height: Math.round(Theme.fontSizeExtraLarge * 1.25)
                                text: modelData.value
                                color: modelData.color
                                font.pixelSize: Theme.fontSizeExtraLarge
                                font.bold: true
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                            }
                            // Полоса прогресса к цели (или прозрачный спейсер
                            // той же высоты — все карточки одной высоты)
                            Rectangle {
                                width: parent.width
                                height: Theme.dp(6)
                                radius: height / 2
                                color: Theme.rgba(modelData.color, 0.2)
                                opacity: modelData.frac >= 0 ? 1 : 0

                                Rectangle {
                                    width: parent.width
                                           * Math.max(0, Math.min(1.0, modelData.frac))
                                    height: parent.height
                                    radius: parent.radius
                                    color: modelData.color
                                }
                            }
                            Label {
                                width: parent.width
                                height: Math.round(Theme.fontSizeExtraSmall * 1.4)
                                text: modelData.sub
                                color: Theme.secondaryColor
                                font.pixelSize: Theme.fontSizeExtraSmall
                                elide: Text.ElideRight
                            }
                        }

                        MouseArea {
                            id: cardMouse
                            anchors.fill: parent
                            onClicked: pageStack.push(Qt.resolvedUrl(modelData.page),
                                                      modelData.props)
                        }
                    }
                }
            }

            Rectangle {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                height: hourlyContent.height + 2 * Theme.paddingLarge
                radius: Theme.dp(20)
                color: page.cardColor
                visible: bluez.estimatedActivity && page.hourly.length > 0

                Column {
                    id: hourlyContent
                    x: Theme.paddingLarge
                    y: Theme.paddingLarge
                    width: parent.width - 2 * x
                    spacing: Theme.paddingSmall

                    Label {
                        text: qsTr("Активность по часам")
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
                            var values = []
                            var maxMinutes = 1
                            for (var i = 0; i < 24; i++)
                                values.push(0)
                            for (i = 0; i < page.hourly.length; i++) {
                                var sample = page.hourly[i]
                                values[sample.hour] = sample.activeMin
                                maxMinutes = Math.max(maxMinutes, sample.activeMin)
                            }
                            var slot = width / 24
                            var chartHeight = height - Theme.fontSizeExtraSmall * 1.5
                            ctx.font = Theme.fontSizeExtraSmall + "px sans-serif"
                            ctx.textAlign = "center"
                            for (i = 0; i < 24; i++) {
                                var barHeight = Math.max(Theme.dp(2), chartHeight * values[i] / maxMinutes)
                                ctx.fillStyle = values[i] > 0 ? page.accentActivity : Theme.rgba(Theme.primaryColor, 0.15)
                                ctx.fillRect(i * slot + slot * 0.2, chartHeight - barHeight, slot * 0.6, barHeight)
                                if (i % 6 === 0) {
                                    ctx.fillStyle = Theme.secondaryColor
                                    ctx.fillText(i, i * slot + slot / 2, height - Theme.dp(2))
                                }
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
