import {
  AccessibilitySystem,
  DOMPipe,
  EventSystem,
  FederatedContainer,
  accessibilityTarget
} from "./chunk-TBEP6LZB.js";
import "./chunk-6DOTQLD2.js";
import "./chunk-4NXWZ6PK.js";
import "./chunk-HSUG4ZVK.js";
import "./chunk-7ZSBF6TF.js";
import {
  Container
} from "./chunk-X3R6DFVM.js";
import "./chunk-IZFAMZ5S.js";
import "./chunk-NWTXCN7L.js";
import {
  extensions
} from "./chunk-U6NYUJ2P.js";
import "./chunk-NZLE2WMY.js";

// ../../../../../../build-artifacts/folia-lyrics-build/node_modules/pixi.js/lib/accessibility/init.mjs
extensions.add(AccessibilitySystem);
extensions.mixin(Container, accessibilityTarget);

// ../../../../../../build-artifacts/folia-lyrics-build/node_modules/pixi.js/lib/dom/init.mjs
extensions.add(DOMPipe);

// ../../../../../../build-artifacts/folia-lyrics-build/node_modules/pixi.js/lib/events/init.mjs
extensions.add(EventSystem);
extensions.mixin(Container, FederatedContainer);
