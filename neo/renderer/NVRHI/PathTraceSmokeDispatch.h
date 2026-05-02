#pragma once

// Public dispatch-size query for the smoke ray generation constants.
//
// The dispatch implementation itself is a private PathTracePrimaryPass method;
// this header exposes the constants-buffer size used when creating NVRHI
// resources without sharing the whole constants layout.

#include <stddef.h>

size_t GetPathTraceSmokeConstantsSize();
