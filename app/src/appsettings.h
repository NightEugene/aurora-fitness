// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <QDir>
#include <QFile>
#include <QSettings>

// Единое хранилище настроек и БД для GUI (песочница sailjail), CLI-режимов
// и демона.
//
// Песочница видит только whitelist firejail: ~/.config/ru.nighteugene.aurorafitness/
// (именно «dotted»-каталог, не ~/.config/<org>/<app>/), ~/.local/share/... и т.п.
// При этом APM при переустановке затирает ~/.config/<org>/<app>/, AppDataLocation
// и /srv/shared/<org>/<app>/, а «dotted»-каталог переживает переустановку.
// QSettings() по умолчанию в песочнице резолвится в затираемый
// ~/.config/<org>/<app>/ — поэтому путь задан явно.
// Идентификатор пакета используется как компонент пути к иконке.
inline bool safeIconPackage(const QString &package)
{
    return !package.isEmpty() && package != QLatin1String(".")
            && package != QLatin1String("..") && !package.contains(QLatin1Char('/'))
            && !package.contains(QLatin1Char('\\')) && !package.contains(QChar(0));
}

inline QString appConfigDir()
{
    return QDir::homePath() + QStringLiteral("/.config/ru.nighteugene.aurorafitness");
}

inline QString appSettingsPath()
{
    return appConfigDir() + QStringLiteral("/aurorafitness.conf");
}

inline QSettings appSettings()
{
    return QSettings(appSettingsPath(), QSettings::IniFormat);
}

inline QString lastSyncSettingsKey(QString address)
{
    address.remove(QLatin1Char(':'));
    return QStringLiteral("sync/lastSuccess_") + address.toLower();
}

// Вызывать при старте (все режимы): каталог нужен до первой записи,
// а миграция переносит настройки из дореформенного расположения
// (~/.config/ru.nighteugene/aurorafitness.conf), куда писали непесоченные
// процессы. В песочнице mkpath может молча не сработать (tmpfs HOME) —
// тогда каталог создаст демон/CLI без песочницы.
inline void appSettingsEnsureDir()
{
    QDir().mkpath(appConfigDir());
    const QString newPath = appSettingsPath();
    const QString oldPath = QDir::homePath()
            + QStringLiteral("/.config/ru.nighteugene/aurorafitness.conf");
    if (!QFile::exists(newPath) && QFile::exists(oldPath))
        QFile::copy(oldPath, newPath);
}
