#include "tools/calculator.h"

namespace {

using Kind = CalcToken::Kind;

int precedence(Kind kind) {
    switch (kind) {
        case Kind::Plus:
        case Kind::Minus: return 1;
        case Kind::Times:
        case Kind::Divide: return 2;
        default: return 0;
    }
}

bool apply(std::vector<double>& values, std::vector<Kind>& ops, std::string* error) {
    if (ops.empty() || values.size() < 2) {
        *error = "operator is missing an operand";
        return false;
    }
    const Kind op = ops.back();
    ops.pop_back();

    const double right = values.back();
    values.pop_back();
    const double left = values.back();
    values.pop_back();

    double result = 0;
    switch (op) {
        case Kind::Plus: result = left + right; break;
        case Kind::Minus: result = left - right; break;
        case Kind::Times: result = left * right; break;
        case Kind::Divide:
            if (right == 0.0) {
                *error = "division by zero";
                return false;
            }
            result = left / right;
            break;
        default:
            *error = "unmatched parenthesis or invalid operator";
            return false;
    }
    values.push_back(result);
    return true;
}

}

bool calc_evaluate(const std::vector<CalcToken>& tokens, double* out, std::string* error) {
    std::vector<double> values;
    std::vector<Kind> ops;
    bool expect_operand = true;
    error->clear();

    for (const auto& token : tokens) {
        if (token.kind == Kind::Number) {
            if (!expect_operand) {
                *error = "expected an operator";
                return false;
            }
            values.push_back(token.value);
            expect_operand = false;
        } else if (token.kind == Kind::LParen) {
            if (!expect_operand) {
                *error = "expected an operator before '('";
                return false;
            }
            ops.push_back(token.kind);
        } else if (token.kind == Kind::RParen) {
            if (expect_operand) {
                *error = "expected an operand before ')'";
                return false;
            }
            while (!ops.empty() && ops.back() != Kind::LParen) {
                if (!apply(values, ops, error)) return false;
            }
            if (ops.empty()) {
                *error = "unmatched ')'";
                return false;
            }
            ops.pop_back();
        } else {
            const int current = precedence(token.kind);
            if (expect_operand || current == 0) {
                *error = "expected an operand or received an invalid operator";
                return false;
            }

            while (!ops.empty() && precedence(ops.back()) >= current) {
                if (!apply(values, ops, error)) return false;
            }
            ops.push_back(token.kind);
            expect_operand = true;
        }
    }

    if (expect_operand) {
        *error = "empty expression or missing operand";
        return false;
    }
    while (!ops.empty()) {
        if (!apply(values, ops, error)) return false;
    }
    if (values.size() != 1) {
        *error = "expression did not produce one value";
        return false;
    }
    *out = values.back();
    return true;
}
