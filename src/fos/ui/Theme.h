#pragma once

// FOs neo-brutalist design tokens (see docs/PRODUCT.md, "Design language").
namespace fos::ui::theme {

constexpr int BORDER = 3;       // every card, tile and button outline
constexpr int SHADOW = 4;       // hard black shadow, down-right; pressed = 0 and shifted by this much
constexpr int GUTTER = 16;      // screen side margin
constexpr int GAP = 12;         // space between blocks
constexpr int MIN_TARGET = 64;  // minimum touch target
constexpr int STATUS_BAR_H = 40;
constexpr int HEADER_H = 72;
constexpr int ROW_H = 72;     // list row
constexpr int ACTION_H = 76;  // full-width main action at the bottom of a screen
constexpr int CHIP_H = 44;
constexpr int TAB_H = 56;

}  // namespace fos::ui::theme
