#ifndef __VERSION_H__
#define __VERSION_H__

// Piano Booster version, in the form MAJOR.MINOR.PATCH.
// Increase it whenever a change is made, and record the change in Changelog.txt.
#define PB_VERSION_MAJOR 1
#define PB_VERSION_MINOR 2
#define PB_VERSION_PATCH 0

// The version as a string, e.g. "1.0.2" (built from the numbers above, do not edit)
#define PB_VERSION_TO_STRING_(x) #x
#define PB_VERSION_TO_STRING(x) PB_VERSION_TO_STRING_(x)
#define PB_VERSION PB_VERSION_TO_STRING(PB_VERSION_MAJOR) "." \
                   PB_VERSION_TO_STRING(PB_VERSION_MINOR) "." \
                   PB_VERSION_TO_STRING(PB_VERSION_PATCH)

#define MUSIC_RELEASE 4

#endif // __VERSION_H__
