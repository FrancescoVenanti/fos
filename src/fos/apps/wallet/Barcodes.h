#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "fos/ui/Canvas.h"

namespace fos::wallet {

// Codes drawn on the device at full contrast (black on white, whole-pixel
// modules, a quiet zone around them).

enum class Format : uint8_t { Qr, Code128, Ean13 };

Format parseFormat(const std::string& kind, const std::string& format);
const char* formatName(Format f);

// Bars for a 1-D code: module widths, alternating bar/space, bar first.
// False when the payload cannot be encoded (Code 128 B: ASCII 32-126;
// EAN-13: 12 or 13 digits, the check digit is computed or verified).
bool encodeCode128(const std::string& payload, std::vector<uint8_t>& widths);
bool encodeEan13(const std::string& payload, std::vector<uint8_t>& widths, std::string& digits);

// Draws the code as large as fits in r (centered). Returns false and draws
// nothing when the payload cannot be encoded.
bool drawCode(ui::Canvas& c, const ui::Rect& r, Format format, const std::string& payload);

}  // namespace fos::wallet
