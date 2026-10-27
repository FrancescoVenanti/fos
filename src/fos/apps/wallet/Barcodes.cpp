#include "Barcodes.h"

#include <qrcode.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <memory>

#include "fos/ui/Fonts.h"

namespace fos::wallet {

using namespace ui;

namespace {

// Code 128 patterns: bar/space widths for values 0..106 (106 = stop, 7 modules
// with the final bar).
constexpr const char* C128[107] = {
    "212222", "222122", "222221", "121223", "121322", "131222", "122213", "122312", "132212", "221213", "221312",
    "231212", "112232", "122132", "122231", "113222", "123122", "123221", "223211", "221132", "221231", "213212",
    "223112", "312131", "311222", "321122", "321221", "312212", "322112", "322211", "212123", "212321", "232121",
    "111323", "131123", "131321", "112313", "132113", "132311", "211313", "231113", "231311", "112133", "112331",
    "132131", "113123", "113321", "133121", "313121", "211331", "231131", "213113", "213311", "213131", "311123",
    "311321", "331121", "312113", "312311", "332111", "314111", "221411", "431111", "111224", "111422", "121124",
    "121421", "141122", "141221", "112214", "112412", "122114", "122411", "142112", "142211", "241211", "221114",
    "413111", "241112", "134111", "111242", "121142", "121241", "114212", "124112", "124211", "411212", "421112",
    "421211", "212141", "214121", "412121", "111143", "111341", "131141", "114113", "114311", "411113", "411311",
    "113141", "114131", "311141", "411131", "211412", "211214", "211232", "2331112"};
constexpr int C128_START_B = 104;
constexpr int C128_STOP = 106;

// EAN-13 digit patterns (L, G, R codes as space/bar widths) and the parity of
// the first digit.
constexpr const char* EAN_L[10] = {"3211", "2221", "2122", "1411", "1132", "1231", "1114", "1312", "1213", "3112"};
constexpr const char* EAN_PARITY[10] = {"LLLLLL", "LLGLGG", "LLGGLG", "LLGGGL", "LGLLGG",
                                        "LGGLLG", "LGGGLL", "LGLGLG", "LGLGGL", "LGGLGL"};

// Byte-mode capacity of QR versions 1-25 at error correction M (ISO 18004).
// The QRCode library does not check capacity itself, so the version is
// chosen here; 25 keeps its stack buffers small on the main task.
constexpr uint16_t QR_BYTES_M[25] = {14,  26,  42,  62,  84,  106, 122, 152, 180, 213, 251, 287, 331,
                                     362, 412, 450, 504, 560, 624, 666, 711, 779, 857, 911, 997};

void append(std::vector<uint8_t>& out, const char* pattern) {
  for (const char* p = pattern; *p; ++p) out.push_back(static_cast<uint8_t>(*p - '0'));
}

// Draws modules starting with a bar, left to right.
void drawBars(Canvas& c, const std::vector<uint8_t>& widths, const int x0, const int y, const int module, const int h) {
  int x = x0;
  for (size_t i = 0; i < widths.size(); ++i) {
    const int w = widths[i] * module;
    if (i % 2 == 0) c.fill({x, y, w, h}, Tone::Black);
    x += w;
  }
}

int totalModules(const std::vector<uint8_t>& widths) {
  int n = 0;
  for (const uint8_t w : widths) n += w;
  return n;
}

}  // namespace

Format parseFormat(const std::string& kind, const std::string& format) {
  std::string f = format;
  for (char& ch : f) ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
  if (f == "ean13" || f == "ean-13" || f == "ean") return Format::Ean13;
  if (f == "code128" || f == "code-128" || f == "barcode") return Format::Code128;
  if (f == "qr") return Format::Qr;
  return kind == "barcode" ? Format::Code128 : Format::Qr;
}

const char* formatName(const Format f) {
  switch (f) {
    case Format::Code128:
      return "Code 128";
    case Format::Ean13:
      return "EAN-13";
    case Format::Qr:
      break;
  }
  return "QR";
}

bool encodeCode128(const std::string& payload, std::vector<uint8_t>& widths) {
  widths.clear();
  if (payload.empty()) return false;
  int sum = C128_START_B;
  append(widths, C128[C128_START_B]);
  int pos = 1;
  for (const char ch : payload) {
    if (ch < 32 || ch > 126) return false;
    const int value = ch - 32;
    sum += value * pos++;
    append(widths, C128[value]);
  }
  append(widths, C128[sum % 103]);
  append(widths, C128[C128_STOP]);
  return true;
}

bool encodeEan13(const std::string& payload, std::vector<uint8_t>& widths, std::string& digits) {
  widths.clear();
  digits.clear();
  for (const char ch : payload) {
    if (isdigit(static_cast<unsigned char>(ch))) digits += ch;
  }
  if (digits.size() != 12 && digits.size() != 13) return false;
  int sum = 0;
  for (int i = 0; i < 12; ++i) sum += (digits[i] - '0') * (i % 2 ? 3 : 1);
  const char check = static_cast<char>('0' + (10 - sum % 10) % 10);
  if (digits.size() == 13 && digits[12] != check) return false;
  if (digits.size() == 12) digits += check;

  // Widths start with a bar: guard 1-1-1 (bar, space, bar).
  append(widths, "111");
  const char* parity = EAN_PARITY[digits[0] - '0'];
  for (int i = 1; i <= 6; ++i) {
    // L and G codes start with a space; G is the reverse of R (= L read backwards).
    const char* l = EAN_L[digits[i] - '0'];
    char code[5] = {0};
    if (parity[i - 1] == 'L') {
      memcpy(code, l, 4);
    } else {
      for (int k = 0; k < 4; ++k) code[k] = l[3 - k];
    }
    append(widths, code);
  }
  append(widths, "11111");                                               // centre guard: space, bar, space, bar, space
  for (int i = 7; i <= 12; ++i) append(widths, EAN_L[digits[i] - '0']);  // R codes start with a bar
  append(widths, "111");
  return true;
}

bool drawCode(Canvas& c, const Rect& r, const Format format, const std::string& payload) {
  if (format == Format::Qr) {
    // The smallest version that fits, error correction M.
    const size_t len = payload.size();
    uint8_t version = 0;
    for (uint8_t v = 1; v <= 25; ++v) {
      if (len <= QR_BYTES_M[v - 1]) {
        version = v;
        break;
      }
    }
    if (version == 0) return false;
    {
      std::unique_ptr<uint8_t[]> buffer(new uint8_t[qrcode_getBufferSize(version)]);
      QRCode qr;
      if (qrcode_initBytes(&qr, buffer.get(), version, ECC_MEDIUM,
                           reinterpret_cast<uint8_t*>(const_cast<char*>(payload.data())),
                           static_cast<uint16_t>(len)) != 0) {
        return false;
      }
      const int quiet = 4;
      const int modules = qr.size + 2 * quiet;
      const int scale = std::max(1, std::min(r.w, r.h) / modules);
      const int side = modules * scale;
      const int x0 = r.x + (r.w - side) / 2 + quiet * scale;
      const int y0 = r.y + (r.h - side) / 2 + quiet * scale;
      c.fill({x0 - quiet * scale, y0 - quiet * scale, side, side}, Tone::White);
      for (int y = 0; y < qr.size; ++y) {
        for (int x = 0; x < qr.size; ++x) {
          if (qrcode_getModule(&qr, x, y)) c.fill({x0 + x * scale, y0 + y * scale, scale, scale}, Tone::Black);
        }
      }
      return true;
    }
  }

  std::vector<uint8_t> widths;
  std::string digits;
  const bool ok = format == Format::Ean13 ? encodeEan13(payload, widths, digits) : encodeCode128(payload, widths);
  if (!ok) return false;
  const int quiet = format == Format::Ean13 ? 11 : 10;
  const int modules = totalModules(widths) + 2 * quiet;
  const int module = std::max(1, r.w / modules);
  const int textH = c.lineHeight(font::BODY);
  const int barH = std::min(r.h - textH - 8, std::max(80, module * 50));
  const int w = modules * module;
  const int x0 = r.x + (r.w - w) / 2;
  const int y0 = r.y + (r.h - barH - textH - 8) / 2;
  c.fill({x0, y0, w, barH + textH + 8}, Tone::White);
  drawBars(c, widths, x0 + quiet * module, y0, module, barH);
  // The human-readable line under the bars.
  const std::string text = format == Format::Ean13 ? digits : payload;
  c.textIn({x0, y0 + barH + 4, w, textH}, font::BODY, text.c_str(), Tone::Black, Align::Center, true);
  return true;
}

}  // namespace fos::wallet
