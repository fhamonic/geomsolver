#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "gs/app/document.hpp"
#include "gs/app/selection.hpp"
#include "gs/engine/model.hpp"

namespace gs::app {

// Whether a params, design, sweeps, let or geometry entry, a world element
// or a body of the sketch is named `name`, the included files' geometry
// too: the namespace expressions share.
bool name_used(const Instance & instance, std::string_view name);
// The first "<prefix><k>", k from 1, that name_used() does not find: the
// name of a new entry of that namespace.
std::string unused_name(const Instance & instance, std::string_view prefix);
// The first "<prefix><k>", k from the list's size plus one, that no entry
// of the list `section` ("constraints", "criteria") is named.
std::string unused_item_name(const Instance & instance,
                             std::string_view section, std::string_view prefix);

// Edits of several selected entries at once, each one batch: one undo step.
// An entry the edit does not apply to, or that is as asked already, is left
// out; the batch is empty when nothing is left.

// Disables the selected constraints and criteria ("enabled": false), or
// enables them by erasing the key. The model takes one enabled objective:
// enabling a minimize or maximize criterion disables the one enabled before
// in the same batch ("switch objective to protrusion"), and of several
// selected objectives only the first is enabled.
EditBatch enable_batch(const Instance & instance, std::span<const SelKey> keys,
                       bool enabled);

// Fixes the selected design variables ("fixed": true), or releases them
// ("fixed": false).
EditBatch fix_batch(const Instance & instance, std::span<const SelKey> keys,
                    bool fixed);

// The display item {"expr": name, "label": name} after the last one for each
// selected let of `model` that no display item draws (display_keys()).
EditBatch draw_batch(const Instance & instance, const Model & model,
                     std::span<const SelKey> keys);

// The places in the document of the display items that draw a selected name
// (display_keys()), the last first. A selected display item that draws a
// name counts as that name.
std::vector<int> drawings_of(const Model & model, std::span<const SelKey> keys);

}  // namespace gs::app
