#pragma once

namespace fos::sync {

// Runs a full sync now: queued entries go up first, then fresh data comes
// down for every app. Blocks until done. Returns true on success.
bool syncNow();

}  // namespace fos::sync
