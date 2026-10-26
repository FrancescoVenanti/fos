#include "Sync.h"

#include <Logging.h>

#include "fos/core/Status.h"

namespace fos::sync {

bool syncNow() {
  // The backend client arrives in Phase 6.
  LOG_INF("SYNC", "Sync requested; no backend client yet");
  setSyncState(SyncState::Idle);
  return false;
}

}  // namespace fos::sync
