import QtQuick 2.0
import Sailfish.Silica 1.0

CoverBackground {
    id: cover

    property var today: ({})

    readonly property color accentKcal: "#ff9800"
    readonly property color accentActivity: "#8bc34a"
    readonly property color goalDone: "#4caf50"

    function reload() {
        today = storage.todaySummary()
    }

    function ratio(value, goal) {
        if (value === undefined || goal <= 0)
            return 0
        return Math.min(1.0, value / goal)
    }

    Component.onCompleted: reload()

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
                    width: parent.width * cover.ratio(cover.today.steps, bluez.stepsGoal)
                    height: parent.height
                    radius: parent.radius
                    color: cover.ratio(cover.today.steps, bluez.stepsGoal) >= 1
                           ? cover.goalDone : Theme.highlightColor
                }
            }
        }

        // Калории
        Column {
            width: parent.width
            spacing: Theme.paddingSmall

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
                    width: parent.width * cover.ratio(cover.today.calories, bluez.caloriesGoal)
                    height: parent.height
                    radius: parent.radius
                    color: cover.ratio(cover.today.calories, bluez.caloriesGoal) >= 1
                           ? cover.goalDone : cover.accentKcal
                }
            }
        }

        // Активность
        Column {
            width: parent.width
            spacing: Theme.paddingSmall

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
                    width: parent.width * cover.ratio(cover.today.activityMin, bluez.activityGoal)
                    height: parent.height
                    radius: parent.radius
                    color: cover.ratio(cover.today.activityMin, bluez.activityGoal) >= 1
                           ? cover.goalDone : cover.accentActivity
                }
            }
        }
    }
}
