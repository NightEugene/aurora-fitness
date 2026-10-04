# aurora-fitness

Фитнес-приложение для ОС Аврора (Aurora OS 5.2) с поддержкой Mi Band 8.
Qt 5.6.3 / C++ / QML (Aurora.Controls), BLE через BlueZ D-Bus.

## Сборка и деплой

- `./build.sh` — сборка в docker-образе `aurora-build-tools-nighteugene:5.2.1.200`
  (Qt 5.6.3! обёртки в `tools/`, сборка сразу под aarch64/armv7hl/x86_64,
  RPM в `build-docker-<arch>/RPMS/`).
- `./build.sh --deploy` — scp на устройство + `sdk-deploy-rpm --silent`
  от defaultuser. Установка приложений ТОЛЬКО от defaultuser.
- Устройство Fplus MP-67A27: `ssh defaultuser@192.168.2.15` (USB; приложения,
  установка ТОЛЬКО от него), `ssh root@192.168.2.15` (journalctl/rfkill).
  По Wi-Fi тоже можно: `DEVICE=defaultuser@<wifi-ip> ./build.sh --deploy`.
  Wi-Fi-адрес выдаётся по DHCP (сейчас 192.168.88.32, смотреть на устройстве:
  `grep -B2 "host LOCAL" /proc/net/fib_trie | grep -oE "([0-9.]+)" | sort -u`).
  Нюанс: Wi-Fi устройства засыпает — с хоста Host Unreachable, пока устройство
  само не пошлёт трафик (`busybox ping 192.168.88.24` с устройства будит ARP).
  Логи: Qt собран с journald — stdout/stderr CLI-режимов молчат ВСЕГДА,
  читать журнал:
  `ssh root@192.168.2.15 'journalctl --no-pager _COMM=ru.nighteugene. --since "-3min"'`.
- GUI-приложение в песочнице (sailjail): stdout НЕ попадает в journald.
  GUI-отладка:
  `ssh defaultuser@192.168.2.15 'XDG_RUNTIME_DIR=/run/user/100000 WAYLAND_DISPLAY=/run/display/wayland-0 /usr/bin/ru.nighteugene.aurorafitness [--qml Страница.qml | --grab /tmp/x.png 12]'`
  QML-ошибки видны в stdout; `--grab` пишет скриншот окна (фон белый/чёрный —
  артефакт запуска без lipstick, тема реально тёмная).
- После каждого деплоя перезапускать демон:
  `ssh defaultuser@192.168.2.15 'systemctl --user restart ru.nighteugene.aurorafitness-daemon'`.
  Юнит ставится вручную (валидатор regular запрещает /usr/lib/systemd/user в
  пакете): лежит в `/usr/share/ru.nighteugene.aurorafitness/`, скопирован в
  `~/.config/systemd/user/`, `enable --now` сделан.
- FileConflict при установке = ручные правки в
  `/usr/share/ru.nighteugene.aurorafitness/` на устройстве →
  `ssh root@192.168.2.15 'rm -rf /usr/share/ru.nighteugene.aurorafitness'` и повторить деплой.

## Подпись релизного RPM

- Релизный ключ и сертификат разработчика лежат вне репозитория (путь спросить
  у пользователя; в git не заносить). Ключ зашифрован — пароль передаётся
  через env KEY_PASSPHRASE. `rpmsign-external` есть только внутри docker-образа
  build-tools.
- apptool при сборке подписывает RPM ключом SDK — перед подписью своим ключом
  СНАЧАЛА УДАЛИТЬ старую подпись, иначе `sign` молча падает с exit=1
  (`--force` тоже работает, но надёжнее delete → sign):

  ```
  docker run --rm -e KEY_PASSPHRASE='...' \
    -v "$PWD/build-docker-aarch64/RPMS:/rpms" -v "<каталог_ключей>:/keys:ro" \
    aurora-build-tools-nighteugene:5.2.1.200 sh -c '
      rpmsign-external delete /rpms/pkg.rpm &&
      rpmsign-external sign --key /keys/key.pem \
        --cert /keys/cert.pem /rpms/pkg.rpm'
  ```
- Проверка: `rpmsign-external dump pkg.rpm` — Subject должен быть разработчика,
  а не SDK.

## CLI-режимы бинаря

`--scan N`, `--read MAC`, `--auth MAC KEY`, `--sync MAC KEY`,
`--notify MAC KEY title [body]`, `--daemon [MAC] [key]`, `--dump-stats`,
`--qml`, `--grab`.

## Браслет Mi Band 8

- MAC `D0:62:2C:CF:19:02`, auth key `688ffad320bc47fc6ca4352bf138cac9`
  (сохранён в QSettings на устройстве; ключ добывается из логов Mi Fitness,
  grep encryptKey).
- Первый BLE-connect часто падает (`le-connection-abort-by-local`) — повторить.
  Если BT off: `ssh root@192.168.2.15 'rfkill unblock bluetooth'`.

## Протокол Mi Band 8 (реализован, работает)

- Канал: сервис FE95, notify 0x0051, write 0x0052. Проприетарная
  фрагментация: кадры `00 00 02 <enc>`, ACK `00 00 03 00`, chunked
  `00 00 00/01`, end-ack шлёт получатель после numChunks чанков.
- Auth: plaintext protobuf Command{1,26/27} (nonce 16B → HMAC-SHA256 →
  HKDF-expand "miwear-auth" → 2 ключа AES + 2 nonce4), дальше AES-128-CCM
  (тег 4B, nonce = nonce4 || u32(0) || u32(counter LE), счётчик с 1,
  входящие всегда counter=0).
- После auth: set time (type=2 subtype=3), battery (2/1), device info (2/2).
  Стандартная характеристика 0x2A19 у MB8 — заглушка (0), батарея читается
  только по зашифрованному каналу.
- Уведомления: Command type=7 subtype=0 (9 — это поле notification в Command,
  НЕ type). Внутри Notification{3=Notification2{1=Notification3{package=1,
  appName=2, title=3, body=5, timestamp=6 "yyyyMMdd'T'HHmmss", id=7}}}.
  Браслет отвечает subtype=16 (ICON_QUERY) → шлём reply (subtype=15,
  Notification.notificationIconReply=14, эхо package) → браслет шлёт
  ICON_REQUEST (subtype=15, status/pixelFormat/size) → заливаем иконку
  через DataUpload (тип 50, payload `0x00+type+md5+size+data+crc32`,
  части по chunkSize-4, каждая — chunked-передача на 0x0055 с шифрованием
  counter=0). РАБОТАЕТ. Историческая грабля: ACK-и браслета на 0x0055
  (`00 00 01 01` и т.п.) отбрасывались фильтром onCharacteristicValue —
  m_uploadPath должен быть в списке разрешённых путей.
- Sync активности: списки файлов — health-команды type=8 (subtype 1/2 —
  списки today/past, 3 — запрос файла, 5 — ACK), файлы стримятся на 0x0053
  ТОЖЕ в кадрах `00 00 02 01` с шифрованием (чанки
  `<total:u16le><num:u16le><data>`, в конце CRC32-IEEE). ACK файла удаляет
  его из очереди браслета.
- daily summary v5 у Mi Band 8 = header 4 байта + 30 слотов (тело 53 байта,
  без 2 reserved-слотов Mi Band 10). Timestamp summary = момент генерации
  файла → в БД ключ нормализуется к локальной полуночи + миграция-дедуп.
- «Время активности» (как на браслете) = число strength-минут: в daily
  details v3 третья группа (8 бит, в Gadgetbridge «TODO») == 0x60 маркирует
  минуту средне-высокой активности. Сверено с MHStrengthRecord в логах
  Mi Fitness — 17/17 минут совпали. Парсер считает strengthMinutes,
  Storage пишет в daily_summary.activity_min. НЕ путать со слотом 29
  daily summary — там activityDuration из Mi Fitness (другая метрика);
  слот 28 = activityType, слот 27 = totalVitality (очки).
- Standing (slot 12, 24-битная маска часов) парсер пока отбрасывает
  (`r.skip(3)`) — посчитать биты, если понадобится карточка «время на ногах».
- Дампы сырых файлов пишутся в `~/activity_dumps/` на устройстве
  (отладочный код в activityfetcher.cpp).
- Спека собрана из Gadgetbridge: XiaomiBleProtocolV1 / XiaomiCharacteristicV1
  / XiaomiAuthService / activity/impl.

## Платформа Аврора (проверено на устройстве)

- **Хранение настроек и БД**: песочница sailjail видит только whitelist
  firejail — среди прочего «dotted»-каталог `~/.config/ru.nighteugene.aurorafitness/`,
  но НЕ файл `~/.config/ru.nighteugene/aurorafitness.conf`. APM при
  переустановке затирает `~/.config/<org>/<app>/`, AppDataLocation И
  `/srv/shared/<org>/<app>/` (проверено маркерами), а dotted-каталог
  ПЕРЕЖИВАЕТ переустановку. Поэтому conf и БД лежат в
  `~/.config/ru.nighteugene.aurorafitness/` (единый путь через
  `app/src/appsettings.h`: `appSettings()/appConfigDir()` — QSettings() по
  умолчанию в песочнице резолвится в затираемый каталог, НЕ использовать).
  Миграция со старых путей — в `appSettingsEnsureDir()` и `Storage::open()`.
- Перехват системных уведомлений: eavesdrop на сессионной шине через
  libdbus-1 (dbus_bus_add_match eavesdrop='true'). Грабля: lipstick
  дублирует каждый Notify как вызов к ru.auroraos.Notifications — фильтровать
  по destination == org.freedesktop.Notifications.
- Арбитраж владения браслетом: имя `ru.nighteugene.aurorafitness.band` на
  сессионной шине. Демон просит его с ALLOW_REPLACEMENT (в очереди, пока
  жив GUI); GUI и CLI (--read/--auth/--sync/--notify) захватывают через
  прямой RequestName с флагами 3 (Qt 5.6 `registerService` на этой сборке
  молча возвращает false — НЕ использовать). Демон по NameLost отключается
  и СНОВА встаёт в очередь (request_name без DO_NOT_QUEUE — иначе после
  закрытия GUI имя остаётся ничьим и демон молчит до рестарта).
  Пока браслетом владеет GUI, демон НЕ молчит, а передаёт каждое
  уведомление вызовом `forwardNotification` на `ru.nighteugene.aurorafitness.gui`
  (/notify, Q_CLASSINFO-интерфейс) — GUI поднимает это имя в relay-режиме
  NotificationDaemon (без eavesdrop: сессионная шина песочницы идёт через
  xdg-dbus-proxy, eavesdrop там недоступен). Подключение GUI/CLI — через
  `BluezManager::connectToBandWhenFree`
  (Disconnect демона асинхронен, иначе его обрыв линка попадает в середину
  чужого Connect). Песочница sailjail владение именем НЕ блокирует
  (проверено: `sailjail -p ...desktop -- /usr/bin/...`).
- BlueZ на устройстве эхом отражает наши WriteValue как
  PropertiesChanged(Value) даже на write-char 0x0052, А шина/QtDBus
  доставляет каждый сигнал Value в слот дважды (dbus-monitor видит по
  одному). Лечится в XiaomiChannel: фильтр собственных записей
  (m_ownWrites) + дедуп одинаковых (path,value) в окне 200 мс.
- Все WriteValue — через FIFO-очередь в XiaomiChannel (asyncCall, ретрай
  «In Progress» через 100 мс до 10 раз). Без очереди BlueZ теряет записи
  и первый файл синка падает по таймауту.

## Архитектура

- `app/src/bluezmanager.*` — BlueZ D-Bus, Q_PROPERTY для QML (userStatus с
  фильтром служебных статусов, connectedDeviceName, цели из QSettings),
  autoConnectLast() при старте GUI.
- `app/src/xiaomi/`: proto.h (мини-proto2), crypto.cpp (OpenSSL CCM/HMAC),
  xiaomichannel.* (транспорт+auth+уведомления+иконки), activityfetcher.*,
  activityparser.*, dataupload.*.
- `app/src/storage.*` — SQLite (4 таблицы).
- `app/src/notificationdaemon.*` — демон уведомлений.
- QML: `MainPage` (статистика: кольцо шагов, карточки 2×3, график 7 дней,
  AppBar из `Aurora.Controls 1.0`, pull-to-refresh через
  `boundsBehavior: DragAndOvershootBounds` + `contentHeight: max(column.height, height+1)`),
  `SettingsPage` (сканирование, свитчи демона, тап по устройству = подключение),
  `DevicePage`, `GoalsPage` (шаги 2000–50000 дефолт 10000, ккал 200–5000
  дефолт 500, активность 10–180 дефолт 30),
  `cover/DefaultCoverPage.qml` (шаги/ккал/активность с полосками прогресса).

## Грабли Qt 5.6

- Нет `QDate::startOfDay`, `Column.bottomPadding`, `ctx.reset()`.
- SectionHeader по умолчанию выравнен ВПРАВО — нужен
  `horizontalAlignment: Text.AlignLeft`.

## Логи

Android-логи Mi Fitness (для изучения протокола) — в `logs/`.
