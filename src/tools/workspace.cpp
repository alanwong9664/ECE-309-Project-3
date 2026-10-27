#include "tools/workspace.h"

namespace fs = std::filesystem;

bool Workspace::resolve(std::string_view user_path, fs::path* out,
                        std::string* reason) const {
    if (user_path.empty()) {
        *reason = "empty path";
        return false;
    }
    if (user_path.find('\0') != std::string_view::npos) {
        *reason = "path contains a NUL byte";
        return false;
    }

    try {
        const fs::path path{std::string(user_path)};
        if (path.is_absolute() || path.has_root_path()) {
            *reason = "path must be relative to the workspace";
            return false;
        }

        const fs::path normalized = path.lexically_normal();
        if (normalized.begin() != normalized.end() && *normalized.begin() == "..") {
            *reason = "path escapes the workspace";
            return false;
        }

        *out = normalized == "." ? root() : root() / normalized;
        reason->clear();
        return true;
    } catch (...) {
        *reason = "invalid workspace path";
        return false;
    }
}
