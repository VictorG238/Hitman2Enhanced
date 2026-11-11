// The list of all fixes. Order = order of application. See fix.h for how to add one.
#include "fix.h"

namespace h2e {

extern const Fix kResolutionFix;
extern const Fix kHorPlusFix;
extern const Fix kTimerFix;
extern const Fix kMenuScaleFix;
extern const Fix kFrameLimitFix;
extern const Fix kHdFontsFix;
extern const Fix kSettingsFix;

// settings stays after framelimit: its saved frame limit overrides h2e.ini [framelimit] MaxFps at attach.
const Fix* const kFixes[] = {&kResolutionFix, &kHorPlusFix, &kTimerFix,   &kMenuScaleFix,
                             &kFrameLimitFix, &kHdFontsFix, &kSettingsFix};
const size_t kFixCount = sizeof(kFixes) / sizeof(kFixes[0]);

}  // namespace h2e
