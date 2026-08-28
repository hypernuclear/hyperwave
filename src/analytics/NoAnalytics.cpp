// The create() for a build configured with HYPERWAVE_ANALYTICS=OFF.
//
// A separate file rather than an #ifdef inside CountlyAnalytics.cpp, because the
// point of the option is that the SDK is not compiled or linked at all -- an
// #ifdef would still need it fetched.
#include "analytics/Analytics.h"

namespace hyperwave {

Analytics *Analytics::create(QObject *) { return nullptr; }

} // namespace hyperwave
