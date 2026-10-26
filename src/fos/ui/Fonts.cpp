#include "Fonts.h"

#include <EpdFont.h>
#include <EpdFontFamily.h>
#include <GfxRenderer.h>

#include "generated/FosFontData.h"

namespace fos::ui {
namespace {

EpdFont smallRegular(&fos_small_regular);
EpdFont smallBold(&fos_small_bold);
EpdFont bodyRegular(&fos_body_regular);
EpdFont bodyBold(&fos_body_bold);
EpdFont titleRegular(&fos_title_regular);
EpdFont displayRegular(&fos_display_regular);
EpdFont hugeRegular(&fos_huge_regular);
EpdFont monoRegular(&fos_mono_regular);
EpdFont monoBold(&fos_mono_bold);

}  // namespace

void registerFonts(GfxRenderer& renderer) {
  // Title, display and huge exist only in bold; they serve both styles.
  renderer.insertFont(font::SMALL, EpdFontFamily(&smallRegular, &smallBold));
  renderer.insertFont(font::BODY, EpdFontFamily(&bodyRegular, &bodyBold));
  renderer.insertFont(font::TITLE, EpdFontFamily(&titleRegular, &titleRegular));
  renderer.insertFont(font::LARGE, EpdFontFamily(&displayRegular, &displayRegular));
  renderer.insertFont(font::JUMBO, EpdFontFamily(&hugeRegular, &hugeRegular));
  renderer.insertFont(font::MONO, EpdFontFamily(&monoRegular, &monoBold));
}

}  // namespace fos::ui
