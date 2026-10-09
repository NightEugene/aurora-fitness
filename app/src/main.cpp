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
#include "bandservice.h"
#include "bandproxy.h"
#include "storage.h"
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
    return reply.isValid() && (reply.value() == 1 || reply.value() == 4); // PRIMARY_OWNER / ALREADY_OWNER
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
        // CLI-режимы, работающие с браслетом, отбирают имя у демона —
        // демон отпустит BLE-линк по NameLost. Подключение —
        // через connectToBandWhenFree: Disconnect демона асинхронен, его
        // обрыв линка не должен попасть в середину нашего Connect.
        if (cliArgs.contains(QStringLiteral("--read")) || cliArgs.contains(QStringLiteral("--auth"))
                || cliArgs.contains(QStringLiteral("--sync"))
                || cliArgs.contains(QStringLiteral("--notify"))) {
            const bool owned = requestBandName();
            if (!owned) {
                qWarning() << "Не удалось получить владение браслетом";
                return 1;
            }
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
                mac = appSettings().value(QStringLiteral("device/lastAddress"), appSettings().value(QStringLiteral("miband8/lastAddress"))).toString();
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
            // D-Bus API для GUI: снапшот состояния + команды к браслету
            BandService bandApi(&manager);
            if (!bandApi.start()) return 1;
            // Каждый входящий D-Bus вызов — повод перечитать настройки
            QObject::connect(&bandApi, &BandService::invoked,
                             &daemon, &NotificationDaemon::reloadSettings);
            // Новый ключ авторизации (ввели в GUI) — переподключиться к браслету
            QObject::connect(&bandApi, &BandService::authKeyChanged, &daemon,
                             [&manager, &daemon]() {
                manager.disconnectBand();
                QTimer::singleShot(1500, &daemon,
                                   [&daemon]() { daemon.ensureBandConnected(); });
            });
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
            bool sent = false;
            QObject::connect(&manager, &BluezManager::bandReadyChanged, app.data(), [&]() {
                if (!manager.bandReady() || sent)
                    return;
                sent = true;
                manager.sendTestNotification(title, body,
                                             QStringLiteral("Аврора Фитнес"),
                                             QStringLiteral("ru.nighteugene.aurorafitness"));
                QTimer::singleShot(15000, app.data(), &QCoreApplication::quit);
            });
            QObject::connect(&manager, &BluezManager::deviceError, app.data(),
                             [&](const QString &msg) {
                qWarning() << "Ошибка операции:" << msg;
                app->exit(1);
            });
            QTimer::singleShot(500, [&]() { manager.connectToBandWhenFree(cliArgs.at(idx + 1)); });
            QTimer::singleShot(90000, app.data(), [&]() { app->exit(2); });
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
            bool started = false;
            QObject::connect(&manager, &BluezManager::bandReadyChanged, app.data(), [&]() {
                if (!manager.bandReady() || started)
                    return;
                started = true;
                manager.syncActivity();
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
                qWarning() << "Ошибка операции:" << msg;
                app->exit(1);
            });
            QTimer::singleShot(500, [&]() { manager.connectToBandWhenFree(cliArgs.at(idx + 1)); });
            QTimer::singleShot(300000, app.data(), [&]() { app->exit(2); });
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
                qWarning() << "Ошибка операции:" << msg;
                app->exit(1);
            });
            QTimer::singleShot(500, [&]() { manager.connectToBandWhenFree(cliArgs.at(idx + 1)); });
            QTimer::singleShot(90000, app.data(), [&]() { app->exit(2); });
            return app->exec();
        }
        if (cliArgs.contains(QStringLiteral("--scan"))) {
            int secs = 10;
            const int idx = cliArgs.indexOf(QStringLiteral("--scan"));
            if (idx + 1 < cliArgs.size())
                secs = cliArgs.at(idx + 1).toInt();
            if (secs < 1 || secs > 3600) {
                qWarning() << "Продолжительность сканирования должна быть от 1 до 3600 секунд";
                return 1;
            }
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
                         [&](const QString &) { app->exit(1); });
        QTimer::singleShot(500, [&]() { manager.connectToBandWhenFree(cliArgs.at(idx + 1)); });
        QTimer::singleShot(60000, app.data(), [&]() { app->exit(2); });
        return app->exec();
    }

    QScopedPointer<QGuiApplication> application(Aurora::Application::application(argc, argv));
    application->setOrganizationName(QStringLiteral("ru.nighteugene"));
    application->setApplicationName(QStringLiteral("aurorafitness"));

    // GUI не трогает BLE и не захватывает имя браслета: браслетом всегда
    // владеет демон, GUI ходит к нему по D-Bus через BandProxy (для QML —
    // тот же интерфейс, что у BluezManager). Storage — локальный (sqlite).
    Storage storage;
    BandProxy proxy(&storage);
    QScopedPointer<QQuickView> view(Aurora::Application::createView());
    view->rootContext()->setContextProperty(QStringLiteral("bluez"), &proxy);
    view->rootContext()->setContextProperty(QStringLiteral("storage"), &storage);

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
