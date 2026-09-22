// Single source of truth for the version. app.rc (VERSIONINFO), the startup log
// line and the "About" dialog all read these macros.
// After changing them, rebuild the whole solution: RC incremental build tracks
// only app.rc, not this header. Then tag per README.
#ifndef LIGHTSEE_VERSION_H
#define LIGHTSEE_VERSION_H 1

#define LIGHTSEE_VERSION_MAJOR 1
#define LIGHTSEE_VERSION_MINOR 0
#define LIGHTSEE_VERSION_PATCH 0
#define LIGHTSEE_VERSION_TWEAK 0

#define LIGHTSEE_VERSION_STRING "1.0.0"

#endif
