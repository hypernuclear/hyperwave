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
    // Bundle id of the app to listen to; empty means the whole system mix.
    // Stored rather than the process object id, which is only meaningful for
    // the run that produced it.
    QString sourceBundleId() const;
    void setSourceBundleId(const QString &bundleId);
    // Track titles and transport controls need an Automation grant per
    // controlled app, so they stay off until asked for. The app's whole first
    // run is otherwise a single clean permission question.
    bool nowPlayingEnabled() const;
    void setNowPlayingEnabled(bool enabled);

Q_SIGNALS:
    void gradientIdChanged(const QString &id);
    void compactChanged(bool compact);
    void sourceBundleIdChanged(const QString &bundleId);
    void nowPlayingEnabledChanged(bool enabled);

private:
    QSettings m_store;
};

} // namespace hyperlight
