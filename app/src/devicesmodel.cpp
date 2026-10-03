// SPDX-License-Identifier: BSD-3-Clause

#include "devicesmodel.h"

DevicesModel::DevicesModel(QObject *parent) : QAbstractListModel(parent)
{
}

int DevicesModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_devices.size();
}

QVariant DevicesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_devices.size())
        return QVariant();

    const BleDevice &d = m_devices.at(index.row());
    switch (role) {
    case NameRole: return d.name.isEmpty() ? QStringLiteral("(без имени)") : d.name;
    case AddressRole: return d.address;
    case RssiRole: return d.rssi;
    case ConnectedRole: return d.connected;
    case ResolvedRole: return d.servicesResolved;
    }
    return QVariant();
}

QHash<int, QByteArray> DevicesModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {AddressRole, "address"},
        {RssiRole, "rssi"},
        {ConnectedRole, "connected"},
        {ResolvedRole, "servicesResolved"}
    };
}

int DevicesModel::indexOfPath(const QString &path) const
{
    for (int i = 0; i < m_devices.size(); ++i) {
        if (m_devices.at(i).path == path)
            return i;
    }
    return -1;
}

void DevicesModel::upsert(const QString &path, const QVariantMap &props)
{
    int row = indexOfPath(path);
    if (row < 0) {
        BleDevice d;
        d.path = path;
        d.address = props.value(QStringLiteral("Address")).toString();
        d.name = props.value(QStringLiteral("Name")).toString();
        d.rssi = static_cast<qint16>(props.value(QStringLiteral("RSSI")).toInt());
        d.connected = props.value(QStringLiteral("Connected")).toBool();
        d.servicesResolved = props.value(QStringLiteral("ServicesResolved")).toBool();
        beginInsertRows(QModelIndex(), m_devices.size(), m_devices.size());
        m_devices.append(d);
        endInsertRows();
        return;
    }

    BleDevice &d = m_devices[row];
    if (props.contains(QStringLiteral("Address")))
        d.address = props.value(QStringLiteral("Address")).toString();
    if (props.contains(QStringLiteral("Name")))
        d.name = props.value(QStringLiteral("Name")).toString();
    if (props.contains(QStringLiteral("RSSI")))
        d.rssi = static_cast<qint16>(props.value(QStringLiteral("RSSI")).toInt());
    if (props.contains(QStringLiteral("Connected")))
        d.connected = props.value(QStringLiteral("Connected")).toBool();
    if (props.contains(QStringLiteral("ServicesResolved")))
        d.servicesResolved = props.value(QStringLiteral("ServicesResolved")).toBool();
    emit dataChanged(index(row), index(row));
}

void DevicesModel::remove(const QString &path)
{
    int row = indexOfPath(path);
    if (row < 0)
        return;
    beginRemoveRows(QModelIndex(), row, row);
    m_devices.remove(row);
    endRemoveRows();
}

void DevicesModel::clearAll()
{
    if (m_devices.isEmpty())
        return;
    beginResetModel();
    m_devices.clear();
    endResetModel();
}

BleDevice DevicesModel::deviceByAddress(const QString &address) const
{
    for (const BleDevice &d : m_devices) {
        if (d.address.compare(address, Qt::CaseInsensitive) == 0)
            return d;
    }
    return BleDevice();
}
