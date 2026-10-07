#include "schema.hpp"

#include <format>
#include <mutex>

#include <nlohmann/json-schema.hpp>

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
  "geometry": {"type": "object", "patternProperties": {"": {"$ref": "#/definitions/geometry_entry"}}}
})JSON";

constexpr const char * kInstance = R"JSON({
  "type": "object",
  "required": ["format"],
  "properties": {
    "format": {"const": "geomsolver-instance/1"},
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
          "max": {"$ref": "#/definitions/number_or_expr"}
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
          "note": {"type": "string"}
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
          "role": {"enum": ["minimize", "maximize", "max", "min", "report"]},
          "bound": {"$ref": "#/definitions/number_or_expr"},
          "unit": {"$ref": "#/definitions/unit"},
          "note": {"type": "string"}
        },
        "allOf": [{"if": {"properties": {"role": {"enum": ["max", "min"]}}}, "then": {"required": ["bound"]}}]
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
          "trace": {"type": "boolean"}
        }
      }
    },
    "solver": {"type": "object"}
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
    std::string text;
    try {
        text = doc.dump();
    } catch(const nlohmann::json::exception & e) {
        // Only a document built in memory can get here (the parser rejects
        // invalid UTF-8): report it rather than throw out of compile().
        out.push_back({Diagnostic::Severity::Error, prefix, -1,
                       std::string("the document contains text that is not "
                                   "valid UTF-8: ") +
                           e.what()});
        return out;
    }
    const nlohmann::json plain = nlohmann::json::parse(text);
    const std::lock_guard<std::mutex> lock(mutex);
    (body == kInstance ? instance_validator : geometry_validator)
        .validate(plain, collector);
    return out;
}

}  // namespace

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

bool schema_lists_key(const std::string & parent_pointer,
                      const std::string & key) {
    const nlohmann::json * node = node_at(
        instance_schema(), nlohmann::json::json_pointer(parent_pointer));
    if(!node) return true;
    if(node->contains("properties") && (*node)["properties"].contains(key))
        return true;
    return !node->contains("properties") &&
           !node->contains("patternProperties");
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
