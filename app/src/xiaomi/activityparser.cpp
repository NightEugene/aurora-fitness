// SPDX-License-Identifier: BSD-3-Clause

// Парсеры бинарных activity-файлов Mi Band 8, портированы из Gadgetbridge:
// service/devices/xiaomi/activity/impl/{DailySummaryParser,DailyDetailsParser,
// SleepDetailsParser,SleepStagesParser,ManualSamplesParser}.java

#include "activityparser.h"

namespace {

// Читатель с защитой от выхода за границы: при переполнении выставляет
// флаг и дальше возвращает нули.
struct Reader {
    const QByteArray &d;
    int pos = 0;
    bool overflow = false;

    explicit Reader(const QByteArray &data, int offset = 0) : d(data), pos(offset) {}

    int remaining() const { return d.size() - pos; }

    quint8 u8()
    {
        if (remaining() < 1) { overflow = true; return 0; }
        return quint8(d.at(pos++));
    }
    quint16 u16()
    {
        const quint8 lo = u8(), hi = u8();
        return quint16(lo) | (quint16(hi) << 8);
    }
    quint32 u32()
    {
        const quint32 lo = u16(), hi = u16();
        return lo | (hi << 16);
    }
    qint64 i64()
    {
        const quint64 lo = u32(), hi = u32();
        return qint64(lo | (hi << 32));
    }
    quint16 u16be() { return (quint16(u8()) << 8) | quint16(u8()); }
    void skip(int n)
    {
        if (n < 0 || remaining() < n) { overflow = true; pos = d.size(); return; }
        pos += n;
    }
};

QVariantMap unknown(const QString &reason)
{
    QVariantMap m;
    m.insert(QStringLiteral("kind"), QStringLiteral("unknown"));
    m.insert(QStringLiteral("reason"), reason);
    return m;
}

// DailySummaryParser.validData: бит i валиден, если header[i/8] & (1<<(7-(i%8)))
bool validData(const QByteArray &header, int i)
{
    const int byteIdx = i / 8;
    if (byteIdx < 0 || byteIdx >= header.size())
        return false;
    return (quint8(header.at(byteIdx)) & (1 << (7 - (i % 8)))) != 0;
}

// ---------------------------------------------------------------------------
// Daily Summary (ACTIVITY/0/SUMMARY) — DailySummaryParser.java
// ---------------------------------------------------------------------------

QVariantMap parseDailySummary(const xiaomiactivity::FileId &id, const QByteArray &payload)
{
    int headerSize, slotCount;
    switch (id.version) {
    case 3: case 4: headerSize = 3; slotCount = 21; break;
    case 5:
        headerSize = 4;
        // Mi Band 8 шлёт v5 без двух последних reserved-слотов (53 байта тела),
        // Mi Band 10+ — полные 32 слота (57 байт)
        slotCount = (payload.size() - headerSize >= 57) ? 32 : 30;
        break;
    default:
        return unknown(QStringLiteral("unsupported daily summary version %1").arg(id.version));
    }

    // Размеры слотов в байтах (см. SLOTS в DailySummaryParser.java)
    static const quint8 slotSize[32] = {
        4, 2, 1, 1, 1, 4, 1, 4, 1, 1, 1, 1, 3, 2, 2, 1,
        1, 4, 1, 4, 1, 2, 2, 1, 1, 1, 1, 2, 1, 1, 2, 2
    };

    int bodySize = 0;
    for (int i = 0; i < slotCount; ++i)
        bodySize += slotSize[i];
    if (payload.size() < headerSize + bodySize)
        return unknown(QStringLiteral("daily summary too short: %1").arg(payload.size()));

    const QByteArray header = payload.left(headerSize);
    Reader r(payload, headerSize);

    QVariantMap out;
    out.insert(QStringLiteral("kind"), QStringLiteral("dailySummary"));
    out.insert(QStringLiteral("timestamp"), qlonglong(id.timestamp));

    for (int i = 0; i < slotCount; ++i) {
        const bool valid = validData(header, i);
        const char *key = nullptr;
        qlonglong value = 0;
        switch (i) {
        case 0:  value = qint32(r.u32()); key = "steps";     break;
        case 1:  r.u16(); break; // active calories
        case 2:  r.u8();  break; // reserved
        case 3:  value = r.u8();  key = "restingHr"; break;
        case 4:  value = r.u8();  key = "maxHr";     break;
        case 5:  r.u32(); break; // max HR ts
        case 6:  value = r.u8();  key = "minHr";     break;
        case 7:  r.u32(); break; // min HR ts
        case 8:  value = r.u8();  key = "avgHr";     break;
        case 9:  value = r.u8();  key = "stressAvg"; break;
        case 10: r.u8();  break; // max stress
        case 11: r.u8();  break; // min stress
        case 12: r.skip(3); break; // 24h standing bitmap
        case 13: value = r.u16(); key = "calories";  break;
        case 14: r.u16(); break; // recovery hours
        case 15: r.u8();  break; // reserved
        case 16: r.u8();  break; // max SpO2
        case 17: r.u32(); break; // max SpO2 ts
        case 18: r.u8();  break; // min SpO2
        case 19: r.u32(); break; // min SpO2 ts
        case 20: value = r.u8();  key = "spo2Avg";   break;
        case 21: r.u16(); break; // training load (day)
        case 22: r.u16(); break; // training load (week)
        case 23: r.u8();  break; // training load level
        case 24: value = r.u8();  key = "vitalityLight";    break;
        case 25: value = r.u8();  key = "vitalityModerate"; break;
        case 26: value = r.u8();  key = "vitalityHigh";     break;
        case 27: value = r.u16(); key = "vitalityCurrent";  break;
        // Слот 28 по дампу Mi Band 8 совпал с VitalityItem из логов
        // Mi Fitness: activityType=1
        case 28: value = r.u8();  key = "activityType"; break;
        // Слот 29 = activityDuration из Mi Fitness, но на браслете
        // «время активности» — другое: счётчик strength-минут (0x60)
        // из daily details. Не используем, чтобы не затирать activityMin.
        default: r.skip(slotSize[i]); break; // 29, 30..31 reserved (Mi Band 10+)
        }
        // Слот всегда потребует свои байты; сохраняем только при валидном бите
        if (valid && key)
            out.insert(QLatin1String(key), value);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Daily Details (ACTIVITY/0/DETAILS) — DailyDetailsParser.java +
// XiaomiComplexActivityParser.java (ниббл-пакинг групп в header)
// ---------------------------------------------------------------------------

struct ComplexParser {
    const QByteArray &header;
    Reader &r;
    int group = -1;
    int groupBits = 0;
    quint32 val = 0;

    ComplexParser(const QByteArray &h, Reader &reader) : header(h), r(reader) {}

    void reset() { group = -1; groupBits = 0; val = 0; }

    int nibble() const
    {
        const quint8 b = quint8(header.at(group / 2));
        return (group % 2 == 0) ? (b >> 4) : (b & 0x0F);
    }

    quint32 consume(int nBits)
    {
        switch (nBits) {
        case 8:  return r.u8();
        case 16: return r.u16();
        case 32: return r.u32();
        }
        return 0;
    }

    // Переход к следующей группе; false — группы нет, буфер не двигается
    // (кроме случая выхода за пределы header — там Java дочитывает байты).
    bool nextGroup(int nBits)
    {
        ++group;
        if (group >= header.size() * 2) {
            consume(nBits);
            return false;
        }
        if ((nibble() & 8) == 0)
            return false;
        groupBits = nBits;
        val = consume(nBits);
        return true;
    }

    bool has(int idx) const { return (nibble() & (1 << (2 - idx))) != 0; }

    quint32 get(int idx, int nBits) const
    {
        const int shift = groupBits - idx - nBits;
        if (shift < 0)
            return 0;
        return (val & (((quint32(1) << nBits) - 1) << shift)) >> shift;
    }
};

QVariantMap parseDailyDetails(const xiaomiactivity::FileId &id, const QByteArray &payload)
{
    int headerSize;
    switch (id.version) {
    case 1: case 2: headerSize = 4; break;
    case 3:         headerSize = 5; break;
    case 4:         headerSize = 6; break;
    default:
        return unknown(QStringLiteral("unsupported daily details version %1").arg(id.version));
    }
    if (payload.size() < headerSize)
        return unknown(QStringLiteral("daily details too short: %1").arg(payload.size()));

    const QByteArray header = payload.left(headerSize);
    Reader r(payload, headerSize);
    ComplexParser cp(header, r);

    QVariantList samples;
    qlonglong ts = id.timestamp;
    int strengthMinutes = 0;
    while (r.remaining() > 0) {
        const int posBefore = r.pos;
        cp.reset();

        QVariantMap sample;
        sample.insert(QStringLiteral("ts"), ts);

        int includeExtraEntry = 0;
        if (cp.nextGroup(16)) {
            if (cp.has(1))
                includeExtraEntry = int(cp.get(1, 1));
            if (cp.has(2))
                sample.insert(QStringLiteral("steps"), qlonglong(cp.get(2, 14)));
        }
        if (cp.nextGroup(8)) {
            // active calories: get(2, 6) — не отдаём
        }
        if (cp.nextGroup(8)) {
            // Маркер «strength»-минуты (средне-высокая активность): 0x60.
            // Сверено с MHStrengthRecord в логах Mi Fitness — 17/17 минут
            // совпали; сумма за день = «время активности» на браслете.
            if (cp.has(0) && cp.get(0, 8) == 0x60)
                ++strengthMinutes;
        }
        if (cp.nextGroup(16)) {
            // distance: get(0, 16) * 100 см — не отдаём
        }
        if (cp.nextGroup(8)) {
            if (cp.has(0))
                sample.insert(QStringLiteral("hr"), qlonglong(cp.get(0, 8)));
        }
        cp.nextGroup(8); // energy
        cp.nextGroup(16); // неизвестная группа

        if (id.version >= 3) {
            if (cp.nextGroup(8)) {
                if (cp.has(0))
                    sample.insert(QStringLiteral("spo2"), qlonglong(cp.get(0, 8)));
            }
            if (cp.nextGroup(8)) {
                if (cp.has(0)) {
                    const quint32 stress = cp.get(0, 8);
                    if (stress != 255) // 255 = нет данных
                        sample.insert(QStringLiteral("stress"), qlonglong(stress));
                }
            }
        }

        if (includeExtraEntry == 1)
            r.u8(); // лишний байт, см. Gadgetbridge #4625

        if (id.version >= 4) {
            cp.nextGroup(16); // light value
            cp.nextGroup(16); // body momentum
        }

        if (r.overflow)
            return unknown(QStringLiteral("truncated daily details"));
        if (r.pos == posBefore) {
            // Ни одна группа не потребила байты — защита от бесконечного цикла
            if (samples.isEmpty())
                return unknown(QStringLiteral("daily details made no progress"));
            break;
        }

        samples.append(sample);
        ts += 60; // поминутные сэмплы от timestamp fileId
    }

    QVariantMap out;
    out.insert(QStringLiteral("kind"), QStringLiteral("dailyDetails"));
    out.insert(QStringLiteral("timestamp"), qlonglong(id.timestamp));
    out.insert(QStringLiteral("strengthMinutes"), qlonglong(strengthMinutes));
    out.insert(QStringLiteral("samples"), samples);
    return out;
}

// ---------------------------------------------------------------------------
// Sleep Details (ACTIVITY/8/*) — SleepDetailsParser.java
// ---------------------------------------------------------------------------

QString sleepStageName(int raw)
{
    switch (raw) {
    case 0: return QStringLiteral("awake");
    case 1: return QStringLiteral("light");
    case 2: return QStringLiteral("deep");
    case 3: return QStringLiteral("rem");
    case 4: return QStringLiteral("notSleep");
    }
    return QStringLiteral("unknown");
}

QVariantMap parseSleepDetails(const xiaomiactivity::FileId &id, const QByteArray &payload)
{
    int headerSize;
    switch (id.version) {
    case 1: case 2: case 3: case 4: headerSize = 1; break;
    case 5:                         headerSize = 2; break;
    default:
        return unknown(QStringLiteral("unsupported sleep details version %1").arg(id.version));
    }
    if (payload.size() < headerSize + 9)
        return unknown(QStringLiteral("sleep details too short: %1").arg(payload.size()));

    const QByteArray header = payload.left(headerSize);
    Reader r(payload, headerSize);

    // headerIdx — бит валидности; продвигается только для полей текущей версии
    int headerIdx = 0;

    const int isAwake = r.u8();
    ++headerIdx;
    const quint32 bedTime = r.u32();
    ++headerIdx;
    const quint32 wakeTime = r.u32();
    ++headerIdx;

    QVariantMap out;
    out.insert(QStringLiteral("kind"), QStringLiteral("sleep"));
    out.insert(QStringLiteral("timestamp"), qlonglong(id.timestamp));
    out.insert(QStringLiteral("isAwake"), isAwake == 1 ? 1 : 0);
    out.insert(QStringLiteral("bedTime"), qlonglong(bedTime));
    out.insert(QStringLiteral("wakeTime"), qlonglong(wakeTime));

    if (id.version >= 4) {
        // Байт quality присутствует в файле только при валидном бите
        if (validData(header, headerIdx))
            out.insert(QStringLiteral("quality"), qlonglong(r.u8()));
        ++headerIdx;
    }

    if (id.version >= 5) {
        r.skip(9);
        r.u32(); // bedTime2 (~за 30 мин до bedTime)
        r.u32(); // wakeTime2 == wakeTime
        headerIdx += 5;
    }

    // HR-сэмплы: unit u16, count u16, [firstRecordTime u32 при v>=2], count байт
    if (validData(header, headerIdx)) {
        r.u16();
        const int count = qint16(r.u16());
        if (count > 0) {
            if (id.version >= 2)
                r.u32();
            r.skip(count);
        }
    }
    ++headerIdx;

    // SpO2-сэмплы — то же строение
    if (validData(header, headerIdx)) {
        r.u16();
        const int count = qint16(r.u16());
        if (count > 0) {
            if (id.version >= 2)
                r.u32();
            r.skip(count);
        }
    }
    ++headerIdx;

    // Snore-сэмплы (v3+): сэмпл = float (4 байта)
    if (id.version >= 3) {
        if (validData(header, headerIdx)) {
            r.u16();
            const int count = qint16(r.u16());
            if (count > 0) {
                if (id.version >= 2)
                    r.u32();
                r.skip(count * 4);
            }
        }
        ++headerIdx;
    }

    QVariantList stages;
    QVariantMap summary;
    bool haveSummary = false;

    // Stage-пакеты ищем сканированием по магии. В Java буфер little-endian и
    // сравнение getInt()==0xfffcfafb соответствует байтам FB FA FC FF в файле.
    while (!r.overflow) {
        int magicAt = -1;
        for (int i = r.pos; i + 4 <= r.d.size(); ++i) {
            if (quint8(r.d.at(i)) == 0xFB && quint8(r.d.at(i + 1)) == 0xFA &&
                quint8(r.d.at(i + 2)) == 0xFC && quint8(r.d.at(i + 3)) == 0xFF) {
                magicAt = i;
                break;
            }
        }
        if (magicAt < 0 || r.d.size() - magicAt < 17)
            break;
        r.pos = magicAt + 4;

        r.u8();            // headerLen (всегда 17)
        const qint64 ts = r.i64(); // у пакетов 16/17 — unix-секунды
        r.u8();            // parity
        const int type = r.u8();
        const int dataLen = r.u16be();

        // Типы 2,3,9,12,13,14,15: байты длины — флаги, данных нет
        if (type == 0x2 || type == 0x3 || type == 0x9 || type == 0xc ||
            type == 0xd || type == 0xe || type == 0xf)
            continue;
        if (dataLen > r.remaining())
            break; // битый хвост — сохраняем то, что собрали (как catch в Java)

        const int dataEnd = r.pos + dataLen;
        if (type == 1) {
            // RR-интервалы — пропускаем
        } else if (type == 16 && dataLen >= 11) { // summary, данные big-endian
            r.u8(); // sleep_index<<4 | wake_count
            summary.insert(QStringLiteral("sleepMin"), qlonglong(r.u16be()));
            summary.insert(QStringLiteral("wakeMin"),  qlonglong(r.u16be()));
            summary.insert(QStringLiteral("lightMin"), qlonglong(r.u16be()));
            summary.insert(QStringLiteral("remMin"),   qlonglong(r.u16be()));
            summary.insert(QStringLiteral("deepMin"),  qlonglong(r.u16be()));
            haveSummary = true;
        } else if (type == 17) { // stages, записи u16be
            // Файл — накопительный снапшот: каждый следующий пакет фаз полнее
            // предыдущего, причём браслет пересматривает раннюю сегментацию.
            // Берём только ПОСЛЕДНИЙ непустой пакет, иначе шкала задваивается.
            QVariantList packetStages;
            qlonglong current = ts;
            for (int i = 0; i < dataLen / 2; ++i) {
                const quint16 val = r.u16be();
                const int stage = val >> 12;
                const int offsetMinutes = val & 0xFFF; // длительность ЭТОЙ фазы
                QVariantMap s;
                s.insert(QStringLiteral("ts"), current);
                s.insert(QStringLiteral("stage"), sleepStageName(stage));
                packetStages.append(s);
                current += qlonglong(offsetMinutes) * 60;
            }
            if (!packetStages.isEmpty())
                stages = packetStages;
        }
        r.pos = dataEnd;
    }

    if (haveSummary)
        out.insert(QStringLiteral("summary"), summary);
    out.insert(QStringLiteral("stages"), stages);
    return out;
}

// ---------------------------------------------------------------------------
// Sleep Stages v2 (ACTIVITY/3/DETAILS) — SleepStagesParser.java
// ---------------------------------------------------------------------------

QVariantMap parseSleepStages(const xiaomiactivity::FileId &id, const QByteArray &payload)
{
    if (id.version != 2)
        return unknown(QStringLiteral("unsupported sleep stages version %1").arg(id.version));
    if (payload.size() < 27)
        return unknown(QStringLiteral("sleep stages too short: %1").arg(payload.size()));

    Reader r(payload);
    r.skip(7);                        // unk1
    const quint16 sleepDuration = r.u16();
    const quint32 bedTime = r.u32();
    const quint32 wakeTime = r.u32();
    r.skip(3);                        // unk2
    const quint16 deep = r.u16();
    const quint16 light = r.u16();
    const quint16 rem = r.u16();
    const quint16 wake = r.u16();
    r.u8();                           // unk3

    QVariantMap summary;
    summary.insert(QStringLiteral("deepMin"),  qlonglong(deep));
    summary.insert(QStringLiteral("lightMin"), qlonglong(light));
    summary.insert(QStringLiteral("remMin"),   qlonglong(rem));
    summary.insert(QStringLiteral("awakeMin"), qlonglong(wake));

    // В отличие от sleep details, коды фаз здесь хранятся уже в нумерации
    // Gadgetbridge (XiaomiSleepStageSample): 2=deep, 3=light, 4=rem, 5=awake
    QVariantList stages;
    while (r.remaining() >= 5) {
        const quint32 ts = r.u32();
        const int phase = r.u8();
        QString name;
        switch (phase) {
        case 2: name = QStringLiteral("deep");  break;
        case 3: name = QStringLiteral("light"); break;
        case 4: name = QStringLiteral("rem");   break;
        case 5: name = QStringLiteral("awake"); break;
        default: name = QStringLiteral("unknown"); break;
        }
        QVariantMap s;
        s.insert(QStringLiteral("ts"), qlonglong(ts));
        s.insert(QStringLiteral("stage"), name);
        stages.append(s);
    }

    QVariantMap out;
    out.insert(QStringLiteral("kind"), QStringLiteral("sleep"));
    out.insert(QStringLiteral("bedTime"), qlonglong(bedTime));
    out.insert(QStringLiteral("wakeTime"), qlonglong(wakeTime));
    out.insert(QStringLiteral("sleepMin"), qlonglong(sleepDuration));
    out.insert(QStringLiteral("summary"), summary);
    out.insert(QStringLiteral("stages"), stages);
    return out;
}

// ---------------------------------------------------------------------------
// Manual Samples (ACTIVITY/6/DETAILS) — ManualSamplesParser.java
// ---------------------------------------------------------------------------

QVariantMap parseManualSamples(const xiaomiactivity::FileId &id, const QByteArray &payload)
{
    QVariantList samples;

    if (id.version == 1) {
        // header-байт 0xFF, далее записи по 6 байт: i32 ts, u8 type, u8 value
        if (payload.size() < 7 || quint8(payload.at(0)) != 0xFF)
            return unknown(QStringLiteral("bad manual samples v1 header"));
        if ((payload.size() - 1) % 6 != 0)
            return unknown(QStringLiteral("bad manual samples v1 length %1").arg(payload.size()));

        Reader r(payload, 1);
        while (r.remaining() > 0) {
            const quint32 ts = r.u32();
            const int type = r.u8();
            const int value = r.u8();
            QString name;
            switch (type) {
            case 0x02: name = QStringLiteral("spo2");   break;
            case 0x03: name = QStringLiteral("stress"); break;
            default:
                return unknown(QStringLiteral("unknown manual samples v1 type %1").arg(type));
            }
            if (value == 0 || value > 100)
                return unknown(QStringLiteral("invalid manual samples v1 value %1").arg(value));
            QVariantMap s;
            s.insert(QStringLiteral("ts"), qlonglong(ts));
            s.insert(QStringLiteral("type"), name);
            s.insert(QStringLiteral("value"), value);
            samples.append(s);
        }
    } else if (id.version == 2) {
        Reader r(payload);
        while (r.remaining() > 0) {
            const quint32 ts = r.u32();
            const int type = r.u8();
            qlonglong value = 0;
            QString name;
            switch (type) {
            case 0x11: name = QStringLiteral("hr");     value = r.u8(); break;
            case 0x12: name = QStringLiteral("spo2");   value = r.u8(); break;
            case 0x13: name = QStringLiteral("stress"); value = r.u8(); break;
            case 0x44: name = QStringLiteral("temperature"); value = qint32(r.u32()); break;
            default:
                // размер записи неизвестен — парсинг продолжить нельзя (как в Java)
                return unknown(QStringLiteral("unknown manual samples type %1").arg(type));
            }
            if (r.overflow)
                return unknown(QStringLiteral("truncated manual samples"));
            if (value == 0)
                continue;
            QVariantMap s;
            s.insert(QStringLiteral("ts"), qlonglong(ts));
            s.insert(QStringLiteral("type"), name);
            s.insert(QStringLiteral("value"), value);
            samples.append(s);
        }
    } else {
        return unknown(QStringLiteral("unsupported manual samples version %1").arg(id.version));
    }

    QVariantMap out;
    out.insert(QStringLiteral("kind"), QStringLiteral("manualSamples"));
    out.insert(QStringLiteral("samples"), samples);
    return out;
}

} // namespace

namespace xiaomiactivity {

FileId parseFileId(const QByteArray &id7)
{
    FileId id;
    if (id7.size() < 7)
        return id;
    Reader r(id7);
    id.timestamp = r.u32();
    id.timezone = qint8(r.u8());
    id.version = r.u8();
    const quint8 flags = r.u8();
    id.type = (flags >> 7) & 1;
    id.subtype = (flags & 0x7F) >> 2;
    id.detailType = flags & 3;
    id.valid = true;
    return id;
}

QVariantMap parseActivityFile(const FileId &id, const QByteArray &payload)
{
    if (!id.valid)
        return unknown(QStringLiteral("invalid file id"));

    if (id.type == 0) { // ACTIVITY
        switch (id.subtype) {
        case 0: // daily
            if (id.detailType == 0)
                return parseDailyDetails(id, payload);
            if (id.detailType == 1)
                return parseDailySummary(id, payload);
            break;
        case 3: // sleep stages
            if (id.detailType == 0)
                return parseSleepStages(id, payload);
            break;
        case 6: // manual samples
            if (id.detailType == 0)
                return parseManualSamples(id, payload);
            break;
        case 8: // sleep details (и DETAILS, и SUMMARY)
            return parseSleepDetails(id, payload);
        }
    }

    return unknown(QStringLiteral("unsupported file type %1 subtype %2 detail %3")
                   .arg(id.type).arg(id.subtype).arg(id.detailType));
}

} // namespace xiaomiactivity
