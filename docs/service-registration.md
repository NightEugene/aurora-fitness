# Автоматическая установка фоновой службы

## Рабочая схема — 09.10.2026

RPM включает юнит `ru.nighteugene.aurorafitness-background.service` в данные
приложения. При обычном запуске GUI в песочнице BandProxy сохраняет юнит в
`~/.config/ru.nighteugene.aurorafitness/` и обращается к пользовательскому
systemd по сессионной D-Bus-шине. Разрешение AppLaunch («Пользовательские службы»)
из desktop-файла предоставляет доступ к `org.freedesktop.systemd1`.

Последовательно выполняются EnableUnitFiles с абсолютным путём, отключение
старого ручного юнита через DisableUnitFiles, Reload и StartUnit/RestartUnit.
Ссылки автозапуска создаёт systemd вне песочницы. SHA-256 бинаря и юнита
сохраняется после успешного запроса запуска; изменение версии требует перезапуска.
Вызовы асинхронные с таймаутом 15 секунд, ошибки доступны в статусе GUI и
`~/.config/ru.nighteugene.aurorafitness/service-install.log`.

Новый юнит конфликтует со старым `ru.nighteugene.aurorafitness-daemon.service`:
старый демон останавливается при запуске нового. Пересылка уведомлений остаётся
вне песочницы, GUI использует прежний D-Bus API `/band`.

Проверено на Fplus MP-67A27 без старого ручного юнита: обычный запуск через
invoker с песочницей создал службу, systemd вернул active/enabled и путь к
юниту в dotted-каталоге. D-Bus API браслета вернул ready=true, батарею 75% и
прошивку 2.3.14. Проверен переход с работающего ручного юнита: после обновления
он disabled/inactive, новый enabled/active. После закрытия и повторного открытия
GUI PID демона сохранился, журнал содержит StartUnit вместо RestartUnit.
Окончательные RPM собраны для aarch64, armv7hl, x86_64 и подписаны сертификатом
NightEugene. Подтверждение стандартных разрешений при первом запуске требуется.

## Почему не используются RPM-скрипты

APM отклоняет RPM-скрипты даже с выключенной валидацией. Юнит из системного
каталога внутри RPM остаётся в контейнере `/opt/app` и не виден пользовательскому
systemd. В GUI также нет исполняемого systemctl. Поэтому используется штатный
D-Bus API systemd с разрешением AppLaunch; копирование файлов в закрытые каталоги
и RPM-скрипты не нужны.

## Результат исследования 06.10.2026

`ExecDBus` уже указан в нашем desktop-файле и обрабатывается ОС. Это
активация приложения по D-Bus, а не установка постоянно работающего
неизолированного user-демона. Заменять текущую службу этим механизмом
без изменения архитектуры нельзя.

## Проверено на Fplus MP-67A27

- Исходный desktop: `ExecDBus=/usr/bin/ru.nighteugene.aurorafitness`.
- ОС создала `/run/user/100000/dbus-1/services/ru.nighteugene.aurorafitness.service`.
  Внутри `Name=ru.nighteugene.aurorafitness`, а Exec вызывает RuntimeManager
  `InvokeIntent Start` с `preferredHandler=ru.nighteugene.aurorafitness`
  и `startDBus=true`. Отдельного одноимённого systemd user-юнита нет.
- Наш API браслета использует другое имя: `ru.nighteugene.aurorafitness.band`.
  Для него этот файл не обеспечивает активацию.
- Попытка запуска через invoker с `--daemon` отклонена sailjail:
  `Command line does not match templates`. В текущем desktop эта команда
  не объявлена. Это не доказывает, что разрешённый шаблон демона не запустится.
- В журнале invoker явно указано `enforcing sandboxing`.
- BaseTask.permission включает фильтр сессионной D-Bus-шины.
  Notifications.permission разрешает talk и broadcast для Notifications,
  но не содержит разрешения перехватывать чужие вызовы Notify.
  Поэтому перенос текущего eavesdrop-перехвата в такую службу не подтверждён
  и противоречит ранее проверенному ограничению песочницы.
- Текущая служба не останавливалась, файлы на устройстве не изменялись.

## Что потребуется для дальнейшего прототипа

Отдельно проверить объявленный шаблон ExecDBus с `--daemon`, владение
базовым D-Bus-именем, совместный запуск GUI и демона, жизненный цикл через
RuntimeManager и поддерживаемый способ получения чужих уведомлений.
Само добавление ExecDBus не обеспечивает запуск после входа пользователя.

ExecSystemService — другой механизм, введённый для профиля MDM. Наличие
его в документации не подтверждает доступность с нашим сертификатом regular.
Переход на него требует отдельной проверки актуального профиля SDK.

## Источники

- [Desktop и ExecDBus, Аврора 5.2.1](https://developer.auroraos.ru/doc/software_development/guidelines/rpm_requirements/desktop_requirements)
- [Экспорт интерфейсов D-Bus](https://developer.auroraos.ru/doc/software_development/guides/d_bus/d_bus)
- [Введение ExecSystemService для MDM](https://developer.auroraos.ru/release_notes/os_4.0.2.89)
