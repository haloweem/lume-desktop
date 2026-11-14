# Island component style cards

Bundled static cards rendered from Lume's native component drawing code on 2026-11-09.
These images are examples for choosing an appearance; the settings page does not
start a live island preview, performance sampler, or preview refresh loop.

The flip and pixel clock cards added on 2026-11-09 use the same native drawing
entry point and 200 x 144 DIP canvas at 192 DPI. clock-flip.png and its -en
variant show 12:54 PM; clock-pixel.png and its -en variant show 15:06:00.
Flip uses system numeric glyphs and a bounded split-flap transition at minute
changes; pixel digits are square geometry with no font dependency. The four
PNG examples remain static and do not start timers in the settings page.

- `clock-simple.png`, `clock-progress.png`, `clock-digital.png`, `clock-date.png`
  (and their `-en` variants): `DrawIslandClockCard`,
  fixed local time 2026-11-09 Monday 16:28:36, 200 x 144 DIP at 192 DPI.
- `music.png`, `calendar-weather.png`, `todos.png` (and their `-en` variants):
  `IslandWindow::Preview`, cropped to the component's native rectangle.
  Music uses the existing bundled Forest cover; weather and tasks are example data.
- `system.png` and `system-en.png`: `DrawIslandPerformanceCard`, fixed CPU 13%, GPU 77%,
  memory 57%, no battery, 276 x 144 DIP at 192 DPI.

The optional `-en` suffix selects the English rendering. Other files use Chinese.
Generate with `lume-dynamic-island-test`; output is under
`verification/dynamic-island/style-*.png` beside the test executable.
Copy those PNGs here after removing the `style-` prefix.
