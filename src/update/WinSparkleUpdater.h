#pragma once

#include "update/AppUpdater.h"

namespace hyperwave {

// WinSparkle, on Windows.
//
// The C API is global rather than per-instance -- one updater per process,
// configured with free functions -- so this is a thin wrapper over that, and
// constructing two would be a bug.
class WinSparkleUpdater : public AppUpdater
{
    Q_OBJECT

public:
    explicit WinSparkleUpdater(QObject *parent = nullptr);
    ~WinSparkleUpdater() override;

    // WinSparkle asks before it starts a check rather than while one is
    // running, so there is no state to report -- unlike Sparkle, whose
    // controller genuinely does refuse re-entry.
    bool canCheckForUpdates() const override;

public Q_SLOTS:
    void checkForUpdates() override;
    void setAutomaticChecksEnabled(bool enabled) override;

private:
    // WinSparkle's C callbacks, so the app tears itself down rather than being
    // killed mid-update.
    static int onCanShutdown();
    static void onShutdownRequest();

    static WinSparkleUpdater *s_instance;
};

} // namespace hyperwave
