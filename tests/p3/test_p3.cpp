#include "../mini_test.h"
#include "../test_helpers.h"

#include "tools/calculator.h"
#include "tools/tool_registry.h"
#include "tools/workspace.h"

#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

class NamedTool : public Tool {
public:
    explicit NamedTool(std::string n) : n_(std::move(n)), schema_(JsonValue::object()) {}
    std::string name() const override { return n_; }
    std::string description() const override { return "test tool"; }
    const JsonValue& schema() const override { return schema_; }
    ToolResult invoke(const JsonValue&, ToolContext&) override { return ToolResult::success(n_); }
private:
    std::string n_;
    JsonValue   schema_;
};

class CountedTool : public NamedTool {
public:
    CountedTool(std::string name, int& destroyed)
        : NamedTool(std::move(name)), destroyed_(destroyed) {}
    ~CountedTool() override { ++destroyed_; }
private:
    int& destroyed_;
};

bool eval(const std::string& expr, double* value, std::string* error) {
    std::vector<CalcToken> tokens;
    if (!calc_tokenize(expr, &tokens, error)) return false;
    return calc_evaluate(tokens, value, error);
}

}

TEST(reg_add_and_find) {
    ToolRegistry r;
    auto t = std::make_unique<NamedTool>("alpha");
    Tool* raw = t.get();
    REQUIRE(r.add(std::move(t)));
    CHECK(r.size() == 1);
    CHECK(r.find("alpha") == raw);
    CHECK(r.find("beta") == nullptr);
}

TEST(reg_duplicate_keeps_original) {
    ToolRegistry r;
    auto first = std::make_unique<NamedTool>("alpha");
    Tool* original = first.get();
    REQUIRE(r.add(std::move(first)));
    CHECK(!r.add(std::make_unique<NamedTool>("alpha")));
    CHECK(r.find("alpha") == original);
    CHECK(r.size() == 1);
}

TEST(ws_rejects_parent_escape) {
    TempDir d;
    Workspace ws(d.path());
    fs::path out;
    std::string reason;
    REQUIRE(ws.resolve("notes/todo.txt", &out, &reason));
    CHECK(out == ws.root() / "notes" / "todo.txt");
    CHECK(!ws.resolve("../etc/passwd", &out, &reason));
    CHECK(!reason.empty());
}

TEST(calc_precedence) {
    double v = 0;
    std::string err;
    REQUIRE(eval("2 + 3 * 4", &v, &err));
    CHECK(v == 14);
}

TEST(calc_division_by_zero) {
    double v = 0;
    std::string err;
    CHECK(!eval("1 / 0", &v, &err));
    CHECK(err == "division by zero");
}

TEST(reg_empty_table) {
    ToolRegistry r;
    CHECK(r.size() == 0);
    CHECK(r.bucket_count() >= 1);
    CHECK(r.bucket_count() <= 64);
    CHECK(r.load_factor() == 0.0);
    CHECK(r.max_probe_length() == 0);
    CHECK(r.find("missing") == nullptr);
    CHECK(r.find("") == nullptr);
    int visits = 0;
    r.for_each([&](const Tool&) { ++visits; });
    CHECK(visits == 0);
}

TEST(reg_null_rejected) {
    ToolRegistry r;
    CHECK(!r.add(nullptr));
    CHECK(r.size() == 0);
    REQUIRE(r.add(std::make_unique<NamedTool>("kept")));
    Tool* original = r.find("kept");
    CHECK(!r.add(nullptr));
    CHECK(r.size() == 1);
    CHECK(r.find("kept") == original);
}

TEST(reg_for_each_visits_each_once) {
    ToolRegistry r;
    std::map<std::string, const Tool*> expected;
    for (int i = 0; i < 100; ++i) {
        auto tool = std::make_unique<NamedTool>("visit_" + std::to_string(i));
        expected.emplace(tool->name(), tool.get());
        REQUIRE(r.add(std::move(tool)));
    }
    const ToolRegistry& view = r;
    std::map<std::string, int> visits;
    view.for_each([&](const Tool& tool) {
        const auto found = expected.find(tool.name());
        REQUIRE(found != expected.end());
        CHECK(found->second == &tool);
        ++visits[tool.name()];
    });
    CHECK(visits.size() == expected.size());
    for (const auto& entry : expected) CHECK(visits[entry.first] == 1);
}

TEST(reg_collisions_still_find_every_key) {
    ToolRegistry r;
    const std::size_t initial = r.bucket_count();
    std::vector<std::string> colliding;

    for (int i = 0; colliding.size() < 6; ++i) {
        const std::string name = "collision_" + std::to_string(i);
        if (ToolRegistry::hash(name) % initial == 0) colliding.push_back(name);
    }
    for (const auto& name : colliding) REQUIRE(r.add(std::make_unique<NamedTool>(name)));
    for (const auto& name : colliding) {
        Tool* tool = r.find(name);
        REQUIRE(tool != nullptr);
        CHECK(tool->name() == name);
    }
    if (r.bucket_count() == initial) CHECK(r.max_probe_length() == colliding.size());
    CHECK(r.find("absent_collision") == nullptr);
    CHECK(r.size() == colliding.size());
}

TEST(reg_owns_tools_and_destroys_rejected_duplicate) {
    int destroyed = 0;
    {
        ToolRegistry r;
        auto first = std::make_unique<CountedTool>("alpha", destroyed);
        Tool* original = first.get();
        REQUIRE(r.add(std::move(first)));
        CHECK(destroyed == 0);
        CHECK(!r.add(std::make_unique<CountedTool>("alpha", destroyed)));
        CHECK(destroyed == 1);
        CHECK(r.size() == 1);
        CHECK(r.find("alpha") == original);
        REQUIRE(r.add(std::make_unique<CountedTool>("beta", destroyed)));

        for (int i = 0; i < 100; ++i)
            REQUIRE(r.add(std::make_unique<NamedTool>("extra_" + std::to_string(i))));
        CHECK(destroyed == 1);
        CHECK(r.find("alpha") == original);
        CHECK(r.size() == 102);
    }
    CHECK(destroyed == 3);
}

TEST(reg_hash_uses_full_ordered_strings) {
    CHECK(ToolRegistry::hash("ab") != ToolRegistry::hash("ba"));
    CHECK(ToolRegistry::hash("tool_1234") != ToolRegistry::hash("tool_1243"));
    CHECK(ToolRegistry::hash("read_file") > 64);
    const std::string embedded = std::string("a") + '\0' + "b";
    CHECK(ToolRegistry::hash(embedded) != ToolRegistry::hash("a"));
    std::set<std::size_t> hashes;
    for (int i = 0; i < 10000; ++i)
        hashes.insert(ToolRegistry::hash("tool_" + std::to_string(i)));
    CHECK(hashes.size() >= 9990);
}

TEST(reg_empty_and_embedded_nul_names) {
    ToolRegistry r;
    REQUIRE(r.add(std::make_unique<NamedTool>("")));
    const std::string name = std::string("tool") + '\0' + "suffix";
    REQUIRE(r.add(std::make_unique<NamedTool>(name)));
    REQUIRE(r.find("") != nullptr);
    REQUIRE(r.find(name) != nullptr);
    CHECK(r.find(name)->name() == name);
    CHECK(r.find("tool") == nullptr);
    CHECK(r.size() == 2);
}

TEST(reg_10000_tools_and_alternative_hash) {
    ToolRegistry r;
    const std::size_t initial = r.bucket_count();
    std::size_t previous = initial;
    std::vector<Tool*> originals;
    for (int i = 0; i < 10000; ++i) {
        const std::string name = "tool_" + std::to_string(i);
        REQUIRE(r.add(std::make_unique<NamedTool>(name)));
        Tool* pointer = r.find(name);
        REQUIRE(pointer != nullptr);
        originals.push_back(pointer);
        CHECK(r.size() == static_cast<std::size_t>(i + 1));
        CHECK(r.load_factor() <= 1.0);
        if (r.bucket_count() != previous) {
            CHECK(r.bucket_count() == previous * 2);
            previous = r.bucket_count();
        }
        CHECK(r.find("tool_0") == originals.front());
    }
    REQUIRE(r.bucket_count() > initial);
    CHECK(r.size() == 10000);
    CHECK(r.find("tool_10000") == nullptr);
    CHECK(r.max_probe_length() <= 64);

    std::vector<int> real_counts(r.bucket_count(), 0);
    std::vector<int> weak_counts(r.bucket_count(), 0);
    for (int i = 0; i < 10000; ++i) {
        const std::string name = "tool_" + std::to_string(i);
        CHECK(r.find(name) == originals[static_cast<std::size_t>(i)]);
        ++real_counts[ToolRegistry::hash(name) % r.bucket_count()];
        const std::size_t weak = static_cast<unsigned char>(name[0]) + name.size();
        ++weak_counts[weak % r.bucket_count()];
    }
    int real_longest = 0;
    int weak_longest = 0;
    for (std::size_t i = 0; i < r.bucket_count(); ++i) {
        if (real_counts[i] > real_longest) real_longest = real_counts[i];
        if (weak_counts[i] > weak_longest) weak_longest = weak_counts[i];
    }
    CHECK(r.max_probe_length() == static_cast<std::size_t>(real_longest));
    CHECK(weak_longest == 9000);
    std::printf("  10000 tools: buckets=%zu load_factor=%.8f max_probe_length=%zu\n"
                "  weak hash, same buckets: longest_chain=%d\n",
                r.bucket_count(), r.load_factor(), r.max_probe_length(), weak_longest);
}

TEST(ws_accepts_every_required_path) {
    TempDir d;
    Workspace ws(d.path());
    const std::vector<std::pair<std::string, fs::path>> cases = {
        {"notes/todo.txt", "notes/todo.txt"},
        {"./notes//todo.txt", "notes/todo.txt"},
        {".", "."},
        {"a/../b", "b"},
        {"a/..", "."},
        {"new/dir/file.txt", "new/dir/file.txt"},
        {"my..notes.txt", "my..notes.txt"},
    };
    for (const auto& row : cases) {
        fs::path out;
        std::string reason;
        std::printf("  accepting path: %s\n", row.first.c_str());
        REQUIRE(ws.resolve(row.first, &out, &reason));
        const fs::path expected = row.second == "." ? ws.root() : ws.root() / row.second;
        CHECK(out == expected);
        CHECK(out.is_absolute());
    }
    CHECK(fs::is_empty(d.path()));
}

TEST(ws_rejects_every_required_path) {
    TempDir d;
    Workspace ws(d.path() / "ws");
    const std::vector<std::string> cases = {
        "", std::string("notes") + '\0' + "/x", "/etc/passwd", "..",
        "../etc/passwd", "a/../../b", "notes/../../ws/notes/todo.txt",
    };
    for (std::size_t i = 0; i < cases.size(); ++i) {
        fs::path out = "unchanged";
        std::string reason;
        std::printf("  rejecting path case: %zu\n", i);
        CHECK(!ws.resolve(cases[i], &out, &reason));
        CHECK(!reason.empty());
        CHECK(out == "unchanged");
        if (cases[i].empty()) CHECK(reason == "empty path");
    }
    CHECK(fs::is_empty(d.path()));
}

TEST(ws_rejects_absolute_path_even_inside_root) {
    TempDir d;
    Workspace ws(d.path());
    fs::path out;
    std::string reason;
    CHECK(!ws.resolve((ws.root() / "safe.txt").string(), &out, &reason));
    CHECK(!reason.empty());
}

TEST(ws_resolution_is_lexical_before_symlink_check) {
    TempDir d;
    TempDir outside;
    Workspace ws(d.path());
    fs::create_directory_symlink(outside.path(), d.path() / "link");
    fs::path out;
    std::string reason;
    REQUIRE(ws.resolve("link/new.txt", &out, &reason));
    CHECK(out == ws.root() / "link/new.txt");

    CHECK(!ws.check_symlinks(out, &reason));
    CHECK(!reason.empty());
    CHECK(!fs::exists(outside.path() / "new.txt"));
}

TEST(ws_view_respects_length_without_terminating_nul) {
    TempDir d;
    Workspace ws(d.path());
    const std::string text = "notes/../bUNUSED";
    fs::path out;
    std::string reason;
    REQUIRE(ws.resolve(std::string_view(text.data(), 10), &out, &reason));
    CHECK(out == ws.root() / "b");
}

TEST(calc_subtraction_is_left_to_right) {
    double value = 0;
    std::string error;
    REQUIRE(eval("10 - 4 - 3", &value, &error));
    CHECK(value == 3);
}

TEST(calc_division_is_left_to_right) {
    double value = 0;
    std::string error;
    REQUIRE(eval("100 / 10 / 5", &value, &error));
    CHECK(value == 2);
}

TEST(calc_parentheses_override_precedence) {
    double value = 0;
    std::string error;
    REQUIRE(eval("(2 + 3) * 4", &value, &error));
    CHECK(value == 20);
    REQUIRE(eval("2 * (3 + 4) - 5", &value, &error));
    CHECK(value == 9);
}

TEST(calc_negative_numbers) {
    double value = 0;
    std::string error;
    REQUIRE(eval("-3 * -3", &value, &error));
    CHECK(value == 9);
    REQUIRE(eval("2*-3 + (-1)", &value, &error));
    CHECK(value == -7);
}

TEST(calc_decimals_and_fractional_division) {
    double value = 0;
    std::string error;
    REQUIRE(eval("7 / 2", &value, &error));
    CHECK(value == 3.5);
    REQUIRE(eval("1.25 + 2.5 * 2", &value, &error));
    CHECK(value == 6.25);
}

TEST(calc_nested_groups_and_operand_order) {
    double value = 0;
    std::string error;
    REQUIRE(eval("20 / (2 * (1 + 1)) - 7", &value, &error));
    CHECK(value == -2);
    REQUIRE(eval("18 - 12 / 3 * 2 + 1", &value, &error));
    CHECK(value == 11);
}

TEST(calc_computed_zero_denominator) {
    double value = 123;
    std::string error;
    CHECK(!eval("1 / (2 - 2)", &value, &error));
    CHECK(error == "division by zero");
    CHECK(value == 123);
    CHECK(!eval("1 / -0", &value, &error));
    CHECK(error == "division by zero");
}

TEST(calc_twenty_thousand_terms) {
    std::string expression = "1";
    for (int i = 1; i < 20000; ++i) expression += "+1";
    double value = 0;
    std::string error;
    REQUIRE(eval(expression, &value, &error));
    CHECK(value == 20000);
}

TEST(calc_five_thousand_nested_parentheses) {
    const std::string expression = std::string(5000, '(') + "2+3" + std::string(5000, ')');
    double value = 0;
    std::string error;
    REQUIRE(eval(expression, &value, &error));
    CHECK(value == 5);
}

TEST(calc_unexpected_tokens_fail_without_crashing) {
    using Kind = CalcToken::Kind;
    const CalcToken one{Kind::Number, 1};
    const CalcToken plus{Kind::Plus, 0};
    const CalcToken left{Kind::LParen, 0};
    const CalcToken right{Kind::RParen, 0};
    const std::vector<std::vector<CalcToken>> cases = {
        {}, {plus}, {one, plus}, {one, one}, {right}, {left, one},
        {one, right}, {left, right}, {one, plus, plus, one},
        {one, left, one, right}, {left, one, plus, right},
    };
    for (const auto& tokens : cases) {
        double value = 123;
        std::string error;
        CHECK(!calc_evaluate(tokens, &value, &error));
        CHECK(!error.empty());
        CHECK(value == 123);
    }
}

int main(int argc, char** argv) { return mt::run_all(argc, argv); }
