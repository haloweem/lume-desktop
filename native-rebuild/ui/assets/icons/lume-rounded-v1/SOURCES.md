# Lume Rounded Soft Fill v1

Seven original icon symbols generated on 2026-11-02 through the user-requested
compatible Images API with gpt-image-2. The default artwork was cropped and keyed
from a single generated sheet; selected variants preserve its geometry and alpha.

Full design specification: docs/current/ui-icon-spec.md.
Generation prompt and original sheet: output/imagegen/lume-ui-icons-v1-*.
No reference application's logo or source icon assets were copied.
API credentials are not included in any asset or script.

On 2026-11-08, launchpad and control-center were generated as a matched extension
with the same user-requested compatible Images API and gpt-image-2, using the
existing settings, clock and quick-launch masters as style references. The two
new default symbols were chroma keyed and normalized, with selected variants
derived from the same pixels and alpha. Both provide 24, 48, 72 and 256px PNGs.
The original seven symbols are unchanged. These assets are used in the Dock
settings sidebar; the desktop Dock's procedural glyph drawing is unchanged.

Prompt, original sheet and alpha sheet:
output/imagegen/lume-launchpad-control-center-v1-*.
Postprocessing: native-rebuild/tools/prepare-lume-dock-icons-v1.py.

A Dock settings symbol was added on the same date: four rounded app tiles on a
low horizontal tray. The settings, launchpad and control-center masters were
used as style references with the same API/model. The initial request failed;
the second request succeeded. Eight transparent files were derived from one
master with matching selected-state alpha. Prompt, original and alpha sheet:
output/imagegen/lume-dock-v1-*. Postprocessing: native-rebuild/tools/prepare-lume-dock-v1.py.
