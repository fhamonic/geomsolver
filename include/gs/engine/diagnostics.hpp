#pragma once

#include <string>
#include <vector>

namespace gs {

struct Diagnostic {
    enum class Severity { Error, Warning };
    Severity severity = Severity::Error;
    // JSON path of the offending value, e.g. "let.p", "constraints[3].expr",
    // "design.A.domain". Problems inside an included file are prefixed with
    // that file's include spec: "example_room.json: geometry.couch.width".
    std::string path;
    // 0-based column inside the expression text, -1 when not tied to one.
    int column = -1;
    std::string message;

    // "let.p at column 0: unknown function 'dyadd'". The column comes before
    // the message, which may end with a reference such as "the README".
    std::string to_string() const;
};

bool has_errors(const std::vector<Diagnostic> & diagnostics);
// One diagnostic per line.
std::string to_string(const std::vector<Diagnostic> & diagnostics);

}  // namespace gs
