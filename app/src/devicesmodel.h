// SPDX-License-Identifier: BSD-3-Clause

#ifndef DEVICESMODEL_H
#define DEVICESMODEL_H

#include <QAbstractListModel>
#include <QVector>
#include <QVariantMap>

struct BleDevice {
    QString path;
    QString name;
    QString address;
    qint16 rssi = 0;
    bool connected = false;
    bool servicesResolved = false;
};

class DevicesModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        AddressRole,
        RssiRole,
        ConnectedRole,
        ResolvedRole
    };

    explicit DevicesModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void upsert(const QString &path, const QVariantMap &props);
    void remove(const QString &path);
    void clearAll();
    BleDevice deviceByAddress(const QString &address) const;
    // Снапшот для D-Bus (BandService): список карт с ролями модели
    QVariantList toList() const;

private:
    int indexOfPath(const QString &path) const;
    QVector<BleDevice> m_devices;
};

#endif // DEVICESMODEL_H
