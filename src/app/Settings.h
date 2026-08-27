#pragma once

#include <QObject>
#include <QSettings>
#include <QString>

#include "render/RenderStyle.h"

namespace hyperwave {

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
                      const QString &application = QStringLiteral("hyperwave"));

    QString gradientId() const;
    void setGradientId(const QString &id);
    // Which visualiser draws: "bars" or "ridge". A string rather than an enum
    // because it crosses a settings file, a command line and a menu, and a
    // value written by a newer build has to survive being read by an older one
    // without becoming a different look by accident.
    QString vizStyle() const;
    void setVizStyle(const QString &id);
    // The visualiser parameters, persisted field by field.
    //
    // Field by field rather than as a blob because a blob written by a newer
    // build is unreadable by an older one, and because a single unknown key is
    // then the difference between one parameter falling back to its default and
    // all of them doing so.
    RenderStyle::Strands strands() const;
    void setStrands(const RenderStyle::Strands &strands);
    RenderStyle::Ridge ridge() const;
    void setRidge(const RenderStyle::Ridge &ridge);
    RenderStyle::Lines lines() const;
    void setLines(const RenderStyle::Lines &lines);
    RenderStyle::Mirror mirror() const;
    void setMirror(const RenderStyle::Mirror &mirror);
    RenderStyle::Ring ring() const;
    void setRing(const RenderStyle::Ring &ring);
    // Per visualiser, so each look keeps its own glow. The fallback is passed
    // in rather than assumed, because the right default differs by look.
    RenderStyle::Bloom bloom(const QString &vizId, const RenderStyle::Bloom &fallback) const;
    void setBloom(const QString &vizId, const RenderStyle::Bloom &bloom);
    void resetVisualiserParameters();

    // Compact mode narrows the strip and drops the band count. Worth having as
    // a preference rather than a pure heuristic: on a notched display a 108pt
    // item can get pushed into the overflow and never be seen at all, and the
    // user is better placed than we are to know how crowded their bar is.
    // Mix an input device into the analysis alongside the system output.
    bool inputEnabled() const;
    void setInputEnabled(bool enabled);

    bool compact() const;
    void setCompact(bool compact);

    // Halves the frame rate. Default on when the machine is in Low Power Mode.
    bool halfRateOnBattery() const;
    void setHalfRateOnBattery(bool enabled);
    // UID of the output device to listen to; empty means the whole system mix.
    // Stored rather than the AudioObjectID, which is only meaningful for the run
    // that produced it.
    QString sourceDeviceUid() const;
    void setSourceDeviceUid(const QString &uid);
    // Track titles and transport controls need an Automation grant per
    // controlled app, so they stay off until asked for. The app's whole first
    // run is otherwise a single clean permission question.
    bool nowPlayingEnabled() const;
    void setNowPlayingEnabled(bool enabled);
    // Retract the notch tab when nothing is playing, so the notch looks stock
    // on a quiet machine. On by default: an always-present tab is clutter the
    // rest of the time.
    bool notchAutoHide() const;
    void setNotchAutoHide(bool enabled);

Q_SIGNALS:
    void gradientIdChanged(const QString &id);
    void visualiserParametersChanged();
    void vizStyleChanged(const QString &id);
    void compactChanged(bool compact);
    void sourceDeviceUidChanged(const QString &uid);
    void nowPlayingEnabledChanged(bool enabled);
    void notchAutoHideChanged(bool enabled);

private:
    QSettings m_store;
};

} // namespace hyperwave
