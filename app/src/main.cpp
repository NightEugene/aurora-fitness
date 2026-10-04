// SPDX-License-Identifier: BSD-3-Clause

#include <auroraapp.h>
#include <QtQuick>
#include <QCoreApplication>
#include <QTimer>
#include <QSettings>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusInterface>
#include <QDBusReply>

#include "bluezmanager.h"
#include "notificationdaemon.h"
#include "mprisbridge.h"
#include "appsettings.h"

namespace {
// Арбитраж владения браслетом: кто держит это имя на сессионной шине,
// тот и работает с BLE-линком (см. notificationdaemon.cpp)
const char BAND_NAME[] = "ru.nighteugene.aurorafitness.band";

// registerService() Qt 5.6 на этой сборке молча возвращает false —
// зовём RequestName напрямую (flags = AllowReplacement | ReplaceExisting)
bool requestBandName()
{
    QDBusInterface dbusIface(QStringLiteral("org.freedesktop.DBus"),
                             QStringLiteral("/org/freedesktop/DBus"),
                             QStringLiteral("org.freedesktop.DBus"),
                             QDBusConnection::sessionBus());
    QDBusReply<uint> reply = dbusIface.call(QStringLiteral("RequestName"),
                                            QString::fromLatin1(BAND_NAME), 3u);
    if (!reply.isValid())
        qWarning() << "RequestName:" << reply.error().message();
    return reply.isValid() && reply.value() == 1; // PRIMARY_OWNER
}
}

int main(int argc, char *argv[])
{
    appSettingsEnsureDir(); // dotted-каталог + миграция conf со старого пути

    // Консольный режим для отладки по SSH:
    //   --scan [секунды]   — сканировать BLE и распечатать устройства
    //   --read <MAC>       — подключиться и распечатать сервисы/данные браслета
    //   --daemon [MAC] [hex-key] — фоновый демон: пересылка уведомлений + автосинк
    //   --dump-stats       — распечатать содержимое БД и выйти
    QStringList cliArgs;
    for (int i = 1; i < argc; ++i)
        cliArgs.append(QString::fromLocal8Bit(argv[i]));

    if (cliArgs.contains(QStringLiteral("--scan")) || cliArgs.contains(QStringLiteral("--read"))
            || cliArgs.contains(QStringLiteral("--auth")) || cliArgs.contains(QStringLiteral("--sync"))
            || cliArgs.contains(QStringLiteral("--notify"))
            || cliArgs.contains(QStringLiteral("--daemon"))
            || cliArgs.contains(QStringLiteral("--dump-stats"))) {
        QScopedPointer<QCoreApplication> app(new QCoreApplication(argc, argv));
        app->setOrganizationName(QStringLiteral("ru.nighteugene"));
        app->setApplicationName(QStringLiteral("aurorafitness"));

        BluezManager manager;
        // CLI-режимы, работающие с браслетом, отбирают имя у демона
        // (как и GUI) — демон отпустит BLE-линк по NameLost. Подключение —
        // через connectToBandWhenFree: Disconnect демона асинхронен, его
        // обрыв линка не должен попасть в середину нашего Connect.
        if (cliArgs.contains(QStringLiteral("--read")) || cliArgs.contains(QStringLiteral("--auth"))
                || cliArgs.contains(QStringLiteral("--sync"))
                || cliArgs.contains(QStringLiteral("--notify"))) {
            const bool owned = requestBandName();
            qInfo() << "D-Bus имя браслета (CLI):" << (owned ? "захвачено" : "НЕ захвачено");
        }
        if (cliArgs.contains(QStringLiteral("--daemon"))) {
            // --daemon [MAC] [hex-key] — демон пересылки уведомлений и автосинка
            const int idx = cliArgs.indexOf(QStringLiteral("--daemon"));
            QString mac;
            if (idx + 1 < cliArgs.size() && !cliArgs.at(idx + 1).startsWith(QLatin1Char('-')))
                mac = cliArgs.at(idx + 1);
            if (idx + 2 < cliArgs.size() && !cliArgs.at(idx + 2).startsWith(QLatin1Char('-')))
                manager.setAuthKey(cliArgs.at(idx + 2));
            if (mac.isEmpty())
                mac = appSettings().value(QStringLiteral("miband8/lastAddress")).toString();
            if (mac.isEmpty()) {
                qWarning() << "Использование: --daemon <MAC> [hex-key]"
                              "(либо предварительно подключитесь из GUI / другого CLI-режима)";
            }
            NotificationDaemon daemon(&manager, mac);
            MprisRelay media;
            if (!media.start())
                qWarning() << "MPRIS relay unavailable";
            if (!daemon.start())
                return 1;
            return app->exec();
        }
        if (cliArgs.contains(QStringLiteral("--notify"))) {
            // --notify <MAC> <hex-key> <заголовок> [текст]
            const int idx = cliArgs.indexOf(QStringLiteral("--notify"));
            if (idx + 3 >= cliArgs.size()) {
                qWarning() << "Использование: --notify <MAC> <hex-key> <заголовок> [текст]";
                return 1;
            }
            const QString title = cliArgs.at(idx + 3);
            const QString body = idx + 4 < cliArgs.size() ? cliArgs.at(idx + 4) : QString();
            manager.setAuthKey(cliArgs.at(idx + 2));
            QObject::connect(&manager, &BluezManager::bandBatteryReceived, app.data(), [&]() {
                // battery приходит сразу после auth — канал готов
                manager.sendTestNotification(title, body);
                // Даём время на цепочку ICON_QUERY → ICON_REPLY → ICON_REQUEST → upload
                QTimer::singleShot(15000, app.data(), &QCoreApplication::quit);
            });
            QObject::connect(&manager, &BluezManager::deviceError, app.data(),
                             [&](const QString &msg) {
                qWarning() << "=== FAILED:" << msg << "===";
                app->quit();
            });
            QTimer::singleShot(500, [&]() { manager.connectToBandWhenFree(cliArgs.at(idx + 1)); });
            QTimer::singleShot(90000, app.data(), &QCoreApplication::quit);
            return app->exec();
        }
        if (cliArgs.contains(QStringLiteral("--dump-stats"))) {
            // Headless-проверка содержимого БД по SSH
            qInfo() << "=== todaySummary ===" << manager.storage()->todaySummary();
            qInfo() << "=== dailySummaries(14) ===" << manager.storage()->dailySummaries(14);
            qInfo() << "=== sleepSessions(10) ===" << manager.storage()->sleepSessions(10);
            qInfo() << "=== minute_samples, строк:" << manager.storage()->minuteSampleCount() << "===";
            return 0;
        }
        if (cliArgs.contains(QStringLiteral("--sync"))) {
            const int idx = cliArgs.indexOf(QStringLiteral("--sync"));
            if (idx + 2 >= cliArgs.size()) {
                qWarning() << "Использование: --sync <MAC> <hex-key>";
                return 1;
            }
            manager.setAuthKey(cliArgs.at(idx + 2));
            QObject::connect(&manager, &BluezManager::deviceReady, app.data(), [&]() {
                // auth стартует автоматически; fetch — после неё
                QTimer::singleShot(3000, [&]() { manager.syncActivity(); });
            });
            QObject::connect(&manager, &BluezManager::activitySyncFinished, app.data(), [&]() {
                qInfo() << "=== SYNC DONE, файлов:" << manager.activityResults().size() << "===";
                for (const QVariant &v : manager.activityResults()) {
                    const QVariantMap m = v.toMap();
                    QVariantMap brief = m;
                    // Массивы сэмплов не печатаем целиком — только размер
                    for (const QString &k : {QStringLiteral("samples"), QStringLiteral("stages")}) {
                        if (brief.contains(k))
                            brief.insert(k, QStringLiteral("<%1 записей>").arg(brief.value(k).toList().size()));
                    }
                    qInfo() << m.value(QStringLiteral("kind")).toString() << brief;
                }
                app->quit();
            });
            QObject::connect(&manager, &BluezManager::deviceError, app.data(),
                             [&](const QString &msg) {
                qWarning() << "=== FAILED:" << msg << "===";
                app->quit();
            });
            QTimer::singleShot(500, [&]() { manager.connectToBandWhenFree(cliArgs.at(idx + 1)); });
            QTimer::singleShot(300000, app.data(), &QCoreApplication::quit);
            return app->exec();
        }
        if (cliArgs.contains(QStringLiteral("--auth"))) {
            const int idx = cliArgs.indexOf(QStringLiteral("--auth"));
            if (idx + 2 >= cliArgs.size()) {
                qWarning() << "Использование: --auth <MAC> <hex-key>";
                return 1;
            }
            manager.setAuthKey(cliArgs.at(idx + 2));
            QObject::connect(&manager, &BluezManager::bandBatteryReceived, app.data(),
                             [&](int level, int) {
                qInfo() << "=== AUTH OK, battery" << level << "% ===";
                app->quit();
            });
            QObject::connect(&manager, &BluezManager::deviceError, app.data(),
                             [&](const QString &msg) {
                qWarning() << "=== FAILED:" << msg << "===";
                app->quit();
            });
            QTimer::singleShot(500, [&]() { manager.connectToBandWhenFree(cliArgs.at(idx + 1)); });
            QTimer::singleShot(90000, app.data(), &QCoreApplication::quit);
            return app->exec();
        }
        if (cliArgs.contains(QStringLiteral("--scan"))) {
            int secs = 10;
            const int idx = cliArgs.indexOf(QStringLiteral("--scan"));
            if (idx + 1 < cliArgs.size())
                secs = cliArgs.at(idx + 1).toInt();
            manager.startScan();
            QTimer::singleShot(secs * 1000, app.data(), [&]() {
                manager.cliScanFinished();
                app->quit();
            });
            return app->exec();
        }

        const int idx = cliArgs.indexOf(QStringLiteral("--read"));
        if (idx + 1 >= cliArgs.size()) {
            qWarning() << "Использование: --read <MAC>";
            return 1;
        }
        QObject::connect(&manager, &BluezManager::deviceReady, app.data(), [&]() {
            manager.cliReadFinished();
            app->quit();
        });
        QObject::connect(&manager, &BluezManager::deviceError, app.data(),
                         [&](const QString &) { app->quit(); });
        QTimer::singleShot(500, [&]() { manager.connectToBandWhenFree(cliArgs.at(idx + 1)); });
        QTimer::singleShot(60000, app.data(), &QCoreApplication::quit);
        return app->exec();
    }

    QScopedPointer<QGuiApplication> application(Aurora::Application::application(argc, argv));
    application->setOrganizationName(QStringLiteral("ru.nighteugene"));
    application->setApplicationName(QStringLiteral("aurorafitness"));

    BluezManager manager;
    QScopedPointer<QQuickView> view(Aurora::Application::createView());
    view->rootContext()->setContextProperty(QStringLiteral("bluez"), &manager);
    view->rootContext()->setContextProperty(QStringLiteral("storage"), manager.storage());

    // GUI отбирает у демона имя браслета (RequestName ReplaceExisting) —
    // демон по NameLost отпустит BLE-линк; autoConnectLast подождёт
    // освобождения линка (connectToBandWhenFree).
    const bool bandNameOwned = requestBandName();
    qInfo() << "D-Bus имя браслета:" << (bandNameOwned ? "захвачено" : "НЕ захвачено");
    QTimer::singleShot(500, &manager, &BluezManager::autoConnectLast);

    // Пока GUI владеет браслетом, демон не шлёт уведомления сам, а передаёт
    // их сюда вызовом forwardNotification (eavesdrop из песочницы GUI
    // недоступен — на сессионной шине сидит xdg-dbus-proxy)
    NotificationDaemon relay(&manager,
            appSettings().value(QStringLiteral("miband8/lastAddress")).toString());
    relay.setRelayMode(true);
    relay.start();
    // --qml pages/Foo.qml — отладочный запуск с другой стартовой страницей
    QString initialQml = QStringLiteral("qml/AuroraFitness.qml");
    const int qmlIdx = cliArgs.indexOf(QStringLiteral("--qml"));
    if (qmlIdx >= 0 && qmlIdx + 1 < cliArgs.size()) {
        initialQml = QStringLiteral("qml/pages/%1").arg(cliArgs.at(qmlIdx + 1));
        qInfo() << "Запуск со страницей" << initialQml;
    }
    view->setSource(Aurora::Application::pathTo(initialQml));
    view->show();

    // --grab /tmp/shot.png [задержка_сек] — отладочный скриншот окна после старта
    const int grabIdx = cliArgs.indexOf(QStringLiteral("--grab"));
    if (grabIdx >= 0 && grabIdx + 1 < cliArgs.size()) {
        const QString path = cliArgs.at(grabIdx + 1);
        const int delaySec = grabIdx + 2 < cliArgs.size()
                ? cliArgs.at(grabIdx + 2).toInt() : 8;
        QTimer::singleShot(delaySec * 1000, application.data(), [&]() {
            const QImage img = view->grabWindow();
            img.save(path);
            qInfo() << "Скриншот сохранён:" << path << img.size();
            application->quit();
        });
    }
    return application->exec();
}
