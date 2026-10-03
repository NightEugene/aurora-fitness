// SPDX-License-Identifier: BSD-3-Clause

#ifndef XIAOMI_PROTO_H
#define XIAOMI_PROTO_H

#include <QByteArray>
#include <QList>
#include <QString>
#include <cstring>

// Минимальный proto2-кодек под протокол Xiaomi (xiaomi.proto).
namespace pb {

struct Field {
    int number = 0;
    int wireType = 0;   // 0 varint, 1 fixed64, 2 len-delim, 5 fixed32
    quint64 varint = 0;
    QByteArray bytes;   // wire 2: содержимое; wire 1/5: сырые байты
};

inline QByteArray encodeVarint(quint64 v)
{
    QByteArray out;
    while (v >= 0x80) {
        out.append(static_cast<char>((v & 0x7f) | 0x80));
        v >>= 7;
    }
    out.append(static_cast<char>(v));
    return out;
}

class Writer
{
public:
    QByteArray data;

    void tag(int field, int wire) { data += encodeVarint((field << 3) | wire); }

    void varint(int field, quint64 value) { tag(field, 0); data += encodeVarint(value); }
    void boolean(int field, bool v) { varint(field, v ? 1 : 0); }
    void sint32(int field, qint32 v)
    {
        varint(field, static_cast<quint32>((v << 1) ^ (v >> 31)));
    }
    void bytes(int field, const QByteArray &b)
    {
        tag(field, 2);
        data += encodeVarint(b.size());
        data += b;
    }
    void str(int field, const QString &s) { bytes(field, s.toUtf8()); }
    void fixedFloat(int field, float v)
    {
        tag(field, 5);
        quint32 bits;
        memcpy(&bits, &v, 4);
        for (int i = 0; i < 4; ++i)
            data.append(static_cast<char>((bits >> (8 * i)) & 0xff));
    }
    void msg(int field, const Writer &sub) { bytes(field, sub.data); }
};

inline QList<Field> parse(const QByteArray &data)
{
    QList<Field> out;
    int pos = 0;
    const int n = data.size();
    const uchar *p = reinterpret_cast<const uchar *>(data.constData());

    auto readVarint = [&]() -> quint64 {
        quint64 v = 0;
        int shift = 0;
        while (pos < n && shift < 70) {
            uchar b = p[pos++];
            v |= quint64(b & 0x7f) << shift;
            if (!(b & 0x80))
                break;
            shift += 7;
        }
        return v;
    };

    while (pos < n) {
        quint64 t = readVarint();
        Field f;
        f.number = static_cast<int>(t >> 3);
        f.wireType = static_cast<int>(t & 7);
        switch (f.wireType) {
        case 0:
            f.varint = readVarint();
            break;
        case 1:
            if (pos + 8 > n) return out;
            f.bytes = data.mid(pos, 8);
            pos += 8;
            break;
        case 2: {
            quint64 len = readVarint();
            if (pos + int(len) > n) return out;
            f.bytes = data.mid(pos, int(len));
            pos += int(len);
            break;
        }
        case 5:
            if (pos + 4 > n) return out;
            f.bytes = data.mid(pos, 4);
            pos += 4;
            break;
        default:
            return out; // неизвестный wire type — стоп
        }
        out.append(f);
    }
    return out;
}

inline QList<Field> byNumber(const QList<Field> &fields, int number)
{
    QList<Field> out;
    for (const Field &f : fields)
        if (f.number == number)
            out.append(f);
    return out;
}

inline Field first(const QList<Field> &fields, int number)
{
    const QList<Field> l = byNumber(fields, number);
    return l.isEmpty() ? Field() : l.first();
}

} // namespace pb

#endif // XIAOMI_PROTO_H
