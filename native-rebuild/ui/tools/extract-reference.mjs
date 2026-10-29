// Build-time source extraction. Not loaded by the UI; never writes to history.
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
const history = "H:/refactor/history/lume1.0.1";
const panel =
  history + "/src/vscode-windhawk-ui/apps/vscode-windhawk-ui/src/app/panel";
const output = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const read = (p) => fs.readFileSync(p, "utf8");
function block(source, name) {
  const m = source.match(
    new RegExp("(?:export )?const " + name + " = styled[^`]*`([\\s\\S]*?)`;"),
  );
  if (!m) throw Error(name);
  return m[1];
}
const source = read(panel + "/DesktopBeautyExperience.styles.ts");
const names = [
  "HomeRoot",
  "HomeModuleTabs",
  "HomeModuleHighlight",
  "ModuleToolbar",
  "ModuleSearch",
  "SearchInput",
  "SearchClear",
  "MarketShell",
  "HomeHero",
  "HomeDots",
  "DesktopBeautyLayout",
  "WallpaperLibraryLayout",
  "SearchResultsLayout",
  "SearchResultsHead",
  "SearchResultsSearch",
  "SearchBackButton",
  "HoverBody",
  "HoverTitle",
  "CardActions",
  "HoverMetaRow",
  "HomeHoverOverlay",
  "Pagination",
  "PageList",
  "CardTitle",
  "CardMeta",
];
const material = read(panel + "/../capsuleMaterial.ts").match(
  /css`([\s\S]*?)`/,
)[1];
let css =
  "/* Extracted from original styled-components. Values retained; selectors renamed. */\n";
for (const name of names) {
  let content = block(source, name).replace("${capsuleMaterial}", material);
  for (const n of names) content = content.replaceAll("${" + n + "}", "." + n);
  if (content.includes("${")) throw Error("Unresolved " + name);
  css += "." + name + " {" + content + "}\n";
}
const moduleSource = read(panel + "/ModuleCard.tsx");
for (const name of [
  "CardGrid",
  "Card",
  "OpenButton",
  "Title",
  "Description",
  "Toggle",
]) {
  let content = block(moduleSource, name)
    .replaceAll("${MEDIA_CARD_GRID.columnGap}", "clamp(22px, 1.7vw, 38px)")
    .replaceAll("${MEDIA_CARD_GRID.rowGap}", "clamp(34px, 2.35vw, 54px)")
    .replaceAll("${MEDIA_CARD_GRID.minWidth}", "clamp(260px, 18vw, 380px)")
    .replaceAll("${MEDIA_CARD_GRID.aspectRatio}", "16 / 9")
    .replace("${blackTexture}", "./assets/black-texture.webp");
  css += "." + name + " {" + content + "}\n";
}
css +=
  ".NavigationBackButton {" +
  block(
    read(panel + "/../components/NavigationBackButton.ts"),
    "NavigationBackButton",
  ) +
  "}\n";
fs.writeFileSync(output + "/reference.css", css);
const titlebar = read(
  history +
    "/third_party/folia-major/upstream/shared/lume-titlebar/titlebar.ts",
);
const titleCss = titlebar
  .match(/const TITLEBAR_CSS = `([\s\S]*?)`;/)[1]
  .replaceAll(
    "${FONT_FAMILY}",
    "-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,'Helvetica Neue',Arial,'Noto Sans',sans-serif",
  )
  .replaceAll("${LINE_HEIGHT}", "22.001px");
fs.writeFileSync(output + "/titlebar.css", titleCss);
const icons = ["USER_ICON_PATH", "GEAR_ICON_PATH", "MUSIC_ICON_PATH"]
  .map(
    (name) =>
      `export const ${name} = '${titlebar.match(new RegExp("const " + name + " = '([^']+)'"))[1]}';`,
  )
  .join("\n");
fs.writeFileSync(output + "/icons.js", icons);
fs.mkdirSync(output + "/assets/fonts", { recursive: true });
fs.copyFileSync(
  panel + "/assets/mod-covers/black-texture.webp",
  output + "/assets/black-texture.webp",
);
for (const [skin, fonts] of [
  ["Welfare", ["GeosansLight.ttf", "Ruthligos.ttf"]],
  ["Mond", ["Anurati.otf", "Quicksand.otf"]],
  ["Inside Dream", ["Allura.otf", "Futurist Fixed-width.ttf"]],
])
  for (const font of fonts)
    fs.copyFileSync(
      history +
        "/src/vscode-windhawk/widget-skins/" +
        skin +
        "/@Resources/Fonts/" +
        font,
      output + "/assets/fonts/" + font,
    );
console.log(
  "Extracted original titlebar and card CSS, texture and six clock fonts. Official covers are loaded from the catalog at runtime.",
);
