import QtQuick 2.0
import QtFeedback 5.0
import Sailfish.Silica 1.0
import Aurora.Controls 1.0

Page {
    id: page

    // Виброотклик при pull-to-refresh
    HapticsEffect {
        id: refreshBuzz
        attackIntensity: 0.0
        fadeIntensity: 0.0
        attackTime: 30
        fadeTime: 30
        intensity: 0.6
        duration: 70
    }

    property var today: ({})
    property var week: []
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

    // Шевроны ">" прямо на кольце: позиция — конец текущего «круга» (overFrac),
    // направление — по ходу заполнения (по часовой), число = полные круги (laps)
    function drawLapChevrons(ctx, cx, cy, r, laps, overFrac, lw) {
        var a = -Math.PI / 2 + overFrac * Math.PI * 2
        var px = cx + r * Math.cos(a)
        var py = cy + r * Math.sin(a)
        var tx = -Math.sin(a), ty = Math.cos(a)  // тангент, по ходу движения
        var nx = Math.cos(a), ny = Math.sin(a)   // радиально наружу
        var s = lw * 0.55                        // полуразмер шеврона
        ctx.strokeStyle = "black"
        ctx.lineWidth = lw * 0.32
        ctx.lineCap = "round"
        for (var k = 0; k < laps; k++) {
            var bx = px - tx * k * s * 1.5
            var by = py - ty * k * s * 1.5
            ctx.beginPath()
            ctx.moveTo(bx - tx * s + nx * s, by - ty * s + ny * s)
            ctx.lineTo(bx + tx * s, by + ty * s)
            ctx.lineTo(bx - tx * s - nx * s, by - ty * s - ny * s)
            ctx.stroke()
        }
    }

    function reload() {
        today = storage.todaySummary()
        week = storage.dailySummaries(7)
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
        weekCanvas.requestPaint()
        hourlyCanvas.requestPaint()
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

    Component.onCompleted: reload()

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
        onStepsGoalChanged: reload()
        onCaloriesGoalChanged: reload()
        onActivityGoalChanged: reload()
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

        Item {
            width: syncSpinner.width
            height: syncSpinner.height

            BusyIndicator {
                id: syncSpinner
                size: BusyIndicatorSize.Small
                // Крутимся при любом активном процессе: ожидание линка,
                // подключение, ожидание сервисов, аутентификация, синк
                running: flick.syncRunning || bluez.busy
                         || bluez.userStatus.indexOf("…") !== -1
                opacity: running ? 1 : 0
            }
        }

        AppBarButton {
            context: qsTr("Синхронизировать")
            icon.source: "image://theme/icon-m-refresh"
            enabled: bluez.ready
                     && !flick.syncRunning
            onClicked: bluez.syncActivity()
        }

        AppBarButton {
            context: qsTr("Меню")
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
                    text: qsTr("Цели")
                    icon.source: "image://theme/icon-m-administrator"
                    onClicked: pageStack.push(Qt.resolvedUrl("GoalsPage.qml"))
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

        // --- pull-to-refresh: синк при отпускании за порогом overscroll ---
        property bool pullArmed: false      // драг ушёл за порог
        property bool syncRunning: false    // синк уже идёт — повторы блокируем
        readonly property bool ready: bluez.ready
        readonly property bool pullOver: contentY < -Theme.itemSizeLarge

        onContentYChanged: {
            if (pullOver && ready && !syncRunning)
                pullArmed = true
        }
        onMovementStarted: pullArmed = false
        onMovementEnded: {
            if (pullArmed && ready && !syncRunning) {
                refreshBuzz.start()
                syncRunning = true
                bluez.syncActivity()
            }
            pullArmed = false
        }

        Connections {
            target: bluez
            // Индикатор крутится при ЛЮБОМ синке: кнопка, pull-to-refresh,
            // автосинк после подключения при старте приложения
            onActivitySyncStarted: flick.syncRunning = true
            onActivitySyncFinished: flick.syncRunning = false
            onDeviceError: flick.syncRunning = false
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
                                        page.drawLapChevrons(ctx, cx, cy, r, laps,
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

            // --- Карточки метрик 2x2 ---
            Grid {
                id: metricsGrid
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                columns: 2
                spacing: Theme.paddingMedium
                readonly property int metricCount: 3 + (bluez.supportsSleep ? 1 : 0)
                                                    + (bluez.supportsStress ? 1 : 0)
                                                    + (bluez.supportsSpO2 ? 1 : 0)
                readonly property real cardHeight: Math.max(caloriesContent.height, activityContent.height,
                                                           heartRateContent.height, sleepContent.height,
                                                           stressContent.height, spo2Content.height,
                                                           batteryContent.height)
                                                   + 2 * Theme.paddingLarge

                // Ккал
                Rectangle {
                    width: (parent.width - Theme.paddingMedium) / 2
                    height: metricsGrid.cardHeight
                    radius: Theme.dp(20)
                    color: page.cardColor

                    Column {
                        id: caloriesContent
                        x: Theme.paddingLarge
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 2 * x
                        spacing: Theme.paddingSmall

                        Rectangle {
                            width: Theme.dp(14); height: width; radius: width / 2
                            color: page.accentKcal
                        }
                        Label {
                            text: qsTr("Ккал")
                            color: Theme.secondaryColor
                            font.pixelSize: Theme.fontSizeExtraSmall
                        }
                        Label {
                            text: page.val(page.today.calories)
                            color: page.accentKcal
                            font.pixelSize: Theme.fontSizeExtraLarge
                            font.bold: true
                        }
                        // Прогресс к дневной цели по калориям
                        Rectangle {
                            width: parent.width
                            height: Theme.dp(6)
                            radius: height / 2
                            color: Theme.rgba(page.accentKcal, 0.2)

                            Rectangle {
                                width: parent.width
                                       * Math.min(1.0, (page.today.calories || 0) / bluez.caloriesGoal)
                                height: parent.height
                                radius: parent.radius
                                color: (page.today.calories || 0) >= bluez.caloriesGoal
                                       ? page.accentKcalBright : page.accentKcal
                            }
                        }
                        Label {
                            text: qsTr("из") + " " + bluez.caloriesGoal + " " + qsTr("ккал")
                                  + page.multiplierText(page.today.calories || 0, bluez.caloriesGoal)
                            color: Theme.secondaryColor
                            font.pixelSize: Theme.fontSizeExtraSmall
                        }
                    }
                }

                // Активность (время активности, мин)
                Rectangle {
                    width: (parent.width - Theme.paddingMedium) / 2
                    height: metricsGrid.cardHeight
                    radius: Theme.dp(20)
                    color: page.cardColor

                    Column {
                        id: activityContent
                        x: Theme.paddingLarge
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 2 * x
                        spacing: Theme.paddingSmall

                        Rectangle {
                            width: Theme.dp(14); height: width; radius: width / 2
                            color: page.accentActivity
                        }
                        Label {
                            text: qsTr("Активность")
                            color: Theme.secondaryColor
                            font.pixelSize: Theme.fontSizeExtraSmall
                        }
                        Label {
                            text: page.val(page.today.activityMin)
                            color: page.accentActivity
                            font.pixelSize: Theme.fontSizeExtraLarge
                            font.bold: true
                        }
                        // Прогресс к дневной цели по активности
                        Rectangle {
                            width: parent.width
                            height: Theme.dp(6)
                            radius: height / 2
                            color: Theme.rgba(page.accentActivity, 0.2)

                            Rectangle {
                                width: parent.width
                                       * Math.min(1.0, (page.today.activityMin || 0) / bluez.activityGoal)
                                height: parent.height
                                radius: parent.radius
                                color: (page.today.activityMin || 0) >= bluez.activityGoal
                                       ? page.accentActivityBright : page.accentActivity
                            }
                        }
                        Label {
                            text: qsTr("из") + " " + bluez.activityGoal + " " + qsTr("мин")
                                  + page.multiplierText(page.today.activityMin || 0, bluez.activityGoal)
                            color: Theme.secondaryColor
                            font.pixelSize: Theme.fontSizeExtraSmall
                        }
                    }
                }

                // Пульс
                Rectangle {
                    width: (parent.width - Theme.paddingMedium) / 2
                    height: metricsGrid.cardHeight
                    radius: Theme.dp(20)
                    color: page.cardColor

                    Column {
                        id: heartRateContent
                        x: Theme.paddingLarge
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 2 * x
                        spacing: Theme.paddingSmall

                        Rectangle {
                            width: Theme.dp(14); height: width; radius: width / 2
                            color: page.accentHr
                        }
                        Label {
                            text: qsTr("Пульс")
                            color: Theme.secondaryColor
                            font.pixelSize: Theme.fontSizeExtraSmall
                        }
                        Label {
                            text: page.val(bluez.heartRate > 0 ? bluez.heartRate : page.today.avgHr)
                            color: page.accentHr
                            font.pixelSize: Theme.fontSizeExtraLarge
                            font.bold: true
                        }
                        Label {
                            text: qsTr("мин") + " " + page.val(page.today.minHr)
                                  + " · " + qsTr("макс") + " " + page.val(page.today.maxHr)
                            color: Theme.secondaryColor
                            font.pixelSize: Theme.fontSizeExtraSmall
                        }
                    }
                }

                // Сон
                Rectangle {
                    visible: bluez.supportsSleep
                    width: (parent.width - Theme.paddingMedium) / 2
                    height: metricsGrid.cardHeight
                    radius: Theme.dp(20)
                    color: sleepMouse.pressed ? Theme.rgba(Theme.highlightColor, 0.3)
                                              : page.cardColor

                    Column {
                        id: sleepContent
                        x: Theme.paddingLarge
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 2 * x
                        spacing: Theme.paddingSmall

                        Rectangle {
                            width: Theme.dp(14); height: width; radius: width / 2
                            color: page.accentSleep
                        }
                        Label {
                            text: qsTr("Сон")
                            color: Theme.secondaryColor
                            font.pixelSize: Theme.fontSizeExtraSmall
                        }
                        Label {
                            text: page.sleep.length > 0 && page.sleep[0].sleepMin > 0
                                  ? page.fmtHM(page.sleep[0].sleepMin) : "—"
                            color: page.accentSleep
                            font.pixelSize: Theme.fontSizeLarge
                            font.bold: true
                        }
                        Label {
                            text: page.sleep.length > 0
                                  ? page.fmtDate(page.sleep[0].bedTime) : qsTr("нет данных")
                            color: Theme.secondaryColor
                            font.pixelSize: Theme.fontSizeExtraSmall
                        }
                    }

                    MouseArea {
                        id: sleepMouse
                        anchors.fill: parent
                        onClicked: pageStack.push(Qt.resolvedUrl("SleepPage.qml"))
                    }
                }

                // Стресс
                Rectangle {
                    visible: bluez.supportsStress
                    width: (parent.width - Theme.paddingMedium) / 2
                    height: metricsGrid.cardHeight
                    radius: Theme.dp(20)
                    color: page.cardColor

                    Column {
                        id: stressContent
                        x: Theme.paddingLarge
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 2 * x
                        spacing: Theme.paddingSmall

                        Rectangle {
                            width: Theme.dp(14); height: width; radius: width / 2
                            color: page.accentStress
                        }
                        Label {
                            text: qsTr("Стресс")
                            color: Theme.secondaryColor
                            font.pixelSize: Theme.fontSizeExtraSmall
                        }
                        Label {
                            text: page.val(page.today.stressAvg)
                            color: page.accentStress
                            font.pixelSize: Theme.fontSizeExtraLarge
                            font.bold: true
                        }
                        Label {
                            text: qsTr("средний")
                            color: Theme.secondaryColor
                            font.pixelSize: Theme.fontSizeExtraSmall
                        }
                    }
                }

                // SpO2
                Rectangle {
                    visible: bluez.supportsSpO2
                    width: (parent.width - Theme.paddingMedium) / 2
                    height: metricsGrid.cardHeight
                    radius: Theme.dp(20)
                    color: page.cardColor

                    Column {
                        id: spo2Content
                        x: Theme.paddingLarge
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 2 * x
                        spacing: Theme.paddingSmall

                        Rectangle {
                            width: Theme.dp(14); height: width; radius: width / 2
                            color: page.accentSpo2
                        }
                        Label {
                            text: "SpO2"
                            color: Theme.secondaryColor
                            font.pixelSize: Theme.fontSizeExtraSmall
                        }
                        Label {
                            text: page.val(page.today.spo2Avg)
                            color: page.accentSpo2
                            font.pixelSize: Theme.fontSizeExtraLarge
                            font.bold: true
                        }
                        Label {
                            text: qsTr("средний, %")
                            color: Theme.secondaryColor
                            font.pixelSize: Theme.fontSizeExtraSmall
                        }
                    }
                }

                Rectangle {
                    visible: metricsGrid.metricCount % 2 === 1
                    width: (parent.width - Theme.paddingMedium) / 2
                    height: metricsGrid.cardHeight
                    radius: Theme.dp(20)
                    color: page.cardColor
                    readonly property int level: bluez.bandInfo.batteryLevel !== undefined
                                                 ? bluez.bandInfo.batteryLevel : -1

                    Column {
                        id: batteryContent
                        x: Theme.paddingLarge
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 2 * x
                        spacing: Theme.paddingSmall

                        Rectangle {
                            width: Theme.dp(14); height: width; radius: width / 2
                            color: page.accentBattery
                        }
                        Label {
                            text: qsTr("Батарея")
                            color: Theme.secondaryColor
                            font.pixelSize: Theme.fontSizeExtraSmall
                        }
                        Label {
                            text: batteryContent.parent.level >= 0 ? batteryContent.parent.level : "—"
                            color: page.accentBattery
                            font.pixelSize: Theme.fontSizeExtraLarge
                            font.bold: true
                        }
                        Rectangle {
                            width: parent.width
                            height: Theme.dp(6)
                            radius: height / 2
                            color: Theme.rgba(page.accentBattery, 0.2)

                            Rectangle {
                                width: parent.width * Math.max(0, Math.min(100, batteryContent.parent.level)) / 100
                                height: parent.height
                                radius: parent.radius
                                color: page.accentBattery
                            }
                        }
                        Label {
                            text: qsTr("из") + " 100 %"
                            color: Theme.secondaryColor
                            font.pixelSize: Theme.fontSizeExtraSmall
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

            // --- Шаги за 7 дней ---
            Rectangle {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                height: weekCol.height + 2 * Theme.paddingMedium
                radius: Theme.dp(20)
                color: page.cardColor
                visible: page.week.length > 0

                Column {
                    id: weekCol
                    x: Theme.paddingLarge
                    y: Theme.paddingMedium
                    width: parent.width - 2 * x
                    spacing: Theme.paddingSmall

                    Label {
                        text: qsTr("Шаги за 7 дней")
                        color: Theme.primaryColor
                        font.pixelSize: Theme.fontSizeSmall
                    }

                    Canvas {
                        id: weekCanvas
                        width: parent.width
                        height: Theme.dp(300)

                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.clearRect(0, 0, width, height)
                            var days = page.week
                            if (days.length === 0)
                                return

                            var maxSteps = 0
                            for (var i = 0; i < days.length; i++)
                                maxSteps = Math.max(maxSteps, days[i].steps || 0)

                            var labelH = Theme.fontSizeExtraSmall * 2.2
                            var chartH = height - labelH
                            var slot = width / days.length
                            var barW = Math.min(slot * 0.55, Theme.dp(64))
                            var todayIdx = days.length - 1

                            ctx.font = Theme.fontSizeExtraSmall + "px sans-serif"
                            ctx.textAlign = "center"
                            for (i = 0; i < days.length; i++) {
                                var steps = days[i].steps || 0
                                var h = maxSteps > 0 ? (chartH * 0.8) * steps / maxSteps : 0
                                var bx = i * slot + (slot - barW) / 2
                                var by = chartH - h
                                var isToday = (i === todayIdx)

                                ctx.fillStyle = isToday
                                        ? Theme.highlightColor
                                        : Theme.rgba(Theme.highlightColor, 0.35)
                                if (h > 0) {
                                    // Столбик со скруглённой верхушкой
                                    var r = Math.min(barW / 2, h, Theme.dp(10))
                                    ctx.beginPath()
                                    ctx.moveTo(bx, by + h)
                                    ctx.lineTo(bx, by + r)
                                    ctx.quadraticCurveTo(bx, by, bx + r, by)
                                    ctx.lineTo(bx + barW - r, by)
                                    ctx.quadraticCurveTo(bx + barW, by, bx + barW, by + r)
                                    ctx.lineTo(bx + barW, by + h)
                                    ctx.closePath()
                                    ctx.fill()
                                }

                                if (steps > 0) {
                                    ctx.fillStyle = isToday
                                            ? Theme.highlightColor : Theme.secondaryColor
                                    ctx.fillText(steps, bx + barW / 2,
                                                 Math.max(Theme.fontSizeExtraSmall, by - Theme.dp(10)))
                                }
                                ctx.fillStyle = Theme.secondaryColor
                                ctx.fillText(page.fmtDate(days[i].ts), bx + barW / 2, height - 2)
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
