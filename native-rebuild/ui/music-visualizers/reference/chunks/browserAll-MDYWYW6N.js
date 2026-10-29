import {
  AccessibilitySystem,
  DOMPipe,
  EventSystem,
  FederatedContainer,
  accessibilityTarget
} from "./chunk-7KQJGMWW.js";
import "./chunk-CYPGVIBI.js";
import "./chunk-PTCDUNF3.js";
import "./chunk-J3WJE4OM.js";
import "./chunk-MLUUFKAR.js";
import "./chunk-OMGNOVTO.js";
import {
  Container
} from "./chunk-YLRGXFJF.js";
import "./chunk-MQOM6CO4.js";
import "./chunk-DHP7E6CG.js";
import {
  extensions
} from "./chunk-HTJVFOAC.js";
import "./chunk-XCBH6NLF.js";

// node_modules/pixi.js/lib/accessibility/init.mjs
extensions.add(AccessibilitySystem);
extensions.mixin(Container, accessibilityTarget);

// node_modules/pixi.js/lib/dom/init.mjs
extensions.add(DOMPipe);

// node_modules/pixi.js/lib/events/init.mjs
extensions.add(EventSystem);
extensions.mixin(Container, FederatedContainer);
