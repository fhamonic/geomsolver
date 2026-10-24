#include "schema.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <mutex>
#include <optional>
#include <string_view>

#include <nlohmann/json-schema.hpp>

#include "gs/engine/keys.hpp"

namespace gs::detail {
namespace {

// Name-keyed maps use patternProperties {"": ...}, never additionalProperties:
// json-schema-validator 2.3 reports only the first error below an
// additionalProperties schema, at the parent's pointer ("design" instead of
// "design.q.type").
constexpr const char * kDefinitions = R"JSON({
  "number_or_expr": {"type": ["number", "string"]},
  "point2": {"type": "array", "items": {"type": "number"}, "minItems": 2, "maxItems": 2},
  "unit": {"enum": ["deg", "rad", "m", "cm", "mm", "%"]},
  "geometry_entry": {
    "type": "object",
    "required": ["type"],
    "properties": {
      "type": {"enum": ["point", "rect", "circle", "polyline"]},
      "x": {"type": "number"},
      "y": {"type": "number"},
      "width": {"type": "number", "minimum": 0},
      "height": {"type": "number", "minimum": 0},
      "angle": {"type": "number"},
      "r": {"type": "number", "minimum": 0},
      "points": {"type": "array", "items": {"$ref": "#/definitions/point2"}, "minItems": 2},
      "closed": {"type": "boolean"},
      "note": {"type": "string"}
    },
    "allOf": [
      {"if": {"properties": {"type": {"const": "point"}}}, "then": {"required": ["x", "y"]}},
      {"if": {"properties": {"type": {"const": "rect"}}}, "then": {"required": ["x", "y", "width", "height"]}},
      {"if": {"properties": {"type": {"const": "circle"}}}, "then": {"required": ["x", "y", "r"]}},
      {"if": {"properties": {"type": {"const": "polyline"}}}, "then": {"required": ["points"]}}
    ]
  },
  "geometry": {"type": "object", "patternProperties": {"": {"$ref": "#/definitions/geometry_entry"}}},
  "sketch_value": {"anyOf": [{"type": "array", "items": {"$ref": "#/definitions/number_or_expr"},
                              "minItems": 2, "maxItems": 2},
                             {"type": "string"}]},
  "sketch_element": {
    "type": "object",
    "required": ["type"],
    "properties": {
      "type": {"enum": ["point", "segment", "polyline", "polygon", "box", "circle", "arc"]},
      "value": {"$ref": "#/definitions/sketch_value"},
      "from": {"type": "string"},
      "to": {"type": "string"},
      "points": {"type": "array", "items": {"type": "string"}, "minItems": 2},
      "corners": {"type": "array", "items": {"type": "string"}, "minItems": 2, "maxItems": 2},
      "center": {"type": "string"},
      "radius": {"$ref": "#/definitions/number_or_expr"},
      "start": {"$ref": "#/definitions/number_or_expr"},
      "end": {"$ref": "#/definitions/number_or_expr"},
      "helper": {"type": "boolean"},
      "fixed": {"type": "boolean"},
      "note": {"type": "string"}
    }
  },
  "sketch_elements": {"type": "object", "patternProperties": {"": {"$ref": "#/definitions/sketch_element"}}},
  "sketch": {
    "type": "object",
    "properties": {
      "extent": {"type": "string"},
      "chord_error": {"$ref": "#/definitions/number_or_expr"},
      "elements": {"$ref": "#/definitions/sketch_elements"},
      "bodies": {
        "type": "object",
        "patternProperties": {"": {
          "type": "object",
          "required": ["pose", "elements"],
          "properties": {
            "pose": {
              "type": "object",
              "required": ["origin", "angle"],
              "properties": {
                "origin": {"type": "object", "required": ["value"],
                           "properties": {"value": {"$ref": "#/definitions/sketch_value"}}},
                "angle": {"type": "object", "required": ["value"],
                          "properties": {"value": {"$ref": "#/definitions/number_or_expr"}}}
              }
            },
            "fixed": {"type": "boolean"},
            "elements": {"$ref": "#/definitions/sketch_elements"}
          }
        }}
      }
    }
  }
})JSON";

// Every key of the key table (gs/engine/keys.hpp) is declared here, in an
// object's base "properties" even when only some types take it: node_at()
// does not look under allOf/if/then, and test_keys.cpp fails on a key one
// declares and the other does not. The solver's keys have empty schemas:
// gs_solve checks their values, and only warns.
constexpr const char * kInstance = R"JSON({
  "type": "object",
  "required": ["format"],
  "properties": {
    "format": {"enum": ["geomsolver-instance/1", "geomsolver-instance/2"]},
    "description": {"type": "string"},
    "include": {"type": "array", "items": {"type": "string"}},
    "params": {"type": "object", "patternProperties": {"": {"$ref": "#/definitions/number_or_expr"}}},
    "geometry": {"$ref": "#/definitions/geometry"},
    "design": {
      "type": "object",
      "patternProperties": {"": {
        "type": "object",
        "required": ["type"],
        "properties": {
          "type": {"enum": ["scalar", "point"]},
          "min": {"$ref": "#/definitions/number_or_expr"},
          "max": {"$ref": "#/definitions/number_or_expr"},
          "domain": {"type": "string"},
          "value": {"anyOf": [{"type": "number"}, {"$ref": "#/definitions/point2"}]},
          "fixed": {"type": "boolean"},
          "unit": {"$ref": "#/definitions/unit"},
          "note": {"type": "string"}
        },
        "allOf": [
          {"if": {"properties": {"type": {"const": "scalar"}}},
           "then": {"required": ["min", "max"], "properties": {"value": {"type": "number"}}}},
          {"if": {"properties": {"type": {"const": "point"}}},
           "then": {"required": ["domain"], "properties": {"value": {"$ref": "#/definitions/point2"}}}}
        ]
      }}
    },
    "sweeps": {
      "type": "object",
      "patternProperties": {"": {
        "type": "object",
        "required": ["min", "max"],
        "properties": {
          "min": {"$ref": "#/definitions/number_or_expr"},
          "max": {"$ref": "#/definitions/number_or_expr"},
          "samples": {"type": "integer", "minimum": 1, "maximum": 1000000},
          "sampling": {"enum": ["adaptive", "fixed"]},
          "note": {"type": "string"}
        }
      }}
    },
    "let": {"type": "object", "patternProperties": {"": {"type": "string"}}},
    "constraints": {
      "type": "array",
      "items": {
        "type": "object",
        "required": ["name", "expr"],
        "properties": {
          "name": {"type": "string"},
          "expr": {"type": "string"},
          "forall": {"type": "string"},
          "enabled": {"type": "boolean"},
          "note": {"type": "string"},
          "frame": {"type": "string"},
          "driving": {"type": "boolean"},
          "label": {"type": "object", "required": ["offset"],
                    "properties": {"offset": {"$ref": "#/definitions/point2"}}}
        }
      }
    },
    "criteria": {
      "type": "array",
      "items": {
        "type": "object",
        "required": ["name", "expr", "role"],
        "properties": {
          "name": {"type": "string"},
          "expr": {"type": "string"},
          "role": {"enum": ["minimize", "maximize", "max", "min", "range", "report"]},
          "bound": {"anyOf": [{"$ref": "#/definitions/number_or_expr"},
                              {"type": "array", "items": {"$ref": "#/definitions/number_or_expr"},
                               "minItems": 2, "maxItems": 2}]},
          "unit": {"$ref": "#/definitions/unit"},
          "enabled": {"type": "boolean"},
          "note": {"type": "string"}
        },
        "allOf": [{"if": {"properties": {"role": {"enum": ["max", "min", "range"]}}}, "then": {"required": ["bound"]}}]
      }
    },
    "display": {
      "type": "array",
      "items": {
        "type": "object",
        "required": ["expr"],
        "properties": {
          "expr": {"type": "string"},
          "color": {"type": "string", "pattern": "^#([0-9a-fA-F]{6}|[0-9a-fA-F]{8})$"},
          "fill": {"type": "boolean"},
          "width": {"type": "number", "minimum": 0},
          "label": {"type": "string"},
          "ghosts": {"type": "integer", "minimum": 0, "maximum": 1000},
          "trace": {"type": "boolean"},
          "visible": {"type": "boolean"},
          "note": {"type": "string"}
        }
      }
    },
    "solver": {"type": "object", "properties": {
      "algorithm": {}, "starts": {}, "seed": {}, "threads": {}, "phase1": {}, "initial_samples": {},
      "verify_samples": {}, "max_exchange_iterations": {}, "feas_tol": {}, "maxeval": {}, "maxtime": {},
      "xtol_rel": {}, "constraint_tol": {}, "include_current": {}, "cluster_x_tol": {},
      "cluster_f_tol": {}, "pareto_starts": {}
    }},
    "sketch": {"$ref": "#/definitions/sketch"}
  }
})JSON";

constexpr const char * kGeometryFile = R"JSON({
  "type": "object",
  "required": ["geometry"],
  "properties": {
    "units": {"const": "m"},
    "geometry": {"$ref": "#/definitions/geometry"}
  }
})JSON";

// The schema node that validates `ptr`, or null. Walks properties,
// patternProperties and items, following $ref into definitions; a node
// reached only through allOf/if/then is not found.
const nlohmann::json * node_at(const nlohmann::json & schema,
                               const nlohmann::json::json_pointer & ptr) {
    const nlohmann::json * node = &schema;
    auto deref = [&](const nlohmann::json * n) {
        while(n && n->is_object() && n->contains("$ref")) {
            const std::string ref = (*n)["$ref"].get<std::string>();
            constexpr std::string_view prefix = "#/definitions/";
            if(!ref.starts_with(prefix))
                return static_cast<const nlohmann::json *>(nullptr);
            const auto & defs = schema["definitions"];
            const std::string name = ref.substr(prefix.size());
            n = defs.contains(name) ? &defs[name] : nullptr;
        }
        return n;
    };
    node = deref(node);
    const std::string text = ptr.to_string();
    if(!text.empty() && text[0] != '/') return nullptr;
    std::vector<std::string> tokens;
    for(std::size_t i = 0; i < text.size(); ++i) {
        if(text[i] == '/') {
            tokens.emplace_back();
        } else if(text[i] == '~' && i + 1 < text.size()) {
            tokens.back() += text[i + 1] == '1' ? '/' : '~';
            ++i;
        } else {
            tokens.back() += text[i];
        }
    }
    for(const std::string & tok : tokens) {
        if(!node || !node->is_object()) return nullptr;
        if(node->contains("properties") && (*node)["properties"].contains(tok))
            node = &(*node)["properties"][tok];
        else if(node->contains("patternProperties") &&
                (*node)["patternProperties"].contains(""))
            node = &(*node)["patternProperties"][""];
        else if(node->contains("items"))
            node = &(*node)["items"];
        else
            return nullptr;
        node = deref(node);
    }
    return node && node->is_object() ? node : nullptr;
}

std::string value_text(const nlohmann::json & v) {
    return v.is_string() ? v.get<std::string>() : v.dump();
}

// JSON text of a value given in the document, cut short: it can be a whole
// object.
std::string short_dump(const nlohmann::json & v) {
    std::string t = v.dump();
    if(t.size() > 40) t = t.substr(0, 37) + "...";
    return t;
}

// "a string", "a number or a string": the "type" of a schema node.
std::string type_text(const nlohmann::json & type) {
    auto one = [](const std::string & t) -> std::string {
        if(t == "integer" || t == "object" || t == "array") return "an " + t;
        if(t == "boolean") return "true or false";
        return "a " + t;
    };
    if(type.is_string()) return one(type.get<std::string>());
    std::string out;
    for(std::size_t i = 0; i < type.size(); ++i)
        if(type[i].is_string())
            out +=
                (out.empty() ? "" : " or ") + one(type[i].get<std::string>());
    return out;
}

// "a number": the JSON type of a value, as type_text words it.
std::string instance_text(const nlohmann::json & v) {
    if(v.is_boolean()) return "a boolean";
    if(v.is_null()) return "null";
    if(v.is_number()) return "a number";
    const std::string t = v.type_name();
    return (t == "object" || t == "array" ? "an " : "a ") + t;
}

class Collector : public nlohmann::json_schema::basic_error_handler {
public:
    Collector(std::vector<Diagnostic> & out, std::string prefix,
              const nlohmann::json & schema)
        : out_(out), prefix_(std::move(prefix)), schema_(schema) {}
    void error(const nlohmann::json::json_pointer & ptr,
               const nlohmann::json & instance,
               const std::string & message) override {
        basic_error_handler::error(ptr, instance, message);
        Diagnostic d;
        constexpr std::string_view noise =
            "at least one subschema has failed, but all of them are required "
            "to validate - ";
        std::string msg = message;
        if(msg.starts_with(noise)) msg.erase(0, noise.size());
        // The validator's own texts name neither the expected nor the given
        // value; the schema node at the error has the first.
        const nlohmann::json * node = node_at(schema_, ptr);
        if(msg == "instance not found in required enum" && node &&
           node->contains("enum")) {
            msg = "expected one of ";
            const nlohmann::json & values = (*node)["enum"];
            for(std::size_t i = 0; i < values.size(); ++i)
                msg += (i ? ", " : "") + value_text(values[i]);
        } else if(msg == "instance not const" && node &&
                  node->contains("const")) {
            msg = std::format("expected {}, got {}", (*node)["const"].dump(),
                              short_dump(instance));
        } else if(msg == "unexpected instance type" && node &&
                  node->contains("type")) {
            msg = std::format("expected {}, got {}", type_text((*node)["type"]),
                              instance_text(instance));
        }
        const std::string p = pointer_to_path(ptr.to_string());
        d.path =
            prefix_.empty() ? p : (p.empty() ? prefix_ : prefix_ + ": " + p);
        d.message = std::move(msg);
        out_.push_back(std::move(d));
    }

private:
    std::vector<Diagnostic> & out_;
    std::string prefix_;
    const nlohmann::json & schema_;
};

nlohmann::json make_schema(const char * body) {
    nlohmann::json s = nlohmann::json::parse(body);
    s["$schema"] = "http://json-schema.org/draft-07/schema#";
    s["definitions"] = nlohmann::json::parse(kDefinitions);
    return s;
}

const nlohmann::json & instance_schema() {
    static const nlohmann::json schema = make_schema(kInstance);
    return schema;
}

std::vector<Diagnostic> run(const char * body,
                            const nlohmann::ordered_json & doc,
                            const std::string & prefix) {
    // json_validator::validate is const but not documented as thread-safe, so
    // concurrent compile() calls are serialised here.
    static std::mutex mutex;
    const nlohmann::json & instance_schema = detail::instance_schema();
    static const nlohmann::json geometry_schema = make_schema(kGeometryFile);
    static nlohmann::json_schema::json_validator instance_validator(
        instance_schema);
    static nlohmann::json_schema::json_validator geometry_validator(
        geometry_schema);
    std::vector<Diagnostic> out;
    Collector collector(out, prefix,
                        body == kInstance ? instance_schema : geometry_schema);
    std::optional<nlohmann::json> plain = plain_copy(doc);
    if(!plain) {
        std::string text;
        try {
            text = doc.dump();
        } catch(const nlohmann::json::exception & e) {
            // Only a document built in memory can get here (the parser
            // rejects invalid UTF-8): report it rather than throw out of
            // compile().
            out.push_back({Diagnostic::Severity::Error, prefix, -1,
                           std::string("the document contains text that is "
                                       "not valid UTF-8: ") +
                               e.what()});
            return out;
        }
        plain = nlohmann::json::parse(text);
    }
    const std::lock_guard<std::mutex> lock(mutex);
    (body == kInstance ? instance_validator : geometry_validator)
        .validate(*plain, collector);
    return out;
}

// Well-formed UTF-8 (the Unicode standard's table 3-7: no overlong form, no
// surrogate, nothing above U+10FFFF), which dump() checks before it writes.
bool utf8(std::string_view s) {
    std::size_t i = 0;
    while(i < s.size()) {
        const auto b = static_cast<unsigned char>(s[i]);
        if(b < 0x80) {
            ++i;
            continue;
        }
        int n = 0;
        unsigned char lo = 0x80, hi = 0xbf;
        if(b >= 0xc2 && b <= 0xdf) {
            n = 1;
        } else if(b >= 0xe0 && b <= 0xef) {
            n = 2;
            if(b == 0xe0) lo = 0xa0;
            if(b == 0xed) hi = 0x9f;
        } else if(b >= 0xf0 && b <= 0xf4) {
            n = 3;
            if(b == 0xf0) lo = 0x90;
            if(b == 0xf4) hi = 0x8f;
        } else {
            return false;
        }
        if(s.size() - i <= static_cast<std::size_t>(n)) return false;
        for(int k = 1; k <= n; ++k) {
            const auto c =
                static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]);
            if(c < (k == 1 ? lo : 0x80) || c > (k == 1 ? hi : 0xbf))
                return false;
        }
        i += static_cast<std::size_t>(n) + 1;
    }
    return true;
}

bool copy_into(const nlohmann::ordered_json & in, nlohmann::json & out) {
    using T = nlohmann::ordered_json::value_t;
    switch(in.type()) {
        case T::null:
            out = nullptr;
            return true;
        case T::boolean:
            out = in.get<bool>();
            return true;
        case T::number_integer: {
            const auto v = in.get<std::int64_t>();
            if(v >= 0)
                out = static_cast<std::uint64_t>(v);
            else
                out = v;
            return true;
        }
        case T::number_unsigned:
            out = in.get<std::uint64_t>();
            return true;
        case T::number_float: {
            const double v = in.get<double>();
            if(std::isfinite(v))
                out = v;
            else
                out = nullptr;
            return true;
        }
        case T::string: {
            const std::string & v = in.get_ref<const std::string &>();
            if(!utf8(v)) return false;
            out = v;
            return true;
        }
        case T::array:
            out = nlohmann::json::array();
            for(const nlohmann::ordered_json & v : in) {
                out.push_back(nullptr);
                if(!copy_into(v, out.back())) return false;
            }
            return true;
        case T::object:
            out = nlohmann::json::object();
            for(const auto & [key, v] : in.items()) {
                if(!utf8(key)) return false;
                if(!copy_into(v, out[key])) return false;
            }
            return true;
        default:
            return false;
    }
}

}  // namespace

std::optional<nlohmann::json> plain_copy(const nlohmann::ordered_json & doc) {
    nlohmann::json out;
    if(!copy_into(doc, out)) return std::nullopt;
    return out;
}

std::string pointer_to_path(const std::string & pointer) {
    std::string out;
    std::size_t i = 0;
    while(i < pointer.size()) {
        if(pointer[i] != '/') {
            ++i;
            continue;
        }
        std::size_t j = pointer.find('/', i + 1);
        if(j == std::string::npos) j = pointer.size();
        std::string tok;
        for(std::size_t k = i + 1; k < j; ++k) {
            if(pointer[k] == '~' && k + 1 < j) {
                tok += pointer[k + 1] == '1' ? '/' : '~';
                ++k;
            } else {
                tok += pointer[k];
            }
        }
        const bool index =
            !tok.empty() &&
            tok.find_first_not_of("0123456789") == std::string::npos;
        if(index)
            out += "[" + tok + "]";
        else
            out += (out.empty() ? "" : ".") + tok;
        i = j;
    }
    return out;
}

const nlohmann::json * schema_node(const std::string & pointer) {
    return node_at(instance_schema(), nlohmann::json::json_pointer(pointer));
}

std::vector<std::string> format2_keys(const nlohmann::ordered_json & doc) {
    std::vector<std::string> out;
    for(const SectionObject & o : section_objects(doc, ""))
        for(const auto & [key, v] : o.object->items()) {
            const InstanceKey * k = o.section->find(key);
            if(k && (k->needs2 == Needs2::Always ||
                     (k->needs2 == Needs2::WhenFalse && v.is_boolean() &&
                      !v.get<bool>())))
                out.push_back(o.path.empty() ? key : o.path + "." + key);
        }
    return out;
}

std::vector<Diagnostic> validate_instance_schema(
    const nlohmann::ordered_json & doc) {
    return run(kInstance, doc, "");
}

std::vector<Diagnostic> validate_geometry_file_schema(
    const nlohmann::ordered_json & doc, const std::string & file) {
    return run(kGeometryFile, doc, file);
}

}  // namespace gs::detail
