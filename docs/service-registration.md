# Штатная регистрация службы: проверка 06.10.2026

## Результат

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
