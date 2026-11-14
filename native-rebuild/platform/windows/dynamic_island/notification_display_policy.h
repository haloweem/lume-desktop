#pragma once
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

namespace lume::platform::windows {
// QUNS_BUSY/APP describe global application activity. The caller separately
// determines fullscreen protection on the island's target display and checks
// the input desktop before showing content.
inline bool AllowIslandNotificationState(QUERY_USER_NOTIFICATION_STATE state, bool protectedOnTarget) {
    if (protectedOnTarget) return false;
    switch (state) {
    case QUNS_ACCEPTS_NOTIFICATIONS:
    case QUNS_BUSY:
    case QUNS_APP:
        return true;
    default:
        // Absent user, exclusive games, presentations, quiet time and unknown
        // states continue to suppress notifications.
        return false;
    }
}
} // namespace lume::platform::windows
