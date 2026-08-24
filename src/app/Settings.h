#pragma once

#include <QObject>
#include <QSettings>
#include <QString>

namespace hyperlight {

// The handful of things worth remembering between launches.
//
// Deliberately small. This app does one job, and a preferences window with
// twenty controls would be a bigger surface than the feature it configures.
class Settings : public QObject
{
    Q_OBJECT

public:
    // The store name is injectable so tests can use a throwaway one rather
    // than trampling the real preferences.
    explicit Settings(QObject *parent = nullptr,
                      const QString &application = QStringLiteral("hyperlight"));

    QString gradientId() const;
    void setGradientId(const QString &id);

    // Compact mode narrows the strip and drops the band count. Worth having as
    // a preference rather than a pure heuristic: on a notched display a 108pt
    // item can get pushed into the overflow and never be seen at all, and the
    // user is better placed than we are to know how crowded their bar is.
    bool compact() const;
    void setCompact(bool compact);

    // Halves the frame rate. Default on when the machine is in Low Power Mode.
    bool halfRateOnBattery() const;
    void setHalfRateOnBattery(bool enabled);

Q_SIGNALS:
    void gradientIdChanged(const QString &id);
    void compactChanged(bool compact);

private:
    QSettings m_store;
};

} // namespace hyperlight
