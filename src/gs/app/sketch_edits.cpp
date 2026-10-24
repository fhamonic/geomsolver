#include "gs/app/sketch_edits.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <utility>

#include "gs/app/entry_edits.hpp"
#include "gs/engine/evaluator.hpp"
#include "gs/engine/expression.hpp"
#include "gs/engine/kind.hpp"
#include "gs/engine/rename.hpp"

namespace gs::app {
namespace {

using Kind = SelKey::Kind;
using K = ConstraintKind;

std::size_t uz(int i) { return static_cast<std::size_t>(i); }

// "constraints[3].expr" -> "/constraints/3/expr"
nlohmann::json_pointer<std::string> pointer(std::string_view path) {
    std::string out;
    for(const char c : path) {
        if(c == '.' || c == '[')
            out += '/';
        else if(c == ']')
            continue;
        else if(c == '~')
            out += "~0";
        else if(c == '/')
            out += "~1";
        else
            out += c;
    }
    if(out.empty() || out.front() != '/') out.insert(out.begin(), '/');
    return nlohmann::json_pointer<std::string>(out);
}

const Json * at(const Json & doc, std::string_view path) {
    const auto p = pointer(path);
    return doc.contains(p) ? &doc.at(p) : nullptr;
}

const Json * member(const Json & j, std::string_view key) {
    if(!j.is_object()) return nullptr;
    const auto it = j.find(key);
    return it == j.end() ? nullptr : &*it;
}

std::string text_of(const Json * j) {
    return j && j->is_string() ? j->get<std::string>() : std::string();
}

bool flag(const Json & j, std::string_view key) {
    const Json * v = member(j, key);
    return v && *v == true;
}

// "P1" -> "sketch.elements.P1", "tv.c" -> "sketch.bodies.tv.elements.c".
std::string element_path(std::string_view name) {
    const std::size_t dot = name.find('.');
    if(dot == std::string_view::npos)
        return std::format("sketch.elements.{}", name);
    return std::format("sketch.bodies.{}.elements.{}", name.substr(0, dot),
                       name.substr(dot + 1));
}

// The body of a qualified name, "" for the world's.
std::string body_of(std::string_view name) {
    const std::size_t dot = name.find('.');
    return dot == std::string_view::npos ? std::string()
                                         : std::string(name.substr(0, dot));
}

// The path of the element whose entry holds `path`, "" outside one:
// "sketch.elements.L1" of "sketch.elements.L1.points[2]".
std::string element_root(std::string_view path) {
    auto name = [](std::string_view rest) {
        return rest.substr(0, rest.find_first_of(".["));
    };
    constexpr std::string_view world = "sketch.elements.",
                               bodies = "sketch.bodies.";
    if(path.starts_with(world)) {
        const std::string_view n = name(path.substr(world.size()));
        return n.empty() ? std::string() : std::format("{}{}", world, n);
    }
    if(!path.starts_with(bodies)) return {};
    const std::string_view rest = path.substr(bodies.size());
    const std::string_view body = name(rest);
    const std::string members = std::format("{}.elements.", body);
    if(body.empty() || !rest.starts_with(members)) return {};
    const std::string_view m = name(rest.substr(members.size()));
    return m.empty() ? std::string()
                     : std::format("{}{}{}", bodies, members, m);
}

struct Element {
    std::string name;  // "P1", "tv.c"
    std::string path;
    std::string body;  // "" for the world
    std::string type;
};

bool chain(const Element & e) {
    return e.type == "polyline" || e.type == "polygon";
}

std::vector<Element> sketch_elements(const Json & doc) {
    std::vector<Element> out;
    const Json * s = member(doc, "sketch");
    if(!s) return out;
    auto add = [&](const Json * map, const std::string & body) {
        if(!map || !map->is_object()) return;
        for(const auto & [n, e] : map->items()) {
            if(!e.is_object()) continue;
            const std::string name = body.empty() ? n : body + "." + n;
            out.push_back(
                {name, element_path(name), body, text_of(member(e, "type"))});
        }
    };
    add(member(*s, "elements"), "");
    if(const Json * b = member(*s, "bodies"); b && b->is_object())
        for(const auto & [n, body] : b->items())
            add(member(body, "elements"), n);
    return out;
}

struct Slot {
    std::string sub;   // "to", "points 2"
    std::string path;  // "sketch.elements.L1.to"
    std::string text;
};

std::vector<Slot> slots_of(const Json & doc, const Element & e) {
    std::vector<Slot> out;
    const Json * j = at(doc, e.path);
    if(!j || !j->is_object()) return out;
    auto one = [&](const char * key) {
        if(const Json * s = member(*j, key); s && s->is_string())
            out.push_back({key, std::format("{}.{}", e.path, key),
                           s->get<std::string>()});
    };
    if(e.type == "segment") {
        one("from");
        one("to");
    } else if(e.type == "circle" || e.type == "arc") {
        one("center");
    } else if(chain(e) || e.type == "box") {
        const char * key = e.type == "box" ? "corners" : "points";
        if(const Json * list = member(*j, key); list && list->is_array())
            for(std::size_t i = 0; i < list->size(); ++i)
                if((*list)[i].is_string())
                    out.push_back({std::format("{} {}", key, i),
                                   std::format("{}.{}[{}]", e.path, key, i),
                                   (*list)[i].get<std::string>()});
    }
    return out;
}

// The slot's text read the same however it is spaced: "A", "end(door)".
std::string canonical(std::string_view text) {
    const ParseResult p = parse_expression(text);
    return p.ok() ? to_string(*p.ast) : std::string(text);
}

// The point a slot shares, when its text is a bare name: "A", "tv.c".
std::optional<std::string> shared_point(std::string_view text) {
    const ParseResult p = parse_expression(text);
    if(!p.ok() || p.ast->kind != Ast::Kind::Name) return std::nullopt;
    return p.ast->text;
}

// The vertex and edge indices of a polyline or polygon after an edit; a
// part the edit removes or splits maps to nothing.
struct IndexMap {
    std::function<std::optional<int>(int)> vertex, edge;
    bool start_gone = false, end_gone = false;
};

struct Scanned {
    std::string text;      // with the indices rewritten
    bool gone = false;     // reads a vertex or an edge the edit removes
    bool unknown = false;  // an index that is not a number literal
};

// edge(c, i), vertex(c, i), start(c) and end(c) of chain `c` in `text`.
Scanned scan_indices(std::string_view text, std::string_view c,
                     const IndexMap & map) {
    Scanned out{std::string(text)};
    const ParseResult p = parse_expression(text);
    if(!p.ok()) return out;
    std::vector<std::pair<TextSpan, std::string>> edits;
    std::function<void(const Ast &)> walk = [&](const Ast & a) {
        const bool of_chain = a.kind == Ast::Kind::Call && !a.args.empty() &&
                              a.args[0]->kind == Ast::Kind::Name &&
                              a.args[0]->text == c;
        if(of_chain && a.args.size() == 2 &&
           (a.text == "vertex" || a.text == "edge")) {
            const Ast & i = *a.args[1];
            if(i.kind != Ast::Kind::Number || i.number < 0 ||
               i.number != std::floor(i.number) || i.number > 1e9) {
                out.unknown = true;
            } else {
                const int was = static_cast<int>(i.number);
                const std::optional<int> now =
                    a.text == "vertex" ? map.vertex(was) : map.edge(was);
                if(!now)
                    out.gone = true;
                else if(*now != was)
                    edits.push_back({{i.begin, i.end}, std::to_string(*now)});
            }
        }
        if(of_chain && a.args.size() == 1 &&
           ((a.text == "start" && map.start_gone) ||
            (a.text == "end" && map.end_gone)))
            out.gone = true;
        for(const auto & arg : a.args) walk(*arg);
    };
    walk(*p.ast);
    std::ranges::sort(edits, std::greater<>(),
                      [](const auto & e) { return e.first.begin; });
    for(const auto & [span, now] : edits)
        out.text.replace(uz(span.begin), uz(span.end - span.begin), now);
    return out;
}

// How Elements and the log name a constraint: its name, "constraints[3]"
// without one.
std::string constraint_label(const Json & doc, std::size_t i) {
    const Json * c = at(doc, std::format("constraints[{}]", i));
    const std::string name = c ? text_of(member(*c, "name")) : std::string();
    return name.empty() ? std::format("constraints[{}]", i) : name;
}

std::optional<std::size_t> constraint_index(std::string_view path) {
    constexpr std::string_view prefix = "constraints[";
    if(!path.starts_with(prefix)) return std::nullopt;
    std::size_t i = 0;
    const char * first = path.data() + prefix.size();
    const auto [last, ec] =
        std::from_chars(first, path.data() + path.size(), i);
    if(ec != std::errc() || last == path.data() + path.size() || *last != ']')
        return std::nullopt;
    return i;
}

std::string joined(const std::vector<std::string> & items) {
    std::string out;
    for(const std::string & s : items) out += (out.empty() ? "" : ", ") + s;
    return out;
}

// What an edit of the sketch takes with it (plan 4.10), worked out on a
// copy of the document whose entries stay in place while it is read: an
// erased entry is only marked, so that every path read keeps its meaning,
// and the batch erases them last, each array from its end.
class Cascade {
public:
    explicit Cascade(Json doc)
        : work_(std::move(doc)), elements_(sketch_elements(work_)) {
        for(std::size_t i = 0; i < elements_.size(); ++i)
            index_.emplace(elements_[i].path, i);
        std::vector<std::string> names;
        for(const Element & e : elements_) names.push_back(e.name);
        for(const std::string & b : bodies()) names.push_back(b);
        for(NameUse & u : find_name_uses(work_, names).uses)
            uses_[u.name].push_back(std::move(u));
    }

    const Json & work() const { return work_; }
    const std::vector<Element> & elements() const { return elements_; }
    const Element * find(std::string_view name) const {
        const auto it = index_.find(element_path(name));
        return it == index_.end() ? nullptr : &elements_[it->second];
    }

    // A field the edit writes, before the cascade reads the document. Its
    // value at the end is the batch's.
    void set(const std::string & path, Json value) {
        work_[pointer(path)] = std::move(value);
        touch(path);
    }
    void touch(const std::string & path) {
        if(!std::ranges::contains(touched_, path)) touched_.push_back(path);
    }

    // A point the orphan pass never erases: a merge's survivor, whose only
    // user may be the segment the merge collapses, which takes it otherwise.
    void keep(std::string name) { kept_.insert(std::move(name)); }

    // `why` is empty for an entry asked for, else why it goes with them.
    void erase_element(const Element & e, std::string why) {
        if(erased(e)) return;
        erased_.push_back(e.path);
        names_.push_back(e.name);
        queue_.push_back(e.name);
        if(!why.empty()) taken_.push_back(std::format("{} ({})", e.name, why));
        for(const Slot & s : slots_of(work_, e)) candidate(e, s.text);
    }
    void erase_body(const std::string & body) {
        const std::string path = std::format("sketch.bodies.{}", body);
        if(std::ranges::contains(erased_, path)) return;
        names_.push_back(body);
        queue_.push_back(body);
        // Before the body's path is marked: erase_element() takes a member
        // under it for one gone already, and its users would stay.
        for(const Element & e : elements_)
            if(e.body == body) erase_element(e, "");
        erased_.push_back(path);
    }
    // `same`, when not -1, is the vertex that vertex `j` has become (a merge
    // made them one point): the edges from `j` are then those from `same`,
    // and only the edge between them goes.
    void remove_vertex(const Element & e, int j, int same = -1) {
        if(same >= 0) alias_[e.path][j] = same;
        if(!removed_[e.path].insert(j).second) return;
        const std::string path = std::format("{}.points[{}]", e.path, j);
        const Json * p = at(work_, path);
        vertices_.push_back(std::format("{}: vertex {} ({})", e.name, j,
                                        p ? json_text(*p) : std::string()));
        if(p && p->is_string()) candidate(e, p->get<std::string>());
    }

    // Follows what was erased and removed to what must go with it, then the
    // constraints, then rewrites the indices of the polylines that lost
    // vertices.
    void close() {
        for(;;) {
            while(!queue_.empty()) {
                const std::string name = queue_.back();
                queue_.pop_back();
                follow(name);
            }
            bool more = false;
            for(const Element & e : elements_) {
                if(!chain(e) || erased(e) || !removed_.contains(e.path))
                    continue;
                const std::size_t left = count(e) - removed_[e.path].size();
                if(left < (e.type == "polygon" ? 3u : 2u)) {
                    erase_element(e, std::format("it keeps {} point{}", left,
                                                 left == 1 ? "" : "s"));
                    more = true;
                    continue;
                }
                for(const NameUse & u : uses(e.name)) {
                    const Element * holder = owner(u.path);
                    if(!holder || erased(*holder) || dead(u.path)) continue;
                    if(scan(u.path, e).gone) {
                        erase_element(*holder,
                                      std::format("it reads a vertex or an "
                                                  "edge {} loses",
                                                  e.name));
                        more = true;
                    }
                }
            }
            for(const std::string & p : std::exchange(candidates_, {})) {
                const Element * e = find(p);
                if(!e || erased(*e) || e->type != "point" || kept_.contains(p))
                    continue;
                if(std::ranges::all_of(uses(p), [&](const NameUse & u) {
                       return dead(u.path);
                   })) {
                    erase_element(*e, "");
                    orphans_.push_back(p);
                    more = true;
                }
            }
            if(!more && queue_.empty()) break;
        }
        constraints();
        rewrite();
    }

    bool dead(std::string_view path) const {
        if(const std::optional<std::size_t> i = constraint_index(path);
           i && std::ranges::contains(gone_constraints_, *i))
            return true;
        if(std::ranges::any_of(erased_, [&](const std::string & e) {
               return path_is_under(path, e);
           }))
            return true;
        for(const auto & [chain_path, removed] : removed_)
            for(const int j : removed)
                if(path_is_under(path,
                                 std::format("{}.points[{}]", chain_path, j)))
                    return true;
        return false;
    }
    bool erased(const Element & e) const {
        return std::ranges::any_of(erased_, [&](const std::string & p) {
            return path_is_under(e.path, p);
        });
    }

    // The batch: the fields written, then the erasures, each array from its
    // end so that no erase moves another's path.
    EditBatch batch(std::string label) const {
        EditBatch b(std::move(label));
        for(const std::string & path : touched_)
            if(!dead(path))
                if(const Json * v = at(work_, path)) b.set(path, *v);
        for(const std::string & path : erasures()) b.erase(path);
        return b;
    }
    std::vector<std::string> erasures() const {
        std::vector<std::string> out;
        for(const std::string & e : erased_)
            if(std::ranges::none_of(erased_, [&](const std::string & o) {
                   return o != e && path_is_under(e, o);
               }))
                out.push_back(e);
        for(const auto & [path, removed] : removed_) {
            if(dead(path)) continue;
            for(auto j = removed.rbegin(); j != removed.rend(); ++j)
                out.push_back(std::format("{}.points[{}]", path, *j));
        }
        std::vector<std::size_t> gone = gone_constraints_;
        std::ranges::sort(gone, std::greater<>());
        for(const std::size_t i : gone)
            out.push_back(std::format("constraints[{}]", i));
        return out;
    }

    // Erases constraint `i` with what it reads, listed.
    void erase_constraint(std::size_t i) {
        if(std::ranges::contains(gone_constraints_, i)) return;
        gone_constraints_.push_back(i);
        removed_names_.push_back(constraint_label(work_, i));
    }

    const std::vector<std::string> & names() const { return names_; }
    const std::vector<std::string> & taken() const { return taken_; }
    const std::vector<std::string> & orphans() const { return orphans_; }
    const std::vector<std::string> & vertices() const { return vertices_; }
    const std::vector<std::string> & removed() const { return removed_names_; }
    const std::vector<std::string> & check() const { return check_; }
    // Formula-kind constraints that name an erased element or body.
    const std::vector<std::string> & broken() const { return broken_; }
    const std::vector<std::string> & touched() const { return touched_; }
    std::vector<std::size_t> gone_constraints() const {
        return gone_constraints_;
    }
    std::vector<std::string> erased_paths() const {
        std::vector<std::string> out = erased_;
        for(const auto & [path, removed] : removed_)
            for(const int j : removed)
                out.push_back(std::format("{}.points[{}]", path, j));
        return out;
    }

private:
    std::vector<std::string> bodies() const {
        std::vector<std::string> out;
        if(const Json * b = at(work_, "sketch.bodies"); b && b->is_object())
            for(const auto & [n, body] : b->items()) out.push_back(n);
        return out;
    }

    const std::vector<NameUse> & uses(const std::string & name) const {
        static const std::vector<NameUse> none;
        const auto it = uses_.find(name);
        return it == uses_.end() ? none : it->second;
    }

    const Element * owner(std::string_view path) const {
        const auto it = index_.find(element_root(path));
        return it == index_.end() ? nullptr : &elements_[it->second];
    }

    std::size_t count(const Element & e) const {
        const Json * p = at(work_, e.path + ".points");
        return p && p->is_array() ? p->size() : 0;
    }

    // A point of `e`'s frame that a slot of `e` shares, which may be left
    // used by nothing once `e` goes.
    void candidate(const Element & e, std::string_view text) {
        const std::optional<std::string> p = shared_point(text);
        if(p && body_of(*p) == e.body) candidates_.push_back(*p);
    }

    // The elements that use `name`, which goes: a polyline's or polygon's
    // vertex is removed, any other element goes too.
    void follow(const std::string & name) {
        for(const NameUse & u : uses(name)) {
            if(dead(u.path)) continue;
            const Element * holder = owner(u.path);
            if(!holder || erased(*holder)) continue;
            if(chain(*holder)) {
                const std::string prefix = holder->path + ".points[";
                int j = -1;
                if(u.path.starts_with(prefix)) {
                    const char * first = u.path.data() + prefix.size();
                    std::from_chars(first, u.path.data() + u.path.size(), j);
                }
                if(j >= 0) {
                    remove_vertex(*holder, j);
                    continue;
                }
            }
            erase_element(*holder, std::format("it uses {}", name));
        }
    }

    IndexMap map(const Element & e) const {
        const auto it = removed_.find(e.path);
        const std::set<int> none;
        const std::set<int> & r = it == removed_.end() ? none : it->second;
        const auto at = alias_.find(e.path);
        const std::map<int, int> no_alias;
        const std::map<int, int> & same =
            at == alias_.end() ? no_alias : at->second;
        const int n = static_cast<int>(count(e));
        const bool polygon = e.type == "polygon";
        // The vertex `v` is now, before renumbering: itself, the one it
        // became, or none.
        auto kept = [r, same](int v) -> std::optional<int> {
            for(int hops = 0; r.contains(v); ++hops) {
                const auto s = same.find(v);
                if(s == same.end() || hops > static_cast<int>(same.size()))
                    return std::nullopt;
                v = s->second;
            }
            return v;
        };
        auto renumber = [r](int v) {
            return v -
                   static_cast<int>(std::distance(r.begin(), r.lower_bound(v)));
        };
        IndexMap m;
        // An index past the end stays as written: it named nothing before
        // the edit either.
        m.vertex = [kept, renumber, n](int v) -> std::optional<int> {
            if(v >= n) return v;
            const std::optional<int> k = kept(v);
            if(!k) return std::nullopt;
            return renumber(*k);
        };
        m.edge = [kept, renumber, n, polygon](int j) -> std::optional<int> {
            if(j >= (polygon ? n : n - 1)) return j;
            const std::optional<int> a = kept(j);
            const std::optional<int> b = kept(polygon ? (j + 1) % n : j + 1);
            if(!a || !b || *a == *b) return std::nullopt;
            return renumber(*a);
        };
        m.start_gone = !polygon && !kept(0);
        m.end_gone = !polygon && !kept(n - 1);
        return m;
    }

    Scanned scan(const std::string & path, const Element & e) const {
        return scan_indices(text_of(at(work_, path)), e.name, map(e));
    }

    // The constraints of a recognised kind that name what goes, or read an
    // edge or a vertex removed, go too; a Formula-kind one stays, listed.
    void constraints() {
        const KindNames kinds(work_);
        struct Hit {
            bool names_gone = false, reads_gone = false, unknown = false;
        };
        std::map<std::size_t, Hit> hits;  // in document order
        std::vector<std::string> elsewhere;
        for(const std::string & n : names_)
            for(const NameUse & u : uses(n))
                if(const auto i = constraint_index(u.path))
                    hits[*i].names_gone = true;
        for(const Element & e : elements_) {
            if(!chain(e) || erased(e) || !removed_.contains(e.path)) continue;
            for(const NameUse & u : uses(e.name)) {
                if(dead(u.path)) continue;
                const Scanned s = scan(u.path, e);
                if(const auto i = constraint_index(u.path)) {
                    hits[*i].reads_gone |= s.gone;
                    hits[*i].unknown |= s.unknown;
                } else if(s.gone || s.unknown) {
                    elsewhere.push_back(u.path);
                }
            }
        }
        for(const auto & [i, hit] : hits) {
            const std::string path = std::format("constraints[{}]", i);
            if(dead(path)) continue;
            if(!hit.names_gone && !hit.reads_gone) {
                if(hit.unknown) note(path);
                continue;
            }
            const KindMatch m =
                match_kind(text_of(at(work_, path + ".expr")), kinds);
            if(m.kind != K::Formula)
                erase_constraint(i);
            else if(hit.names_gone)
                broken_.push_back(constraint_label(work_, i));
            else
                note(path);
        }
        for(const std::string & path : elsewhere) note(path);
    }

    void note(const std::string & path) {
        const std::optional<std::size_t> i = constraint_index(path);
        std::string what = i ? constraint_label(work_, *i) : entry_root(path);
        if(what.empty()) what = path;
        if(!std::ranges::contains(check_, what)) check_.push_back(what);
    }

    void rewrite() {
        for(const Element & e : elements_) {
            if(!chain(e) || erased(e) || !removed_.contains(e.path)) continue;
            for(const NameUse & u : uses(e.name)) {
                if(dead(u.path)) continue;
                const Scanned s = scan(u.path, e);
                if(s.text != text_of(at(work_, u.path))) set(u.path, s.text);
            }
        }
    }

    Json work_;
    std::vector<Element> elements_;
    std::map<std::string, std::size_t> index_;  // by path
    std::map<std::string, std::vector<NameUse>> uses_;
    std::vector<std::string> touched_, erased_, names_, queue_, candidates_;
    std::map<std::string, std::set<int>> removed_;  // by chain path
    std::set<std::string> kept_;
    std::map<std::string, std::map<int, int>> alias_;  // by chain path
    std::vector<std::size_t> gone_constraints_;
    std::vector<std::string> taken_, orphans_, vertices_, removed_names_,
        check_, broken_;
};

// "delete vertex 2 of wall: deleted h3, d4"
std::string report_of(const std::string & label,
                      const std::vector<std::string> & removed,
                      const std::vector<std::string> & check) {
    if(removed.empty() && check.empty()) return {};
    std::string out = label;
    if(!removed.empty()) out += ": deleted " + joined(removed);
    if(!check.empty())
        out += std::format("{} check {}", removed.empty() ? ":" : ";",
                           joined(check));
    return out;
}

// `refs` in the sketch model at the drawing (Document::sketch_q()).
std::vector<GeoValue> drawn(const Document & doc,
                            std::span<const ExprRef> refs) {
    Evaluator ev(*doc.sketch_model());
    ev.set_q(doc.sketch_q());
    return ev.values(doc.sketch_x(), {}, refs);
}

int model_element(const Model & m, std::string_view name) {
    const auto it = std::ranges::find(m.sketch(), name, &SketchInfo::name);
    return it == m.sketch().end() ? -1
                                  : static_cast<int>(it - m.sketch().begin());
}

int model_body(const Model & m, std::string_view name) {
    const auto it =
        std::ranges::find(m.sketch_bodies(), name, &SketchBody::name);
    return it == m.sketch_bodies().end()
               ? -1
               : static_cast<int>(it - m.sketch_bodies().begin());
}

bool has_pair(const Json * j) {
    return j && j->is_array() && j->size() == 2 && (*j)[0].is_number() &&
           (*j)[1].is_number();
}

// The selected elements of the document, whole (no `sub`).
std::vector<Element> selected_elements(const Instance & inst,
                                       std::span<const SelKey> keys) {
    std::vector<Element> out;
    for(const SelKey & k : keys) {
        if(k.kind != Kind::Element || !k.sub.empty()) continue;
        const Json * e = inst.get(element_path(k.name));
        if(!e || !e->is_object()) continue;
        out.push_back({k.name, element_path(k.name), body_of(k.name),
                       text_of(member(*e, "type"))});
    }
    return out;
}

// --- Merge: what reads a point twice once two points are one.

// The functions whose first two arguments are two points or two curves: the
// same one twice relates it to itself.
constexpr std::string_view kPairFunctions[] = {
    "dist",      "coincident", "horizontal",    "vertical",  "segment",
    "symmetric", "parallel",   "perpendicular", "angle",     "line_dist",
    "on_line",   "on_segment", "tangent",       "tangent_at"};

// Whether `after`, the merged formula, writes alike two arguments that
// `before` wrote apart: dist(A, A), A - A, segment(A, A), or the two sides
// of a relation, O.y == O.y (each pair of a chain lo <= e <= hi). The merge
// puts a name where a name was, so both trees have one shape.
bool collapses(const Ast & before, const Ast & after) {
    if(before.kind != after.kind || before.args.size() != after.args.size())
        return false;
    auto alike = [&](std::size_t i, std::size_t j) {
        return to_string(*after.args[i]) == to_string(*after.args[j]) &&
               to_string(*before.args[i]) != to_string(*before.args[j]);
    };
    const bool pair = after.args.size() >= 2 &&
                      ((after.kind == Ast::Kind::Call &&
                        std::ranges::contains(kPairFunctions, after.text)) ||
                       (after.kind == Ast::Kind::Binary && after.text == "-"));
    if(pair && alike(0, 1)) return true;
    if(after.kind == Ast::Kind::Relation)
        for(std::size_t i = 0; i + 1 < after.args.size(); ++i)
            if(alike(i, i + 1)) return true;
    for(std::size_t i = 0; i < after.args.size(); ++i)
        if(collapses(*before.args[i], *after.args[i])) return true;
    return false;
}

// The slot text of element `name` that `key` names in `doc`, read the same
// however it is spaced.
std::optional<std::string> slot_text(const Json & doc, const std::string & name,
                                     const std::string & key, int i = -1) {
    const Json * e = at(doc, element_path(name));
    if(!e) return std::nullopt;
    const Json * s = member(*e, key);
    if(s && i >= 0) {
        if(!s->is_array() || s->empty()) return std::nullopt;
        s = &(*s)[uz(
            ((i % static_cast<int>(s->size())) + static_cast<int>(s->size())) %
            static_cast<int>(s->size()))];
    }
    if(!s || !s->is_string()) return std::nullopt;
    return canonical(s->get<std::string>());
}

int points_count(const Json & doc, const std::string & name) {
    const Json * p = at(doc, element_path(name) + ".points");
    return p && p->is_array() ? static_cast<int>(p->size()) : 0;
}

// A point operand as the point it is in `doc`: a point element's name, the
// slot a part reads, or the part itself for a derived point.
std::optional<std::string> point_of(const Json & doc, const KindNames & names,
                                    const KindOperand & op) {
    if(op.element.empty()) return std::nullopt;
    const std::optional<SketchType> t = names.element(op.element);
    if(!t) return std::nullopt;
    const std::string & e = op.element;
    auto derived = [&](std::string_view part) {
        return std::format("{}({})", part, e);
    };
    auto box_vertex = [&](int i) -> std::optional<std::string> {
        if(i == 0) return slot_text(doc, e, "corners", 0);
        if(i == 2) return slot_text(doc, e, "corners", 1);
        return std::format("vertex({}, {})", e, i);
    };
    switch(op.part) {
        case ElementPart::Whole:
            if(*t == SketchType::Point) return e;
            return std::nullopt;
        case ElementPart::Start:
        case ElementPart::End: {
            const bool start = op.part == ElementPart::Start;
            if(*t == SketchType::Segment)
                return slot_text(doc, e, start ? "from" : "to");
            if(*t == SketchType::Polyline)
                return slot_text(doc, e, "points",
                                 start ? 0 : points_count(doc, e) - 1);
            return derived(start ? "start" : "end");
        }
        case ElementPart::Vertex:
            if(*t == SketchType::Segment)
                return slot_text(doc, e, op.index == 0 ? "from" : "to");
            if(*t == SketchType::Box) return box_vertex(op.index);
            return slot_text(doc, e, "points", op.index);
        case ElementPart::Center:
            return slot_text(doc, e, "center");
        case ElementPart::Mid:
            return derived("mid");
        case ElementPart::Edge:
            return std::nullopt;
    }
    return std::nullopt;
}

// A segment operand as its two ends in `doc`.
std::optional<std::pair<std::string, std::string>> segment_of(
    const Json & doc, const KindNames & names, const KindOperand & op) {
    if(op.element.empty()) return std::nullopt;
    const std::optional<SketchType> t = names.element(op.element);
    if(!t) return std::nullopt;
    const std::string & e = op.element;
    if(op.part == ElementPart::Whole && *t == SketchType::Segment) {
        auto a = slot_text(doc, e, "from"), b = slot_text(doc, e, "to");
        if(a && b) return std::pair{*a, *b};
        return std::nullopt;
    }
    if(op.part != ElementPart::Edge) return std::nullopt;
    if(*t == SketchType::Box) {
        auto v = [&](int i) -> std::optional<std::string> {
            if(i % 4 == 0) return slot_text(doc, e, "corners", 0);
            if(i % 4 == 2) return slot_text(doc, e, "corners", 1);
            return std::format("vertex({}, {})", e, i % 4);
        };
        auto a = v(op.index), b = v(op.index + 1);
        if(a && b) return std::pair{*a, *b};
        return std::nullopt;
    }
    auto a = slot_text(doc, e, "points", op.index);
    auto b = slot_text(doc, e, "points", op.index + 1);
    if(a && b) return std::pair{*a, *b};
    return std::nullopt;
}

// Whether a constraint of a recognised kind reads a point twice in `doc`:
// two of its points are one, a segment of it has one point at both ends, a
// point lies at an end of the line it is measured from, or its two
// segments are one.
bool degenerate(const Json & doc, const KindNames & names,
                const KindMatch & m) {
    std::vector<std::optional<std::string>> points;
    std::vector<std::optional<std::pair<std::string, std::string>>> segments;
    for(const KindOperand & op : m.operands) {
        points.push_back(point_of(doc, names, op));
        segments.push_back(segment_of(doc, names, op));
        if(segments.back() && segments.back()->first == segments.back()->second)
            return true;
    }
    auto same_points = [&](std::size_t a, std::size_t b) {
        return a < points.size() && b < points.size() && points[a] &&
               points[b] && *points[a] == *points[b];
    };
    auto same_segments = [&](std::size_t a, std::size_t b) {
        if(a >= segments.size() || b >= segments.size() || !segments[a] ||
           !segments[b])
            return false;
        const auto & [p, q] = *segments[a];
        const auto & [r, s] = *segments[b];
        return (p == r && q == s) || (p == s && q == r);
    };
    switch(m.kind) {
        case K::Distance:
        case K::Coincident:
        case K::DistanceX:
        case K::DistanceY:
        case K::OrderX:
        case K::OrderY:
        case K::Horizontal:
        case K::Vertical:
        case K::Symmetric:
            return same_points(0, 1);
        case K::LineDistance:
        case K::OnLine:
        case K::OnSegment:
            return points.size() >= 2 && segments.size() >= 2 && points[0] &&
                   segments[1] &&
                   (*points[0] == segments[1]->first ||
                    *points[0] == segments[1]->second);
        case K::Parallel:
        case K::Perpendicular:
        case K::Angle:
        case K::Equal:
            return same_segments(0, 1);
        default:
            return false;
    }
}

// The fixed value of point `name` in its frame, as drawn.
std::optional<Vec2d> point_value(const Document & doc,
                                 const std::string & name) {
    const Model * m = doc.sketch_model().get();
    const int i = m ? model_element(*m, name) : -1;
    if(i < 0) return std::nullopt;
    const SketchInfo & si = m->sketch()[uz(i)];
    return drawn(doc, {&si.local, 1})[0].vec();
}

// tangent_at(X, Y, P, s) with s negated reverses one curve's direction at
// P: no solve turns a drawing that far, since the wrapped row starts on its
// cut, 180 degrees off (sketch-arc's smooth stops short at 1.26 m). A
// segment among X and Y whose other end is a free point of its frame gets
// that point mirrored through P, onto the flipped tangent.
void mirror_far_end(const Document & doc, const std::string & text,
                    const KindMatch & k, EditBatch & batch) {
    const Model * m = doc.sketch_model().get();
    if(!m || doc.sketch_stale() || k.operands.size() < 3) return;
    const Instance & inst = *doc.instance();
    const TextSpan p = k.operands[2].span;
    const std::string joint = canonical(
        std::string_view(text).substr(uz(p.begin), uz(p.end - p.begin)));
    const std::vector<Element> all = sketch_elements(inst.doc());
    for(const int i : {1, 0}) {
        const KindOperand & op = k.operands[uz(i)];
        if(op.part != ElementPart::Whole) continue;
        const auto e = std::ranges::find(all, op.element, &Element::name);
        if(e == all.end() || e->type != "segment") continue;
        const std::vector<Slot> slots = slots_of(inst.doc(), *e);
        if(slots.size() != 2) continue;
        const int at = canonical(slots[0].text) == joint   ? 0
                       : canonical(slots[1].text) == joint ? 1
                                                           : -1;
        if(at < 0) continue;
        const Slot & far = slots[uz(1 - at)];
        const std::optional<std::string> f = shared_point(far.text);
        if(!f || body_of(*f) != e->body) continue;
        const Json * fp = inst.get(element_path(*f));
        if(!fp || !fp->is_object() || text_of(member(*fp, "type")) != "point" ||
           flag(*fp, "fixed"))
            continue;
        const int s = model_element(*m, e->name);
        if(s < 0) continue;
        const SketchInfo & si = m->sketch()[uz(s)];
        auto local = [&](const Slot & slot) -> std::optional<Vec2d> {
            const auto it =
                std::ranges::find(si.slots, slot.path, &SketchSlot::path);
            if(it == si.slots.end()) return std::nullopt;
            return drawn(doc, {&it->local, 1})[0].vec();
        };
        const std::optional<Vec2d> j = local(slots[uz(at)]), q = local(far);
        if(!j || !q) continue;
        batch.set(element_path(*f) + ".value",
                  Json::array({2.0 * j->x - q->x, 2.0 * j->y - q->y}));
        return;
    }
}

}  // namespace

std::optional<std::string> slot_path(const Instance & instance,
                                     const SelKey & key) {
    if(key.kind != Kind::Element || key.sub.empty()) return std::nullopt;
    const std::string base = element_path(key.name);
    std::string path;
    if(key.sub == "from" || key.sub == "to" || key.sub == "center") {
        path = std::format("{}.{}", base, key.sub);
    } else {
        for(const std::string_view list : {"points", "corners"}) {
            if(!key.sub.starts_with(list) ||
               key.sub.size() <= list.size() + 1 || key.sub[list.size()] != ' ')
                continue;
            int i = -1;
            const char * first = key.sub.data() + list.size() + 1;
            const char * last = key.sub.data() + key.sub.size();
            if(std::from_chars(first, last, i).ptr != last || i < 0)
                return std::nullopt;
            path = std::format("{}.{}[{}]", base, list, i);
        }
    }
    const Json * j = path.empty() ? nullptr : instance.get(path);
    if(!j || !j->is_string()) return std::nullopt;
    return path;
}

std::string unused_point_name(const Instance & instance, std::string_view body,
                              std::span<const std::string> taken,
                              std::string_view prefix) {
    for(std::size_t k = 1;; ++k) {
        const std::string name = std::format("{}{}", prefix, k);
        if(body.empty()) {
            if(!name_used(instance, name) &&
               !std::ranges::contains(taken, name))
                return name;
            continue;
        }
        const std::string qualified = std::format("{}.{}", body, name);
        if(!instance.get(element_path(qualified)) &&
           !std::ranges::contains(taken, qualified))
            return qualified;
    }
}

// What a merge takes with it once `r` has substituted `survivor` for the
// point `gone` everywhere: `gone` is erased, the segments and boxes whose
// two ends became one, the second of two vertices of a chain that became
// one, and the constraints of a recognised kind that read a point twice;
// a Formula-kind one that would refuses the merge. `before` writes what
// the survivor takes from `gone`, read before the cascade.
static SketchEdit merged(const Instance & inst, const Element & gone,
                         std::string_view survivor, RenameResult r,
                         const std::function<void(Cascade &)> & take) {
    SketchEdit out;
    const Element & b = gone;
    const std::string a_name(survivor);
    const std::vector<std::string> rewritten = r.rewritten;
    // The merged document as substitute_entry() wrote it, every array as it
    // was: the cascade below renumbers indices without compacting its
    // copy's arrays, so a point read there by index would be another.
    const Json after = r.doc;
    Cascade c(std::move(r.doc));
    for(const std::string & path : rewritten) c.touch(path);
    take(c);
    c.keep(a_name);
    c.erase_element(*c.find(b.name), "");
    // Elements that read the merged point at both of their ends.
    for(const Element & e : c.elements()) {
        if(c.erased(e)) continue;
        const std::vector<Slot> slots = slots_of(c.work(), e);
        if(e.type == "segment" || e.type == "box") {
            if(slots.size() == 2 &&
               canonical(slots[0].text) == canonical(slots[1].text))
                c.erase_element(e, std::format("both its ends are {}", a_name));
        } else if(chain(e) && !slots.empty()) {
            const std::string first = canonical(slots[0].text);
            std::string last = first;
            std::size_t kept = 0;
            for(std::size_t j = 1; j < slots.size(); ++j) {
                const std::string now = canonical(slots[j].text);
                if(now == last) {
                    c.remove_vertex(e, static_cast<int>(j),
                                    static_cast<int>(kept));
                } else {
                    last = now;
                    kept = j;
                }
            }
            if(e.type == "polygon" && kept > 0 && last == first)
                c.remove_vertex(e, static_cast<int>(kept), 0);
        }
    }
    c.close();

    // Constraints that would relate a point to itself.
    const Json & before = inst.doc();
    const KindNames names_before(before), names_after(after);
    std::vector<std::string> refused = c.broken();
    for(const std::string & f : c.check())
        if(!std::ranges::contains(refused, f)) refused.push_back(f);
    if(const Json * list = at(c.work(), "constraints");
       list && list->is_array())
        for(std::size_t i = 0; i < list->size(); ++i) {
            const std::string expr = std::format("constraints[{}].expr", i);
            if(c.dead(expr)) continue;
            const ParseResult pb = parse_expression(text_of(at(before, expr)));
            const ParseResult pa = parse_expression(text_of(at(after, expr)));
            if(!pb.ok() || !pa.ok()) continue;
            const KindMatch mb = match_kind(*pb.ast, names_before);
            const KindMatch ma = match_kind(*pa.ast, names_after);
            const bool now =
                collapses(*pb.ast, *pa.ast) ||
                (ma.kind != K::Formula && degenerate(after, names_after, ma) &&
                 !degenerate(before, names_before, mb));
            if(!now) continue;
            if(ma.kind != K::Formula)
                c.erase_constraint(i);
            else
                refused.push_back(constraint_label(c.work(), i));
        }
    if(!refused.empty()) {
        out.refusal = std::format(
            "cannot merge {} into {}: {} {} would read one point twice, "
            "{}",
            b.name, a_name,
            refused.size() == 1 ? "the formula" : "the formulas",
            joined(refused),
            refused.size() == 1 ? "edit it first" : "edit them first");
        return out;
    }
    const std::string label = std::format("merge {} into {}", b.name, a_name);
    out.batch = c.batch(label);
    out.removed = c.removed();
    out.merged = {SelKey(Kind::Element, b.name), SelKey(Kind::Element, a_name)};
    std::vector<std::string> deleted = c.taken();
    deleted.insert(deleted.end(), out.removed.begin(), out.removed.end());
    out.report = report_of(label, deleted, {});
    return out;
}

SketchEdit merge_points(const Document & doc, std::span<const SelKey> keys) {
    SketchEdit out;
    if(!doc.has_instance()) return out;
    const Instance & inst = *doc.instance();
    const std::vector<Element> picked = selected_elements(inst, keys);
    if(keys.size() != 2 || picked.size() != 2 || picked[0].type != "point" ||
       picked[1].type != "point" || picked[0].name == picked[1].name) {
        out.refusal = "merge: select two points";
        return out;
    }
    const Element & a = picked[0];
    const Element & b = picked[1];
    if(a.body != b.body) {
        out.refusal = std::format(
            "cannot merge {} and {}: they are in different frames; pin them "
            "with coincident({}, {})",
            a.name, b.name, a.name, b.name);
        return out;
    }
    const Json & ja = *inst.get(a.path);
    const Json & jb = *inst.get(b.path);
    const bool fa = flag(ja, "fixed"), fb = flag(jb, "fixed");
    if(fa && fb && member(ja, "value") && member(jb, "value") &&
       *member(ja, "value") != *member(jb, "value")) {
        const std::optional<Vec2d> va = point_value(doc, a.name);
        const std::optional<Vec2d> vb = point_value(doc, b.name);
        const double scale =
            va ? std::max({1.0, std::fabs(va->x), std::fabs(va->y)}) : 1.0;
        if(!va || !vb ||
           std::hypot(va->x - vb->x, va->y - vb->y) > 1e-12 * scale) {
            out.refusal = std::format(
                "cannot merge {} and {}: both are fixed, at different places",
                a.name, b.name);
            return out;
        }
    }
    RenameResult r = substitute_entry(inst, b.path, a.name);
    if(!r.ok()) {
        out.refusal =
            std::format("cannot merge {} into {}: {}", b.name, a.name, r.error);
        return out;
    }
    return merged(inst, b, a.name, std::move(r), [&](Cascade & c) {
        if(fb && !fa) {
            c.set(a.path + ".value", *member(jb, "value"));
            c.set(a.path + ".fixed", true);
        }
    });
}

bool drawn_from(const Instance & inst, std::string_view point,
                std::string_view derived) {
    // The elements `derived` reads, through their slots and values, the
    // elements those read, and so on: P among them would read itself once
    // substituted, end(door) for door's centre.
    const ParseResult parsed = parse_expression(derived);
    if(!parsed.ok()) return false;
    const std::vector<Element> all = sketch_elements(inst.doc());
    std::vector<std::string> todo, seen;
    std::function<void(const Ast &)> names = [&](const Ast & a) {
        if(a.kind == Ast::Kind::Name) todo.push_back(a.text);
        for(const auto & arg : a.args) names(*arg);
    };
    names(*parsed.ast);
    while(!todo.empty()) {
        const std::string n = todo.back();
        todo.pop_back();
        if(std::ranges::contains(seen, n)) continue;
        seen.push_back(n);
        if(n == point) return true;
        const auto e = std::ranges::find(all, n, &Element::name);
        if(e == all.end()) continue;
        // A fixed value or radius may be an expression too.
        for(const auto & [key, value] : inst.get(e->path)->items()) {
            if(!value.is_string() || key == "type" || key == "note") continue;
            if(const ParseResult q = parse_expression(value.get<std::string>());
               q.ok())
                names(*q.ast);
        }
        for(const Slot & slot : slots_of(inst.doc(), *e))
            if(const ParseResult q = parse_expression(slot.text); q.ok())
                names(*q.ast);
    }
    return false;
}

std::string Coincidence::formula() const {
    return std::format("coincident({}, {})", first, second);
}

Coincidence coincidence(const Instance & inst, const Model & m,
                        std::string_view a, std::string_view frame_a,
                        std::string_view b, std::string_view frame_b) {
    auto element = [&](std::string_view name) -> const SketchInfo * {
        const auto e = std::ranges::find(m.sketch(), name, &SketchInfo::name);
        return e != m.sketch().end() && e->type == SketchType::Point ? &*e
                                                                     : nullptr;
    };
    const SketchInfo * pa = element(a);
    const SketchInfo * pb = element(b);
    Coincidence out{.first = std::string(a), .second = std::string(b)};
    if((pa ? std::string_view(pa->frame) : frame_a) !=
       (pb ? std::string_view(pb->frame) : frame_b)) {
        out.how = Coincidence::How::Pin;
        return out;
    }
    if(pa && pb) {
        out.how = Coincidence::How::Merge;
        return out;
    }
    const SketchInfo * p = pa ? pa : pb;
    if(!p || p->fixed) return out;
    out.how = Coincidence::How::MergeInto;
    out.first = p->name;
    out.second = std::string(pa ? b : a);
    if(drawn_from(inst, out.first, out.second))
        out.refusal = std::format(
            "cannot merge {0} into {1}: {1} is drawn "
            "from {0}",
            out.first, out.second);
    return out;
}

SketchEdit merge_into(const Instance & inst, const SelKey & point,
                      std::string_view derived) {
    SketchEdit out;
    const std::vector<Element> picked = selected_elements(inst, {&point, 1});
    if(picked.size() != 1 || picked[0].type != "point") {
        out.refusal = "merge: select a point";
        return out;
    }
    const Element & p = picked[0];
    const std::string into = canonical(derived);
    if(flag(*inst.get(p.path), "fixed")) {
        out.refusal = std::format(
            "cannot merge {} into {}: {} is fixed, which a derived point "
            "cannot be",
            p.name, into, p.name);
        return out;
    }
    const ParseResult parsed = parse_expression(into);
    if(!parsed.ok()) {
        out.refusal = std::format("cannot merge {} into {}: {}", p.name, into,
                                  parsed.error);
        return out;
    }
    if(drawn_from(inst, p.name, into)) {
        out.refusal =
            std::format("cannot merge {} into {}: {} is drawn from {}", p.name,
                        into, into, p.name);
        return out;
    }
    RenameResult r = substitute_entry(inst, p.path, into);
    if(!r.ok()) {
        out.refusal =
            std::format("cannot merge {} into {}: {}", p.name, into, r.error);
        return out;
    }
    SketchEdit edit = merged(inst, p, into, std::move(r), [](Cascade &) {});
    // The survivor is no entry: its element is.
    if(edit.merged)
        edit.merged->second =
            SelKey(Kind::Element, parsed.ast->kind == Ast::Kind::Call &&
                                          !parsed.ast->args.empty()
                                      ? parsed.ast->args[0]->text
                                      : into);
    return edit;
}

namespace {

bool enabled(const Json & constraint) {
    const Json * e = member(constraint, "enabled");
    return !(e && *e == false);
}

// Calls `f(expr, match)` for each enabled constraint of `doc` that parses.
template <class F>
void each_kind(const Json & doc, F && f) {
    const Json * list = at(doc, "constraints");
    if(!list || !list->is_array()) return;
    const KindNames names(doc);
    for(std::size_t i = 0; i < list->size(); ++i) {
        const Json & c = (*list)[i];
        if(!c.is_object() || !enabled(c)) continue;
        const std::string expr = text_of(member(c, "expr"));
        const ParseResult parsed = parse_expression(expr);
        if(!parsed.ok()) continue;
        f(c, expr, names, match_kind(*parsed.ast, names));
    }
}

std::string operand_text(std::string_view expr, const KindOperand & op) {
    return canonical(
        expr.substr(uz(op.span.begin), uz(op.span.end - op.span.begin)));
}

}  // namespace

bool lies_on(const Instance & instance, std::string_view point,
             std::string_view curve, std::span<const std::string> ends) {
    const std::string p = canonical(point), k = canonical(curve);
    std::vector<std::string> on_curve;
    for(const std::string & e : ends) on_curve.push_back(canonical(e));
    if(std::ranges::contains(on_curve, p)) return true;
    bool found = false;
    const Json & doc = instance.doc();
    each_kind(doc, [&](const Json &, const std::string & expr,
                       const KindNames & names, const KindMatch & m) {
        if(found || m.operands.size() < 2) return;
        const std::optional<std::string> a =
            point_of(doc, names, m.operands[0]);
        if(m.kind == K::On || m.kind == K::OnLine || m.kind == K::OnSegment) {
            found = a == p && operand_text(expr, m.operands[1]) == k;
            return;
        }
        if(m.kind != K::Coincident) return;
        const std::optional<std::string> b =
            point_of(doc, names, m.operands[1]);
        found = a && b &&
                ((*a == p && std::ranges::contains(on_curve, *b)) ||
                 (*b == p && std::ranges::contains(on_curve, *a)));
    });
    return found;
}

std::vector<DirectionConstraint> direction_constraints(
    const Instance & instance, std::string_view a, std::string_view b) {
    const std::string p = canonical(a), q = canonical(b);
    std::vector<DirectionConstraint> out;
    const Json & doc = instance.doc();
    each_kind(doc, [&](const Json & c, const std::string &,
                       const KindNames & names, const KindMatch & m) {
        if(m.kind != K::Horizontal && m.kind != K::Vertical) return;
        std::optional<std::pair<std::string, std::string>> ends;
        if(m.operands.size() == 1) {
            ends = segment_of(doc, names, m.operands[0]);
        } else if(m.operands.size() == 2) {
            const auto u = point_of(doc, names, m.operands[0]);
            const auto v = point_of(doc, names, m.operands[1]);
            if(u && v) ends = std::pair{*u, *v};
        }
        if(ends && ((ends->first == p && ends->second == q) ||
                    (ends->first == q && ends->second == p)))
            out.push_back({text_of(member(c, "name")), m.kind == K::Vertical});
    });
    return out;
}

SketchEdit detach(const Document & doc, std::span<const SelKey> keys) {
    SketchEdit out;
    if(!doc.has_instance()) return out;
    const Instance & inst = *doc.instance();
    const Model * m = doc.sketch_model().get();
    if(!m || doc.sketch_stale()) {
        out.refusal = "detach: the sketch does not compile";
        return out;
    }
    const std::vector<Element> all = sketch_elements(inst.doc());
    const std::vector<Element> picked = selected_elements(inst, keys);
    std::vector<std::string> taken, done;
    std::set<std::string> filled;  // slot paths given a fresh point
    // Fills the slots of `e` at `paths` with one fresh point at the first
    // one's position, in `e`'s frame.
    auto fresh = [&](const Element & e,
                     const std::vector<std::string> & paths) {
        const int i = model_element(*m, e.name);
        if(i < 0 || paths.empty()) return false;
        const SketchInfo & si = m->sketch()[uz(i)];
        const auto slot =
            std::ranges::find(si.slots, paths.front(), &SketchSlot::path);
        if(slot == si.slots.end()) return false;
        const Vec2d v = drawn(doc, {&slot->local, 1})[0].vec();
        const std::string name = unused_point_name(inst, e.body, taken);
        taken.push_back(name);
        out.batch.set(element_path(name),
                      Json{{"type", "point"}, {"value", {v.x, v.y}}});
        for(const std::string & p : paths) {
            out.batch.set(p, name);
            filled.insert(p);
        }
        out.created.emplace_back(Kind::Element, name);
        return true;
    };
    // The elements that hold point `p` in a slot, in document order, with
    // those slots.
    auto holders = [&](const std::string & p) {
        std::vector<std::pair<Element, std::vector<std::string>>> found;
        for(const Element & e : all) {
            std::vector<std::string> paths;
            for(const Slot & s : slots_of(inst.doc(), e))
                if(shared_point(s.text) == p) paths.push_back(s.path);
            if(!paths.empty()) found.emplace_back(e, std::move(paths));
        }
        return found;
    };
    for(const SelKey & k : keys) {
        if(k.kind != Kind::Element || k.sub.empty()) continue;
        const std::optional<std::string> path = slot_path(inst, k);
        if(!path || filled.contains(*path)) continue;
        const auto e =
            std::ranges::find(all, element_path(k.name), &Element::path);
        if(e == all.end()) continue;
        const std::string text = text_of(inst.get(*path));
        // A point this slot alone uses: a fresh point would only rename it.
        if(const std::optional<std::string> p = shared_point(text);
           p && body_of(*p) == e->body &&
           std::ranges::all_of(
               find_name_uses(inst.doc(), *p).uses,
               [&](const NameUse & u) { return u.path == *path; })) {
            out.refusal = std::format(
                "cannot detach {}.{}: it holds {}, which nothing else uses",
                k.name, k.sub, *p);
            return out;
        }
        if(fresh(*e, {*path}))
            done.push_back(std::format("{}.{}", k.name, k.sub));
    }
    std::vector<const Element *> others;
    for(const Element & e : picked)
        if(e.type != "point") others.push_back(&e);
    for(const Element & p : picked) {
        if(p.type != "point") continue;
        auto found = holders(p.name);
        std::vector<std::pair<Element, std::vector<std::string>>> chosen;
        if(!others.empty()) {
            for(auto & h : found)
                if(std::ranges::any_of(others, [&](const Element * o) {
                       return o->name == h.first.name;
                   }))
                    chosen.push_back(std::move(h));
            if(chosen.empty()) {
                out.refusal = std::format(
                    "cannot detach {}: no selected element holds it", p.name);
                return out;
            }
        } else {
            if(found.size() < 2) {
                out.refusal = std::format(
                    "cannot detach {}: {} holds it, nothing else to detach "
                    "from",
                    p.name,
                    found.empty() ? std::string("no element")
                                  : "only " + found[0].first.name);
                return out;
            }
            chosen.assign(std::make_move_iterator(found.begin() + 1),
                          std::make_move_iterator(found.end()));
        }
        for(auto & [e, paths] : chosen) {
            std::erase_if(paths, [&](const std::string & s) {
                return filled.contains(s);
            });
            if(fresh(e, paths)) done.push_back(p.name);
        }
    }
    if(out.batch.ops.empty()) {
        if(out.refusal.empty())
            out.refusal = "detach: select a point, or a slot of an element";
        return out;
    }
    std::ranges::sort(done);
    const auto [first, last] = std::ranges::unique(done);
    done.erase(first, last);
    out.batch.label = done.size() == 1
                          ? "detach " + done.front()
                          : std::format("detach {} points", done.size());
    return out;
}

SketchEdit fix_sketch(const Document & doc, std::span<const SelKey> keys,
                      bool fixed) {
    SketchEdit out;
    if(!doc.has_instance()) return out;
    const Instance & inst = *doc.instance();
    const Model * m = doc.sketch_model().get();
    std::vector<std::string> done;
    for(const SelKey & k : keys) {
        if((k.kind != Kind::Element && k.kind != Kind::Body) || !k.sub.empty())
            continue;
        const std::string path = k.kind == Kind::Body
                                     ? "sketch.bodies." + k.name
                                     : element_path(k.name);
        const Json * e = inst.get(path);
        if(!e || !e->is_object()) continue;
        const std::string type =
            k.kind == Kind::Body ? "body" : text_of(member(*e, "type"));
        if(type != "point" && type != "circle" && type != "arc" &&
           type != "body")
            continue;
        if(flag(*e, "fixed") == fixed) continue;
        done.push_back(k.name);
        const std::string at_fixed = path + ".fixed";
        if(fixed) {
            out.batch.set(at_fixed, true);
            continue;
        }
        out.batch.erase(at_fixed);
        // A free value must be a number: an expression freed is written as
        // the number it has now.
        if(type == "body") {
            const int b = m ? model_body(*m, k.name) : -1;
            const Json * origin = inst.get(path + ".pose.origin.value");
            const Json * angle = inst.get(path + ".pose.angle.value");
            if(b < 0 || (has_pair(origin) && angle && angle->is_number()))
                continue;
            const SketchBody & body = m->sketch_bodies()[uz(b)];
            const std::array refs{body.origin, body.angle};
            const std::vector<GeoValue> v = drawn(doc, refs);
            if(!has_pair(origin))
                out.batch.set(path + ".pose.origin.value",
                              Json::array({v[0].vec().x, v[0].vec().y}));
            if(!angle || !angle->is_number())
                out.batch.set(path + ".pose.angle.value", v[1].scalar());
            continue;
        }
        const int i = m ? model_element(*m, k.name) : -1;
        if(i < 0) continue;
        const SketchInfo & si = m->sketch()[uz(i)];
        if(type == "point" && !has_pair(member(*e, "value"))) {
            const std::vector<GeoValue> v = drawn(doc, {&si.local, 1});
            out.batch.set(path + ".value",
                          Json::array({v[0].vec().x, v[0].vec().y}));
        }
        std::vector<std::pair<std::string, ExprRef>> parts;
        if(type == "circle" || type == "arc")
            parts.push_back(
                {"radius", type == "arc" ? si.arc.radius : ExprRef{}});
        if(type == "arc") {
            parts.push_back({"start", si.arc.start_angle});
            parts.push_back({"end", si.arc.end_angle});
        }
        for(const auto & [part, ref] : parts) {
            const Json * now = member(*e, part);
            if(now && now->is_number()) continue;
            double v = ref.node >= 0
                           ? drawn(doc, {&ref, 1})[0].scalar()
                           : drawn(doc, {&si.local, 1})[0].circle_radius();
            // An arc's angles are in its frame: its world angle less its
            // body's.
            if(part != "radius" && !si.frame.empty())
                if(const int b = model_body(*m, si.frame); b >= 0)
                    v -= drawn(doc, {&m->sketch_bodies()[uz(b)].angle, 1})[0]
                             .scalar();
            out.batch.set(std::format("{}.{}", path, part), v);
        }
    }
    if(done.size() == 1)
        out.batch.label =
            std::format("{} {}", fixed ? "fix" : "free", done.front());
    else if(!done.empty())
        out.batch.label = std::format("{} {} sketch entries",
                                      fixed ? "fix" : "free", done.size());
    return out;
}

SketchEdit set_driving(const Document & doc, std::span<const SelKey> keys,
                       bool driving) {
    SketchEdit out;
    if(!doc.has_instance() || !doc.has_sketch()) return out;
    const Instance & inst = *doc.instance();
    std::vector<std::string> done;
    for(const SelKey & k : keys) {
        if(k.kind != Kind::Constraint) continue;
        for(const std::string & base : entry_paths(inst, k)) {
            const Json * d = inst.get(base + ".driving");
            const bool now = !(d && *d == false);
            if(now == driving ||
               (!driving && !dimension_constraint(inst, base)))
                continue;
            if(driving)
                out.batch.erase(base + ".driving");
            else
                out.batch.set(base + ".driving", false);
            done.push_back(key_name(k));
        }
    }
    if(done.size() == 1)
        out.batch.label = std::format("make {} {}", done.front(),
                                      driving ? "driving" : "a reference");
    else if(!done.empty())
        out.batch.label =
            std::format("make {} constraints {}", done.size(),
                        driving ? "driving" : "reference dimensions");
    return out;
}

bool dimension_constraint(const Instance & instance, std::string_view path) {
    const Json * expr = instance.get(std::format("{}.expr", path));
    if(!expr || !expr->is_string()) return false;
    const KindMatch m =
        match_kind(expr->get<std::string>(), KindNames(instance.doc()));
    return m.kind != K::Formula && m.relation == KindRelation::Equal &&
           !m.constants.empty();
}

bool flippable(const Instance & instance, std::string_view path) {
    const Json * expr = instance.get(std::format("{}.expr", path));
    if(!expr || !expr->is_string()) return false;
    const KindMatch m =
        match_kind(expr->get<std::string>(), KindNames(instance.doc()));
    return m.kind == K::LineDistance ||
           ((m.kind == K::Tangent || m.kind == K::TangentAt ||
             m.kind == K::Parallel || m.kind == K::Perpendicular) &&
            m.side);
}

SketchEdit flip_side(const Document & doc, std::span<const SelKey> keys) {
    SketchEdit out;
    if(!doc.has_instance()) return out;
    const Instance & inst = *doc.instance();
    const KindNames names(inst.doc());
    std::vector<std::string> done;
    for(const SelKey & k : keys) {
        if(k.kind != Kind::Constraint) continue;
        for(const std::string & base : entry_paths(inst, k)) {
            if(!flippable(inst, base)) continue;
            const std::string path = base + ".expr";
            std::string text = inst.get(path)->get<std::string>();
            const KindMatch m = match_kind(text, names);
            // A disabled or reference tangency is not enforced: the drawing
            // stays where it is.
            if(const Json * c = inst.get(base);
               m.kind == K::TangentAt && c && c->is_object() &&
               !(c->contains("enabled") && (*c)["enabled"] == false) &&
               !(c->contains("driving") && (*c)["driving"] == false))
                mirror_far_end(doc, text, m, out.batch);
            if(!m.side) {
                // line_dist(P, S) == c reads side +1.
                text.insert(uz(m.operands.at(1).span.end), ", -1");
            } else {
                const TextSpan s = *m.side;
                const std::string side =
                    text.substr(uz(s.begin), uz(s.end - s.begin));
                std::string flipped;
                if(m.sign == 1) {
                    flipped = "-1";
                } else if(m.sign == -1) {
                    flipped = "1";
                } else {
                    const ParseResult p = parse_expression(side);
                    if(p.ok() && p.ast->kind == Ast::Kind::Neg) {
                        const Ast & inner = *p.ast->args[0];
                        flipped = side.substr(uz(inner.begin),
                                              uz(inner.end - inner.begin));
                    } else if(p.ok() && (p.ast->kind == Ast::Kind::Name ||
                                         p.ast->kind == Ast::Kind::Number ||
                                         p.ast->kind == Ast::Kind::Call)) {
                        flipped = "-" + side;
                    } else {
                        flipped = "-(" + side + ")";
                    }
                }
                text.replace(uz(s.begin), uz(s.end - s.begin), flipped);
            }
            out.batch.set(path, text);
            done.push_back(key_name(k));
        }
    }
    if(done.size() == 1)
        out.batch.label = "flip the side of " + done.front();
    else if(!done.empty())
        out.batch.label =
            std::format("flip the sides of {} constraints", done.size());
    return out;
}

SketchEdit delete_vertex(const Document & doc, std::span<const SelKey> keys) {
    SketchEdit out;
    if(!doc.has_instance()) return out;
    const Instance & inst = *doc.instance();
    Cascade c(inst.doc());
    std::vector<std::string> what;
    for(const SelKey & k : keys) {
        if(k.kind != Kind::Element) continue;
        if(k.sub.starts_with("points ")) {
            const Element * e = c.find(k.name);
            if(!e || !chain(*e) || !slot_path(inst, k)) continue;
            c.remove_vertex(*e, std::stoi(k.sub.substr(7)));
            what.push_back(
                std::format("vertex {} of {}", k.sub.substr(7), k.name));
            continue;
        }
        const Element * p = c.find(k.name);
        if(!k.sub.empty() || !p || p->type != "point") continue;
        for(const Element & e : c.elements()) {
            if(!chain(e)) continue;
            const std::vector<Slot> slots = slots_of(inst.doc(), e);
            for(std::size_t j = 0; j < slots.size(); ++j)
                if(shared_point(slots[j].text) == k.name) {
                    c.remove_vertex(e, static_cast<int>(j));
                    what.push_back(std::format("vertex {} of {}", j, e.name));
                }
        }
    }
    if(what.empty()) {
        out.refusal =
            "delete vertex: select a vertex of a polyline or "
            "polygon";
        return out;
    }
    c.close();
    const std::string label =
        what.size() == 1 ? "delete " + what.front()
                         : std::format("delete {} vertices", what.size());
    out.batch = c.batch(label);
    out.removed = c.removed();
    out.check = c.check();
    for(const std::string & b : c.broken())
        if(!std::ranges::contains(out.check, b)) out.check.push_back(b);
    out.report = report_of(label, out.removed, out.check);
    return out;
}

SketchEdit insert_vertex(const Document & doc, const SelKey & element, int edge,
                         Vec2d at_point) {
    SketchEdit out;
    if(!doc.has_instance()) return out;
    const Instance & inst = *doc.instance();
    const std::string path = element_path(element.name);
    const Json * e = inst.get(path);
    const std::string type = e ? text_of(member(*e, "type")) : std::string();
    const Json * points = e ? member(*e, "points") : nullptr;
    if(element.kind != Kind::Element ||
       (type != "polyline" && type != "polygon") || !points ||
       !points->is_array()) {
        out.refusal = "insert vertex: select a polyline or a polygon";
        return out;
    }
    const int n = static_cast<int>(points->size());
    const int edges = type == "polygon" ? n : n - 1;
    if(edge < 0 || edge >= edges) {
        out.refusal =
            std::format("insert vertex: {} has no edge {}", element.name, edge);
        return out;
    }
    const std::string name = unused_point_name(inst, body_of(element.name), {});
    Json list = *points;
    list.insert(list.begin() + edge + 1, name);
    out.batch.set(element_path(name),
                  Json{{"type", "point"}, {"value", {at_point.x, at_point.y}}});
    out.batch.set(path + ".points", std::move(list));
    out.created.emplace_back(Kind::Element, name);

    IndexMap map;
    map.vertex = [edge](int v) -> std::optional<int> {
        return v <= edge ? v : v + 1;
    };
    map.edge = [edge](int j) -> std::optional<int> {
        if(j == edge) return std::nullopt;
        return j < edge ? j : j + 1;
    };
    const KindNames kinds(inst.doc());
    std::vector<std::size_t> gone;
    for(const NameUse & u : find_name_uses(inst.doc(), element.name).uses) {
        const Scanned s =
            scan_indices(text_of(inst.get(u.path)), element.name, map);
        const std::optional<std::size_t> i = constraint_index(u.path);
        if(s.gone && i &&
           match_kind(
               text_of(inst.get(std::format("constraints[{}].expr", *i))),
               kinds)
                   .kind != K::Formula) {
            if(!std::ranges::contains(gone, *i)) gone.push_back(*i);
            continue;
        }
        const std::string what = i ? constraint_label(inst.doc(), *i) : u.path;
        if((s.gone || s.unknown) && !std::ranges::contains(out.check, what))
            out.check.push_back(what);
        if(s.text != text_of(inst.get(u.path))) out.batch.set(u.path, s.text);
    }
    std::ranges::sort(gone, std::greater<>());
    for(const std::size_t i : gone) {
        out.removed.push_back(constraint_label(inst.doc(), i));
        out.batch.erase(std::format("constraints[{}]", i));
    }
    std::ranges::reverse(out.removed);
    out.batch.label =
        std::format("insert vertex {} in {}", edge + 1, element.name);
    out.report = report_of(out.batch.label, out.removed, out.check);
    return out;
}

SketchCascade sketch_cascade(const Instance & instance,
                             std::span<const SelKey> keys) {
    SketchCascade out;
    Cascade c(instance.doc());
    bool any = false;
    for(const SelKey & k : keys) {
        if(k.kind == Kind::Body) {
            if(instance.get("sketch.bodies." + k.name)) {
                c.erase_body(k.name);
                any = true;
            }
            continue;
        }
        if(k.kind != Kind::Element) continue;
        const Element * e = c.find(k.name);
        if(!e) continue;
        any = true;
        if(chain(*e) && k.sub.starts_with("points ") && slot_path(instance, k))
            c.remove_vertex(*e, std::stoi(k.sub.substr(7)));
        else
            c.erase_element(*e, "");
    }
    if(!any) return out;
    c.close();
    out.erased = c.erased_paths();
    out.names = c.names();
    out.taken = c.taken();
    out.orphans = c.orphans();
    out.vertices = c.vertices();
    out.removed = c.removed();
    out.check = c.check();
    for(const std::string & path : c.touched())
        if(!c.dead(path))
            if(const Json * v = at(c.work(), path))
                out.sets.emplace_back(path, *v);
    for(const std::size_t i : c.gone_constraints())
        out.constraints.push_back(std::format("constraints[{}]", i));
    return out;
}

}  // namespace gs::app
