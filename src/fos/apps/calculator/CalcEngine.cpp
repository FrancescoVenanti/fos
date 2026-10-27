#include "CalcEngine.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "fos/ui/Keyboard.h"

namespace fos::calc {
namespace {

// Recursive descent over the expression text.
class Parser {
 public:
  explicit Parser(const std::string& text) : s_(text) {}

  bool parse(double& out) {
    if (!expr(out)) return false;
    // Missing closing parentheses are forgiven.
    while (pos_ < s_.size() && s_[pos_] == ')') ++pos_;
    return pos_ == s_.size() && std::isfinite(out);
  }

 private:
  bool expr(double& out) {
    if (!term(out)) return false;
    while (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-')) {
      const char op = s_[pos_++];
      double rhs = 0;
      if (!term(rhs)) return false;
      out = op == '+' ? out + rhs : out - rhs;
    }
    return true;
  }

  bool term(double& out) {
    if (!factor(out)) return false;
    while (pos_ < s_.size() && (s_[pos_] == '*' || s_[pos_] == '/')) {
      const char op = s_[pos_++];
      double rhs = 0;
      if (!factor(rhs)) return false;
      if (op == '/' && rhs == 0) return false;
      out = op == '*' ? out * rhs : out / rhs;
    }
    return true;
  }

  bool factor(double& out) {
    if (pos_ >= s_.size()) return false;
    if (s_[pos_] == '-') {
      ++pos_;
      if (!factor(out)) return false;
      out = -out;
      return true;
    }
    if (s_[pos_] == '(') {
      ++pos_;
      if (!expr(out)) return false;
      if (pos_ < s_.size() && s_[pos_] == ')') ++pos_;
    } else {
      const char* start = s_.c_str() + pos_;
      char* end = nullptr;
      out = strtod(start, &end);
      if (end == start) return false;
      pos_ += static_cast<size_t>(end - start);
    }
    while (pos_ < s_.size() && s_[pos_] == '%') {
      out /= 100.0;
      ++pos_;
    }
    return true;
  }

  const std::string& s_;
  size_t pos_ = 0;
};

}  // namespace

bool evaluate(const std::string& expr, double& out) {
  if (expr.empty()) return false;
  Parser p(expr);
  return p.parse(out);
}

std::string formatNumber(double value) {
  if (value == 0) value = 0;  // no "-0"
  const double mag = std::fabs(value);
  const char sep = ui::decimalSeparator();
  if (mag != 0 && (mag >= 1e12 || mag < 1e-6)) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.6g", value);
    std::string out = buf;
    for (char& ch : out) {
      if (ch == '.') ch = sep;
    }
    return out;
  }
  // 10 significant digits, then drop trailing zeros.
  const int intDigits = mag >= 1 ? static_cast<int>(std::floor(std::log10(mag))) + 1 : 1;
  const int decimals = std::max(0, std::min(9, 10 - intDigits));
  char buf[48];
  snprintf(buf, sizeof(buf), "%.*f", decimals, value);
  std::string text = buf;
  const size_t dot = text.find('.');
  if (dot != std::string::npos) {
    while (!text.empty() && text.back() == '0') text.pop_back();
    if (!text.empty() && text.back() == '.') text.pop_back();
  }
  // Group the integer part.
  const bool negative = !text.empty() && text[0] == '-';
  const size_t intStart = negative ? 1 : 0;
  size_t intEnd = text.find('.');
  if (intEnd == std::string::npos) intEnd = text.size();
  const char group = sep == ',' ? '.' : ',';
  std::string out = negative ? "\xE2\x88\x92" : "";
  for (size_t i = intStart; i < intEnd; ++i) {
    if (i > intStart && (intEnd - i) % 3 == 0) out += group;
    out += text[i];
  }
  if (intEnd < text.size()) {
    out += sep;
    out += text.substr(intEnd + 1);
  }
  return out;
}

std::string displayExpression(const std::string& expr) {
  std::string out;
  const char sep = ui::decimalSeparator();
  for (const char ch : expr) {
    switch (ch) {
      case '*':
        out += " \xC3\x97 ";
        break;
      case '/':
        out += " \xC3\xB7 ";
        break;
      case '+':
        out += " + ";
        break;
      case '-':
        out += " \xE2\x88\x92 ";
        break;
      case '.':
        out += sep;
        break;
      default:
        out += ch;
        break;
    }
  }
  return out;
}

}  // namespace fos::calc
