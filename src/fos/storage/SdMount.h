#pragma once

namespace fos::sdcard {

// Mounts the microSD card for setup(). On the Sticky a failed mount is retried
// after powering the card off (a card can stay wedged after a bad transfer), and
// a final failure logs why, from read-only probes: card not answering, no
// partition table, or no readable FAT/exFAT boot sector. Never writes the card.
bool mount();

// Cuts the card's supply for 300 ms and powers it again (Sticky; no-op elsewhere).
// SdFat must initialize the card afterwards.
void powerCycleCard();

}  // namespace fos::sdcard
