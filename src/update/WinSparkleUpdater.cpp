#include "update/WinSparkleUpdater.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QString>

#include <winsparkle.h>

namespace hyperwave {

WinSparkleUpdater *WinSparkleUpdater::s_instance = nullptr;

WinSparkleUpdater::WinSparkleUpdater(QObject *parent)
    : AppUpdater(parent)
{
    s_instance = this;

    // The same display-versus-comparison split macOS gets from
    // CFBundleShortVersionString and CFBundleVersion:
    //   set_app_details        the string in "you have X", human-readable
    //   set_app_build_version  the value compared against <sparkle:version>
    // Passing only the first would make WinSparkle compare marketing strings,
    // and "0.1.0" never sorts above "0.1.0".
    win_sparkle_set_app_details(L"Hypernuclear", L"hyperwave",
                                QStringLiteral(HYPERWAVE_VERSION).toStdWString().c_str());
    win_sparkle_set_app_build_version(
        QStringLiteral(HYPERWAVE_VERSION_BUILD).toStdWString().c_str());

    win_sparkle_set_appcast_url(HYPERWAVE_APPCAST_URL);
    win_sparkle_set_eddsa_public_key(HYPERWAVE_EDDSA_PUBKEY);

    // Without a shutdown callback WinSparkle kills the process to swap the
    // binary, so the appbar would be torn down by the OS rather than by us --
    // and an appbar that never sends ABM_REMOVE leaves its reserved strip
    // behind after the process is gone. Routing it through quit() lets the
    // normal teardown run, which is the only thing that gives the space back.
    win_sparkle_set_can_shutdown_callback(onCanShutdown);
    win_sparkle_set_shutdown_request_callback(onShutdownRequest);

    win_sparkle_init();
}

WinSparkleUpdater::~WinSparkleUpdater()
{
    win_sparkle_cleanup();
    s_instance = nullptr;
}

bool WinSparkleUpdater::canCheckForUpdates() const { return true; }

void WinSparkleUpdater::checkForUpdates() { win_sparkle_check_update_with_ui(); }

void WinSparkleUpdater::setAutomaticChecksEnabled(bool enabled)
{
    win_sparkle_set_automatic_check_for_updates(enabled ? 1 : 0);
}

int WinSparkleUpdater::onCanShutdown() { return 1; }

void WinSparkleUpdater::onShutdownRequest()
{
    // Called on WinSparkle's own thread. quit() has to happen on the one that
    // owns the event loop.
    QMetaObject::invokeMethod(
        qApp, [] { qApp->quit(); }, Qt::QueuedConnection);
}

AppUpdater *AppUpdater::create(QObject *parent)
{
    return new WinSparkleUpdater(parent);
}

} // namespace hyperwave
