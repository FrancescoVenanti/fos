#pragma once

#include <string>

namespace fos::calc {

// Evaluates an expression of numbers ('.' decimal), + - * /, parentheses,
// unary minus and postfix % (divide by 100). False on a syntax error or a
// division by zero. Unclosed parentheses are closed at the end.
bool evaluate(const std::string& expr, double& out);

// A number for the display: up to 10 significant digits, no trailing zeros,
// thousands grouping and the decimal separator from Settings > Region.
// Very large or small values use scientific notation ("1,5e12").
std::string formatNumber(double value);

// The expression as shown: '*' as x, '/' as the division sign, '-' as the
// minus sign, '.' as the user's decimal separator.
std::string displayExpression(const std::string& expr);

}  // namespace fos::calc
