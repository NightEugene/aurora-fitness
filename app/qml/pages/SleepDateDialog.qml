import QtQuick 2.0
import Sailfish.Silica 1.0

// Календарь выбора даты сна. Даты, на которые есть записи,
// отмечены точкой под числом (markedDates — строки "yyyy-MM-dd").
Dialog {
    id: dialog

    property date date: new Date()
    property var markedDates: []
    readonly property date selectedDate: picker.date

    function key(y, m, d) {
        return y + "-" + ("0" + m).slice(-2) + "-" + ("0" + d).slice(-2)
    }

    DialogHeader { id: header }

    DatePicker {
        id: picker
        anchors {
            top: header.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        date: dialog.date
        daysVisible: true

        // Дефолтный делегат DatePicker + точка под датами с данными
        delegate: Component {
            MouseArea {
                id: cellMouse
                width: picker.cellWidth
                height: picker.cellHeight

                property bool marked: dialog.markedDates.indexOf(
                                          dialog.key(model.year, model.month,
                                                     model.day)) >= 0

                Label {
                    id: dayLabel
                    anchors.centerIn: parent
                    text: model.day.toLocaleString()
                    font.bold: model.day === picker._today.getDate()
                               && model.month === picker._today.getMonth() + 1
                               && model.year === picker._today.getFullYear()
                    color: {
                        if (cellMouse.pressed && cellMouse.containsMouse
                                || model.day === picker.day
                                && model.month === picker.month
                                && model.year === picker.year)
                            return Theme.highlightColor
                        if (model.month === model.primaryMonth)
                            return Theme.primaryColor
                        return Theme.secondaryColor
                    }
                }

                Rectangle {
                    anchors.right: dayLabel.left
                    anchors.rightMargin: Theme.dp(4)
                    anchors.verticalCenter: parent.verticalCenter
                    visible: cellMouse.marked
                    width: Theme.dp(8)
                    height: width
                    radius: width / 2
                    color: Theme.highlightColor
                }

                onClicked: picker.date = new Date(model.year, model.month - 1,
                                                  model.day, 12, 0, 0)
            }
        }
    }
}
