// The seam between the app and whichever updater the platform has.
//
// macOS gets Sparkle, Windows gets WinSparkle, and everywhere else gets nothing
// at all -- create() returns null and the menu entry stays hidden. Both real
// backends read the same appcast format and verify the same EdDSA signature, so
// one feed generator serves both.
#pragma once

#include <QObject>

namespace hyperwave {

class AppUpdater : public QObject
{
    Q_OBJECT

public:
    explicit AppUpdater(QObject *parent = nullptr) : QObject(parent) {}
    ~AppUpdater() override = default;

    // The updater for this platform, or null where there is not one.
    //
    // Null is a normal answer rather than a failure: it is what a Linux build
    // returns, and what any build configured with HYPERWAVE_AUTO_UPDATE=OFF
    // returns -- which includes every build made without an EdDSA public key,
    // since an updater that cannot verify a signature is worse than none.
    // Callers hide their update UI and carry on.
    static AppUpdater *create(QObject *parent = nullptr);

    // Whether a check can start right now. False while one is already running,
    // or while an update is being installed.
    virtual bool canCheckForUpdates() const = 0;

public Q_SLOTS:
    // A check the user asked for, with the platform's own progress and "you are
    // up to date" UI. Distinct from the silent background check the updater
    // runs on its own schedule, which says nothing unless it has something to
    // say.
    virtual void checkForUpdates() = 0;

    // Turn the background schedule on or off.
    virtual void setAutomaticChecksEnabled(bool enabled) = 0;

Q_SIGNALS:
    void canCheckForUpdatesChanged(bool canCheck);
};

} // namespace hyperwave
