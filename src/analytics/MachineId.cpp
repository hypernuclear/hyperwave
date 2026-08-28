#include "analytics/MachineId.h"

#include <QCryptographicHash>
#include <QNetworkInterface>
#include <QSysInfo>

namespace hyperwave {

QString machineId()
{
    static QString cached;
    if (!cached.isEmpty())
        return cached;

    // machineUniqueId is IOPlatformUUID on macOS and MachineGuid on Windows.
    // Both survive reboots and app reinstalls, which is the whole requirement --
    // an id that changed would make every launch look like a new install.
    QString raw = QString::fromUtf8(QSysInfo::machineUniqueId());
    if (raw.isEmpty()) {
        // No platform id. Hostname plus the first non-loopback MAC is weaker --
        // a renamed machine or a swapped adapter changes it -- but it only has
        // to be stable on one machine, and the alternative is no id at all.
        QString mac;
        for (const QNetworkInterface &i : QNetworkInterface::allInterfaces()) {
            if (i.flags().testFlag(QNetworkInterface::IsLoopBack))
                continue;
            if (!i.hardwareAddress().isEmpty()) {
                mac = i.hardwareAddress();
                break;
            }
        }
        raw = QSysInfo::machineHostName() + QLatin1Char(':') + mac;
    }

    // Hashed, never sent raw. The input is an identifier that would otherwise
    // correlate this install with anything else that ever read the same value
    // off this machine.
    cached = QString::fromLatin1(
        QCryptographicHash::hash(raw.toUtf8(), QCryptographicHash::Sha256).toHex());
    return cached;
}

} // namespace hyperwave
