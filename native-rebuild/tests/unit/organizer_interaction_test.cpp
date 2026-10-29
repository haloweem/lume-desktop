#include "core/organizer/organizer_interaction.h"

#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using lume::organizer::Interaction;

namespace {

void Check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool Equal(const std::vector<std::string>& actual,
           std::initializer_list<std::string_view> expected) {
    if (actual.size() != expected.size()) return false;
    std::size_t index = 0;
    for (const auto value : expected) {
        if (actual[index++] != value) return false;
    }
    return true;
}

void CheckEqual(const std::vector<std::string>& actual,
                std::initializer_list<std::string_view> expected,
                const char* message) {
    Check(Equal(actual, expected), message);
}

void SelectionUsesStableIdsAndVisibleOrder() {
    Interaction interaction;
    const std::vector<std::string> visible{"a", "b", "c", "d", "e"};

    interaction.Select(visible, "a", false, false);
    interaction.Select(visible, "c", false, true);
    CheckEqual(interaction.Selected(), {"a", "b", "c"},
               "shift selects the inclusive visible range");

    interaction.Select(visible, "e", true, false);
    CheckEqual(interaction.Selected(), {"a", "b", "c", "e"},
               "ctrl adds a stable ID");

    interaction.Select(visible, "b", true, false);
    CheckEqual(interaction.Selected(), {"a", "c", "e"},
               "ctrl toggles a stable ID");

    interaction.Select(visible, "d", true, true);
    CheckEqual(interaction.Selected(), {"a", "b", "c", "d", "e"},
               "ctrl-shift appends the inclusive range");
    Check(interaction.IsSelected("a") && interaction.IsSelected("e") &&
              !interaction.IsSelected("missing"),
          "selection lookup uses IDs");

    interaction.Select(visible, "c", false, false, true);
    CheckEqual(interaction.Selected(), {"a", "b", "c", "d", "e"},
               "preserveGroup keeps an already selected group");
    interaction.Select(visible, "c", false, false);
    CheckEqual(interaction.Selected(), {"c"},
               "a normal mouse-up selection collapses the group");

    interaction.SelectAll(visible);
    CheckEqual(interaction.Selected(), {"a", "b", "c", "d", "e"},
               "select all follows visible order");
    interaction.Clear();
    Check(interaction.Selected().empty() && !interaction.IsSelected("a"),
          "clear removes selection");
}

void ReconcileUsesIdsAndRepairsAnchor() {
    Interaction interaction;
    const std::vector<std::string> initial{"a", "b", "c", "d"};
    interaction.Select(initial, "a", false, false);
    interaction.Select(initial, "c", true, false);
    CheckEqual(interaction.Selected(), {"a", "c"}, "fixture has noncontiguous selection");

    const std::vector<std::string> reordered{"c", "d", "a"};
    interaction.Reconcile(reordered);
    CheckEqual(interaction.Selected(), {"c", "a"},
               "reconcile filters removed IDs and follows current visible order");

    interaction.Select(reordered, "a", false, true);
    CheckEqual(interaction.Selected(), {"c", "d", "a"},
               "range anchor follows its ID after reordering, not its old index");

    interaction.Select(reordered, "a", false, false);
    interaction.Reconcile({"c", "d"});
    interaction.Select({"c", "d"}, "d", false, true);
    CheckEqual(interaction.Selected(), {"d"},
               "shift falls back to the clicked ID when its anchor was removed");

    interaction.SelectAll({"d", "c"});
    interaction.Reconcile({"c"});
    CheckEqual(interaction.Selected(), {"c"},
               "reconcile also handles select-all after removal");
    interaction.Reconcile({});
    Check(interaction.Selected().empty(), "empty visibility removes all selection");
    interaction.Select(initial, "d", false, true);
    CheckEqual(interaction.Selected(), {"d"}, "removed anchor cannot revive at its old index");
}

void DragPreviewMovesStableNoncontiguousGroup() {
    Interaction interaction;
    const std::vector<std::string> order{"a", "b", "c", "d", "e", "f"};
    interaction.Select(order, "b", false, false);
    interaction.Select(order, "d", true, false);

    const auto before = order;
    Check(interaction.BeginDrag(order, "d"), "drag starts for a selected ID");
    Check(interaction.Dragging(), "dragging state is set");
    CheckEqual(interaction.Dragged(), {"b", "d"},
               "dragged group follows original order");
    CheckEqual(interaction.Preview(), {"a", "b", "c", "d", "e", "f"},
               "drag preview starts at original order");
    interaction.MoveBefore("b");
    Check(interaction.Preview() == order, "initial self target does not compact the group");
    Check(order == before, "begin drag does not mutate the caller's order");

    interaction.MoveBefore("e");
    CheckEqual(interaction.Preview(), {"a", "c", "b", "d", "e", "f"},
               "group moves forward before a target");
    interaction.MoveBefore("a");
    CheckEqual(interaction.Preview(), {"b", "d", "a", "c", "e", "f"},
               "group moves backward before a target");
    interaction.MoveBefore(std::nullopt);
    CheckEqual(interaction.Preview(), {"a", "c", "e", "f", "b", "d"},
               "no target moves the group to the end");

    const auto previewBeforeSelfTarget = interaction.Preview();
    interaction.MoveBefore("d");
    Check(interaction.Preview() == previewBeforeSelfTarget,
          "target inside dragged group leaves preview unchanged");
    interaction.MoveBefore("b");
    Check(interaction.Preview() == previewBeforeSelfTarget,
          "every dragged ID is a self target, including the first one");
    interaction.MoveBefore("c");
    CheckEqual(interaction.Preview(), {"a", "b", "d", "c", "e", "f"},
               "later moves use the original order and keep the dragged group ordered");
    Check(order == before, "preview operations never mutate model order");

    interaction.EndDrag();
    Check(!interaction.Dragging() && interaction.Dragged().empty() &&
              interaction.Preview().empty(),
          "end drag clears drag state");
    CheckEqual(interaction.Selected(), {"b", "d"},
               "end drag keeps selection");
    Check(order == before, "ending an uncommitted drag leaves model order unchanged");
}

void DraggingUnselectedIdSelectsOnlyThatId() {
    Interaction interaction;
    const std::vector<std::string> order{"a", "b", "c"};
    interaction.Select(order, "a", false, false);
    interaction.Select(order, "c", true, false);

    Check(interaction.BeginDrag(order, "b"), "drag starts for an unselected ID");
    CheckEqual(interaction.Selected(), {"b"},
               "dragging outside the group selects the grabbed ID");
    CheckEqual(interaction.Dragged(), {"b"},
               "dragging outside the group drags only the grabbed ID");

    interaction.EndDrag();
    Check(!interaction.BeginDrag(order, "removed"),
          "drag does not start for an ID absent from the order");
    Check(!interaction.Dragging() && interaction.Dragged().empty(),
          "failed drag leaves no drag state");
}

void RepeatedShiftKeepsAnchorAndCtrlShiftAddsRange() {
    Interaction interaction;
    const std::vector<std::string> visible{"a", "b", "c", "d", "e", "f"};
    interaction.Select(visible, "d", false, false);
    interaction.Select(visible, "b", false, true);
    CheckEqual(interaction.Selected(), {"b", "c", "d"}, "reverse shift range is inclusive");
    interaction.Select(visible, "f", false, true);
    CheckEqual(interaction.Selected(), {"d", "e", "f"},
               "repeated shift replaces the range from the original anchor");
    interaction.Select(visible, "e", false, true);
    CheckEqual(interaction.Selected(), {"d", "e"}, "shift can contract the range");
    interaction.Select(visible, "b", true, true);
    CheckEqual(interaction.Selected(), {"b", "c", "d", "e"},
               "ctrl-shift preserves selection outside the new range");
    interaction.Select(visible, "a", true, true);
    CheckEqual(interaction.Selected(), {"a", "b", "c", "d", "e"},
               "repeated ctrl-shift has no duplicate IDs");
}

void MissingAnchorAndPreserveGroupRespectModifiers() {
    Interaction interaction;
    const std::vector<std::string> visible{"a", "b", "c", "d"};
    interaction.Select(visible, "c", false, true);
    CheckEqual(interaction.Selected(), {"c"}, "initial shift falls back to the clicked ID");
    interaction.Select(visible, "d", false, true);
    CheckEqual(interaction.Selected(), {"c", "d"}, "fallback click establishes an anchor");
    interaction.Clear();
    interaction.Select(visible, "b", true, true);
    CheckEqual(interaction.Selected(), {"b"}, "clear removes the previous anchor");
    interaction.Select(visible, "d", true, false);
    interaction.Select(visible, "b", true, false, true);
    CheckEqual(interaction.Selected(), {"d"}, "preserveGroup does not suppress ctrl toggle");
    interaction.Select(visible, "a", false, true, true);
    CheckEqual(interaction.Selected(), {"a", "b"}, "preserveGroup does not suppress shift range");
    interaction.Select(visible, "d", false, false, true);
    CheckEqual(interaction.Selected(), {"d"}, "preserveGroup still selects an unselected ID alone");

    interaction.Select({"a", "b"}, "a", false, true);
    CheckEqual(interaction.Selected(), {"a"}, "select itself repairs an invisible anchor");
    interaction.Select({"a", "b"}, "removed", false, false);
    CheckEqual(interaction.Selected(), {"a"}, "an invisible clicked ID cannot become selected");
}

void SelectionAndDragFollowNewOrder() {
    Interaction interaction;
    const std::vector<std::string> initial{"a", "b", "c", "d"};
    interaction.Select(initial, "d", false, false);
    interaction.Select(initial, "b", true, false);
    CheckEqual(interaction.Selected(), {"b", "d"}, "selection order ignores ctrl click order");

    interaction.Select({"d", "c", "b", "a"}, "c", true, false);
    CheckEqual(interaction.Selected(), {"d", "c", "b"}, "selection follows the latest visible order");
    Check(interaction.BeginDrag({"b", "a", "d"}, "d"), "drag reconciles new model order");
    CheckEqual(interaction.Selected(), {"b", "d"}, "begin drag removes stale selected IDs");
    CheckEqual(interaction.Dragged(), {"b", "d"}, "dragged IDs follow original model order");
    interaction.MoveBefore("a");
    CheckEqual(interaction.Preview(), {"b", "d", "a"}, "reconciled group moves together");
    const auto committed = interaction.Preview();
    interaction.EndDrag();
    interaction.Reconcile(committed);
    CheckEqual(interaction.Selected(), {"b", "d"}, "selection follows an explicitly committed order");

    interaction.SelectAll({"d", "a", "d", "b", "a"});
    CheckEqual(interaction.Selected(), {"d", "a", "b"}, "select all contains each stable ID once");
    interaction.SelectAll({});
    Check(interaction.Selected().empty(), "select all on an empty view clears selection");
}

void CancelLeavesModelAndSelectionUntouched() {
    Interaction interaction;
    const std::vector<std::string> model{"a", "b", "c", "d"};
    interaction.Select(model, "b", false, false);
    interaction.Select(model, "d", true, false);
    const auto selected = interaction.Selected();
    Check(interaction.BeginDrag(model, "b"), "cancel fixture begins a group drag");
    interaction.MoveBefore("a");
    const auto preview = interaction.Preview();
    Check(preview != model, "cancel fixture has a pending change");
    interaction.EndDrag();
    CheckEqual(model, {"a", "b", "c", "d"}, "cancel cannot write to the external model");
    Check(interaction.Selected() == selected, "cancel preserves selection");
    interaction.MoveBefore(std::nullopt);
    interaction.EndDrag();
    Check(!interaction.Dragging() && interaction.Preview().empty() && interaction.Dragged().empty(),
          "cancel is idempotent and later pointer moves do nothing");
    CheckEqual(preview, {"b", "d", "a", "c"}, "caller can retain a preview by value for explicit commit");
    Check(interaction.BeginDrag(model, "d"), "a fresh drag can begin after cancel");
    Check(interaction.Preview() == model, "new drag starts at current model order, not canceled preview");
}

void EmptyAndWholeGroupDragAreStable() {
    Interaction interaction;
    Check(interaction.Selected().empty() && !interaction.Dragging() &&
              interaction.Dragged().empty() && interaction.Preview().empty(), "default state is idle");
    interaction.MoveBefore("missing");
    Check(!interaction.BeginDrag({}, "a") && interaction.Preview().empty(), "empty order cannot begin a drag");
    const std::vector<std::string> order{"a", "b", "c"};
    interaction.SelectAll(order);
    Check(interaction.BeginDrag(order, "c"), "whole selection can be dragged");
    interaction.MoveBefore(std::nullopt);
    Check(interaction.Preview() == order, "moving the whole order to the end changes nothing");
    interaction.MoveBefore("b");
    Check(interaction.Preview() == order, "whole-group self target is stable");
}

void ReconcileCancelsStaleDragSnapshots() {
    Interaction interaction;
    const std::vector<std::string> order{"a", "b", "c", "d"};
    interaction.Select(order, "b", false, false);
    interaction.Select(order, "d", true, false);
    Check(interaction.BeginDrag(order, "b"), "reconcile fixture begins a drag");
    interaction.MoveBefore("a");
    const auto preview = interaction.Preview();
    interaction.Reconcile(order);
    Check(interaction.Dragging() && interaction.Preview() == preview,
          "unchanged visibility does not interrupt a drag");
    interaction.Reconcile({"c", "d", "a"});
    CheckEqual(interaction.Selected(), {"d"}, "removal filters selection while dragging");
    Check(!interaction.Dragging() && interaction.Dragged().empty() && interaction.Preview().empty(),
          "changed visibility cancels a snapshot that could restore removed IDs");
}

void StringViewsAreCopiedAndComparedByLength() {
    Interaction interaction;
    std::vector<std::string> order{std::string(80, 'x'), "b", "c"};
    auto storage = order.front() + "suffix";
    interaction.Select(order, std::string_view(storage.data(), 80), false, false);
    storage.assign(100, 'z');
    interaction.Select(order, "b", false, true);
    Check(interaction.Selected() == std::vector<std::string>({order.front(), "b"}),
          "anchor owns its ID rather than retaining a temporary string view");
    Check(interaction.BeginDrag(order, std::string_view(order[1])), "drag accepts string views");
    order.assign({"replacement"});
    interaction.MoveBefore("c");
    CheckEqual(interaction.Dragged(), {std::string(80, 'x'), "b"}, "dragged IDs own their storage");
    Check(interaction.Preview().size() == 3 && interaction.Preview().back() == "c",
          "drag retains a snapshot rather than references to caller storage");
}

}  // namespace

int main() {
    const struct { const char* name; void (*run)(); } tests[] = {
        {"selection modifiers", SelectionUsesStableIdsAndVisibleOrder},
        {"stable IDs and anchors", ReconcileUsesIdsAndRepairsAnchor},
        {"noncontiguous drag preview", DragPreviewMovesStableNoncontiguousGroup},
        {"unselected drag", DraggingUnselectedIdSelectsOnlyThatId},
        {"repeated shift", RepeatedShiftKeepsAnchorAndCtrlShiftAddsRange},
        {"fallback and preserveGroup", MissingAnchorAndPreserveGroupRespectModifiers},
        {"new visible and model order", SelectionAndDragFollowNewOrder},
        {"cancel without model mutation", CancelLeavesModelAndSelectionUntouched},
        {"empty and whole-group drag", EmptyAndWholeGroupDragAreStable},
        {"stale drag reconciliation", ReconcileCancelsStaleDragSnapshots},
        {"owned stable IDs", StringViewsAreCopiedAndComparedByLength},
    };
    unsigned failures = 0;
    for (const auto& test : tests) {
        try {
            test.run();
            std::cout << "PASS: " << test.name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL: " << test.name << ": " << error.what() << '\n';
        }
    }
    std::cout << "organizer interaction: " << failures << " failures\n";
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
