#pragma once

#include "update/AppUpdater.h"

namespace hyperwave {

// Sparkle, on macOS.
//
// Deliberately thin: the feed URL and the public key both come from Info.plist
// (SUFeedURL / SUPublicEDKey), which is also where Sparkle's own installer
// reads them, so there is nothing to configure here and no way for the two to
// disagree. This class exists so the menu entry can grey itself out while a
// check is in flight.
class SparkleUpdater : public AppUpdater
{
    Q_OBJECT

public:
    explicit SparkleUpdater(QObject *parent = nullptr);
    ~SparkleUpdater() override;

    bool canCheckForUpdates() const override;

public Q_SLOTS:
    void checkForUpdates() override;
    void setAutomaticChecksEnabled(bool enabled) override;

private:
    // SPUStandardUpdaterController and its KVO observer, held as void* so this
    // header stays includable from plain C++.
    void *m_delegate = nullptr;
};

} // namespace hyperwave
