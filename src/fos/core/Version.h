#pragma once

// FOs release version. FOS_BUILD comes from the build (git branch and commit
// on development builds); see platformio.ini [fos].
#ifndef FOS_VERSION
#define FOS_VERSION "0.1.0"
#endif

#ifdef CROSSPOINT_VERSION
#define FOS_VERSION_STRING FOS_VERSION " (" CROSSPOINT_VERSION ")"
#else
#define FOS_VERSION_STRING FOS_VERSION
#endif
