import QtQuick 2.0
import Sailfish.Silica 1.0
import Aurora.Controls 1.0

Page {
    id: page

    AppBar {
        id: appBar

        headerText: qsTr("Цели")
        subHeaderText: qsTr("Дневные цели активности")
    }

    Column {
        anchors {
            top: appBar.bottom
            left: parent.left
            right: parent.right
        }
        spacing: Theme.paddingMedium

        SectionHeader {
            text: qsTr("Шаги")
            horizontalAlignment: Text.AlignLeft
        }

        Slider {
            id: stepsSlider
            width: parent.width
            minimumValue: 2000
            maximumValue: 50000
            stepSize: 500
            label: qsTr("Цель по шагам в день")
            valueText: value

            // Защита от рекурсии при синхронизации ползунка извне
            property bool syncGuard: false

            Component.onCompleted: value = Math.max(minimumValue,
                                                    Math.min(maximumValue, bluez.stepsGoal))
            onValueChanged: if (!syncGuard) bluez.stepsGoal = value

            Connections {
                target: bluez
                onStepsGoalChanged: {
                    stepsSlider.syncGuard = true
                    stepsSlider.value = bluez.stepsGoal
                    stepsSlider.syncGuard = false
                }
            }
        }

        TextField {
            id: stepsField
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * x
            label: qsTr("Вручную, шагов (2000–50000)")
            inputMethodHints: Qt.ImhDigitsOnly
            validator: IntValidator { bottom: 2000; top: 50000 }

            function applyText() {
                var v = parseInt(text)
                if (!isNaN(v) && v >= 2000 && v <= 50000)
                    bluez.stepsGoal = v
                text = bluez.stepsGoal // нормализация/откат недопустимого ввода
            }

            Component.onCompleted: text = bluez.stepsGoal
            EnterKey.onClicked: {
                applyText()
                focus = false
            }
            onActiveFocusChanged: if (!activeFocus) applyText()

            Connections {
                target: bluez
                onStepsGoalChanged: if (!stepsField.activeFocus)
                    stepsField.text = bluez.stepsGoal
            }
        }

        Item { width: 1; height: Theme.paddingMedium }

        SectionHeader {
            text: qsTr("Калории")
            horizontalAlignment: Text.AlignLeft
        }

        Slider {
            id: caloriesSlider
            width: parent.width
            minimumValue: 200
            maximumValue: 5000
            stepSize: 100
            label: qsTr("Цель по калориям в день")
            valueText: value

            property bool syncGuard: false

            Component.onCompleted: value = Math.max(minimumValue,
                                                    Math.min(maximumValue, bluez.caloriesGoal))
            onValueChanged: if (!syncGuard) bluez.caloriesGoal = value

            Connections {
                target: bluez
                onCaloriesGoalChanged: {
                    caloriesSlider.syncGuard = true
                    caloriesSlider.value = bluez.caloriesGoal
                    caloriesSlider.syncGuard = false
                }
            }
        }

        TextField {
            id: caloriesField
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * x
            label: qsTr("Вручную, ккал (200–5000)")
            inputMethodHints: Qt.ImhDigitsOnly
            validator: IntValidator { bottom: 200; top: 5000 }

            function applyText() {
                var v = parseInt(text)
                if (!isNaN(v) && v >= 200 && v <= 5000)
                    bluez.caloriesGoal = v
                text = bluez.caloriesGoal
            }

            Component.onCompleted: text = bluez.caloriesGoal
            EnterKey.onClicked: {
                applyText()
                focus = false
            }
            onActiveFocusChanged: if (!activeFocus) applyText()

            Connections {
                target: bluez
                onCaloriesGoalChanged: if (!caloriesField.activeFocus)
                    caloriesField.text = bluez.caloriesGoal
            }
        }

        Item { width: 1; height: Theme.paddingMedium }

        SectionHeader {
            text: qsTr("Активность, мин")
            horizontalAlignment: Text.AlignLeft
        }

        Slider {
            id: activitySlider
            width: parent.width
            minimumValue: 10
            maximumValue: 180
            stepSize: 5
            label: qsTr("Цель по времени активности в день")
            valueText: value

            property bool syncGuard: false

            Component.onCompleted: value = bluez.activityGoal
            onValueChanged: if (!syncGuard) bluez.activityGoal = value

            Connections {
                target: bluez
                onActivityGoalChanged: {
                    activitySlider.syncGuard = true
                    activitySlider.value = bluez.activityGoal
                    activitySlider.syncGuard = false
                }
            }
        }

        TextField {
            id: activityField
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * x
            label: qsTr("Вручную, мин (10–180)")
            inputMethodHints: Qt.ImhDigitsOnly
            validator: IntValidator { bottom: 10; top: 180 }

            function applyText() {
                var v = parseInt(text)
                if (!isNaN(v) && v >= 10 && v <= 180)
                    bluez.activityGoal = v
                text = bluez.activityGoal
            }

            Component.onCompleted: text = bluez.activityGoal
            EnterKey.onClicked: {
                applyText()
                focus = false
            }
            onActiveFocusChanged: if (!activeFocus) applyText()

            Connections {
                target: bluez
                onActivityGoalChanged: if (!activityField.activeFocus)
                    activityField.text = bluez.activityGoal
            }
        }
    }
}
