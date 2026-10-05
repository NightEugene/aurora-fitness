# SPDX-License-Identifier: BSD-3-Clause

TEMPLATE = app

TARGET = ru.nighteugene.aurorafitness

QT += dbus sql

QMAKE_CXXFLAGS += -std=c++17

CONFIG += auroraapp

SOURCES += \
    src/wearablechannel.cpp \
    src/pinetimechannel.cpp \
    src/mprisbridge.cpp \
    src/bluezmanager.cpp \
    src/devicesmodel.cpp \
    src/storage.cpp \
    src/notificationdaemon.cpp \
    src/main.cpp \
    src/xiaomi/crypto.cpp \
    src/xiaomi/xiaomichannel.cpp \
    src/xiaomi/activityfetcher.cpp \
    src/xiaomi/activityparser.cpp \
    src/xiaomi/dataupload.cpp

HEADERS += \
    src/appsettings.h \
    src/wearablechannel.h \
    src/pinetimechannel.h \
    src/mprisbridge.h \
    src/bluezmanager.h \
    src/devicesmodel.h \
    src/storage.h \
    src/notificationdaemon.h \
    src/xiaomi/crypto.h \
    src/xiaomi/proto.h \
    src/xiaomi/xiaomichannel.h \
    src/xiaomi/activityfetcher.h \
    src/xiaomi/activityparser.h \
    src/xiaomi/dataupload.h

LIBS += -lcrypto

# libdbus-1 для eavesdrop на сессионной шине (Qt 5.6 это не умеет)
INCLUDEPATH += /usr/include/dbus-1.0 /usr/lib/dbus-1.0/include
LIBS += -ldbus-1

# systemd user-юнит демона (валидатор regular-профиля запрещает установку
# в /usr/lib/systemd/user — кладём в данные приложения, GUI копирует его в
# ~/.config/systemd/user при включении переключателя)
daemon_unit.files = ru.nighteugene.aurorafitness-daemon.service
daemon_unit.path = /usr/share/ru.nighteugene.aurorafitness
INSTALLS += daemon_unit

AURORAAPP_ICONS = 86x86 108x108 128x128 172x172

DISTFILES += \
    qml/AuroraFitness.qml \
    qml/pages/AboutPage.qml \
    qml/pages/MainPage.qml \
    qml/pages/DevicePage.qml \
    qml/pages/SettingsPage.qml \
    qml/pages/GoalsPage.qml \
    qml/cover/DefaultCoverPage.qml \
    ru.nighteugene.aurorafitness.desktop \
    ru.nighteugene.aurorafitness-daemon.service
