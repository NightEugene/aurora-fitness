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
  counter=0). РАБОТАЕТ. Иконки разные для каждого приложения: package =
  id источника (hint x-aurora-application-id, иначе поиск локализованного
  имени по /usr/share/applications/*.desktop — resolveAppPackage; алиасы:
  «Система» → __system, «Пропущенные оповещения календаря» → ru.omp.calendar).
  Поиск иконки (iconCandidatePaths): hicolor/*/apps/<id>.png → Icon= из
  desktop-файла в теме aurora-default (системные приложения OMP) →
  __system=icon-m-setting, __unknown=icon-m-question; для прочих пакетов
  без иконки на ICON_QUERY НЕ отвечаем (как Gadgetbridge) — браслет
  показывает свою иконку по умолчанию. Своя иконка подставляется ТОЛЬКО
  для своего пакета (никогда не fallback).
  ХРАНИЛИЩЕ ИКОНОК БРАСЛЕТА — КРИТИЧНО (выяснено опытным путём 05.10):
  ~6 слотов, дедупа по md5 НЕТ (один и тот же md5 принимается повторно
  и ест новый слот), errno=1 в DataUploadAck = «нет места»; таблица
  чистится ТОЛЬКО ребутом браслета. Поэтому: никакой соли в package
  (каждая соль = новый набор пакетов = вся таблица), никакой уникализации
  контента (md5 ни на что не влияет). Загружаем запрошенные 28/44px,
  каждый размер один раз за сессию (m_iconServed хранит package:size),
  80px пропускаем. Вывод «28px достаточно для всех» оказался неверным:
  у нашего приложения после 28px оставался пузырёк и запросы 44px.
  06.10 загрузка 44px прошла с errno=0; после неё, в том числе после
  рестарта демона, браслет запрашивает только 80px. Визуальный результат
  ещё требует подтверждения пользователем. Если иконка
  не найдена — молчим, чтобы не жечь слоты на мусорные пакеты.
  Браслет переспрашивает иконку на каждое уведомление, пока привязка
  package→иконка не создана; после успешной загрузки перестаёт спрашивать.
  Браслетом всегда владеет демон (GUI ходит по D-Bus) — иконки грузятся
  независимо от того, открыт ли GUI.
  Кэш иконок (~/.config/ru.nighteugene.aurorafitness/icons/<pkg>.png)
  пишется один раз (cacheIcon пропускает существующие) — испорченный
  файл отравляет пакет до ручного удаления; при подозрениях чистить
  вручную.
  Историческая грабля: ACK-и браслета на 0x0055
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
- Sleep details (ACTIVITY/8, DETAILS v2): файл — НАКОПИТЕЛЬНЫЙ снапшот ночи:
  содержит серию пакетов type 16 (summary) и type 17 (фазы), каждый следующий
  полнее, причём браслет ПЕРЕСМАТРИВАЕТ раннюю сегментацию. Склеивать записи
  всех пакетов нельзя (шкала задваивается — был такой баг): берём только
  последний непустой пакет type 17. В записи u16be: верхний ниббл — фаза,
  нижние 12 бит — длительность ЭТОЙ фазы в минутах. Контроль: агрегация фаз
  (с хвостом до wakeTime) должна сойтись с последним type-16 summary
  (light/deep/rem/wake). Короткие бодрствования по 1 мин — реальные данные
  браслета (совпадают с wake_count/wake_min из summary), не баг парсера.
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
  сессионной шине. Его ВСЕГДА держит демон; GUI имя не трогает и BLE не
  трогает — работает через D-Bus API демона (`BandService`, см. ниже).
  Отбирают имя только CLI-режимы (--read/--auth/--sync/--notify) — прямым
  RequestName с флагами 3 (Qt 5.6 `registerService` на этой сборке молча
  возвращает false — НЕ использовать). Демон по потере имени отключается
  и СНОВА встаёт в очередь (без DO_NOT_QUEUE), по возврату —
  переподключается. Подключение CLI — через `BluezManager::connectToBandWhenFree`
  (Disconnect демона асинхронен, иначе его обрыв линка попадает в середину
  чужого Connect). Песочница sailjail владение именем НЕ блокирует.
  ГРАБЛЯ: имя запрашивается через Qt-соединение (`requestBandNameQt` в
  notificationdaemon.cpp), а не через raw libdbus — D-Bus объект /band живёт
  на Qt-соединении, вызовы по well-known имени идут владельцу имени.
  Соответственно за именем следим по широковещательному NameOwnerChanged
  (NameAcquired/NameLost — юникаст в Qt-соединение, на raw-соединении
  eavesdrop-а их не видно).
- D-Bus API демона (`app/src/bandservice.*`): сервис/интерфейс
  `ru.nighteugene.aurorafitness.band`, путь `/band`. Метод `getState()` →
  a{sv} со всем состоянием (scanning/status/ready/bandInfo/devices/...),
  сигнал `stateChanged(a{sv})` (со схлопыванием 200 мс; массивы samples/stages
  в activityResults заменены списком нулей той же длины — QVariant() шина
  не маршалит), сигналы `activitySyncStarted/Finished`, `deviceError`,
  методы startScan/stopScan/connectToBand/disconnectBand/syncActivity/
  startBandAuth/setAuthKey/sendTestNotification. Каждый вызов логируется
  (песоченый GUI в журнал не пишет) и дёргает reloadSettings демона.
  Снапшот содержит dataRevision: счётчик Storage::dataChanged демона.
  BandProxy по его изменению вызывает Storage::refresh() GUI (перечитать
  настройки выбранного устройства и уведомить QML), без записи в БД.
  Так обложка/карточки обновляются при фоновом синке и чтении батареи.
- GUI: `app/src/bandproxy.*` — context property `bluez` с теми же
  именами property/методов/сигналов, что BluezManager. Локально (без шины):
  цели, профиль, вид карточек, свитчи демона (appSettings). Переключатели
  notify/sync не останавливают службу: демон нужен GUI независимо от них.
  После сохранения свитча вызывается getState (invoked → reloadSettings).
  Цели на ползунках сохраняются после отпускания; изменение цели обновляет
  графику без перечитывания SQLite. Маркеры переполнения — полукруги из
  qml/ProgressMarkers.js, каждый ориентирован по касательной к кольцу.
  По шине — всё
  BLE-состояние и действия. Вложенные QVariantMap/List из D-Bus нужно
  рекурсивно распаковывать из QDBusArgument/QDBusVariant: toMap()/toList()
  напрямую дают пустые контейнеры (батарея/прошивка/сервисы пропадали).
  Следит за именем (QDBusServiceWatcher): демона
  нет → status «Служба браслета не запущена» + попытка systemctl --user start
  (копирование юнита из песочницы запрещено — сообщение с ручной командой).
- BlueZ на устройстве эхом отражает наши WriteValue как
  PropertiesChanged(Value) даже на write-char 0x0052, А шина/QtDBus
  доставляет каждый сигнал Value в слот дважды (dbus-monitor видит по
  одному). Лечится в XiaomiChannel: фильтр собственных записей
  (m_ownWrites) + дедуп одинаковых (path,value) в окне 200 мс.
- Все WriteValue — через FIFO-очередь в XiaomiChannel (asyncCall, ретрай
  «In Progress» через 100 мс до 10 раз). Без очереди BlueZ теряет записи
  и первый файл синка падает по таймауту.

## Архитектура

- `app/src/bluezmanager.*` — BlueZ D-Bus; живёт в демоне и CLI-режимах
  (GUI его больше не создаёт). userStatus с фильтром служебных статусов,
  connectedDeviceName, цели из QSettings.
- `app/src/bandservice.*` — D-Bus API демона для GUI (см. раздел
  «Платформа Аврора»), `app/src/bandproxy.*` — его клиент в GUI
  (context property `bluez`).
- `app/src/xiaomi/`: proto.h (мини-proto2), crypto.cpp (OpenSSL CCM/HMAC),
  xiaomichannel.* (транспорт+auth+уведомления+иконки), activityfetcher.*,
  activityparser.*, dataupload.*.
- `app/src/storage.*` — SQLite (7 таблиц; отдельная БД на устройство).
- `app/src/notificationdaemon.*` — демон: перехват уведомлений (eavesdrop),
  автосинк, арбитраж имени браслета.
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

## Документация Авроры (MCP)

- MCP-сервер документации: https://developer.auroraos.ru/api/mcp (инструменты:
  `search`, `search_code`, `get_document`, `get_code_source`, `get_doc_versions`,
  `get_ext_tools_list`). WAF режет не-браузерный User-Agent — слать браузерный
  заголовок. Если MCP-инструменты недоступны в сессии, ходить curl'ом:
  POST JSON-RPC (initialize → notifications/initialized → tools/call), Accept:
  `application/json, text/event-stream`, сохранять `Mcp-Session-Id` из заголовков.
  `get_document` работает не для всех разделов («Раздел undefined не
  поддерживает get_document») — тогда читать страницу напрямую по URL из search.
- Например, так найден нативный `PullToRefresh` (attached-свойства Aurora.Controls
  1.0: refreshHandler, refreshCompleted[Custom]) — он же есть на устройстве в
  `/usr/lib/qt5/qml/Aurora/Controls/private/`.

## Логи

Android-логи Mi Fitness (для изучения протокола) — в `logs/`.
