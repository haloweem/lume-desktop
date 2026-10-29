import {
  ColorBlend,
  ColorBurnBlend,
  ColorDodgeBlend,
  DarkenBlend,
  DifferenceBlend,
  DivideBlend,
  ExclusionBlend,
  HardLightBlend,
  HardMixBlend,
  LightenBlend,
  LinearBurnBlend,
  LinearDodgeBlend,
  LinearLightBlend,
  LuminosityBlend,
  NegationBlend,
  OverlayBlend,
  PinLightBlend,
  SaturationBlend,
  SoftLightBlend,
  SubtractBlend,
  VividLightBlend
} from "./chunk-XS5YDGGF.js";
import {
  SONNET_TRANSITION_KINDS,
  findSonnetParagraphIndexAtTime,
  hashSonnetSeed
} from "./chunk-42RMLPHV.js";
import "./chunk-DHP7E6CG.js";
import {
  extensions
} from "./chunk-HTJVFOAC.js";
import {
  require_react_dom
} from "./chunk-VACLHSAV.js";
import {
  lucide_react_exports
} from "./chunk-VROG7HKI.js";
import {
  layoutWithLines,
  prepareWithSegments
} from "./chunk-KECZQD7W.js";
import "./chunk-LDALZVSS.js";
import {
  require_react
} from "./chunk-RG3JEOUL.js";
import "./chunk-LSTKNWUO.js";
import {
  resolveThemeFontStack,
  resolveThemeFontWeight
} from "./chunk-2CN2J6VF.js";
import {
  __commonJS,
  __toESM
} from "./chunk-XCBH6NLF.js";

// node_modules/react-dom/cjs/react-dom-server-legacy.browser.production.js
var require_react_dom_server_legacy_browser_production = __commonJS({
  "node_modules/react-dom/cjs/react-dom-server-legacy.browser.production.js"(exports) {
    "use strict";
    var React2 = require_react();
    var ReactDOM = require_react_dom();
    function formatProdErrorMessage(code) {
      var url = "https://react.dev/errors/" + code;
      if (1 < arguments.length) {
        url += "?args[]=" + encodeURIComponent(arguments[1]);
        for (var i = 2; i < arguments.length; i++)
          url += "&args[]=" + encodeURIComponent(arguments[i]);
      }
      return "Minified React error #" + code + "; visit " + url + " for the full message or use the non-minified dev environment for full errors and additional helpful warnings.";
    }
    var REACT_ELEMENT_TYPE = Symbol.for("react.transitional.element");
    var REACT_PORTAL_TYPE = Symbol.for("react.portal");
    var REACT_FRAGMENT_TYPE = Symbol.for("react.fragment");
    var REACT_STRICT_MODE_TYPE = Symbol.for("react.strict_mode");
    var REACT_PROFILER_TYPE = Symbol.for("react.profiler");
    var REACT_CONSUMER_TYPE = Symbol.for("react.consumer");
    var REACT_CONTEXT_TYPE = Symbol.for("react.context");
    var REACT_FORWARD_REF_TYPE = Symbol.for("react.forward_ref");
    var REACT_SUSPENSE_TYPE = Symbol.for("react.suspense");
    var REACT_SUSPENSE_LIST_TYPE = Symbol.for("react.suspense_list");
    var REACT_MEMO_TYPE = Symbol.for("react.memo");
    var REACT_LAZY_TYPE = Symbol.for("react.lazy");
    var REACT_SCOPE_TYPE = Symbol.for("react.scope");
    var REACT_ACTIVITY_TYPE = Symbol.for("react.activity");
    var REACT_LEGACY_HIDDEN_TYPE = Symbol.for("react.legacy_hidden");
    var REACT_MEMO_CACHE_SENTINEL = Symbol.for("react.memo_cache_sentinel");
    var REACT_VIEW_TRANSITION_TYPE = Symbol.for("react.view_transition");
    var MAYBE_ITERATOR_SYMBOL = Symbol.iterator;
    function getIteratorFn(maybeIterable) {
      if (null === maybeIterable || "object" !== typeof maybeIterable) return null;
      maybeIterable = MAYBE_ITERATOR_SYMBOL && maybeIterable[MAYBE_ITERATOR_SYMBOL] || maybeIterable["@@iterator"];
      return "function" === typeof maybeIterable ? maybeIterable : null;
    }
    var isArrayImpl = Array.isArray;
    function murmurhash3_32_gc(key, seed) {
      var remainder = key.length & 3;
      var bytes = key.length - remainder;
      var h1 = seed;
      for (seed = 0; seed < bytes; ) {
        var k1 = key.charCodeAt(seed) & 255 | (key.charCodeAt(++seed) & 255) << 8 | (key.charCodeAt(++seed) & 255) << 16 | (key.charCodeAt(++seed) & 255) << 24;
        ++seed;
        k1 = 3432918353 * (k1 & 65535) + ((3432918353 * (k1 >>> 16) & 65535) << 16) & 4294967295;
        k1 = k1 << 15 | k1 >>> 17;
        k1 = 461845907 * (k1 & 65535) + ((461845907 * (k1 >>> 16) & 65535) << 16) & 4294967295;
        h1 ^= k1;
        h1 = h1 << 13 | h1 >>> 19;
        h1 = 5 * (h1 & 65535) + ((5 * (h1 >>> 16) & 65535) << 16) & 4294967295;
        h1 = (h1 & 65535) + 27492 + (((h1 >>> 16) + 58964 & 65535) << 16);
      }
      k1 = 0;
      switch (remainder) {
        case 3:
          k1 ^= (key.charCodeAt(seed + 2) & 255) << 16;
        case 2:
          k1 ^= (key.charCodeAt(seed + 1) & 255) << 8;
        case 1:
          k1 ^= key.charCodeAt(seed) & 255, k1 = 3432918353 * (k1 & 65535) + ((3432918353 * (k1 >>> 16) & 65535) << 16) & 4294967295, k1 = k1 << 15 | k1 >>> 17, h1 ^= 461845907 * (k1 & 65535) + ((461845907 * (k1 >>> 16) & 65535) << 16) & 4294967295;
      }
      h1 ^= key.length;
      h1 ^= h1 >>> 16;
      h1 = 2246822507 * (h1 & 65535) + ((2246822507 * (h1 >>> 16) & 65535) << 16) & 4294967295;
      h1 ^= h1 >>> 13;
      h1 = 3266489909 * (h1 & 65535) + ((3266489909 * (h1 >>> 16) & 65535) << 16) & 4294967295;
      return (h1 ^ h1 >>> 16) >>> 0;
    }
    var assign = Object.assign;
    var hasOwnProperty = Object.prototype.hasOwnProperty;
    var VALID_ATTRIBUTE_NAME_REGEX = RegExp(
      "^[:A-Z_a-z\\u00C0-\\u00D6\\u00D8-\\u00F6\\u00F8-\\u02FF\\u0370-\\u037D\\u037F-\\u1FFF\\u200C-\\u200D\\u2070-\\u218F\\u2C00-\\u2FEF\\u3001-\\uD7FF\\uF900-\\uFDCF\\uFDF0-\\uFFFD][:A-Z_a-z\\u00C0-\\u00D6\\u00D8-\\u00F6\\u00F8-\\u02FF\\u0370-\\u037D\\u037F-\\u1FFF\\u200C-\\u200D\\u2070-\\u218F\\u2C00-\\u2FEF\\u3001-\\uD7FF\\uF900-\\uFDCF\\uFDF0-\\uFFFD\\-.0-9\\u00B7\\u0300-\\u036F\\u203F-\\u2040]*$"
    );
    var illegalAttributeNameCache = {};
    var validatedAttributeNameCache = {};
    function isAttributeNameSafe(attributeName) {
      if (hasOwnProperty.call(validatedAttributeNameCache, attributeName))
        return true;
      if (hasOwnProperty.call(illegalAttributeNameCache, attributeName)) return false;
      if (VALID_ATTRIBUTE_NAME_REGEX.test(attributeName))
        return validatedAttributeNameCache[attributeName] = true;
      illegalAttributeNameCache[attributeName] = true;
      return false;
    }
    var unitlessNumbers = new Set(
      "animationIterationCount aspectRatio borderImageOutset borderImageSlice borderImageWidth boxFlex boxFlexGroup boxOrdinalGroup columnCount columns flex flexGrow flexPositive flexShrink flexNegative flexOrder gridArea gridRow gridRowEnd gridRowSpan gridRowStart gridColumn gridColumnEnd gridColumnSpan gridColumnStart fontWeight lineClamp lineHeight opacity order orphans scale tabSize widows zIndex zoom fillOpacity floodOpacity stopOpacity strokeDasharray strokeDashoffset strokeMiterlimit strokeOpacity strokeWidth MozAnimationIterationCount MozBoxFlex MozBoxFlexGroup MozLineClamp msAnimationIterationCount msFlex msZoom msFlexGrow msFlexNegative msFlexOrder msFlexPositive msFlexShrink msGridColumn msGridColumnSpan msGridRow msGridRowSpan WebkitAnimationIterationCount WebkitBoxFlex WebKitBoxFlexGroup WebkitBoxOrdinalGroup WebkitColumnCount WebkitColumns WebkitFlex WebkitFlexGrow WebkitFlexPositive WebkitFlexShrink WebkitLineClamp".split(
        " "
      )
    );
    var aliases = /* @__PURE__ */ new Map([
      ["acceptCharset", "accept-charset"],
      ["htmlFor", "for"],
      ["httpEquiv", "http-equiv"],
      ["crossOrigin", "crossorigin"],
      ["accentHeight", "accent-height"],
      ["alignmentBaseline", "alignment-baseline"],
      ["arabicForm", "arabic-form"],
      ["baselineShift", "baseline-shift"],
      ["capHeight", "cap-height"],
      ["clipPath", "clip-path"],
      ["clipRule", "clip-rule"],
      ["colorInterpolation", "color-interpolation"],
      ["colorInterpolationFilters", "color-interpolation-filters"],
      ["colorProfile", "color-profile"],
      ["colorRendering", "color-rendering"],
      ["dominantBaseline", "dominant-baseline"],
      ["enableBackground", "enable-background"],
      ["fillOpacity", "fill-opacity"],
      ["fillRule", "fill-rule"],
      ["floodColor", "flood-color"],
      ["floodOpacity", "flood-opacity"],
      ["fontFamily", "font-family"],
      ["fontSize", "font-size"],
      ["fontSizeAdjust", "font-size-adjust"],
      ["fontStretch", "font-stretch"],
      ["fontStyle", "font-style"],
      ["fontVariant", "font-variant"],
      ["fontWeight", "font-weight"],
      ["glyphName", "glyph-name"],
      ["glyphOrientationHorizontal", "glyph-orientation-horizontal"],
      ["glyphOrientationVertical", "glyph-orientation-vertical"],
      ["horizAdvX", "horiz-adv-x"],
      ["horizOriginX", "horiz-origin-x"],
      ["imageRendering", "image-rendering"],
      ["letterSpacing", "letter-spacing"],
      ["lightingColor", "lighting-color"],
      ["markerEnd", "marker-end"],
      ["markerMid", "marker-mid"],
      ["markerStart", "marker-start"],
      ["overlinePosition", "overline-position"],
      ["overlineThickness", "overline-thickness"],
      ["paintOrder", "paint-order"],
      ["panose-1", "panose-1"],
      ["pointerEvents", "pointer-events"],
      ["renderingIntent", "rendering-intent"],
      ["shapeRendering", "shape-rendering"],
      ["stopColor", "stop-color"],
      ["stopOpacity", "stop-opacity"],
      ["strikethroughPosition", "strikethrough-position"],
      ["strikethroughThickness", "strikethrough-thickness"],
      ["strokeDasharray", "stroke-dasharray"],
      ["strokeDashoffset", "stroke-dashoffset"],
      ["strokeLinecap", "stroke-linecap"],
      ["strokeLinejoin", "stroke-linejoin"],
      ["strokeMiterlimit", "stroke-miterlimit"],
      ["strokeOpacity", "stroke-opacity"],
      ["strokeWidth", "stroke-width"],
      ["textAnchor", "text-anchor"],
      ["textDecoration", "text-decoration"],
      ["textRendering", "text-rendering"],
      ["transformOrigin", "transform-origin"],
      ["underlinePosition", "underline-position"],
      ["underlineThickness", "underline-thickness"],
      ["unicodeBidi", "unicode-bidi"],
      ["unicodeRange", "unicode-range"],
      ["unitsPerEm", "units-per-em"],
      ["vAlphabetic", "v-alphabetic"],
      ["vHanging", "v-hanging"],
      ["vIdeographic", "v-ideographic"],
      ["vMathematical", "v-mathematical"],
      ["vectorEffect", "vector-effect"],
      ["vertAdvY", "vert-adv-y"],
      ["vertOriginX", "vert-origin-x"],
      ["vertOriginY", "vert-origin-y"],
      ["wordSpacing", "word-spacing"],
      ["writingMode", "writing-mode"],
      ["xmlnsXlink", "xmlns:xlink"],
      ["xHeight", "x-height"]
    ]);
    var matchHtmlRegExp = /["'&<>]/;
    function escapeTextForBrowser(text) {
      if ("boolean" === typeof text || "number" === typeof text || "bigint" === typeof text)
        return "" + text;
      text = "" + text;
      var match = matchHtmlRegExp.exec(text);
      if (match) {
        var html = "", index, lastIndex = 0;
        for (index = match.index; index < text.length; index++) {
          switch (text.charCodeAt(index)) {
            case 34:
              match = "&quot;";
              break;
            case 38:
              match = "&amp;";
              break;
            case 39:
              match = "&#x27;";
              break;
            case 60:
              match = "&lt;";
              break;
            case 62:
              match = "&gt;";
              break;
            default:
              continue;
          }
          lastIndex !== index && (html += text.slice(lastIndex, index));
          lastIndex = index + 1;
          html += match;
        }
        text = lastIndex !== index ? html + text.slice(lastIndex, index) : html;
      }
      return text;
    }
    var uppercasePattern = /([A-Z])/g;
    var msPattern = /^ms-/;
    var isJavaScriptProtocol = /^[\u0000-\u001F ]*j[\r\n\t]*a[\r\n\t]*v[\r\n\t]*a[\r\n\t]*s[\r\n\t]*c[\r\n\t]*r[\r\n\t]*i[\r\n\t]*p[\r\n\t]*t[\r\n\t]*:/i;
    function sanitizeURL(url) {
      return isJavaScriptProtocol.test("" + url) ? "javascript:throw new Error('React has blocked a javascript: URL as a security precaution.')" : url;
    }
    var ReactSharedInternals = React2.__CLIENT_INTERNALS_DO_NOT_USE_OR_WARN_USERS_THEY_CANNOT_UPGRADE;
    var ReactDOMSharedInternals = ReactDOM.__DOM_INTERNALS_DO_NOT_USE_OR_WARN_USERS_THEY_CANNOT_UPGRADE;
    var sharedNotPendingObject = {
      pending: false,
      data: null,
      method: null,
      action: null
    };
    var previousDispatcher = ReactDOMSharedInternals.d;
    ReactDOMSharedInternals.d = {
      f: previousDispatcher.f,
      r: previousDispatcher.r,
      D: prefetchDNS,
      C: preconnect,
      L: preload,
      m: preloadModule,
      X: preinitScript,
      S: preinitStyle,
      M: preinitModuleScript
    };
    var PRELOAD_NO_CREDS = [];
    var currentlyFlushingRenderState = null;
    var scriptRegex = /(<\/|<)(s)(cript)/gi;
    function scriptReplacer(match, prefix2, s, suffix2) {
      return "" + prefix2 + ("s" === s ? "\\u0073" : "\\u0053") + suffix2;
    }
    function createResumableState(identifierPrefix, externalRuntimeConfig, bootstrapScriptContent, bootstrapScripts, bootstrapModules) {
      return {
        idPrefix: void 0 === identifierPrefix ? "" : identifierPrefix,
        nextFormID: 0,
        streamingFormat: 0,
        bootstrapScriptContent,
        bootstrapScripts,
        bootstrapModules,
        instructions: 0,
        hasBody: false,
        hasHtml: false,
        unknownResources: {},
        dnsResources: {},
        connectResources: { default: {}, anonymous: {}, credentials: {} },
        imageResources: {},
        styleResources: {},
        scriptResources: {},
        moduleUnknownResources: {},
        moduleScriptResources: {}
      };
    }
    function createFormatContext(insertionMode, selectedValue, tagScope, viewTransition) {
      return {
        insertionMode,
        selectedValue,
        tagScope,
        viewTransition
      };
    }
    function getChildFormatContext(parentContext, type, props) {
      var subtreeScope = parentContext.tagScope & -25;
      switch (type) {
        case "noscript":
          return createFormatContext(2, null, subtreeScope | 1, null);
        case "select":
          return createFormatContext(
            2,
            null != props.value ? props.value : props.defaultValue,
            subtreeScope,
            null
          );
        case "svg":
          return createFormatContext(4, null, subtreeScope, null);
        case "picture":
          return createFormatContext(2, null, subtreeScope | 2, null);
        case "math":
          return createFormatContext(5, null, subtreeScope, null);
        case "foreignObject":
          return createFormatContext(2, null, subtreeScope, null);
        case "table":
          return createFormatContext(6, null, subtreeScope, null);
        case "thead":
        case "tbody":
        case "tfoot":
          return createFormatContext(7, null, subtreeScope, null);
        case "colgroup":
          return createFormatContext(9, null, subtreeScope, null);
        case "tr":
          return createFormatContext(8, null, subtreeScope, null);
        case "head":
          if (2 > parentContext.insertionMode)
            return createFormatContext(3, null, subtreeScope, null);
          break;
        case "html":
          if (0 === parentContext.insertionMode)
            return createFormatContext(1, null, subtreeScope, null);
      }
      return 6 <= parentContext.insertionMode || 2 > parentContext.insertionMode ? createFormatContext(2, null, subtreeScope, null) : parentContext.tagScope !== subtreeScope ? createFormatContext(
        parentContext.insertionMode,
        parentContext.selectedValue,
        subtreeScope,
        null
      ) : parentContext;
    }
    function getSuspenseViewTransition(parentViewTransition) {
      return null === parentViewTransition ? null : {
        update: parentViewTransition.update,
        enter: "none",
        exit: "none",
        share: parentViewTransition.update,
        name: parentViewTransition.autoName,
        autoName: parentViewTransition.autoName,
        nameIdx: 0
      };
    }
    function getSuspenseFallbackFormatContext(resumableState, parentContext) {
      parentContext.tagScope & 32 && (resumableState.instructions |= 128);
      return createFormatContext(
        parentContext.insertionMode,
        parentContext.selectedValue,
        parentContext.tagScope | 12,
        getSuspenseViewTransition(parentContext.viewTransition)
      );
    }
    function getSuspenseContentFormatContext(resumableState, parentContext) {
      resumableState = getSuspenseViewTransition(parentContext.viewTransition);
      var subtreeScope = parentContext.tagScope | 16;
      null !== resumableState && "none" !== resumableState.share && (subtreeScope |= 64);
      return createFormatContext(
        parentContext.insertionMode,
        parentContext.selectedValue,
        subtreeScope,
        resumableState
      );
    }
    var styleNameCache = /* @__PURE__ */ new Map();
    function pushStyleAttribute(target, style) {
      if ("object" !== typeof style) throw Error(formatProdErrorMessage(62));
      var isFirst = true, styleName;
      for (styleName in style)
        if (hasOwnProperty.call(style, styleName)) {
          var styleValue = style[styleName];
          if (null != styleValue && "boolean" !== typeof styleValue && "" !== styleValue) {
            if (0 === styleName.indexOf("--")) {
              var nameChunk = escapeTextForBrowser(styleName);
              styleValue = escapeTextForBrowser(("" + styleValue).trim());
            } else
              nameChunk = styleNameCache.get(styleName), void 0 === nameChunk && (nameChunk = escapeTextForBrowser(
                styleName.replace(uppercasePattern, "-$1").toLowerCase().replace(msPattern, "-ms-")
              ), styleNameCache.set(styleName, nameChunk)), styleValue = "number" === typeof styleValue ? 0 === styleValue || unitlessNumbers.has(styleName) ? "" + styleValue : styleValue + "px" : escapeTextForBrowser(("" + styleValue).trim());
            isFirst ? (isFirst = false, target.push(' style="', nameChunk, ":", styleValue)) : target.push(";", nameChunk, ":", styleValue);
          }
        }
      isFirst || target.push('"');
    }
    function pushBooleanAttribute(target, name, value) {
      value && "function" !== typeof value && "symbol" !== typeof value && target.push(" ", name, '=""');
    }
    function pushStringAttribute(target, name, value) {
      "function" !== typeof value && "symbol" !== typeof value && "boolean" !== typeof value && target.push(" ", name, '="', escapeTextForBrowser(value), '"');
    }
    var actionJavaScriptURL = escapeTextForBrowser(
      "javascript:throw new Error('React form unexpectedly submitted.')"
    );
    function pushAdditionalFormField(value, key) {
      this.push('<input type="hidden"');
      validateAdditionalFormField(value);
      pushStringAttribute(this, "name", key);
      pushStringAttribute(this, "value", value);
      this.push("/>");
    }
    function validateAdditionalFormField(value) {
      if ("string" !== typeof value) throw Error(formatProdErrorMessage(480));
    }
    function getCustomFormFields(resumableState, formAction) {
      if ("function" === typeof formAction.$$FORM_ACTION) {
        var id = resumableState.nextFormID++;
        resumableState = resumableState.idPrefix + id;
        try {
          var customFields = formAction.$$FORM_ACTION(resumableState);
          if (customFields) {
            var formData = customFields.data;
            null != formData && formData.forEach(validateAdditionalFormField);
          }
          return customFields;
        } catch (x) {
          if ("object" === typeof x && null !== x && "function" === typeof x.then)
            throw x;
        }
      }
      return null;
    }
    function pushFormActionAttribute(target, resumableState, renderState, formAction, formEncType, formMethod, formTarget, name) {
      var formData = null;
      if ("function" === typeof formAction) {
        var customFields = getCustomFormFields(resumableState, formAction);
        null !== customFields ? (name = customFields.name, formAction = customFields.action || "", formEncType = customFields.encType, formMethod = customFields.method, formTarget = customFields.target, formData = customFields.data) : (target.push(" ", "formAction", '="', actionJavaScriptURL, '"'), formTarget = formMethod = formEncType = formAction = name = null, injectFormReplayingRuntime(resumableState, renderState));
      }
      null != name && pushAttribute(target, "name", name);
      null != formAction && pushAttribute(target, "formAction", formAction);
      null != formEncType && pushAttribute(target, "formEncType", formEncType);
      null != formMethod && pushAttribute(target, "formMethod", formMethod);
      null != formTarget && pushAttribute(target, "formTarget", formTarget);
      return formData;
    }
    function pushAttribute(target, name, value) {
      switch (name) {
        case "className":
          pushStringAttribute(target, "class", value);
          break;
        case "tabIndex":
          pushStringAttribute(target, "tabindex", value);
          break;
        case "dir":
        case "role":
        case "viewBox":
        case "width":
        case "height":
          pushStringAttribute(target, name, value);
          break;
        case "style":
          pushStyleAttribute(target, value);
          break;
        case "src":
        case "href":
          if ("" === value) break;
        case "action":
        case "formAction":
          if (null == value || "function" === typeof value || "symbol" === typeof value || "boolean" === typeof value)
            break;
          value = sanitizeURL("" + value);
          target.push(" ", name, '="', escapeTextForBrowser(value), '"');
          break;
        case "defaultValue":
        case "defaultChecked":
        case "innerHTML":
        case "suppressContentEditableWarning":
        case "suppressHydrationWarning":
        case "ref":
          break;
        case "autoFocus":
        case "multiple":
        case "muted":
          pushBooleanAttribute(target, name.toLowerCase(), value);
          break;
        case "xlinkHref":
          if ("function" === typeof value || "symbol" === typeof value || "boolean" === typeof value)
            break;
          value = sanitizeURL("" + value);
          target.push(" ", "xlink:href", '="', escapeTextForBrowser(value), '"');
          break;
        case "contentEditable":
        case "spellCheck":
        case "draggable":
        case "value":
        case "autoReverse":
        case "externalResourcesRequired":
        case "focusable":
        case "preserveAlpha":
          "function" !== typeof value && "symbol" !== typeof value && target.push(" ", name, '="', escapeTextForBrowser(value), '"');
          break;
        case "inert":
        case "allowFullScreen":
        case "async":
        case "autoPlay":
        case "controls":
        case "default":
        case "defer":
        case "disabled":
        case "disablePictureInPicture":
        case "disableRemotePlayback":
        case "formNoValidate":
        case "hidden":
        case "loop":
        case "noModule":
        case "noValidate":
        case "open":
        case "playsInline":
        case "readOnly":
        case "required":
        case "reversed":
        case "scoped":
        case "seamless":
        case "itemScope":
          value && "function" !== typeof value && "symbol" !== typeof value && target.push(" ", name, '=""');
          break;
        case "capture":
        case "download":
          true === value ? target.push(" ", name, '=""') : false !== value && "function" !== typeof value && "symbol" !== typeof value && target.push(" ", name, '="', escapeTextForBrowser(value), '"');
          break;
        case "cols":
        case "rows":
        case "size":
        case "span":
          "function" !== typeof value && "symbol" !== typeof value && !isNaN(value) && 1 <= value && target.push(" ", name, '="', escapeTextForBrowser(value), '"');
          break;
        case "rowSpan":
        case "start":
          "function" === typeof value || "symbol" === typeof value || isNaN(value) || target.push(" ", name, '="', escapeTextForBrowser(value), '"');
          break;
        case "xlinkActuate":
          pushStringAttribute(target, "xlink:actuate", value);
          break;
        case "xlinkArcrole":
          pushStringAttribute(target, "xlink:arcrole", value);
          break;
        case "xlinkRole":
          pushStringAttribute(target, "xlink:role", value);
          break;
        case "xlinkShow":
          pushStringAttribute(target, "xlink:show", value);
          break;
        case "xlinkTitle":
          pushStringAttribute(target, "xlink:title", value);
          break;
        case "xlinkType":
          pushStringAttribute(target, "xlink:type", value);
          break;
        case "xmlBase":
          pushStringAttribute(target, "xml:base", value);
          break;
        case "xmlLang":
          pushStringAttribute(target, "xml:lang", value);
          break;
        case "xmlSpace":
          pushStringAttribute(target, "xml:space", value);
          break;
        default:
          if (!(2 < name.length) || "o" !== name[0] && "O" !== name[0] || "n" !== name[1] && "N" !== name[1]) {
            if (name = aliases.get(name) || name, isAttributeNameSafe(name)) {
              switch (typeof value) {
                case "function":
                case "symbol":
                  return;
                case "boolean":
                  var prefix$8 = name.toLowerCase().slice(0, 5);
                  if ("data-" !== prefix$8 && "aria-" !== prefix$8) return;
              }
              target.push(" ", name, '="', escapeTextForBrowser(value), '"');
            }
          }
      }
    }
    function pushInnerHTML(target, innerHTML, children) {
      if (null != innerHTML) {
        if (null != children) throw Error(formatProdErrorMessage(60));
        if ("object" !== typeof innerHTML || !("__html" in innerHTML))
          throw Error(formatProdErrorMessage(61));
        innerHTML = innerHTML.__html;
        null !== innerHTML && void 0 !== innerHTML && target.push("" + innerHTML);
      }
    }
    function flattenOptionChildren(children) {
      var content = "";
      React2.Children.forEach(children, function(child) {
        null != child && (content += child);
      });
      return content;
    }
    function injectFormReplayingRuntime(resumableState, renderState) {
      if (0 === (resumableState.instructions & 16)) {
        resumableState.instructions |= 16;
        var preamble = renderState.preamble, bootstrapChunks = renderState.bootstrapChunks;
        (preamble.htmlChunks || preamble.headChunks) && 0 === bootstrapChunks.length ? (bootstrapChunks.push(renderState.startInlineScript), pushCompletedShellIdAttribute(bootstrapChunks, resumableState), bootstrapChunks.push(
          ">",
          `addEventListener("submit",function(a){if(!a.defaultPrevented){var c=a.target,d=a.submitter,e=c.action,b=d;if(d){var f=d.getAttribute("formAction");null!=f&&(e=f,b=null)}"javascript:throw new Error('React form unexpectedly submitted.')"===e&&(a.preventDefault(),b?(a=document.createElement("input"),a.name=b.name,a.value=b.value,b.parentNode.insertBefore(a,b),b=new FormData(c),a.parentNode.removeChild(a)):b=new FormData(c),a=c.ownerDocument||c,(a.$$reactFormReplay=a.$$reactFormReplay||[]).push(c,d,b))}});`,
          "<\/script>"
        )) : bootstrapChunks.unshift(
          renderState.startInlineScript,
          ">",
          `addEventListener("submit",function(a){if(!a.defaultPrevented){var c=a.target,d=a.submitter,e=c.action,b=d;if(d){var f=d.getAttribute("formAction");null!=f&&(e=f,b=null)}"javascript:throw new Error('React form unexpectedly submitted.')"===e&&(a.preventDefault(),b?(a=document.createElement("input"),a.name=b.name,a.value=b.value,b.parentNode.insertBefore(a,b),b=new FormData(c),a.parentNode.removeChild(a)):b=new FormData(c),a=c.ownerDocument||c,(a.$$reactFormReplay=a.$$reactFormReplay||[]).push(c,d,b))}});`,
          "<\/script>"
        );
      }
    }
    function pushLinkImpl(target, props) {
      target.push(startChunkForTag("link"));
      for (var propKey in props)
        if (hasOwnProperty.call(props, propKey)) {
          var propValue = props[propKey];
          if (null != propValue)
            switch (propKey) {
              case "children":
              case "dangerouslySetInnerHTML":
                throw Error(formatProdErrorMessage(399, "link"));
              default:
                pushAttribute(target, propKey, propValue);
            }
        }
      target.push("/>");
      return null;
    }
    var styleRegex = /(<\/|<)(s)(tyle)/gi;
    function styleReplacer(match, prefix2, s, suffix2) {
      return "" + prefix2 + ("s" === s ? "\\73 " : "\\53 ") + suffix2;
    }
    function pushSelfClosing(target, props, tag) {
      target.push(startChunkForTag(tag));
      for (var propKey in props)
        if (hasOwnProperty.call(props, propKey)) {
          var propValue = props[propKey];
          if (null != propValue)
            switch (propKey) {
              case "children":
              case "dangerouslySetInnerHTML":
                throw Error(formatProdErrorMessage(399, tag));
              default:
                pushAttribute(target, propKey, propValue);
            }
        }
      target.push("/>");
      return null;
    }
    function pushTitleImpl(target, props) {
      target.push(startChunkForTag("title"));
      var children = null, innerHTML = null, propKey;
      for (propKey in props)
        if (hasOwnProperty.call(props, propKey)) {
          var propValue = props[propKey];
          if (null != propValue)
            switch (propKey) {
              case "children":
                children = propValue;
                break;
              case "dangerouslySetInnerHTML":
                innerHTML = propValue;
                break;
              default:
                pushAttribute(target, propKey, propValue);
            }
        }
      target.push(">");
      props = Array.isArray(children) ? 2 > children.length ? children[0] : null : children;
      "function" !== typeof props && "symbol" !== typeof props && null !== props && void 0 !== props && target.push(escapeTextForBrowser("" + props));
      pushInnerHTML(target, innerHTML, children);
      target.push(endChunkForTag("title"));
      return null;
    }
    function pushScriptImpl(target, props) {
      target.push(startChunkForTag("script"));
      var children = null, innerHTML = null, propKey;
      for (propKey in props)
        if (hasOwnProperty.call(props, propKey)) {
          var propValue = props[propKey];
          if (null != propValue)
            switch (propKey) {
              case "children":
                children = propValue;
                break;
              case "dangerouslySetInnerHTML":
                innerHTML = propValue;
                break;
              default:
                pushAttribute(target, propKey, propValue);
            }
        }
      target.push(">");
      pushInnerHTML(target, innerHTML, children);
      "string" === typeof children && target.push(("" + children).replace(scriptRegex, scriptReplacer));
      target.push(endChunkForTag("script"));
      return null;
    }
    function pushStartSingletonElement(target, props, tag) {
      target.push(startChunkForTag(tag));
      var innerHTML = tag = null, propKey;
      for (propKey in props)
        if (hasOwnProperty.call(props, propKey)) {
          var propValue = props[propKey];
          if (null != propValue)
            switch (propKey) {
              case "children":
                tag = propValue;
                break;
              case "dangerouslySetInnerHTML":
                innerHTML = propValue;
                break;
              default:
                pushAttribute(target, propKey, propValue);
            }
        }
      target.push(">");
      pushInnerHTML(target, innerHTML, tag);
      return tag;
    }
    function pushStartGenericElement(target, props, tag) {
      target.push(startChunkForTag(tag));
      var innerHTML = tag = null, propKey;
      for (propKey in props)
        if (hasOwnProperty.call(props, propKey)) {
          var propValue = props[propKey];
          if (null != propValue)
            switch (propKey) {
              case "children":
                tag = propValue;
                break;
              case "dangerouslySetInnerHTML":
                innerHTML = propValue;
                break;
              default:
                pushAttribute(target, propKey, propValue);
            }
        }
      target.push(">");
      pushInnerHTML(target, innerHTML, tag);
      return "string" === typeof tag ? (target.push(escapeTextForBrowser(tag)), null) : tag;
    }
    var VALID_TAG_REGEX = /^[a-zA-Z][a-zA-Z:_\.\-\d]*$/;
    var validatedTagCache = /* @__PURE__ */ new Map();
    function startChunkForTag(tag) {
      var tagStartChunk = validatedTagCache.get(tag);
      if (void 0 === tagStartChunk) {
        if (!VALID_TAG_REGEX.test(tag))
          throw Error(formatProdErrorMessage(65, tag));
        tagStartChunk = "<" + tag;
        validatedTagCache.set(tag, tagStartChunk);
      }
      return tagStartChunk;
    }
    function pushStartInstance(target$jscomp$0, type, props, resumableState, renderState, preambleState, hoistableState, formatContext, textEmbedded) {
      switch (type) {
        case "div":
        case "span":
        case "svg":
        case "path":
          break;
        case "a":
          target$jscomp$0.push(startChunkForTag("a"));
          var children = null, innerHTML = null, propKey;
          for (propKey in props)
            if (hasOwnProperty.call(props, propKey)) {
              var propValue = props[propKey];
              if (null != propValue)
                switch (propKey) {
                  case "children":
                    children = propValue;
                    break;
                  case "dangerouslySetInnerHTML":
                    innerHTML = propValue;
                    break;
                  case "href":
                    "" === propValue ? pushStringAttribute(target$jscomp$0, "href", "") : pushAttribute(target$jscomp$0, propKey, propValue);
                    break;
                  default:
                    pushAttribute(target$jscomp$0, propKey, propValue);
                }
            }
          target$jscomp$0.push(">");
          pushInnerHTML(target$jscomp$0, innerHTML, children);
          if ("string" === typeof children) {
            target$jscomp$0.push(escapeTextForBrowser(children));
            var JSCompiler_inline_result = null;
          } else JSCompiler_inline_result = children;
          return JSCompiler_inline_result;
        case "g":
        case "p":
        case "li":
          break;
        case "select":
          target$jscomp$0.push(startChunkForTag("select"));
          var children$jscomp$0 = null, innerHTML$jscomp$0 = null, propKey$jscomp$0;
          for (propKey$jscomp$0 in props)
            if (hasOwnProperty.call(props, propKey$jscomp$0)) {
              var propValue$jscomp$0 = props[propKey$jscomp$0];
              if (null != propValue$jscomp$0)
                switch (propKey$jscomp$0) {
                  case "children":
                    children$jscomp$0 = propValue$jscomp$0;
                    break;
                  case "dangerouslySetInnerHTML":
                    innerHTML$jscomp$0 = propValue$jscomp$0;
                    break;
                  case "defaultValue":
                  case "value":
                    break;
                  default:
                    pushAttribute(
                      target$jscomp$0,
                      propKey$jscomp$0,
                      propValue$jscomp$0
                    );
                }
            }
          target$jscomp$0.push(">");
          pushInnerHTML(target$jscomp$0, innerHTML$jscomp$0, children$jscomp$0);
          return children$jscomp$0;
        case "option":
          var selectedValue = formatContext.selectedValue;
          target$jscomp$0.push(startChunkForTag("option"));
          var children$jscomp$1 = null, value = null, selected = null, innerHTML$jscomp$1 = null, propKey$jscomp$1;
          for (propKey$jscomp$1 in props)
            if (hasOwnProperty.call(props, propKey$jscomp$1)) {
              var propValue$jscomp$1 = props[propKey$jscomp$1];
              if (null != propValue$jscomp$1)
                switch (propKey$jscomp$1) {
                  case "children":
                    children$jscomp$1 = propValue$jscomp$1;
                    break;
                  case "selected":
                    selected = propValue$jscomp$1;
                    break;
                  case "dangerouslySetInnerHTML":
                    innerHTML$jscomp$1 = propValue$jscomp$1;
                    break;
                  case "value":
                    value = propValue$jscomp$1;
                  default:
                    pushAttribute(
                      target$jscomp$0,
                      propKey$jscomp$1,
                      propValue$jscomp$1
                    );
                }
            }
          if (null != selectedValue) {
            var stringValue = null !== value ? "" + value : flattenOptionChildren(children$jscomp$1);
            if (isArrayImpl(selectedValue))
              for (var i = 0; i < selectedValue.length; i++) {
                if ("" + selectedValue[i] === stringValue) {
                  target$jscomp$0.push(' selected=""');
                  break;
                }
              }
            else
              "" + selectedValue === stringValue && target$jscomp$0.push(' selected=""');
          } else selected && target$jscomp$0.push(' selected=""');
          target$jscomp$0.push(">");
          pushInnerHTML(target$jscomp$0, innerHTML$jscomp$1, children$jscomp$1);
          return children$jscomp$1;
        case "textarea":
          target$jscomp$0.push(startChunkForTag("textarea"));
          var value$jscomp$0 = null, defaultValue = null, children$jscomp$2 = null, propKey$jscomp$2;
          for (propKey$jscomp$2 in props)
            if (hasOwnProperty.call(props, propKey$jscomp$2)) {
              var propValue$jscomp$2 = props[propKey$jscomp$2];
              if (null != propValue$jscomp$2)
                switch (propKey$jscomp$2) {
                  case "children":
                    children$jscomp$2 = propValue$jscomp$2;
                    break;
                  case "value":
                    value$jscomp$0 = propValue$jscomp$2;
                    break;
                  case "defaultValue":
                    defaultValue = propValue$jscomp$2;
                    break;
                  case "dangerouslySetInnerHTML":
                    throw Error(formatProdErrorMessage(91));
                  default:
                    pushAttribute(
                      target$jscomp$0,
                      propKey$jscomp$2,
                      propValue$jscomp$2
                    );
                }
            }
          null === value$jscomp$0 && null !== defaultValue && (value$jscomp$0 = defaultValue);
          target$jscomp$0.push(">");
          if (null != children$jscomp$2) {
            if (null != value$jscomp$0) throw Error(formatProdErrorMessage(92));
            if (isArrayImpl(children$jscomp$2)) {
              if (1 < children$jscomp$2.length)
                throw Error(formatProdErrorMessage(93));
              value$jscomp$0 = "" + children$jscomp$2[0];
            }
            value$jscomp$0 = "" + children$jscomp$2;
          }
          "string" === typeof value$jscomp$0 && "\n" === value$jscomp$0[0] && target$jscomp$0.push("\n");
          null !== value$jscomp$0 && target$jscomp$0.push(escapeTextForBrowser("" + value$jscomp$0));
          return null;
        case "input":
          target$jscomp$0.push(startChunkForTag("input"));
          var name = null, formAction = null, formEncType = null, formMethod = null, formTarget = null, value$jscomp$1 = null, defaultValue$jscomp$0 = null, checked = null, defaultChecked = null, propKey$jscomp$3;
          for (propKey$jscomp$3 in props)
            if (hasOwnProperty.call(props, propKey$jscomp$3)) {
              var propValue$jscomp$3 = props[propKey$jscomp$3];
              if (null != propValue$jscomp$3)
                switch (propKey$jscomp$3) {
                  case "children":
                  case "dangerouslySetInnerHTML":
                    throw Error(formatProdErrorMessage(399, "input"));
                  case "name":
                    name = propValue$jscomp$3;
                    break;
                  case "formAction":
                    formAction = propValue$jscomp$3;
                    break;
                  case "formEncType":
                    formEncType = propValue$jscomp$3;
                    break;
                  case "formMethod":
                    formMethod = propValue$jscomp$3;
                    break;
                  case "formTarget":
                    formTarget = propValue$jscomp$3;
                    break;
                  case "defaultChecked":
                    defaultChecked = propValue$jscomp$3;
                    break;
                  case "defaultValue":
                    defaultValue$jscomp$0 = propValue$jscomp$3;
                    break;
                  case "checked":
                    checked = propValue$jscomp$3;
                    break;
                  case "value":
                    value$jscomp$1 = propValue$jscomp$3;
                    break;
                  default:
                    pushAttribute(
                      target$jscomp$0,
                      propKey$jscomp$3,
                      propValue$jscomp$3
                    );
                }
            }
          var formData = pushFormActionAttribute(
            target$jscomp$0,
            resumableState,
            renderState,
            formAction,
            formEncType,
            formMethod,
            formTarget,
            name
          );
          null !== checked ? pushBooleanAttribute(target$jscomp$0, "checked", checked) : null !== defaultChecked && pushBooleanAttribute(target$jscomp$0, "checked", defaultChecked);
          null !== value$jscomp$1 ? pushAttribute(target$jscomp$0, "value", value$jscomp$1) : null !== defaultValue$jscomp$0 && pushAttribute(target$jscomp$0, "value", defaultValue$jscomp$0);
          target$jscomp$0.push("/>");
          null != formData && formData.forEach(pushAdditionalFormField, target$jscomp$0);
          return null;
        case "button":
          target$jscomp$0.push(startChunkForTag("button"));
          var children$jscomp$3 = null, innerHTML$jscomp$2 = null, name$jscomp$0 = null, formAction$jscomp$0 = null, formEncType$jscomp$0 = null, formMethod$jscomp$0 = null, formTarget$jscomp$0 = null, propKey$jscomp$4;
          for (propKey$jscomp$4 in props)
            if (hasOwnProperty.call(props, propKey$jscomp$4)) {
              var propValue$jscomp$4 = props[propKey$jscomp$4];
              if (null != propValue$jscomp$4)
                switch (propKey$jscomp$4) {
                  case "children":
                    children$jscomp$3 = propValue$jscomp$4;
                    break;
                  case "dangerouslySetInnerHTML":
                    innerHTML$jscomp$2 = propValue$jscomp$4;
                    break;
                  case "name":
                    name$jscomp$0 = propValue$jscomp$4;
                    break;
                  case "formAction":
                    formAction$jscomp$0 = propValue$jscomp$4;
                    break;
                  case "formEncType":
                    formEncType$jscomp$0 = propValue$jscomp$4;
                    break;
                  case "formMethod":
                    formMethod$jscomp$0 = propValue$jscomp$4;
                    break;
                  case "formTarget":
                    formTarget$jscomp$0 = propValue$jscomp$4;
                    break;
                  default:
                    pushAttribute(
                      target$jscomp$0,
                      propKey$jscomp$4,
                      propValue$jscomp$4
                    );
                }
            }
          var formData$jscomp$0 = pushFormActionAttribute(
            target$jscomp$0,
            resumableState,
            renderState,
            formAction$jscomp$0,
            formEncType$jscomp$0,
            formMethod$jscomp$0,
            formTarget$jscomp$0,
            name$jscomp$0
          );
          target$jscomp$0.push(">");
          null != formData$jscomp$0 && formData$jscomp$0.forEach(pushAdditionalFormField, target$jscomp$0);
          pushInnerHTML(target$jscomp$0, innerHTML$jscomp$2, children$jscomp$3);
          if ("string" === typeof children$jscomp$3) {
            target$jscomp$0.push(escapeTextForBrowser(children$jscomp$3));
            var JSCompiler_inline_result$jscomp$0 = null;
          } else JSCompiler_inline_result$jscomp$0 = children$jscomp$3;
          return JSCompiler_inline_result$jscomp$0;
        case "form":
          target$jscomp$0.push(startChunkForTag("form"));
          var children$jscomp$4 = null, innerHTML$jscomp$3 = null, formAction$jscomp$1 = null, formEncType$jscomp$1 = null, formMethod$jscomp$1 = null, formTarget$jscomp$1 = null, propKey$jscomp$5;
          for (propKey$jscomp$5 in props)
            if (hasOwnProperty.call(props, propKey$jscomp$5)) {
              var propValue$jscomp$5 = props[propKey$jscomp$5];
              if (null != propValue$jscomp$5)
                switch (propKey$jscomp$5) {
                  case "children":
                    children$jscomp$4 = propValue$jscomp$5;
                    break;
                  case "dangerouslySetInnerHTML":
                    innerHTML$jscomp$3 = propValue$jscomp$5;
                    break;
                  case "action":
                    formAction$jscomp$1 = propValue$jscomp$5;
                    break;
                  case "encType":
                    formEncType$jscomp$1 = propValue$jscomp$5;
                    break;
                  case "method":
                    formMethod$jscomp$1 = propValue$jscomp$5;
                    break;
                  case "target":
                    formTarget$jscomp$1 = propValue$jscomp$5;
                    break;
                  default:
                    pushAttribute(
                      target$jscomp$0,
                      propKey$jscomp$5,
                      propValue$jscomp$5
                    );
                }
            }
          var formData$jscomp$1 = null, formActionName = null;
          if ("function" === typeof formAction$jscomp$1) {
            var customFields = getCustomFormFields(
              resumableState,
              formAction$jscomp$1
            );
            null !== customFields ? (formAction$jscomp$1 = customFields.action || "", formEncType$jscomp$1 = customFields.encType, formMethod$jscomp$1 = customFields.method, formTarget$jscomp$1 = customFields.target, formData$jscomp$1 = customFields.data, formActionName = customFields.name) : (target$jscomp$0.push(
              " ",
              "action",
              '="',
              actionJavaScriptURL,
              '"'
            ), formTarget$jscomp$1 = formMethod$jscomp$1 = formEncType$jscomp$1 = formAction$jscomp$1 = null, injectFormReplayingRuntime(resumableState, renderState));
          }
          null != formAction$jscomp$1 && pushAttribute(target$jscomp$0, "action", formAction$jscomp$1);
          null != formEncType$jscomp$1 && pushAttribute(target$jscomp$0, "encType", formEncType$jscomp$1);
          null != formMethod$jscomp$1 && pushAttribute(target$jscomp$0, "method", formMethod$jscomp$1);
          null != formTarget$jscomp$1 && pushAttribute(target$jscomp$0, "target", formTarget$jscomp$1);
          target$jscomp$0.push(">");
          null !== formActionName && (target$jscomp$0.push('<input type="hidden"'), pushStringAttribute(target$jscomp$0, "name", formActionName), target$jscomp$0.push("/>"), null != formData$jscomp$1 && formData$jscomp$1.forEach(pushAdditionalFormField, target$jscomp$0));
          pushInnerHTML(target$jscomp$0, innerHTML$jscomp$3, children$jscomp$4);
          if ("string" === typeof children$jscomp$4) {
            target$jscomp$0.push(escapeTextForBrowser(children$jscomp$4));
            var JSCompiler_inline_result$jscomp$1 = null;
          } else JSCompiler_inline_result$jscomp$1 = children$jscomp$4;
          return JSCompiler_inline_result$jscomp$1;
        case "menuitem":
          target$jscomp$0.push(startChunkForTag("menuitem"));
          for (var propKey$jscomp$6 in props)
            if (hasOwnProperty.call(props, propKey$jscomp$6)) {
              var propValue$jscomp$6 = props[propKey$jscomp$6];
              if (null != propValue$jscomp$6)
                switch (propKey$jscomp$6) {
                  case "children":
                  case "dangerouslySetInnerHTML":
                    throw Error(formatProdErrorMessage(400));
                  default:
                    pushAttribute(
                      target$jscomp$0,
                      propKey$jscomp$6,
                      propValue$jscomp$6
                    );
                }
            }
          target$jscomp$0.push(">");
          return null;
        case "object":
          target$jscomp$0.push(startChunkForTag("object"));
          var children$jscomp$5 = null, innerHTML$jscomp$4 = null, propKey$jscomp$7;
          for (propKey$jscomp$7 in props)
            if (hasOwnProperty.call(props, propKey$jscomp$7)) {
              var propValue$jscomp$7 = props[propKey$jscomp$7];
              if (null != propValue$jscomp$7)
                switch (propKey$jscomp$7) {
                  case "children":
                    children$jscomp$5 = propValue$jscomp$7;
                    break;
                  case "dangerouslySetInnerHTML":
                    innerHTML$jscomp$4 = propValue$jscomp$7;
                    break;
                  case "data":
                    var sanitizedValue = sanitizeURL("" + propValue$jscomp$7);
                    if ("" === sanitizedValue) break;
                    target$jscomp$0.push(
                      " ",
                      "data",
                      '="',
                      escapeTextForBrowser(sanitizedValue),
                      '"'
                    );
                    break;
                  default:
                    pushAttribute(
                      target$jscomp$0,
                      propKey$jscomp$7,
                      propValue$jscomp$7
                    );
                }
            }
          target$jscomp$0.push(">");
          pushInnerHTML(target$jscomp$0, innerHTML$jscomp$4, children$jscomp$5);
          if ("string" === typeof children$jscomp$5) {
            target$jscomp$0.push(escapeTextForBrowser(children$jscomp$5));
            var JSCompiler_inline_result$jscomp$2 = null;
          } else JSCompiler_inline_result$jscomp$2 = children$jscomp$5;
          return JSCompiler_inline_result$jscomp$2;
        case "title":
          var noscriptTagInScope = formatContext.tagScope & 1, isFallback = formatContext.tagScope & 4;
          if (4 === formatContext.insertionMode || noscriptTagInScope || null != props.itemProp)
            var JSCompiler_inline_result$jscomp$3 = pushTitleImpl(
              target$jscomp$0,
              props
            );
          else
            isFallback ? JSCompiler_inline_result$jscomp$3 = null : (pushTitleImpl(renderState.hoistableChunks, props), JSCompiler_inline_result$jscomp$3 = void 0);
          return JSCompiler_inline_result$jscomp$3;
        case "link":
          var noscriptTagInScope$jscomp$0 = formatContext.tagScope & 1, isFallback$jscomp$0 = formatContext.tagScope & 4, rel = props.rel, href = props.href, precedence = props.precedence;
          if (4 === formatContext.insertionMode || noscriptTagInScope$jscomp$0 || null != props.itemProp || "string" !== typeof rel || "string" !== typeof href || "" === href) {
            pushLinkImpl(target$jscomp$0, props);
            var JSCompiler_inline_result$jscomp$4 = null;
          } else if ("stylesheet" === props.rel)
            if ("string" !== typeof precedence || null != props.disabled || props.onLoad || props.onError)
              JSCompiler_inline_result$jscomp$4 = pushLinkImpl(
                target$jscomp$0,
                props
              );
            else {
              var styleQueue = renderState.styles.get(precedence), resourceState = resumableState.styleResources.hasOwnProperty(href) ? resumableState.styleResources[href] : void 0;
              if (null !== resourceState) {
                resumableState.styleResources[href] = null;
                styleQueue || (styleQueue = {
                  precedence: escapeTextForBrowser(precedence),
                  rules: [],
                  hrefs: [],
                  sheets: /* @__PURE__ */ new Map()
                }, renderState.styles.set(precedence, styleQueue));
                var resource = {
                  state: 0,
                  props: assign({}, props, {
                    "data-precedence": props.precedence,
                    precedence: null
                  })
                };
                if (resourceState) {
                  2 === resourceState.length && adoptPreloadCredentials(resource.props, resourceState);
                  var preloadResource = renderState.preloads.stylesheets.get(href);
                  preloadResource && 0 < preloadResource.length ? preloadResource.length = 0 : resource.state = 1;
                }
                styleQueue.sheets.set(href, resource);
                hoistableState && hoistableState.stylesheets.add(resource);
              } else if (styleQueue) {
                var resource$9 = styleQueue.sheets.get(href);
                resource$9 && hoistableState && hoistableState.stylesheets.add(resource$9);
              }
              textEmbedded && target$jscomp$0.push("<!-- -->");
              JSCompiler_inline_result$jscomp$4 = null;
            }
          else
            props.onLoad || props.onError ? JSCompiler_inline_result$jscomp$4 = pushLinkImpl(
              target$jscomp$0,
              props
            ) : (textEmbedded && target$jscomp$0.push("<!-- -->"), JSCompiler_inline_result$jscomp$4 = isFallback$jscomp$0 ? null : pushLinkImpl(renderState.hoistableChunks, props));
          return JSCompiler_inline_result$jscomp$4;
        case "script":
          var noscriptTagInScope$jscomp$1 = formatContext.tagScope & 1, asyncProp = props.async;
          if ("string" !== typeof props.src || !props.src || !asyncProp || "function" === typeof asyncProp || "symbol" === typeof asyncProp || props.onLoad || props.onError || 4 === formatContext.insertionMode || noscriptTagInScope$jscomp$1 || null != props.itemProp)
            var JSCompiler_inline_result$jscomp$5 = pushScriptImpl(
              target$jscomp$0,
              props
            );
          else {
            var key = props.src;
            if ("module" === props.type) {
              var resources = resumableState.moduleScriptResources;
              var preloads = renderState.preloads.moduleScripts;
            } else
              resources = resumableState.scriptResources, preloads = renderState.preloads.scripts;
            var resourceState$jscomp$0 = resources.hasOwnProperty(key) ? resources[key] : void 0;
            if (null !== resourceState$jscomp$0) {
              resources[key] = null;
              var scriptProps = props;
              if (resourceState$jscomp$0) {
                2 === resourceState$jscomp$0.length && (scriptProps = assign({}, props), adoptPreloadCredentials(scriptProps, resourceState$jscomp$0));
                var preloadResource$jscomp$0 = preloads.get(key);
                preloadResource$jscomp$0 && (preloadResource$jscomp$0.length = 0);
              }
              var resource$jscomp$0 = [];
              renderState.scripts.add(resource$jscomp$0);
              pushScriptImpl(resource$jscomp$0, scriptProps);
            }
            textEmbedded && target$jscomp$0.push("<!-- -->");
            JSCompiler_inline_result$jscomp$5 = null;
          }
          return JSCompiler_inline_result$jscomp$5;
        case "style":
          var noscriptTagInScope$jscomp$2 = formatContext.tagScope & 1, precedence$jscomp$0 = props.precedence, href$jscomp$0 = props.href, nonce = props.nonce;
          if (4 === formatContext.insertionMode || noscriptTagInScope$jscomp$2 || null != props.itemProp || "string" !== typeof precedence$jscomp$0 || "string" !== typeof href$jscomp$0 || "" === href$jscomp$0) {
            target$jscomp$0.push(startChunkForTag("style"));
            var children$jscomp$6 = null, innerHTML$jscomp$5 = null, propKey$jscomp$8;
            for (propKey$jscomp$8 in props)
              if (hasOwnProperty.call(props, propKey$jscomp$8)) {
                var propValue$jscomp$8 = props[propKey$jscomp$8];
                if (null != propValue$jscomp$8)
                  switch (propKey$jscomp$8) {
                    case "children":
                      children$jscomp$6 = propValue$jscomp$8;
                      break;
                    case "dangerouslySetInnerHTML":
                      innerHTML$jscomp$5 = propValue$jscomp$8;
                      break;
                    default:
                      pushAttribute(
                        target$jscomp$0,
                        propKey$jscomp$8,
                        propValue$jscomp$8
                      );
                  }
              }
            target$jscomp$0.push(">");
            var child = Array.isArray(children$jscomp$6) ? 2 > children$jscomp$6.length ? children$jscomp$6[0] : null : children$jscomp$6;
            "function" !== typeof child && "symbol" !== typeof child && null !== child && void 0 !== child && target$jscomp$0.push(("" + child).replace(styleRegex, styleReplacer));
            pushInnerHTML(target$jscomp$0, innerHTML$jscomp$5, children$jscomp$6);
            target$jscomp$0.push(endChunkForTag("style"));
            var JSCompiler_inline_result$jscomp$6 = null;
          } else {
            var styleQueue$jscomp$0 = renderState.styles.get(precedence$jscomp$0);
            if (null !== (resumableState.styleResources.hasOwnProperty(href$jscomp$0) ? resumableState.styleResources[href$jscomp$0] : void 0)) {
              resumableState.styleResources[href$jscomp$0] = null;
              styleQueue$jscomp$0 || (styleQueue$jscomp$0 = {
                precedence: escapeTextForBrowser(precedence$jscomp$0),
                rules: [],
                hrefs: [],
                sheets: /* @__PURE__ */ new Map()
              }, renderState.styles.set(precedence$jscomp$0, styleQueue$jscomp$0));
              var nonceStyle = renderState.nonce.style;
              if (!nonceStyle || nonceStyle === nonce) {
                styleQueue$jscomp$0.hrefs.push(escapeTextForBrowser(href$jscomp$0));
                var target = styleQueue$jscomp$0.rules, children$jscomp$7 = null, innerHTML$jscomp$6 = null, propKey$jscomp$9;
                for (propKey$jscomp$9 in props)
                  if (hasOwnProperty.call(props, propKey$jscomp$9)) {
                    var propValue$jscomp$9 = props[propKey$jscomp$9];
                    if (null != propValue$jscomp$9)
                      switch (propKey$jscomp$9) {
                        case "children":
                          children$jscomp$7 = propValue$jscomp$9;
                          break;
                        case "dangerouslySetInnerHTML":
                          innerHTML$jscomp$6 = propValue$jscomp$9;
                      }
                  }
                var child$jscomp$0 = Array.isArray(children$jscomp$7) ? 2 > children$jscomp$7.length ? children$jscomp$7[0] : null : children$jscomp$7;
                "function" !== typeof child$jscomp$0 && "symbol" !== typeof child$jscomp$0 && null !== child$jscomp$0 && void 0 !== child$jscomp$0 && target.push(
                  ("" + child$jscomp$0).replace(styleRegex, styleReplacer)
                );
                pushInnerHTML(target, innerHTML$jscomp$6, children$jscomp$7);
              }
            }
            styleQueue$jscomp$0 && hoistableState && hoistableState.styles.add(styleQueue$jscomp$0);
            textEmbedded && target$jscomp$0.push("<!-- -->");
            JSCompiler_inline_result$jscomp$6 = void 0;
          }
          return JSCompiler_inline_result$jscomp$6;
        case "meta":
          var noscriptTagInScope$jscomp$3 = formatContext.tagScope & 1, isFallback$jscomp$1 = formatContext.tagScope & 4;
          if (4 === formatContext.insertionMode || noscriptTagInScope$jscomp$3 || null != props.itemProp)
            var JSCompiler_inline_result$jscomp$7 = pushSelfClosing(
              target$jscomp$0,
              props,
              "meta"
            );
          else
            textEmbedded && target$jscomp$0.push("<!-- -->"), JSCompiler_inline_result$jscomp$7 = isFallback$jscomp$1 ? null : "string" === typeof props.charSet ? pushSelfClosing(renderState.charsetChunks, props, "meta") : "viewport" === props.name ? pushSelfClosing(renderState.viewportChunks, props, "meta") : pushSelfClosing(renderState.hoistableChunks, props, "meta");
          return JSCompiler_inline_result$jscomp$7;
        case "listing":
        case "pre":
          target$jscomp$0.push(startChunkForTag(type));
          var children$jscomp$8 = null, innerHTML$jscomp$7 = null, propKey$jscomp$10;
          for (propKey$jscomp$10 in props)
            if (hasOwnProperty.call(props, propKey$jscomp$10)) {
              var propValue$jscomp$10 = props[propKey$jscomp$10];
              if (null != propValue$jscomp$10)
                switch (propKey$jscomp$10) {
                  case "children":
                    children$jscomp$8 = propValue$jscomp$10;
                    break;
                  case "dangerouslySetInnerHTML":
                    innerHTML$jscomp$7 = propValue$jscomp$10;
                    break;
                  default:
                    pushAttribute(
                      target$jscomp$0,
                      propKey$jscomp$10,
                      propValue$jscomp$10
                    );
                }
            }
          target$jscomp$0.push(">");
          if (null != innerHTML$jscomp$7) {
            if (null != children$jscomp$8) throw Error(formatProdErrorMessage(60));
            if ("object" !== typeof innerHTML$jscomp$7 || !("__html" in innerHTML$jscomp$7))
              throw Error(formatProdErrorMessage(61));
            var html = innerHTML$jscomp$7.__html;
            null !== html && void 0 !== html && ("string" === typeof html && 0 < html.length && "\n" === html[0] ? target$jscomp$0.push("\n", html) : target$jscomp$0.push("" + html));
          }
          "string" === typeof children$jscomp$8 && "\n" === children$jscomp$8[0] && target$jscomp$0.push("\n");
          return children$jscomp$8;
        case "img":
          var pictureOrNoScriptTagInScope = formatContext.tagScope & 3, src = props.src, srcSet = props.srcSet;
          if (!("lazy" === props.loading || !src && !srcSet || "string" !== typeof src && null != src || "string" !== typeof srcSet && null != srcSet || "low" === props.fetchPriority || pictureOrNoScriptTagInScope) && ("string" !== typeof src || ":" !== src[4] || "d" !== src[0] && "D" !== src[0] || "a" !== src[1] && "A" !== src[1] || "t" !== src[2] && "T" !== src[2] || "a" !== src[3] && "A" !== src[3]) && ("string" !== typeof srcSet || ":" !== srcSet[4] || "d" !== srcSet[0] && "D" !== srcSet[0] || "a" !== srcSet[1] && "A" !== srcSet[1] || "t" !== srcSet[2] && "T" !== srcSet[2] || "a" !== srcSet[3] && "A" !== srcSet[3])) {
            null !== hoistableState && formatContext.tagScope & 64 && (hoistableState.suspenseyImages = true);
            var sizes = "string" === typeof props.sizes ? props.sizes : void 0, key$jscomp$0 = srcSet ? srcSet + "\n" + (sizes || "") : src, promotablePreloads = renderState.preloads.images, resource$jscomp$1 = promotablePreloads.get(key$jscomp$0);
            if (resource$jscomp$1) {
              if ("high" === props.fetchPriority || 10 > renderState.highImagePreloads.size)
                promotablePreloads.delete(key$jscomp$0), renderState.highImagePreloads.add(resource$jscomp$1);
            } else if (!resumableState.imageResources.hasOwnProperty(key$jscomp$0)) {
              resumableState.imageResources[key$jscomp$0] = PRELOAD_NO_CREDS;
              var input = props.crossOrigin;
              var JSCompiler_inline_result$jscomp$8 = "string" === typeof input ? "use-credentials" === input ? input : "" : void 0;
              var headers = renderState.headers, header;
              headers && 0 < headers.remainingCapacity && "string" !== typeof props.srcSet && ("high" === props.fetchPriority || 500 > headers.highImagePreloads.length) && (header = getPreloadAsHeader(src, "image", {
                imageSrcSet: props.srcSet,
                imageSizes: props.sizes,
                crossOrigin: JSCompiler_inline_result$jscomp$8,
                integrity: props.integrity,
                nonce: props.nonce,
                type: props.type,
                fetchPriority: props.fetchPriority,
                referrerPolicy: props.refererPolicy
              }), 0 <= (headers.remainingCapacity -= header.length + 2)) ? (renderState.resets.image[key$jscomp$0] = PRELOAD_NO_CREDS, headers.highImagePreloads && (headers.highImagePreloads += ", "), headers.highImagePreloads += header) : (resource$jscomp$1 = [], pushLinkImpl(resource$jscomp$1, {
                rel: "preload",
                as: "image",
                href: srcSet ? void 0 : src,
                imageSrcSet: srcSet,
                imageSizes: sizes,
                crossOrigin: JSCompiler_inline_result$jscomp$8,
                integrity: props.integrity,
                type: props.type,
                fetchPriority: props.fetchPriority,
                referrerPolicy: props.referrerPolicy
              }), "high" === props.fetchPriority || 10 > renderState.highImagePreloads.size ? renderState.highImagePreloads.add(resource$jscomp$1) : (renderState.bulkPreloads.add(resource$jscomp$1), promotablePreloads.set(key$jscomp$0, resource$jscomp$1)));
            }
          }
          return pushSelfClosing(target$jscomp$0, props, "img");
        case "base":
        case "area":
        case "br":
        case "col":
        case "embed":
        case "hr":
        case "keygen":
        case "param":
        case "source":
        case "track":
        case "wbr":
          return pushSelfClosing(target$jscomp$0, props, type);
        case "annotation-xml":
        case "color-profile":
        case "font-face":
        case "font-face-src":
        case "font-face-uri":
        case "font-face-format":
        case "font-face-name":
        case "missing-glyph":
          break;
        case "head":
          if (2 > formatContext.insertionMode) {
            var preamble = preambleState || renderState.preamble;
            if (preamble.headChunks)
              throw Error(formatProdErrorMessage(545, "`<head>`"));
            null !== preambleState && target$jscomp$0.push("<!--head-->");
            preamble.headChunks = [];
            var JSCompiler_inline_result$jscomp$9 = pushStartSingletonElement(
              preamble.headChunks,
              props,
              "head"
            );
          } else
            JSCompiler_inline_result$jscomp$9 = pushStartGenericElement(
              target$jscomp$0,
              props,
              "head"
            );
          return JSCompiler_inline_result$jscomp$9;
        case "body":
          if (2 > formatContext.insertionMode) {
            var preamble$jscomp$0 = preambleState || renderState.preamble;
            if (preamble$jscomp$0.bodyChunks)
              throw Error(formatProdErrorMessage(545, "`<body>`"));
            null !== preambleState && target$jscomp$0.push("<!--body-->");
            preamble$jscomp$0.bodyChunks = [];
            var JSCompiler_inline_result$jscomp$10 = pushStartSingletonElement(
              preamble$jscomp$0.bodyChunks,
              props,
              "body"
            );
          } else
            JSCompiler_inline_result$jscomp$10 = pushStartGenericElement(
              target$jscomp$0,
              props,
              "body"
            );
          return JSCompiler_inline_result$jscomp$10;
        case "html":
          if (0 === formatContext.insertionMode) {
            var preamble$jscomp$1 = preambleState || renderState.preamble;
            if (preamble$jscomp$1.htmlChunks)
              throw Error(formatProdErrorMessage(545, "`<html>`"));
            null !== preambleState && target$jscomp$0.push("<!--html-->");
            preamble$jscomp$1.htmlChunks = [""];
            var JSCompiler_inline_result$jscomp$11 = pushStartSingletonElement(
              preamble$jscomp$1.htmlChunks,
              props,
              "html"
            );
          } else
            JSCompiler_inline_result$jscomp$11 = pushStartGenericElement(
              target$jscomp$0,
              props,
              "html"
            );
          return JSCompiler_inline_result$jscomp$11;
        default:
          if (-1 !== type.indexOf("-")) {
            target$jscomp$0.push(startChunkForTag(type));
            var children$jscomp$9 = null, innerHTML$jscomp$8 = null, propKey$jscomp$11;
            for (propKey$jscomp$11 in props)
              if (hasOwnProperty.call(props, propKey$jscomp$11)) {
                var propValue$jscomp$11 = props[propKey$jscomp$11];
                if (null != propValue$jscomp$11) {
                  var attributeName = propKey$jscomp$11;
                  switch (propKey$jscomp$11) {
                    case "children":
                      children$jscomp$9 = propValue$jscomp$11;
                      break;
                    case "dangerouslySetInnerHTML":
                      innerHTML$jscomp$8 = propValue$jscomp$11;
                      break;
                    case "style":
                      pushStyleAttribute(target$jscomp$0, propValue$jscomp$11);
                      break;
                    case "suppressContentEditableWarning":
                    case "suppressHydrationWarning":
                    case "ref":
                      break;
                    case "className":
                      attributeName = "class";
                    default:
                      if (isAttributeNameSafe(propKey$jscomp$11) && "function" !== typeof propValue$jscomp$11 && "symbol" !== typeof propValue$jscomp$11 && false !== propValue$jscomp$11) {
                        if (true === propValue$jscomp$11) propValue$jscomp$11 = "";
                        else if ("object" === typeof propValue$jscomp$11) continue;
                        target$jscomp$0.push(
                          " ",
                          attributeName,
                          '="',
                          escapeTextForBrowser(propValue$jscomp$11),
                          '"'
                        );
                      }
                  }
                }
              }
            target$jscomp$0.push(">");
            pushInnerHTML(target$jscomp$0, innerHTML$jscomp$8, children$jscomp$9);
            return children$jscomp$9;
          }
      }
      return pushStartGenericElement(target$jscomp$0, props, type);
    }
    var endTagCache = /* @__PURE__ */ new Map();
    function endChunkForTag(tag) {
      var chunk = endTagCache.get(tag);
      void 0 === chunk && (chunk = "</" + tag + ">", endTagCache.set(tag, chunk));
      return chunk;
    }
    function hoistPreambleState(renderState, preambleState) {
      renderState = renderState.preamble;
      null === renderState.htmlChunks && preambleState.htmlChunks && (renderState.htmlChunks = preambleState.htmlChunks);
      null === renderState.headChunks && preambleState.headChunks && (renderState.headChunks = preambleState.headChunks);
      null === renderState.bodyChunks && preambleState.bodyChunks && (renderState.bodyChunks = preambleState.bodyChunks);
    }
    function writeBootstrap(destination, renderState) {
      renderState = renderState.bootstrapChunks;
      for (var i = 0; i < renderState.length - 1; i++)
        destination.push(renderState[i]);
      return i < renderState.length ? (i = renderState[i], renderState.length = 0, destination.push(i)) : true;
    }
    function writeStartPendingSuspenseBoundary(destination, renderState, id) {
      destination.push('<!--$?--><template id="');
      if (null === id) throw Error(formatProdErrorMessage(395));
      destination.push(renderState.boundaryPrefix);
      renderState = id.toString(16);
      destination.push(renderState);
      return destination.push('"></template>');
    }
    function writeStartSegment(destination, renderState, formatContext, id) {
      switch (formatContext.insertionMode) {
        case 0:
        case 1:
        case 3:
        case 2:
          return destination.push('<div hidden id="'), destination.push(renderState.segmentPrefix), renderState = id.toString(16), destination.push(renderState), destination.push('">');
        case 4:
          return destination.push('<svg aria-hidden="true" style="display:none" id="'), destination.push(renderState.segmentPrefix), renderState = id.toString(16), destination.push(renderState), destination.push('">');
        case 5:
          return destination.push('<math aria-hidden="true" style="display:none" id="'), destination.push(renderState.segmentPrefix), renderState = id.toString(16), destination.push(renderState), destination.push('">');
        case 6:
          return destination.push('<table hidden id="'), destination.push(renderState.segmentPrefix), renderState = id.toString(16), destination.push(renderState), destination.push('">');
        case 7:
          return destination.push('<table hidden><tbody id="'), destination.push(renderState.segmentPrefix), renderState = id.toString(16), destination.push(renderState), destination.push('">');
        case 8:
          return destination.push('<table hidden><tr id="'), destination.push(renderState.segmentPrefix), renderState = id.toString(16), destination.push(renderState), destination.push('">');
        case 9:
          return destination.push('<table hidden><colgroup id="'), destination.push(renderState.segmentPrefix), renderState = id.toString(16), destination.push(renderState), destination.push('">');
        default:
          throw Error(formatProdErrorMessage(397));
      }
    }
    function writeEndSegment(destination, formatContext) {
      switch (formatContext.insertionMode) {
        case 0:
        case 1:
        case 3:
        case 2:
          return destination.push("</div>");
        case 4:
          return destination.push("</svg>");
        case 5:
          return destination.push("</math>");
        case 6:
          return destination.push("</table>");
        case 7:
          return destination.push("</tbody></table>");
        case 8:
          return destination.push("</tr></table>");
        case 9:
          return destination.push("</colgroup></table>");
        default:
          throw Error(formatProdErrorMessage(397));
      }
    }
    var regexForJSStringsInInstructionScripts = /[<\u2028\u2029]/g;
    function escapeJSStringsForInstructionScripts(input) {
      return JSON.stringify(input).replace(
        regexForJSStringsInInstructionScripts,
        function(match) {
          switch (match) {
            case "<":
              return "\\u003c";
            case "\u2028":
              return "\\u2028";
            case "\u2029":
              return "\\u2029";
            default:
              throw Error(
                "escapeJSStringsForInstructionScripts encountered a match it does not know how to replace. this means the match regex and the replacement characters are no longer in sync. This is a bug in React"
              );
          }
        }
      );
    }
    var regexForJSStringsInScripts = /[&><\u2028\u2029]/g;
    function escapeJSObjectForInstructionScripts(input) {
      return JSON.stringify(input).replace(
        regexForJSStringsInScripts,
        function(match) {
          switch (match) {
            case "&":
              return "\\u0026";
            case ">":
              return "\\u003e";
            case "<":
              return "\\u003c";
            case "\u2028":
              return "\\u2028";
            case "\u2029":
              return "\\u2029";
            default:
              throw Error(
                "escapeJSObjectForInstructionScripts encountered a match it does not know how to replace. this means the match regex and the replacement characters are no longer in sync. This is a bug in React"
              );
          }
        }
      );
    }
    var currentlyRenderingBoundaryHasStylesToHoist = false;
    var destinationHasCapacity = true;
    function flushStyleTagsLateForBoundary(styleQueue) {
      var rules = styleQueue.rules, hrefs = styleQueue.hrefs, i = 0;
      if (hrefs.length) {
        this.push(currentlyFlushingRenderState.startInlineStyle);
        this.push(' media="not all" data-precedence="');
        this.push(styleQueue.precedence);
        for (this.push('" data-href="'); i < hrefs.length - 1; i++)
          this.push(hrefs[i]), this.push(" ");
        this.push(hrefs[i]);
        this.push('">');
        for (i = 0; i < rules.length; i++) this.push(rules[i]);
        destinationHasCapacity = this.push("</style>");
        currentlyRenderingBoundaryHasStylesToHoist = true;
        rules.length = 0;
        hrefs.length = 0;
      }
    }
    function hasStylesToHoist(stylesheet) {
      return 2 !== stylesheet.state ? currentlyRenderingBoundaryHasStylesToHoist = true : false;
    }
    function writeHoistablesForBoundary(destination, hoistableState, renderState) {
      currentlyRenderingBoundaryHasStylesToHoist = false;
      destinationHasCapacity = true;
      currentlyFlushingRenderState = renderState;
      hoistableState.styles.forEach(flushStyleTagsLateForBoundary, destination);
      currentlyFlushingRenderState = null;
      hoistableState.stylesheets.forEach(hasStylesToHoist);
      currentlyRenderingBoundaryHasStylesToHoist && (renderState.stylesToHoist = true);
      return destinationHasCapacity;
    }
    function flushResource(resource) {
      for (var i = 0; i < resource.length; i++) this.push(resource[i]);
      resource.length = 0;
    }
    var stylesheetFlushingQueue = [];
    function flushStyleInPreamble(stylesheet) {
      pushLinkImpl(stylesheetFlushingQueue, stylesheet.props);
      for (var i = 0; i < stylesheetFlushingQueue.length; i++)
        this.push(stylesheetFlushingQueue[i]);
      stylesheetFlushingQueue.length = 0;
      stylesheet.state = 2;
    }
    function flushStylesInPreamble(styleQueue) {
      var hasStylesheets = 0 < styleQueue.sheets.size;
      styleQueue.sheets.forEach(flushStyleInPreamble, this);
      styleQueue.sheets.clear();
      var rules = styleQueue.rules, hrefs = styleQueue.hrefs;
      if (!hasStylesheets || hrefs.length) {
        this.push(currentlyFlushingRenderState.startInlineStyle);
        this.push(' data-precedence="');
        this.push(styleQueue.precedence);
        styleQueue = 0;
        if (hrefs.length) {
          for (this.push('" data-href="'); styleQueue < hrefs.length - 1; styleQueue++)
            this.push(hrefs[styleQueue]), this.push(" ");
          this.push(hrefs[styleQueue]);
        }
        this.push('">');
        for (styleQueue = 0; styleQueue < rules.length; styleQueue++)
          this.push(rules[styleQueue]);
        this.push("</style>");
        rules.length = 0;
        hrefs.length = 0;
      }
    }
    function preloadLateStyle(stylesheet) {
      if (0 === stylesheet.state) {
        stylesheet.state = 1;
        var props = stylesheet.props;
        pushLinkImpl(stylesheetFlushingQueue, {
          rel: "preload",
          as: "style",
          href: stylesheet.props.href,
          crossOrigin: props.crossOrigin,
          fetchPriority: props.fetchPriority,
          integrity: props.integrity,
          media: props.media,
          hrefLang: props.hrefLang,
          referrerPolicy: props.referrerPolicy
        });
        for (stylesheet = 0; stylesheet < stylesheetFlushingQueue.length; stylesheet++)
          this.push(stylesheetFlushingQueue[stylesheet]);
        stylesheetFlushingQueue.length = 0;
      }
    }
    function preloadLateStyles(styleQueue) {
      styleQueue.sheets.forEach(preloadLateStyle, this);
      styleQueue.sheets.clear();
    }
    function pushCompletedShellIdAttribute(target, resumableState) {
      0 === (resumableState.instructions & 32) && (resumableState.instructions |= 32, target.push(
        ' id="',
        escapeTextForBrowser("_" + resumableState.idPrefix + "R_"),
        '"'
      ));
    }
    function writeStyleResourceDependenciesInJS(destination, hoistableState) {
      destination.push("[");
      var nextArrayOpenBrackChunk = "[";
      hoistableState.stylesheets.forEach(function(resource) {
        if (2 !== resource.state)
          if (3 === resource.state)
            destination.push(nextArrayOpenBrackChunk), resource = escapeJSObjectForInstructionScripts(
              "" + resource.props.href
            ), destination.push(resource), destination.push("]"), nextArrayOpenBrackChunk = ",[";
          else {
            destination.push(nextArrayOpenBrackChunk);
            var precedence = resource.props["data-precedence"], props = resource.props, coercedHref = sanitizeURL("" + resource.props.href);
            coercedHref = escapeJSObjectForInstructionScripts(coercedHref);
            destination.push(coercedHref);
            precedence = "" + precedence;
            destination.push(",");
            precedence = escapeJSObjectForInstructionScripts(precedence);
            destination.push(precedence);
            for (var propKey in props)
              if (hasOwnProperty.call(props, propKey) && (precedence = props[propKey], null != precedence))
                switch (propKey) {
                  case "href":
                  case "rel":
                  case "precedence":
                  case "data-precedence":
                    break;
                  case "children":
                  case "dangerouslySetInnerHTML":
                    throw Error(formatProdErrorMessage(399, "link"));
                  default:
                    writeStyleResourceAttributeInJS(
                      destination,
                      propKey,
                      precedence
                    );
                }
            destination.push("]");
            nextArrayOpenBrackChunk = ",[";
            resource.state = 3;
          }
      });
      destination.push("]");
    }
    function writeStyleResourceAttributeInJS(destination, name, value) {
      var attributeName = name.toLowerCase();
      switch (typeof value) {
        case "function":
        case "symbol":
          return;
      }
      switch (name) {
        case "innerHTML":
        case "dangerouslySetInnerHTML":
        case "suppressContentEditableWarning":
        case "suppressHydrationWarning":
        case "style":
        case "ref":
          return;
        case "className":
          attributeName = "class";
          name = "" + value;
          break;
        case "hidden":
          if (false === value) return;
          name = "";
          break;
        case "src":
        case "href":
          value = sanitizeURL(value);
          name = "" + value;
          break;
        default:
          if (2 < name.length && ("o" === name[0] || "O" === name[0]) && ("n" === name[1] || "N" === name[1]) || !isAttributeNameSafe(name))
            return;
          name = "" + value;
      }
      destination.push(",");
      attributeName = escapeJSObjectForInstructionScripts(attributeName);
      destination.push(attributeName);
      destination.push(",");
      attributeName = escapeJSObjectForInstructionScripts(name);
      destination.push(attributeName);
    }
    function createHoistableState() {
      return { styles: /* @__PURE__ */ new Set(), stylesheets: /* @__PURE__ */ new Set(), suspenseyImages: false };
    }
    function prefetchDNS(href) {
      var request = currentRequest ? currentRequest : null;
      if (request) {
        var resumableState = request.resumableState, renderState = request.renderState;
        if ("string" === typeof href && href) {
          if (!resumableState.dnsResources.hasOwnProperty(href)) {
            resumableState.dnsResources[href] = null;
            resumableState = renderState.headers;
            var header, JSCompiler_temp;
            if (JSCompiler_temp = resumableState && 0 < resumableState.remainingCapacity)
              JSCompiler_temp = (header = "<" + ("" + href).replace(
                regexForHrefInLinkHeaderURLContext,
                escapeHrefForLinkHeaderURLContextReplacer
              ) + ">; rel=dns-prefetch", 0 <= (resumableState.remainingCapacity -= header.length + 2));
            JSCompiler_temp ? (renderState.resets.dns[href] = null, resumableState.preconnects && (resumableState.preconnects += ", "), resumableState.preconnects += header) : (header = [], pushLinkImpl(header, { href, rel: "dns-prefetch" }), renderState.preconnects.add(header));
          }
          enqueueFlush(request);
        }
      } else previousDispatcher.D(href);
    }
    function preconnect(href, crossOrigin) {
      var request = currentRequest ? currentRequest : null;
      if (request) {
        var resumableState = request.resumableState, renderState = request.renderState;
        if ("string" === typeof href && href) {
          var bucket = "use-credentials" === crossOrigin ? "credentials" : "string" === typeof crossOrigin ? "anonymous" : "default";
          if (!resumableState.connectResources[bucket].hasOwnProperty(href)) {
            resumableState.connectResources[bucket][href] = null;
            resumableState = renderState.headers;
            var header, JSCompiler_temp;
            if (JSCompiler_temp = resumableState && 0 < resumableState.remainingCapacity) {
              JSCompiler_temp = "<" + ("" + href).replace(
                regexForHrefInLinkHeaderURLContext,
                escapeHrefForLinkHeaderURLContextReplacer
              ) + ">; rel=preconnect";
              if ("string" === typeof crossOrigin) {
                var escapedCrossOrigin = ("" + crossOrigin).replace(
                  regexForLinkHeaderQuotedParamValueContext,
                  escapeStringForLinkHeaderQuotedParamValueContextReplacer
                );
                JSCompiler_temp += '; crossorigin="' + escapedCrossOrigin + '"';
              }
              JSCompiler_temp = (header = JSCompiler_temp, 0 <= (resumableState.remainingCapacity -= header.length + 2));
            }
            JSCompiler_temp ? (renderState.resets.connect[bucket][href] = null, resumableState.preconnects && (resumableState.preconnects += ", "), resumableState.preconnects += header) : (bucket = [], pushLinkImpl(bucket, {
              rel: "preconnect",
              href,
              crossOrigin
            }), renderState.preconnects.add(bucket));
          }
          enqueueFlush(request);
        }
      } else previousDispatcher.C(href, crossOrigin);
    }
    function preload(href, as, options) {
      var request = currentRequest ? currentRequest : null;
      if (request) {
        var resumableState = request.resumableState, renderState = request.renderState;
        if (as && href) {
          switch (as) {
            case "image":
              if (options) {
                var imageSrcSet = options.imageSrcSet;
                var imageSizes = options.imageSizes;
                var fetchPriority = options.fetchPriority;
              }
              var key = imageSrcSet ? imageSrcSet + "\n" + (imageSizes || "") : href;
              if (resumableState.imageResources.hasOwnProperty(key)) return;
              resumableState.imageResources[key] = PRELOAD_NO_CREDS;
              resumableState = renderState.headers;
              var header;
              resumableState && 0 < resumableState.remainingCapacity && "string" !== typeof imageSrcSet && "high" === fetchPriority && (header = getPreloadAsHeader(href, as, options), 0 <= (resumableState.remainingCapacity -= header.length + 2)) ? (renderState.resets.image[key] = PRELOAD_NO_CREDS, resumableState.highImagePreloads && (resumableState.highImagePreloads += ", "), resumableState.highImagePreloads += header) : (resumableState = [], pushLinkImpl(
                resumableState,
                assign(
                  { rel: "preload", href: imageSrcSet ? void 0 : href, as },
                  options
                )
              ), "high" === fetchPriority ? renderState.highImagePreloads.add(resumableState) : (renderState.bulkPreloads.add(resumableState), renderState.preloads.images.set(key, resumableState)));
              break;
            case "style":
              if (resumableState.styleResources.hasOwnProperty(href)) return;
              imageSrcSet = [];
              pushLinkImpl(
                imageSrcSet,
                assign({ rel: "preload", href, as }, options)
              );
              resumableState.styleResources[href] = !options || "string" !== typeof options.crossOrigin && "string" !== typeof options.integrity ? PRELOAD_NO_CREDS : [options.crossOrigin, options.integrity];
              renderState.preloads.stylesheets.set(href, imageSrcSet);
              renderState.bulkPreloads.add(imageSrcSet);
              break;
            case "script":
              if (resumableState.scriptResources.hasOwnProperty(href)) return;
              imageSrcSet = [];
              renderState.preloads.scripts.set(href, imageSrcSet);
              renderState.bulkPreloads.add(imageSrcSet);
              pushLinkImpl(
                imageSrcSet,
                assign({ rel: "preload", href, as }, options)
              );
              resumableState.scriptResources[href] = !options || "string" !== typeof options.crossOrigin && "string" !== typeof options.integrity ? PRELOAD_NO_CREDS : [options.crossOrigin, options.integrity];
              break;
            default:
              if (resumableState.unknownResources.hasOwnProperty(as)) {
                if (imageSrcSet = resumableState.unknownResources[as], imageSrcSet.hasOwnProperty(href))
                  return;
              } else
                imageSrcSet = {}, resumableState.unknownResources[as] = imageSrcSet;
              imageSrcSet[href] = PRELOAD_NO_CREDS;
              if ((resumableState = renderState.headers) && 0 < resumableState.remainingCapacity && "font" === as && (key = getPreloadAsHeader(href, as, options), 0 <= (resumableState.remainingCapacity -= key.length + 2)))
                renderState.resets.font[href] = PRELOAD_NO_CREDS, resumableState.fontPreloads && (resumableState.fontPreloads += ", "), resumableState.fontPreloads += key;
              else
                switch (resumableState = [], href = assign({ rel: "preload", href, as }, options), pushLinkImpl(resumableState, href), as) {
                  case "font":
                    renderState.fontPreloads.add(resumableState);
                    break;
                  default:
                    renderState.bulkPreloads.add(resumableState);
                }
          }
          enqueueFlush(request);
        }
      } else previousDispatcher.L(href, as, options);
    }
    function preloadModule(href, options) {
      var request = currentRequest ? currentRequest : null;
      if (request) {
        var resumableState = request.resumableState, renderState = request.renderState;
        if (href) {
          var as = options && "string" === typeof options.as ? options.as : "script";
          switch (as) {
            case "script":
              if (resumableState.moduleScriptResources.hasOwnProperty(href)) return;
              as = [];
              resumableState.moduleScriptResources[href] = !options || "string" !== typeof options.crossOrigin && "string" !== typeof options.integrity ? PRELOAD_NO_CREDS : [options.crossOrigin, options.integrity];
              renderState.preloads.moduleScripts.set(href, as);
              break;
            default:
              if (resumableState.moduleUnknownResources.hasOwnProperty(as)) {
                var resources = resumableState.unknownResources[as];
                if (resources.hasOwnProperty(href)) return;
              } else
                resources = {}, resumableState.moduleUnknownResources[as] = resources;
              as = [];
              resources[href] = PRELOAD_NO_CREDS;
          }
          pushLinkImpl(as, assign({ rel: "modulepreload", href }, options));
          renderState.bulkPreloads.add(as);
          enqueueFlush(request);
        }
      } else previousDispatcher.m(href, options);
    }
    function preinitStyle(href, precedence, options) {
      var request = currentRequest ? currentRequest : null;
      if (request) {
        var resumableState = request.resumableState, renderState = request.renderState;
        if (href) {
          precedence = precedence || "default";
          var styleQueue = renderState.styles.get(precedence), resourceState = resumableState.styleResources.hasOwnProperty(href) ? resumableState.styleResources[href] : void 0;
          null !== resourceState && (resumableState.styleResources[href] = null, styleQueue || (styleQueue = {
            precedence: escapeTextForBrowser(precedence),
            rules: [],
            hrefs: [],
            sheets: /* @__PURE__ */ new Map()
          }, renderState.styles.set(precedence, styleQueue)), precedence = {
            state: 0,
            props: assign(
              { rel: "stylesheet", href, "data-precedence": precedence },
              options
            )
          }, resourceState && (2 === resourceState.length && adoptPreloadCredentials(precedence.props, resourceState), (renderState = renderState.preloads.stylesheets.get(href)) && 0 < renderState.length ? renderState.length = 0 : precedence.state = 1), styleQueue.sheets.set(href, precedence), enqueueFlush(request));
        }
      } else previousDispatcher.S(href, precedence, options);
    }
    function preinitScript(src, options) {
      var request = currentRequest ? currentRequest : null;
      if (request) {
        var resumableState = request.resumableState, renderState = request.renderState;
        if (src) {
          var resourceState = resumableState.scriptResources.hasOwnProperty(src) ? resumableState.scriptResources[src] : void 0;
          null !== resourceState && (resumableState.scriptResources[src] = null, options = assign({ src, async: true }, options), resourceState && (2 === resourceState.length && adoptPreloadCredentials(options, resourceState), src = renderState.preloads.scripts.get(src)) && (src.length = 0), src = [], renderState.scripts.add(src), pushScriptImpl(src, options), enqueueFlush(request));
        }
      } else previousDispatcher.X(src, options);
    }
    function preinitModuleScript(src, options) {
      var request = currentRequest ? currentRequest : null;
      if (request) {
        var resumableState = request.resumableState, renderState = request.renderState;
        if (src) {
          var resourceState = resumableState.moduleScriptResources.hasOwnProperty(
            src
          ) ? resumableState.moduleScriptResources[src] : void 0;
          null !== resourceState && (resumableState.moduleScriptResources[src] = null, options = assign({ src, type: "module", async: true }, options), resourceState && (2 === resourceState.length && adoptPreloadCredentials(options, resourceState), src = renderState.preloads.moduleScripts.get(src)) && (src.length = 0), src = [], renderState.scripts.add(src), pushScriptImpl(src, options), enqueueFlush(request));
        }
      } else previousDispatcher.M(src, options);
    }
    function adoptPreloadCredentials(target, preloadState) {
      null == target.crossOrigin && (target.crossOrigin = preloadState[0]);
      null == target.integrity && (target.integrity = preloadState[1]);
    }
    function getPreloadAsHeader(href, as, params) {
      href = ("" + href).replace(
        regexForHrefInLinkHeaderURLContext,
        escapeHrefForLinkHeaderURLContextReplacer
      );
      as = ("" + as).replace(
        regexForLinkHeaderQuotedParamValueContext,
        escapeStringForLinkHeaderQuotedParamValueContextReplacer
      );
      as = "<" + href + '>; rel=preload; as="' + as + '"';
      for (var paramName in params)
        hasOwnProperty.call(params, paramName) && (href = params[paramName], "string" === typeof href && (as += "; " + paramName.toLowerCase() + '="' + ("" + href).replace(
          regexForLinkHeaderQuotedParamValueContext,
          escapeStringForLinkHeaderQuotedParamValueContextReplacer
        ) + '"'));
      return as;
    }
    var regexForHrefInLinkHeaderURLContext = /[<>\r\n]/g;
    function escapeHrefForLinkHeaderURLContextReplacer(match) {
      switch (match) {
        case "<":
          return "%3C";
        case ">":
          return "%3E";
        case "\n":
          return "%0A";
        case "\r":
          return "%0D";
        default:
          throw Error(
            "escapeLinkHrefForHeaderContextReplacer encountered a match it does not know how to replace. this means the match regex and the replacement characters are no longer in sync. This is a bug in React"
          );
      }
    }
    var regexForLinkHeaderQuotedParamValueContext = /["';,\r\n]/g;
    function escapeStringForLinkHeaderQuotedParamValueContextReplacer(match) {
      switch (match) {
        case '"':
          return "%22";
        case "'":
          return "%27";
        case ";":
          return "%3B";
        case ",":
          return "%2C";
        case "\n":
          return "%0A";
        case "\r":
          return "%0D";
        default:
          throw Error(
            "escapeStringForLinkHeaderQuotedParamValueContextReplacer encountered a match it does not know how to replace. this means the match regex and the replacement characters are no longer in sync. This is a bug in React"
          );
      }
    }
    function hoistStyleQueueDependency(styleQueue) {
      this.styles.add(styleQueue);
    }
    function hoistStylesheetDependency(stylesheet) {
      this.stylesheets.add(stylesheet);
    }
    function hoistHoistables(parentState, childState) {
      childState.styles.forEach(hoistStyleQueueDependency, parentState);
      childState.stylesheets.forEach(hoistStylesheetDependency, parentState);
      childState.suspenseyImages && (parentState.suspenseyImages = true);
    }
    function createRenderState(resumableState, generateStaticMarkup) {
      var idPrefix = resumableState.idPrefix, bootstrapChunks = [], bootstrapScriptContent = resumableState.bootstrapScriptContent, bootstrapScripts = resumableState.bootstrapScripts, bootstrapModules = resumableState.bootstrapModules;
      void 0 !== bootstrapScriptContent && (bootstrapChunks.push("<script"), pushCompletedShellIdAttribute(bootstrapChunks, resumableState), bootstrapChunks.push(
        ">",
        ("" + bootstrapScriptContent).replace(scriptRegex, scriptReplacer),
        "<\/script>"
      ));
      bootstrapScriptContent = idPrefix + "P:";
      var JSCompiler_object_inline_segmentPrefix_1673 = idPrefix + "S:";
      idPrefix += "B:";
      var JSCompiler_object_inline_preconnects_1687 = /* @__PURE__ */ new Set(), JSCompiler_object_inline_fontPreloads_1688 = /* @__PURE__ */ new Set(), JSCompiler_object_inline_highImagePreloads_1689 = /* @__PURE__ */ new Set(), JSCompiler_object_inline_styles_1690 = /* @__PURE__ */ new Map(), JSCompiler_object_inline_bootstrapScripts_1691 = /* @__PURE__ */ new Set(), JSCompiler_object_inline_scripts_1692 = /* @__PURE__ */ new Set(), JSCompiler_object_inline_bulkPreloads_1693 = /* @__PURE__ */ new Set(), JSCompiler_object_inline_preloads_1694 = {
        images: /* @__PURE__ */ new Map(),
        stylesheets: /* @__PURE__ */ new Map(),
        scripts: /* @__PURE__ */ new Map(),
        moduleScripts: /* @__PURE__ */ new Map()
      };
      if (void 0 !== bootstrapScripts)
        for (var i = 0; i < bootstrapScripts.length; i++) {
          var scriptConfig = bootstrapScripts[i], src, crossOrigin = void 0, integrity = void 0, props = {
            rel: "preload",
            as: "script",
            fetchPriority: "low",
            nonce: void 0
          };
          "string" === typeof scriptConfig ? props.href = src = scriptConfig : (props.href = src = scriptConfig.src, props.integrity = integrity = "string" === typeof scriptConfig.integrity ? scriptConfig.integrity : void 0, props.crossOrigin = crossOrigin = "string" === typeof scriptConfig || null == scriptConfig.crossOrigin ? void 0 : "use-credentials" === scriptConfig.crossOrigin ? "use-credentials" : "");
          scriptConfig = resumableState;
          var href = src;
          scriptConfig.scriptResources[href] = null;
          scriptConfig.moduleScriptResources[href] = null;
          scriptConfig = [];
          pushLinkImpl(scriptConfig, props);
          JSCompiler_object_inline_bootstrapScripts_1691.add(scriptConfig);
          bootstrapChunks.push('<script src="', escapeTextForBrowser(src), '"');
          "string" === typeof integrity && bootstrapChunks.push(
            ' integrity="',
            escapeTextForBrowser(integrity),
            '"'
          );
          "string" === typeof crossOrigin && bootstrapChunks.push(
            ' crossorigin="',
            escapeTextForBrowser(crossOrigin),
            '"'
          );
          pushCompletedShellIdAttribute(bootstrapChunks, resumableState);
          bootstrapChunks.push(' async=""><\/script>');
        }
      if (void 0 !== bootstrapModules)
        for (bootstrapScripts = 0; bootstrapScripts < bootstrapModules.length; bootstrapScripts++)
          props = bootstrapModules[bootstrapScripts], crossOrigin = src = void 0, integrity = {
            rel: "modulepreload",
            fetchPriority: "low",
            nonce: void 0
          }, "string" === typeof props ? integrity.href = i = props : (integrity.href = i = props.src, integrity.integrity = crossOrigin = "string" === typeof props.integrity ? props.integrity : void 0, integrity.crossOrigin = src = "string" === typeof props || null == props.crossOrigin ? void 0 : "use-credentials" === props.crossOrigin ? "use-credentials" : ""), props = resumableState, scriptConfig = i, props.scriptResources[scriptConfig] = null, props.moduleScriptResources[scriptConfig] = null, props = [], pushLinkImpl(props, integrity), JSCompiler_object_inline_bootstrapScripts_1691.add(props), bootstrapChunks.push(
            '<script type="module" src="',
            escapeTextForBrowser(i),
            '"'
          ), "string" === typeof crossOrigin && bootstrapChunks.push(
            ' integrity="',
            escapeTextForBrowser(crossOrigin),
            '"'
          ), "string" === typeof src && bootstrapChunks.push(
            ' crossorigin="',
            escapeTextForBrowser(src),
            '"'
          ), pushCompletedShellIdAttribute(bootstrapChunks, resumableState), bootstrapChunks.push(' async=""><\/script>');
      return {
        placeholderPrefix: bootstrapScriptContent,
        segmentPrefix: JSCompiler_object_inline_segmentPrefix_1673,
        boundaryPrefix: idPrefix,
        startInlineScript: "<script",
        startInlineStyle: "<style",
        preamble: { htmlChunks: null, headChunks: null, bodyChunks: null },
        externalRuntimeScript: null,
        bootstrapChunks,
        importMapChunks: [],
        onHeaders: void 0,
        headers: null,
        resets: {
          font: {},
          dns: {},
          connect: { default: {}, anonymous: {}, credentials: {} },
          image: {},
          style: {}
        },
        charsetChunks: [],
        viewportChunks: [],
        hoistableChunks: [],
        preconnects: JSCompiler_object_inline_preconnects_1687,
        fontPreloads: JSCompiler_object_inline_fontPreloads_1688,
        highImagePreloads: JSCompiler_object_inline_highImagePreloads_1689,
        styles: JSCompiler_object_inline_styles_1690,
        bootstrapScripts: JSCompiler_object_inline_bootstrapScripts_1691,
        scripts: JSCompiler_object_inline_scripts_1692,
        bulkPreloads: JSCompiler_object_inline_bulkPreloads_1693,
        preloads: JSCompiler_object_inline_preloads_1694,
        nonce: { script: void 0, style: void 0 },
        stylesToHoist: false,
        generateStaticMarkup
      };
    }
    function pushTextInstance(target, text, renderState, textEmbedded) {
      if (renderState.generateStaticMarkup)
        return target.push(escapeTextForBrowser(text)), false;
      "" === text ? target = textEmbedded : (textEmbedded && target.push("<!-- -->"), target.push(escapeTextForBrowser(text)), target = true);
      return target;
    }
    function pushSegmentFinale(target, renderState, lastPushedText, textEmbedded) {
      renderState.generateStaticMarkup || lastPushedText && textEmbedded && target.push("<!-- -->");
    }
    var bind = Function.prototype.bind;
    var REACT_CLIENT_REFERENCE = Symbol.for("react.client.reference");
    function getComponentNameFromType(type) {
      if (null == type) return null;
      if ("function" === typeof type)
        return type.$$typeof === REACT_CLIENT_REFERENCE ? null : type.displayName || type.name || null;
      if ("string" === typeof type) return type;
      switch (type) {
        case REACT_FRAGMENT_TYPE:
          return "Fragment";
        case REACT_PROFILER_TYPE:
          return "Profiler";
        case REACT_STRICT_MODE_TYPE:
          return "StrictMode";
        case REACT_SUSPENSE_TYPE:
          return "Suspense";
        case REACT_SUSPENSE_LIST_TYPE:
          return "SuspenseList";
        case REACT_ACTIVITY_TYPE:
          return "Activity";
      }
      if ("object" === typeof type)
        switch (type.$$typeof) {
          case REACT_PORTAL_TYPE:
            return "Portal";
          case REACT_CONTEXT_TYPE:
            return type.displayName || "Context";
          case REACT_CONSUMER_TYPE:
            return (type._context.displayName || "Context") + ".Consumer";
          case REACT_FORWARD_REF_TYPE:
            var innerType = type.render;
            type = type.displayName;
            type || (type = innerType.displayName || innerType.name || "", type = "" !== type ? "ForwardRef(" + type + ")" : "ForwardRef");
            return type;
          case REACT_MEMO_TYPE:
            return innerType = type.displayName || null, null !== innerType ? innerType : getComponentNameFromType(type.type) || "Memo";
          case REACT_LAZY_TYPE:
            innerType = type._payload;
            type = type._init;
            try {
              return getComponentNameFromType(type(innerType));
            } catch (x) {
            }
        }
      return null;
    }
    var emptyContextObject = {};
    var currentActiveSnapshot = null;
    function popToNearestCommonAncestor(prev, next) {
      if (prev !== next) {
        prev.context._currentValue2 = prev.parentValue;
        prev = prev.parent;
        var parentNext = next.parent;
        if (null === prev) {
          if (null !== parentNext) throw Error(formatProdErrorMessage(401));
        } else {
          if (null === parentNext) throw Error(formatProdErrorMessage(401));
          popToNearestCommonAncestor(prev, parentNext);
        }
        next.context._currentValue2 = next.value;
      }
    }
    function popAllPrevious(prev) {
      prev.context._currentValue2 = prev.parentValue;
      prev = prev.parent;
      null !== prev && popAllPrevious(prev);
    }
    function pushAllNext(next) {
      var parentNext = next.parent;
      null !== parentNext && pushAllNext(parentNext);
      next.context._currentValue2 = next.value;
    }
    function popPreviousToCommonLevel(prev, next) {
      prev.context._currentValue2 = prev.parentValue;
      prev = prev.parent;
      if (null === prev) throw Error(formatProdErrorMessage(402));
      prev.depth === next.depth ? popToNearestCommonAncestor(prev, next) : popPreviousToCommonLevel(prev, next);
    }
    function popNextToCommonLevel(prev, next) {
      var parentNext = next.parent;
      if (null === parentNext) throw Error(formatProdErrorMessage(402));
      prev.depth === parentNext.depth ? popToNearestCommonAncestor(prev, parentNext) : popNextToCommonLevel(prev, parentNext);
      next.context._currentValue2 = next.value;
    }
    function switchContext(newSnapshot) {
      var prev = currentActiveSnapshot;
      prev !== newSnapshot && (null === prev ? pushAllNext(newSnapshot) : null === newSnapshot ? popAllPrevious(prev) : prev.depth === newSnapshot.depth ? popToNearestCommonAncestor(prev, newSnapshot) : prev.depth > newSnapshot.depth ? popPreviousToCommonLevel(prev, newSnapshot) : popNextToCommonLevel(prev, newSnapshot), currentActiveSnapshot = newSnapshot);
    }
    var classComponentUpdater = {
      enqueueSetState: function(inst, payload) {
        inst = inst._reactInternals;
        null !== inst.queue && inst.queue.push(payload);
      },
      enqueueReplaceState: function(inst, payload) {
        inst = inst._reactInternals;
        inst.replace = true;
        inst.queue = [payload];
      },
      enqueueForceUpdate: function() {
      }
    };
    var emptyTreeContext = { id: 1, overflow: "" };
    function pushTreeContext(baseContext, totalChildren, index) {
      var baseIdWithLeadingBit = baseContext.id;
      baseContext = baseContext.overflow;
      var baseLength = 32 - clz32(baseIdWithLeadingBit) - 1;
      baseIdWithLeadingBit &= ~(1 << baseLength);
      index += 1;
      var length = 32 - clz32(totalChildren) + baseLength;
      if (30 < length) {
        var numberOfOverflowBits = baseLength - baseLength % 5;
        length = (baseIdWithLeadingBit & (1 << numberOfOverflowBits) - 1).toString(32);
        baseIdWithLeadingBit >>= numberOfOverflowBits;
        baseLength -= numberOfOverflowBits;
        return {
          id: 1 << 32 - clz32(totalChildren) + baseLength | index << baseLength | baseIdWithLeadingBit,
          overflow: length + baseContext
        };
      }
      return {
        id: 1 << length | index << baseLength | baseIdWithLeadingBit,
        overflow: baseContext
      };
    }
    var clz32 = Math.clz32 ? Math.clz32 : clz32Fallback;
    var log = Math.log;
    var LN2 = Math.LN2;
    function clz32Fallback(x) {
      x >>>= 0;
      return 0 === x ? 32 : 31 - (log(x) / LN2 | 0) | 0;
    }
    function noop() {
    }
    var SuspenseException = Error(formatProdErrorMessage(460));
    function trackUsedThenable(thenableState2, thenable, index) {
      index = thenableState2[index];
      void 0 === index ? thenableState2.push(thenable) : index !== thenable && (thenable.then(noop, noop), thenable = index);
      switch (thenable.status) {
        case "fulfilled":
          return thenable.value;
        case "rejected":
          throw thenable.reason;
        default:
          "string" === typeof thenable.status ? thenable.then(noop, noop) : (thenableState2 = thenable, thenableState2.status = "pending", thenableState2.then(
            function(fulfilledValue) {
              if ("pending" === thenable.status) {
                var fulfilledThenable = thenable;
                fulfilledThenable.status = "fulfilled";
                fulfilledThenable.value = fulfilledValue;
              }
            },
            function(error) {
              if ("pending" === thenable.status) {
                var rejectedThenable = thenable;
                rejectedThenable.status = "rejected";
                rejectedThenable.reason = error;
              }
            }
          ));
          switch (thenable.status) {
            case "fulfilled":
              return thenable.value;
            case "rejected":
              throw thenable.reason;
          }
          suspendedThenable = thenable;
          throw SuspenseException;
      }
    }
    var suspendedThenable = null;
    function getSuspendedThenable() {
      if (null === suspendedThenable) throw Error(formatProdErrorMessage(459));
      var thenable = suspendedThenable;
      suspendedThenable = null;
      return thenable;
    }
    function is(x, y) {
      return x === y && (0 !== x || 1 / x === 1 / y) || x !== x && y !== y;
    }
    var objectIs = "function" === typeof Object.is ? Object.is : is;
    var currentlyRenderingComponent = null;
    var currentlyRenderingTask = null;
    var currentlyRenderingRequest = null;
    var currentlyRenderingKeyPath = null;
    var firstWorkInProgressHook = null;
    var workInProgressHook = null;
    var isReRender = false;
    var didScheduleRenderPhaseUpdate = false;
    var localIdCounter = 0;
    var actionStateCounter = 0;
    var actionStateMatchingIndex = -1;
    var thenableIndexCounter = 0;
    var thenableState = null;
    var renderPhaseUpdates = null;
    var numberOfReRenders = 0;
    function resolveCurrentlyRenderingComponent() {
      if (null === currentlyRenderingComponent)
        throw Error(formatProdErrorMessage(321));
      return currentlyRenderingComponent;
    }
    function createHook() {
      if (0 < numberOfReRenders) throw Error(formatProdErrorMessage(312));
      return { memoizedState: null, queue: null, next: null };
    }
    function createWorkInProgressHook() {
      null === workInProgressHook ? null === firstWorkInProgressHook ? (isReRender = false, firstWorkInProgressHook = workInProgressHook = createHook()) : (isReRender = true, workInProgressHook = firstWorkInProgressHook) : null === workInProgressHook.next ? (isReRender = false, workInProgressHook = workInProgressHook.next = createHook()) : (isReRender = true, workInProgressHook = workInProgressHook.next);
      return workInProgressHook;
    }
    function getThenableStateAfterSuspending() {
      var state = thenableState;
      thenableState = null;
      return state;
    }
    function resetHooksState() {
      currentlyRenderingKeyPath = currentlyRenderingRequest = currentlyRenderingTask = currentlyRenderingComponent = null;
      didScheduleRenderPhaseUpdate = false;
      firstWorkInProgressHook = null;
      numberOfReRenders = 0;
      workInProgressHook = renderPhaseUpdates = null;
    }
    function basicStateReducer(state, action) {
      return "function" === typeof action ? action(state) : action;
    }
    function useReducer(reducer, initialArg, init) {
      currentlyRenderingComponent = resolveCurrentlyRenderingComponent();
      workInProgressHook = createWorkInProgressHook();
      if (isReRender) {
        var queue = workInProgressHook.queue;
        initialArg = queue.dispatch;
        if (null !== renderPhaseUpdates && (init = renderPhaseUpdates.get(queue), void 0 !== init)) {
          renderPhaseUpdates.delete(queue);
          queue = workInProgressHook.memoizedState;
          do
            queue = reducer(queue, init.action), init = init.next;
          while (null !== init);
          workInProgressHook.memoizedState = queue;
          return [queue, initialArg];
        }
        return [workInProgressHook.memoizedState, initialArg];
      }
      reducer = reducer === basicStateReducer ? "function" === typeof initialArg ? initialArg() : initialArg : void 0 !== init ? init(initialArg) : initialArg;
      workInProgressHook.memoizedState = reducer;
      reducer = workInProgressHook.queue = { last: null, dispatch: null };
      reducer = reducer.dispatch = dispatchAction.bind(
        null,
        currentlyRenderingComponent,
        reducer
      );
      return [workInProgressHook.memoizedState, reducer];
    }
    function useMemo(nextCreate, deps) {
      currentlyRenderingComponent = resolveCurrentlyRenderingComponent();
      workInProgressHook = createWorkInProgressHook();
      deps = void 0 === deps ? null : deps;
      if (null !== workInProgressHook) {
        var prevState = workInProgressHook.memoizedState;
        if (null !== prevState && null !== deps) {
          var prevDeps = prevState[1];
          a: if (null === prevDeps) prevDeps = false;
          else {
            for (var i = 0; i < prevDeps.length && i < deps.length; i++)
              if (!objectIs(deps[i], prevDeps[i])) {
                prevDeps = false;
                break a;
              }
            prevDeps = true;
          }
          if (prevDeps) return prevState[0];
        }
      }
      nextCreate = nextCreate();
      workInProgressHook.memoizedState = [nextCreate, deps];
      return nextCreate;
    }
    function dispatchAction(componentIdentity, queue, action) {
      if (25 <= numberOfReRenders) throw Error(formatProdErrorMessage(301));
      if (componentIdentity === currentlyRenderingComponent)
        if (didScheduleRenderPhaseUpdate = true, componentIdentity = { action, next: null }, null === renderPhaseUpdates && (renderPhaseUpdates = /* @__PURE__ */ new Map()), action = renderPhaseUpdates.get(queue), void 0 === action)
          renderPhaseUpdates.set(queue, componentIdentity);
        else {
          for (queue = action; null !== queue.next; ) queue = queue.next;
          queue.next = componentIdentity;
        }
    }
    function throwOnUseEffectEventCall() {
      throw Error(formatProdErrorMessage(440));
    }
    function unsupportedStartTransition() {
      throw Error(formatProdErrorMessage(394));
    }
    function unsupportedSetOptimisticState() {
      throw Error(formatProdErrorMessage(479));
    }
    function useActionState(action, initialState, permalink) {
      resolveCurrentlyRenderingComponent();
      var actionStateHookIndex = actionStateCounter++, request = currentlyRenderingRequest;
      if ("function" === typeof action.$$FORM_ACTION) {
        var nextPostbackStateKey = null, componentKeyPath = currentlyRenderingKeyPath;
        request = request.formState;
        var isSignatureEqual = action.$$IS_SIGNATURE_EQUAL;
        if (null !== request && "function" === typeof isSignatureEqual) {
          var postbackKey = request[1];
          isSignatureEqual.call(action, request[2], request[3]) && (nextPostbackStateKey = void 0 !== permalink ? "p" + permalink : "k" + murmurhash3_32_gc(
            JSON.stringify([componentKeyPath, null, actionStateHookIndex]),
            0
          ), postbackKey === nextPostbackStateKey && (actionStateMatchingIndex = actionStateHookIndex, initialState = request[0]));
        }
        var boundAction = action.bind(null, initialState);
        action = function(payload) {
          boundAction(payload);
        };
        "function" === typeof boundAction.$$FORM_ACTION && (action.$$FORM_ACTION = function(prefix2) {
          prefix2 = boundAction.$$FORM_ACTION(prefix2);
          void 0 !== permalink && (permalink += "", prefix2.action = permalink);
          var formData = prefix2.data;
          formData && (null === nextPostbackStateKey && (nextPostbackStateKey = void 0 !== permalink ? "p" + permalink : "k" + murmurhash3_32_gc(
            JSON.stringify([
              componentKeyPath,
              null,
              actionStateHookIndex
            ]),
            0
          )), formData.append("$ACTION_KEY", nextPostbackStateKey));
          return prefix2;
        });
        return [initialState, action, false];
      }
      var boundAction$22 = action.bind(null, initialState);
      return [
        initialState,
        function(payload) {
          boundAction$22(payload);
        },
        false
      ];
    }
    function unwrapThenable(thenable) {
      var index = thenableIndexCounter;
      thenableIndexCounter += 1;
      null === thenableState && (thenableState = []);
      return trackUsedThenable(thenableState, thenable, index);
    }
    function unsupportedRefresh() {
      throw Error(formatProdErrorMessage(393));
    }
    var HooksDispatcher = {
      readContext: function(context) {
        return context._currentValue2;
      },
      use: function(usable) {
        if (null !== usable && "object" === typeof usable) {
          if ("function" === typeof usable.then) return unwrapThenable(usable);
          if (usable.$$typeof === REACT_CONTEXT_TYPE)
            return usable._currentValue2;
        }
        throw Error(formatProdErrorMessage(438, String(usable)));
      },
      useContext: function(context) {
        resolveCurrentlyRenderingComponent();
        return context._currentValue2;
      },
      useMemo,
      useReducer,
      useRef: function(initialValue) {
        currentlyRenderingComponent = resolveCurrentlyRenderingComponent();
        workInProgressHook = createWorkInProgressHook();
        var previousRef = workInProgressHook.memoizedState;
        return null === previousRef ? (initialValue = { current: initialValue }, workInProgressHook.memoizedState = initialValue) : previousRef;
      },
      useState: function(initialState) {
        return useReducer(basicStateReducer, initialState);
      },
      useInsertionEffect: noop,
      useLayoutEffect: noop,
      useCallback: function(callback, deps) {
        return useMemo(function() {
          return callback;
        }, deps);
      },
      useImperativeHandle: noop,
      useEffect: noop,
      useDebugValue: noop,
      useDeferredValue: function(value, initialValue) {
        resolveCurrentlyRenderingComponent();
        return void 0 !== initialValue ? initialValue : value;
      },
      useTransition: function() {
        resolveCurrentlyRenderingComponent();
        return [false, unsupportedStartTransition];
      },
      useId: function() {
        var JSCompiler_inline_result = currentlyRenderingTask.treeContext;
        var overflow = JSCompiler_inline_result.overflow;
        JSCompiler_inline_result = JSCompiler_inline_result.id;
        JSCompiler_inline_result = (JSCompiler_inline_result & ~(1 << 32 - clz32(JSCompiler_inline_result) - 1)).toString(32) + overflow;
        var resumableState = currentResumableState;
        if (null === resumableState) throw Error(formatProdErrorMessage(404));
        overflow = localIdCounter++;
        JSCompiler_inline_result = "_" + resumableState.idPrefix + "R_" + JSCompiler_inline_result;
        0 < overflow && (JSCompiler_inline_result += "H" + overflow.toString(32));
        return JSCompiler_inline_result + "_";
      },
      useSyncExternalStore: function(subscribe, getSnapshot, getServerSnapshot) {
        if (void 0 === getServerSnapshot)
          throw Error(formatProdErrorMessage(407));
        return getServerSnapshot();
      },
      useOptimistic: function(passthrough) {
        resolveCurrentlyRenderingComponent();
        return [passthrough, unsupportedSetOptimisticState];
      },
      useActionState,
      useFormState: useActionState,
      useHostTransitionStatus: function() {
        resolveCurrentlyRenderingComponent();
        return sharedNotPendingObject;
      },
      useMemoCache: function(size) {
        for (var data = Array(size), i = 0; i < size; i++)
          data[i] = REACT_MEMO_CACHE_SENTINEL;
        return data;
      },
      useCacheRefresh: function() {
        return unsupportedRefresh;
      },
      useEffectEvent: function() {
        return throwOnUseEffectEventCall;
      }
    };
    var currentResumableState = null;
    var DefaultAsyncDispatcher = {
      getCacheForType: function() {
        throw Error(formatProdErrorMessage(248));
      },
      cacheSignal: function() {
        throw Error(formatProdErrorMessage(248));
      }
    };
    var prefix;
    var suffix;
    function describeBuiltInComponentFrame(name) {
      if (void 0 === prefix)
        try {
          throw Error();
        } catch (x) {
          var match = x.stack.trim().match(/\n( *(at )?)/);
          prefix = match && match[1] || "";
          suffix = -1 < x.stack.indexOf("\n    at") ? " (<anonymous>)" : -1 < x.stack.indexOf("@") ? "@unknown:0:0" : "";
        }
      return "\n" + prefix + name + suffix;
    }
    var reentry = false;
    function describeNativeComponentFrame(fn, construct) {
      if (!fn || reentry) return "";
      reentry = true;
      var previousPrepareStackTrace = Error.prepareStackTrace;
      Error.prepareStackTrace = void 0;
      try {
        var RunInRootFrame = {
          DetermineComponentFrameRoot: function() {
            try {
              if (construct) {
                var Fake = function() {
                  throw Error();
                };
                Object.defineProperty(Fake.prototype, "props", {
                  set: function() {
                    throw Error();
                  }
                });
                if ("object" === typeof Reflect && Reflect.construct) {
                  try {
                    Reflect.construct(Fake, []);
                  } catch (x) {
                    var control = x;
                  }
                  Reflect.construct(fn, [], Fake);
                } else {
                  try {
                    Fake.call();
                  } catch (x$24) {
                    control = x$24;
                  }
                  fn.call(Fake.prototype);
                }
              } else {
                try {
                  throw Error();
                } catch (x$25) {
                  control = x$25;
                }
                (Fake = fn()) && "function" === typeof Fake.catch && Fake.catch(function() {
                });
              }
            } catch (sample) {
              if (sample && control && "string" === typeof sample.stack)
                return [sample.stack, control.stack];
            }
            return [null, null];
          }
        };
        RunInRootFrame.DetermineComponentFrameRoot.displayName = "DetermineComponentFrameRoot";
        var namePropDescriptor = Object.getOwnPropertyDescriptor(
          RunInRootFrame.DetermineComponentFrameRoot,
          "name"
        );
        namePropDescriptor && namePropDescriptor.configurable && Object.defineProperty(
          RunInRootFrame.DetermineComponentFrameRoot,
          "name",
          { value: "DetermineComponentFrameRoot" }
        );
        var _RunInRootFrame$Deter = RunInRootFrame.DetermineComponentFrameRoot(), sampleStack = _RunInRootFrame$Deter[0], controlStack = _RunInRootFrame$Deter[1];
        if (sampleStack && controlStack) {
          var sampleLines = sampleStack.split("\n"), controlLines = controlStack.split("\n");
          for (namePropDescriptor = RunInRootFrame = 0; RunInRootFrame < sampleLines.length && !sampleLines[RunInRootFrame].includes("DetermineComponentFrameRoot"); )
            RunInRootFrame++;
          for (; namePropDescriptor < controlLines.length && !controlLines[namePropDescriptor].includes(
            "DetermineComponentFrameRoot"
          ); )
            namePropDescriptor++;
          if (RunInRootFrame === sampleLines.length || namePropDescriptor === controlLines.length)
            for (RunInRootFrame = sampleLines.length - 1, namePropDescriptor = controlLines.length - 1; 1 <= RunInRootFrame && 0 <= namePropDescriptor && sampleLines[RunInRootFrame] !== controlLines[namePropDescriptor]; )
              namePropDescriptor--;
          for (; 1 <= RunInRootFrame && 0 <= namePropDescriptor; RunInRootFrame--, namePropDescriptor--)
            if (sampleLines[RunInRootFrame] !== controlLines[namePropDescriptor]) {
              if (1 !== RunInRootFrame || 1 !== namePropDescriptor) {
                do
                  if (RunInRootFrame--, namePropDescriptor--, 0 > namePropDescriptor || sampleLines[RunInRootFrame] !== controlLines[namePropDescriptor]) {
                    var frame = "\n" + sampleLines[RunInRootFrame].replace(" at new ", " at ");
                    fn.displayName && frame.includes("<anonymous>") && (frame = frame.replace("<anonymous>", fn.displayName));
                    return frame;
                  }
                while (1 <= RunInRootFrame && 0 <= namePropDescriptor);
              }
              break;
            }
        }
      } finally {
        reentry = false, Error.prepareStackTrace = previousPrepareStackTrace;
      }
      return (previousPrepareStackTrace = fn ? fn.displayName || fn.name : "") ? describeBuiltInComponentFrame(previousPrepareStackTrace) : "";
    }
    function describeComponentStackByType(type) {
      if ("string" === typeof type) return describeBuiltInComponentFrame(type);
      if ("function" === typeof type)
        return type.prototype && type.prototype.isReactComponent ? describeNativeComponentFrame(type, true) : describeNativeComponentFrame(type, false);
      if ("object" === typeof type && null !== type) {
        switch (type.$$typeof) {
          case REACT_FORWARD_REF_TYPE:
            return describeNativeComponentFrame(type.render, false);
          case REACT_MEMO_TYPE:
            return describeNativeComponentFrame(type.type, false);
          case REACT_LAZY_TYPE:
            var lazyComponent = type, payload = lazyComponent._payload;
            lazyComponent = lazyComponent._init;
            try {
              type = lazyComponent(payload);
            } catch (x) {
              return describeBuiltInComponentFrame("Lazy");
            }
            return describeComponentStackByType(type);
        }
        if ("string" === typeof type.name) {
          a: {
            payload = type.name;
            lazyComponent = type.env;
            var location = type.debugLocation;
            if (null != location && (type = Error.prepareStackTrace, Error.prepareStackTrace = void 0, location = location.stack, Error.prepareStackTrace = type, location.startsWith("Error: react-stack-top-frame\n") && (location = location.slice(29)), type = location.indexOf("\n"), -1 !== type && (location = location.slice(type + 1)), type = location.indexOf("react_stack_bottom_frame"), -1 !== type && (type = location.lastIndexOf("\n", type)), type = -1 !== type ? location = location.slice(0, type) : "", location = type.lastIndexOf("\n"), type = -1 === location ? type : type.slice(location + 1), -1 !== type.indexOf(payload))) {
              payload = "\n" + type;
              break a;
            }
            payload = describeBuiltInComponentFrame(
              payload + (lazyComponent ? " [" + lazyComponent + "]" : "")
            );
          }
          return payload;
        }
      }
      switch (type) {
        case REACT_SUSPENSE_LIST_TYPE:
          return describeBuiltInComponentFrame("SuspenseList");
        case REACT_SUSPENSE_TYPE:
          return describeBuiltInComponentFrame("Suspense");
      }
      return "";
    }
    function isEligibleForOutlining(request, boundary) {
      return (500 < boundary.byteSize || false) && null === boundary.contentPreamble;
    }
    function defaultErrorHandler(error) {
      if ("object" === typeof error && null !== error && "string" === typeof error.environmentName) {
        var JSCompiler_inline_result = error.environmentName;
        error = [error].slice(0);
        "string" === typeof error[0] ? error.splice(
          0,
          1,
          "[%s] " + error[0],
          " " + JSCompiler_inline_result + " "
        ) : error.splice(0, 0, "[%s]", " " + JSCompiler_inline_result + " ");
        error.unshift(console);
        JSCompiler_inline_result = bind.apply(console.error, error);
        JSCompiler_inline_result();
      } else console.error(error);
      return null;
    }
    function RequestInstance(resumableState, renderState, rootFormatContext, progressiveChunkSize, onError2, onAllReady, onShellReady, onShellError, onFatalError, onPostpone, formState) {
      var abortSet = /* @__PURE__ */ new Set();
      this.destination = null;
      this.flushScheduled = false;
      this.resumableState = resumableState;
      this.renderState = renderState;
      this.rootFormatContext = rootFormatContext;
      this.progressiveChunkSize = void 0 === progressiveChunkSize ? 12800 : progressiveChunkSize;
      this.status = 10;
      this.fatalError = null;
      this.pendingRootTasks = this.allPendingTasks = this.nextSegmentId = 0;
      this.completedPreambleSegments = this.completedRootSegment = null;
      this.byteSize = 0;
      this.abortableTasks = abortSet;
      this.pingedTasks = [];
      this.clientRenderedBoundaries = [];
      this.completedBoundaries = [];
      this.partialBoundaries = [];
      this.trackedPostpones = null;
      this.onError = void 0 === onError2 ? defaultErrorHandler : onError2;
      this.onPostpone = void 0 === onPostpone ? noop : onPostpone;
      this.onAllReady = void 0 === onAllReady ? noop : onAllReady;
      this.onShellReady = void 0 === onShellReady ? noop : onShellReady;
      this.onShellError = void 0 === onShellError ? noop : onShellError;
      this.onFatalError = void 0 === onFatalError ? noop : onFatalError;
      this.formState = void 0 === formState ? null : formState;
    }
    function createRequest(children, resumableState, renderState, rootFormatContext, progressiveChunkSize, onError2, onAllReady, onShellReady, onShellError, onFatalError, onPostpone, formState) {
      resumableState = new RequestInstance(
        resumableState,
        renderState,
        rootFormatContext,
        progressiveChunkSize,
        onError2,
        onAllReady,
        onShellReady,
        onShellError,
        onFatalError,
        onPostpone,
        formState
      );
      renderState = createPendingSegment(
        resumableState,
        0,
        null,
        rootFormatContext,
        false,
        false
      );
      renderState.parentFlushed = true;
      children = createRenderTask(
        resumableState,
        null,
        children,
        -1,
        null,
        renderState,
        null,
        null,
        resumableState.abortableTasks,
        null,
        rootFormatContext,
        null,
        emptyTreeContext,
        null,
        null
      );
      pushComponentStack(children);
      resumableState.pingedTasks.push(children);
      return resumableState;
    }
    var currentRequest = null;
    function pingTask(request, task) {
      request.pingedTasks.push(task);
      1 === request.pingedTasks.length && (request.flushScheduled = null !== request.destination, performWork(request));
    }
    function createSuspenseBoundary(request, row, fallbackAbortableTasks, contentPreamble, fallbackPreamble) {
      fallbackAbortableTasks = {
        status: 0,
        rootSegmentID: -1,
        parentFlushed: false,
        pendingTasks: 0,
        row,
        completedSegments: [],
        byteSize: 0,
        fallbackAbortableTasks,
        errorDigest: null,
        contentState: createHoistableState(),
        fallbackState: createHoistableState(),
        contentPreamble,
        fallbackPreamble,
        trackedContentKeyPath: null,
        trackedFallbackNode: null
      };
      null !== row && (row.pendingTasks++, contentPreamble = row.boundaries, null !== contentPreamble && (request.allPendingTasks++, fallbackAbortableTasks.pendingTasks++, contentPreamble.push(fallbackAbortableTasks)), request = row.inheritedHoistables, null !== request && hoistHoistables(fallbackAbortableTasks.contentState, request));
      return fallbackAbortableTasks;
    }
    function createRenderTask(request, thenableState2, node, childIndex, blockedBoundary, blockedSegment, blockedPreamble, hoistableState, abortSet, keyPath, formatContext, context, treeContext, row, componentStack) {
      request.allPendingTasks++;
      null === blockedBoundary ? request.pendingRootTasks++ : blockedBoundary.pendingTasks++;
      null !== row && row.pendingTasks++;
      var task = {
        replay: null,
        node,
        childIndex,
        ping: function() {
          return pingTask(request, task);
        },
        blockedBoundary,
        blockedSegment,
        blockedPreamble,
        hoistableState,
        abortSet,
        keyPath,
        formatContext,
        context,
        treeContext,
        row,
        componentStack,
        thenableState: thenableState2
      };
      abortSet.add(task);
      return task;
    }
    function createReplayTask(request, thenableState2, replay, node, childIndex, blockedBoundary, hoistableState, abortSet, keyPath, formatContext, context, treeContext, row, componentStack) {
      request.allPendingTasks++;
      null === blockedBoundary ? request.pendingRootTasks++ : blockedBoundary.pendingTasks++;
      null !== row && row.pendingTasks++;
      replay.pendingTasks++;
      var task = {
        replay,
        node,
        childIndex,
        ping: function() {
          return pingTask(request, task);
        },
        blockedBoundary,
        blockedSegment: null,
        blockedPreamble: null,
        hoistableState,
        abortSet,
        keyPath,
        formatContext,
        context,
        treeContext,
        row,
        componentStack,
        thenableState: thenableState2
      };
      abortSet.add(task);
      return task;
    }
    function createPendingSegment(request, index, boundary, parentFormatContext, lastPushedText, textEmbedded) {
      return {
        status: 0,
        parentFlushed: false,
        id: -1,
        index,
        chunks: [],
        children: [],
        preambleChildren: [],
        parentFormatContext,
        boundary,
        lastPushedText,
        textEmbedded
      };
    }
    function pushComponentStack(task) {
      var node = task.node;
      if ("object" === typeof node && null !== node)
        switch (node.$$typeof) {
          case REACT_ELEMENT_TYPE:
            task.componentStack = { parent: task.componentStack, type: node.type };
        }
    }
    function replaceSuspenseComponentStackWithSuspenseFallbackStack(componentStack) {
      return null === componentStack ? null : { parent: componentStack.parent, type: "Suspense Fallback" };
    }
    function getThrownInfo(node$jscomp$0) {
      var errorInfo = {};
      node$jscomp$0 && Object.defineProperty(errorInfo, "componentStack", {
        configurable: true,
        enumerable: true,
        get: function() {
          try {
            var info = "", node = node$jscomp$0;
            do
              info += describeComponentStackByType(node.type), node = node.parent;
            while (node);
            var JSCompiler_inline_result = info;
          } catch (x) {
            JSCompiler_inline_result = "\nError generating stack: " + x.message + "\n" + x.stack;
          }
          Object.defineProperty(errorInfo, "componentStack", {
            value: JSCompiler_inline_result
          });
          return JSCompiler_inline_result;
        }
      });
      return errorInfo;
    }
    function logRecoverableError(request, error, errorInfo) {
      request = request.onError;
      error = request(error, errorInfo);
      if (null == error || "string" === typeof error) return error;
    }
    function fatalError(request, error) {
      var onShellError = request.onShellError, onFatalError = request.onFatalError;
      onShellError(error);
      onFatalError(error);
      null !== request.destination ? (request.status = 14, request.destination.destroy(error)) : (request.status = 13, request.fatalError = error);
    }
    function finishSuspenseListRow(request, row) {
      unblockSuspenseListRow(request, row.next, row.hoistables);
    }
    function unblockSuspenseListRow(request, unblockedRow, inheritedHoistables) {
      for (; null !== unblockedRow; ) {
        null !== inheritedHoistables && (hoistHoistables(unblockedRow.hoistables, inheritedHoistables), unblockedRow.inheritedHoistables = inheritedHoistables);
        var unblockedBoundaries = unblockedRow.boundaries;
        if (null !== unblockedBoundaries) {
          unblockedRow.boundaries = null;
          for (var i = 0; i < unblockedBoundaries.length; i++) {
            var unblockedBoundary = unblockedBoundaries[i];
            null !== inheritedHoistables && hoistHoistables(unblockedBoundary.contentState, inheritedHoistables);
            finishedTask(request, unblockedBoundary, null, null);
          }
        }
        unblockedRow.pendingTasks--;
        if (0 < unblockedRow.pendingTasks) break;
        inheritedHoistables = unblockedRow.hoistables;
        unblockedRow = unblockedRow.next;
      }
    }
    function tryToResolveTogetherRow(request, togetherRow) {
      var boundaries = togetherRow.boundaries;
      if (null !== boundaries && togetherRow.pendingTasks === boundaries.length) {
        for (var allCompleteAndInlinable = true, i = 0; i < boundaries.length; i++) {
          var rowBoundary = boundaries[i];
          if (1 !== rowBoundary.pendingTasks || rowBoundary.parentFlushed || isEligibleForOutlining(request, rowBoundary)) {
            allCompleteAndInlinable = false;
            break;
          }
        }
        allCompleteAndInlinable && unblockSuspenseListRow(request, togetherRow, togetherRow.hoistables);
      }
    }
    function createSuspenseListRow(previousRow) {
      var newRow = {
        pendingTasks: 1,
        boundaries: null,
        hoistables: createHoistableState(),
        inheritedHoistables: null,
        together: false,
        next: null
      };
      null !== previousRow && 0 < previousRow.pendingTasks && (newRow.pendingTasks++, newRow.boundaries = [], previousRow.next = newRow);
      return newRow;
    }
    function renderSuspenseListRows(request, task, keyPath, rows, revealOrder) {
      var prevKeyPath = task.keyPath, prevTreeContext = task.treeContext, prevRow = task.row;
      task.keyPath = keyPath;
      keyPath = rows.length;
      var previousSuspenseListRow = null;
      if (null !== task.replay) {
        var resumeSlots = task.replay.slots;
        if (null !== resumeSlots && "object" === typeof resumeSlots)
          for (var n = 0; n < keyPath; n++) {
            var i = "backwards" !== revealOrder && "unstable_legacy-backwards" !== revealOrder ? n : keyPath - 1 - n, node = rows[i];
            task.row = previousSuspenseListRow = createSuspenseListRow(
              previousSuspenseListRow
            );
            task.treeContext = pushTreeContext(prevTreeContext, keyPath, i);
            var resumeSegmentID = resumeSlots[i];
            "number" === typeof resumeSegmentID ? (resumeNode(request, task, resumeSegmentID, node, i), delete resumeSlots[i]) : renderNode(request, task, node, i);
            0 === --previousSuspenseListRow.pendingTasks && finishSuspenseListRow(request, previousSuspenseListRow);
          }
        else
          for (resumeSlots = 0; resumeSlots < keyPath; resumeSlots++)
            n = "backwards" !== revealOrder && "unstable_legacy-backwards" !== revealOrder ? resumeSlots : keyPath - 1 - resumeSlots, i = rows[n], task.row = previousSuspenseListRow = createSuspenseListRow(previousSuspenseListRow), task.treeContext = pushTreeContext(prevTreeContext, keyPath, n), renderNode(request, task, i, n), 0 === --previousSuspenseListRow.pendingTasks && finishSuspenseListRow(request, previousSuspenseListRow);
      } else if ("backwards" !== revealOrder && "unstable_legacy-backwards" !== revealOrder)
        for (revealOrder = 0; revealOrder < keyPath; revealOrder++)
          resumeSlots = rows[revealOrder], task.row = previousSuspenseListRow = createSuspenseListRow(previousSuspenseListRow), task.treeContext = pushTreeContext(
            prevTreeContext,
            keyPath,
            revealOrder
          ), renderNode(request, task, resumeSlots, revealOrder), 0 === --previousSuspenseListRow.pendingTasks && finishSuspenseListRow(request, previousSuspenseListRow);
      else {
        revealOrder = task.blockedSegment;
        resumeSlots = revealOrder.children.length;
        n = revealOrder.chunks.length;
        for (i = keyPath - 1; 0 <= i; i--) {
          node = rows[i];
          task.row = previousSuspenseListRow = createSuspenseListRow(
            previousSuspenseListRow
          );
          task.treeContext = pushTreeContext(prevTreeContext, keyPath, i);
          resumeSegmentID = createPendingSegment(
            request,
            n,
            null,
            task.formatContext,
            0 === i ? revealOrder.lastPushedText : true,
            true
          );
          revealOrder.children.splice(resumeSlots, 0, resumeSegmentID);
          task.blockedSegment = resumeSegmentID;
          try {
            renderNode(request, task, node, i), pushSegmentFinale(
              resumeSegmentID.chunks,
              request.renderState,
              resumeSegmentID.lastPushedText,
              resumeSegmentID.textEmbedded
            ), resumeSegmentID.status = 1, 0 === --previousSuspenseListRow.pendingTasks && finishSuspenseListRow(request, previousSuspenseListRow);
          } catch (thrownValue) {
            throw resumeSegmentID.status = 12 === request.status ? 3 : 4, thrownValue;
          }
        }
        task.blockedSegment = revealOrder;
        revealOrder.lastPushedText = false;
      }
      null !== prevRow && null !== previousSuspenseListRow && 0 < previousSuspenseListRow.pendingTasks && (prevRow.pendingTasks++, previousSuspenseListRow.next = prevRow);
      task.treeContext = prevTreeContext;
      task.row = prevRow;
      task.keyPath = prevKeyPath;
    }
    function renderWithHooks(request, task, keyPath, Component, props, secondArg) {
      var prevThenableState = task.thenableState;
      task.thenableState = null;
      currentlyRenderingComponent = {};
      currentlyRenderingTask = task;
      currentlyRenderingRequest = request;
      currentlyRenderingKeyPath = keyPath;
      actionStateCounter = localIdCounter = 0;
      actionStateMatchingIndex = -1;
      thenableIndexCounter = 0;
      thenableState = prevThenableState;
      for (request = Component(props, secondArg); didScheduleRenderPhaseUpdate; )
        didScheduleRenderPhaseUpdate = false, actionStateCounter = localIdCounter = 0, actionStateMatchingIndex = -1, thenableIndexCounter = 0, numberOfReRenders += 1, workInProgressHook = null, request = Component(props, secondArg);
      resetHooksState();
      return request;
    }
    function finishFunctionComponent(request, task, keyPath, children, hasId, actionStateCount, actionStateMatchingIndex2) {
      var didEmitActionStateMarkers = false;
      if (0 !== actionStateCount && null !== request.formState) {
        var segment = task.blockedSegment;
        if (null !== segment) {
          didEmitActionStateMarkers = true;
          segment = segment.chunks;
          for (var i = 0; i < actionStateCount; i++)
            i === actionStateMatchingIndex2 ? segment.push("<!--F!-->") : segment.push("<!--F-->");
        }
      }
      actionStateCount = task.keyPath;
      task.keyPath = keyPath;
      hasId ? (keyPath = task.treeContext, task.treeContext = pushTreeContext(keyPath, 1, 0), renderNode(request, task, children, -1), task.treeContext = keyPath) : didEmitActionStateMarkers ? renderNode(request, task, children, -1) : renderNodeDestructive(request, task, children, -1);
      task.keyPath = actionStateCount;
    }
    function renderElement(request, task, keyPath, type, props, ref) {
      if ("function" === typeof type)
        if (type.prototype && type.prototype.isReactComponent) {
          var newProps = props;
          if ("ref" in props) {
            newProps = {};
            for (var propName in props)
              "ref" !== propName && (newProps[propName] = props[propName]);
          }
          var defaultProps = type.defaultProps;
          if (defaultProps) {
            newProps === props && (newProps = assign({}, newProps, props));
            for (var propName$43 in defaultProps)
              void 0 === newProps[propName$43] && (newProps[propName$43] = defaultProps[propName$43]);
          }
          props = newProps;
          newProps = emptyContextObject;
          defaultProps = type.contextType;
          "object" === typeof defaultProps && null !== defaultProps && (newProps = defaultProps._currentValue2);
          newProps = new type(props, newProps);
          var initialState = void 0 !== newProps.state ? newProps.state : null;
          newProps.updater = classComponentUpdater;
          newProps.props = props;
          newProps.state = initialState;
          defaultProps = { queue: [], replace: false };
          newProps._reactInternals = defaultProps;
          ref = type.contextType;
          newProps.context = "object" === typeof ref && null !== ref ? ref._currentValue2 : emptyContextObject;
          ref = type.getDerivedStateFromProps;
          "function" === typeof ref && (ref = ref(props, initialState), initialState = null === ref || void 0 === ref ? initialState : assign({}, initialState, ref), newProps.state = initialState);
          if ("function" !== typeof type.getDerivedStateFromProps && "function" !== typeof newProps.getSnapshotBeforeUpdate && ("function" === typeof newProps.UNSAFE_componentWillMount || "function" === typeof newProps.componentWillMount))
            if (type = newProps.state, "function" === typeof newProps.componentWillMount && newProps.componentWillMount(), "function" === typeof newProps.UNSAFE_componentWillMount && newProps.UNSAFE_componentWillMount(), type !== newProps.state && classComponentUpdater.enqueueReplaceState(
              newProps,
              newProps.state,
              null
            ), null !== defaultProps.queue && 0 < defaultProps.queue.length)
              if (type = defaultProps.queue, ref = defaultProps.replace, defaultProps.queue = null, defaultProps.replace = false, ref && 1 === type.length)
                newProps.state = type[0];
              else {
                defaultProps = ref ? type[0] : newProps.state;
                initialState = true;
                for (ref = ref ? 1 : 0; ref < type.length; ref++)
                  propName$43 = type[ref], propName$43 = "function" === typeof propName$43 ? propName$43.call(newProps, defaultProps, props, void 0) : propName$43, null != propName$43 && (initialState ? (initialState = false, defaultProps = assign({}, defaultProps, propName$43)) : assign(defaultProps, propName$43));
                newProps.state = defaultProps;
              }
            else defaultProps.queue = null;
          type = newProps.render();
          if (12 === request.status) throw null;
          props = task.keyPath;
          task.keyPath = keyPath;
          renderNodeDestructive(request, task, type, -1);
          task.keyPath = props;
        } else {
          type = renderWithHooks(request, task, keyPath, type, props, void 0);
          if (12 === request.status) throw null;
          finishFunctionComponent(
            request,
            task,
            keyPath,
            type,
            0 !== localIdCounter,
            actionStateCounter,
            actionStateMatchingIndex
          );
        }
      else if ("string" === typeof type)
        if (newProps = task.blockedSegment, null === newProps)
          newProps = props.children, defaultProps = task.formatContext, initialState = task.keyPath, task.formatContext = getChildFormatContext(defaultProps, type, props), task.keyPath = keyPath, renderNode(request, task, newProps, -1), task.formatContext = defaultProps, task.keyPath = initialState;
        else {
          initialState = pushStartInstance(
            newProps.chunks,
            type,
            props,
            request.resumableState,
            request.renderState,
            task.blockedPreamble,
            task.hoistableState,
            task.formatContext,
            newProps.lastPushedText
          );
          newProps.lastPushedText = false;
          defaultProps = task.formatContext;
          ref = task.keyPath;
          task.keyPath = keyPath;
          if (3 === (task.formatContext = getChildFormatContext(defaultProps, type, props)).insertionMode) {
            keyPath = createPendingSegment(
              request,
              0,
              null,
              task.formatContext,
              false,
              false
            );
            newProps.preambleChildren.push(keyPath);
            task.blockedSegment = keyPath;
            try {
              keyPath.status = 6, renderNode(request, task, initialState, -1), pushSegmentFinale(
                keyPath.chunks,
                request.renderState,
                keyPath.lastPushedText,
                keyPath.textEmbedded
              ), keyPath.status = 1;
            } finally {
              task.blockedSegment = newProps;
            }
          } else renderNode(request, task, initialState, -1);
          task.formatContext = defaultProps;
          task.keyPath = ref;
          a: {
            task = newProps.chunks;
            request = request.resumableState;
            switch (type) {
              case "title":
              case "style":
              case "script":
              case "area":
              case "base":
              case "br":
              case "col":
              case "embed":
              case "hr":
              case "img":
              case "input":
              case "keygen":
              case "link":
              case "meta":
              case "param":
              case "source":
              case "track":
              case "wbr":
                break a;
              case "body":
                if (1 >= defaultProps.insertionMode) {
                  request.hasBody = true;
                  break a;
                }
                break;
              case "html":
                if (0 === defaultProps.insertionMode) {
                  request.hasHtml = true;
                  break a;
                }
                break;
              case "head":
                if (1 >= defaultProps.insertionMode) break a;
            }
            task.push(endChunkForTag(type));
          }
          newProps.lastPushedText = false;
        }
      else {
        switch (type) {
          case REACT_LEGACY_HIDDEN_TYPE:
          case REACT_STRICT_MODE_TYPE:
          case REACT_PROFILER_TYPE:
          case REACT_FRAGMENT_TYPE:
            type = task.keyPath;
            task.keyPath = keyPath;
            renderNodeDestructive(request, task, props.children, -1);
            task.keyPath = type;
            return;
          case REACT_ACTIVITY_TYPE:
            type = task.blockedSegment;
            null === type ? "hidden" !== props.mode && (type = task.keyPath, task.keyPath = keyPath, renderNode(request, task, props.children, -1), task.keyPath = type) : "hidden" !== props.mode && (request.renderState.generateStaticMarkup || type.chunks.push("<!--&-->"), type.lastPushedText = false, newProps = task.keyPath, task.keyPath = keyPath, renderNode(request, task, props.children, -1), task.keyPath = newProps, request.renderState.generateStaticMarkup || type.chunks.push("<!--/&-->"), type.lastPushedText = false);
            return;
          case REACT_SUSPENSE_LIST_TYPE:
            a: {
              type = props.children;
              props = props.revealOrder;
              if ("forwards" === props || "backwards" === props || "unstable_legacy-backwards" === props) {
                if (isArrayImpl(type)) {
                  renderSuspenseListRows(request, task, keyPath, type, props);
                  break a;
                }
                if (newProps = getIteratorFn(type)) {
                  if (newProps = newProps.call(type)) {
                    defaultProps = newProps.next();
                    if (!defaultProps.done) {
                      do
                        defaultProps = newProps.next();
                      while (!defaultProps.done);
                      renderSuspenseListRows(request, task, keyPath, type, props);
                    }
                    break a;
                  }
                }
              }
              "together" === props ? (props = task.keyPath, newProps = task.row, defaultProps = task.row = createSuspenseListRow(null), defaultProps.boundaries = [], defaultProps.together = true, task.keyPath = keyPath, renderNodeDestructive(request, task, type, -1), 0 === --defaultProps.pendingTasks && finishSuspenseListRow(request, defaultProps), task.keyPath = props, task.row = newProps, null !== newProps && 0 < defaultProps.pendingTasks && (newProps.pendingTasks++, defaultProps.next = newProps)) : (props = task.keyPath, task.keyPath = keyPath, renderNodeDestructive(request, task, type, -1), task.keyPath = props);
            }
            return;
          case REACT_VIEW_TRANSITION_TYPE:
          case REACT_SCOPE_TYPE:
            throw Error(formatProdErrorMessage(343));
          case REACT_SUSPENSE_TYPE:
            a: if (null !== task.replay) {
              type = task.keyPath;
              newProps = task.formatContext;
              defaultProps = task.row;
              task.keyPath = keyPath;
              task.formatContext = getSuspenseContentFormatContext(
                request.resumableState,
                newProps
              );
              task.row = null;
              keyPath = props.children;
              try {
                renderNode(request, task, keyPath, -1);
              } finally {
                task.keyPath = type, task.formatContext = newProps, task.row = defaultProps;
              }
            } else {
              type = task.keyPath;
              ref = task.formatContext;
              var prevRow = task.row, parentBoundary = task.blockedBoundary;
              propName$43 = task.blockedPreamble;
              var parentHoistableState = task.hoistableState;
              propName = task.blockedSegment;
              var fallback = props.fallback;
              props = props.children;
              var fallbackAbortSet = /* @__PURE__ */ new Set();
              var newBoundary = createSuspenseBoundary(
                request,
                task.row,
                fallbackAbortSet,
                null,
                null
              );
              null !== request.trackedPostpones && (newBoundary.trackedContentKeyPath = keyPath);
              var boundarySegment = createPendingSegment(
                request,
                propName.chunks.length,
                newBoundary,
                task.formatContext,
                false,
                false
              );
              propName.children.push(boundarySegment);
              propName.lastPushedText = false;
              var contentRootSegment = createPendingSegment(
                request,
                0,
                null,
                task.formatContext,
                false,
                false
              );
              contentRootSegment.parentFlushed = true;
              if (null !== request.trackedPostpones) {
                newProps = task.componentStack;
                defaultProps = [keyPath[0], "Suspense Fallback", keyPath[2]];
                initialState = [defaultProps[1], defaultProps[2], [], null];
                request.trackedPostpones.workingMap.set(defaultProps, initialState);
                newBoundary.trackedFallbackNode = initialState;
                task.blockedSegment = boundarySegment;
                task.blockedPreamble = newBoundary.fallbackPreamble;
                task.keyPath = defaultProps;
                task.formatContext = getSuspenseFallbackFormatContext(
                  request.resumableState,
                  ref
                );
                task.componentStack = replaceSuspenseComponentStackWithSuspenseFallbackStack(newProps);
                boundarySegment.status = 6;
                try {
                  renderNode(request, task, fallback, -1), pushSegmentFinale(
                    boundarySegment.chunks,
                    request.renderState,
                    boundarySegment.lastPushedText,
                    boundarySegment.textEmbedded
                  ), boundarySegment.status = 1;
                } catch (thrownValue) {
                  throw boundarySegment.status = 12 === request.status ? 3 : 4, thrownValue;
                } finally {
                  task.blockedSegment = propName, task.blockedPreamble = propName$43, task.keyPath = type, task.formatContext = ref;
                }
                task = createRenderTask(
                  request,
                  null,
                  props,
                  -1,
                  newBoundary,
                  contentRootSegment,
                  newBoundary.contentPreamble,
                  newBoundary.contentState,
                  task.abortSet,
                  keyPath,
                  getSuspenseContentFormatContext(
                    request.resumableState,
                    task.formatContext
                  ),
                  task.context,
                  task.treeContext,
                  null,
                  newProps
                );
                pushComponentStack(task);
                request.pingedTasks.push(task);
              } else {
                task.blockedBoundary = newBoundary;
                task.blockedPreamble = newBoundary.contentPreamble;
                task.hoistableState = newBoundary.contentState;
                task.blockedSegment = contentRootSegment;
                task.keyPath = keyPath;
                task.formatContext = getSuspenseContentFormatContext(
                  request.resumableState,
                  ref
                );
                task.row = null;
                contentRootSegment.status = 6;
                try {
                  if (renderNode(request, task, props, -1), pushSegmentFinale(
                    contentRootSegment.chunks,
                    request.renderState,
                    contentRootSegment.lastPushedText,
                    contentRootSegment.textEmbedded
                  ), contentRootSegment.status = 1, queueCompletedSegment(newBoundary, contentRootSegment), 0 === newBoundary.pendingTasks && 0 === newBoundary.status) {
                    if (newBoundary.status = 1, !isEligibleForOutlining(request, newBoundary)) {
                      null !== prevRow && 0 === --prevRow.pendingTasks && finishSuspenseListRow(request, prevRow);
                      0 === request.pendingRootTasks && task.blockedPreamble && preparePreamble(request);
                      break a;
                    }
                  } else
                    null !== prevRow && prevRow.together && tryToResolveTogetherRow(request, prevRow);
                } catch (thrownValue$30) {
                  newBoundary.status = 4, 12 === request.status ? (contentRootSegment.status = 3, newProps = request.fatalError) : (contentRootSegment.status = 4, newProps = thrownValue$30), defaultProps = getThrownInfo(task.componentStack), initialState = logRecoverableError(
                    request,
                    newProps,
                    defaultProps
                  ), newBoundary.errorDigest = initialState, untrackBoundary(request, newBoundary);
                } finally {
                  task.blockedBoundary = parentBoundary, task.blockedPreamble = propName$43, task.hoistableState = parentHoistableState, task.blockedSegment = propName, task.keyPath = type, task.formatContext = ref, task.row = prevRow;
                }
                task = createRenderTask(
                  request,
                  null,
                  fallback,
                  -1,
                  parentBoundary,
                  boundarySegment,
                  newBoundary.fallbackPreamble,
                  newBoundary.fallbackState,
                  fallbackAbortSet,
                  [keyPath[0], "Suspense Fallback", keyPath[2]],
                  getSuspenseFallbackFormatContext(
                    request.resumableState,
                    task.formatContext
                  ),
                  task.context,
                  task.treeContext,
                  task.row,
                  replaceSuspenseComponentStackWithSuspenseFallbackStack(
                    task.componentStack
                  )
                );
                pushComponentStack(task);
                request.pingedTasks.push(task);
              }
            }
            return;
        }
        if ("object" === typeof type && null !== type)
          switch (type.$$typeof) {
            case REACT_FORWARD_REF_TYPE:
              if ("ref" in props)
                for (fallback in newProps = {}, props)
                  "ref" !== fallback && (newProps[fallback] = props[fallback]);
              else newProps = props;
              type = renderWithHooks(
                request,
                task,
                keyPath,
                type.render,
                newProps,
                ref
              );
              finishFunctionComponent(
                request,
                task,
                keyPath,
                type,
                0 !== localIdCounter,
                actionStateCounter,
                actionStateMatchingIndex
              );
              return;
            case REACT_MEMO_TYPE:
              renderElement(request, task, keyPath, type.type, props, ref);
              return;
            case REACT_CONTEXT_TYPE:
              defaultProps = props.children;
              newProps = task.keyPath;
              props = props.value;
              initialState = type._currentValue2;
              type._currentValue2 = props;
              ref = currentActiveSnapshot;
              currentActiveSnapshot = type = {
                parent: ref,
                depth: null === ref ? 0 : ref.depth + 1,
                context: type,
                parentValue: initialState,
                value: props
              };
              task.context = type;
              task.keyPath = keyPath;
              renderNodeDestructive(request, task, defaultProps, -1);
              request = currentActiveSnapshot;
              if (null === request) throw Error(formatProdErrorMessage(403));
              request.context._currentValue2 = request.parentValue;
              request = currentActiveSnapshot = request.parent;
              task.context = request;
              task.keyPath = newProps;
              return;
            case REACT_CONSUMER_TYPE:
              props = props.children;
              type = props(type._context._currentValue2);
              props = task.keyPath;
              task.keyPath = keyPath;
              renderNodeDestructive(request, task, type, -1);
              task.keyPath = props;
              return;
            case REACT_LAZY_TYPE:
              newProps = type._init;
              type = newProps(type._payload);
              if (12 === request.status) throw null;
              renderElement(request, task, keyPath, type, props, ref);
              return;
          }
        throw Error(
          formatProdErrorMessage(130, null == type ? type : typeof type, "")
        );
      }
    }
    function resumeNode(request, task, segmentId, node, childIndex) {
      var prevReplay = task.replay, blockedBoundary = task.blockedBoundary, resumedSegment = createPendingSegment(
        request,
        0,
        null,
        task.formatContext,
        false,
        false
      );
      resumedSegment.id = segmentId;
      resumedSegment.parentFlushed = true;
      try {
        task.replay = null, task.blockedSegment = resumedSegment, renderNode(request, task, node, childIndex), resumedSegment.status = 1, null === blockedBoundary ? request.completedRootSegment = resumedSegment : (queueCompletedSegment(blockedBoundary, resumedSegment), blockedBoundary.parentFlushed && request.partialBoundaries.push(blockedBoundary));
      } finally {
        task.replay = prevReplay, task.blockedSegment = null;
      }
    }
    function renderNodeDestructive(request, task, node, childIndex) {
      null !== task.replay && "number" === typeof task.replay.slots ? resumeNode(request, task, task.replay.slots, node, childIndex) : (task.node = node, task.childIndex = childIndex, node = task.componentStack, pushComponentStack(task), retryNode(request, task), task.componentStack = node);
    }
    function retryNode(request, task) {
      var node = task.node, childIndex = task.childIndex;
      if (null !== node) {
        if ("object" === typeof node) {
          switch (node.$$typeof) {
            case REACT_ELEMENT_TYPE:
              var type = node.type, key = node.key, props = node.props;
              node = props.ref;
              var ref = void 0 !== node ? node : null, name = getComponentNameFromType(type), keyOrIndex = null == key ? -1 === childIndex ? 0 : childIndex : key;
              key = [task.keyPath, name, keyOrIndex];
              if (null !== task.replay)
                a: {
                  var replay = task.replay;
                  childIndex = replay.nodes;
                  for (node = 0; node < childIndex.length; node++) {
                    var node$jscomp$0 = childIndex[node];
                    if (keyOrIndex === node$jscomp$0[1]) {
                      if (4 === node$jscomp$0.length) {
                        if (null !== name && name !== node$jscomp$0[0])
                          throw Error(
                            formatProdErrorMessage(490, node$jscomp$0[0], name)
                          );
                        var childNodes = node$jscomp$0[2];
                        name = node$jscomp$0[3];
                        keyOrIndex = task.node;
                        task.replay = {
                          nodes: childNodes,
                          slots: name,
                          pendingTasks: 1
                        };
                        try {
                          renderElement(request, task, key, type, props, ref);
                          if (1 === task.replay.pendingTasks && 0 < task.replay.nodes.length)
                            throw Error(formatProdErrorMessage(488));
                          task.replay.pendingTasks--;
                        } catch (x) {
                          if ("object" === typeof x && null !== x && (x === SuspenseException || "function" === typeof x.then))
                            throw task.node === keyOrIndex ? task.replay = replay : childIndex.splice(node, 1), x;
                          task.replay.pendingTasks--;
                          props = getThrownInfo(task.componentStack);
                          key = request;
                          request = task.blockedBoundary;
                          type = x;
                          props = logRecoverableError(key, type, props);
                          abortRemainingReplayNodes(
                            key,
                            request,
                            childNodes,
                            name,
                            type,
                            props
                          );
                        }
                        task.replay = replay;
                      } else {
                        if (type !== REACT_SUSPENSE_TYPE)
                          throw Error(
                            formatProdErrorMessage(
                              490,
                              "Suspense",
                              getComponentNameFromType(type) || "Unknown"
                            )
                          );
                        b: {
                          replay = void 0;
                          type = node$jscomp$0[5];
                          ref = node$jscomp$0[2];
                          name = node$jscomp$0[3];
                          keyOrIndex = null === node$jscomp$0[4] ? [] : node$jscomp$0[4][2];
                          node$jscomp$0 = null === node$jscomp$0[4] ? null : node$jscomp$0[4][3];
                          var prevKeyPath = task.keyPath, prevContext = task.formatContext, prevRow = task.row, previousReplaySet = task.replay, parentBoundary = task.blockedBoundary, parentHoistableState = task.hoistableState, content = props.children, fallback = props.fallback, fallbackAbortSet = /* @__PURE__ */ new Set();
                          props = createSuspenseBoundary(
                            request,
                            task.row,
                            fallbackAbortSet,
                            null,
                            null
                          );
                          props.parentFlushed = true;
                          props.rootSegmentID = type;
                          task.blockedBoundary = props;
                          task.hoistableState = props.contentState;
                          task.keyPath = key;
                          task.formatContext = getSuspenseContentFormatContext(
                            request.resumableState,
                            prevContext
                          );
                          task.row = null;
                          task.replay = {
                            nodes: ref,
                            slots: name,
                            pendingTasks: 1
                          };
                          try {
                            renderNode(request, task, content, -1);
                            if (1 === task.replay.pendingTasks && 0 < task.replay.nodes.length)
                              throw Error(formatProdErrorMessage(488));
                            task.replay.pendingTasks--;
                            if (0 === props.pendingTasks && 0 === props.status) {
                              props.status = 1;
                              request.completedBoundaries.push(props);
                              break b;
                            }
                          } catch (error) {
                            props.status = 4, childNodes = getThrownInfo(task.componentStack), replay = logRecoverableError(
                              request,
                              error,
                              childNodes
                            ), props.errorDigest = replay, task.replay.pendingTasks--, request.clientRenderedBoundaries.push(props);
                          } finally {
                            task.blockedBoundary = parentBoundary, task.hoistableState = parentHoistableState, task.replay = previousReplaySet, task.keyPath = prevKeyPath, task.formatContext = prevContext, task.row = prevRow;
                          }
                          childNodes = createReplayTask(
                            request,
                            null,
                            {
                              nodes: keyOrIndex,
                              slots: node$jscomp$0,
                              pendingTasks: 0
                            },
                            fallback,
                            -1,
                            parentBoundary,
                            props.fallbackState,
                            fallbackAbortSet,
                            [key[0], "Suspense Fallback", key[2]],
                            getSuspenseFallbackFormatContext(
                              request.resumableState,
                              task.formatContext
                            ),
                            task.context,
                            task.treeContext,
                            task.row,
                            replaceSuspenseComponentStackWithSuspenseFallbackStack(
                              task.componentStack
                            )
                          );
                          pushComponentStack(childNodes);
                          request.pingedTasks.push(childNodes);
                        }
                      }
                      childIndex.splice(node, 1);
                      break a;
                    }
                  }
                }
              else renderElement(request, task, key, type, props, ref);
              return;
            case REACT_PORTAL_TYPE:
              throw Error(formatProdErrorMessage(257));
            case REACT_LAZY_TYPE:
              childNodes = node._init;
              node = childNodes(node._payload);
              if (12 === request.status) throw null;
              renderNodeDestructive(request, task, node, childIndex);
              return;
          }
          if (isArrayImpl(node)) {
            renderChildrenArray(request, task, node, childIndex);
            return;
          }
          if (childNodes = getIteratorFn(node)) {
            if (childNodes = childNodes.call(node)) {
              node = childNodes.next();
              if (!node.done) {
                props = [];
                do
                  props.push(node.value), node = childNodes.next();
                while (!node.done);
                renderChildrenArray(request, task, props, childIndex);
              }
              return;
            }
          }
          if ("function" === typeof node.then)
            return task.thenableState = null, renderNodeDestructive(request, task, unwrapThenable(node), childIndex);
          if (node.$$typeof === REACT_CONTEXT_TYPE)
            return renderNodeDestructive(
              request,
              task,
              node._currentValue2,
              childIndex
            );
          childIndex = Object.prototype.toString.call(node);
          throw Error(
            formatProdErrorMessage(
              31,
              "[object Object]" === childIndex ? "object with keys {" + Object.keys(node).join(", ") + "}" : childIndex
            )
          );
        }
        if ("string" === typeof node)
          childIndex = task.blockedSegment, null !== childIndex && (childIndex.lastPushedText = pushTextInstance(
            childIndex.chunks,
            node,
            request.renderState,
            childIndex.lastPushedText
          ));
        else if ("number" === typeof node || "bigint" === typeof node)
          childIndex = task.blockedSegment, null !== childIndex && (childIndex.lastPushedText = pushTextInstance(
            childIndex.chunks,
            "" + node,
            request.renderState,
            childIndex.lastPushedText
          ));
      }
    }
    function renderChildrenArray(request, task, children, childIndex) {
      var prevKeyPath = task.keyPath;
      if (-1 !== childIndex && (task.keyPath = [task.keyPath, "Fragment", childIndex], null !== task.replay)) {
        for (var replay = task.replay, replayNodes = replay.nodes, j = 0; j < replayNodes.length; j++) {
          var node = replayNodes[j];
          if (node[1] === childIndex) {
            childIndex = node[2];
            node = node[3];
            task.replay = { nodes: childIndex, slots: node, pendingTasks: 1 };
            try {
              renderChildrenArray(request, task, children, -1);
              if (1 === task.replay.pendingTasks && 0 < task.replay.nodes.length)
                throw Error(formatProdErrorMessage(488));
              task.replay.pendingTasks--;
            } catch (x) {
              if ("object" === typeof x && null !== x && (x === SuspenseException || "function" === typeof x.then))
                throw x;
              task.replay.pendingTasks--;
              children = getThrownInfo(task.componentStack);
              var boundary = task.blockedBoundary, error = x;
              children = logRecoverableError(request, error, children);
              abortRemainingReplayNodes(
                request,
                boundary,
                childIndex,
                node,
                error,
                children
              );
            }
            task.replay = replay;
            replayNodes.splice(j, 1);
            break;
          }
        }
        task.keyPath = prevKeyPath;
        return;
      }
      replay = task.treeContext;
      replayNodes = children.length;
      if (null !== task.replay && (j = task.replay.slots, null !== j && "object" === typeof j)) {
        for (childIndex = 0; childIndex < replayNodes; childIndex++)
          node = children[childIndex], task.treeContext = pushTreeContext(replay, replayNodes, childIndex), boundary = j[childIndex], "number" === typeof boundary ? (resumeNode(request, task, boundary, node, childIndex), delete j[childIndex]) : renderNode(request, task, node, childIndex);
        task.treeContext = replay;
        task.keyPath = prevKeyPath;
        return;
      }
      for (j = 0; j < replayNodes; j++)
        childIndex = children[j], task.treeContext = pushTreeContext(replay, replayNodes, j), renderNode(request, task, childIndex, j);
      task.treeContext = replay;
      task.keyPath = prevKeyPath;
    }
    function trackPostponedBoundary(request, trackedPostpones, boundary) {
      boundary.status = 5;
      boundary.rootSegmentID = request.nextSegmentId++;
      request = boundary.trackedContentKeyPath;
      if (null === request) throw Error(formatProdErrorMessage(486));
      var fallbackReplayNode = boundary.trackedFallbackNode, children = [], boundaryNode = trackedPostpones.workingMap.get(request);
      if (void 0 === boundaryNode)
        return boundary = [
          request[1],
          request[2],
          children,
          null,
          fallbackReplayNode,
          boundary.rootSegmentID
        ], trackedPostpones.workingMap.set(request, boundary), addToReplayParent(boundary, request[0], trackedPostpones), boundary;
      boundaryNode[4] = fallbackReplayNode;
      boundaryNode[5] = boundary.rootSegmentID;
      return boundaryNode;
    }
    function trackPostpone(request, trackedPostpones, task, segment) {
      segment.status = 5;
      var keyPath = task.keyPath, boundary = task.blockedBoundary;
      if (null === boundary)
        segment.id = request.nextSegmentId++, trackedPostpones.rootSlots = segment.id, null !== request.completedRootSegment && (request.completedRootSegment.status = 5);
      else {
        if (null !== boundary && 0 === boundary.status) {
          var boundaryNode = trackPostponedBoundary(
            request,
            trackedPostpones,
            boundary
          );
          if (boundary.trackedContentKeyPath === keyPath && -1 === task.childIndex) {
            -1 === segment.id && (segment.id = segment.parentFlushed ? boundary.rootSegmentID : request.nextSegmentId++);
            boundaryNode[3] = segment.id;
            return;
          }
        }
        -1 === segment.id && (segment.id = segment.parentFlushed && null !== boundary ? boundary.rootSegmentID : request.nextSegmentId++);
        if (-1 === task.childIndex)
          null === keyPath ? trackedPostpones.rootSlots = segment.id : (task = trackedPostpones.workingMap.get(keyPath), void 0 === task ? (task = [keyPath[1], keyPath[2], [], segment.id], addToReplayParent(task, keyPath[0], trackedPostpones)) : task[3] = segment.id);
        else {
          if (null === keyPath)
            if (request = trackedPostpones.rootSlots, null === request)
              request = trackedPostpones.rootSlots = {};
            else {
              if ("number" === typeof request)
                throw Error(formatProdErrorMessage(491));
            }
          else if (boundary = trackedPostpones.workingMap, boundaryNode = boundary.get(keyPath), void 0 === boundaryNode)
            request = {}, boundaryNode = [keyPath[1], keyPath[2], [], request], boundary.set(keyPath, boundaryNode), addToReplayParent(boundaryNode, keyPath[0], trackedPostpones);
          else if (request = boundaryNode[3], null === request)
            request = boundaryNode[3] = {};
          else if ("number" === typeof request)
            throw Error(formatProdErrorMessage(491));
          request[task.childIndex] = segment.id;
        }
      }
    }
    function untrackBoundary(request, boundary) {
      request = request.trackedPostpones;
      null !== request && (boundary = boundary.trackedContentKeyPath, null !== boundary && (boundary = request.workingMap.get(boundary), void 0 !== boundary && (boundary.length = 4, boundary[2] = [], boundary[3] = null)));
    }
    function spawnNewSuspendedReplayTask(request, task, thenableState2) {
      return createReplayTask(
        request,
        thenableState2,
        task.replay,
        task.node,
        task.childIndex,
        task.blockedBoundary,
        task.hoistableState,
        task.abortSet,
        task.keyPath,
        task.formatContext,
        task.context,
        task.treeContext,
        task.row,
        task.componentStack
      );
    }
    function spawnNewSuspendedRenderTask(request, task, thenableState2) {
      var segment = task.blockedSegment, newSegment = createPendingSegment(
        request,
        segment.chunks.length,
        null,
        task.formatContext,
        segment.lastPushedText,
        true
      );
      segment.children.push(newSegment);
      segment.lastPushedText = false;
      return createRenderTask(
        request,
        thenableState2,
        task.node,
        task.childIndex,
        task.blockedBoundary,
        newSegment,
        task.blockedPreamble,
        task.hoistableState,
        task.abortSet,
        task.keyPath,
        task.formatContext,
        task.context,
        task.treeContext,
        task.row,
        task.componentStack
      );
    }
    function renderNode(request, task, node, childIndex) {
      var previousFormatContext = task.formatContext, previousContext = task.context, previousKeyPath = task.keyPath, previousTreeContext = task.treeContext, previousComponentStack = task.componentStack, segment = task.blockedSegment;
      if (null === segment) {
        segment = task.replay;
        try {
          return renderNodeDestructive(request, task, node, childIndex);
        } catch (thrownValue) {
          if (resetHooksState(), node = thrownValue === SuspenseException ? getSuspendedThenable() : thrownValue, 12 !== request.status && "object" === typeof node && null !== node) {
            if ("function" === typeof node.then) {
              childIndex = thrownValue === SuspenseException ? getThenableStateAfterSuspending() : null;
              request = spawnNewSuspendedReplayTask(request, task, childIndex).ping;
              node.then(request, request);
              task.formatContext = previousFormatContext;
              task.context = previousContext;
              task.keyPath = previousKeyPath;
              task.treeContext = previousTreeContext;
              task.componentStack = previousComponentStack;
              task.replay = segment;
              switchContext(previousContext);
              return;
            }
            if ("Maximum call stack size exceeded" === node.message) {
              node = thrownValue === SuspenseException ? getThenableStateAfterSuspending() : null;
              node = spawnNewSuspendedReplayTask(request, task, node);
              request.pingedTasks.push(node);
              task.formatContext = previousFormatContext;
              task.context = previousContext;
              task.keyPath = previousKeyPath;
              task.treeContext = previousTreeContext;
              task.componentStack = previousComponentStack;
              task.replay = segment;
              switchContext(previousContext);
              return;
            }
          }
        }
      } else {
        var childrenLength = segment.children.length, chunkLength = segment.chunks.length;
        try {
          return renderNodeDestructive(request, task, node, childIndex);
        } catch (thrownValue$62) {
          if (resetHooksState(), segment.children.length = childrenLength, segment.chunks.length = chunkLength, node = thrownValue$62 === SuspenseException ? getSuspendedThenable() : thrownValue$62, 12 !== request.status && "object" === typeof node && null !== node) {
            if ("function" === typeof node.then) {
              segment = node;
              node = thrownValue$62 === SuspenseException ? getThenableStateAfterSuspending() : null;
              request = spawnNewSuspendedRenderTask(request, task, node).ping;
              segment.then(request, request);
              task.formatContext = previousFormatContext;
              task.context = previousContext;
              task.keyPath = previousKeyPath;
              task.treeContext = previousTreeContext;
              task.componentStack = previousComponentStack;
              switchContext(previousContext);
              return;
            }
            if ("Maximum call stack size exceeded" === node.message) {
              segment = thrownValue$62 === SuspenseException ? getThenableStateAfterSuspending() : null;
              segment = spawnNewSuspendedRenderTask(request, task, segment);
              request.pingedTasks.push(segment);
              task.formatContext = previousFormatContext;
              task.context = previousContext;
              task.keyPath = previousKeyPath;
              task.treeContext = previousTreeContext;
              task.componentStack = previousComponentStack;
              switchContext(previousContext);
              return;
            }
          }
        }
      }
      task.formatContext = previousFormatContext;
      task.context = previousContext;
      task.keyPath = previousKeyPath;
      task.treeContext = previousTreeContext;
      switchContext(previousContext);
      throw node;
    }
    function abortTaskSoft(task) {
      var boundary = task.blockedBoundary, segment = task.blockedSegment;
      null !== segment && (segment.status = 3, finishedTask(this, boundary, task.row, segment));
    }
    function abortRemainingReplayNodes(request$jscomp$0, boundary, nodes, slots, error, errorDigest$jscomp$0) {
      for (var i = 0; i < nodes.length; i++) {
        var node = nodes[i];
        if (4 === node.length)
          abortRemainingReplayNodes(
            request$jscomp$0,
            boundary,
            node[2],
            node[3],
            error,
            errorDigest$jscomp$0
          );
        else {
          node = node[5];
          var request = request$jscomp$0, errorDigest = errorDigest$jscomp$0, resumedBoundary = createSuspenseBoundary(
            request,
            null,
            /* @__PURE__ */ new Set(),
            null,
            null
          );
          resumedBoundary.parentFlushed = true;
          resumedBoundary.rootSegmentID = node;
          resumedBoundary.status = 4;
          resumedBoundary.errorDigest = errorDigest;
          resumedBoundary.parentFlushed && request.clientRenderedBoundaries.push(resumedBoundary);
        }
      }
      nodes.length = 0;
      if (null !== slots) {
        if (null === boundary) throw Error(formatProdErrorMessage(487));
        4 !== boundary.status && (boundary.status = 4, boundary.errorDigest = errorDigest$jscomp$0, boundary.parentFlushed && request$jscomp$0.clientRenderedBoundaries.push(boundary));
        if ("object" === typeof slots) for (var index in slots) delete slots[index];
      }
    }
    function abortTask(task, request, error) {
      var boundary = task.blockedBoundary, segment = task.blockedSegment;
      if (null !== segment) {
        if (6 === segment.status) return;
        segment.status = 3;
      }
      var errorInfo = getThrownInfo(task.componentStack);
      if (null === boundary) {
        if (13 !== request.status && 14 !== request.status) {
          boundary = task.replay;
          if (null === boundary) {
            null !== request.trackedPostpones && null !== segment ? (boundary = request.trackedPostpones, logRecoverableError(request, error, errorInfo), trackPostpone(request, boundary, task, segment), finishedTask(request, null, task.row, segment)) : (logRecoverableError(request, error, errorInfo), fatalError(request, error));
            return;
          }
          boundary.pendingTasks--;
          0 === boundary.pendingTasks && 0 < boundary.nodes.length && (segment = logRecoverableError(request, error, errorInfo), abortRemainingReplayNodes(
            request,
            null,
            boundary.nodes,
            boundary.slots,
            error,
            segment
          ));
          request.pendingRootTasks--;
          0 === request.pendingRootTasks && completeShell(request);
        }
      } else {
        var trackedPostpones$63 = request.trackedPostpones;
        if (4 !== boundary.status) {
          if (null !== trackedPostpones$63 && null !== segment)
            return logRecoverableError(request, error, errorInfo), trackPostpone(request, trackedPostpones$63, task, segment), boundary.fallbackAbortableTasks.forEach(function(fallbackTask) {
              return abortTask(fallbackTask, request, error);
            }), boundary.fallbackAbortableTasks.clear(), finishedTask(request, boundary, task.row, segment);
          boundary.status = 4;
          segment = logRecoverableError(request, error, errorInfo);
          boundary.status = 4;
          boundary.errorDigest = segment;
          untrackBoundary(request, boundary);
          boundary.parentFlushed && request.clientRenderedBoundaries.push(boundary);
        }
        boundary.pendingTasks--;
        segment = boundary.row;
        null !== segment && 0 === --segment.pendingTasks && finishSuspenseListRow(request, segment);
        boundary.fallbackAbortableTasks.forEach(function(fallbackTask) {
          return abortTask(fallbackTask, request, error);
        });
        boundary.fallbackAbortableTasks.clear();
      }
      task = task.row;
      null !== task && 0 === --task.pendingTasks && finishSuspenseListRow(request, task);
      request.allPendingTasks--;
      0 === request.allPendingTasks && completeAll(request);
    }
    function safelyEmitEarlyPreloads(request, shellComplete) {
      try {
        var renderState = request.renderState, onHeaders = renderState.onHeaders;
        if (onHeaders) {
          var headers = renderState.headers;
          if (headers) {
            renderState.headers = null;
            var linkHeader = headers.preconnects;
            headers.fontPreloads && (linkHeader && (linkHeader += ", "), linkHeader += headers.fontPreloads);
            headers.highImagePreloads && (linkHeader && (linkHeader += ", "), linkHeader += headers.highImagePreloads);
            if (!shellComplete) {
              var queueIter = renderState.styles.values(), queueStep = queueIter.next();
              b: for (; 0 < headers.remainingCapacity && !queueStep.done; queueStep = queueIter.next())
                for (var sheetIter = queueStep.value.sheets.values(), sheetStep = sheetIter.next(); 0 < headers.remainingCapacity && !sheetStep.done; sheetStep = sheetIter.next()) {
                  var sheet = sheetStep.value, props = sheet.props, key = props.href, props$jscomp$0 = sheet.props, header = getPreloadAsHeader(props$jscomp$0.href, "style", {
                    crossOrigin: props$jscomp$0.crossOrigin,
                    integrity: props$jscomp$0.integrity,
                    nonce: props$jscomp$0.nonce,
                    type: props$jscomp$0.type,
                    fetchPriority: props$jscomp$0.fetchPriority,
                    referrerPolicy: props$jscomp$0.referrerPolicy,
                    media: props$jscomp$0.media
                  });
                  if (0 <= (headers.remainingCapacity -= header.length + 2))
                    renderState.resets.style[key] = PRELOAD_NO_CREDS, linkHeader && (linkHeader += ", "), linkHeader += header, renderState.resets.style[key] = "string" === typeof props.crossOrigin || "string" === typeof props.integrity ? [props.crossOrigin, props.integrity] : PRELOAD_NO_CREDS;
                  else break b;
                }
            }
            linkHeader ? onHeaders({ Link: linkHeader }) : onHeaders({});
          }
        }
      } catch (error) {
        logRecoverableError(request, error, {});
      }
    }
    function completeShell(request) {
      null === request.trackedPostpones && safelyEmitEarlyPreloads(request, true);
      null === request.trackedPostpones && preparePreamble(request);
      request.onShellError = noop;
      request = request.onShellReady;
      request();
    }
    function completeAll(request) {
      safelyEmitEarlyPreloads(
        request,
        null === request.trackedPostpones ? true : null === request.completedRootSegment || 5 !== request.completedRootSegment.status
      );
      preparePreamble(request);
      request = request.onAllReady;
      request();
    }
    function queueCompletedSegment(boundary, segment) {
      if (0 === segment.chunks.length && 1 === segment.children.length && null === segment.children[0].boundary && -1 === segment.children[0].id) {
        var childSegment = segment.children[0];
        childSegment.id = segment.id;
        childSegment.parentFlushed = true;
        1 !== childSegment.status && 3 !== childSegment.status && 4 !== childSegment.status || queueCompletedSegment(boundary, childSegment);
      } else boundary.completedSegments.push(segment);
    }
    function finishedTask(request, boundary, row, segment) {
      null !== row && (0 === --row.pendingTasks ? finishSuspenseListRow(request, row) : row.together && tryToResolveTogetherRow(request, row));
      request.allPendingTasks--;
      if (null === boundary) {
        if (null !== segment && segment.parentFlushed) {
          if (null !== request.completedRootSegment)
            throw Error(formatProdErrorMessage(389));
          request.completedRootSegment = segment;
        }
        request.pendingRootTasks--;
        0 === request.pendingRootTasks && completeShell(request);
      } else if (boundary.pendingTasks--, 4 !== boundary.status)
        if (0 === boundary.pendingTasks)
          if (0 === boundary.status && (boundary.status = 1), null !== segment && segment.parentFlushed && (1 === segment.status || 3 === segment.status) && queueCompletedSegment(boundary, segment), boundary.parentFlushed && request.completedBoundaries.push(boundary), 1 === boundary.status)
            row = boundary.row, null !== row && hoistHoistables(row.hoistables, boundary.contentState), isEligibleForOutlining(request, boundary) || (boundary.fallbackAbortableTasks.forEach(abortTaskSoft, request), boundary.fallbackAbortableTasks.clear(), null !== row && 0 === --row.pendingTasks && finishSuspenseListRow(request, row)), 0 === request.pendingRootTasks && null === request.trackedPostpones && null !== boundary.contentPreamble && preparePreamble(request);
          else {
            if (5 === boundary.status && (boundary = boundary.row, null !== boundary)) {
              if (null !== request.trackedPostpones) {
                row = request.trackedPostpones;
                var postponedRow = boundary.next;
                if (null !== postponedRow && (segment = postponedRow.boundaries, null !== segment))
                  for (postponedRow.boundaries = null, postponedRow = 0; postponedRow < segment.length; postponedRow++) {
                    var postponedBoundary = segment[postponedRow];
                    trackPostponedBoundary(request, row, postponedBoundary);
                    finishedTask(request, postponedBoundary, null, null);
                  }
              }
              0 === --boundary.pendingTasks && finishSuspenseListRow(request, boundary);
            }
          }
        else
          null === segment || !segment.parentFlushed || 1 !== segment.status && 3 !== segment.status || (queueCompletedSegment(boundary, segment), 1 === boundary.completedSegments.length && boundary.parentFlushed && request.partialBoundaries.push(boundary)), boundary = boundary.row, null !== boundary && boundary.together && tryToResolveTogetherRow(request, boundary);
      0 === request.allPendingTasks && completeAll(request);
    }
    function performWork(request$jscomp$2) {
      if (14 !== request$jscomp$2.status && 13 !== request$jscomp$2.status) {
        var prevContext = currentActiveSnapshot, prevDispatcher = ReactSharedInternals.H;
        ReactSharedInternals.H = HooksDispatcher;
        var prevAsyncDispatcher = ReactSharedInternals.A;
        ReactSharedInternals.A = DefaultAsyncDispatcher;
        var prevRequest = currentRequest;
        currentRequest = request$jscomp$2;
        var prevResumableState = currentResumableState;
        currentResumableState = request$jscomp$2.resumableState;
        try {
          var pingedTasks = request$jscomp$2.pingedTasks, i;
          for (i = 0; i < pingedTasks.length; i++) {
            var task = pingedTasks[i], request = request$jscomp$2, segment = task.blockedSegment;
            if (null === segment) {
              var request$jscomp$0 = request;
              if (0 !== task.replay.pendingTasks) {
                switchContext(task.context);
                try {
                  "number" === typeof task.replay.slots ? resumeNode(
                    request$jscomp$0,
                    task,
                    task.replay.slots,
                    task.node,
                    task.childIndex
                  ) : retryNode(request$jscomp$0, task);
                  if (1 === task.replay.pendingTasks && 0 < task.replay.nodes.length)
                    throw Error(formatProdErrorMessage(488));
                  task.replay.pendingTasks--;
                  task.abortSet.delete(task);
                  finishedTask(
                    request$jscomp$0,
                    task.blockedBoundary,
                    task.row,
                    null
                  );
                } catch (thrownValue) {
                  resetHooksState();
                  var x = thrownValue === SuspenseException ? getSuspendedThenable() : thrownValue;
                  if ("object" === typeof x && null !== x && "function" === typeof x.then) {
                    var ping = task.ping;
                    x.then(ping, ping);
                    task.thenableState = thrownValue === SuspenseException ? getThenableStateAfterSuspending() : null;
                  } else {
                    task.replay.pendingTasks--;
                    task.abortSet.delete(task);
                    var errorInfo = getThrownInfo(task.componentStack);
                    request = void 0;
                    var request$jscomp$1 = request$jscomp$0, boundary = task.blockedBoundary, error$jscomp$0 = 12 === request$jscomp$0.status ? request$jscomp$0.fatalError : x, replayNodes = task.replay.nodes, resumeSlots = task.replay.slots;
                    request = logRecoverableError(
                      request$jscomp$1,
                      error$jscomp$0,
                      errorInfo
                    );
                    abortRemainingReplayNodes(
                      request$jscomp$1,
                      boundary,
                      replayNodes,
                      resumeSlots,
                      error$jscomp$0,
                      request
                    );
                    request$jscomp$0.pendingRootTasks--;
                    0 === request$jscomp$0.pendingRootTasks && completeShell(request$jscomp$0);
                    request$jscomp$0.allPendingTasks--;
                    0 === request$jscomp$0.allPendingTasks && completeAll(request$jscomp$0);
                  }
                } finally {
                }
              }
            } else if (request$jscomp$0 = void 0, request$jscomp$1 = segment, 0 === request$jscomp$1.status) {
              request$jscomp$1.status = 6;
              switchContext(task.context);
              var childrenLength = request$jscomp$1.children.length, chunkLength = request$jscomp$1.chunks.length;
              try {
                retryNode(request, task), pushSegmentFinale(
                  request$jscomp$1.chunks,
                  request.renderState,
                  request$jscomp$1.lastPushedText,
                  request$jscomp$1.textEmbedded
                ), task.abortSet.delete(task), request$jscomp$1.status = 1, finishedTask(
                  request,
                  task.blockedBoundary,
                  task.row,
                  request$jscomp$1
                );
              } catch (thrownValue) {
                resetHooksState();
                request$jscomp$1.children.length = childrenLength;
                request$jscomp$1.chunks.length = chunkLength;
                var x$jscomp$0 = thrownValue === SuspenseException ? getSuspendedThenable() : 12 === request.status ? request.fatalError : thrownValue;
                if (12 === request.status && null !== request.trackedPostpones) {
                  var trackedPostpones = request.trackedPostpones, thrownInfo = getThrownInfo(task.componentStack);
                  task.abortSet.delete(task);
                  logRecoverableError(request, x$jscomp$0, thrownInfo);
                  trackPostpone(request, trackedPostpones, task, request$jscomp$1);
                  finishedTask(
                    request,
                    task.blockedBoundary,
                    task.row,
                    request$jscomp$1
                  );
                } else if ("object" === typeof x$jscomp$0 && null !== x$jscomp$0 && "function" === typeof x$jscomp$0.then) {
                  request$jscomp$1.status = 0;
                  task.thenableState = thrownValue === SuspenseException ? getThenableStateAfterSuspending() : null;
                  var ping$jscomp$0 = task.ping;
                  x$jscomp$0.then(ping$jscomp$0, ping$jscomp$0);
                } else {
                  var errorInfo$jscomp$0 = getThrownInfo(task.componentStack);
                  task.abortSet.delete(task);
                  request$jscomp$1.status = 4;
                  var boundary$jscomp$0 = task.blockedBoundary, row = task.row;
                  null !== row && 0 === --row.pendingTasks && finishSuspenseListRow(request, row);
                  request.allPendingTasks--;
                  request$jscomp$0 = logRecoverableError(
                    request,
                    x$jscomp$0,
                    errorInfo$jscomp$0
                  );
                  if (null === boundary$jscomp$0) fatalError(request, x$jscomp$0);
                  else if (boundary$jscomp$0.pendingTasks--, 4 !== boundary$jscomp$0.status) {
                    boundary$jscomp$0.status = 4;
                    boundary$jscomp$0.errorDigest = request$jscomp$0;
                    untrackBoundary(request, boundary$jscomp$0);
                    var boundaryRow = boundary$jscomp$0.row;
                    null !== boundaryRow && 0 === --boundaryRow.pendingTasks && finishSuspenseListRow(request, boundaryRow);
                    boundary$jscomp$0.parentFlushed && request.clientRenderedBoundaries.push(boundary$jscomp$0);
                    0 === request.pendingRootTasks && null === request.trackedPostpones && null !== boundary$jscomp$0.contentPreamble && preparePreamble(request);
                  }
                  0 === request.allPendingTasks && completeAll(request);
                }
              } finally {
              }
            }
          }
          pingedTasks.splice(0, i);
          null !== request$jscomp$2.destination && flushCompletedQueues(request$jscomp$2, request$jscomp$2.destination);
        } catch (error) {
          logRecoverableError(request$jscomp$2, error, {}), fatalError(request$jscomp$2, error);
        } finally {
          currentResumableState = prevResumableState, ReactSharedInternals.H = prevDispatcher, ReactSharedInternals.A = prevAsyncDispatcher, prevDispatcher === HooksDispatcher && switchContext(prevContext), currentRequest = prevRequest;
        }
      }
    }
    function preparePreambleFromSubtree(request, segment, collectedPreambleSegments) {
      segment.preambleChildren.length && collectedPreambleSegments.push(segment.preambleChildren);
      for (var pendingPreambles = false, i = 0; i < segment.children.length; i++)
        pendingPreambles = preparePreambleFromSegment(
          request,
          segment.children[i],
          collectedPreambleSegments
        ) || pendingPreambles;
      return pendingPreambles;
    }
    function preparePreambleFromSegment(request, segment, collectedPreambleSegments) {
      var boundary = segment.boundary;
      if (null === boundary)
        return preparePreambleFromSubtree(
          request,
          segment,
          collectedPreambleSegments
        );
      var preamble = boundary.contentPreamble, fallbackPreamble = boundary.fallbackPreamble;
      if (null === preamble || null === fallbackPreamble) return false;
      switch (boundary.status) {
        case 1:
          hoistPreambleState(request.renderState, preamble);
          request.byteSize += boundary.byteSize;
          segment = boundary.completedSegments[0];
          if (!segment) throw Error(formatProdErrorMessage(391));
          return preparePreambleFromSubtree(
            request,
            segment,
            collectedPreambleSegments
          );
        case 5:
          if (null !== request.trackedPostpones) return true;
        case 4:
          if (1 === segment.status)
            return hoistPreambleState(request.renderState, fallbackPreamble), preparePreambleFromSubtree(
              request,
              segment,
              collectedPreambleSegments
            );
        default:
          return true;
      }
    }
    function preparePreamble(request) {
      if (request.completedRootSegment && null === request.completedPreambleSegments) {
        var collectedPreambleSegments = [], originalRequestByteSize = request.byteSize, hasPendingPreambles = preparePreambleFromSegment(
          request,
          request.completedRootSegment,
          collectedPreambleSegments
        ), preamble = request.renderState.preamble;
        false === hasPendingPreambles || preamble.headChunks && preamble.bodyChunks ? request.completedPreambleSegments = collectedPreambleSegments : request.byteSize = originalRequestByteSize;
      }
    }
    function flushSubtree(request, destination, segment, hoistableState) {
      segment.parentFlushed = true;
      switch (segment.status) {
        case 0:
          segment.id = request.nextSegmentId++;
        case 5:
          return hoistableState = segment.id, segment.lastPushedText = false, segment.textEmbedded = false, request = request.renderState, destination.push('<template id="'), destination.push(request.placeholderPrefix), request = hoistableState.toString(16), destination.push(request), destination.push('"></template>');
        case 1:
          segment.status = 2;
          var r = true, chunks = segment.chunks, chunkIdx = 0;
          segment = segment.children;
          for (var childIdx = 0; childIdx < segment.length; childIdx++) {
            for (r = segment[childIdx]; chunkIdx < r.index; chunkIdx++)
              destination.push(chunks[chunkIdx]);
            r = flushSegment(request, destination, r, hoistableState);
          }
          for (; chunkIdx < chunks.length - 1; chunkIdx++)
            destination.push(chunks[chunkIdx]);
          chunkIdx < chunks.length && (r = destination.push(chunks[chunkIdx]));
          return r;
        case 3:
          return true;
        default:
          throw Error(formatProdErrorMessage(390));
      }
    }
    var flushedByteSize = 0;
    function flushSegment(request, destination, segment, hoistableState) {
      var boundary = segment.boundary;
      if (null === boundary)
        return flushSubtree(request, destination, segment, hoistableState);
      boundary.parentFlushed = true;
      if (4 === boundary.status) {
        var row = boundary.row;
        null !== row && 0 === --row.pendingTasks && finishSuspenseListRow(request, row);
        request.renderState.generateStaticMarkup || (boundary = boundary.errorDigest, destination.push("<!--$!-->"), destination.push("<template"), boundary && (destination.push(' data-dgst="'), boundary = escapeTextForBrowser(boundary), destination.push(boundary), destination.push('"')), destination.push("></template>"));
        flushSubtree(request, destination, segment, hoistableState);
        request = request.renderState.generateStaticMarkup ? true : destination.push("<!--/$-->");
        return request;
      }
      if (1 !== boundary.status)
        return 0 === boundary.status && (boundary.rootSegmentID = request.nextSegmentId++), 0 < boundary.completedSegments.length && request.partialBoundaries.push(boundary), writeStartPendingSuspenseBoundary(
          destination,
          request.renderState,
          boundary.rootSegmentID
        ), hoistableState && hoistHoistables(hoistableState, boundary.fallbackState), flushSubtree(request, destination, segment, hoistableState), destination.push("<!--/$-->");
      if (!flushingPartialBoundaries && isEligibleForOutlining(request, boundary) && flushedByteSize + boundary.byteSize > request.progressiveChunkSize)
        return boundary.rootSegmentID = request.nextSegmentId++, request.completedBoundaries.push(boundary), writeStartPendingSuspenseBoundary(
          destination,
          request.renderState,
          boundary.rootSegmentID
        ), flushSubtree(request, destination, segment, hoistableState), destination.push("<!--/$-->");
      flushedByteSize += boundary.byteSize;
      hoistableState && hoistHoistables(hoistableState, boundary.contentState);
      segment = boundary.row;
      null !== segment && isEligibleForOutlining(request, boundary) && 0 === --segment.pendingTasks && finishSuspenseListRow(request, segment);
      request.renderState.generateStaticMarkup || destination.push("<!--$-->");
      segment = boundary.completedSegments;
      if (1 !== segment.length) throw Error(formatProdErrorMessage(391));
      flushSegment(request, destination, segment[0], hoistableState);
      request = request.renderState.generateStaticMarkup ? true : destination.push("<!--/$-->");
      return request;
    }
    function flushSegmentContainer(request, destination, segment, hoistableState) {
      writeStartSegment(
        destination,
        request.renderState,
        segment.parentFormatContext,
        segment.id
      );
      flushSegment(request, destination, segment, hoistableState);
      return writeEndSegment(destination, segment.parentFormatContext);
    }
    function flushCompletedBoundary(request, destination, boundary) {
      flushedByteSize = boundary.byteSize;
      for (var completedSegments = boundary.completedSegments, i = 0; i < completedSegments.length; i++)
        flushPartiallyCompletedSegment(
          request,
          destination,
          boundary,
          completedSegments[i]
        );
      completedSegments.length = 0;
      completedSegments = boundary.row;
      null !== completedSegments && isEligibleForOutlining(request, boundary) && 0 === --completedSegments.pendingTasks && finishSuspenseListRow(request, completedSegments);
      writeHoistablesForBoundary(
        destination,
        boundary.contentState,
        request.renderState
      );
      completedSegments = request.resumableState;
      request = request.renderState;
      i = boundary.rootSegmentID;
      boundary = boundary.contentState;
      var requiresStyleInsertion = request.stylesToHoist;
      request.stylesToHoist = false;
      destination.push(request.startInlineScript);
      destination.push(">");
      requiresStyleInsertion ? (0 === (completedSegments.instructions & 4) && (completedSegments.instructions |= 4, destination.push(
        '$RX=function(b,c,d,e,f){var a=document.getElementById(b);a&&(b=a.previousSibling,b.data="$!",a=a.dataset,c&&(a.dgst=c),d&&(a.msg=d),e&&(a.stck=e),f&&(a.cstck=f),b._reactRetry&&b._reactRetry())};'
      )), 0 === (completedSegments.instructions & 2) && (completedSegments.instructions |= 2, destination.push(
        '$RB=[];$RV=function(a){$RT=performance.now();for(var b=0;b<a.length;b+=2){var c=a[b],e=a[b+1];null!==e.parentNode&&e.parentNode.removeChild(e);var f=c.parentNode;if(f){var g=c.previousSibling,h=0;do{if(c&&8===c.nodeType){var d=c.data;if("/$"===d||"/&"===d)if(0===h)break;else h--;else"$"!==d&&"$?"!==d&&"$~"!==d&&"$!"!==d&&"&"!==d||h++}d=c.nextSibling;f.removeChild(c);c=d}while(c);for(;e.firstChild;)f.insertBefore(e.firstChild,c);g.data="$";g._reactRetry&&requestAnimationFrame(g._reactRetry)}}a.length=0};\n$RC=function(a,b){if(b=document.getElementById(b))(a=document.getElementById(a))?(a.previousSibling.data="$~",$RB.push(a,b),2===$RB.length&&("number"!==typeof $RT?requestAnimationFrame($RV.bind(null,$RB)):(a=performance.now(),setTimeout($RV.bind(null,$RB),2300>a&&2E3<a?2300-a:$RT+300-a)))):b.parentNode.removeChild(b)};'
      )), 0 === (completedSegments.instructions & 8) ? (completedSegments.instructions |= 8, destination.push(
        '$RM=new Map;$RR=function(n,w,p){function u(q){this._p=null;q()}for(var r=new Map,t=document,h,b,e=t.querySelectorAll("link[data-precedence],style[data-precedence]"),v=[],k=0;b=e[k++];)"not all"===b.getAttribute("media")?v.push(b):("LINK"===b.tagName&&$RM.set(b.getAttribute("href"),b),r.set(b.dataset.precedence,h=b));e=0;b=[];var l,a;for(k=!0;;){if(k){var f=p[e++];if(!f){k=!1;e=0;continue}var c=!1,m=0;var d=f[m++];if(a=$RM.get(d)){var g=a._p;c=!0}else{a=t.createElement("link");a.href=d;a.rel=\n"stylesheet";for(a.dataset.precedence=l=f[m++];g=f[m++];)a.setAttribute(g,f[m++]);g=a._p=new Promise(function(q,x){a.onload=u.bind(a,q);a.onerror=u.bind(a,x)});$RM.set(d,a)}d=a.getAttribute("media");!g||d&&!matchMedia(d).matches||b.push(g);if(c)continue}else{a=v[e++];if(!a)break;l=a.getAttribute("data-precedence");a.removeAttribute("media")}c=r.get(l)||h;c===h&&(h=a);r.set(l,a);c?c.parentNode.insertBefore(a,c.nextSibling):(c=t.head,c.insertBefore(a,c.firstChild))}if(p=document.getElementById(n))p.previousSibling.data=\n"$~";Promise.all(b).then($RC.bind(null,n,w),$RX.bind(null,n,"CSS failed to load"))};$RR("'
      )) : destination.push('$RR("')) : (0 === (completedSegments.instructions & 2) && (completedSegments.instructions |= 2, destination.push(
        '$RB=[];$RV=function(a){$RT=performance.now();for(var b=0;b<a.length;b+=2){var c=a[b],e=a[b+1];null!==e.parentNode&&e.parentNode.removeChild(e);var f=c.parentNode;if(f){var g=c.previousSibling,h=0;do{if(c&&8===c.nodeType){var d=c.data;if("/$"===d||"/&"===d)if(0===h)break;else h--;else"$"!==d&&"$?"!==d&&"$~"!==d&&"$!"!==d&&"&"!==d||h++}d=c.nextSibling;f.removeChild(c);c=d}while(c);for(;e.firstChild;)f.insertBefore(e.firstChild,c);g.data="$";g._reactRetry&&requestAnimationFrame(g._reactRetry)}}a.length=0};\n$RC=function(a,b){if(b=document.getElementById(b))(a=document.getElementById(a))?(a.previousSibling.data="$~",$RB.push(a,b),2===$RB.length&&("number"!==typeof $RT?requestAnimationFrame($RV.bind(null,$RB)):(a=performance.now(),setTimeout($RV.bind(null,$RB),2300>a&&2E3<a?2300-a:$RT+300-a)))):b.parentNode.removeChild(b)};'
      )), destination.push('$RC("'));
      completedSegments = i.toString(16);
      destination.push(request.boundaryPrefix);
      destination.push(completedSegments);
      destination.push('","');
      destination.push(request.segmentPrefix);
      destination.push(completedSegments);
      requiresStyleInsertion ? (destination.push('",'), writeStyleResourceDependenciesInJS(destination, boundary)) : destination.push('"');
      boundary = destination.push(")<\/script>");
      return writeBootstrap(destination, request) && boundary;
    }
    function flushPartiallyCompletedSegment(request, destination, boundary, segment) {
      if (2 === segment.status) return true;
      var hoistableState = boundary.contentState, segmentID = segment.id;
      if (-1 === segmentID) {
        if (-1 === (segment.id = boundary.rootSegmentID))
          throw Error(formatProdErrorMessage(392));
        return flushSegmentContainer(request, destination, segment, hoistableState);
      }
      if (segmentID === boundary.rootSegmentID)
        return flushSegmentContainer(request, destination, segment, hoistableState);
      flushSegmentContainer(request, destination, segment, hoistableState);
      boundary = request.resumableState;
      request = request.renderState;
      destination.push(request.startInlineScript);
      destination.push(">");
      0 === (boundary.instructions & 1) ? (boundary.instructions |= 1, destination.push(
        '$RS=function(a,b){a=document.getElementById(a);b=document.getElementById(b);for(a.parentNode.removeChild(a);a.firstChild;)b.parentNode.insertBefore(a.firstChild,b);b.parentNode.removeChild(b)};$RS("'
      )) : destination.push('$RS("');
      destination.push(request.segmentPrefix);
      segmentID = segmentID.toString(16);
      destination.push(segmentID);
      destination.push('","');
      destination.push(request.placeholderPrefix);
      destination.push(segmentID);
      destination = destination.push('")<\/script>');
      return destination;
    }
    var flushingPartialBoundaries = false;
    function flushCompletedQueues(request, destination) {
      try {
        if (!(0 < request.pendingRootTasks)) {
          var i, completedRootSegment = request.completedRootSegment;
          if (null !== completedRootSegment) {
            if (5 === completedRootSegment.status) return;
            var completedPreambleSegments = request.completedPreambleSegments;
            if (null === completedPreambleSegments) return;
            flushedByteSize = request.byteSize;
            var resumableState = request.resumableState, renderState = request.renderState, preamble = renderState.preamble, htmlChunks = preamble.htmlChunks, headChunks = preamble.headChunks, i$jscomp$0;
            if (htmlChunks) {
              for (i$jscomp$0 = 0; i$jscomp$0 < htmlChunks.length; i$jscomp$0++)
                destination.push(htmlChunks[i$jscomp$0]);
              if (headChunks)
                for (i$jscomp$0 = 0; i$jscomp$0 < headChunks.length; i$jscomp$0++)
                  destination.push(headChunks[i$jscomp$0]);
              else {
                var chunk = startChunkForTag("head");
                destination.push(chunk);
                destination.push(">");
              }
            } else if (headChunks)
              for (i$jscomp$0 = 0; i$jscomp$0 < headChunks.length; i$jscomp$0++)
                destination.push(headChunks[i$jscomp$0]);
            var charsetChunks = renderState.charsetChunks;
            for (i$jscomp$0 = 0; i$jscomp$0 < charsetChunks.length; i$jscomp$0++)
              destination.push(charsetChunks[i$jscomp$0]);
            charsetChunks.length = 0;
            renderState.preconnects.forEach(flushResource, destination);
            renderState.preconnects.clear();
            var viewportChunks = renderState.viewportChunks;
            for (i$jscomp$0 = 0; i$jscomp$0 < viewportChunks.length; i$jscomp$0++)
              destination.push(viewportChunks[i$jscomp$0]);
            viewportChunks.length = 0;
            renderState.fontPreloads.forEach(flushResource, destination);
            renderState.fontPreloads.clear();
            renderState.highImagePreloads.forEach(flushResource, destination);
            renderState.highImagePreloads.clear();
            currentlyFlushingRenderState = renderState;
            renderState.styles.forEach(flushStylesInPreamble, destination);
            currentlyFlushingRenderState = null;
            var importMapChunks = renderState.importMapChunks;
            for (i$jscomp$0 = 0; i$jscomp$0 < importMapChunks.length; i$jscomp$0++)
              destination.push(importMapChunks[i$jscomp$0]);
            importMapChunks.length = 0;
            renderState.bootstrapScripts.forEach(flushResource, destination);
            renderState.scripts.forEach(flushResource, destination);
            renderState.scripts.clear();
            renderState.bulkPreloads.forEach(flushResource, destination);
            renderState.bulkPreloads.clear();
            resumableState.instructions |= 32;
            var hoistableChunks = renderState.hoistableChunks;
            for (i$jscomp$0 = 0; i$jscomp$0 < hoistableChunks.length; i$jscomp$0++)
              destination.push(hoistableChunks[i$jscomp$0]);
            for (resumableState = hoistableChunks.length = 0; resumableState < completedPreambleSegments.length; resumableState++) {
              var segments = completedPreambleSegments[resumableState];
              for (renderState = 0; renderState < segments.length; renderState++)
                flushSegment(request, destination, segments[renderState], null);
            }
            var preamble$jscomp$0 = request.renderState.preamble, headChunks$jscomp$0 = preamble$jscomp$0.headChunks;
            if (preamble$jscomp$0.htmlChunks || headChunks$jscomp$0) {
              var chunk$jscomp$0 = endChunkForTag("head");
              destination.push(chunk$jscomp$0);
            }
            var bodyChunks = preamble$jscomp$0.bodyChunks;
            if (bodyChunks)
              for (completedPreambleSegments = 0; completedPreambleSegments < bodyChunks.length; completedPreambleSegments++)
                destination.push(bodyChunks[completedPreambleSegments]);
            flushSegment(request, destination, completedRootSegment, null);
            request.completedRootSegment = null;
            var renderState$jscomp$0 = request.renderState;
            if (0 !== request.allPendingTasks || 0 !== request.clientRenderedBoundaries.length || 0 !== request.completedBoundaries.length || null !== request.trackedPostpones && (0 !== request.trackedPostpones.rootNodes.length || null !== request.trackedPostpones.rootSlots)) {
              var resumableState$jscomp$0 = request.resumableState;
              if (0 === (resumableState$jscomp$0.instructions & 64)) {
                resumableState$jscomp$0.instructions |= 64;
                destination.push(renderState$jscomp$0.startInlineScript);
                if (0 === (resumableState$jscomp$0.instructions & 32)) {
                  resumableState$jscomp$0.instructions |= 32;
                  var shellId = "_" + resumableState$jscomp$0.idPrefix + "R_";
                  destination.push(' id="');
                  var chunk$jscomp$1 = escapeTextForBrowser(shellId);
                  destination.push(chunk$jscomp$1);
                  destination.push('"');
                }
                destination.push(">");
                destination.push(
                  "requestAnimationFrame(function(){$RT=performance.now()});"
                );
                destination.push("<\/script>");
              }
            }
            writeBootstrap(destination, renderState$jscomp$0);
          }
          var renderState$jscomp$1 = request.renderState;
          completedRootSegment = 0;
          var viewportChunks$jscomp$0 = renderState$jscomp$1.viewportChunks;
          for (completedRootSegment = 0; completedRootSegment < viewportChunks$jscomp$0.length; completedRootSegment++)
            destination.push(viewportChunks$jscomp$0[completedRootSegment]);
          viewportChunks$jscomp$0.length = 0;
          renderState$jscomp$1.preconnects.forEach(flushResource, destination);
          renderState$jscomp$1.preconnects.clear();
          renderState$jscomp$1.fontPreloads.forEach(flushResource, destination);
          renderState$jscomp$1.fontPreloads.clear();
          renderState$jscomp$1.highImagePreloads.forEach(
            flushResource,
            destination
          );
          renderState$jscomp$1.highImagePreloads.clear();
          renderState$jscomp$1.styles.forEach(preloadLateStyles, destination);
          renderState$jscomp$1.scripts.forEach(flushResource, destination);
          renderState$jscomp$1.scripts.clear();
          renderState$jscomp$1.bulkPreloads.forEach(flushResource, destination);
          renderState$jscomp$1.bulkPreloads.clear();
          var hoistableChunks$jscomp$0 = renderState$jscomp$1.hoistableChunks;
          for (completedRootSegment = 0; completedRootSegment < hoistableChunks$jscomp$0.length; completedRootSegment++)
            destination.push(hoistableChunks$jscomp$0[completedRootSegment]);
          hoistableChunks$jscomp$0.length = 0;
          var clientRenderedBoundaries = request.clientRenderedBoundaries;
          for (i = 0; i < clientRenderedBoundaries.length; i++) {
            var boundary = clientRenderedBoundaries[i];
            renderState$jscomp$1 = destination;
            var resumableState$jscomp$1 = request.resumableState, renderState$jscomp$2 = request.renderState, id = boundary.rootSegmentID, errorDigest = boundary.errorDigest;
            renderState$jscomp$1.push(renderState$jscomp$2.startInlineScript);
            renderState$jscomp$1.push(">");
            0 === (resumableState$jscomp$1.instructions & 4) ? (resumableState$jscomp$1.instructions |= 4, renderState$jscomp$1.push(
              '$RX=function(b,c,d,e,f){var a=document.getElementById(b);a&&(b=a.previousSibling,b.data="$!",a=a.dataset,c&&(a.dgst=c),d&&(a.msg=d),e&&(a.stck=e),f&&(a.cstck=f),b._reactRetry&&b._reactRetry())};;$RX("'
            )) : renderState$jscomp$1.push('$RX("');
            renderState$jscomp$1.push(renderState$jscomp$2.boundaryPrefix);
            var chunk$jscomp$2 = id.toString(16);
            renderState$jscomp$1.push(chunk$jscomp$2);
            renderState$jscomp$1.push('"');
            if (errorDigest) {
              renderState$jscomp$1.push(",");
              var chunk$jscomp$3 = escapeJSStringsForInstructionScripts(
                errorDigest || ""
              );
              renderState$jscomp$1.push(chunk$jscomp$3);
            }
            var JSCompiler_inline_result = renderState$jscomp$1.push(")<\/script>");
            if (!JSCompiler_inline_result) {
              request.destination = null;
              i++;
              clientRenderedBoundaries.splice(0, i);
              return;
            }
          }
          clientRenderedBoundaries.splice(0, i);
          var completedBoundaries = request.completedBoundaries;
          for (i = 0; i < completedBoundaries.length; i++)
            if (!flushCompletedBoundary(request, destination, completedBoundaries[i])) {
              request.destination = null;
              i++;
              completedBoundaries.splice(0, i);
              return;
            }
          completedBoundaries.splice(0, i);
          flushingPartialBoundaries = true;
          var partialBoundaries = request.partialBoundaries;
          for (i = 0; i < partialBoundaries.length; i++) {
            var boundary$69 = partialBoundaries[i];
            a: {
              clientRenderedBoundaries = request;
              boundary = destination;
              flushedByteSize = boundary$69.byteSize;
              var completedSegments = boundary$69.completedSegments;
              for (JSCompiler_inline_result = 0; JSCompiler_inline_result < completedSegments.length; JSCompiler_inline_result++)
                if (!flushPartiallyCompletedSegment(
                  clientRenderedBoundaries,
                  boundary,
                  boundary$69,
                  completedSegments[JSCompiler_inline_result]
                )) {
                  JSCompiler_inline_result++;
                  completedSegments.splice(0, JSCompiler_inline_result);
                  var JSCompiler_inline_result$jscomp$0 = false;
                  break a;
                }
              completedSegments.splice(0, JSCompiler_inline_result);
              var row = boundary$69.row;
              null !== row && row.together && 1 === boundary$69.pendingTasks && (1 === row.pendingTasks ? unblockSuspenseListRow(
                clientRenderedBoundaries,
                row,
                row.hoistables
              ) : row.pendingTasks--);
              JSCompiler_inline_result$jscomp$0 = writeHoistablesForBoundary(
                boundary,
                boundary$69.contentState,
                clientRenderedBoundaries.renderState
              );
            }
            if (!JSCompiler_inline_result$jscomp$0) {
              request.destination = null;
              i++;
              partialBoundaries.splice(0, i);
              return;
            }
          }
          partialBoundaries.splice(0, i);
          flushingPartialBoundaries = false;
          var largeBoundaries = request.completedBoundaries;
          for (i = 0; i < largeBoundaries.length; i++)
            if (!flushCompletedBoundary(request, destination, largeBoundaries[i])) {
              request.destination = null;
              i++;
              largeBoundaries.splice(0, i);
              return;
            }
          largeBoundaries.splice(0, i);
        }
      } finally {
        flushingPartialBoundaries = false, 0 === request.allPendingTasks && 0 === request.clientRenderedBoundaries.length && 0 === request.completedBoundaries.length && (request.flushScheduled = false, i = request.resumableState, i.hasBody && (partialBoundaries = endChunkForTag("body"), destination.push(partialBoundaries)), i.hasHtml && (i = endChunkForTag("html"), destination.push(i)), request.status = 14, destination.push(null), request.destination = null);
      }
    }
    function enqueueFlush(request) {
      if (false === request.flushScheduled && 0 === request.pingedTasks.length && null !== request.destination) {
        request.flushScheduled = true;
        var destination = request.destination;
        destination ? flushCompletedQueues(request, destination) : request.flushScheduled = false;
      }
    }
    function startFlowing(request, destination) {
      if (13 === request.status)
        request.status = 14, destination.destroy(request.fatalError);
      else if (14 !== request.status && null === request.destination) {
        request.destination = destination;
        try {
          flushCompletedQueues(request, destination);
        } catch (error) {
          logRecoverableError(request, error, {}), fatalError(request, error);
        }
      }
    }
    function abort(request, reason) {
      if (11 === request.status || 10 === request.status) request.status = 12;
      try {
        var abortableTasks = request.abortableTasks;
        if (0 < abortableTasks.size) {
          var error = void 0 === reason ? Error(formatProdErrorMessage(432)) : "object" === typeof reason && null !== reason && "function" === typeof reason.then ? Error(formatProdErrorMessage(530)) : reason;
          request.fatalError = error;
          abortableTasks.forEach(function(task) {
            return abortTask(task, request, error);
          });
          abortableTasks.clear();
        }
        null !== request.destination && flushCompletedQueues(request, request.destination);
      } catch (error$71) {
        logRecoverableError(request, error$71, {}), fatalError(request, error$71);
      }
    }
    function addToReplayParent(node, parentKeyPath, trackedPostpones) {
      if (null === parentKeyPath) trackedPostpones.rootNodes.push(node);
      else {
        var workingMap = trackedPostpones.workingMap, parentNode = workingMap.get(parentKeyPath);
        void 0 === parentNode && (parentNode = [parentKeyPath[1], parentKeyPath[2], [], null], workingMap.set(parentKeyPath, parentNode), addToReplayParent(parentNode, parentKeyPath[0], trackedPostpones));
        parentNode[2].push(node);
      }
    }
    function onError() {
    }
    function renderToStringImpl(children, options, generateStaticMarkup, abortReason) {
      var didFatal = false, fatalError2 = null, result = "", readyToStream = false;
      options = createResumableState(options ? options.identifierPrefix : void 0);
      children = createRequest(
        children,
        options,
        createRenderState(options, generateStaticMarkup),
        createFormatContext(0, null, 0, null),
        Infinity,
        onError,
        void 0,
        function() {
          readyToStream = true;
        },
        void 0,
        void 0,
        void 0
      );
      children.flushScheduled = null !== children.destination;
      performWork(children);
      10 === children.status && (children.status = 11);
      null === children.trackedPostpones && safelyEmitEarlyPreloads(children, 0 === children.pendingRootTasks);
      abort(children, abortReason);
      startFlowing(children, {
        push: function(chunk) {
          null !== chunk && (result += chunk);
          return true;
        },
        destroy: function(error) {
          didFatal = true;
          fatalError2 = error;
        }
      });
      if (didFatal && fatalError2 !== abortReason) throw fatalError2;
      if (!readyToStream) throw Error(formatProdErrorMessage(426));
      return result;
    }
    exports.renderToStaticMarkup = function(children, options) {
      return renderToStringImpl(
        children,
        options,
        true,
        'The server used "renderToStaticMarkup" which does not support Suspense. If you intended to have the server wait for the suspended component please switch to "renderToReadableStream" which supports Suspense on the server'
      );
    };
    exports.renderToString = function(children, options) {
      return renderToStringImpl(
        children,
        options,
        false,
        'The server used "renderToString" which does not support Suspense. If you intended for this Suspense boundary to render the fallback content on the server consider throwing an Error somewhere within the Suspense boundary. If you intended to have the server wait for the suspended component please switch to "renderToReadableStream" which supports Suspense on the server'
      );
    };
    exports.version = "19.2.8";
  }
});

// node_modules/react-dom/cjs/react-dom-server.browser.production.js
var require_react_dom_server_browser_production = __commonJS({
  "node_modules/react-dom/cjs/react-dom-server.browser.production.js"(exports) {
    "use strict";
    var React2 = require_react();
    var ReactDOM = require_react_dom();
    function formatProdErrorMessage(code) {
      var url = "https://react.dev/errors/" + code;
      if (1 < arguments.length) {
        url += "?args[]=" + encodeURIComponent(arguments[1]);
        for (var i = 2; i < arguments.length; i++)
          url += "&args[]=" + encodeURIComponent(arguments[i]);
      }
      return "Minified React error #" + code + "; visit " + url + " for the full message or use the non-minified dev environment for full errors and additional helpful warnings.";
    }
    var REACT_ELEMENT_TYPE = Symbol.for("react.transitional.element");
    var REACT_PORTAL_TYPE = Symbol.for("react.portal");
    var REACT_FRAGMENT_TYPE = Symbol.for("react.fragment");
    var REACT_STRICT_MODE_TYPE = Symbol.for("react.strict_mode");
    var REACT_PROFILER_TYPE = Symbol.for("react.profiler");
    var REACT_CONSUMER_TYPE = Symbol.for("react.consumer");
    var REACT_CONTEXT_TYPE = Symbol.for("react.context");
    var REACT_FORWARD_REF_TYPE = Symbol.for("react.forward_ref");
    var REACT_SUSPENSE_TYPE = Symbol.for("react.suspense");
    var REACT_SUSPENSE_LIST_TYPE = Symbol.for("react.suspense_list");
    var REACT_MEMO_TYPE = Symbol.for("react.memo");
    var REACT_LAZY_TYPE = Symbol.for("react.lazy");
    var REACT_SCOPE_TYPE = Symbol.for("react.scope");
    var REACT_ACTIVITY_TYPE = Symbol.for("react.activity");
    var REACT_LEGACY_HIDDEN_TYPE = Symbol.for("react.legacy_hidden");
    var REACT_MEMO_CACHE_SENTINEL = Symbol.for("react.memo_cache_sentinel");
    var REACT_VIEW_TRANSITION_TYPE = Symbol.for("react.view_transition");
    var MAYBE_ITERATOR_SYMBOL = Symbol.iterator;
    function getIteratorFn(maybeIterable) {
      if (null === maybeIterable || "object" !== typeof maybeIterable) return null;
      maybeIterable = MAYBE_ITERATOR_SYMBOL && maybeIterable[MAYBE_ITERATOR_SYMBOL] || maybeIterable["@@iterator"];
      return "function" === typeof maybeIterable ? maybeIterable : null;
    }
    var isArrayImpl = Array.isArray;
    function murmurhash3_32_gc(key, seed) {
      var remainder = key.length & 3;
      var bytes = key.length - remainder;
      var h1 = seed;
      for (seed = 0; seed < bytes; ) {
        var k1 = key.charCodeAt(seed) & 255 | (key.charCodeAt(++seed) & 255) << 8 | (key.charCodeAt(++seed) & 255) << 16 | (key.charCodeAt(++seed) & 255) << 24;
        ++seed;
        k1 = 3432918353 * (k1 & 65535) + ((3432918353 * (k1 >>> 16) & 65535) << 16) & 4294967295;
        k1 = k1 << 15 | k1 >>> 17;
        k1 = 461845907 * (k1 & 65535) + ((461845907 * (k1 >>> 16) & 65535) << 16) & 4294967295;
        h1 ^= k1;
        h1 = h1 << 13 | h1 >>> 19;
        h1 = 5 * (h1 & 65535) + ((5 * (h1 >>> 16) & 65535) << 16) & 4294967295;
        h1 = (h1 & 65535) + 27492 + (((h1 >>> 16) + 58964 & 65535) << 16);
      }
      k1 = 0;
      switch (remainder) {
        case 3:
          k1 ^= (key.charCodeAt(seed + 2) & 255) << 16;
        case 2:
          k1 ^= (key.charCodeAt(seed + 1) & 255) << 8;
        case 1:
          k1 ^= key.charCodeAt(seed) & 255, k1 = 3432918353 * (k1 & 65535) + ((3432918353 * (k1 >>> 16) & 65535) << 16) & 4294967295, k1 = k1 << 15 | k1 >>> 17, h1 ^= 461845907 * (k1 & 65535) + ((461845907 * (k1 >>> 16) & 65535) << 16) & 4294967295;
      }
      h1 ^= key.length;
      h1 ^= h1 >>> 16;
      h1 = 2246822507 * (h1 & 65535) + ((2246822507 * (h1 >>> 16) & 65535) << 16) & 4294967295;
      h1 ^= h1 >>> 13;
      h1 = 3266489909 * (h1 & 65535) + ((3266489909 * (h1 >>> 16) & 65535) << 16) & 4294967295;
      return (h1 ^ h1 >>> 16) >>> 0;
    }
    var channel = new MessageChannel();
    var taskQueue = [];
    channel.port1.onmessage = function() {
      var task = taskQueue.shift();
      task && task();
    };
    function scheduleWork(callback) {
      taskQueue.push(callback);
      channel.port2.postMessage(null);
    }
    function handleErrorInNextTick(error) {
      setTimeout(function() {
        throw error;
      });
    }
    var LocalPromise = Promise;
    var scheduleMicrotask = "function" === typeof queueMicrotask ? queueMicrotask : function(callback) {
      LocalPromise.resolve(null).then(callback).catch(handleErrorInNextTick);
    };
    var currentView = null;
    var writtenBytes = 0;
    function writeChunk(destination, chunk) {
      if (0 !== chunk.byteLength)
        if (2048 < chunk.byteLength)
          0 < writtenBytes && (destination.enqueue(
            new Uint8Array(currentView.buffer, 0, writtenBytes)
          ), currentView = new Uint8Array(2048), writtenBytes = 0), destination.enqueue(chunk);
        else {
          var allowableBytes = currentView.length - writtenBytes;
          allowableBytes < chunk.byteLength && (0 === allowableBytes ? destination.enqueue(currentView) : (currentView.set(chunk.subarray(0, allowableBytes), writtenBytes), destination.enqueue(currentView), chunk = chunk.subarray(allowableBytes)), currentView = new Uint8Array(2048), writtenBytes = 0);
          currentView.set(chunk, writtenBytes);
          writtenBytes += chunk.byteLength;
        }
    }
    function writeChunkAndReturn(destination, chunk) {
      writeChunk(destination, chunk);
      return true;
    }
    function completeWriting(destination) {
      currentView && 0 < writtenBytes && (destination.enqueue(new Uint8Array(currentView.buffer, 0, writtenBytes)), currentView = null, writtenBytes = 0);
    }
    var textEncoder = new TextEncoder();
    function stringToChunk(content) {
      return textEncoder.encode(content);
    }
    function stringToPrecomputedChunk(content) {
      return textEncoder.encode(content);
    }
    function byteLengthOfChunk(chunk) {
      return chunk.byteLength;
    }
    function closeWithError(destination, error) {
      "function" === typeof destination.error ? destination.error(error) : destination.close();
    }
    var assign = Object.assign;
    var hasOwnProperty = Object.prototype.hasOwnProperty;
    var VALID_ATTRIBUTE_NAME_REGEX = RegExp(
      "^[:A-Z_a-z\\u00C0-\\u00D6\\u00D8-\\u00F6\\u00F8-\\u02FF\\u0370-\\u037D\\u037F-\\u1FFF\\u200C-\\u200D\\u2070-\\u218F\\u2C00-\\u2FEF\\u3001-\\uD7FF\\uF900-\\uFDCF\\uFDF0-\\uFFFD][:A-Z_a-z\\u00C0-\\u00D6\\u00D8-\\u00F6\\u00F8-\\u02FF\\u0370-\\u037D\\u037F-\\u1FFF\\u200C-\\u200D\\u2070-\\u218F\\u2C00-\\u2FEF\\u3001-\\uD7FF\\uF900-\\uFDCF\\uFDF0-\\uFFFD\\-.0-9\\u00B7\\u0300-\\u036F\\u203F-\\u2040]*$"
    );
    var illegalAttributeNameCache = {};
    var validatedAttributeNameCache = {};
    function isAttributeNameSafe(attributeName) {
      if (hasOwnProperty.call(validatedAttributeNameCache, attributeName))
        return true;
      if (hasOwnProperty.call(illegalAttributeNameCache, attributeName)) return false;
      if (VALID_ATTRIBUTE_NAME_REGEX.test(attributeName))
        return validatedAttributeNameCache[attributeName] = true;
      illegalAttributeNameCache[attributeName] = true;
      return false;
    }
    var unitlessNumbers = new Set(
      "animationIterationCount aspectRatio borderImageOutset borderImageSlice borderImageWidth boxFlex boxFlexGroup boxOrdinalGroup columnCount columns flex flexGrow flexPositive flexShrink flexNegative flexOrder gridArea gridRow gridRowEnd gridRowSpan gridRowStart gridColumn gridColumnEnd gridColumnSpan gridColumnStart fontWeight lineClamp lineHeight opacity order orphans scale tabSize widows zIndex zoom fillOpacity floodOpacity stopOpacity strokeDasharray strokeDashoffset strokeMiterlimit strokeOpacity strokeWidth MozAnimationIterationCount MozBoxFlex MozBoxFlexGroup MozLineClamp msAnimationIterationCount msFlex msZoom msFlexGrow msFlexNegative msFlexOrder msFlexPositive msFlexShrink msGridColumn msGridColumnSpan msGridRow msGridRowSpan WebkitAnimationIterationCount WebkitBoxFlex WebKitBoxFlexGroup WebkitBoxOrdinalGroup WebkitColumnCount WebkitColumns WebkitFlex WebkitFlexGrow WebkitFlexPositive WebkitFlexShrink WebkitLineClamp".split(
        " "
      )
    );
    var aliases = /* @__PURE__ */ new Map([
      ["acceptCharset", "accept-charset"],
      ["htmlFor", "for"],
      ["httpEquiv", "http-equiv"],
      ["crossOrigin", "crossorigin"],
      ["accentHeight", "accent-height"],
      ["alignmentBaseline", "alignment-baseline"],
      ["arabicForm", "arabic-form"],
      ["baselineShift", "baseline-shift"],
      ["capHeight", "cap-height"],
      ["clipPath", "clip-path"],
      ["clipRule", "clip-rule"],
      ["colorInterpolation", "color-interpolation"],
      ["colorInterpolationFilters", "color-interpolation-filters"],
      ["colorProfile", "color-profile"],
      ["colorRendering", "color-rendering"],
      ["dominantBaseline", "dominant-baseline"],
      ["enableBackground", "enable-background"],
      ["fillOpacity", "fill-opacity"],
      ["fillRule", "fill-rule"],
      ["floodColor", "flood-color"],
      ["floodOpacity", "flood-opacity"],
      ["fontFamily", "font-family"],
      ["fontSize", "font-size"],
      ["fontSizeAdjust", "font-size-adjust"],
      ["fontStretch", "font-stretch"],
      ["fontStyle", "font-style"],
      ["fontVariant", "font-variant"],
      ["fontWeight", "font-weight"],
      ["glyphName", "glyph-name"],
      ["glyphOrientationHorizontal", "glyph-orientation-horizontal"],
      ["glyphOrientationVertical", "glyph-orientation-vertical"],
      ["horizAdvX", "horiz-adv-x"],
      ["horizOriginX", "horiz-origin-x"],
      ["imageRendering", "image-rendering"],
      ["letterSpacing", "letter-spacing"],
      ["lightingColor", "lighting-color"],
      ["markerEnd", "marker-end"],
      ["markerMid", "marker-mid"],
      ["markerStart", "marker-start"],
      ["overlinePosition", "overline-position"],
      ["overlineThickness", "overline-thickness"],
      ["paintOrder", "paint-order"],
      ["panose-1", "panose-1"],
      ["pointerEvents", "pointer-events"],
      ["renderingIntent", "rendering-intent"],
      ["shapeRendering", "shape-rendering"],
      ["stopColor", "stop-color"],
      ["stopOpacity", "stop-opacity"],
      ["strikethroughPosition", "strikethrough-position"],
      ["strikethroughThickness", "strikethrough-thickness"],
      ["strokeDasharray", "stroke-dasharray"],
      ["strokeDashoffset", "stroke-dashoffset"],
      ["strokeLinecap", "stroke-linecap"],
      ["strokeLinejoin", "stroke-linejoin"],
      ["strokeMiterlimit", "stroke-miterlimit"],
      ["strokeOpacity", "stroke-opacity"],
      ["strokeWidth", "stroke-width"],
      ["textAnchor", "text-anchor"],
      ["textDecoration", "text-decoration"],
      ["textRendering", "text-rendering"],
      ["transformOrigin", "transform-origin"],
      ["underlinePosition", "underline-position"],
      ["underlineThickness", "underline-thickness"],
      ["unicodeBidi", "unicode-bidi"],
      ["unicodeRange", "unicode-range"],
      ["unitsPerEm", "units-per-em"],
      ["vAlphabetic", "v-alphabetic"],
      ["vHanging", "v-hanging"],
      ["vIdeographic", "v-ideographic"],
      ["vMathematical", "v-mathematical"],
      ["vectorEffect", "vector-effect"],
      ["vertAdvY", "vert-adv-y"],
      ["vertOriginX", "vert-origin-x"],
      ["vertOriginY", "vert-origin-y"],
      ["wordSpacing", "word-spacing"],
      ["writingMode", "writing-mode"],
      ["xmlnsXlink", "xmlns:xlink"],
      ["xHeight", "x-height"]
    ]);
    var matchHtmlRegExp = /["'&<>]/;
    function escapeTextForBrowser(text) {
      if ("boolean" === typeof text || "number" === typeof text || "bigint" === typeof text)
        return "" + text;
      text = "" + text;
      var match = matchHtmlRegExp.exec(text);
      if (match) {
        var html = "", index, lastIndex = 0;
        for (index = match.index; index < text.length; index++) {
          switch (text.charCodeAt(index)) {
            case 34:
              match = "&quot;";
              break;
            case 38:
              match = "&amp;";
              break;
            case 39:
              match = "&#x27;";
              break;
            case 60:
              match = "&lt;";
              break;
            case 62:
              match = "&gt;";
              break;
            default:
              continue;
          }
          lastIndex !== index && (html += text.slice(lastIndex, index));
          lastIndex = index + 1;
          html += match;
        }
        text = lastIndex !== index ? html + text.slice(lastIndex, index) : html;
      }
      return text;
    }
    var uppercasePattern = /([A-Z])/g;
    var msPattern = /^ms-/;
    var isJavaScriptProtocol = /^[\u0000-\u001F ]*j[\r\n\t]*a[\r\n\t]*v[\r\n\t]*a[\r\n\t]*s[\r\n\t]*c[\r\n\t]*r[\r\n\t]*i[\r\n\t]*p[\r\n\t]*t[\r\n\t]*:/i;
    function sanitizeURL(url) {
      return isJavaScriptProtocol.test("" + url) ? "javascript:throw new Error('React has blocked a javascript: URL as a security precaution.')" : url;
    }
    var ReactSharedInternals = React2.__CLIENT_INTERNALS_DO_NOT_USE_OR_WARN_USERS_THEY_CANNOT_UPGRADE;
    var ReactDOMSharedInternals = ReactDOM.__DOM_INTERNALS_DO_NOT_USE_OR_WARN_USERS_THEY_CANNOT_UPGRADE;
    var sharedNotPendingObject = {
      pending: false,
      data: null,
      method: null,
      action: null
    };
    var previousDispatcher = ReactDOMSharedInternals.d;
    ReactDOMSharedInternals.d = {
      f: previousDispatcher.f,
      r: previousDispatcher.r,
      D: prefetchDNS,
      C: preconnect,
      L: preload,
      m: preloadModule,
      X: preinitScript,
      S: preinitStyle,
      M: preinitModuleScript
    };
    var PRELOAD_NO_CREDS = [];
    var currentlyFlushingRenderState = null;
    stringToPrecomputedChunk('"></template>');
    var startInlineScript = stringToPrecomputedChunk("<script");
    var endInlineScript = stringToPrecomputedChunk("<\/script>");
    var startScriptSrc = stringToPrecomputedChunk('<script src="');
    var startModuleSrc = stringToPrecomputedChunk('<script type="module" src="');
    var scriptNonce = stringToPrecomputedChunk(' nonce="');
    var scriptIntegirty = stringToPrecomputedChunk(' integrity="');
    var scriptCrossOrigin = stringToPrecomputedChunk(' crossorigin="');
    var endAsyncScript = stringToPrecomputedChunk(' async=""><\/script>');
    var startInlineStyle = stringToPrecomputedChunk("<style");
    var scriptRegex = /(<\/|<)(s)(cript)/gi;
    function scriptReplacer(match, prefix2, s, suffix2) {
      return "" + prefix2 + ("s" === s ? "\\u0073" : "\\u0053") + suffix2;
    }
    var importMapScriptStart = stringToPrecomputedChunk(
      '<script type="importmap">'
    );
    var importMapScriptEnd = stringToPrecomputedChunk("<\/script>");
    function createRenderState(resumableState, nonce, externalRuntimeConfig, importMap, onHeaders, maxHeadersLength) {
      externalRuntimeConfig = "string" === typeof nonce ? nonce : nonce && nonce.script;
      var inlineScriptWithNonce = void 0 === externalRuntimeConfig ? startInlineScript : stringToPrecomputedChunk(
        '<script nonce="' + escapeTextForBrowser(externalRuntimeConfig) + '"'
      ), nonceStyle = "string" === typeof nonce ? void 0 : nonce && nonce.style, inlineStyleWithNonce = void 0 === nonceStyle ? startInlineStyle : stringToPrecomputedChunk(
        '<style nonce="' + escapeTextForBrowser(nonceStyle) + '"'
      ), idPrefix = resumableState.idPrefix, bootstrapChunks = [], bootstrapScriptContent = resumableState.bootstrapScriptContent, bootstrapScripts = resumableState.bootstrapScripts, bootstrapModules = resumableState.bootstrapModules;
      void 0 !== bootstrapScriptContent && (bootstrapChunks.push(inlineScriptWithNonce), pushCompletedShellIdAttribute(bootstrapChunks, resumableState), bootstrapChunks.push(
        endOfStartTag,
        stringToChunk(
          ("" + bootstrapScriptContent).replace(scriptRegex, scriptReplacer)
        ),
        endInlineScript
      ));
      bootstrapScriptContent = [];
      void 0 !== importMap && (bootstrapScriptContent.push(importMapScriptStart), bootstrapScriptContent.push(
        stringToChunk(
          ("" + JSON.stringify(importMap)).replace(scriptRegex, scriptReplacer)
        )
      ), bootstrapScriptContent.push(importMapScriptEnd));
      importMap = onHeaders ? {
        preconnects: "",
        fontPreloads: "",
        highImagePreloads: "",
        remainingCapacity: 2 + ("number" === typeof maxHeadersLength ? maxHeadersLength : 2e3)
      } : null;
      onHeaders = {
        placeholderPrefix: stringToPrecomputedChunk(idPrefix + "P:"),
        segmentPrefix: stringToPrecomputedChunk(idPrefix + "S:"),
        boundaryPrefix: stringToPrecomputedChunk(idPrefix + "B:"),
        startInlineScript: inlineScriptWithNonce,
        startInlineStyle: inlineStyleWithNonce,
        preamble: createPreambleState(),
        externalRuntimeScript: null,
        bootstrapChunks,
        importMapChunks: bootstrapScriptContent,
        onHeaders,
        headers: importMap,
        resets: {
          font: {},
          dns: {},
          connect: { default: {}, anonymous: {}, credentials: {} },
          image: {},
          style: {}
        },
        charsetChunks: [],
        viewportChunks: [],
        hoistableChunks: [],
        preconnects: /* @__PURE__ */ new Set(),
        fontPreloads: /* @__PURE__ */ new Set(),
        highImagePreloads: /* @__PURE__ */ new Set(),
        styles: /* @__PURE__ */ new Map(),
        bootstrapScripts: /* @__PURE__ */ new Set(),
        scripts: /* @__PURE__ */ new Set(),
        bulkPreloads: /* @__PURE__ */ new Set(),
        preloads: {
          images: /* @__PURE__ */ new Map(),
          stylesheets: /* @__PURE__ */ new Map(),
          scripts: /* @__PURE__ */ new Map(),
          moduleScripts: /* @__PURE__ */ new Map()
        },
        nonce: { script: externalRuntimeConfig, style: nonceStyle },
        hoistableState: null,
        stylesToHoist: false
      };
      if (void 0 !== bootstrapScripts)
        for (importMap = 0; importMap < bootstrapScripts.length; importMap++)
          idPrefix = bootstrapScripts[importMap], nonceStyle = inlineScriptWithNonce = void 0, inlineStyleWithNonce = {
            rel: "preload",
            as: "script",
            fetchPriority: "low",
            nonce
          }, "string" === typeof idPrefix ? inlineStyleWithNonce.href = maxHeadersLength = idPrefix : (inlineStyleWithNonce.href = maxHeadersLength = idPrefix.src, inlineStyleWithNonce.integrity = nonceStyle = "string" === typeof idPrefix.integrity ? idPrefix.integrity : void 0, inlineStyleWithNonce.crossOrigin = inlineScriptWithNonce = "string" === typeof idPrefix || null == idPrefix.crossOrigin ? void 0 : "use-credentials" === idPrefix.crossOrigin ? "use-credentials" : ""), idPrefix = resumableState, bootstrapScriptContent = maxHeadersLength, idPrefix.scriptResources[bootstrapScriptContent] = null, idPrefix.moduleScriptResources[bootstrapScriptContent] = null, idPrefix = [], pushLinkImpl(idPrefix, inlineStyleWithNonce), onHeaders.bootstrapScripts.add(idPrefix), bootstrapChunks.push(
            startScriptSrc,
            stringToChunk(escapeTextForBrowser(maxHeadersLength)),
            attributeEnd
          ), externalRuntimeConfig && bootstrapChunks.push(
            scriptNonce,
            stringToChunk(escapeTextForBrowser(externalRuntimeConfig)),
            attributeEnd
          ), "string" === typeof nonceStyle && bootstrapChunks.push(
            scriptIntegirty,
            stringToChunk(escapeTextForBrowser(nonceStyle)),
            attributeEnd
          ), "string" === typeof inlineScriptWithNonce && bootstrapChunks.push(
            scriptCrossOrigin,
            stringToChunk(escapeTextForBrowser(inlineScriptWithNonce)),
            attributeEnd
          ), pushCompletedShellIdAttribute(bootstrapChunks, resumableState), bootstrapChunks.push(endAsyncScript);
      if (void 0 !== bootstrapModules)
        for (nonce = 0; nonce < bootstrapModules.length; nonce++)
          nonceStyle = bootstrapModules[nonce], maxHeadersLength = importMap = void 0, inlineScriptWithNonce = {
            rel: "modulepreload",
            fetchPriority: "low",
            nonce: externalRuntimeConfig
          }, "string" === typeof nonceStyle ? inlineScriptWithNonce.href = bootstrapScripts = nonceStyle : (inlineScriptWithNonce.href = bootstrapScripts = nonceStyle.src, inlineScriptWithNonce.integrity = maxHeadersLength = "string" === typeof nonceStyle.integrity ? nonceStyle.integrity : void 0, inlineScriptWithNonce.crossOrigin = importMap = "string" === typeof nonceStyle || null == nonceStyle.crossOrigin ? void 0 : "use-credentials" === nonceStyle.crossOrigin ? "use-credentials" : ""), nonceStyle = resumableState, inlineStyleWithNonce = bootstrapScripts, nonceStyle.scriptResources[inlineStyleWithNonce] = null, nonceStyle.moduleScriptResources[inlineStyleWithNonce] = null, nonceStyle = [], pushLinkImpl(nonceStyle, inlineScriptWithNonce), onHeaders.bootstrapScripts.add(nonceStyle), bootstrapChunks.push(
            startModuleSrc,
            stringToChunk(escapeTextForBrowser(bootstrapScripts)),
            attributeEnd
          ), externalRuntimeConfig && bootstrapChunks.push(
            scriptNonce,
            stringToChunk(escapeTextForBrowser(externalRuntimeConfig)),
            attributeEnd
          ), "string" === typeof maxHeadersLength && bootstrapChunks.push(
            scriptIntegirty,
            stringToChunk(escapeTextForBrowser(maxHeadersLength)),
            attributeEnd
          ), "string" === typeof importMap && bootstrapChunks.push(
            scriptCrossOrigin,
            stringToChunk(escapeTextForBrowser(importMap)),
            attributeEnd
          ), pushCompletedShellIdAttribute(bootstrapChunks, resumableState), bootstrapChunks.push(endAsyncScript);
      return onHeaders;
    }
    function createResumableState(identifierPrefix, externalRuntimeConfig, bootstrapScriptContent, bootstrapScripts, bootstrapModules) {
      return {
        idPrefix: void 0 === identifierPrefix ? "" : identifierPrefix,
        nextFormID: 0,
        streamingFormat: 0,
        bootstrapScriptContent,
        bootstrapScripts,
        bootstrapModules,
        instructions: 0,
        hasBody: false,
        hasHtml: false,
        unknownResources: {},
        dnsResources: {},
        connectResources: { default: {}, anonymous: {}, credentials: {} },
        imageResources: {},
        styleResources: {},
        scriptResources: {},
        moduleUnknownResources: {},
        moduleScriptResources: {}
      };
    }
    function createPreambleState() {
      return { htmlChunks: null, headChunks: null, bodyChunks: null };
    }
    function createFormatContext(insertionMode, selectedValue, tagScope, viewTransition) {
      return {
        insertionMode,
        selectedValue,
        tagScope,
        viewTransition
      };
    }
    function createRootFormatContext(namespaceURI) {
      return createFormatContext(
        "http://www.w3.org/2000/svg" === namespaceURI ? 4 : "http://www.w3.org/1998/Math/MathML" === namespaceURI ? 5 : 0,
        null,
        0,
        null
      );
    }
    function getChildFormatContext(parentContext, type, props) {
      var subtreeScope = parentContext.tagScope & -25;
      switch (type) {
        case "noscript":
          return createFormatContext(2, null, subtreeScope | 1, null);
        case "select":
          return createFormatContext(
            2,
            null != props.value ? props.value : props.defaultValue,
            subtreeScope,
            null
          );
        case "svg":
          return createFormatContext(4, null, subtreeScope, null);
        case "picture":
          return createFormatContext(2, null, subtreeScope | 2, null);
        case "math":
          return createFormatContext(5, null, subtreeScope, null);
        case "foreignObject":
          return createFormatContext(2, null, subtreeScope, null);
        case "table":
          return createFormatContext(6, null, subtreeScope, null);
        case "thead":
        case "tbody":
        case "tfoot":
          return createFormatContext(7, null, subtreeScope, null);
        case "colgroup":
          return createFormatContext(9, null, subtreeScope, null);
        case "tr":
          return createFormatContext(8, null, subtreeScope, null);
        case "head":
          if (2 > parentContext.insertionMode)
            return createFormatContext(3, null, subtreeScope, null);
          break;
        case "html":
          if (0 === parentContext.insertionMode)
            return createFormatContext(1, null, subtreeScope, null);
      }
      return 6 <= parentContext.insertionMode || 2 > parentContext.insertionMode ? createFormatContext(2, null, subtreeScope, null) : parentContext.tagScope !== subtreeScope ? createFormatContext(
        parentContext.insertionMode,
        parentContext.selectedValue,
        subtreeScope,
        null
      ) : parentContext;
    }
    function getSuspenseViewTransition(parentViewTransition) {
      return null === parentViewTransition ? null : {
        update: parentViewTransition.update,
        enter: "none",
        exit: "none",
        share: parentViewTransition.update,
        name: parentViewTransition.autoName,
        autoName: parentViewTransition.autoName,
        nameIdx: 0
      };
    }
    function getSuspenseFallbackFormatContext(resumableState, parentContext) {
      parentContext.tagScope & 32 && (resumableState.instructions |= 128);
      return createFormatContext(
        parentContext.insertionMode,
        parentContext.selectedValue,
        parentContext.tagScope | 12,
        getSuspenseViewTransition(parentContext.viewTransition)
      );
    }
    function getSuspenseContentFormatContext(resumableState, parentContext) {
      resumableState = getSuspenseViewTransition(parentContext.viewTransition);
      var subtreeScope = parentContext.tagScope | 16;
      null !== resumableState && "none" !== resumableState.share && (subtreeScope |= 64);
      return createFormatContext(
        parentContext.insertionMode,
        parentContext.selectedValue,
        subtreeScope,
        resumableState
      );
    }
    var textSeparator = stringToPrecomputedChunk("<!-- -->");
    function pushTextInstance(target, text, renderState, textEmbedded) {
      if ("" === text) return textEmbedded;
      textEmbedded && target.push(textSeparator);
      target.push(stringToChunk(escapeTextForBrowser(text)));
      return true;
    }
    var styleNameCache = /* @__PURE__ */ new Map();
    var styleAttributeStart = stringToPrecomputedChunk(' style="');
    var styleAssign = stringToPrecomputedChunk(":");
    var styleSeparator = stringToPrecomputedChunk(";");
    function pushStyleAttribute(target, style) {
      if ("object" !== typeof style) throw Error(formatProdErrorMessage(62));
      var isFirst = true, styleName;
      for (styleName in style)
        if (hasOwnProperty.call(style, styleName)) {
          var styleValue = style[styleName];
          if (null != styleValue && "boolean" !== typeof styleValue && "" !== styleValue) {
            if (0 === styleName.indexOf("--")) {
              var nameChunk = stringToChunk(escapeTextForBrowser(styleName));
              styleValue = stringToChunk(
                escapeTextForBrowser(("" + styleValue).trim())
              );
            } else
              nameChunk = styleNameCache.get(styleName), void 0 === nameChunk && (nameChunk = stringToPrecomputedChunk(
                escapeTextForBrowser(
                  styleName.replace(uppercasePattern, "-$1").toLowerCase().replace(msPattern, "-ms-")
                )
              ), styleNameCache.set(styleName, nameChunk)), styleValue = "number" === typeof styleValue ? 0 === styleValue || unitlessNumbers.has(styleName) ? stringToChunk("" + styleValue) : stringToChunk(styleValue + "px") : stringToChunk(
                escapeTextForBrowser(("" + styleValue).trim())
              );
            isFirst ? (isFirst = false, target.push(
              styleAttributeStart,
              nameChunk,
              styleAssign,
              styleValue
            )) : target.push(styleSeparator, nameChunk, styleAssign, styleValue);
          }
        }
      isFirst || target.push(attributeEnd);
    }
    var attributeSeparator = stringToPrecomputedChunk(" ");
    var attributeAssign = stringToPrecomputedChunk('="');
    var attributeEnd = stringToPrecomputedChunk('"');
    var attributeEmptyString = stringToPrecomputedChunk('=""');
    function pushBooleanAttribute(target, name, value) {
      value && "function" !== typeof value && "symbol" !== typeof value && target.push(attributeSeparator, stringToChunk(name), attributeEmptyString);
    }
    function pushStringAttribute(target, name, value) {
      "function" !== typeof value && "symbol" !== typeof value && "boolean" !== typeof value && target.push(
        attributeSeparator,
        stringToChunk(name),
        attributeAssign,
        stringToChunk(escapeTextForBrowser(value)),
        attributeEnd
      );
    }
    var actionJavaScriptURL = stringToPrecomputedChunk(
      escapeTextForBrowser(
        "javascript:throw new Error('React form unexpectedly submitted.')"
      )
    );
    var startHiddenInputChunk = stringToPrecomputedChunk('<input type="hidden"');
    function pushAdditionalFormField(value, key) {
      this.push(startHiddenInputChunk);
      validateAdditionalFormField(value);
      pushStringAttribute(this, "name", key);
      pushStringAttribute(this, "value", value);
      this.push(endOfStartTagSelfClosing);
    }
    function validateAdditionalFormField(value) {
      if ("string" !== typeof value) throw Error(formatProdErrorMessage(480));
    }
    function getCustomFormFields(resumableState, formAction) {
      if ("function" === typeof formAction.$$FORM_ACTION) {
        var id = resumableState.nextFormID++;
        resumableState = resumableState.idPrefix + id;
        try {
          var customFields = formAction.$$FORM_ACTION(resumableState);
          if (customFields) {
            var formData = customFields.data;
            null != formData && formData.forEach(validateAdditionalFormField);
          }
          return customFields;
        } catch (x) {
          if ("object" === typeof x && null !== x && "function" === typeof x.then)
            throw x;
        }
      }
      return null;
    }
    function pushFormActionAttribute(target, resumableState, renderState, formAction, formEncType, formMethod, formTarget, name) {
      var formData = null;
      if ("function" === typeof formAction) {
        var customFields = getCustomFormFields(resumableState, formAction);
        null !== customFields ? (name = customFields.name, formAction = customFields.action || "", formEncType = customFields.encType, formMethod = customFields.method, formTarget = customFields.target, formData = customFields.data) : (target.push(
          attributeSeparator,
          stringToChunk("formAction"),
          attributeAssign,
          actionJavaScriptURL,
          attributeEnd
        ), formTarget = formMethod = formEncType = formAction = name = null, injectFormReplayingRuntime(resumableState, renderState));
      }
      null != name && pushAttribute(target, "name", name);
      null != formAction && pushAttribute(target, "formAction", formAction);
      null != formEncType && pushAttribute(target, "formEncType", formEncType);
      null != formMethod && pushAttribute(target, "formMethod", formMethod);
      null != formTarget && pushAttribute(target, "formTarget", formTarget);
      return formData;
    }
    function pushAttribute(target, name, value) {
      switch (name) {
        case "className":
          pushStringAttribute(target, "class", value);
          break;
        case "tabIndex":
          pushStringAttribute(target, "tabindex", value);
          break;
        case "dir":
        case "role":
        case "viewBox":
        case "width":
        case "height":
          pushStringAttribute(target, name, value);
          break;
        case "style":
          pushStyleAttribute(target, value);
          break;
        case "src":
        case "href":
          if ("" === value) break;
        case "action":
        case "formAction":
          if (null == value || "function" === typeof value || "symbol" === typeof value || "boolean" === typeof value)
            break;
          value = sanitizeURL("" + value);
          target.push(
            attributeSeparator,
            stringToChunk(name),
            attributeAssign,
            stringToChunk(escapeTextForBrowser(value)),
            attributeEnd
          );
          break;
        case "defaultValue":
        case "defaultChecked":
        case "innerHTML":
        case "suppressContentEditableWarning":
        case "suppressHydrationWarning":
        case "ref":
          break;
        case "autoFocus":
        case "multiple":
        case "muted":
          pushBooleanAttribute(target, name.toLowerCase(), value);
          break;
        case "xlinkHref":
          if ("function" === typeof value || "symbol" === typeof value || "boolean" === typeof value)
            break;
          value = sanitizeURL("" + value);
          target.push(
            attributeSeparator,
            stringToChunk("xlink:href"),
            attributeAssign,
            stringToChunk(escapeTextForBrowser(value)),
            attributeEnd
          );
          break;
        case "contentEditable":
        case "spellCheck":
        case "draggable":
        case "value":
        case "autoReverse":
        case "externalResourcesRequired":
        case "focusable":
        case "preserveAlpha":
          "function" !== typeof value && "symbol" !== typeof value && target.push(
            attributeSeparator,
            stringToChunk(name),
            attributeAssign,
            stringToChunk(escapeTextForBrowser(value)),
            attributeEnd
          );
          break;
        case "inert":
        case "allowFullScreen":
        case "async":
        case "autoPlay":
        case "controls":
        case "default":
        case "defer":
        case "disabled":
        case "disablePictureInPicture":
        case "disableRemotePlayback":
        case "formNoValidate":
        case "hidden":
        case "loop":
        case "noModule":
        case "noValidate":
        case "open":
        case "playsInline":
        case "readOnly":
        case "required":
        case "reversed":
        case "scoped":
        case "seamless":
        case "itemScope":
          value && "function" !== typeof value && "symbol" !== typeof value && target.push(
            attributeSeparator,
            stringToChunk(name),
            attributeEmptyString
          );
          break;
        case "capture":
        case "download":
          true === value ? target.push(
            attributeSeparator,
            stringToChunk(name),
            attributeEmptyString
          ) : false !== value && "function" !== typeof value && "symbol" !== typeof value && target.push(
            attributeSeparator,
            stringToChunk(name),
            attributeAssign,
            stringToChunk(escapeTextForBrowser(value)),
            attributeEnd
          );
          break;
        case "cols":
        case "rows":
        case "size":
        case "span":
          "function" !== typeof value && "symbol" !== typeof value && !isNaN(value) && 1 <= value && target.push(
            attributeSeparator,
            stringToChunk(name),
            attributeAssign,
            stringToChunk(escapeTextForBrowser(value)),
            attributeEnd
          );
          break;
        case "rowSpan":
        case "start":
          "function" === typeof value || "symbol" === typeof value || isNaN(value) || target.push(
            attributeSeparator,
            stringToChunk(name),
            attributeAssign,
            stringToChunk(escapeTextForBrowser(value)),
            attributeEnd
          );
          break;
        case "xlinkActuate":
          pushStringAttribute(target, "xlink:actuate", value);
          break;
        case "xlinkArcrole":
          pushStringAttribute(target, "xlink:arcrole", value);
          break;
        case "xlinkRole":
          pushStringAttribute(target, "xlink:role", value);
          break;
        case "xlinkShow":
          pushStringAttribute(target, "xlink:show", value);
          break;
        case "xlinkTitle":
          pushStringAttribute(target, "xlink:title", value);
          break;
        case "xlinkType":
          pushStringAttribute(target, "xlink:type", value);
          break;
        case "xmlBase":
          pushStringAttribute(target, "xml:base", value);
          break;
        case "xmlLang":
          pushStringAttribute(target, "xml:lang", value);
          break;
        case "xmlSpace":
          pushStringAttribute(target, "xml:space", value);
          break;
        default:
          if (!(2 < name.length) || "o" !== name[0] && "O" !== name[0] || "n" !== name[1] && "N" !== name[1]) {
            if (name = aliases.get(name) || name, isAttributeNameSafe(name)) {
              switch (typeof value) {
                case "function":
                case "symbol":
                  return;
                case "boolean":
                  var prefix$8 = name.toLowerCase().slice(0, 5);
                  if ("data-" !== prefix$8 && "aria-" !== prefix$8) return;
              }
              target.push(
                attributeSeparator,
                stringToChunk(name),
                attributeAssign,
                stringToChunk(escapeTextForBrowser(value)),
                attributeEnd
              );
            }
          }
      }
    }
    var endOfStartTag = stringToPrecomputedChunk(">");
    var endOfStartTagSelfClosing = stringToPrecomputedChunk("/>");
    function pushInnerHTML(target, innerHTML, children) {
      if (null != innerHTML) {
        if (null != children) throw Error(formatProdErrorMessage(60));
        if ("object" !== typeof innerHTML || !("__html" in innerHTML))
          throw Error(formatProdErrorMessage(61));
        innerHTML = innerHTML.__html;
        null !== innerHTML && void 0 !== innerHTML && target.push(stringToChunk("" + innerHTML));
      }
    }
    function flattenOptionChildren(children) {
      var content = "";
      React2.Children.forEach(children, function(child) {
        null != child && (content += child);
      });
      return content;
    }
    var selectedMarkerAttribute = stringToPrecomputedChunk(' selected=""');
    var formReplayingRuntimeScript = stringToPrecomputedChunk(
      `addEventListener("submit",function(a){if(!a.defaultPrevented){var c=a.target,d=a.submitter,e=c.action,b=d;if(d){var f=d.getAttribute("formAction");null!=f&&(e=f,b=null)}"javascript:throw new Error('React form unexpectedly submitted.')"===e&&(a.preventDefault(),b?(a=document.createElement("input"),a.name=b.name,a.value=b.value,b.parentNode.insertBefore(a,b),b=new FormData(c),a.parentNode.removeChild(a)):b=new FormData(c),a=c.ownerDocument||c,(a.$$reactFormReplay=a.$$reactFormReplay||[]).push(c,d,b))}});`
    );
    function injectFormReplayingRuntime(resumableState, renderState) {
      if (0 === (resumableState.instructions & 16)) {
        resumableState.instructions |= 16;
        var preamble = renderState.preamble, bootstrapChunks = renderState.bootstrapChunks;
        (preamble.htmlChunks || preamble.headChunks) && 0 === bootstrapChunks.length ? (bootstrapChunks.push(renderState.startInlineScript), pushCompletedShellIdAttribute(bootstrapChunks, resumableState), bootstrapChunks.push(
          endOfStartTag,
          formReplayingRuntimeScript,
          endInlineScript
        )) : bootstrapChunks.unshift(
          renderState.startInlineScript,
          endOfStartTag,
          formReplayingRuntimeScript,
          endInlineScript
        );
      }
    }
    var formStateMarkerIsMatching = stringToPrecomputedChunk("<!--F!-->");
    var formStateMarkerIsNotMatching = stringToPrecomputedChunk("<!--F-->");
    function pushLinkImpl(target, props) {
      target.push(startChunkForTag("link"));
      for (var propKey in props)
        if (hasOwnProperty.call(props, propKey)) {
          var propValue = props[propKey];
          if (null != propValue)
            switch (propKey) {
              case "children":
              case "dangerouslySetInnerHTML":
                throw Error(formatProdErrorMessage(399, "link"));
              default:
                pushAttribute(target, propKey, propValue);
            }
        }
      target.push(endOfStartTagSelfClosing);
      return null;
    }
    var styleRegex = /(<\/|<)(s)(tyle)/gi;
    function styleReplacer(match, prefix2, s, suffix2) {
      return "" + prefix2 + ("s" === s ? "\\73 " : "\\53 ") + suffix2;
    }
    function pushSelfClosing(target, props, tag) {
      target.push(startChunkForTag(tag));
      for (var propKey in props)
        if (hasOwnProperty.call(props, propKey)) {
          var propValue = props[propKey];
          if (null != propValue)
            switch (propKey) {
              case "children":
              case "dangerouslySetInnerHTML":
                throw Error(formatProdErrorMessage(399, tag));
              default:
                pushAttribute(target, propKey, propValue);
            }
        }
      target.push(endOfStartTagSelfClosing);
      return null;
    }
    function pushTitleImpl(target, props) {
      target.push(startChunkForTag("title"));
      var children = null, innerHTML = null, propKey;
      for (propKey in props)
        if (hasOwnProperty.call(props, propKey)) {
          var propValue = props[propKey];
          if (null != propValue)
            switch (propKey) {
              case "children":
                children = propValue;
                break;
              case "dangerouslySetInnerHTML":
                innerHTML = propValue;
                break;
              default:
                pushAttribute(target, propKey, propValue);
            }
        }
      target.push(endOfStartTag);
      props = Array.isArray(children) ? 2 > children.length ? children[0] : null : children;
      "function" !== typeof props && "symbol" !== typeof props && null !== props && void 0 !== props && target.push(stringToChunk(escapeTextForBrowser("" + props)));
      pushInnerHTML(target, innerHTML, children);
      target.push(endChunkForTag("title"));
      return null;
    }
    var headPreambleContributionChunk = stringToPrecomputedChunk("<!--head-->");
    var bodyPreambleContributionChunk = stringToPrecomputedChunk("<!--body-->");
    var htmlPreambleContributionChunk = stringToPrecomputedChunk("<!--html-->");
    function pushScriptImpl(target, props) {
      target.push(startChunkForTag("script"));
      var children = null, innerHTML = null, propKey;
      for (propKey in props)
        if (hasOwnProperty.call(props, propKey)) {
          var propValue = props[propKey];
          if (null != propValue)
            switch (propKey) {
              case "children":
                children = propValue;
                break;
              case "dangerouslySetInnerHTML":
                innerHTML = propValue;
                break;
              default:
                pushAttribute(target, propKey, propValue);
            }
        }
      target.push(endOfStartTag);
      pushInnerHTML(target, innerHTML, children);
      "string" === typeof children && target.push(
        stringToChunk(("" + children).replace(scriptRegex, scriptReplacer))
      );
      target.push(endChunkForTag("script"));
      return null;
    }
    function pushStartSingletonElement(target, props, tag) {
      target.push(startChunkForTag(tag));
      var innerHTML = tag = null, propKey;
      for (propKey in props)
        if (hasOwnProperty.call(props, propKey)) {
          var propValue = props[propKey];
          if (null != propValue)
            switch (propKey) {
              case "children":
                tag = propValue;
                break;
              case "dangerouslySetInnerHTML":
                innerHTML = propValue;
                break;
              default:
                pushAttribute(target, propKey, propValue);
            }
        }
      target.push(endOfStartTag);
      pushInnerHTML(target, innerHTML, tag);
      return tag;
    }
    function pushStartGenericElement(target, props, tag) {
      target.push(startChunkForTag(tag));
      var innerHTML = tag = null, propKey;
      for (propKey in props)
        if (hasOwnProperty.call(props, propKey)) {
          var propValue = props[propKey];
          if (null != propValue)
            switch (propKey) {
              case "children":
                tag = propValue;
                break;
              case "dangerouslySetInnerHTML":
                innerHTML = propValue;
                break;
              default:
                pushAttribute(target, propKey, propValue);
            }
        }
      target.push(endOfStartTag);
      pushInnerHTML(target, innerHTML, tag);
      return "string" === typeof tag ? (target.push(stringToChunk(escapeTextForBrowser(tag))), null) : tag;
    }
    var leadingNewline = stringToPrecomputedChunk("\n");
    var VALID_TAG_REGEX = /^[a-zA-Z][a-zA-Z:_\.\-\d]*$/;
    var validatedTagCache = /* @__PURE__ */ new Map();
    function startChunkForTag(tag) {
      var tagStartChunk = validatedTagCache.get(tag);
      if (void 0 === tagStartChunk) {
        if (!VALID_TAG_REGEX.test(tag))
          throw Error(formatProdErrorMessage(65, tag));
        tagStartChunk = stringToPrecomputedChunk("<" + tag);
        validatedTagCache.set(tag, tagStartChunk);
      }
      return tagStartChunk;
    }
    var doctypeChunk = stringToPrecomputedChunk("<!DOCTYPE html>");
    function pushStartInstance(target$jscomp$0, type, props, resumableState, renderState, preambleState, hoistableState, formatContext, textEmbedded) {
      switch (type) {
        case "div":
        case "span":
        case "svg":
        case "path":
          break;
        case "a":
          target$jscomp$0.push(startChunkForTag("a"));
          var children = null, innerHTML = null, propKey;
          for (propKey in props)
            if (hasOwnProperty.call(props, propKey)) {
              var propValue = props[propKey];
              if (null != propValue)
                switch (propKey) {
                  case "children":
                    children = propValue;
                    break;
                  case "dangerouslySetInnerHTML":
                    innerHTML = propValue;
                    break;
                  case "href":
                    "" === propValue ? pushStringAttribute(target$jscomp$0, "href", "") : pushAttribute(target$jscomp$0, propKey, propValue);
                    break;
                  default:
                    pushAttribute(target$jscomp$0, propKey, propValue);
                }
            }
          target$jscomp$0.push(endOfStartTag);
          pushInnerHTML(target$jscomp$0, innerHTML, children);
          if ("string" === typeof children) {
            target$jscomp$0.push(stringToChunk(escapeTextForBrowser(children)));
            var JSCompiler_inline_result = null;
          } else JSCompiler_inline_result = children;
          return JSCompiler_inline_result;
        case "g":
        case "p":
        case "li":
          break;
        case "select":
          target$jscomp$0.push(startChunkForTag("select"));
          var children$jscomp$0 = null, innerHTML$jscomp$0 = null, propKey$jscomp$0;
          for (propKey$jscomp$0 in props)
            if (hasOwnProperty.call(props, propKey$jscomp$0)) {
              var propValue$jscomp$0 = props[propKey$jscomp$0];
              if (null != propValue$jscomp$0)
                switch (propKey$jscomp$0) {
                  case "children":
                    children$jscomp$0 = propValue$jscomp$0;
                    break;
                  case "dangerouslySetInnerHTML":
                    innerHTML$jscomp$0 = propValue$jscomp$0;
                    break;
                  case "defaultValue":
                  case "value":
                    break;
                  default:
                    pushAttribute(
                      target$jscomp$0,
                      propKey$jscomp$0,
                      propValue$jscomp$0
                    );
                }
            }
          target$jscomp$0.push(endOfStartTag);
          pushInnerHTML(target$jscomp$0, innerHTML$jscomp$0, children$jscomp$0);
          return children$jscomp$0;
        case "option":
          var selectedValue = formatContext.selectedValue;
          target$jscomp$0.push(startChunkForTag("option"));
          var children$jscomp$1 = null, value = null, selected = null, innerHTML$jscomp$1 = null, propKey$jscomp$1;
          for (propKey$jscomp$1 in props)
            if (hasOwnProperty.call(props, propKey$jscomp$1)) {
              var propValue$jscomp$1 = props[propKey$jscomp$1];
              if (null != propValue$jscomp$1)
                switch (propKey$jscomp$1) {
                  case "children":
                    children$jscomp$1 = propValue$jscomp$1;
                    break;
                  case "selected":
                    selected = propValue$jscomp$1;
                    break;
                  case "dangerouslySetInnerHTML":
                    innerHTML$jscomp$1 = propValue$jscomp$1;
                    break;
                  case "value":
                    value = propValue$jscomp$1;
                  default:
                    pushAttribute(
                      target$jscomp$0,
                      propKey$jscomp$1,
                      propValue$jscomp$1
                    );
                }
            }
          if (null != selectedValue) {
            var stringValue = null !== value ? "" + value : flattenOptionChildren(children$jscomp$1);
            if (isArrayImpl(selectedValue))
              for (var i = 0; i < selectedValue.length; i++) {
                if ("" + selectedValue[i] === stringValue) {
                  target$jscomp$0.push(selectedMarkerAttribute);
                  break;
                }
              }
            else
              "" + selectedValue === stringValue && target$jscomp$0.push(selectedMarkerAttribute);
          } else selected && target$jscomp$0.push(selectedMarkerAttribute);
          target$jscomp$0.push(endOfStartTag);
          pushInnerHTML(target$jscomp$0, innerHTML$jscomp$1, children$jscomp$1);
          return children$jscomp$1;
        case "textarea":
          target$jscomp$0.push(startChunkForTag("textarea"));
          var value$jscomp$0 = null, defaultValue = null, children$jscomp$2 = null, propKey$jscomp$2;
          for (propKey$jscomp$2 in props)
            if (hasOwnProperty.call(props, propKey$jscomp$2)) {
              var propValue$jscomp$2 = props[propKey$jscomp$2];
              if (null != propValue$jscomp$2)
                switch (propKey$jscomp$2) {
                  case "children":
                    children$jscomp$2 = propValue$jscomp$2;
                    break;
                  case "value":
                    value$jscomp$0 = propValue$jscomp$2;
                    break;
                  case "defaultValue":
                    defaultValue = propValue$jscomp$2;
                    break;
                  case "dangerouslySetInnerHTML":
                    throw Error(formatProdErrorMessage(91));
                  default:
                    pushAttribute(
                      target$jscomp$0,
                      propKey$jscomp$2,
                      propValue$jscomp$2
                    );
                }
            }
          null === value$jscomp$0 && null !== defaultValue && (value$jscomp$0 = defaultValue);
          target$jscomp$0.push(endOfStartTag);
          if (null != children$jscomp$2) {
            if (null != value$jscomp$0) throw Error(formatProdErrorMessage(92));
            if (isArrayImpl(children$jscomp$2)) {
              if (1 < children$jscomp$2.length)
                throw Error(formatProdErrorMessage(93));
              value$jscomp$0 = "" + children$jscomp$2[0];
            }
            value$jscomp$0 = "" + children$jscomp$2;
          }
          "string" === typeof value$jscomp$0 && "\n" === value$jscomp$0[0] && target$jscomp$0.push(leadingNewline);
          null !== value$jscomp$0 && target$jscomp$0.push(
            stringToChunk(escapeTextForBrowser("" + value$jscomp$0))
          );
          return null;
        case "input":
          target$jscomp$0.push(startChunkForTag("input"));
          var name = null, formAction = null, formEncType = null, formMethod = null, formTarget = null, value$jscomp$1 = null, defaultValue$jscomp$0 = null, checked = null, defaultChecked = null, propKey$jscomp$3;
          for (propKey$jscomp$3 in props)
            if (hasOwnProperty.call(props, propKey$jscomp$3)) {
              var propValue$jscomp$3 = props[propKey$jscomp$3];
              if (null != propValue$jscomp$3)
                switch (propKey$jscomp$3) {
                  case "children":
                  case "dangerouslySetInnerHTML":
                    throw Error(formatProdErrorMessage(399, "input"));
                  case "name":
                    name = propValue$jscomp$3;
                    break;
                  case "formAction":
                    formAction = propValue$jscomp$3;
                    break;
                  case "formEncType":
                    formEncType = propValue$jscomp$3;
                    break;
                  case "formMethod":
                    formMethod = propValue$jscomp$3;
                    break;
                  case "formTarget":
                    formTarget = propValue$jscomp$3;
                    break;
                  case "defaultChecked":
                    defaultChecked = propValue$jscomp$3;
                    break;
                  case "defaultValue":
                    defaultValue$jscomp$0 = propValue$jscomp$3;
                    break;
                  case "checked":
                    checked = propValue$jscomp$3;
                    break;
                  case "value":
                    value$jscomp$1 = propValue$jscomp$3;
                    break;
                  default:
                    pushAttribute(
                      target$jscomp$0,
                      propKey$jscomp$3,
                      propValue$jscomp$3
                    );
                }
            }
          var formData = pushFormActionAttribute(
            target$jscomp$0,
            resumableState,
            renderState,
            formAction,
            formEncType,
            formMethod,
            formTarget,
            name
          );
          null !== checked ? pushBooleanAttribute(target$jscomp$0, "checked", checked) : null !== defaultChecked && pushBooleanAttribute(target$jscomp$0, "checked", defaultChecked);
          null !== value$jscomp$1 ? pushAttribute(target$jscomp$0, "value", value$jscomp$1) : null !== defaultValue$jscomp$0 && pushAttribute(target$jscomp$0, "value", defaultValue$jscomp$0);
          target$jscomp$0.push(endOfStartTagSelfClosing);
          null != formData && formData.forEach(pushAdditionalFormField, target$jscomp$0);
          return null;
        case "button":
          target$jscomp$0.push(startChunkForTag("button"));
          var children$jscomp$3 = null, innerHTML$jscomp$2 = null, name$jscomp$0 = null, formAction$jscomp$0 = null, formEncType$jscomp$0 = null, formMethod$jscomp$0 = null, formTarget$jscomp$0 = null, propKey$jscomp$4;
          for (propKey$jscomp$4 in props)
            if (hasOwnProperty.call(props, propKey$jscomp$4)) {
              var propValue$jscomp$4 = props[propKey$jscomp$4];
              if (null != propValue$jscomp$4)
                switch (propKey$jscomp$4) {
                  case "children":
                    children$jscomp$3 = propValue$jscomp$4;
                    break;
                  case "dangerouslySetInnerHTML":
                    innerHTML$jscomp$2 = propValue$jscomp$4;
                    break;
                  case "name":
                    name$jscomp$0 = propValue$jscomp$4;
                    break;
                  case "formAction":
                    formAction$jscomp$0 = propValue$jscomp$4;
                    break;
                  case "formEncType":
                    formEncType$jscomp$0 = propValue$jscomp$4;
                    break;
                  case "formMethod":
                    formMethod$jscomp$0 = propValue$jscomp$4;
                    break;
                  case "formTarget":
                    formTarget$jscomp$0 = propValue$jscomp$4;
                    break;
                  default:
                    pushAttribute(
                      target$jscomp$0,
                      propKey$jscomp$4,
                      propValue$jscomp$4
                    );
                }
            }
          var formData$jscomp$0 = pushFormActionAttribute(
            target$jscomp$0,
            resumableState,
            renderState,
            formAction$jscomp$0,
            formEncType$jscomp$0,
            formMethod$jscomp$0,
            formTarget$jscomp$0,
            name$jscomp$0
          );
          target$jscomp$0.push(endOfStartTag);
          null != formData$jscomp$0 && formData$jscomp$0.forEach(pushAdditionalFormField, target$jscomp$0);
          pushInnerHTML(target$jscomp$0, innerHTML$jscomp$2, children$jscomp$3);
          if ("string" === typeof children$jscomp$3) {
            target$jscomp$0.push(
              stringToChunk(escapeTextForBrowser(children$jscomp$3))
            );
            var JSCompiler_inline_result$jscomp$0 = null;
          } else JSCompiler_inline_result$jscomp$0 = children$jscomp$3;
          return JSCompiler_inline_result$jscomp$0;
        case "form":
          target$jscomp$0.push(startChunkForTag("form"));
          var children$jscomp$4 = null, innerHTML$jscomp$3 = null, formAction$jscomp$1 = null, formEncType$jscomp$1 = null, formMethod$jscomp$1 = null, formTarget$jscomp$1 = null, propKey$jscomp$5;
          for (propKey$jscomp$5 in props)
            if (hasOwnProperty.call(props, propKey$jscomp$5)) {
              var propValue$jscomp$5 = props[propKey$jscomp$5];
              if (null != propValue$jscomp$5)
                switch (propKey$jscomp$5) {
                  case "children":
                    children$jscomp$4 = propValue$jscomp$5;
                    break;
                  case "dangerouslySetInnerHTML":
                    innerHTML$jscomp$3 = propValue$jscomp$5;
                    break;
                  case "action":
                    formAction$jscomp$1 = propValue$jscomp$5;
                    break;
                  case "encType":
                    formEncType$jscomp$1 = propValue$jscomp$5;
                    break;
                  case "method":
                    formMethod$jscomp$1 = propValue$jscomp$5;
                    break;
                  case "target":
                    formTarget$jscomp$1 = propValue$jscomp$5;
                    break;
                  default:
                    pushAttribute(
                      target$jscomp$0,
                      propKey$jscomp$5,
                      propValue$jscomp$5
                    );
                }
            }
          var formData$jscomp$1 = null, formActionName = null;
          if ("function" === typeof formAction$jscomp$1) {
            var customFields = getCustomFormFields(
              resumableState,
              formAction$jscomp$1
            );
            null !== customFields ? (formAction$jscomp$1 = customFields.action || "", formEncType$jscomp$1 = customFields.encType, formMethod$jscomp$1 = customFields.method, formTarget$jscomp$1 = customFields.target, formData$jscomp$1 = customFields.data, formActionName = customFields.name) : (target$jscomp$0.push(
              attributeSeparator,
              stringToChunk("action"),
              attributeAssign,
              actionJavaScriptURL,
              attributeEnd
            ), formTarget$jscomp$1 = formMethod$jscomp$1 = formEncType$jscomp$1 = formAction$jscomp$1 = null, injectFormReplayingRuntime(resumableState, renderState));
          }
          null != formAction$jscomp$1 && pushAttribute(target$jscomp$0, "action", formAction$jscomp$1);
          null != formEncType$jscomp$1 && pushAttribute(target$jscomp$0, "encType", formEncType$jscomp$1);
          null != formMethod$jscomp$1 && pushAttribute(target$jscomp$0, "method", formMethod$jscomp$1);
          null != formTarget$jscomp$1 && pushAttribute(target$jscomp$0, "target", formTarget$jscomp$1);
          target$jscomp$0.push(endOfStartTag);
          null !== formActionName && (target$jscomp$0.push(startHiddenInputChunk), pushStringAttribute(target$jscomp$0, "name", formActionName), target$jscomp$0.push(endOfStartTagSelfClosing), null != formData$jscomp$1 && formData$jscomp$1.forEach(pushAdditionalFormField, target$jscomp$0));
          pushInnerHTML(target$jscomp$0, innerHTML$jscomp$3, children$jscomp$4);
          if ("string" === typeof children$jscomp$4) {
            target$jscomp$0.push(
              stringToChunk(escapeTextForBrowser(children$jscomp$4))
            );
            var JSCompiler_inline_result$jscomp$1 = null;
          } else JSCompiler_inline_result$jscomp$1 = children$jscomp$4;
          return JSCompiler_inline_result$jscomp$1;
        case "menuitem":
          target$jscomp$0.push(startChunkForTag("menuitem"));
          for (var propKey$jscomp$6 in props)
            if (hasOwnProperty.call(props, propKey$jscomp$6)) {
              var propValue$jscomp$6 = props[propKey$jscomp$6];
              if (null != propValue$jscomp$6)
                switch (propKey$jscomp$6) {
                  case "children":
                  case "dangerouslySetInnerHTML":
                    throw Error(formatProdErrorMessage(400));
                  default:
                    pushAttribute(
                      target$jscomp$0,
                      propKey$jscomp$6,
                      propValue$jscomp$6
                    );
                }
            }
          target$jscomp$0.push(endOfStartTag);
          return null;
        case "object":
          target$jscomp$0.push(startChunkForTag("object"));
          var children$jscomp$5 = null, innerHTML$jscomp$4 = null, propKey$jscomp$7;
          for (propKey$jscomp$7 in props)
            if (hasOwnProperty.call(props, propKey$jscomp$7)) {
              var propValue$jscomp$7 = props[propKey$jscomp$7];
              if (null != propValue$jscomp$7)
                switch (propKey$jscomp$7) {
                  case "children":
                    children$jscomp$5 = propValue$jscomp$7;
                    break;
                  case "dangerouslySetInnerHTML":
                    innerHTML$jscomp$4 = propValue$jscomp$7;
                    break;
                  case "data":
                    var sanitizedValue = sanitizeURL("" + propValue$jscomp$7);
                    if ("" === sanitizedValue) break;
                    target$jscomp$0.push(
                      attributeSeparator,
                      stringToChunk("data"),
                      attributeAssign,
                      stringToChunk(escapeTextForBrowser(sanitizedValue)),
                      attributeEnd
                    );
                    break;
                  default:
                    pushAttribute(
                      target$jscomp$0,
                      propKey$jscomp$7,
                      propValue$jscomp$7
                    );
                }
            }
          target$jscomp$0.push(endOfStartTag);
          pushInnerHTML(target$jscomp$0, innerHTML$jscomp$4, children$jscomp$5);
          if ("string" === typeof children$jscomp$5) {
            target$jscomp$0.push(
              stringToChunk(escapeTextForBrowser(children$jscomp$5))
            );
            var JSCompiler_inline_result$jscomp$2 = null;
          } else JSCompiler_inline_result$jscomp$2 = children$jscomp$5;
          return JSCompiler_inline_result$jscomp$2;
        case "title":
          var noscriptTagInScope = formatContext.tagScope & 1, isFallback = formatContext.tagScope & 4;
          if (4 === formatContext.insertionMode || noscriptTagInScope || null != props.itemProp)
            var JSCompiler_inline_result$jscomp$3 = pushTitleImpl(
              target$jscomp$0,
              props
            );
          else
            isFallback ? JSCompiler_inline_result$jscomp$3 = null : (pushTitleImpl(renderState.hoistableChunks, props), JSCompiler_inline_result$jscomp$3 = void 0);
          return JSCompiler_inline_result$jscomp$3;
        case "link":
          var noscriptTagInScope$jscomp$0 = formatContext.tagScope & 1, isFallback$jscomp$0 = formatContext.tagScope & 4, rel = props.rel, href = props.href, precedence = props.precedence;
          if (4 === formatContext.insertionMode || noscriptTagInScope$jscomp$0 || null != props.itemProp || "string" !== typeof rel || "string" !== typeof href || "" === href) {
            pushLinkImpl(target$jscomp$0, props);
            var JSCompiler_inline_result$jscomp$4 = null;
          } else if ("stylesheet" === props.rel)
            if ("string" !== typeof precedence || null != props.disabled || props.onLoad || props.onError)
              JSCompiler_inline_result$jscomp$4 = pushLinkImpl(
                target$jscomp$0,
                props
              );
            else {
              var styleQueue = renderState.styles.get(precedence), resourceState = resumableState.styleResources.hasOwnProperty(href) ? resumableState.styleResources[href] : void 0;
              if (null !== resourceState) {
                resumableState.styleResources[href] = null;
                styleQueue || (styleQueue = {
                  precedence: stringToChunk(escapeTextForBrowser(precedence)),
                  rules: [],
                  hrefs: [],
                  sheets: /* @__PURE__ */ new Map()
                }, renderState.styles.set(precedence, styleQueue));
                var resource = {
                  state: 0,
                  props: assign({}, props, {
                    "data-precedence": props.precedence,
                    precedence: null
                  })
                };
                if (resourceState) {
                  2 === resourceState.length && adoptPreloadCredentials(resource.props, resourceState);
                  var preloadResource = renderState.preloads.stylesheets.get(href);
                  preloadResource && 0 < preloadResource.length ? preloadResource.length = 0 : resource.state = 1;
                }
                styleQueue.sheets.set(href, resource);
                hoistableState && hoistableState.stylesheets.add(resource);
              } else if (styleQueue) {
                var resource$9 = styleQueue.sheets.get(href);
                resource$9 && hoistableState && hoistableState.stylesheets.add(resource$9);
              }
              textEmbedded && target$jscomp$0.push(textSeparator);
              JSCompiler_inline_result$jscomp$4 = null;
            }
          else
            props.onLoad || props.onError ? JSCompiler_inline_result$jscomp$4 = pushLinkImpl(
              target$jscomp$0,
              props
            ) : (textEmbedded && target$jscomp$0.push(textSeparator), JSCompiler_inline_result$jscomp$4 = isFallback$jscomp$0 ? null : pushLinkImpl(renderState.hoistableChunks, props));
          return JSCompiler_inline_result$jscomp$4;
        case "script":
          var noscriptTagInScope$jscomp$1 = formatContext.tagScope & 1, asyncProp = props.async;
          if ("string" !== typeof props.src || !props.src || !asyncProp || "function" === typeof asyncProp || "symbol" === typeof asyncProp || props.onLoad || props.onError || 4 === formatContext.insertionMode || noscriptTagInScope$jscomp$1 || null != props.itemProp)
            var JSCompiler_inline_result$jscomp$5 = pushScriptImpl(
              target$jscomp$0,
              props
            );
          else {
            var key = props.src;
            if ("module" === props.type) {
              var resources = resumableState.moduleScriptResources;
              var preloads = renderState.preloads.moduleScripts;
            } else
              resources = resumableState.scriptResources, preloads = renderState.preloads.scripts;
            var resourceState$jscomp$0 = resources.hasOwnProperty(key) ? resources[key] : void 0;
            if (null !== resourceState$jscomp$0) {
              resources[key] = null;
              var scriptProps = props;
              if (resourceState$jscomp$0) {
                2 === resourceState$jscomp$0.length && (scriptProps = assign({}, props), adoptPreloadCredentials(scriptProps, resourceState$jscomp$0));
                var preloadResource$jscomp$0 = preloads.get(key);
                preloadResource$jscomp$0 && (preloadResource$jscomp$0.length = 0);
              }
              var resource$jscomp$0 = [];
              renderState.scripts.add(resource$jscomp$0);
              pushScriptImpl(resource$jscomp$0, scriptProps);
            }
            textEmbedded && target$jscomp$0.push(textSeparator);
            JSCompiler_inline_result$jscomp$5 = null;
          }
          return JSCompiler_inline_result$jscomp$5;
        case "style":
          var noscriptTagInScope$jscomp$2 = formatContext.tagScope & 1, precedence$jscomp$0 = props.precedence, href$jscomp$0 = props.href, nonce = props.nonce;
          if (4 === formatContext.insertionMode || noscriptTagInScope$jscomp$2 || null != props.itemProp || "string" !== typeof precedence$jscomp$0 || "string" !== typeof href$jscomp$0 || "" === href$jscomp$0) {
            target$jscomp$0.push(startChunkForTag("style"));
            var children$jscomp$6 = null, innerHTML$jscomp$5 = null, propKey$jscomp$8;
            for (propKey$jscomp$8 in props)
              if (hasOwnProperty.call(props, propKey$jscomp$8)) {
                var propValue$jscomp$8 = props[propKey$jscomp$8];
                if (null != propValue$jscomp$8)
                  switch (propKey$jscomp$8) {
                    case "children":
                      children$jscomp$6 = propValue$jscomp$8;
                      break;
                    case "dangerouslySetInnerHTML":
                      innerHTML$jscomp$5 = propValue$jscomp$8;
                      break;
                    default:
                      pushAttribute(
                        target$jscomp$0,
                        propKey$jscomp$8,
                        propValue$jscomp$8
                      );
                  }
              }
            target$jscomp$0.push(endOfStartTag);
            var child = Array.isArray(children$jscomp$6) ? 2 > children$jscomp$6.length ? children$jscomp$6[0] : null : children$jscomp$6;
            "function" !== typeof child && "symbol" !== typeof child && null !== child && void 0 !== child && target$jscomp$0.push(
              stringToChunk(("" + child).replace(styleRegex, styleReplacer))
            );
            pushInnerHTML(target$jscomp$0, innerHTML$jscomp$5, children$jscomp$6);
            target$jscomp$0.push(endChunkForTag("style"));
            var JSCompiler_inline_result$jscomp$6 = null;
          } else {
            var styleQueue$jscomp$0 = renderState.styles.get(precedence$jscomp$0);
            if (null !== (resumableState.styleResources.hasOwnProperty(href$jscomp$0) ? resumableState.styleResources[href$jscomp$0] : void 0)) {
              resumableState.styleResources[href$jscomp$0] = null;
              styleQueue$jscomp$0 || (styleQueue$jscomp$0 = {
                precedence: stringToChunk(
                  escapeTextForBrowser(precedence$jscomp$0)
                ),
                rules: [],
                hrefs: [],
                sheets: /* @__PURE__ */ new Map()
              }, renderState.styles.set(precedence$jscomp$0, styleQueue$jscomp$0));
              var nonceStyle = renderState.nonce.style;
              if (!nonceStyle || nonceStyle === nonce) {
                styleQueue$jscomp$0.hrefs.push(
                  stringToChunk(escapeTextForBrowser(href$jscomp$0))
                );
                var target = styleQueue$jscomp$0.rules, children$jscomp$7 = null, innerHTML$jscomp$6 = null, propKey$jscomp$9;
                for (propKey$jscomp$9 in props)
                  if (hasOwnProperty.call(props, propKey$jscomp$9)) {
                    var propValue$jscomp$9 = props[propKey$jscomp$9];
                    if (null != propValue$jscomp$9)
                      switch (propKey$jscomp$9) {
                        case "children":
                          children$jscomp$7 = propValue$jscomp$9;
                          break;
                        case "dangerouslySetInnerHTML":
                          innerHTML$jscomp$6 = propValue$jscomp$9;
                      }
                  }
                var child$jscomp$0 = Array.isArray(children$jscomp$7) ? 2 > children$jscomp$7.length ? children$jscomp$7[0] : null : children$jscomp$7;
                "function" !== typeof child$jscomp$0 && "symbol" !== typeof child$jscomp$0 && null !== child$jscomp$0 && void 0 !== child$jscomp$0 && target.push(
                  stringToChunk(
                    ("" + child$jscomp$0).replace(styleRegex, styleReplacer)
                  )
                );
                pushInnerHTML(target, innerHTML$jscomp$6, children$jscomp$7);
              }
            }
            styleQueue$jscomp$0 && hoistableState && hoistableState.styles.add(styleQueue$jscomp$0);
            textEmbedded && target$jscomp$0.push(textSeparator);
            JSCompiler_inline_result$jscomp$6 = void 0;
          }
          return JSCompiler_inline_result$jscomp$6;
        case "meta":
          var noscriptTagInScope$jscomp$3 = formatContext.tagScope & 1, isFallback$jscomp$1 = formatContext.tagScope & 4;
          if (4 === formatContext.insertionMode || noscriptTagInScope$jscomp$3 || null != props.itemProp)
            var JSCompiler_inline_result$jscomp$7 = pushSelfClosing(
              target$jscomp$0,
              props,
              "meta"
            );
          else
            textEmbedded && target$jscomp$0.push(textSeparator), JSCompiler_inline_result$jscomp$7 = isFallback$jscomp$1 ? null : "string" === typeof props.charSet ? pushSelfClosing(renderState.charsetChunks, props, "meta") : "viewport" === props.name ? pushSelfClosing(renderState.viewportChunks, props, "meta") : pushSelfClosing(renderState.hoistableChunks, props, "meta");
          return JSCompiler_inline_result$jscomp$7;
        case "listing":
        case "pre":
          target$jscomp$0.push(startChunkForTag(type));
          var children$jscomp$8 = null, innerHTML$jscomp$7 = null, propKey$jscomp$10;
          for (propKey$jscomp$10 in props)
            if (hasOwnProperty.call(props, propKey$jscomp$10)) {
              var propValue$jscomp$10 = props[propKey$jscomp$10];
              if (null != propValue$jscomp$10)
                switch (propKey$jscomp$10) {
                  case "children":
                    children$jscomp$8 = propValue$jscomp$10;
                    break;
                  case "dangerouslySetInnerHTML":
                    innerHTML$jscomp$7 = propValue$jscomp$10;
                    break;
                  default:
                    pushAttribute(
                      target$jscomp$0,
                      propKey$jscomp$10,
                      propValue$jscomp$10
                    );
                }
            }
          target$jscomp$0.push(endOfStartTag);
          if (null != innerHTML$jscomp$7) {
            if (null != children$jscomp$8) throw Error(formatProdErrorMessage(60));
            if ("object" !== typeof innerHTML$jscomp$7 || !("__html" in innerHTML$jscomp$7))
              throw Error(formatProdErrorMessage(61));
            var html = innerHTML$jscomp$7.__html;
            null !== html && void 0 !== html && ("string" === typeof html && 0 < html.length && "\n" === html[0] ? target$jscomp$0.push(leadingNewline, stringToChunk(html)) : target$jscomp$0.push(stringToChunk("" + html)));
          }
          "string" === typeof children$jscomp$8 && "\n" === children$jscomp$8[0] && target$jscomp$0.push(leadingNewline);
          return children$jscomp$8;
        case "img":
          var pictureOrNoScriptTagInScope = formatContext.tagScope & 3, src = props.src, srcSet = props.srcSet;
          if (!("lazy" === props.loading || !src && !srcSet || "string" !== typeof src && null != src || "string" !== typeof srcSet && null != srcSet || "low" === props.fetchPriority || pictureOrNoScriptTagInScope) && ("string" !== typeof src || ":" !== src[4] || "d" !== src[0] && "D" !== src[0] || "a" !== src[1] && "A" !== src[1] || "t" !== src[2] && "T" !== src[2] || "a" !== src[3] && "A" !== src[3]) && ("string" !== typeof srcSet || ":" !== srcSet[4] || "d" !== srcSet[0] && "D" !== srcSet[0] || "a" !== srcSet[1] && "A" !== srcSet[1] || "t" !== srcSet[2] && "T" !== srcSet[2] || "a" !== srcSet[3] && "A" !== srcSet[3])) {
            null !== hoistableState && formatContext.tagScope & 64 && (hoistableState.suspenseyImages = true);
            var sizes = "string" === typeof props.sizes ? props.sizes : void 0, key$jscomp$0 = srcSet ? srcSet + "\n" + (sizes || "") : src, promotablePreloads = renderState.preloads.images, resource$jscomp$1 = promotablePreloads.get(key$jscomp$0);
            if (resource$jscomp$1) {
              if ("high" === props.fetchPriority || 10 > renderState.highImagePreloads.size)
                promotablePreloads.delete(key$jscomp$0), renderState.highImagePreloads.add(resource$jscomp$1);
            } else if (!resumableState.imageResources.hasOwnProperty(key$jscomp$0)) {
              resumableState.imageResources[key$jscomp$0] = PRELOAD_NO_CREDS;
              var input = props.crossOrigin;
              var JSCompiler_inline_result$jscomp$8 = "string" === typeof input ? "use-credentials" === input ? input : "" : void 0;
              var headers = renderState.headers, header;
              headers && 0 < headers.remainingCapacity && "string" !== typeof props.srcSet && ("high" === props.fetchPriority || 500 > headers.highImagePreloads.length) && (header = getPreloadAsHeader(src, "image", {
                imageSrcSet: props.srcSet,
                imageSizes: props.sizes,
                crossOrigin: JSCompiler_inline_result$jscomp$8,
                integrity: props.integrity,
                nonce: props.nonce,
                type: props.type,
                fetchPriority: props.fetchPriority,
                referrerPolicy: props.refererPolicy
              }), 0 <= (headers.remainingCapacity -= header.length + 2)) ? (renderState.resets.image[key$jscomp$0] = PRELOAD_NO_CREDS, headers.highImagePreloads && (headers.highImagePreloads += ", "), headers.highImagePreloads += header) : (resource$jscomp$1 = [], pushLinkImpl(resource$jscomp$1, {
                rel: "preload",
                as: "image",
                href: srcSet ? void 0 : src,
                imageSrcSet: srcSet,
                imageSizes: sizes,
                crossOrigin: JSCompiler_inline_result$jscomp$8,
                integrity: props.integrity,
                type: props.type,
                fetchPriority: props.fetchPriority,
                referrerPolicy: props.referrerPolicy
              }), "high" === props.fetchPriority || 10 > renderState.highImagePreloads.size ? renderState.highImagePreloads.add(resource$jscomp$1) : (renderState.bulkPreloads.add(resource$jscomp$1), promotablePreloads.set(key$jscomp$0, resource$jscomp$1)));
            }
          }
          return pushSelfClosing(target$jscomp$0, props, "img");
        case "base":
        case "area":
        case "br":
        case "col":
        case "embed":
        case "hr":
        case "keygen":
        case "param":
        case "source":
        case "track":
        case "wbr":
          return pushSelfClosing(target$jscomp$0, props, type);
        case "annotation-xml":
        case "color-profile":
        case "font-face":
        case "font-face-src":
        case "font-face-uri":
        case "font-face-format":
        case "font-face-name":
        case "missing-glyph":
          break;
        case "head":
          if (2 > formatContext.insertionMode) {
            var preamble = preambleState || renderState.preamble;
            if (preamble.headChunks)
              throw Error(formatProdErrorMessage(545, "`<head>`"));
            null !== preambleState && target$jscomp$0.push(headPreambleContributionChunk);
            preamble.headChunks = [];
            var JSCompiler_inline_result$jscomp$9 = pushStartSingletonElement(
              preamble.headChunks,
              props,
              "head"
            );
          } else
            JSCompiler_inline_result$jscomp$9 = pushStartGenericElement(
              target$jscomp$0,
              props,
              "head"
            );
          return JSCompiler_inline_result$jscomp$9;
        case "body":
          if (2 > formatContext.insertionMode) {
            var preamble$jscomp$0 = preambleState || renderState.preamble;
            if (preamble$jscomp$0.bodyChunks)
              throw Error(formatProdErrorMessage(545, "`<body>`"));
            null !== preambleState && target$jscomp$0.push(bodyPreambleContributionChunk);
            preamble$jscomp$0.bodyChunks = [];
            var JSCompiler_inline_result$jscomp$10 = pushStartSingletonElement(
              preamble$jscomp$0.bodyChunks,
              props,
              "body"
            );
          } else
            JSCompiler_inline_result$jscomp$10 = pushStartGenericElement(
              target$jscomp$0,
              props,
              "body"
            );
          return JSCompiler_inline_result$jscomp$10;
        case "html":
          if (0 === formatContext.insertionMode) {
            var preamble$jscomp$1 = preambleState || renderState.preamble;
            if (preamble$jscomp$1.htmlChunks)
              throw Error(formatProdErrorMessage(545, "`<html>`"));
            null !== preambleState && target$jscomp$0.push(htmlPreambleContributionChunk);
            preamble$jscomp$1.htmlChunks = [doctypeChunk];
            var JSCompiler_inline_result$jscomp$11 = pushStartSingletonElement(
              preamble$jscomp$1.htmlChunks,
              props,
              "html"
            );
          } else
            JSCompiler_inline_result$jscomp$11 = pushStartGenericElement(
              target$jscomp$0,
              props,
              "html"
            );
          return JSCompiler_inline_result$jscomp$11;
        default:
          if (-1 !== type.indexOf("-")) {
            target$jscomp$0.push(startChunkForTag(type));
            var children$jscomp$9 = null, innerHTML$jscomp$8 = null, propKey$jscomp$11;
            for (propKey$jscomp$11 in props)
              if (hasOwnProperty.call(props, propKey$jscomp$11)) {
                var propValue$jscomp$11 = props[propKey$jscomp$11];
                if (null != propValue$jscomp$11) {
                  var attributeName = propKey$jscomp$11;
                  switch (propKey$jscomp$11) {
                    case "children":
                      children$jscomp$9 = propValue$jscomp$11;
                      break;
                    case "dangerouslySetInnerHTML":
                      innerHTML$jscomp$8 = propValue$jscomp$11;
                      break;
                    case "style":
                      pushStyleAttribute(target$jscomp$0, propValue$jscomp$11);
                      break;
                    case "suppressContentEditableWarning":
                    case "suppressHydrationWarning":
                    case "ref":
                      break;
                    case "className":
                      attributeName = "class";
                    default:
                      if (isAttributeNameSafe(propKey$jscomp$11) && "function" !== typeof propValue$jscomp$11 && "symbol" !== typeof propValue$jscomp$11 && false !== propValue$jscomp$11) {
                        if (true === propValue$jscomp$11) propValue$jscomp$11 = "";
                        else if ("object" === typeof propValue$jscomp$11) continue;
                        target$jscomp$0.push(
                          attributeSeparator,
                          stringToChunk(attributeName),
                          attributeAssign,
                          stringToChunk(escapeTextForBrowser(propValue$jscomp$11)),
                          attributeEnd
                        );
                      }
                  }
                }
              }
            target$jscomp$0.push(endOfStartTag);
            pushInnerHTML(target$jscomp$0, innerHTML$jscomp$8, children$jscomp$9);
            return children$jscomp$9;
          }
      }
      return pushStartGenericElement(target$jscomp$0, props, type);
    }
    var endTagCache = /* @__PURE__ */ new Map();
    function endChunkForTag(tag) {
      var chunk = endTagCache.get(tag);
      void 0 === chunk && (chunk = stringToPrecomputedChunk("</" + tag + ">"), endTagCache.set(tag, chunk));
      return chunk;
    }
    function hoistPreambleState(renderState, preambleState) {
      renderState = renderState.preamble;
      null === renderState.htmlChunks && preambleState.htmlChunks && (renderState.htmlChunks = preambleState.htmlChunks);
      null === renderState.headChunks && preambleState.headChunks && (renderState.headChunks = preambleState.headChunks);
      null === renderState.bodyChunks && preambleState.bodyChunks && (renderState.bodyChunks = preambleState.bodyChunks);
    }
    function writeBootstrap(destination, renderState) {
      renderState = renderState.bootstrapChunks;
      for (var i = 0; i < renderState.length - 1; i++)
        writeChunk(destination, renderState[i]);
      return i < renderState.length ? (i = renderState[i], renderState.length = 0, writeChunkAndReturn(destination, i)) : true;
    }
    var shellTimeRuntimeScript = stringToPrecomputedChunk(
      "requestAnimationFrame(function(){$RT=performance.now()});"
    );
    var placeholder1 = stringToPrecomputedChunk('<template id="');
    var placeholder2 = stringToPrecomputedChunk('"></template>');
    var startActivityBoundary = stringToPrecomputedChunk("<!--&-->");
    var endActivityBoundary = stringToPrecomputedChunk("<!--/&-->");
    var startCompletedSuspenseBoundary = stringToPrecomputedChunk("<!--$-->");
    var startPendingSuspenseBoundary1 = stringToPrecomputedChunk(
      '<!--$?--><template id="'
    );
    var startPendingSuspenseBoundary2 = stringToPrecomputedChunk('"></template>');
    var startClientRenderedSuspenseBoundary = stringToPrecomputedChunk("<!--$!-->");
    var endSuspenseBoundary = stringToPrecomputedChunk("<!--/$-->");
    var clientRenderedSuspenseBoundaryError1 = stringToPrecomputedChunk("<template");
    var clientRenderedSuspenseBoundaryErrorAttrInterstitial = stringToPrecomputedChunk('"');
    var clientRenderedSuspenseBoundaryError1A = stringToPrecomputedChunk(' data-dgst="');
    stringToPrecomputedChunk(' data-msg="');
    stringToPrecomputedChunk(' data-stck="');
    stringToPrecomputedChunk(' data-cstck="');
    var clientRenderedSuspenseBoundaryError2 = stringToPrecomputedChunk("></template>");
    function writeStartPendingSuspenseBoundary(destination, renderState, id) {
      writeChunk(destination, startPendingSuspenseBoundary1);
      if (null === id) throw Error(formatProdErrorMessage(395));
      writeChunk(destination, renderState.boundaryPrefix);
      writeChunk(destination, stringToChunk(id.toString(16)));
      return writeChunkAndReturn(destination, startPendingSuspenseBoundary2);
    }
    var startSegmentHTML = stringToPrecomputedChunk('<div hidden id="');
    var startSegmentHTML2 = stringToPrecomputedChunk('">');
    var endSegmentHTML = stringToPrecomputedChunk("</div>");
    var startSegmentSVG = stringToPrecomputedChunk(
      '<svg aria-hidden="true" style="display:none" id="'
    );
    var startSegmentSVG2 = stringToPrecomputedChunk('">');
    var endSegmentSVG = stringToPrecomputedChunk("</svg>");
    var startSegmentMathML = stringToPrecomputedChunk(
      '<math aria-hidden="true" style="display:none" id="'
    );
    var startSegmentMathML2 = stringToPrecomputedChunk('">');
    var endSegmentMathML = stringToPrecomputedChunk("</math>");
    var startSegmentTable = stringToPrecomputedChunk('<table hidden id="');
    var startSegmentTable2 = stringToPrecomputedChunk('">');
    var endSegmentTable = stringToPrecomputedChunk("</table>");
    var startSegmentTableBody = stringToPrecomputedChunk('<table hidden><tbody id="');
    var startSegmentTableBody2 = stringToPrecomputedChunk('">');
    var endSegmentTableBody = stringToPrecomputedChunk("</tbody></table>");
    var startSegmentTableRow = stringToPrecomputedChunk('<table hidden><tr id="');
    var startSegmentTableRow2 = stringToPrecomputedChunk('">');
    var endSegmentTableRow = stringToPrecomputedChunk("</tr></table>");
    var startSegmentColGroup = stringToPrecomputedChunk(
      '<table hidden><colgroup id="'
    );
    var startSegmentColGroup2 = stringToPrecomputedChunk('">');
    var endSegmentColGroup = stringToPrecomputedChunk("</colgroup></table>");
    function writeStartSegment(destination, renderState, formatContext, id) {
      switch (formatContext.insertionMode) {
        case 0:
        case 1:
        case 3:
        case 2:
          return writeChunk(destination, startSegmentHTML), writeChunk(destination, renderState.segmentPrefix), writeChunk(destination, stringToChunk(id.toString(16))), writeChunkAndReturn(destination, startSegmentHTML2);
        case 4:
          return writeChunk(destination, startSegmentSVG), writeChunk(destination, renderState.segmentPrefix), writeChunk(destination, stringToChunk(id.toString(16))), writeChunkAndReturn(destination, startSegmentSVG2);
        case 5:
          return writeChunk(destination, startSegmentMathML), writeChunk(destination, renderState.segmentPrefix), writeChunk(destination, stringToChunk(id.toString(16))), writeChunkAndReturn(destination, startSegmentMathML2);
        case 6:
          return writeChunk(destination, startSegmentTable), writeChunk(destination, renderState.segmentPrefix), writeChunk(destination, stringToChunk(id.toString(16))), writeChunkAndReturn(destination, startSegmentTable2);
        case 7:
          return writeChunk(destination, startSegmentTableBody), writeChunk(destination, renderState.segmentPrefix), writeChunk(destination, stringToChunk(id.toString(16))), writeChunkAndReturn(destination, startSegmentTableBody2);
        case 8:
          return writeChunk(destination, startSegmentTableRow), writeChunk(destination, renderState.segmentPrefix), writeChunk(destination, stringToChunk(id.toString(16))), writeChunkAndReturn(destination, startSegmentTableRow2);
        case 9:
          return writeChunk(destination, startSegmentColGroup), writeChunk(destination, renderState.segmentPrefix), writeChunk(destination, stringToChunk(id.toString(16))), writeChunkAndReturn(destination, startSegmentColGroup2);
        default:
          throw Error(formatProdErrorMessage(397));
      }
    }
    function writeEndSegment(destination, formatContext) {
      switch (formatContext.insertionMode) {
        case 0:
        case 1:
        case 3:
        case 2:
          return writeChunkAndReturn(destination, endSegmentHTML);
        case 4:
          return writeChunkAndReturn(destination, endSegmentSVG);
        case 5:
          return writeChunkAndReturn(destination, endSegmentMathML);
        case 6:
          return writeChunkAndReturn(destination, endSegmentTable);
        case 7:
          return writeChunkAndReturn(destination, endSegmentTableBody);
        case 8:
          return writeChunkAndReturn(destination, endSegmentTableRow);
        case 9:
          return writeChunkAndReturn(destination, endSegmentColGroup);
        default:
          throw Error(formatProdErrorMessage(397));
      }
    }
    var completeSegmentScript1Full = stringToPrecomputedChunk(
      '$RS=function(a,b){a=document.getElementById(a);b=document.getElementById(b);for(a.parentNode.removeChild(a);a.firstChild;)b.parentNode.insertBefore(a.firstChild,b);b.parentNode.removeChild(b)};$RS("'
    );
    var completeSegmentScript1Partial = stringToPrecomputedChunk('$RS("');
    var completeSegmentScript2 = stringToPrecomputedChunk('","');
    var completeSegmentScriptEnd = stringToPrecomputedChunk('")<\/script>');
    stringToPrecomputedChunk('<template data-rsi="" data-sid="');
    stringToPrecomputedChunk('" data-pid="');
    var completeBoundaryScriptFunctionOnly = stringToPrecomputedChunk(
      '$RB=[];$RV=function(a){$RT=performance.now();for(var b=0;b<a.length;b+=2){var c=a[b],e=a[b+1];null!==e.parentNode&&e.parentNode.removeChild(e);var f=c.parentNode;if(f){var g=c.previousSibling,h=0;do{if(c&&8===c.nodeType){var d=c.data;if("/$"===d||"/&"===d)if(0===h)break;else h--;else"$"!==d&&"$?"!==d&&"$~"!==d&&"$!"!==d&&"&"!==d||h++}d=c.nextSibling;f.removeChild(c);c=d}while(c);for(;e.firstChild;)f.insertBefore(e.firstChild,c);g.data="$";g._reactRetry&&requestAnimationFrame(g._reactRetry)}}a.length=0};\n$RC=function(a,b){if(b=document.getElementById(b))(a=document.getElementById(a))?(a.previousSibling.data="$~",$RB.push(a,b),2===$RB.length&&("number"!==typeof $RT?requestAnimationFrame($RV.bind(null,$RB)):(a=performance.now(),setTimeout($RV.bind(null,$RB),2300>a&&2E3<a?2300-a:$RT+300-a)))):b.parentNode.removeChild(b)};'
    );
    stringToChunk(
      `$RV=function(A,g){function k(a,b){var e=a.getAttribute(b);e&&(b=a.style,l.push(a,b.viewTransitionName,b.viewTransitionClass),"auto"!==e&&(b.viewTransitionClass=e),(a=a.getAttribute("vt-name"))||(a="_T_"+K++ +"_"),b.viewTransitionName=a,B=!0)}var B=!1,K=0,l=[];try{var f=document.__reactViewTransition;if(f){f.finished.finally($RV.bind(null,g));return}var m=new Map;for(f=1;f<g.length;f+=2)for(var h=g[f].querySelectorAll("[vt-share]"),d=0;d<h.length;d++){var c=h[d];m.set(c.getAttribute("vt-name"),c)}var u=[];for(h=0;h<g.length;h+=2){var C=g[h],x=C.parentNode;if(x){var v=x.getBoundingClientRect();if(v.left||v.top||v.width||v.height){c=C;for(f=0;c;){if(8===c.nodeType){var r=c.data;if("/$"===r)if(0===f)break;else f--;else"$"!==r&&"$?"!==r&&"$~"!==r&&"$!"!==r||f++}else if(1===c.nodeType){d=c;var D=d.getAttribute("vt-name"),y=m.get(D);k(d,y?"vt-share":"vt-exit");y&&(k(y,"vt-share"),m.set(D,null));var E=d.querySelectorAll("[vt-share]");for(d=0;d<E.length;d++){var F=E[d],G=F.getAttribute("vt-name"),
H=m.get(G);H&&(k(F,"vt-share"),k(H,"vt-share"),m.set(G,null))}}c=c.nextSibling}for(var I=g[h+1],t=I.firstElementChild;t;)null!==m.get(t.getAttribute("vt-name"))&&k(t,"vt-enter"),t=t.nextElementSibling;c=x;do for(var n=c.firstElementChild;n;){var J=n.getAttribute("vt-update");J&&"none"!==J&&!l.includes(n)&&k(n,"vt-update");n=n.nextElementSibling}while((c=c.parentNode)&&1===c.nodeType&&"none"!==c.getAttribute("vt-update"));u.push.apply(u,I.querySelectorAll('img[src]:not([loading="lazy"])'))}}}if(B){var z=
document.__reactViewTransition=document.startViewTransition({update:function(){A(g);for(var a=[document.documentElement.clientHeight,document.fonts.ready],b={},e=0;e<u.length;b={g:b.g},e++)if(b.g=u[e],!b.g.complete){var p=b.g.getBoundingClientRect();0<p.bottom&&0<p.right&&p.top<window.innerHeight&&p.left<window.innerWidth&&(p=new Promise(function(w){return function(q){w.g.addEventListener("load",q);w.g.addEventListener("error",q)}}(b)),a.push(p))}return Promise.race([Promise.all(a),new Promise(function(w){var q=
performance.now();setTimeout(w,2300>q&&2E3<q?2300-q:500)})])},types:[]});z.ready.finally(function(){for(var a=l.length-3;0<=a;a-=3){var b=l[a],e=b.style;e.viewTransitionName=l[a+1];e.viewTransitionClass=l[a+1];""===b.getAttribute("style")&&b.removeAttribute("style")}});z.finished.finally(function(){document.__reactViewTransition===z&&(document.__reactViewTransition=null)});$RB=[];return}}catch(a){}A(g)}.bind(null,$RV);`
    );
    var completeBoundaryScript1Partial = stringToPrecomputedChunk('$RC("');
    var completeBoundaryWithStylesScript1FullPartial = stringToPrecomputedChunk(
      '$RM=new Map;$RR=function(n,w,p){function u(q){this._p=null;q()}for(var r=new Map,t=document,h,b,e=t.querySelectorAll("link[data-precedence],style[data-precedence]"),v=[],k=0;b=e[k++];)"not all"===b.getAttribute("media")?v.push(b):("LINK"===b.tagName&&$RM.set(b.getAttribute("href"),b),r.set(b.dataset.precedence,h=b));e=0;b=[];var l,a;for(k=!0;;){if(k){var f=p[e++];if(!f){k=!1;e=0;continue}var c=!1,m=0;var d=f[m++];if(a=$RM.get(d)){var g=a._p;c=!0}else{a=t.createElement("link");a.href=d;a.rel=\n"stylesheet";for(a.dataset.precedence=l=f[m++];g=f[m++];)a.setAttribute(g,f[m++]);g=a._p=new Promise(function(q,x){a.onload=u.bind(a,q);a.onerror=u.bind(a,x)});$RM.set(d,a)}d=a.getAttribute("media");!g||d&&!matchMedia(d).matches||b.push(g);if(c)continue}else{a=v[e++];if(!a)break;l=a.getAttribute("data-precedence");a.removeAttribute("media")}c=r.get(l)||h;c===h&&(h=a);r.set(l,a);c?c.parentNode.insertBefore(a,c.nextSibling):(c=t.head,c.insertBefore(a,c.firstChild))}if(p=document.getElementById(n))p.previousSibling.data=\n"$~";Promise.all(b).then($RC.bind(null,n,w),$RX.bind(null,n,"CSS failed to load"))};$RR("'
    );
    var completeBoundaryWithStylesScript1Partial = stringToPrecomputedChunk('$RR("');
    var completeBoundaryScript2 = stringToPrecomputedChunk('","');
    var completeBoundaryScript3a = stringToPrecomputedChunk('",');
    var completeBoundaryScript3b = stringToPrecomputedChunk('"');
    var completeBoundaryScriptEnd = stringToPrecomputedChunk(")<\/script>");
    stringToPrecomputedChunk('<template data-rci="" data-bid="');
    stringToPrecomputedChunk('<template data-rri="" data-bid="');
    stringToPrecomputedChunk('" data-sid="');
    stringToPrecomputedChunk('" data-sty="');
    var clientRenderScriptFunctionOnly = stringToPrecomputedChunk(
      '$RX=function(b,c,d,e,f){var a=document.getElementById(b);a&&(b=a.previousSibling,b.data="$!",a=a.dataset,c&&(a.dgst=c),d&&(a.msg=d),e&&(a.stck=e),f&&(a.cstck=f),b._reactRetry&&b._reactRetry())};'
    );
    var clientRenderScript1Full = stringToPrecomputedChunk(
      '$RX=function(b,c,d,e,f){var a=document.getElementById(b);a&&(b=a.previousSibling,b.data="$!",a=a.dataset,c&&(a.dgst=c),d&&(a.msg=d),e&&(a.stck=e),f&&(a.cstck=f),b._reactRetry&&b._reactRetry())};;$RX("'
    );
    var clientRenderScript1Partial = stringToPrecomputedChunk('$RX("');
    var clientRenderScript1A = stringToPrecomputedChunk('"');
    var clientRenderErrorScriptArgInterstitial = stringToPrecomputedChunk(",");
    var clientRenderScriptEnd = stringToPrecomputedChunk(")<\/script>");
    stringToPrecomputedChunk('<template data-rxi="" data-bid="');
    stringToPrecomputedChunk('" data-dgst="');
    stringToPrecomputedChunk('" data-msg="');
    stringToPrecomputedChunk('" data-stck="');
    stringToPrecomputedChunk('" data-cstck="');
    var regexForJSStringsInInstructionScripts = /[<\u2028\u2029]/g;
    function escapeJSStringsForInstructionScripts(input) {
      return JSON.stringify(input).replace(
        regexForJSStringsInInstructionScripts,
        function(match) {
          switch (match) {
            case "<":
              return "\\u003c";
            case "\u2028":
              return "\\u2028";
            case "\u2029":
              return "\\u2029";
            default:
              throw Error(
                "escapeJSStringsForInstructionScripts encountered a match it does not know how to replace. this means the match regex and the replacement characters are no longer in sync. This is a bug in React"
              );
          }
        }
      );
    }
    var regexForJSStringsInScripts = /[&><\u2028\u2029]/g;
    function escapeJSObjectForInstructionScripts(input) {
      return JSON.stringify(input).replace(
        regexForJSStringsInScripts,
        function(match) {
          switch (match) {
            case "&":
              return "\\u0026";
            case ">":
              return "\\u003e";
            case "<":
              return "\\u003c";
            case "\u2028":
              return "\\u2028";
            case "\u2029":
              return "\\u2029";
            default:
              throw Error(
                "escapeJSObjectForInstructionScripts encountered a match it does not know how to replace. this means the match regex and the replacement characters are no longer in sync. This is a bug in React"
              );
          }
        }
      );
    }
    var lateStyleTagResourceOpen1 = stringToPrecomputedChunk(
      ' media="not all" data-precedence="'
    );
    var lateStyleTagResourceOpen2 = stringToPrecomputedChunk('" data-href="');
    var lateStyleTagResourceOpen3 = stringToPrecomputedChunk('">');
    var lateStyleTagTemplateClose = stringToPrecomputedChunk("</style>");
    var currentlyRenderingBoundaryHasStylesToHoist = false;
    var destinationHasCapacity = true;
    function flushStyleTagsLateForBoundary(styleQueue) {
      var rules = styleQueue.rules, hrefs = styleQueue.hrefs, i = 0;
      if (hrefs.length) {
        writeChunk(this, currentlyFlushingRenderState.startInlineStyle);
        writeChunk(this, lateStyleTagResourceOpen1);
        writeChunk(this, styleQueue.precedence);
        for (writeChunk(this, lateStyleTagResourceOpen2); i < hrefs.length - 1; i++)
          writeChunk(this, hrefs[i]), writeChunk(this, spaceSeparator);
        writeChunk(this, hrefs[i]);
        writeChunk(this, lateStyleTagResourceOpen3);
        for (i = 0; i < rules.length; i++) writeChunk(this, rules[i]);
        destinationHasCapacity = writeChunkAndReturn(
          this,
          lateStyleTagTemplateClose
        );
        currentlyRenderingBoundaryHasStylesToHoist = true;
        rules.length = 0;
        hrefs.length = 0;
      }
    }
    function hasStylesToHoist(stylesheet) {
      return 2 !== stylesheet.state ? currentlyRenderingBoundaryHasStylesToHoist = true : false;
    }
    function writeHoistablesForBoundary(destination, hoistableState, renderState) {
      currentlyRenderingBoundaryHasStylesToHoist = false;
      destinationHasCapacity = true;
      currentlyFlushingRenderState = renderState;
      hoistableState.styles.forEach(flushStyleTagsLateForBoundary, destination);
      currentlyFlushingRenderState = null;
      hoistableState.stylesheets.forEach(hasStylesToHoist);
      currentlyRenderingBoundaryHasStylesToHoist && (renderState.stylesToHoist = true);
      return destinationHasCapacity;
    }
    function flushResource(resource) {
      for (var i = 0; i < resource.length; i++) writeChunk(this, resource[i]);
      resource.length = 0;
    }
    var stylesheetFlushingQueue = [];
    function flushStyleInPreamble(stylesheet) {
      pushLinkImpl(stylesheetFlushingQueue, stylesheet.props);
      for (var i = 0; i < stylesheetFlushingQueue.length; i++)
        writeChunk(this, stylesheetFlushingQueue[i]);
      stylesheetFlushingQueue.length = 0;
      stylesheet.state = 2;
    }
    var styleTagResourceOpen1 = stringToPrecomputedChunk(' data-precedence="');
    var styleTagResourceOpen2 = stringToPrecomputedChunk('" data-href="');
    var spaceSeparator = stringToPrecomputedChunk(" ");
    var styleTagResourceOpen3 = stringToPrecomputedChunk('">');
    var styleTagResourceClose = stringToPrecomputedChunk("</style>");
    function flushStylesInPreamble(styleQueue) {
      var hasStylesheets = 0 < styleQueue.sheets.size;
      styleQueue.sheets.forEach(flushStyleInPreamble, this);
      styleQueue.sheets.clear();
      var rules = styleQueue.rules, hrefs = styleQueue.hrefs;
      if (!hasStylesheets || hrefs.length) {
        writeChunk(this, currentlyFlushingRenderState.startInlineStyle);
        writeChunk(this, styleTagResourceOpen1);
        writeChunk(this, styleQueue.precedence);
        styleQueue = 0;
        if (hrefs.length) {
          for (writeChunk(this, styleTagResourceOpen2); styleQueue < hrefs.length - 1; styleQueue++)
            writeChunk(this, hrefs[styleQueue]), writeChunk(this, spaceSeparator);
          writeChunk(this, hrefs[styleQueue]);
        }
        writeChunk(this, styleTagResourceOpen3);
        for (styleQueue = 0; styleQueue < rules.length; styleQueue++)
          writeChunk(this, rules[styleQueue]);
        writeChunk(this, styleTagResourceClose);
        rules.length = 0;
        hrefs.length = 0;
      }
    }
    function preloadLateStyle(stylesheet) {
      if (0 === stylesheet.state) {
        stylesheet.state = 1;
        var props = stylesheet.props;
        pushLinkImpl(stylesheetFlushingQueue, {
          rel: "preload",
          as: "style",
          href: stylesheet.props.href,
          crossOrigin: props.crossOrigin,
          fetchPriority: props.fetchPriority,
          integrity: props.integrity,
          media: props.media,
          hrefLang: props.hrefLang,
          referrerPolicy: props.referrerPolicy
        });
        for (stylesheet = 0; stylesheet < stylesheetFlushingQueue.length; stylesheet++)
          writeChunk(this, stylesheetFlushingQueue[stylesheet]);
        stylesheetFlushingQueue.length = 0;
      }
    }
    function preloadLateStyles(styleQueue) {
      styleQueue.sheets.forEach(preloadLateStyle, this);
      styleQueue.sheets.clear();
    }
    stringToPrecomputedChunk('<link rel="expect" href="#');
    stringToPrecomputedChunk('" blocking="render"/>');
    var completedShellIdAttributeStart = stringToPrecomputedChunk(' id="');
    function pushCompletedShellIdAttribute(target, resumableState) {
      0 === (resumableState.instructions & 32) && (resumableState.instructions |= 32, target.push(
        completedShellIdAttributeStart,
        stringToChunk(escapeTextForBrowser("_" + resumableState.idPrefix + "R_")),
        attributeEnd
      ));
    }
    var arrayFirstOpenBracket = stringToPrecomputedChunk("[");
    var arraySubsequentOpenBracket = stringToPrecomputedChunk(",[");
    var arrayInterstitial = stringToPrecomputedChunk(",");
    var arrayCloseBracket = stringToPrecomputedChunk("]");
    function writeStyleResourceDependenciesInJS(destination, hoistableState) {
      writeChunk(destination, arrayFirstOpenBracket);
      var nextArrayOpenBrackChunk = arrayFirstOpenBracket;
      hoistableState.stylesheets.forEach(function(resource) {
        if (2 !== resource.state)
          if (3 === resource.state)
            writeChunk(destination, nextArrayOpenBrackChunk), writeChunk(
              destination,
              stringToChunk(
                escapeJSObjectForInstructionScripts("" + resource.props.href)
              )
            ), writeChunk(destination, arrayCloseBracket), nextArrayOpenBrackChunk = arraySubsequentOpenBracket;
          else {
            writeChunk(destination, nextArrayOpenBrackChunk);
            var precedence = resource.props["data-precedence"], props = resource.props, coercedHref = sanitizeURL("" + resource.props.href);
            writeChunk(
              destination,
              stringToChunk(escapeJSObjectForInstructionScripts(coercedHref))
            );
            precedence = "" + precedence;
            writeChunk(destination, arrayInterstitial);
            writeChunk(
              destination,
              stringToChunk(escapeJSObjectForInstructionScripts(precedence))
            );
            for (var propKey in props)
              if (hasOwnProperty.call(props, propKey) && (precedence = props[propKey], null != precedence))
                switch (propKey) {
                  case "href":
                  case "rel":
                  case "precedence":
                  case "data-precedence":
                    break;
                  case "children":
                  case "dangerouslySetInnerHTML":
                    throw Error(formatProdErrorMessage(399, "link"));
                  default:
                    writeStyleResourceAttributeInJS(
                      destination,
                      propKey,
                      precedence
                    );
                }
            writeChunk(destination, arrayCloseBracket);
            nextArrayOpenBrackChunk = arraySubsequentOpenBracket;
            resource.state = 3;
          }
      });
      writeChunk(destination, arrayCloseBracket);
    }
    function writeStyleResourceAttributeInJS(destination, name, value) {
      var attributeName = name.toLowerCase();
      switch (typeof value) {
        case "function":
        case "symbol":
          return;
      }
      switch (name) {
        case "innerHTML":
        case "dangerouslySetInnerHTML":
        case "suppressContentEditableWarning":
        case "suppressHydrationWarning":
        case "style":
        case "ref":
          return;
        case "className":
          attributeName = "class";
          name = "" + value;
          break;
        case "hidden":
          if (false === value) return;
          name = "";
          break;
        case "src":
        case "href":
          value = sanitizeURL(value);
          name = "" + value;
          break;
        default:
          if (2 < name.length && ("o" === name[0] || "O" === name[0]) && ("n" === name[1] || "N" === name[1]) || !isAttributeNameSafe(name))
            return;
          name = "" + value;
      }
      writeChunk(destination, arrayInterstitial);
      writeChunk(
        destination,
        stringToChunk(escapeJSObjectForInstructionScripts(attributeName))
      );
      writeChunk(destination, arrayInterstitial);
      writeChunk(
        destination,
        stringToChunk(escapeJSObjectForInstructionScripts(name))
      );
    }
    function createHoistableState() {
      return { styles: /* @__PURE__ */ new Set(), stylesheets: /* @__PURE__ */ new Set(), suspenseyImages: false };
    }
    function prefetchDNS(href) {
      var request = currentRequest ? currentRequest : null;
      if (request) {
        var resumableState = request.resumableState, renderState = request.renderState;
        if ("string" === typeof href && href) {
          if (!resumableState.dnsResources.hasOwnProperty(href)) {
            resumableState.dnsResources[href] = null;
            resumableState = renderState.headers;
            var header, JSCompiler_temp;
            if (JSCompiler_temp = resumableState && 0 < resumableState.remainingCapacity)
              JSCompiler_temp = (header = "<" + ("" + href).replace(
                regexForHrefInLinkHeaderURLContext,
                escapeHrefForLinkHeaderURLContextReplacer
              ) + ">; rel=dns-prefetch", 0 <= (resumableState.remainingCapacity -= header.length + 2));
            JSCompiler_temp ? (renderState.resets.dns[href] = null, resumableState.preconnects && (resumableState.preconnects += ", "), resumableState.preconnects += header) : (header = [], pushLinkImpl(header, { href, rel: "dns-prefetch" }), renderState.preconnects.add(header));
          }
          enqueueFlush(request);
        }
      } else previousDispatcher.D(href);
    }
    function preconnect(href, crossOrigin) {
      var request = currentRequest ? currentRequest : null;
      if (request) {
        var resumableState = request.resumableState, renderState = request.renderState;
        if ("string" === typeof href && href) {
          var bucket = "use-credentials" === crossOrigin ? "credentials" : "string" === typeof crossOrigin ? "anonymous" : "default";
          if (!resumableState.connectResources[bucket].hasOwnProperty(href)) {
            resumableState.connectResources[bucket][href] = null;
            resumableState = renderState.headers;
            var header, JSCompiler_temp;
            if (JSCompiler_temp = resumableState && 0 < resumableState.remainingCapacity) {
              JSCompiler_temp = "<" + ("" + href).replace(
                regexForHrefInLinkHeaderURLContext,
                escapeHrefForLinkHeaderURLContextReplacer
              ) + ">; rel=preconnect";
              if ("string" === typeof crossOrigin) {
                var escapedCrossOrigin = ("" + crossOrigin).replace(
                  regexForLinkHeaderQuotedParamValueContext,
                  escapeStringForLinkHeaderQuotedParamValueContextReplacer
                );
                JSCompiler_temp += '; crossorigin="' + escapedCrossOrigin + '"';
              }
              JSCompiler_temp = (header = JSCompiler_temp, 0 <= (resumableState.remainingCapacity -= header.length + 2));
            }
            JSCompiler_temp ? (renderState.resets.connect[bucket][href] = null, resumableState.preconnects && (resumableState.preconnects += ", "), resumableState.preconnects += header) : (bucket = [], pushLinkImpl(bucket, {
              rel: "preconnect",
              href,
              crossOrigin
            }), renderState.preconnects.add(bucket));
          }
          enqueueFlush(request);
        }
      } else previousDispatcher.C(href, crossOrigin);
    }
    function preload(href, as, options) {
      var request = currentRequest ? currentRequest : null;
      if (request) {
        var resumableState = request.resumableState, renderState = request.renderState;
        if (as && href) {
          switch (as) {
            case "image":
              if (options) {
                var imageSrcSet = options.imageSrcSet;
                var imageSizes = options.imageSizes;
                var fetchPriority = options.fetchPriority;
              }
              var key = imageSrcSet ? imageSrcSet + "\n" + (imageSizes || "") : href;
              if (resumableState.imageResources.hasOwnProperty(key)) return;
              resumableState.imageResources[key] = PRELOAD_NO_CREDS;
              resumableState = renderState.headers;
              var header;
              resumableState && 0 < resumableState.remainingCapacity && "string" !== typeof imageSrcSet && "high" === fetchPriority && (header = getPreloadAsHeader(href, as, options), 0 <= (resumableState.remainingCapacity -= header.length + 2)) ? (renderState.resets.image[key] = PRELOAD_NO_CREDS, resumableState.highImagePreloads && (resumableState.highImagePreloads += ", "), resumableState.highImagePreloads += header) : (resumableState = [], pushLinkImpl(
                resumableState,
                assign(
                  { rel: "preload", href: imageSrcSet ? void 0 : href, as },
                  options
                )
              ), "high" === fetchPriority ? renderState.highImagePreloads.add(resumableState) : (renderState.bulkPreloads.add(resumableState), renderState.preloads.images.set(key, resumableState)));
              break;
            case "style":
              if (resumableState.styleResources.hasOwnProperty(href)) return;
              imageSrcSet = [];
              pushLinkImpl(
                imageSrcSet,
                assign({ rel: "preload", href, as }, options)
              );
              resumableState.styleResources[href] = !options || "string" !== typeof options.crossOrigin && "string" !== typeof options.integrity ? PRELOAD_NO_CREDS : [options.crossOrigin, options.integrity];
              renderState.preloads.stylesheets.set(href, imageSrcSet);
              renderState.bulkPreloads.add(imageSrcSet);
              break;
            case "script":
              if (resumableState.scriptResources.hasOwnProperty(href)) return;
              imageSrcSet = [];
              renderState.preloads.scripts.set(href, imageSrcSet);
              renderState.bulkPreloads.add(imageSrcSet);
              pushLinkImpl(
                imageSrcSet,
                assign({ rel: "preload", href, as }, options)
              );
              resumableState.scriptResources[href] = !options || "string" !== typeof options.crossOrigin && "string" !== typeof options.integrity ? PRELOAD_NO_CREDS : [options.crossOrigin, options.integrity];
              break;
            default:
              if (resumableState.unknownResources.hasOwnProperty(as)) {
                if (imageSrcSet = resumableState.unknownResources[as], imageSrcSet.hasOwnProperty(href))
                  return;
              } else
                imageSrcSet = {}, resumableState.unknownResources[as] = imageSrcSet;
              imageSrcSet[href] = PRELOAD_NO_CREDS;
              if ((resumableState = renderState.headers) && 0 < resumableState.remainingCapacity && "font" === as && (key = getPreloadAsHeader(href, as, options), 0 <= (resumableState.remainingCapacity -= key.length + 2)))
                renderState.resets.font[href] = PRELOAD_NO_CREDS, resumableState.fontPreloads && (resumableState.fontPreloads += ", "), resumableState.fontPreloads += key;
              else
                switch (resumableState = [], href = assign({ rel: "preload", href, as }, options), pushLinkImpl(resumableState, href), as) {
                  case "font":
                    renderState.fontPreloads.add(resumableState);
                    break;
                  default:
                    renderState.bulkPreloads.add(resumableState);
                }
          }
          enqueueFlush(request);
        }
      } else previousDispatcher.L(href, as, options);
    }
    function preloadModule(href, options) {
      var request = currentRequest ? currentRequest : null;
      if (request) {
        var resumableState = request.resumableState, renderState = request.renderState;
        if (href) {
          var as = options && "string" === typeof options.as ? options.as : "script";
          switch (as) {
            case "script":
              if (resumableState.moduleScriptResources.hasOwnProperty(href)) return;
              as = [];
              resumableState.moduleScriptResources[href] = !options || "string" !== typeof options.crossOrigin && "string" !== typeof options.integrity ? PRELOAD_NO_CREDS : [options.crossOrigin, options.integrity];
              renderState.preloads.moduleScripts.set(href, as);
              break;
            default:
              if (resumableState.moduleUnknownResources.hasOwnProperty(as)) {
                var resources = resumableState.unknownResources[as];
                if (resources.hasOwnProperty(href)) return;
              } else
                resources = {}, resumableState.moduleUnknownResources[as] = resources;
              as = [];
              resources[href] = PRELOAD_NO_CREDS;
          }
          pushLinkImpl(as, assign({ rel: "modulepreload", href }, options));
          renderState.bulkPreloads.add(as);
          enqueueFlush(request);
        }
      } else previousDispatcher.m(href, options);
    }
    function preinitStyle(href, precedence, options) {
      var request = currentRequest ? currentRequest : null;
      if (request) {
        var resumableState = request.resumableState, renderState = request.renderState;
        if (href) {
          precedence = precedence || "default";
          var styleQueue = renderState.styles.get(precedence), resourceState = resumableState.styleResources.hasOwnProperty(href) ? resumableState.styleResources[href] : void 0;
          null !== resourceState && (resumableState.styleResources[href] = null, styleQueue || (styleQueue = {
            precedence: stringToChunk(escapeTextForBrowser(precedence)),
            rules: [],
            hrefs: [],
            sheets: /* @__PURE__ */ new Map()
          }, renderState.styles.set(precedence, styleQueue)), precedence = {
            state: 0,
            props: assign(
              { rel: "stylesheet", href, "data-precedence": precedence },
              options
            )
          }, resourceState && (2 === resourceState.length && adoptPreloadCredentials(precedence.props, resourceState), (renderState = renderState.preloads.stylesheets.get(href)) && 0 < renderState.length ? renderState.length = 0 : precedence.state = 1), styleQueue.sheets.set(href, precedence), enqueueFlush(request));
        }
      } else previousDispatcher.S(href, precedence, options);
    }
    function preinitScript(src, options) {
      var request = currentRequest ? currentRequest : null;
      if (request) {
        var resumableState = request.resumableState, renderState = request.renderState;
        if (src) {
          var resourceState = resumableState.scriptResources.hasOwnProperty(src) ? resumableState.scriptResources[src] : void 0;
          null !== resourceState && (resumableState.scriptResources[src] = null, options = assign({ src, async: true }, options), resourceState && (2 === resourceState.length && adoptPreloadCredentials(options, resourceState), src = renderState.preloads.scripts.get(src)) && (src.length = 0), src = [], renderState.scripts.add(src), pushScriptImpl(src, options), enqueueFlush(request));
        }
      } else previousDispatcher.X(src, options);
    }
    function preinitModuleScript(src, options) {
      var request = currentRequest ? currentRequest : null;
      if (request) {
        var resumableState = request.resumableState, renderState = request.renderState;
        if (src) {
          var resourceState = resumableState.moduleScriptResources.hasOwnProperty(
            src
          ) ? resumableState.moduleScriptResources[src] : void 0;
          null !== resourceState && (resumableState.moduleScriptResources[src] = null, options = assign({ src, type: "module", async: true }, options), resourceState && (2 === resourceState.length && adoptPreloadCredentials(options, resourceState), src = renderState.preloads.moduleScripts.get(src)) && (src.length = 0), src = [], renderState.scripts.add(src), pushScriptImpl(src, options), enqueueFlush(request));
        }
      } else previousDispatcher.M(src, options);
    }
    function adoptPreloadCredentials(target, preloadState) {
      null == target.crossOrigin && (target.crossOrigin = preloadState[0]);
      null == target.integrity && (target.integrity = preloadState[1]);
    }
    function getPreloadAsHeader(href, as, params) {
      href = ("" + href).replace(
        regexForHrefInLinkHeaderURLContext,
        escapeHrefForLinkHeaderURLContextReplacer
      );
      as = ("" + as).replace(
        regexForLinkHeaderQuotedParamValueContext,
        escapeStringForLinkHeaderQuotedParamValueContextReplacer
      );
      as = "<" + href + '>; rel=preload; as="' + as + '"';
      for (var paramName in params)
        hasOwnProperty.call(params, paramName) && (href = params[paramName], "string" === typeof href && (as += "; " + paramName.toLowerCase() + '="' + ("" + href).replace(
          regexForLinkHeaderQuotedParamValueContext,
          escapeStringForLinkHeaderQuotedParamValueContextReplacer
        ) + '"'));
      return as;
    }
    var regexForHrefInLinkHeaderURLContext = /[<>\r\n]/g;
    function escapeHrefForLinkHeaderURLContextReplacer(match) {
      switch (match) {
        case "<":
          return "%3C";
        case ">":
          return "%3E";
        case "\n":
          return "%0A";
        case "\r":
          return "%0D";
        default:
          throw Error(
            "escapeLinkHrefForHeaderContextReplacer encountered a match it does not know how to replace. this means the match regex and the replacement characters are no longer in sync. This is a bug in React"
          );
      }
    }
    var regexForLinkHeaderQuotedParamValueContext = /["';,\r\n]/g;
    function escapeStringForLinkHeaderQuotedParamValueContextReplacer(match) {
      switch (match) {
        case '"':
          return "%22";
        case "'":
          return "%27";
        case ";":
          return "%3B";
        case ",":
          return "%2C";
        case "\n":
          return "%0A";
        case "\r":
          return "%0D";
        default:
          throw Error(
            "escapeStringForLinkHeaderQuotedParamValueContextReplacer encountered a match it does not know how to replace. this means the match regex and the replacement characters are no longer in sync. This is a bug in React"
          );
      }
    }
    function hoistStyleQueueDependency(styleQueue) {
      this.styles.add(styleQueue);
    }
    function hoistStylesheetDependency(stylesheet) {
      this.stylesheets.add(stylesheet);
    }
    function hoistHoistables(parentState, childState) {
      childState.styles.forEach(hoistStyleQueueDependency, parentState);
      childState.stylesheets.forEach(hoistStylesheetDependency, parentState);
      childState.suspenseyImages && (parentState.suspenseyImages = true);
    }
    function hasSuspenseyContent(hoistableState) {
      return 0 < hoistableState.stylesheets.size || hoistableState.suspenseyImages;
    }
    var bind = Function.prototype.bind;
    var REACT_CLIENT_REFERENCE = Symbol.for("react.client.reference");
    function getComponentNameFromType(type) {
      if (null == type) return null;
      if ("function" === typeof type)
        return type.$$typeof === REACT_CLIENT_REFERENCE ? null : type.displayName || type.name || null;
      if ("string" === typeof type) return type;
      switch (type) {
        case REACT_FRAGMENT_TYPE:
          return "Fragment";
        case REACT_PROFILER_TYPE:
          return "Profiler";
        case REACT_STRICT_MODE_TYPE:
          return "StrictMode";
        case REACT_SUSPENSE_TYPE:
          return "Suspense";
        case REACT_SUSPENSE_LIST_TYPE:
          return "SuspenseList";
        case REACT_ACTIVITY_TYPE:
          return "Activity";
      }
      if ("object" === typeof type)
        switch (type.$$typeof) {
          case REACT_PORTAL_TYPE:
            return "Portal";
          case REACT_CONTEXT_TYPE:
            return type.displayName || "Context";
          case REACT_CONSUMER_TYPE:
            return (type._context.displayName || "Context") + ".Consumer";
          case REACT_FORWARD_REF_TYPE:
            var innerType = type.render;
            type = type.displayName;
            type || (type = innerType.displayName || innerType.name || "", type = "" !== type ? "ForwardRef(" + type + ")" : "ForwardRef");
            return type;
          case REACT_MEMO_TYPE:
            return innerType = type.displayName || null, null !== innerType ? innerType : getComponentNameFromType(type.type) || "Memo";
          case REACT_LAZY_TYPE:
            innerType = type._payload;
            type = type._init;
            try {
              return getComponentNameFromType(type(innerType));
            } catch (x) {
            }
        }
      return null;
    }
    var emptyContextObject = {};
    var currentActiveSnapshot = null;
    function popToNearestCommonAncestor(prev, next) {
      if (prev !== next) {
        prev.context._currentValue = prev.parentValue;
        prev = prev.parent;
        var parentNext = next.parent;
        if (null === prev) {
          if (null !== parentNext) throw Error(formatProdErrorMessage(401));
        } else {
          if (null === parentNext) throw Error(formatProdErrorMessage(401));
          popToNearestCommonAncestor(prev, parentNext);
        }
        next.context._currentValue = next.value;
      }
    }
    function popAllPrevious(prev) {
      prev.context._currentValue = prev.parentValue;
      prev = prev.parent;
      null !== prev && popAllPrevious(prev);
    }
    function pushAllNext(next) {
      var parentNext = next.parent;
      null !== parentNext && pushAllNext(parentNext);
      next.context._currentValue = next.value;
    }
    function popPreviousToCommonLevel(prev, next) {
      prev.context._currentValue = prev.parentValue;
      prev = prev.parent;
      if (null === prev) throw Error(formatProdErrorMessage(402));
      prev.depth === next.depth ? popToNearestCommonAncestor(prev, next) : popPreviousToCommonLevel(prev, next);
    }
    function popNextToCommonLevel(prev, next) {
      var parentNext = next.parent;
      if (null === parentNext) throw Error(formatProdErrorMessage(402));
      prev.depth === parentNext.depth ? popToNearestCommonAncestor(prev, parentNext) : popNextToCommonLevel(prev, parentNext);
      next.context._currentValue = next.value;
    }
    function switchContext(newSnapshot) {
      var prev = currentActiveSnapshot;
      prev !== newSnapshot && (null === prev ? pushAllNext(newSnapshot) : null === newSnapshot ? popAllPrevious(prev) : prev.depth === newSnapshot.depth ? popToNearestCommonAncestor(prev, newSnapshot) : prev.depth > newSnapshot.depth ? popPreviousToCommonLevel(prev, newSnapshot) : popNextToCommonLevel(prev, newSnapshot), currentActiveSnapshot = newSnapshot);
    }
    var classComponentUpdater = {
      enqueueSetState: function(inst, payload) {
        inst = inst._reactInternals;
        null !== inst.queue && inst.queue.push(payload);
      },
      enqueueReplaceState: function(inst, payload) {
        inst = inst._reactInternals;
        inst.replace = true;
        inst.queue = [payload];
      },
      enqueueForceUpdate: function() {
      }
    };
    var emptyTreeContext = { id: 1, overflow: "" };
    function pushTreeContext(baseContext, totalChildren, index) {
      var baseIdWithLeadingBit = baseContext.id;
      baseContext = baseContext.overflow;
      var baseLength = 32 - clz32(baseIdWithLeadingBit) - 1;
      baseIdWithLeadingBit &= ~(1 << baseLength);
      index += 1;
      var length = 32 - clz32(totalChildren) + baseLength;
      if (30 < length) {
        var numberOfOverflowBits = baseLength - baseLength % 5;
        length = (baseIdWithLeadingBit & (1 << numberOfOverflowBits) - 1).toString(32);
        baseIdWithLeadingBit >>= numberOfOverflowBits;
        baseLength -= numberOfOverflowBits;
        return {
          id: 1 << 32 - clz32(totalChildren) + baseLength | index << baseLength | baseIdWithLeadingBit,
          overflow: length + baseContext
        };
      }
      return {
        id: 1 << length | index << baseLength | baseIdWithLeadingBit,
        overflow: baseContext
      };
    }
    var clz32 = Math.clz32 ? Math.clz32 : clz32Fallback;
    var log = Math.log;
    var LN2 = Math.LN2;
    function clz32Fallback(x) {
      x >>>= 0;
      return 0 === x ? 32 : 31 - (log(x) / LN2 | 0) | 0;
    }
    function noop() {
    }
    var SuspenseException = Error(formatProdErrorMessage(460));
    function trackUsedThenable(thenableState2, thenable, index) {
      index = thenableState2[index];
      void 0 === index ? thenableState2.push(thenable) : index !== thenable && (thenable.then(noop, noop), thenable = index);
      switch (thenable.status) {
        case "fulfilled":
          return thenable.value;
        case "rejected":
          throw thenable.reason;
        default:
          "string" === typeof thenable.status ? thenable.then(noop, noop) : (thenableState2 = thenable, thenableState2.status = "pending", thenableState2.then(
            function(fulfilledValue) {
              if ("pending" === thenable.status) {
                var fulfilledThenable = thenable;
                fulfilledThenable.status = "fulfilled";
                fulfilledThenable.value = fulfilledValue;
              }
            },
            function(error) {
              if ("pending" === thenable.status) {
                var rejectedThenable = thenable;
                rejectedThenable.status = "rejected";
                rejectedThenable.reason = error;
              }
            }
          ));
          switch (thenable.status) {
            case "fulfilled":
              return thenable.value;
            case "rejected":
              throw thenable.reason;
          }
          suspendedThenable = thenable;
          throw SuspenseException;
      }
    }
    var suspendedThenable = null;
    function getSuspendedThenable() {
      if (null === suspendedThenable) throw Error(formatProdErrorMessage(459));
      var thenable = suspendedThenable;
      suspendedThenable = null;
      return thenable;
    }
    function is(x, y) {
      return x === y && (0 !== x || 1 / x === 1 / y) || x !== x && y !== y;
    }
    var objectIs = "function" === typeof Object.is ? Object.is : is;
    var currentlyRenderingComponent = null;
    var currentlyRenderingTask = null;
    var currentlyRenderingRequest = null;
    var currentlyRenderingKeyPath = null;
    var firstWorkInProgressHook = null;
    var workInProgressHook = null;
    var isReRender = false;
    var didScheduleRenderPhaseUpdate = false;
    var localIdCounter = 0;
    var actionStateCounter = 0;
    var actionStateMatchingIndex = -1;
    var thenableIndexCounter = 0;
    var thenableState = null;
    var renderPhaseUpdates = null;
    var numberOfReRenders = 0;
    function resolveCurrentlyRenderingComponent() {
      if (null === currentlyRenderingComponent)
        throw Error(formatProdErrorMessage(321));
      return currentlyRenderingComponent;
    }
    function createHook() {
      if (0 < numberOfReRenders) throw Error(formatProdErrorMessage(312));
      return { memoizedState: null, queue: null, next: null };
    }
    function createWorkInProgressHook() {
      null === workInProgressHook ? null === firstWorkInProgressHook ? (isReRender = false, firstWorkInProgressHook = workInProgressHook = createHook()) : (isReRender = true, workInProgressHook = firstWorkInProgressHook) : null === workInProgressHook.next ? (isReRender = false, workInProgressHook = workInProgressHook.next = createHook()) : (isReRender = true, workInProgressHook = workInProgressHook.next);
      return workInProgressHook;
    }
    function getThenableStateAfterSuspending() {
      var state = thenableState;
      thenableState = null;
      return state;
    }
    function resetHooksState() {
      currentlyRenderingKeyPath = currentlyRenderingRequest = currentlyRenderingTask = currentlyRenderingComponent = null;
      didScheduleRenderPhaseUpdate = false;
      firstWorkInProgressHook = null;
      numberOfReRenders = 0;
      workInProgressHook = renderPhaseUpdates = null;
    }
    function basicStateReducer(state, action) {
      return "function" === typeof action ? action(state) : action;
    }
    function useReducer(reducer, initialArg, init) {
      currentlyRenderingComponent = resolveCurrentlyRenderingComponent();
      workInProgressHook = createWorkInProgressHook();
      if (isReRender) {
        var queue = workInProgressHook.queue;
        initialArg = queue.dispatch;
        if (null !== renderPhaseUpdates && (init = renderPhaseUpdates.get(queue), void 0 !== init)) {
          renderPhaseUpdates.delete(queue);
          queue = workInProgressHook.memoizedState;
          do
            queue = reducer(queue, init.action), init = init.next;
          while (null !== init);
          workInProgressHook.memoizedState = queue;
          return [queue, initialArg];
        }
        return [workInProgressHook.memoizedState, initialArg];
      }
      reducer = reducer === basicStateReducer ? "function" === typeof initialArg ? initialArg() : initialArg : void 0 !== init ? init(initialArg) : initialArg;
      workInProgressHook.memoizedState = reducer;
      reducer = workInProgressHook.queue = { last: null, dispatch: null };
      reducer = reducer.dispatch = dispatchAction.bind(
        null,
        currentlyRenderingComponent,
        reducer
      );
      return [workInProgressHook.memoizedState, reducer];
    }
    function useMemo(nextCreate, deps) {
      currentlyRenderingComponent = resolveCurrentlyRenderingComponent();
      workInProgressHook = createWorkInProgressHook();
      deps = void 0 === deps ? null : deps;
      if (null !== workInProgressHook) {
        var prevState = workInProgressHook.memoizedState;
        if (null !== prevState && null !== deps) {
          var prevDeps = prevState[1];
          a: if (null === prevDeps) prevDeps = false;
          else {
            for (var i = 0; i < prevDeps.length && i < deps.length; i++)
              if (!objectIs(deps[i], prevDeps[i])) {
                prevDeps = false;
                break a;
              }
            prevDeps = true;
          }
          if (prevDeps) return prevState[0];
        }
      }
      nextCreate = nextCreate();
      workInProgressHook.memoizedState = [nextCreate, deps];
      return nextCreate;
    }
    function dispatchAction(componentIdentity, queue, action) {
      if (25 <= numberOfReRenders) throw Error(formatProdErrorMessage(301));
      if (componentIdentity === currentlyRenderingComponent)
        if (didScheduleRenderPhaseUpdate = true, componentIdentity = { action, next: null }, null === renderPhaseUpdates && (renderPhaseUpdates = /* @__PURE__ */ new Map()), action = renderPhaseUpdates.get(queue), void 0 === action)
          renderPhaseUpdates.set(queue, componentIdentity);
        else {
          for (queue = action; null !== queue.next; ) queue = queue.next;
          queue.next = componentIdentity;
        }
    }
    function throwOnUseEffectEventCall() {
      throw Error(formatProdErrorMessage(440));
    }
    function unsupportedStartTransition() {
      throw Error(formatProdErrorMessage(394));
    }
    function unsupportedSetOptimisticState() {
      throw Error(formatProdErrorMessage(479));
    }
    function useActionState(action, initialState, permalink) {
      resolveCurrentlyRenderingComponent();
      var actionStateHookIndex = actionStateCounter++, request = currentlyRenderingRequest;
      if ("function" === typeof action.$$FORM_ACTION) {
        var nextPostbackStateKey = null, componentKeyPath = currentlyRenderingKeyPath;
        request = request.formState;
        var isSignatureEqual = action.$$IS_SIGNATURE_EQUAL;
        if (null !== request && "function" === typeof isSignatureEqual) {
          var postbackKey = request[1];
          isSignatureEqual.call(action, request[2], request[3]) && (nextPostbackStateKey = void 0 !== permalink ? "p" + permalink : "k" + murmurhash3_32_gc(
            JSON.stringify([componentKeyPath, null, actionStateHookIndex]),
            0
          ), postbackKey === nextPostbackStateKey && (actionStateMatchingIndex = actionStateHookIndex, initialState = request[0]));
        }
        var boundAction = action.bind(null, initialState);
        action = function(payload) {
          boundAction(payload);
        };
        "function" === typeof boundAction.$$FORM_ACTION && (action.$$FORM_ACTION = function(prefix2) {
          prefix2 = boundAction.$$FORM_ACTION(prefix2);
          void 0 !== permalink && (permalink += "", prefix2.action = permalink);
          var formData = prefix2.data;
          formData && (null === nextPostbackStateKey && (nextPostbackStateKey = void 0 !== permalink ? "p" + permalink : "k" + murmurhash3_32_gc(
            JSON.stringify([
              componentKeyPath,
              null,
              actionStateHookIndex
            ]),
            0
          )), formData.append("$ACTION_KEY", nextPostbackStateKey));
          return prefix2;
        });
        return [initialState, action, false];
      }
      var boundAction$22 = action.bind(null, initialState);
      return [
        initialState,
        function(payload) {
          boundAction$22(payload);
        },
        false
      ];
    }
    function unwrapThenable(thenable) {
      var index = thenableIndexCounter;
      thenableIndexCounter += 1;
      null === thenableState && (thenableState = []);
      return trackUsedThenable(thenableState, thenable, index);
    }
    function unsupportedRefresh() {
      throw Error(formatProdErrorMessage(393));
    }
    var HooksDispatcher = {
      readContext: function(context) {
        return context._currentValue;
      },
      use: function(usable) {
        if (null !== usable && "object" === typeof usable) {
          if ("function" === typeof usable.then) return unwrapThenable(usable);
          if (usable.$$typeof === REACT_CONTEXT_TYPE) return usable._currentValue;
        }
        throw Error(formatProdErrorMessage(438, String(usable)));
      },
      useContext: function(context) {
        resolveCurrentlyRenderingComponent();
        return context._currentValue;
      },
      useMemo,
      useReducer,
      useRef: function(initialValue) {
        currentlyRenderingComponent = resolveCurrentlyRenderingComponent();
        workInProgressHook = createWorkInProgressHook();
        var previousRef = workInProgressHook.memoizedState;
        return null === previousRef ? (initialValue = { current: initialValue }, workInProgressHook.memoizedState = initialValue) : previousRef;
      },
      useState: function(initialState) {
        return useReducer(basicStateReducer, initialState);
      },
      useInsertionEffect: noop,
      useLayoutEffect: noop,
      useCallback: function(callback, deps) {
        return useMemo(function() {
          return callback;
        }, deps);
      },
      useImperativeHandle: noop,
      useEffect: noop,
      useDebugValue: noop,
      useDeferredValue: function(value, initialValue) {
        resolveCurrentlyRenderingComponent();
        return void 0 !== initialValue ? initialValue : value;
      },
      useTransition: function() {
        resolveCurrentlyRenderingComponent();
        return [false, unsupportedStartTransition];
      },
      useId: function() {
        var JSCompiler_inline_result = currentlyRenderingTask.treeContext;
        var overflow = JSCompiler_inline_result.overflow;
        JSCompiler_inline_result = JSCompiler_inline_result.id;
        JSCompiler_inline_result = (JSCompiler_inline_result & ~(1 << 32 - clz32(JSCompiler_inline_result) - 1)).toString(32) + overflow;
        var resumableState = currentResumableState;
        if (null === resumableState) throw Error(formatProdErrorMessage(404));
        overflow = localIdCounter++;
        JSCompiler_inline_result = "_" + resumableState.idPrefix + "R_" + JSCompiler_inline_result;
        0 < overflow && (JSCompiler_inline_result += "H" + overflow.toString(32));
        return JSCompiler_inline_result + "_";
      },
      useSyncExternalStore: function(subscribe, getSnapshot, getServerSnapshot) {
        if (void 0 === getServerSnapshot)
          throw Error(formatProdErrorMessage(407));
        return getServerSnapshot();
      },
      useOptimistic: function(passthrough) {
        resolveCurrentlyRenderingComponent();
        return [passthrough, unsupportedSetOptimisticState];
      },
      useActionState,
      useFormState: useActionState,
      useHostTransitionStatus: function() {
        resolveCurrentlyRenderingComponent();
        return sharedNotPendingObject;
      },
      useMemoCache: function(size) {
        for (var data = Array(size), i = 0; i < size; i++)
          data[i] = REACT_MEMO_CACHE_SENTINEL;
        return data;
      },
      useCacheRefresh: function() {
        return unsupportedRefresh;
      },
      useEffectEvent: function() {
        return throwOnUseEffectEventCall;
      }
    };
    var currentResumableState = null;
    var DefaultAsyncDispatcher = {
      getCacheForType: function() {
        throw Error(formatProdErrorMessage(248));
      },
      cacheSignal: function() {
        throw Error(formatProdErrorMessage(248));
      }
    };
    var prefix;
    var suffix;
    function describeBuiltInComponentFrame(name) {
      if (void 0 === prefix)
        try {
          throw Error();
        } catch (x) {
          var match = x.stack.trim().match(/\n( *(at )?)/);
          prefix = match && match[1] || "";
          suffix = -1 < x.stack.indexOf("\n    at") ? " (<anonymous>)" : -1 < x.stack.indexOf("@") ? "@unknown:0:0" : "";
        }
      return "\n" + prefix + name + suffix;
    }
    var reentry = false;
    function describeNativeComponentFrame(fn, construct) {
      if (!fn || reentry) return "";
      reentry = true;
      var previousPrepareStackTrace = Error.prepareStackTrace;
      Error.prepareStackTrace = void 0;
      try {
        var RunInRootFrame = {
          DetermineComponentFrameRoot: function() {
            try {
              if (construct) {
                var Fake = function() {
                  throw Error();
                };
                Object.defineProperty(Fake.prototype, "props", {
                  set: function() {
                    throw Error();
                  }
                });
                if ("object" === typeof Reflect && Reflect.construct) {
                  try {
                    Reflect.construct(Fake, []);
                  } catch (x) {
                    var control = x;
                  }
                  Reflect.construct(fn, [], Fake);
                } else {
                  try {
                    Fake.call();
                  } catch (x$24) {
                    control = x$24;
                  }
                  fn.call(Fake.prototype);
                }
              } else {
                try {
                  throw Error();
                } catch (x$25) {
                  control = x$25;
                }
                (Fake = fn()) && "function" === typeof Fake.catch && Fake.catch(function() {
                });
              }
            } catch (sample) {
              if (sample && control && "string" === typeof sample.stack)
                return [sample.stack, control.stack];
            }
            return [null, null];
          }
        };
        RunInRootFrame.DetermineComponentFrameRoot.displayName = "DetermineComponentFrameRoot";
        var namePropDescriptor = Object.getOwnPropertyDescriptor(
          RunInRootFrame.DetermineComponentFrameRoot,
          "name"
        );
        namePropDescriptor && namePropDescriptor.configurable && Object.defineProperty(
          RunInRootFrame.DetermineComponentFrameRoot,
          "name",
          { value: "DetermineComponentFrameRoot" }
        );
        var _RunInRootFrame$Deter = RunInRootFrame.DetermineComponentFrameRoot(), sampleStack = _RunInRootFrame$Deter[0], controlStack = _RunInRootFrame$Deter[1];
        if (sampleStack && controlStack) {
          var sampleLines = sampleStack.split("\n"), controlLines = controlStack.split("\n");
          for (namePropDescriptor = RunInRootFrame = 0; RunInRootFrame < sampleLines.length && !sampleLines[RunInRootFrame].includes("DetermineComponentFrameRoot"); )
            RunInRootFrame++;
          for (; namePropDescriptor < controlLines.length && !controlLines[namePropDescriptor].includes(
            "DetermineComponentFrameRoot"
          ); )
            namePropDescriptor++;
          if (RunInRootFrame === sampleLines.length || namePropDescriptor === controlLines.length)
            for (RunInRootFrame = sampleLines.length - 1, namePropDescriptor = controlLines.length - 1; 1 <= RunInRootFrame && 0 <= namePropDescriptor && sampleLines[RunInRootFrame] !== controlLines[namePropDescriptor]; )
              namePropDescriptor--;
          for (; 1 <= RunInRootFrame && 0 <= namePropDescriptor; RunInRootFrame--, namePropDescriptor--)
            if (sampleLines[RunInRootFrame] !== controlLines[namePropDescriptor]) {
              if (1 !== RunInRootFrame || 1 !== namePropDescriptor) {
                do
                  if (RunInRootFrame--, namePropDescriptor--, 0 > namePropDescriptor || sampleLines[RunInRootFrame] !== controlLines[namePropDescriptor]) {
                    var frame = "\n" + sampleLines[RunInRootFrame].replace(" at new ", " at ");
                    fn.displayName && frame.includes("<anonymous>") && (frame = frame.replace("<anonymous>", fn.displayName));
                    return frame;
                  }
                while (1 <= RunInRootFrame && 0 <= namePropDescriptor);
              }
              break;
            }
        }
      } finally {
        reentry = false, Error.prepareStackTrace = previousPrepareStackTrace;
      }
      return (previousPrepareStackTrace = fn ? fn.displayName || fn.name : "") ? describeBuiltInComponentFrame(previousPrepareStackTrace) : "";
    }
    function describeComponentStackByType(type) {
      if ("string" === typeof type) return describeBuiltInComponentFrame(type);
      if ("function" === typeof type)
        return type.prototype && type.prototype.isReactComponent ? describeNativeComponentFrame(type, true) : describeNativeComponentFrame(type, false);
      if ("object" === typeof type && null !== type) {
        switch (type.$$typeof) {
          case REACT_FORWARD_REF_TYPE:
            return describeNativeComponentFrame(type.render, false);
          case REACT_MEMO_TYPE:
            return describeNativeComponentFrame(type.type, false);
          case REACT_LAZY_TYPE:
            var lazyComponent = type, payload = lazyComponent._payload;
            lazyComponent = lazyComponent._init;
            try {
              type = lazyComponent(payload);
            } catch (x) {
              return describeBuiltInComponentFrame("Lazy");
            }
            return describeComponentStackByType(type);
        }
        if ("string" === typeof type.name) {
          a: {
            payload = type.name;
            lazyComponent = type.env;
            var location = type.debugLocation;
            if (null != location && (type = Error.prepareStackTrace, Error.prepareStackTrace = void 0, location = location.stack, Error.prepareStackTrace = type, location.startsWith("Error: react-stack-top-frame\n") && (location = location.slice(29)), type = location.indexOf("\n"), -1 !== type && (location = location.slice(type + 1)), type = location.indexOf("react_stack_bottom_frame"), -1 !== type && (type = location.lastIndexOf("\n", type)), type = -1 !== type ? location = location.slice(0, type) : "", location = type.lastIndexOf("\n"), type = -1 === location ? type : type.slice(location + 1), -1 !== type.indexOf(payload))) {
              payload = "\n" + type;
              break a;
            }
            payload = describeBuiltInComponentFrame(
              payload + (lazyComponent ? " [" + lazyComponent + "]" : "")
            );
          }
          return payload;
        }
      }
      switch (type) {
        case REACT_SUSPENSE_LIST_TYPE:
          return describeBuiltInComponentFrame("SuspenseList");
        case REACT_SUSPENSE_TYPE:
          return describeBuiltInComponentFrame("Suspense");
      }
      return "";
    }
    function isEligibleForOutlining(request, boundary) {
      return (500 < boundary.byteSize || hasSuspenseyContent(boundary.contentState)) && null === boundary.contentPreamble;
    }
    function defaultErrorHandler(error) {
      if ("object" === typeof error && null !== error && "string" === typeof error.environmentName) {
        var JSCompiler_inline_result = error.environmentName;
        error = [error].slice(0);
        "string" === typeof error[0] ? error.splice(
          0,
          1,
          "%c%s%c " + error[0],
          "background: #e6e6e6;background: light-dark(rgba(0,0,0,0.1), rgba(255,255,255,0.25));color: #000000;color: light-dark(#000000, #ffffff);border-radius: 2px",
          " " + JSCompiler_inline_result + " ",
          ""
        ) : error.splice(
          0,
          0,
          "%c%s%c",
          "background: #e6e6e6;background: light-dark(rgba(0,0,0,0.1), rgba(255,255,255,0.25));color: #000000;color: light-dark(#000000, #ffffff);border-radius: 2px",
          " " + JSCompiler_inline_result + " ",
          ""
        );
        error.unshift(console);
        JSCompiler_inline_result = bind.apply(console.error, error);
        JSCompiler_inline_result();
      } else console.error(error);
      return null;
    }
    function RequestInstance(resumableState, renderState, rootFormatContext, progressiveChunkSize, onError, onAllReady, onShellReady, onShellError, onFatalError, onPostpone, formState) {
      var abortSet = /* @__PURE__ */ new Set();
      this.destination = null;
      this.flushScheduled = false;
      this.resumableState = resumableState;
      this.renderState = renderState;
      this.rootFormatContext = rootFormatContext;
      this.progressiveChunkSize = void 0 === progressiveChunkSize ? 12800 : progressiveChunkSize;
      this.status = 10;
      this.fatalError = null;
      this.pendingRootTasks = this.allPendingTasks = this.nextSegmentId = 0;
      this.completedPreambleSegments = this.completedRootSegment = null;
      this.byteSize = 0;
      this.abortableTasks = abortSet;
      this.pingedTasks = [];
      this.clientRenderedBoundaries = [];
      this.completedBoundaries = [];
      this.partialBoundaries = [];
      this.trackedPostpones = null;
      this.onError = void 0 === onError ? defaultErrorHandler : onError;
      this.onPostpone = void 0 === onPostpone ? noop : onPostpone;
      this.onAllReady = void 0 === onAllReady ? noop : onAllReady;
      this.onShellReady = void 0 === onShellReady ? noop : onShellReady;
      this.onShellError = void 0 === onShellError ? noop : onShellError;
      this.onFatalError = void 0 === onFatalError ? noop : onFatalError;
      this.formState = void 0 === formState ? null : formState;
    }
    function createRequest(children, resumableState, renderState, rootFormatContext, progressiveChunkSize, onError, onAllReady, onShellReady, onShellError, onFatalError, onPostpone, formState) {
      resumableState = new RequestInstance(
        resumableState,
        renderState,
        rootFormatContext,
        progressiveChunkSize,
        onError,
        onAllReady,
        onShellReady,
        onShellError,
        onFatalError,
        onPostpone,
        formState
      );
      renderState = createPendingSegment(
        resumableState,
        0,
        null,
        rootFormatContext,
        false,
        false
      );
      renderState.parentFlushed = true;
      children = createRenderTask(
        resumableState,
        null,
        children,
        -1,
        null,
        renderState,
        null,
        null,
        resumableState.abortableTasks,
        null,
        rootFormatContext,
        null,
        emptyTreeContext,
        null,
        null
      );
      pushComponentStack(children);
      resumableState.pingedTasks.push(children);
      return resumableState;
    }
    function createPrerenderRequest(children, resumableState, renderState, rootFormatContext, progressiveChunkSize, onError, onAllReady, onShellReady, onShellError, onFatalError, onPostpone) {
      children = createRequest(
        children,
        resumableState,
        renderState,
        rootFormatContext,
        progressiveChunkSize,
        onError,
        onAllReady,
        onShellReady,
        onShellError,
        onFatalError,
        onPostpone,
        void 0
      );
      children.trackedPostpones = {
        workingMap: /* @__PURE__ */ new Map(),
        rootNodes: [],
        rootSlots: null
      };
      return children;
    }
    function resumeRequest(children, postponedState, renderState, onError, onAllReady, onShellReady, onShellError, onFatalError, onPostpone) {
      renderState = new RequestInstance(
        postponedState.resumableState,
        renderState,
        postponedState.rootFormatContext,
        postponedState.progressiveChunkSize,
        onError,
        onAllReady,
        onShellReady,
        onShellError,
        onFatalError,
        onPostpone,
        null
      );
      renderState.nextSegmentId = postponedState.nextSegmentId;
      if ("number" === typeof postponedState.replaySlots)
        return onError = createPendingSegment(
          renderState,
          0,
          null,
          postponedState.rootFormatContext,
          false,
          false
        ), onError.parentFlushed = true, children = createRenderTask(
          renderState,
          null,
          children,
          -1,
          null,
          onError,
          null,
          null,
          renderState.abortableTasks,
          null,
          postponedState.rootFormatContext,
          null,
          emptyTreeContext,
          null,
          null
        ), pushComponentStack(children), renderState.pingedTasks.push(children), renderState;
      children = createReplayTask(
        renderState,
        null,
        {
          nodes: postponedState.replayNodes,
          slots: postponedState.replaySlots,
          pendingTasks: 0
        },
        children,
        -1,
        null,
        null,
        renderState.abortableTasks,
        null,
        postponedState.rootFormatContext,
        null,
        emptyTreeContext,
        null,
        null
      );
      pushComponentStack(children);
      renderState.pingedTasks.push(children);
      return renderState;
    }
    function resumeAndPrerenderRequest(children, postponedState, renderState, onError, onAllReady, onShellReady, onShellError, onFatalError, onPostpone) {
      children = resumeRequest(
        children,
        postponedState,
        renderState,
        onError,
        onAllReady,
        onShellReady,
        onShellError,
        onFatalError,
        onPostpone
      );
      children.trackedPostpones = {
        workingMap: /* @__PURE__ */ new Map(),
        rootNodes: [],
        rootSlots: null
      };
      return children;
    }
    var currentRequest = null;
    function pingTask(request, task) {
      request.pingedTasks.push(task);
      1 === request.pingedTasks.length && (request.flushScheduled = null !== request.destination, null !== request.trackedPostpones || 10 === request.status ? scheduleMicrotask(function() {
        return performWork(request);
      }) : scheduleWork(function() {
        return performWork(request);
      }));
    }
    function createSuspenseBoundary(request, row, fallbackAbortableTasks, contentPreamble, fallbackPreamble) {
      fallbackAbortableTasks = {
        status: 0,
        rootSegmentID: -1,
        parentFlushed: false,
        pendingTasks: 0,
        row,
        completedSegments: [],
        byteSize: 0,
        fallbackAbortableTasks,
        errorDigest: null,
        contentState: createHoistableState(),
        fallbackState: createHoistableState(),
        contentPreamble,
        fallbackPreamble,
        trackedContentKeyPath: null,
        trackedFallbackNode: null
      };
      null !== row && (row.pendingTasks++, contentPreamble = row.boundaries, null !== contentPreamble && (request.allPendingTasks++, fallbackAbortableTasks.pendingTasks++, contentPreamble.push(fallbackAbortableTasks)), request = row.inheritedHoistables, null !== request && hoistHoistables(fallbackAbortableTasks.contentState, request));
      return fallbackAbortableTasks;
    }
    function createRenderTask(request, thenableState2, node, childIndex, blockedBoundary, blockedSegment, blockedPreamble, hoistableState, abortSet, keyPath, formatContext, context, treeContext, row, componentStack) {
      request.allPendingTasks++;
      null === blockedBoundary ? request.pendingRootTasks++ : blockedBoundary.pendingTasks++;
      null !== row && row.pendingTasks++;
      var task = {
        replay: null,
        node,
        childIndex,
        ping: function() {
          return pingTask(request, task);
        },
        blockedBoundary,
        blockedSegment,
        blockedPreamble,
        hoistableState,
        abortSet,
        keyPath,
        formatContext,
        context,
        treeContext,
        row,
        componentStack,
        thenableState: thenableState2
      };
      abortSet.add(task);
      return task;
    }
    function createReplayTask(request, thenableState2, replay, node, childIndex, blockedBoundary, hoistableState, abortSet, keyPath, formatContext, context, treeContext, row, componentStack) {
      request.allPendingTasks++;
      null === blockedBoundary ? request.pendingRootTasks++ : blockedBoundary.pendingTasks++;
      null !== row && row.pendingTasks++;
      replay.pendingTasks++;
      var task = {
        replay,
        node,
        childIndex,
        ping: function() {
          return pingTask(request, task);
        },
        blockedBoundary,
        blockedSegment: null,
        blockedPreamble: null,
        hoistableState,
        abortSet,
        keyPath,
        formatContext,
        context,
        treeContext,
        row,
        componentStack,
        thenableState: thenableState2
      };
      abortSet.add(task);
      return task;
    }
    function createPendingSegment(request, index, boundary, parentFormatContext, lastPushedText, textEmbedded) {
      return {
        status: 0,
        parentFlushed: false,
        id: -1,
        index,
        chunks: [],
        children: [],
        preambleChildren: [],
        parentFormatContext,
        boundary,
        lastPushedText,
        textEmbedded
      };
    }
    function pushComponentStack(task) {
      var node = task.node;
      if ("object" === typeof node && null !== node)
        switch (node.$$typeof) {
          case REACT_ELEMENT_TYPE:
            task.componentStack = { parent: task.componentStack, type: node.type };
        }
    }
    function replaceSuspenseComponentStackWithSuspenseFallbackStack(componentStack) {
      return null === componentStack ? null : { parent: componentStack.parent, type: "Suspense Fallback" };
    }
    function getThrownInfo(node$jscomp$0) {
      var errorInfo = {};
      node$jscomp$0 && Object.defineProperty(errorInfo, "componentStack", {
        configurable: true,
        enumerable: true,
        get: function() {
          try {
            var info = "", node = node$jscomp$0;
            do
              info += describeComponentStackByType(node.type), node = node.parent;
            while (node);
            var JSCompiler_inline_result = info;
          } catch (x) {
            JSCompiler_inline_result = "\nError generating stack: " + x.message + "\n" + x.stack;
          }
          Object.defineProperty(errorInfo, "componentStack", {
            value: JSCompiler_inline_result
          });
          return JSCompiler_inline_result;
        }
      });
      return errorInfo;
    }
    function logRecoverableError(request, error, errorInfo) {
      request = request.onError;
      error = request(error, errorInfo);
      if (null == error || "string" === typeof error) return error;
    }
    function fatalError(request, error) {
      var onShellError = request.onShellError, onFatalError = request.onFatalError;
      onShellError(error);
      onFatalError(error);
      null !== request.destination ? (request.status = 14, closeWithError(request.destination, error)) : (request.status = 13, request.fatalError = error);
    }
    function finishSuspenseListRow(request, row) {
      unblockSuspenseListRow(request, row.next, row.hoistables);
    }
    function unblockSuspenseListRow(request, unblockedRow, inheritedHoistables) {
      for (; null !== unblockedRow; ) {
        null !== inheritedHoistables && (hoistHoistables(unblockedRow.hoistables, inheritedHoistables), unblockedRow.inheritedHoistables = inheritedHoistables);
        var unblockedBoundaries = unblockedRow.boundaries;
        if (null !== unblockedBoundaries) {
          unblockedRow.boundaries = null;
          for (var i = 0; i < unblockedBoundaries.length; i++) {
            var unblockedBoundary = unblockedBoundaries[i];
            null !== inheritedHoistables && hoistHoistables(unblockedBoundary.contentState, inheritedHoistables);
            finishedTask(request, unblockedBoundary, null, null);
          }
        }
        unblockedRow.pendingTasks--;
        if (0 < unblockedRow.pendingTasks) break;
        inheritedHoistables = unblockedRow.hoistables;
        unblockedRow = unblockedRow.next;
      }
    }
    function tryToResolveTogetherRow(request, togetherRow) {
      var boundaries = togetherRow.boundaries;
      if (null !== boundaries && togetherRow.pendingTasks === boundaries.length) {
        for (var allCompleteAndInlinable = true, i = 0; i < boundaries.length; i++) {
          var rowBoundary = boundaries[i];
          if (1 !== rowBoundary.pendingTasks || rowBoundary.parentFlushed || isEligibleForOutlining(request, rowBoundary)) {
            allCompleteAndInlinable = false;
            break;
          }
        }
        allCompleteAndInlinable && unblockSuspenseListRow(request, togetherRow, togetherRow.hoistables);
      }
    }
    function createSuspenseListRow(previousRow) {
      var newRow = {
        pendingTasks: 1,
        boundaries: null,
        hoistables: createHoistableState(),
        inheritedHoistables: null,
        together: false,
        next: null
      };
      null !== previousRow && 0 < previousRow.pendingTasks && (newRow.pendingTasks++, newRow.boundaries = [], previousRow.next = newRow);
      return newRow;
    }
    function renderSuspenseListRows(request, task, keyPath, rows, revealOrder) {
      var prevKeyPath = task.keyPath, prevTreeContext = task.treeContext, prevRow = task.row;
      task.keyPath = keyPath;
      keyPath = rows.length;
      var previousSuspenseListRow = null;
      if (null !== task.replay) {
        var resumeSlots = task.replay.slots;
        if (null !== resumeSlots && "object" === typeof resumeSlots)
          for (var n = 0; n < keyPath; n++) {
            var i = "backwards" !== revealOrder && "unstable_legacy-backwards" !== revealOrder ? n : keyPath - 1 - n, node = rows[i];
            task.row = previousSuspenseListRow = createSuspenseListRow(
              previousSuspenseListRow
            );
            task.treeContext = pushTreeContext(prevTreeContext, keyPath, i);
            var resumeSegmentID = resumeSlots[i];
            "number" === typeof resumeSegmentID ? (resumeNode(request, task, resumeSegmentID, node, i), delete resumeSlots[i]) : renderNode(request, task, node, i);
            0 === --previousSuspenseListRow.pendingTasks && finishSuspenseListRow(request, previousSuspenseListRow);
          }
        else
          for (resumeSlots = 0; resumeSlots < keyPath; resumeSlots++)
            n = "backwards" !== revealOrder && "unstable_legacy-backwards" !== revealOrder ? resumeSlots : keyPath - 1 - resumeSlots, i = rows[n], task.row = previousSuspenseListRow = createSuspenseListRow(previousSuspenseListRow), task.treeContext = pushTreeContext(prevTreeContext, keyPath, n), renderNode(request, task, i, n), 0 === --previousSuspenseListRow.pendingTasks && finishSuspenseListRow(request, previousSuspenseListRow);
      } else if ("backwards" !== revealOrder && "unstable_legacy-backwards" !== revealOrder)
        for (revealOrder = 0; revealOrder < keyPath; revealOrder++)
          resumeSlots = rows[revealOrder], task.row = previousSuspenseListRow = createSuspenseListRow(previousSuspenseListRow), task.treeContext = pushTreeContext(
            prevTreeContext,
            keyPath,
            revealOrder
          ), renderNode(request, task, resumeSlots, revealOrder), 0 === --previousSuspenseListRow.pendingTasks && finishSuspenseListRow(request, previousSuspenseListRow);
      else {
        revealOrder = task.blockedSegment;
        resumeSlots = revealOrder.children.length;
        n = revealOrder.chunks.length;
        for (i = keyPath - 1; 0 <= i; i--) {
          node = rows[i];
          task.row = previousSuspenseListRow = createSuspenseListRow(
            previousSuspenseListRow
          );
          task.treeContext = pushTreeContext(prevTreeContext, keyPath, i);
          resumeSegmentID = createPendingSegment(
            request,
            n,
            null,
            task.formatContext,
            0 === i ? revealOrder.lastPushedText : true,
            true
          );
          revealOrder.children.splice(resumeSlots, 0, resumeSegmentID);
          task.blockedSegment = resumeSegmentID;
          try {
            renderNode(request, task, node, i), resumeSegmentID.lastPushedText && resumeSegmentID.textEmbedded && resumeSegmentID.chunks.push(textSeparator), resumeSegmentID.status = 1, finishedSegment(request, task.blockedBoundary, resumeSegmentID), 0 === --previousSuspenseListRow.pendingTasks && finishSuspenseListRow(request, previousSuspenseListRow);
          } catch (thrownValue) {
            throw resumeSegmentID.status = 12 === request.status ? 3 : 4, thrownValue;
          }
        }
        task.blockedSegment = revealOrder;
        revealOrder.lastPushedText = false;
      }
      null !== prevRow && null !== previousSuspenseListRow && 0 < previousSuspenseListRow.pendingTasks && (prevRow.pendingTasks++, previousSuspenseListRow.next = prevRow);
      task.treeContext = prevTreeContext;
      task.row = prevRow;
      task.keyPath = prevKeyPath;
    }
    function renderWithHooks(request, task, keyPath, Component, props, secondArg) {
      var prevThenableState = task.thenableState;
      task.thenableState = null;
      currentlyRenderingComponent = {};
      currentlyRenderingTask = task;
      currentlyRenderingRequest = request;
      currentlyRenderingKeyPath = keyPath;
      actionStateCounter = localIdCounter = 0;
      actionStateMatchingIndex = -1;
      thenableIndexCounter = 0;
      thenableState = prevThenableState;
      for (request = Component(props, secondArg); didScheduleRenderPhaseUpdate; )
        didScheduleRenderPhaseUpdate = false, actionStateCounter = localIdCounter = 0, actionStateMatchingIndex = -1, thenableIndexCounter = 0, numberOfReRenders += 1, workInProgressHook = null, request = Component(props, secondArg);
      resetHooksState();
      return request;
    }
    function finishFunctionComponent(request, task, keyPath, children, hasId, actionStateCount, actionStateMatchingIndex2) {
      var didEmitActionStateMarkers = false;
      if (0 !== actionStateCount && null !== request.formState) {
        var segment = task.blockedSegment;
        if (null !== segment) {
          didEmitActionStateMarkers = true;
          segment = segment.chunks;
          for (var i = 0; i < actionStateCount; i++)
            i === actionStateMatchingIndex2 ? segment.push(formStateMarkerIsMatching) : segment.push(formStateMarkerIsNotMatching);
        }
      }
      actionStateCount = task.keyPath;
      task.keyPath = keyPath;
      hasId ? (keyPath = task.treeContext, task.treeContext = pushTreeContext(keyPath, 1, 0), renderNode(request, task, children, -1), task.treeContext = keyPath) : didEmitActionStateMarkers ? renderNode(request, task, children, -1) : renderNodeDestructive(request, task, children, -1);
      task.keyPath = actionStateCount;
    }
    function renderElement(request, task, keyPath, type, props, ref) {
      if ("function" === typeof type)
        if (type.prototype && type.prototype.isReactComponent) {
          var newProps = props;
          if ("ref" in props) {
            newProps = {};
            for (var propName in props)
              "ref" !== propName && (newProps[propName] = props[propName]);
          }
          var defaultProps = type.defaultProps;
          if (defaultProps) {
            newProps === props && (newProps = assign({}, newProps, props));
            for (var propName$44 in defaultProps)
              void 0 === newProps[propName$44] && (newProps[propName$44] = defaultProps[propName$44]);
          }
          props = newProps;
          newProps = emptyContextObject;
          defaultProps = type.contextType;
          "object" === typeof defaultProps && null !== defaultProps && (newProps = defaultProps._currentValue);
          newProps = new type(props, newProps);
          var initialState = void 0 !== newProps.state ? newProps.state : null;
          newProps.updater = classComponentUpdater;
          newProps.props = props;
          newProps.state = initialState;
          defaultProps = { queue: [], replace: false };
          newProps._reactInternals = defaultProps;
          ref = type.contextType;
          newProps.context = "object" === typeof ref && null !== ref ? ref._currentValue : emptyContextObject;
          ref = type.getDerivedStateFromProps;
          "function" === typeof ref && (ref = ref(props, initialState), initialState = null === ref || void 0 === ref ? initialState : assign({}, initialState, ref), newProps.state = initialState);
          if ("function" !== typeof type.getDerivedStateFromProps && "function" !== typeof newProps.getSnapshotBeforeUpdate && ("function" === typeof newProps.UNSAFE_componentWillMount || "function" === typeof newProps.componentWillMount))
            if (type = newProps.state, "function" === typeof newProps.componentWillMount && newProps.componentWillMount(), "function" === typeof newProps.UNSAFE_componentWillMount && newProps.UNSAFE_componentWillMount(), type !== newProps.state && classComponentUpdater.enqueueReplaceState(
              newProps,
              newProps.state,
              null
            ), null !== defaultProps.queue && 0 < defaultProps.queue.length)
              if (type = defaultProps.queue, ref = defaultProps.replace, defaultProps.queue = null, defaultProps.replace = false, ref && 1 === type.length)
                newProps.state = type[0];
              else {
                defaultProps = ref ? type[0] : newProps.state;
                initialState = true;
                for (ref = ref ? 1 : 0; ref < type.length; ref++)
                  propName$44 = type[ref], propName$44 = "function" === typeof propName$44 ? propName$44.call(newProps, defaultProps, props, void 0) : propName$44, null != propName$44 && (initialState ? (initialState = false, defaultProps = assign({}, defaultProps, propName$44)) : assign(defaultProps, propName$44));
                newProps.state = defaultProps;
              }
            else defaultProps.queue = null;
          type = newProps.render();
          if (12 === request.status) throw null;
          props = task.keyPath;
          task.keyPath = keyPath;
          renderNodeDestructive(request, task, type, -1);
          task.keyPath = props;
        } else {
          type = renderWithHooks(request, task, keyPath, type, props, void 0);
          if (12 === request.status) throw null;
          finishFunctionComponent(
            request,
            task,
            keyPath,
            type,
            0 !== localIdCounter,
            actionStateCounter,
            actionStateMatchingIndex
          );
        }
      else if ("string" === typeof type)
        if (newProps = task.blockedSegment, null === newProps)
          newProps = props.children, defaultProps = task.formatContext, initialState = task.keyPath, task.formatContext = getChildFormatContext(defaultProps, type, props), task.keyPath = keyPath, renderNode(request, task, newProps, -1), task.formatContext = defaultProps, task.keyPath = initialState;
        else {
          initialState = pushStartInstance(
            newProps.chunks,
            type,
            props,
            request.resumableState,
            request.renderState,
            task.blockedPreamble,
            task.hoistableState,
            task.formatContext,
            newProps.lastPushedText
          );
          newProps.lastPushedText = false;
          defaultProps = task.formatContext;
          ref = task.keyPath;
          task.keyPath = keyPath;
          if (3 === (task.formatContext = getChildFormatContext(defaultProps, type, props)).insertionMode) {
            keyPath = createPendingSegment(
              request,
              0,
              null,
              task.formatContext,
              false,
              false
            );
            newProps.preambleChildren.push(keyPath);
            task.blockedSegment = keyPath;
            try {
              keyPath.status = 6, renderNode(request, task, initialState, -1), keyPath.lastPushedText && keyPath.textEmbedded && keyPath.chunks.push(textSeparator), keyPath.status = 1, finishedSegment(request, task.blockedBoundary, keyPath);
            } finally {
              task.blockedSegment = newProps;
            }
          } else renderNode(request, task, initialState, -1);
          task.formatContext = defaultProps;
          task.keyPath = ref;
          a: {
            task = newProps.chunks;
            request = request.resumableState;
            switch (type) {
              case "title":
              case "style":
              case "script":
              case "area":
              case "base":
              case "br":
              case "col":
              case "embed":
              case "hr":
              case "img":
              case "input":
              case "keygen":
              case "link":
              case "meta":
              case "param":
              case "source":
              case "track":
              case "wbr":
                break a;
              case "body":
                if (1 >= defaultProps.insertionMode) {
                  request.hasBody = true;
                  break a;
                }
                break;
              case "html":
                if (0 === defaultProps.insertionMode) {
                  request.hasHtml = true;
                  break a;
                }
                break;
              case "head":
                if (1 >= defaultProps.insertionMode) break a;
            }
            task.push(endChunkForTag(type));
          }
          newProps.lastPushedText = false;
        }
      else {
        switch (type) {
          case REACT_LEGACY_HIDDEN_TYPE:
          case REACT_STRICT_MODE_TYPE:
          case REACT_PROFILER_TYPE:
          case REACT_FRAGMENT_TYPE:
            type = task.keyPath;
            task.keyPath = keyPath;
            renderNodeDestructive(request, task, props.children, -1);
            task.keyPath = type;
            return;
          case REACT_ACTIVITY_TYPE:
            type = task.blockedSegment;
            null === type ? "hidden" !== props.mode && (type = task.keyPath, task.keyPath = keyPath, renderNode(request, task, props.children, -1), task.keyPath = type) : "hidden" !== props.mode && (type.chunks.push(startActivityBoundary), type.lastPushedText = false, newProps = task.keyPath, task.keyPath = keyPath, renderNode(request, task, props.children, -1), task.keyPath = newProps, type.chunks.push(endActivityBoundary), type.lastPushedText = false);
            return;
          case REACT_SUSPENSE_LIST_TYPE:
            a: {
              type = props.children;
              props = props.revealOrder;
              if ("forwards" === props || "backwards" === props || "unstable_legacy-backwards" === props) {
                if (isArrayImpl(type)) {
                  renderSuspenseListRows(request, task, keyPath, type, props);
                  break a;
                }
                if (newProps = getIteratorFn(type)) {
                  if (newProps = newProps.call(type)) {
                    defaultProps = newProps.next();
                    if (!defaultProps.done) {
                      do
                        defaultProps = newProps.next();
                      while (!defaultProps.done);
                      renderSuspenseListRows(request, task, keyPath, type, props);
                    }
                    break a;
                  }
                }
              }
              "together" === props ? (props = task.keyPath, newProps = task.row, defaultProps = task.row = createSuspenseListRow(null), defaultProps.boundaries = [], defaultProps.together = true, task.keyPath = keyPath, renderNodeDestructive(request, task, type, -1), 0 === --defaultProps.pendingTasks && finishSuspenseListRow(request, defaultProps), task.keyPath = props, task.row = newProps, null !== newProps && 0 < defaultProps.pendingTasks && (newProps.pendingTasks++, defaultProps.next = newProps)) : (props = task.keyPath, task.keyPath = keyPath, renderNodeDestructive(request, task, type, -1), task.keyPath = props);
            }
            return;
          case REACT_VIEW_TRANSITION_TYPE:
          case REACT_SCOPE_TYPE:
            throw Error(formatProdErrorMessage(343));
          case REACT_SUSPENSE_TYPE:
            a: if (null !== task.replay) {
              type = task.keyPath;
              newProps = task.formatContext;
              defaultProps = task.row;
              task.keyPath = keyPath;
              task.formatContext = getSuspenseContentFormatContext(
                request.resumableState,
                newProps
              );
              task.row = null;
              keyPath = props.children;
              try {
                renderNode(request, task, keyPath, -1);
              } finally {
                task.keyPath = type, task.formatContext = newProps, task.row = defaultProps;
              }
            } else {
              type = task.keyPath;
              ref = task.formatContext;
              var prevRow = task.row;
              propName$44 = task.blockedBoundary;
              propName = task.blockedPreamble;
              var parentHoistableState = task.hoistableState, parentSegment = task.blockedSegment, fallback = props.fallback;
              props = props.children;
              var fallbackAbortSet = /* @__PURE__ */ new Set();
              var newBoundary = 2 > task.formatContext.insertionMode ? createSuspenseBoundary(
                request,
                task.row,
                fallbackAbortSet,
                createPreambleState(),
                createPreambleState()
              ) : createSuspenseBoundary(
                request,
                task.row,
                fallbackAbortSet,
                null,
                null
              );
              null !== request.trackedPostpones && (newBoundary.trackedContentKeyPath = keyPath);
              var boundarySegment = createPendingSegment(
                request,
                parentSegment.chunks.length,
                newBoundary,
                task.formatContext,
                false,
                false
              );
              parentSegment.children.push(boundarySegment);
              parentSegment.lastPushedText = false;
              var contentRootSegment = createPendingSegment(
                request,
                0,
                null,
                task.formatContext,
                false,
                false
              );
              contentRootSegment.parentFlushed = true;
              if (null !== request.trackedPostpones) {
                newProps = task.componentStack;
                defaultProps = [keyPath[0], "Suspense Fallback", keyPath[2]];
                initialState = [defaultProps[1], defaultProps[2], [], null];
                request.trackedPostpones.workingMap.set(defaultProps, initialState);
                newBoundary.trackedFallbackNode = initialState;
                task.blockedSegment = boundarySegment;
                task.blockedPreamble = newBoundary.fallbackPreamble;
                task.keyPath = defaultProps;
                task.formatContext = getSuspenseFallbackFormatContext(
                  request.resumableState,
                  ref
                );
                task.componentStack = replaceSuspenseComponentStackWithSuspenseFallbackStack(newProps);
                boundarySegment.status = 6;
                try {
                  renderNode(request, task, fallback, -1), boundarySegment.lastPushedText && boundarySegment.textEmbedded && boundarySegment.chunks.push(textSeparator), boundarySegment.status = 1, finishedSegment(request, propName$44, boundarySegment);
                } catch (thrownValue) {
                  throw boundarySegment.status = 12 === request.status ? 3 : 4, thrownValue;
                } finally {
                  task.blockedSegment = parentSegment, task.blockedPreamble = propName, task.keyPath = type, task.formatContext = ref;
                }
                task = createRenderTask(
                  request,
                  null,
                  props,
                  -1,
                  newBoundary,
                  contentRootSegment,
                  newBoundary.contentPreamble,
                  newBoundary.contentState,
                  task.abortSet,
                  keyPath,
                  getSuspenseContentFormatContext(
                    request.resumableState,
                    task.formatContext
                  ),
                  task.context,
                  task.treeContext,
                  null,
                  newProps
                );
                pushComponentStack(task);
                request.pingedTasks.push(task);
              } else {
                task.blockedBoundary = newBoundary;
                task.blockedPreamble = newBoundary.contentPreamble;
                task.hoistableState = newBoundary.contentState;
                task.blockedSegment = contentRootSegment;
                task.keyPath = keyPath;
                task.formatContext = getSuspenseContentFormatContext(
                  request.resumableState,
                  ref
                );
                task.row = null;
                contentRootSegment.status = 6;
                try {
                  if (renderNode(request, task, props, -1), contentRootSegment.lastPushedText && contentRootSegment.textEmbedded && contentRootSegment.chunks.push(textSeparator), contentRootSegment.status = 1, finishedSegment(request, newBoundary, contentRootSegment), queueCompletedSegment(newBoundary, contentRootSegment), 0 === newBoundary.pendingTasks && 0 === newBoundary.status) {
                    if (newBoundary.status = 1, !isEligibleForOutlining(request, newBoundary)) {
                      null !== prevRow && 0 === --prevRow.pendingTasks && finishSuspenseListRow(request, prevRow);
                      0 === request.pendingRootTasks && task.blockedPreamble && preparePreamble(request);
                      break a;
                    }
                  } else
                    null !== prevRow && prevRow.together && tryToResolveTogetherRow(request, prevRow);
                } catch (thrownValue$31) {
                  newBoundary.status = 4, 12 === request.status ? (contentRootSegment.status = 3, newProps = request.fatalError) : (contentRootSegment.status = 4, newProps = thrownValue$31), defaultProps = getThrownInfo(task.componentStack), initialState = logRecoverableError(
                    request,
                    newProps,
                    defaultProps
                  ), newBoundary.errorDigest = initialState, untrackBoundary(request, newBoundary);
                } finally {
                  task.blockedBoundary = propName$44, task.blockedPreamble = propName, task.hoistableState = parentHoistableState, task.blockedSegment = parentSegment, task.keyPath = type, task.formatContext = ref, task.row = prevRow;
                }
                task = createRenderTask(
                  request,
                  null,
                  fallback,
                  -1,
                  propName$44,
                  boundarySegment,
                  newBoundary.fallbackPreamble,
                  newBoundary.fallbackState,
                  fallbackAbortSet,
                  [keyPath[0], "Suspense Fallback", keyPath[2]],
                  getSuspenseFallbackFormatContext(
                    request.resumableState,
                    task.formatContext
                  ),
                  task.context,
                  task.treeContext,
                  task.row,
                  replaceSuspenseComponentStackWithSuspenseFallbackStack(
                    task.componentStack
                  )
                );
                pushComponentStack(task);
                request.pingedTasks.push(task);
              }
            }
            return;
        }
        if ("object" === typeof type && null !== type)
          switch (type.$$typeof) {
            case REACT_FORWARD_REF_TYPE:
              if ("ref" in props)
                for (parentSegment in newProps = {}, props)
                  "ref" !== parentSegment && (newProps[parentSegment] = props[parentSegment]);
              else newProps = props;
              type = renderWithHooks(
                request,
                task,
                keyPath,
                type.render,
                newProps,
                ref
              );
              finishFunctionComponent(
                request,
                task,
                keyPath,
                type,
                0 !== localIdCounter,
                actionStateCounter,
                actionStateMatchingIndex
              );
              return;
            case REACT_MEMO_TYPE:
              renderElement(request, task, keyPath, type.type, props, ref);
              return;
            case REACT_CONTEXT_TYPE:
              defaultProps = props.children;
              newProps = task.keyPath;
              props = props.value;
              initialState = type._currentValue;
              type._currentValue = props;
              ref = currentActiveSnapshot;
              currentActiveSnapshot = type = {
                parent: ref,
                depth: null === ref ? 0 : ref.depth + 1,
                context: type,
                parentValue: initialState,
                value: props
              };
              task.context = type;
              task.keyPath = keyPath;
              renderNodeDestructive(request, task, defaultProps, -1);
              request = currentActiveSnapshot;
              if (null === request) throw Error(formatProdErrorMessage(403));
              request.context._currentValue = request.parentValue;
              request = currentActiveSnapshot = request.parent;
              task.context = request;
              task.keyPath = newProps;
              return;
            case REACT_CONSUMER_TYPE:
              props = props.children;
              type = props(type._context._currentValue);
              props = task.keyPath;
              task.keyPath = keyPath;
              renderNodeDestructive(request, task, type, -1);
              task.keyPath = props;
              return;
            case REACT_LAZY_TYPE:
              newProps = type._init;
              type = newProps(type._payload);
              if (12 === request.status) throw null;
              renderElement(request, task, keyPath, type, props, ref);
              return;
          }
        throw Error(
          formatProdErrorMessage(130, null == type ? type : typeof type, "")
        );
      }
    }
    function resumeNode(request, task, segmentId, node, childIndex) {
      var prevReplay = task.replay, blockedBoundary = task.blockedBoundary, resumedSegment = createPendingSegment(
        request,
        0,
        null,
        task.formatContext,
        false,
        false
      );
      resumedSegment.id = segmentId;
      resumedSegment.parentFlushed = true;
      try {
        task.replay = null, task.blockedSegment = resumedSegment, renderNode(request, task, node, childIndex), resumedSegment.status = 1, finishedSegment(request, blockedBoundary, resumedSegment), null === blockedBoundary ? request.completedRootSegment = resumedSegment : (queueCompletedSegment(blockedBoundary, resumedSegment), blockedBoundary.parentFlushed && request.partialBoundaries.push(blockedBoundary));
      } finally {
        task.replay = prevReplay, task.blockedSegment = null;
      }
    }
    function renderNodeDestructive(request, task, node, childIndex) {
      null !== task.replay && "number" === typeof task.replay.slots ? resumeNode(request, task, task.replay.slots, node, childIndex) : (task.node = node, task.childIndex = childIndex, node = task.componentStack, pushComponentStack(task), retryNode(request, task), task.componentStack = node);
    }
    function retryNode(request, task) {
      var node = task.node, childIndex = task.childIndex;
      if (null !== node) {
        if ("object" === typeof node) {
          switch (node.$$typeof) {
            case REACT_ELEMENT_TYPE:
              var type = node.type, key = node.key, props = node.props;
              node = props.ref;
              var ref = void 0 !== node ? node : null, name = getComponentNameFromType(type), keyOrIndex = null == key ? -1 === childIndex ? 0 : childIndex : key;
              key = [task.keyPath, name, keyOrIndex];
              if (null !== task.replay)
                a: {
                  var replay = task.replay;
                  childIndex = replay.nodes;
                  for (node = 0; node < childIndex.length; node++) {
                    var node$jscomp$0 = childIndex[node];
                    if (keyOrIndex === node$jscomp$0[1]) {
                      if (4 === node$jscomp$0.length) {
                        if (null !== name && name !== node$jscomp$0[0])
                          throw Error(
                            formatProdErrorMessage(490, node$jscomp$0[0], name)
                          );
                        var childNodes = node$jscomp$0[2];
                        name = node$jscomp$0[3];
                        keyOrIndex = task.node;
                        task.replay = {
                          nodes: childNodes,
                          slots: name,
                          pendingTasks: 1
                        };
                        try {
                          renderElement(request, task, key, type, props, ref);
                          if (1 === task.replay.pendingTasks && 0 < task.replay.nodes.length)
                            throw Error(formatProdErrorMessage(488));
                          task.replay.pendingTasks--;
                        } catch (x) {
                          if ("object" === typeof x && null !== x && (x === SuspenseException || "function" === typeof x.then))
                            throw task.node === keyOrIndex ? task.replay = replay : childIndex.splice(node, 1), x;
                          task.replay.pendingTasks--;
                          props = getThrownInfo(task.componentStack);
                          key = request;
                          request = task.blockedBoundary;
                          type = x;
                          props = logRecoverableError(key, type, props);
                          abortRemainingReplayNodes(
                            key,
                            request,
                            childNodes,
                            name,
                            type,
                            props
                          );
                        }
                        task.replay = replay;
                      } else {
                        if (type !== REACT_SUSPENSE_TYPE)
                          throw Error(
                            formatProdErrorMessage(
                              490,
                              "Suspense",
                              getComponentNameFromType(type) || "Unknown"
                            )
                          );
                        b: {
                          replay = void 0;
                          type = node$jscomp$0[5];
                          ref = node$jscomp$0[2];
                          name = node$jscomp$0[3];
                          keyOrIndex = null === node$jscomp$0[4] ? [] : node$jscomp$0[4][2];
                          node$jscomp$0 = null === node$jscomp$0[4] ? null : node$jscomp$0[4][3];
                          var prevKeyPath = task.keyPath, prevContext = task.formatContext, prevRow = task.row, previousReplaySet = task.replay, parentBoundary = task.blockedBoundary, parentHoistableState = task.hoistableState, content = props.children, fallback = props.fallback, fallbackAbortSet = /* @__PURE__ */ new Set();
                          props = 2 > task.formatContext.insertionMode ? createSuspenseBoundary(
                            request,
                            task.row,
                            fallbackAbortSet,
                            createPreambleState(),
                            createPreambleState()
                          ) : createSuspenseBoundary(
                            request,
                            task.row,
                            fallbackAbortSet,
                            null,
                            null
                          );
                          props.parentFlushed = true;
                          props.rootSegmentID = type;
                          task.blockedBoundary = props;
                          task.hoistableState = props.contentState;
                          task.keyPath = key;
                          task.formatContext = getSuspenseContentFormatContext(
                            request.resumableState,
                            prevContext
                          );
                          task.row = null;
                          task.replay = {
                            nodes: ref,
                            slots: name,
                            pendingTasks: 1
                          };
                          try {
                            renderNode(request, task, content, -1);
                            if (1 === task.replay.pendingTasks && 0 < task.replay.nodes.length)
                              throw Error(formatProdErrorMessage(488));
                            task.replay.pendingTasks--;
                            if (0 === props.pendingTasks && 0 === props.status) {
                              props.status = 1;
                              request.completedBoundaries.push(props);
                              break b;
                            }
                          } catch (error) {
                            props.status = 4, childNodes = getThrownInfo(task.componentStack), replay = logRecoverableError(
                              request,
                              error,
                              childNodes
                            ), props.errorDigest = replay, task.replay.pendingTasks--, request.clientRenderedBoundaries.push(props);
                          } finally {
                            task.blockedBoundary = parentBoundary, task.hoistableState = parentHoistableState, task.replay = previousReplaySet, task.keyPath = prevKeyPath, task.formatContext = prevContext, task.row = prevRow;
                          }
                          childNodes = createReplayTask(
                            request,
                            null,
                            {
                              nodes: keyOrIndex,
                              slots: node$jscomp$0,
                              pendingTasks: 0
                            },
                            fallback,
                            -1,
                            parentBoundary,
                            props.fallbackState,
                            fallbackAbortSet,
                            [key[0], "Suspense Fallback", key[2]],
                            getSuspenseFallbackFormatContext(
                              request.resumableState,
                              task.formatContext
                            ),
                            task.context,
                            task.treeContext,
                            task.row,
                            replaceSuspenseComponentStackWithSuspenseFallbackStack(
                              task.componentStack
                            )
                          );
                          pushComponentStack(childNodes);
                          request.pingedTasks.push(childNodes);
                        }
                      }
                      childIndex.splice(node, 1);
                      break a;
                    }
                  }
                }
              else renderElement(request, task, key, type, props, ref);
              return;
            case REACT_PORTAL_TYPE:
              throw Error(formatProdErrorMessage(257));
            case REACT_LAZY_TYPE:
              childNodes = node._init;
              node = childNodes(node._payload);
              if (12 === request.status) throw null;
              renderNodeDestructive(request, task, node, childIndex);
              return;
          }
          if (isArrayImpl(node)) {
            renderChildrenArray(request, task, node, childIndex);
            return;
          }
          if (childNodes = getIteratorFn(node)) {
            if (childNodes = childNodes.call(node)) {
              node = childNodes.next();
              if (!node.done) {
                props = [];
                do
                  props.push(node.value), node = childNodes.next();
                while (!node.done);
                renderChildrenArray(request, task, props, childIndex);
              }
              return;
            }
          }
          if ("function" === typeof node.then)
            return task.thenableState = null, renderNodeDestructive(request, task, unwrapThenable(node), childIndex);
          if (node.$$typeof === REACT_CONTEXT_TYPE)
            return renderNodeDestructive(
              request,
              task,
              node._currentValue,
              childIndex
            );
          childIndex = Object.prototype.toString.call(node);
          throw Error(
            formatProdErrorMessage(
              31,
              "[object Object]" === childIndex ? "object with keys {" + Object.keys(node).join(", ") + "}" : childIndex
            )
          );
        }
        if ("string" === typeof node)
          childIndex = task.blockedSegment, null !== childIndex && (childIndex.lastPushedText = pushTextInstance(
            childIndex.chunks,
            node,
            request.renderState,
            childIndex.lastPushedText
          ));
        else if ("number" === typeof node || "bigint" === typeof node)
          childIndex = task.blockedSegment, null !== childIndex && (childIndex.lastPushedText = pushTextInstance(
            childIndex.chunks,
            "" + node,
            request.renderState,
            childIndex.lastPushedText
          ));
      }
    }
    function renderChildrenArray(request, task, children, childIndex) {
      var prevKeyPath = task.keyPath;
      if (-1 !== childIndex && (task.keyPath = [task.keyPath, "Fragment", childIndex], null !== task.replay)) {
        for (var replay = task.replay, replayNodes = replay.nodes, j = 0; j < replayNodes.length; j++) {
          var node = replayNodes[j];
          if (node[1] === childIndex) {
            childIndex = node[2];
            node = node[3];
            task.replay = { nodes: childIndex, slots: node, pendingTasks: 1 };
            try {
              renderChildrenArray(request, task, children, -1);
              if (1 === task.replay.pendingTasks && 0 < task.replay.nodes.length)
                throw Error(formatProdErrorMessage(488));
              task.replay.pendingTasks--;
            } catch (x) {
              if ("object" === typeof x && null !== x && (x === SuspenseException || "function" === typeof x.then))
                throw x;
              task.replay.pendingTasks--;
              children = getThrownInfo(task.componentStack);
              var boundary = task.blockedBoundary, error = x;
              children = logRecoverableError(request, error, children);
              abortRemainingReplayNodes(
                request,
                boundary,
                childIndex,
                node,
                error,
                children
              );
            }
            task.replay = replay;
            replayNodes.splice(j, 1);
            break;
          }
        }
        task.keyPath = prevKeyPath;
        return;
      }
      replay = task.treeContext;
      replayNodes = children.length;
      if (null !== task.replay && (j = task.replay.slots, null !== j && "object" === typeof j)) {
        for (childIndex = 0; childIndex < replayNodes; childIndex++)
          node = children[childIndex], task.treeContext = pushTreeContext(replay, replayNodes, childIndex), boundary = j[childIndex], "number" === typeof boundary ? (resumeNode(request, task, boundary, node, childIndex), delete j[childIndex]) : renderNode(request, task, node, childIndex);
        task.treeContext = replay;
        task.keyPath = prevKeyPath;
        return;
      }
      for (j = 0; j < replayNodes; j++)
        childIndex = children[j], task.treeContext = pushTreeContext(replay, replayNodes, j), renderNode(request, task, childIndex, j);
      task.treeContext = replay;
      task.keyPath = prevKeyPath;
    }
    function trackPostponedBoundary(request, trackedPostpones, boundary) {
      boundary.status = 5;
      boundary.rootSegmentID = request.nextSegmentId++;
      request = boundary.trackedContentKeyPath;
      if (null === request) throw Error(formatProdErrorMessage(486));
      var fallbackReplayNode = boundary.trackedFallbackNode, children = [], boundaryNode = trackedPostpones.workingMap.get(request);
      if (void 0 === boundaryNode)
        return boundary = [
          request[1],
          request[2],
          children,
          null,
          fallbackReplayNode,
          boundary.rootSegmentID
        ], trackedPostpones.workingMap.set(request, boundary), addToReplayParent(boundary, request[0], trackedPostpones), boundary;
      boundaryNode[4] = fallbackReplayNode;
      boundaryNode[5] = boundary.rootSegmentID;
      return boundaryNode;
    }
    function trackPostpone(request, trackedPostpones, task, segment) {
      segment.status = 5;
      var keyPath = task.keyPath, boundary = task.blockedBoundary;
      if (null === boundary)
        segment.id = request.nextSegmentId++, trackedPostpones.rootSlots = segment.id, null !== request.completedRootSegment && (request.completedRootSegment.status = 5);
      else {
        if (null !== boundary && 0 === boundary.status) {
          var boundaryNode = trackPostponedBoundary(
            request,
            trackedPostpones,
            boundary
          );
          if (boundary.trackedContentKeyPath === keyPath && -1 === task.childIndex) {
            -1 === segment.id && (segment.id = segment.parentFlushed ? boundary.rootSegmentID : request.nextSegmentId++);
            boundaryNode[3] = segment.id;
            return;
          }
        }
        -1 === segment.id && (segment.id = segment.parentFlushed && null !== boundary ? boundary.rootSegmentID : request.nextSegmentId++);
        if (-1 === task.childIndex)
          null === keyPath ? trackedPostpones.rootSlots = segment.id : (task = trackedPostpones.workingMap.get(keyPath), void 0 === task ? (task = [keyPath[1], keyPath[2], [], segment.id], addToReplayParent(task, keyPath[0], trackedPostpones)) : task[3] = segment.id);
        else {
          if (null === keyPath)
            if (request = trackedPostpones.rootSlots, null === request)
              request = trackedPostpones.rootSlots = {};
            else {
              if ("number" === typeof request)
                throw Error(formatProdErrorMessage(491));
            }
          else if (boundary = trackedPostpones.workingMap, boundaryNode = boundary.get(keyPath), void 0 === boundaryNode)
            request = {}, boundaryNode = [keyPath[1], keyPath[2], [], request], boundary.set(keyPath, boundaryNode), addToReplayParent(boundaryNode, keyPath[0], trackedPostpones);
          else if (request = boundaryNode[3], null === request)
            request = boundaryNode[3] = {};
          else if ("number" === typeof request)
            throw Error(formatProdErrorMessage(491));
          request[task.childIndex] = segment.id;
        }
      }
    }
    function untrackBoundary(request, boundary) {
      request = request.trackedPostpones;
      null !== request && (boundary = boundary.trackedContentKeyPath, null !== boundary && (boundary = request.workingMap.get(boundary), void 0 !== boundary && (boundary.length = 4, boundary[2] = [], boundary[3] = null)));
    }
    function spawnNewSuspendedReplayTask(request, task, thenableState2) {
      return createReplayTask(
        request,
        thenableState2,
        task.replay,
        task.node,
        task.childIndex,
        task.blockedBoundary,
        task.hoistableState,
        task.abortSet,
        task.keyPath,
        task.formatContext,
        task.context,
        task.treeContext,
        task.row,
        task.componentStack
      );
    }
    function spawnNewSuspendedRenderTask(request, task, thenableState2) {
      var segment = task.blockedSegment, newSegment = createPendingSegment(
        request,
        segment.chunks.length,
        null,
        task.formatContext,
        segment.lastPushedText,
        true
      );
      segment.children.push(newSegment);
      segment.lastPushedText = false;
      return createRenderTask(
        request,
        thenableState2,
        task.node,
        task.childIndex,
        task.blockedBoundary,
        newSegment,
        task.blockedPreamble,
        task.hoistableState,
        task.abortSet,
        task.keyPath,
        task.formatContext,
        task.context,
        task.treeContext,
        task.row,
        task.componentStack
      );
    }
    function renderNode(request, task, node, childIndex) {
      var previousFormatContext = task.formatContext, previousContext = task.context, previousKeyPath = task.keyPath, previousTreeContext = task.treeContext, previousComponentStack = task.componentStack, segment = task.blockedSegment;
      if (null === segment) {
        segment = task.replay;
        try {
          return renderNodeDestructive(request, task, node, childIndex);
        } catch (thrownValue) {
          if (resetHooksState(), node = thrownValue === SuspenseException ? getSuspendedThenable() : thrownValue, 12 !== request.status && "object" === typeof node && null !== node) {
            if ("function" === typeof node.then) {
              childIndex = thrownValue === SuspenseException ? getThenableStateAfterSuspending() : null;
              request = spawnNewSuspendedReplayTask(request, task, childIndex).ping;
              node.then(request, request);
              task.formatContext = previousFormatContext;
              task.context = previousContext;
              task.keyPath = previousKeyPath;
              task.treeContext = previousTreeContext;
              task.componentStack = previousComponentStack;
              task.replay = segment;
              switchContext(previousContext);
              return;
            }
            if ("Maximum call stack size exceeded" === node.message) {
              node = thrownValue === SuspenseException ? getThenableStateAfterSuspending() : null;
              node = spawnNewSuspendedReplayTask(request, task, node);
              request.pingedTasks.push(node);
              task.formatContext = previousFormatContext;
              task.context = previousContext;
              task.keyPath = previousKeyPath;
              task.treeContext = previousTreeContext;
              task.componentStack = previousComponentStack;
              task.replay = segment;
              switchContext(previousContext);
              return;
            }
          }
        }
      } else {
        var childrenLength = segment.children.length, chunkLength = segment.chunks.length;
        try {
          return renderNodeDestructive(request, task, node, childIndex);
        } catch (thrownValue$63) {
          if (resetHooksState(), segment.children.length = childrenLength, segment.chunks.length = chunkLength, node = thrownValue$63 === SuspenseException ? getSuspendedThenable() : thrownValue$63, 12 !== request.status && "object" === typeof node && null !== node) {
            if ("function" === typeof node.then) {
              segment = node;
              node = thrownValue$63 === SuspenseException ? getThenableStateAfterSuspending() : null;
              request = spawnNewSuspendedRenderTask(request, task, node).ping;
              segment.then(request, request);
              task.formatContext = previousFormatContext;
              task.context = previousContext;
              task.keyPath = previousKeyPath;
              task.treeContext = previousTreeContext;
              task.componentStack = previousComponentStack;
              switchContext(previousContext);
              return;
            }
            if ("Maximum call stack size exceeded" === node.message) {
              segment = thrownValue$63 === SuspenseException ? getThenableStateAfterSuspending() : null;
              segment = spawnNewSuspendedRenderTask(request, task, segment);
              request.pingedTasks.push(segment);
              task.formatContext = previousFormatContext;
              task.context = previousContext;
              task.keyPath = previousKeyPath;
              task.treeContext = previousTreeContext;
              task.componentStack = previousComponentStack;
              switchContext(previousContext);
              return;
            }
          }
        }
      }
      task.formatContext = previousFormatContext;
      task.context = previousContext;
      task.keyPath = previousKeyPath;
      task.treeContext = previousTreeContext;
      switchContext(previousContext);
      throw node;
    }
    function abortTaskSoft(task) {
      var boundary = task.blockedBoundary, segment = task.blockedSegment;
      null !== segment && (segment.status = 3, finishedTask(this, boundary, task.row, segment));
    }
    function abortRemainingReplayNodes(request$jscomp$0, boundary, nodes, slots, error, errorDigest$jscomp$0) {
      for (var i = 0; i < nodes.length; i++) {
        var node = nodes[i];
        if (4 === node.length)
          abortRemainingReplayNodes(
            request$jscomp$0,
            boundary,
            node[2],
            node[3],
            error,
            errorDigest$jscomp$0
          );
        else {
          node = node[5];
          var request = request$jscomp$0, errorDigest = errorDigest$jscomp$0, resumedBoundary = createSuspenseBoundary(
            request,
            null,
            /* @__PURE__ */ new Set(),
            null,
            null
          );
          resumedBoundary.parentFlushed = true;
          resumedBoundary.rootSegmentID = node;
          resumedBoundary.status = 4;
          resumedBoundary.errorDigest = errorDigest;
          resumedBoundary.parentFlushed && request.clientRenderedBoundaries.push(resumedBoundary);
        }
      }
      nodes.length = 0;
      if (null !== slots) {
        if (null === boundary) throw Error(formatProdErrorMessage(487));
        4 !== boundary.status && (boundary.status = 4, boundary.errorDigest = errorDigest$jscomp$0, boundary.parentFlushed && request$jscomp$0.clientRenderedBoundaries.push(boundary));
        if ("object" === typeof slots) for (var index in slots) delete slots[index];
      }
    }
    function abortTask(task, request, error) {
      var boundary = task.blockedBoundary, segment = task.blockedSegment;
      if (null !== segment) {
        if (6 === segment.status) return;
        segment.status = 3;
      }
      var errorInfo = getThrownInfo(task.componentStack);
      if (null === boundary) {
        if (13 !== request.status && 14 !== request.status) {
          boundary = task.replay;
          if (null === boundary) {
            null !== request.trackedPostpones && null !== segment ? (boundary = request.trackedPostpones, logRecoverableError(request, error, errorInfo), trackPostpone(request, boundary, task, segment), finishedTask(request, null, task.row, segment)) : (logRecoverableError(request, error, errorInfo), fatalError(request, error));
            return;
          }
          boundary.pendingTasks--;
          0 === boundary.pendingTasks && 0 < boundary.nodes.length && (segment = logRecoverableError(request, error, errorInfo), abortRemainingReplayNodes(
            request,
            null,
            boundary.nodes,
            boundary.slots,
            error,
            segment
          ));
          request.pendingRootTasks--;
          0 === request.pendingRootTasks && completeShell(request);
        }
      } else {
        var trackedPostpones$64 = request.trackedPostpones;
        if (4 !== boundary.status) {
          if (null !== trackedPostpones$64 && null !== segment)
            return logRecoverableError(request, error, errorInfo), trackPostpone(request, trackedPostpones$64, task, segment), boundary.fallbackAbortableTasks.forEach(function(fallbackTask) {
              return abortTask(fallbackTask, request, error);
            }), boundary.fallbackAbortableTasks.clear(), finishedTask(request, boundary, task.row, segment);
          boundary.status = 4;
          segment = logRecoverableError(request, error, errorInfo);
          boundary.status = 4;
          boundary.errorDigest = segment;
          untrackBoundary(request, boundary);
          boundary.parentFlushed && request.clientRenderedBoundaries.push(boundary);
        }
        boundary.pendingTasks--;
        segment = boundary.row;
        null !== segment && 0 === --segment.pendingTasks && finishSuspenseListRow(request, segment);
        boundary.fallbackAbortableTasks.forEach(function(fallbackTask) {
          return abortTask(fallbackTask, request, error);
        });
        boundary.fallbackAbortableTasks.clear();
      }
      task = task.row;
      null !== task && 0 === --task.pendingTasks && finishSuspenseListRow(request, task);
      request.allPendingTasks--;
      0 === request.allPendingTasks && completeAll(request);
    }
    function safelyEmitEarlyPreloads(request, shellComplete) {
      try {
        var renderState = request.renderState, onHeaders = renderState.onHeaders;
        if (onHeaders) {
          var headers = renderState.headers;
          if (headers) {
            renderState.headers = null;
            var linkHeader = headers.preconnects;
            headers.fontPreloads && (linkHeader && (linkHeader += ", "), linkHeader += headers.fontPreloads);
            headers.highImagePreloads && (linkHeader && (linkHeader += ", "), linkHeader += headers.highImagePreloads);
            if (!shellComplete) {
              var queueIter = renderState.styles.values(), queueStep = queueIter.next();
              b: for (; 0 < headers.remainingCapacity && !queueStep.done; queueStep = queueIter.next())
                for (var sheetIter = queueStep.value.sheets.values(), sheetStep = sheetIter.next(); 0 < headers.remainingCapacity && !sheetStep.done; sheetStep = sheetIter.next()) {
                  var sheet = sheetStep.value, props = sheet.props, key = props.href, props$jscomp$0 = sheet.props, header = getPreloadAsHeader(props$jscomp$0.href, "style", {
                    crossOrigin: props$jscomp$0.crossOrigin,
                    integrity: props$jscomp$0.integrity,
                    nonce: props$jscomp$0.nonce,
                    type: props$jscomp$0.type,
                    fetchPriority: props$jscomp$0.fetchPriority,
                    referrerPolicy: props$jscomp$0.referrerPolicy,
                    media: props$jscomp$0.media
                  });
                  if (0 <= (headers.remainingCapacity -= header.length + 2))
                    renderState.resets.style[key] = PRELOAD_NO_CREDS, linkHeader && (linkHeader += ", "), linkHeader += header, renderState.resets.style[key] = "string" === typeof props.crossOrigin || "string" === typeof props.integrity ? [props.crossOrigin, props.integrity] : PRELOAD_NO_CREDS;
                  else break b;
                }
            }
            linkHeader ? onHeaders({ Link: linkHeader }) : onHeaders({});
          }
        }
      } catch (error) {
        logRecoverableError(request, error, {});
      }
    }
    function completeShell(request) {
      null === request.trackedPostpones && safelyEmitEarlyPreloads(request, true);
      null === request.trackedPostpones && preparePreamble(request);
      request.onShellError = noop;
      request = request.onShellReady;
      request();
    }
    function completeAll(request) {
      safelyEmitEarlyPreloads(
        request,
        null === request.trackedPostpones ? true : null === request.completedRootSegment || 5 !== request.completedRootSegment.status
      );
      preparePreamble(request);
      request = request.onAllReady;
      request();
    }
    function queueCompletedSegment(boundary, segment) {
      if (0 === segment.chunks.length && 1 === segment.children.length && null === segment.children[0].boundary && -1 === segment.children[0].id) {
        var childSegment = segment.children[0];
        childSegment.id = segment.id;
        childSegment.parentFlushed = true;
        1 !== childSegment.status && 3 !== childSegment.status && 4 !== childSegment.status || queueCompletedSegment(boundary, childSegment);
      } else boundary.completedSegments.push(segment);
    }
    function finishedSegment(request, boundary, segment) {
      if (null !== byteLengthOfChunk) {
        segment = segment.chunks;
        for (var segmentByteSize = 0, i = 0; i < segment.length; i++)
          segmentByteSize += segment[i].byteLength;
        null === boundary ? request.byteSize += segmentByteSize : boundary.byteSize += segmentByteSize;
      }
    }
    function finishedTask(request, boundary, row, segment) {
      null !== row && (0 === --row.pendingTasks ? finishSuspenseListRow(request, row) : row.together && tryToResolveTogetherRow(request, row));
      request.allPendingTasks--;
      if (null === boundary) {
        if (null !== segment && segment.parentFlushed) {
          if (null !== request.completedRootSegment)
            throw Error(formatProdErrorMessage(389));
          request.completedRootSegment = segment;
        }
        request.pendingRootTasks--;
        0 === request.pendingRootTasks && completeShell(request);
      } else if (boundary.pendingTasks--, 4 !== boundary.status)
        if (0 === boundary.pendingTasks)
          if (0 === boundary.status && (boundary.status = 1), null !== segment && segment.parentFlushed && (1 === segment.status || 3 === segment.status) && queueCompletedSegment(boundary, segment), boundary.parentFlushed && request.completedBoundaries.push(boundary), 1 === boundary.status)
            row = boundary.row, null !== row && hoistHoistables(row.hoistables, boundary.contentState), isEligibleForOutlining(request, boundary) || (boundary.fallbackAbortableTasks.forEach(abortTaskSoft, request), boundary.fallbackAbortableTasks.clear(), null !== row && 0 === --row.pendingTasks && finishSuspenseListRow(request, row)), 0 === request.pendingRootTasks && null === request.trackedPostpones && null !== boundary.contentPreamble && preparePreamble(request);
          else {
            if (5 === boundary.status && (boundary = boundary.row, null !== boundary)) {
              if (null !== request.trackedPostpones) {
                row = request.trackedPostpones;
                var postponedRow = boundary.next;
                if (null !== postponedRow && (segment = postponedRow.boundaries, null !== segment))
                  for (postponedRow.boundaries = null, postponedRow = 0; postponedRow < segment.length; postponedRow++) {
                    var postponedBoundary = segment[postponedRow];
                    trackPostponedBoundary(request, row, postponedBoundary);
                    finishedTask(request, postponedBoundary, null, null);
                  }
              }
              0 === --boundary.pendingTasks && finishSuspenseListRow(request, boundary);
            }
          }
        else
          null === segment || !segment.parentFlushed || 1 !== segment.status && 3 !== segment.status || (queueCompletedSegment(boundary, segment), 1 === boundary.completedSegments.length && boundary.parentFlushed && request.partialBoundaries.push(boundary)), boundary = boundary.row, null !== boundary && boundary.together && tryToResolveTogetherRow(request, boundary);
      0 === request.allPendingTasks && completeAll(request);
    }
    function performWork(request$jscomp$2) {
      if (14 !== request$jscomp$2.status && 13 !== request$jscomp$2.status) {
        var prevContext = currentActiveSnapshot, prevDispatcher = ReactSharedInternals.H;
        ReactSharedInternals.H = HooksDispatcher;
        var prevAsyncDispatcher = ReactSharedInternals.A;
        ReactSharedInternals.A = DefaultAsyncDispatcher;
        var prevRequest = currentRequest;
        currentRequest = request$jscomp$2;
        var prevResumableState = currentResumableState;
        currentResumableState = request$jscomp$2.resumableState;
        try {
          var pingedTasks = request$jscomp$2.pingedTasks, i;
          for (i = 0; i < pingedTasks.length; i++) {
            var task = pingedTasks[i], request = request$jscomp$2, segment = task.blockedSegment;
            if (null === segment) {
              var request$jscomp$0 = request;
              if (0 !== task.replay.pendingTasks) {
                switchContext(task.context);
                try {
                  "number" === typeof task.replay.slots ? resumeNode(
                    request$jscomp$0,
                    task,
                    task.replay.slots,
                    task.node,
                    task.childIndex
                  ) : retryNode(request$jscomp$0, task);
                  if (1 === task.replay.pendingTasks && 0 < task.replay.nodes.length)
                    throw Error(formatProdErrorMessage(488));
                  task.replay.pendingTasks--;
                  task.abortSet.delete(task);
                  finishedTask(
                    request$jscomp$0,
                    task.blockedBoundary,
                    task.row,
                    null
                  );
                } catch (thrownValue) {
                  resetHooksState();
                  var x = thrownValue === SuspenseException ? getSuspendedThenable() : thrownValue;
                  if ("object" === typeof x && null !== x && "function" === typeof x.then) {
                    var ping = task.ping;
                    x.then(ping, ping);
                    task.thenableState = thrownValue === SuspenseException ? getThenableStateAfterSuspending() : null;
                  } else {
                    task.replay.pendingTasks--;
                    task.abortSet.delete(task);
                    var errorInfo = getThrownInfo(task.componentStack);
                    request = void 0;
                    var request$jscomp$1 = request$jscomp$0, boundary = task.blockedBoundary, error$jscomp$0 = 12 === request$jscomp$0.status ? request$jscomp$0.fatalError : x, replayNodes = task.replay.nodes, resumeSlots = task.replay.slots;
                    request = logRecoverableError(
                      request$jscomp$1,
                      error$jscomp$0,
                      errorInfo
                    );
                    abortRemainingReplayNodes(
                      request$jscomp$1,
                      boundary,
                      replayNodes,
                      resumeSlots,
                      error$jscomp$0,
                      request
                    );
                    request$jscomp$0.pendingRootTasks--;
                    0 === request$jscomp$0.pendingRootTasks && completeShell(request$jscomp$0);
                    request$jscomp$0.allPendingTasks--;
                    0 === request$jscomp$0.allPendingTasks && completeAll(request$jscomp$0);
                  }
                } finally {
                }
              }
            } else if (request$jscomp$0 = void 0, request$jscomp$1 = segment, 0 === request$jscomp$1.status) {
              request$jscomp$1.status = 6;
              switchContext(task.context);
              var childrenLength = request$jscomp$1.children.length, chunkLength = request$jscomp$1.chunks.length;
              try {
                retryNode(request, task), request$jscomp$1.lastPushedText && request$jscomp$1.textEmbedded && request$jscomp$1.chunks.push(textSeparator), task.abortSet.delete(task), request$jscomp$1.status = 1, finishedSegment(request, task.blockedBoundary, request$jscomp$1), finishedTask(
                  request,
                  task.blockedBoundary,
                  task.row,
                  request$jscomp$1
                );
              } catch (thrownValue) {
                resetHooksState();
                request$jscomp$1.children.length = childrenLength;
                request$jscomp$1.chunks.length = chunkLength;
                var x$jscomp$0 = thrownValue === SuspenseException ? getSuspendedThenable() : 12 === request.status ? request.fatalError : thrownValue;
                if (12 === request.status && null !== request.trackedPostpones) {
                  var trackedPostpones = request.trackedPostpones, thrownInfo = getThrownInfo(task.componentStack);
                  task.abortSet.delete(task);
                  logRecoverableError(request, x$jscomp$0, thrownInfo);
                  trackPostpone(request, trackedPostpones, task, request$jscomp$1);
                  finishedTask(
                    request,
                    task.blockedBoundary,
                    task.row,
                    request$jscomp$1
                  );
                } else if ("object" === typeof x$jscomp$0 && null !== x$jscomp$0 && "function" === typeof x$jscomp$0.then) {
                  request$jscomp$1.status = 0;
                  task.thenableState = thrownValue === SuspenseException ? getThenableStateAfterSuspending() : null;
                  var ping$jscomp$0 = task.ping;
                  x$jscomp$0.then(ping$jscomp$0, ping$jscomp$0);
                } else {
                  var errorInfo$jscomp$0 = getThrownInfo(task.componentStack);
                  task.abortSet.delete(task);
                  request$jscomp$1.status = 4;
                  var boundary$jscomp$0 = task.blockedBoundary, row = task.row;
                  null !== row && 0 === --row.pendingTasks && finishSuspenseListRow(request, row);
                  request.allPendingTasks--;
                  request$jscomp$0 = logRecoverableError(
                    request,
                    x$jscomp$0,
                    errorInfo$jscomp$0
                  );
                  if (null === boundary$jscomp$0) fatalError(request, x$jscomp$0);
                  else if (boundary$jscomp$0.pendingTasks--, 4 !== boundary$jscomp$0.status) {
                    boundary$jscomp$0.status = 4;
                    boundary$jscomp$0.errorDigest = request$jscomp$0;
                    untrackBoundary(request, boundary$jscomp$0);
                    var boundaryRow = boundary$jscomp$0.row;
                    null !== boundaryRow && 0 === --boundaryRow.pendingTasks && finishSuspenseListRow(request, boundaryRow);
                    boundary$jscomp$0.parentFlushed && request.clientRenderedBoundaries.push(boundary$jscomp$0);
                    0 === request.pendingRootTasks && null === request.trackedPostpones && null !== boundary$jscomp$0.contentPreamble && preparePreamble(request);
                  }
                  0 === request.allPendingTasks && completeAll(request);
                }
              } finally {
              }
            }
          }
          pingedTasks.splice(0, i);
          null !== request$jscomp$2.destination && flushCompletedQueues(request$jscomp$2, request$jscomp$2.destination);
        } catch (error) {
          logRecoverableError(request$jscomp$2, error, {}), fatalError(request$jscomp$2, error);
        } finally {
          currentResumableState = prevResumableState, ReactSharedInternals.H = prevDispatcher, ReactSharedInternals.A = prevAsyncDispatcher, prevDispatcher === HooksDispatcher && switchContext(prevContext), currentRequest = prevRequest;
        }
      }
    }
    function preparePreambleFromSubtree(request, segment, collectedPreambleSegments) {
      segment.preambleChildren.length && collectedPreambleSegments.push(segment.preambleChildren);
      for (var pendingPreambles = false, i = 0; i < segment.children.length; i++)
        pendingPreambles = preparePreambleFromSegment(
          request,
          segment.children[i],
          collectedPreambleSegments
        ) || pendingPreambles;
      return pendingPreambles;
    }
    function preparePreambleFromSegment(request, segment, collectedPreambleSegments) {
      var boundary = segment.boundary;
      if (null === boundary)
        return preparePreambleFromSubtree(
          request,
          segment,
          collectedPreambleSegments
        );
      var preamble = boundary.contentPreamble, fallbackPreamble = boundary.fallbackPreamble;
      if (null === preamble || null === fallbackPreamble) return false;
      switch (boundary.status) {
        case 1:
          hoistPreambleState(request.renderState, preamble);
          request.byteSize += boundary.byteSize;
          segment = boundary.completedSegments[0];
          if (!segment) throw Error(formatProdErrorMessage(391));
          return preparePreambleFromSubtree(
            request,
            segment,
            collectedPreambleSegments
          );
        case 5:
          if (null !== request.trackedPostpones) return true;
        case 4:
          if (1 === segment.status)
            return hoistPreambleState(request.renderState, fallbackPreamble), preparePreambleFromSubtree(
              request,
              segment,
              collectedPreambleSegments
            );
        default:
          return true;
      }
    }
    function preparePreamble(request) {
      if (request.completedRootSegment && null === request.completedPreambleSegments) {
        var collectedPreambleSegments = [], originalRequestByteSize = request.byteSize, hasPendingPreambles = preparePreambleFromSegment(
          request,
          request.completedRootSegment,
          collectedPreambleSegments
        ), preamble = request.renderState.preamble;
        false === hasPendingPreambles || preamble.headChunks && preamble.bodyChunks ? request.completedPreambleSegments = collectedPreambleSegments : request.byteSize = originalRequestByteSize;
      }
    }
    function flushSubtree(request, destination, segment, hoistableState) {
      segment.parentFlushed = true;
      switch (segment.status) {
        case 0:
          segment.id = request.nextSegmentId++;
        case 5:
          return hoistableState = segment.id, segment.lastPushedText = false, segment.textEmbedded = false, request = request.renderState, writeChunk(destination, placeholder1), writeChunk(destination, request.placeholderPrefix), request = stringToChunk(hoistableState.toString(16)), writeChunk(destination, request), writeChunkAndReturn(destination, placeholder2);
        case 1:
          segment.status = 2;
          var r = true, chunks = segment.chunks, chunkIdx = 0;
          segment = segment.children;
          for (var childIdx = 0; childIdx < segment.length; childIdx++) {
            for (r = segment[childIdx]; chunkIdx < r.index; chunkIdx++)
              writeChunk(destination, chunks[chunkIdx]);
            r = flushSegment(request, destination, r, hoistableState);
          }
          for (; chunkIdx < chunks.length - 1; chunkIdx++)
            writeChunk(destination, chunks[chunkIdx]);
          chunkIdx < chunks.length && (r = writeChunkAndReturn(destination, chunks[chunkIdx]));
          return r;
        case 3:
          return true;
        default:
          throw Error(formatProdErrorMessage(390));
      }
    }
    var flushedByteSize = 0;
    function flushSegment(request, destination, segment, hoistableState) {
      var boundary = segment.boundary;
      if (null === boundary)
        return flushSubtree(request, destination, segment, hoistableState);
      boundary.parentFlushed = true;
      if (4 === boundary.status) {
        var row = boundary.row;
        null !== row && 0 === --row.pendingTasks && finishSuspenseListRow(request, row);
        boundary = boundary.errorDigest;
        writeChunkAndReturn(destination, startClientRenderedSuspenseBoundary);
        writeChunk(destination, clientRenderedSuspenseBoundaryError1);
        boundary && (writeChunk(destination, clientRenderedSuspenseBoundaryError1A), writeChunk(destination, stringToChunk(escapeTextForBrowser(boundary))), writeChunk(
          destination,
          clientRenderedSuspenseBoundaryErrorAttrInterstitial
        ));
        writeChunkAndReturn(destination, clientRenderedSuspenseBoundaryError2);
        flushSubtree(request, destination, segment, hoistableState);
      } else if (1 !== boundary.status)
        0 === boundary.status && (boundary.rootSegmentID = request.nextSegmentId++), 0 < boundary.completedSegments.length && request.partialBoundaries.push(boundary), writeStartPendingSuspenseBoundary(
          destination,
          request.renderState,
          boundary.rootSegmentID
        ), hoistableState && hoistHoistables(hoistableState, boundary.fallbackState), flushSubtree(request, destination, segment, hoistableState);
      else if (!flushingPartialBoundaries && isEligibleForOutlining(request, boundary) && (flushedByteSize + boundary.byteSize > request.progressiveChunkSize || hasSuspenseyContent(boundary.contentState)))
        boundary.rootSegmentID = request.nextSegmentId++, request.completedBoundaries.push(boundary), writeStartPendingSuspenseBoundary(
          destination,
          request.renderState,
          boundary.rootSegmentID
        ), flushSubtree(request, destination, segment, hoistableState);
      else {
        flushedByteSize += boundary.byteSize;
        hoistableState && hoistHoistables(hoistableState, boundary.contentState);
        segment = boundary.row;
        null !== segment && isEligibleForOutlining(request, boundary) && 0 === --segment.pendingTasks && finishSuspenseListRow(request, segment);
        writeChunkAndReturn(destination, startCompletedSuspenseBoundary);
        segment = boundary.completedSegments;
        if (1 !== segment.length) throw Error(formatProdErrorMessage(391));
        flushSegment(request, destination, segment[0], hoistableState);
      }
      return writeChunkAndReturn(destination, endSuspenseBoundary);
    }
    function flushSegmentContainer(request, destination, segment, hoistableState) {
      writeStartSegment(
        destination,
        request.renderState,
        segment.parentFormatContext,
        segment.id
      );
      flushSegment(request, destination, segment, hoistableState);
      return writeEndSegment(destination, segment.parentFormatContext);
    }
    function flushCompletedBoundary(request, destination, boundary) {
      flushedByteSize = boundary.byteSize;
      for (var completedSegments = boundary.completedSegments, i = 0; i < completedSegments.length; i++)
        flushPartiallyCompletedSegment(
          request,
          destination,
          boundary,
          completedSegments[i]
        );
      completedSegments.length = 0;
      completedSegments = boundary.row;
      null !== completedSegments && isEligibleForOutlining(request, boundary) && 0 === --completedSegments.pendingTasks && finishSuspenseListRow(request, completedSegments);
      writeHoistablesForBoundary(
        destination,
        boundary.contentState,
        request.renderState
      );
      completedSegments = request.resumableState;
      request = request.renderState;
      i = boundary.rootSegmentID;
      boundary = boundary.contentState;
      var requiresStyleInsertion = request.stylesToHoist;
      request.stylesToHoist = false;
      writeChunk(destination, request.startInlineScript);
      writeChunk(destination, endOfStartTag);
      requiresStyleInsertion ? (0 === (completedSegments.instructions & 4) && (completedSegments.instructions |= 4, writeChunk(destination, clientRenderScriptFunctionOnly)), 0 === (completedSegments.instructions & 2) && (completedSegments.instructions |= 2, writeChunk(destination, completeBoundaryScriptFunctionOnly)), 0 === (completedSegments.instructions & 8) ? (completedSegments.instructions |= 8, writeChunk(destination, completeBoundaryWithStylesScript1FullPartial)) : writeChunk(destination, completeBoundaryWithStylesScript1Partial)) : (0 === (completedSegments.instructions & 2) && (completedSegments.instructions |= 2, writeChunk(destination, completeBoundaryScriptFunctionOnly)), writeChunk(destination, completeBoundaryScript1Partial));
      completedSegments = stringToChunk(i.toString(16));
      writeChunk(destination, request.boundaryPrefix);
      writeChunk(destination, completedSegments);
      writeChunk(destination, completeBoundaryScript2);
      writeChunk(destination, request.segmentPrefix);
      writeChunk(destination, completedSegments);
      requiresStyleInsertion ? (writeChunk(destination, completeBoundaryScript3a), writeStyleResourceDependenciesInJS(destination, boundary)) : writeChunk(destination, completeBoundaryScript3b);
      boundary = writeChunkAndReturn(destination, completeBoundaryScriptEnd);
      return writeBootstrap(destination, request) && boundary;
    }
    function flushPartiallyCompletedSegment(request, destination, boundary, segment) {
      if (2 === segment.status) return true;
      var hoistableState = boundary.contentState, segmentID = segment.id;
      if (-1 === segmentID) {
        if (-1 === (segment.id = boundary.rootSegmentID))
          throw Error(formatProdErrorMessage(392));
        return flushSegmentContainer(request, destination, segment, hoistableState);
      }
      if (segmentID === boundary.rootSegmentID)
        return flushSegmentContainer(request, destination, segment, hoistableState);
      flushSegmentContainer(request, destination, segment, hoistableState);
      boundary = request.resumableState;
      request = request.renderState;
      writeChunk(destination, request.startInlineScript);
      writeChunk(destination, endOfStartTag);
      0 === (boundary.instructions & 1) ? (boundary.instructions |= 1, writeChunk(destination, completeSegmentScript1Full)) : writeChunk(destination, completeSegmentScript1Partial);
      writeChunk(destination, request.segmentPrefix);
      segmentID = stringToChunk(segmentID.toString(16));
      writeChunk(destination, segmentID);
      writeChunk(destination, completeSegmentScript2);
      writeChunk(destination, request.placeholderPrefix);
      writeChunk(destination, segmentID);
      destination = writeChunkAndReturn(destination, completeSegmentScriptEnd);
      return destination;
    }
    var flushingPartialBoundaries = false;
    function flushCompletedQueues(request, destination) {
      currentView = new Uint8Array(2048);
      writtenBytes = 0;
      try {
        if (!(0 < request.pendingRootTasks)) {
          var i, completedRootSegment = request.completedRootSegment;
          if (null !== completedRootSegment) {
            if (5 === completedRootSegment.status) return;
            var completedPreambleSegments = request.completedPreambleSegments;
            if (null === completedPreambleSegments) return;
            flushedByteSize = request.byteSize;
            var resumableState = request.resumableState, renderState = request.renderState, preamble = renderState.preamble, htmlChunks = preamble.htmlChunks, headChunks = preamble.headChunks, i$jscomp$0;
            if (htmlChunks) {
              for (i$jscomp$0 = 0; i$jscomp$0 < htmlChunks.length; i$jscomp$0++)
                writeChunk(destination, htmlChunks[i$jscomp$0]);
              if (headChunks)
                for (i$jscomp$0 = 0; i$jscomp$0 < headChunks.length; i$jscomp$0++)
                  writeChunk(destination, headChunks[i$jscomp$0]);
              else
                writeChunk(destination, startChunkForTag("head")), writeChunk(destination, endOfStartTag);
            } else if (headChunks)
              for (i$jscomp$0 = 0; i$jscomp$0 < headChunks.length; i$jscomp$0++)
                writeChunk(destination, headChunks[i$jscomp$0]);
            var charsetChunks = renderState.charsetChunks;
            for (i$jscomp$0 = 0; i$jscomp$0 < charsetChunks.length; i$jscomp$0++)
              writeChunk(destination, charsetChunks[i$jscomp$0]);
            charsetChunks.length = 0;
            renderState.preconnects.forEach(flushResource, destination);
            renderState.preconnects.clear();
            var viewportChunks = renderState.viewportChunks;
            for (i$jscomp$0 = 0; i$jscomp$0 < viewportChunks.length; i$jscomp$0++)
              writeChunk(destination, viewportChunks[i$jscomp$0]);
            viewportChunks.length = 0;
            renderState.fontPreloads.forEach(flushResource, destination);
            renderState.fontPreloads.clear();
            renderState.highImagePreloads.forEach(flushResource, destination);
            renderState.highImagePreloads.clear();
            currentlyFlushingRenderState = renderState;
            renderState.styles.forEach(flushStylesInPreamble, destination);
            currentlyFlushingRenderState = null;
            var importMapChunks = renderState.importMapChunks;
            for (i$jscomp$0 = 0; i$jscomp$0 < importMapChunks.length; i$jscomp$0++)
              writeChunk(destination, importMapChunks[i$jscomp$0]);
            importMapChunks.length = 0;
            renderState.bootstrapScripts.forEach(flushResource, destination);
            renderState.scripts.forEach(flushResource, destination);
            renderState.scripts.clear();
            renderState.bulkPreloads.forEach(flushResource, destination);
            renderState.bulkPreloads.clear();
            htmlChunks || headChunks || (resumableState.instructions |= 32);
            var hoistableChunks = renderState.hoistableChunks;
            for (i$jscomp$0 = 0; i$jscomp$0 < hoistableChunks.length; i$jscomp$0++)
              writeChunk(destination, hoistableChunks[i$jscomp$0]);
            for (resumableState = hoistableChunks.length = 0; resumableState < completedPreambleSegments.length; resumableState++) {
              var segments = completedPreambleSegments[resumableState];
              for (renderState = 0; renderState < segments.length; renderState++)
                flushSegment(request, destination, segments[renderState], null);
            }
            var preamble$jscomp$0 = request.renderState.preamble, headChunks$jscomp$0 = preamble$jscomp$0.headChunks;
            (preamble$jscomp$0.htmlChunks || headChunks$jscomp$0) && writeChunk(destination, endChunkForTag("head"));
            var bodyChunks = preamble$jscomp$0.bodyChunks;
            if (bodyChunks)
              for (completedPreambleSegments = 0; completedPreambleSegments < bodyChunks.length; completedPreambleSegments++)
                writeChunk(destination, bodyChunks[completedPreambleSegments]);
            flushSegment(request, destination, completedRootSegment, null);
            request.completedRootSegment = null;
            var renderState$jscomp$0 = request.renderState;
            if (0 !== request.allPendingTasks || 0 !== request.clientRenderedBoundaries.length || 0 !== request.completedBoundaries.length || null !== request.trackedPostpones && (0 !== request.trackedPostpones.rootNodes.length || null !== request.trackedPostpones.rootSlots)) {
              var resumableState$jscomp$0 = request.resumableState;
              if (0 === (resumableState$jscomp$0.instructions & 64)) {
                resumableState$jscomp$0.instructions |= 64;
                writeChunk(destination, renderState$jscomp$0.startInlineScript);
                if (0 === (resumableState$jscomp$0.instructions & 32)) {
                  resumableState$jscomp$0.instructions |= 32;
                  var shellId = "_" + resumableState$jscomp$0.idPrefix + "R_";
                  writeChunk(destination, completedShellIdAttributeStart);
                  writeChunk(
                    destination,
                    stringToChunk(escapeTextForBrowser(shellId))
                  );
                  writeChunk(destination, attributeEnd);
                }
                writeChunk(destination, endOfStartTag);
                writeChunk(destination, shellTimeRuntimeScript);
                writeChunkAndReturn(destination, endInlineScript);
              }
            }
            writeBootstrap(destination, renderState$jscomp$0);
          }
          var renderState$jscomp$1 = request.renderState;
          completedRootSegment = 0;
          var viewportChunks$jscomp$0 = renderState$jscomp$1.viewportChunks;
          for (completedRootSegment = 0; completedRootSegment < viewportChunks$jscomp$0.length; completedRootSegment++)
            writeChunk(destination, viewportChunks$jscomp$0[completedRootSegment]);
          viewportChunks$jscomp$0.length = 0;
          renderState$jscomp$1.preconnects.forEach(flushResource, destination);
          renderState$jscomp$1.preconnects.clear();
          renderState$jscomp$1.fontPreloads.forEach(flushResource, destination);
          renderState$jscomp$1.fontPreloads.clear();
          renderState$jscomp$1.highImagePreloads.forEach(
            flushResource,
            destination
          );
          renderState$jscomp$1.highImagePreloads.clear();
          renderState$jscomp$1.styles.forEach(preloadLateStyles, destination);
          renderState$jscomp$1.scripts.forEach(flushResource, destination);
          renderState$jscomp$1.scripts.clear();
          renderState$jscomp$1.bulkPreloads.forEach(flushResource, destination);
          renderState$jscomp$1.bulkPreloads.clear();
          var hoistableChunks$jscomp$0 = renderState$jscomp$1.hoistableChunks;
          for (completedRootSegment = 0; completedRootSegment < hoistableChunks$jscomp$0.length; completedRootSegment++)
            writeChunk(destination, hoistableChunks$jscomp$0[completedRootSegment]);
          hoistableChunks$jscomp$0.length = 0;
          var clientRenderedBoundaries = request.clientRenderedBoundaries;
          for (i = 0; i < clientRenderedBoundaries.length; i++) {
            var boundary = clientRenderedBoundaries[i];
            renderState$jscomp$1 = destination;
            var resumableState$jscomp$1 = request.resumableState, renderState$jscomp$2 = request.renderState, id = boundary.rootSegmentID, errorDigest = boundary.errorDigest;
            writeChunk(
              renderState$jscomp$1,
              renderState$jscomp$2.startInlineScript
            );
            writeChunk(renderState$jscomp$1, endOfStartTag);
            0 === (resumableState$jscomp$1.instructions & 4) ? (resumableState$jscomp$1.instructions |= 4, writeChunk(renderState$jscomp$1, clientRenderScript1Full)) : writeChunk(renderState$jscomp$1, clientRenderScript1Partial);
            writeChunk(renderState$jscomp$1, renderState$jscomp$2.boundaryPrefix);
            writeChunk(renderState$jscomp$1, stringToChunk(id.toString(16)));
            writeChunk(renderState$jscomp$1, clientRenderScript1A);
            errorDigest && (writeChunk(
              renderState$jscomp$1,
              clientRenderErrorScriptArgInterstitial
            ), writeChunk(
              renderState$jscomp$1,
              stringToChunk(
                escapeJSStringsForInstructionScripts(errorDigest || "")
              )
            ));
            var JSCompiler_inline_result = writeChunkAndReturn(
              renderState$jscomp$1,
              clientRenderScriptEnd
            );
            if (!JSCompiler_inline_result) {
              request.destination = null;
              i++;
              clientRenderedBoundaries.splice(0, i);
              return;
            }
          }
          clientRenderedBoundaries.splice(0, i);
          var completedBoundaries = request.completedBoundaries;
          for (i = 0; i < completedBoundaries.length; i++)
            if (!flushCompletedBoundary(request, destination, completedBoundaries[i])) {
              request.destination = null;
              i++;
              completedBoundaries.splice(0, i);
              return;
            }
          completedBoundaries.splice(0, i);
          completeWriting(destination);
          currentView = new Uint8Array(2048);
          writtenBytes = 0;
          flushingPartialBoundaries = true;
          var partialBoundaries = request.partialBoundaries;
          for (i = 0; i < partialBoundaries.length; i++) {
            var boundary$70 = partialBoundaries[i];
            a: {
              clientRenderedBoundaries = request;
              boundary = destination;
              flushedByteSize = boundary$70.byteSize;
              var completedSegments = boundary$70.completedSegments;
              for (JSCompiler_inline_result = 0; JSCompiler_inline_result < completedSegments.length; JSCompiler_inline_result++)
                if (!flushPartiallyCompletedSegment(
                  clientRenderedBoundaries,
                  boundary,
                  boundary$70,
                  completedSegments[JSCompiler_inline_result]
                )) {
                  JSCompiler_inline_result++;
                  completedSegments.splice(0, JSCompiler_inline_result);
                  var JSCompiler_inline_result$jscomp$0 = false;
                  break a;
                }
              completedSegments.splice(0, JSCompiler_inline_result);
              var row = boundary$70.row;
              null !== row && row.together && 1 === boundary$70.pendingTasks && (1 === row.pendingTasks ? unblockSuspenseListRow(
                clientRenderedBoundaries,
                row,
                row.hoistables
              ) : row.pendingTasks--);
              JSCompiler_inline_result$jscomp$0 = writeHoistablesForBoundary(
                boundary,
                boundary$70.contentState,
                clientRenderedBoundaries.renderState
              );
            }
            if (!JSCompiler_inline_result$jscomp$0) {
              request.destination = null;
              i++;
              partialBoundaries.splice(0, i);
              return;
            }
          }
          partialBoundaries.splice(0, i);
          flushingPartialBoundaries = false;
          var largeBoundaries = request.completedBoundaries;
          for (i = 0; i < largeBoundaries.length; i++)
            if (!flushCompletedBoundary(request, destination, largeBoundaries[i])) {
              request.destination = null;
              i++;
              largeBoundaries.splice(0, i);
              return;
            }
          largeBoundaries.splice(0, i);
        }
      } finally {
        flushingPartialBoundaries = false, 0 === request.allPendingTasks && 0 === request.clientRenderedBoundaries.length && 0 === request.completedBoundaries.length ? (request.flushScheduled = false, i = request.resumableState, i.hasBody && writeChunk(destination, endChunkForTag("body")), i.hasHtml && writeChunk(destination, endChunkForTag("html")), completeWriting(destination), request.status = 14, destination.close(), request.destination = null) : completeWriting(destination);
      }
    }
    function startWork(request) {
      request.flushScheduled = null !== request.destination;
      scheduleMicrotask(function() {
        return performWork(request);
      });
      scheduleWork(function() {
        10 === request.status && (request.status = 11);
        null === request.trackedPostpones && safelyEmitEarlyPreloads(request, 0 === request.pendingRootTasks);
      });
    }
    function enqueueFlush(request) {
      false === request.flushScheduled && 0 === request.pingedTasks.length && null !== request.destination && (request.flushScheduled = true, scheduleWork(function() {
        var destination = request.destination;
        destination ? flushCompletedQueues(request, destination) : request.flushScheduled = false;
      }));
    }
    function startFlowing(request, destination) {
      if (13 === request.status)
        request.status = 14, closeWithError(destination, request.fatalError);
      else if (14 !== request.status && null === request.destination) {
        request.destination = destination;
        try {
          flushCompletedQueues(request, destination);
        } catch (error) {
          logRecoverableError(request, error, {}), fatalError(request, error);
        }
      }
    }
    function abort(request, reason) {
      if (11 === request.status || 10 === request.status) request.status = 12;
      try {
        var abortableTasks = request.abortableTasks;
        if (0 < abortableTasks.size) {
          var error = void 0 === reason ? Error(formatProdErrorMessage(432)) : "object" === typeof reason && null !== reason && "function" === typeof reason.then ? Error(formatProdErrorMessage(530)) : reason;
          request.fatalError = error;
          abortableTasks.forEach(function(task) {
            return abortTask(task, request, error);
          });
          abortableTasks.clear();
        }
        null !== request.destination && flushCompletedQueues(request, request.destination);
      } catch (error$72) {
        logRecoverableError(request, error$72, {}), fatalError(request, error$72);
      }
    }
    function addToReplayParent(node, parentKeyPath, trackedPostpones) {
      if (null === parentKeyPath) trackedPostpones.rootNodes.push(node);
      else {
        var workingMap = trackedPostpones.workingMap, parentNode = workingMap.get(parentKeyPath);
        void 0 === parentNode && (parentNode = [parentKeyPath[1], parentKeyPath[2], [], null], workingMap.set(parentKeyPath, parentNode), addToReplayParent(parentNode, parentKeyPath[0], trackedPostpones));
        parentNode[2].push(node);
      }
    }
    function getPostponedState(request) {
      var trackedPostpones = request.trackedPostpones;
      if (null === trackedPostpones || 0 === trackedPostpones.rootNodes.length && null === trackedPostpones.rootSlots)
        return request.trackedPostpones = null;
      if (null === request.completedRootSegment || 5 !== request.completedRootSegment.status && null !== request.completedPreambleSegments) {
        var nextSegmentId = request.nextSegmentId;
        var replaySlots = trackedPostpones.rootSlots;
        var resumableState = request.resumableState;
        resumableState.bootstrapScriptContent = void 0;
        resumableState.bootstrapScripts = void 0;
        resumableState.bootstrapModules = void 0;
      } else {
        nextSegmentId = 0;
        replaySlots = -1;
        resumableState = request.resumableState;
        var renderState = request.renderState;
        resumableState.nextFormID = 0;
        resumableState.hasBody = false;
        resumableState.hasHtml = false;
        resumableState.unknownResources = { font: renderState.resets.font };
        resumableState.dnsResources = renderState.resets.dns;
        resumableState.connectResources = renderState.resets.connect;
        resumableState.imageResources = renderState.resets.image;
        resumableState.styleResources = renderState.resets.style;
        resumableState.scriptResources = {};
        resumableState.moduleUnknownResources = {};
        resumableState.moduleScriptResources = {};
        resumableState.instructions = 0;
      }
      return {
        nextSegmentId,
        rootFormatContext: request.rootFormatContext,
        progressiveChunkSize: request.progressiveChunkSize,
        resumableState: request.resumableState,
        replayNodes: trackedPostpones.rootNodes,
        replaySlots
      };
    }
    function ensureCorrectIsomorphicReactVersion() {
      var isomorphicReactPackageVersion = React2.version;
      if ("19.2.8" !== isomorphicReactPackageVersion)
        throw Error(
          formatProdErrorMessage(
            527,
            isomorphicReactPackageVersion,
            "19.2.8"
          )
        );
    }
    ensureCorrectIsomorphicReactVersion();
    ensureCorrectIsomorphicReactVersion();
    exports.prerender = function(children, options) {
      return new Promise(function(resolve, reject) {
        var onHeaders = options ? options.onHeaders : void 0, onHeadersImpl;
        onHeaders && (onHeadersImpl = function(headersDescriptor) {
          onHeaders(new Headers(headersDescriptor));
        });
        var resources = createResumableState(
          options ? options.identifierPrefix : void 0,
          options ? options.unstable_externalRuntimeSrc : void 0,
          options ? options.bootstrapScriptContent : void 0,
          options ? options.bootstrapScripts : void 0,
          options ? options.bootstrapModules : void 0
        ), request = createPrerenderRequest(
          children,
          resources,
          createRenderState(
            resources,
            void 0,
            options ? options.unstable_externalRuntimeSrc : void 0,
            options ? options.importMap : void 0,
            onHeadersImpl,
            options ? options.maxHeadersLength : void 0
          ),
          createRootFormatContext(options ? options.namespaceURI : void 0),
          options ? options.progressiveChunkSize : void 0,
          options ? options.onError : void 0,
          function() {
            var stream = new ReadableStream(
              {
                type: "bytes",
                pull: function(controller) {
                  startFlowing(request, controller);
                },
                cancel: function(reason) {
                  request.destination = null;
                  abort(request, reason);
                }
              },
              { highWaterMark: 0 }
            );
            stream = { postponed: getPostponedState(request), prelude: stream };
            resolve(stream);
          },
          void 0,
          void 0,
          reject,
          options ? options.onPostpone : void 0
        );
        if (options && options.signal) {
          var signal = options.signal;
          if (signal.aborted) abort(request, signal.reason);
          else {
            var listener = function() {
              abort(request, signal.reason);
              signal.removeEventListener("abort", listener);
            };
            signal.addEventListener("abort", listener);
          }
        }
        startWork(request);
      });
    };
    exports.renderToReadableStream = function(children, options) {
      return new Promise(function(resolve, reject) {
        var onFatalError, onAllReady, allReady = new Promise(function(res, rej) {
          onAllReady = res;
          onFatalError = rej;
        }), onHeaders = options ? options.onHeaders : void 0, onHeadersImpl;
        onHeaders && (onHeadersImpl = function(headersDescriptor) {
          onHeaders(new Headers(headersDescriptor));
        });
        var resumableState = createResumableState(
          options ? options.identifierPrefix : void 0,
          options ? options.unstable_externalRuntimeSrc : void 0,
          options ? options.bootstrapScriptContent : void 0,
          options ? options.bootstrapScripts : void 0,
          options ? options.bootstrapModules : void 0
        ), request = createRequest(
          children,
          resumableState,
          createRenderState(
            resumableState,
            options ? options.nonce : void 0,
            options ? options.unstable_externalRuntimeSrc : void 0,
            options ? options.importMap : void 0,
            onHeadersImpl,
            options ? options.maxHeadersLength : void 0
          ),
          createRootFormatContext(options ? options.namespaceURI : void 0),
          options ? options.progressiveChunkSize : void 0,
          options ? options.onError : void 0,
          onAllReady,
          function() {
            var stream = new ReadableStream(
              {
                type: "bytes",
                pull: function(controller) {
                  startFlowing(request, controller);
                },
                cancel: function(reason) {
                  request.destination = null;
                  abort(request, reason);
                }
              },
              { highWaterMark: 0 }
            );
            stream.allReady = allReady;
            resolve(stream);
          },
          function(error) {
            allReady.catch(function() {
            });
            reject(error);
          },
          onFatalError,
          options ? options.onPostpone : void 0,
          options ? options.formState : void 0
        );
        if (options && options.signal) {
          var signal = options.signal;
          if (signal.aborted) abort(request, signal.reason);
          else {
            var listener = function() {
              abort(request, signal.reason);
              signal.removeEventListener("abort", listener);
            };
            signal.addEventListener("abort", listener);
          }
        }
        startWork(request);
      });
    };
    exports.resume = function(children, postponedState, options) {
      return new Promise(function(resolve, reject) {
        var onFatalError, onAllReady, allReady = new Promise(function(res, rej) {
          onAllReady = res;
          onFatalError = rej;
        }), request = resumeRequest(
          children,
          postponedState,
          createRenderState(
            postponedState.resumableState,
            options ? options.nonce : void 0,
            void 0,
            void 0,
            void 0,
            void 0
          ),
          options ? options.onError : void 0,
          onAllReady,
          function() {
            var stream = new ReadableStream(
              {
                type: "bytes",
                pull: function(controller) {
                  startFlowing(request, controller);
                },
                cancel: function(reason) {
                  request.destination = null;
                  abort(request, reason);
                }
              },
              { highWaterMark: 0 }
            );
            stream.allReady = allReady;
            resolve(stream);
          },
          function(error) {
            allReady.catch(function() {
            });
            reject(error);
          },
          onFatalError,
          options ? options.onPostpone : void 0
        );
        if (options && options.signal) {
          var signal = options.signal;
          if (signal.aborted) abort(request, signal.reason);
          else {
            var listener = function() {
              abort(request, signal.reason);
              signal.removeEventListener("abort", listener);
            };
            signal.addEventListener("abort", listener);
          }
        }
        startWork(request);
      });
    };
    exports.resumeAndPrerender = function(children, postponedState, options) {
      return new Promise(function(resolve, reject) {
        var request = resumeAndPrerenderRequest(
          children,
          postponedState,
          createRenderState(
            postponedState.resumableState,
            void 0,
            void 0,
            void 0,
            void 0,
            void 0
          ),
          options ? options.onError : void 0,
          function() {
            var stream = new ReadableStream(
              {
                type: "bytes",
                pull: function(controller) {
                  startFlowing(request, controller);
                },
                cancel: function(reason) {
                  request.destination = null;
                  abort(request, reason);
                }
              },
              { highWaterMark: 0 }
            );
            stream = { postponed: getPostponedState(request), prelude: stream };
            resolve(stream);
          },
          void 0,
          void 0,
          reject,
          options ? options.onPostpone : void 0
        );
        if (options && options.signal) {
          var signal = options.signal;
          if (signal.aborted) abort(request, signal.reason);
          else {
            var listener = function() {
              abort(request, signal.reason);
              signal.removeEventListener("abort", listener);
            };
            signal.addEventListener("abort", listener);
          }
        }
        startWork(request);
      });
    };
    exports.version = "19.2.8";
  }
});

// node_modules/react-dom/server.browser.js
var require_server_browser = __commonJS({
  "node_modules/react-dom/server.browser.js"(exports) {
    "use strict";
    var l;
    var s;
    if (true) {
      l = require_react_dom_server_legacy_browser_production();
      s = require_react_dom_server_browser_production();
    } else {
      l = null;
      s = null;
    }
    exports.version = l.version;
    exports.renderToString = l.renderToString;
    exports.renderToStaticMarkup = l.renderToStaticMarkup;
    exports.renderToReadableStream = s.renderToReadableStream;
    exports.resume = s.resume;
  }
});

// src/components/visualizer/sonnet/sonnetIcons.ts
var import_react = __toESM(require_react(), 1);
var import_server = __toESM(require_server_browser(), 1);
var LUCIDE_ICON_NAMES = Object.keys(lucide_react_exports).filter((name) => {
  const candidate = lucide_react_exports[name];
  return /^[A-Z]/.test(name) && (typeof candidate === "object" || typeof candidate === "function");
});
var LUCIDE_ICON_NAMES_BY_LOWERCASE = new Map(LUCIDE_ICON_NAMES.map((name) => [name.toLowerCase(), name]));
var resolveSonnetIconNames = (names) => {
  const resolved = [
    ...new Set((names ?? []).map((name) => LUCIDE_ICON_NAMES_BY_LOWERCASE.get(name.toLowerCase())).filter(Boolean))
  ];
  return resolved.length > 0 ? resolved : ["Flower"];
};
var buildSonnetIconParticleIndices = (iconCount, particleCount, seed) => {
  const safeIconCount = Math.max(0, Math.floor(iconCount));
  const safeParticleCount = Math.max(0, Math.floor(particleCount));
  if (safeIconCount === 0) {
    return Array.from({ length: safeParticleCount }, () => null);
  }
  const iconParticleCount = Math.min(
    safeParticleCount,
    Math.max(Math.ceil(safeParticleCount / 4), safeIconCount)
  );
  let emittedIconCount = 0;
  return Array.from({ length: safeParticleCount }, (_, index) => {
    const previousBand = Math.floor(index * iconParticleCount / safeParticleCount);
    const currentBand = Math.floor((index + 1) * iconParticleCount / safeParticleCount);
    if (currentBand === previousBand) {
      return null;
    }
    const iconIndex = ((seed + emittedIconCount) % safeIconCount + safeIconCount) % safeIconCount;
    emittedIconCount += 1;
    return iconIndex;
  });
};
var resolveSonnetIconEntryPhase = (index, iconCount) => {
  const safeCount = Math.max(0, Math.floor(iconCount));
  if (safeCount <= 1) return 0.12;
  const safeIndex = Math.min(safeCount - 1, Math.max(0, Math.floor(index)));
  return 0.04 + safeIndex / (safeCount - 1) * 0.82;
};
var resolveSonnetIconEntryDuration = (sceneDuration, preferredDuration) => {
  const safeSceneDuration = Math.max(0.01, sceneDuration);
  return Math.min(
    Math.max(0.01, preferredDuration),
    Math.max(0.08, safeSceneDuration * 0.18),
    safeSceneDuration
  );
};
var resolveSonnetIconEntryDelay = (entryPhase, sceneDuration, entryDuration) => Math.min(1, Math.max(0, entryPhase)) * Math.max(0, sceneDuration - entryDuration);
var buildSonnetIconTextureKey = (name, color, strokeWidth, size, resolution) => `${name}|${color}|${strokeWidth}|${size}|${resolution}`;
var buildSonnetIconDataUrl = (name, color, strokeWidth, size) => {
  const Icon = lucide_react_exports[name];
  if (!Icon) return null;
  const markup = (0, import_server.renderToStaticMarkup)(import_react.default.createElement(Icon, {
    size,
    color,
    strokeWidth,
    absoluteStrokeWidth: true,
    fill: "none",
    xmlns: "http://www.w3.org/2000/svg"
  }));
  return `data:image/svg+xml;charset=utf-8,${encodeURIComponent(markup)}`;
};

// src/components/visualizer/sonnet/sonnetMotion.ts
var clamp01 = (value) => Math.min(1, Math.max(0, value));
var cubicCoordinate = (point1, point2, time) => {
  const inverse = 1 - time;
  return 3 * inverse * inverse * time * point1 + 3 * inverse * time * time * point2 + time * time * time;
};
var resolveCubicBezier = (x1, y1, x2, y2, value) => {
  const target = clamp01(value);
  if (target === 0 || target === 1) return target;
  let low = 0;
  let high = 1;
  let parameter = target;
  for (let iteration = 0; iteration < 12; iteration += 1) {
    const x = cubicCoordinate(x1, x2, parameter);
    if (x < target) low = parameter;
    else high = parameter;
    parameter = (low + high) / 2;
  }
  return cubicCoordinate(y1, y2, parameter);
};
var easeSonnetInOut = (value) => resolveCubicBezier(0.65, 0, 0.35, 1, value);
var easeSonnetExpoOut = (value) => value === 1 ? 1 : 1 - Math.pow(2, -10 * value);
var resolveShotProgress = (shot, time) => clamp01((time - shot.startTime) / Math.max(shot.endTime - shot.startTime, 1e-3));
var resolveSegmentProgress = (startTime, endTime, time) => (
  // 使用更高张力的 ExpoOut 产生“打击感”的单字入场
  easeSonnetExpoOut(clamp01((time - startTime) / Math.max(endTime - startTime, 0.08)))
);
var resolveSonnetSegmentDepth = (role, random = Math.random) => {
  if (role !== "decoration") return 0;
  return random() > 0.5 ? 0.5 + random() * 0.8 : -0.5 - random() * 0.8;
};
var resolveSonnetSegmentNormalOffset = (role, layoutDirection, rotation, fontSize, randomValue) => {
  if (role !== "support") return { x: 0, y: 0 };
  const distance = (Math.min(1, Math.max(0, randomValue)) * 2 - 1) * fontSize * 0.3;
  const normalAngle = rotation + (layoutDirection === "vertical" ? 0 : Math.PI / 2);
  return {
    x: Math.cos(normalAngle) * distance,
    y: Math.sin(normalAngle) * distance
  };
};
var SONNET_CAMERA_SMOOTHING_SAMPLES = [
  { offset: -1, weight: 1 },
  { offset: -0.5, weight: 4 },
  { offset: 0, weight: 6 },
  { offset: 0.5, weight: 4 },
  { offset: 1, weight: 1 }
];
var resolveSonnetSmoothedCameraFocus = (time, startTime, endTime, sampleFocus, smoothingWindow = 0.12, maxBlendDistance = 96) => {
  const safeStart = Math.min(startTime, endTime);
  const safeEnd = Math.max(startTime, endTime);
  const radius = Math.max(0, smoothingWindow);
  if (radius === 0 || safeStart === safeEnd) {
    return sampleFocus(Math.min(safeEnd, Math.max(safeStart, time)));
  }
  const samples = SONNET_CAMERA_SMOOTHING_SAMPLES.map(({ offset, weight }) => {
    const sampleTime = Math.min(safeEnd, Math.max(safeStart, time + offset * radius));
    return { point: sampleFocus(sampleTime), weight };
  });
  const center = samples[2].point;
  const maxDistanceSquared = Math.max(0, maxBlendDistance) ** 2;
  let x = 0;
  let y = 0;
  let totalWeight = 0;
  samples.forEach(({ point, weight }) => {
    const distanceSquared = (point.x - center.x) ** 2 + (point.y - center.y) ** 2;
    if (distanceSquared > maxDistanceSquared) return;
    x += point.x * weight;
    y += point.y * weight;
    totalWeight += weight;
  });
  return { x: x / totalWeight, y: y / totalWeight };
};
var resolveSonnetFocusWeights = (ranges, time, sigma = 0.35) => {
  if (ranges.length === 0) return [];
  const safeSigma = Math.max(1e-3, sigma);
  const logWeights = ranges.map((range) => {
    const startTime = Math.min(range.startTime, range.endTime);
    const endTime = Math.max(range.startTime, range.endTime);
    const distance = time < startTime ? startTime - time : time > endTime ? time - endTime : 0;
    return -(distance * distance) / (2 * safeSigma * safeSigma);
  });
  const maxLogWeight = Math.max(...logWeights);
  const weights = logWeights.map((weight) => Math.exp(weight - maxLogWeight));
  const totalWeight = weights.reduce((total, weight) => total + weight, 0);
  return weights.map((weight) => weight / totalWeight);
};
var resolveShotPathProgress = (kind, progress) => {
  const linear = clamp01(progress);
  if (kind === "tracking-ribbon" || kind === "fragment-collage" || kind === "quiet-tableau" || kind === "poster-blocks") {
    return easeSonnetInOut(linear);
  }
  if (linear < 0.2) return easeSonnetExpoOut(linear / 0.2) * 0.3;
  if (linear < 0.72) return 0.3;
  return 0.3 + easeSonnetInOut((linear - 0.72) / 0.28) * 0.7;
};
var resolveShotMotionFrame = (kind, progress) => {
  const linear = clamp01(progress);
  const eased = resolveShotPathProgress(kind, linear);
  const frames = {
    "editorial-column": {
      x: -0.055 + eased * 0.095,
      y: 0.025 - eased * 0.04,
      scale: 0.98 + eased * 0.07,
      rotation: -6e-3 + eased * 0.01
    },
    "type-impact": {
      x: -0.035 + eased * 0.07,
      y: 0.018 - eased * 0.028,
      scale: 1 + (1 - easeSonnetExpoOut(Math.min(linear / 0.18, 1))) * 0.22 + eased * 0.08,
      rotation: -0.01 + eased * 0.016
    },
    "fragment-collage": {
      x: -0.045 + eased * 0.085,
      y: 0.028 - Math.sin(eased * Math.PI) * 0.055,
      scale: 0.97 + eased * 0.09,
      rotation: -0.014 + eased * 0.028
    },
    "tracking-ribbon": {
      x: -0.16 + eased * 0.28,
      y: 0.05 - eased * 0.085,
      scale: 0.98 + eased * 0.07,
      rotation: 8e-3 - eased * 0.014
    },
    "mask-reveal": {
      x: 0.035 - eased * 0.065,
      y: 0.1 - eased * 0.135,
      scale: 0.96 + eased * 0.12,
      rotation: -6e-3 + eased * 9e-3
    },
    "poster-blocks": {
      x: -0.012 + eased * 0.024,
      y: 8e-3 - eased * 0.016,
      scale: 0.99 + eased * 0.025,
      rotation: -15e-4 + eased * 3e-3
    },
    "quiet-tableau": {
      x: -0.022 + eased * 0.04,
      y: 0.014 - eased * 0.025,
      scale: 1 + eased * 0.028,
      rotation: -2e-3 + eased * 3e-3
    }
  };
  return frames[kind];
};
var resolveTimelineShake = (time, intensity) => {
  if (intensity <= 0) return { x: 0, y: 0, rotation: 0 };
  const shakeX = Math.sin(time * 123.456) * Math.cos(time * 789.123);
  const shakeY = Math.cos(time * 345.678) * Math.sin(time * 901.234);
  const shakeRot = Math.sin(time * 567.89);
  return {
    x: shakeX * 0.02 * intensity,
    y: shakeY * 0.02 * intensity,
    rotation: shakeRot * 5e-3 * intensity
  };
};

// src/components/visualizer/sonnet/sonnetTransitions.ts
var IDLE_SONNET_TRANSITION_FRAME = {
  x: 0,
  y: 0,
  scale: 1,
  rotation: 0,
  alpha: 1,
  blur: 0,
  glitch: 0,
  glitchSeed: 0
};
var resolveBoundaryKind = (seed, boundaryIndex) => {
  const mixed = (seed ^ Math.imul(boundaryIndex + 1, 2654435761)) >>> 0;
  return SONNET_TRANSITION_KINDS[mixed % SONNET_TRANSITION_KINDS.length];
};
var resolveSonnetTransitionEffectFrame = (kind, phase, progress, seed) => {
  const linear = clamp01(progress);
  const eased = easeSonnetInOut(linear);
  const amount = phase === "exit" ? eased : 1 - eased;
  if (kind === "fast-blur") {
    return {
      x: 0,
      y: 0,
      scale: phase === "exit" ? 1 + amount * 0.035 : 1 - amount * 0.035,
      rotation: 0,
      alpha: phase === "exit" ? 1 - amount : 1 - amount * 0.82,
      blur: amount * 14,
      glitch: 0,
      glitchSeed: 0
    };
  }
  if (kind === "mono-glitch") {
    const step = Math.floor(linear * 14);
    return {
      x: 0,
      y: 0,
      scale: 1,
      rotation: 0,
      alpha: phase === "exit" && linear > 0.86 ? 1 - (linear - 0.86) / 0.14 : 1,
      blur: 0,
      glitch: amount,
      glitchSeed: seed * 1e-4 + step * 0.173
    };
  }
  return {
    x: 0,
    y: 0,
    scale: phase === "exit" ? 1 + amount * 0.22 : 1 - amount * 0.18,
    rotation: 0,
    alpha: phase === "exit" ? 1 - amount : 1 - amount * 0.72,
    blur: 0,
    glitch: 0,
    glitchSeed: 0
  };
};
var resolveSonnetExitTransitionFrame = (paragraph, time, enabled, seed) => {
  const transition = paragraph.transitionOut;
  if (!enabled || !transition || time < transition.startTime) return IDLE_SONNET_TRANSITION_FRAME;
  const progress = (time - transition.startTime) / Math.max(transition.endTime - transition.startTime, 1e-3);
  return resolveSonnetTransitionEffectFrame(transition.kind, "exit", progress, seed);
};
var resolveSonnetEnterTransitionFrame = (kind, timeSinceStart, duration, enabled, seed) => {
  if (!enabled || !kind || timeSinceStart < 0 || timeSinceStart > duration) {
    return IDLE_SONNET_TRANSITION_FRAME;
  }
  return resolveSonnetTransitionEffectFrame(kind, "enter", timeSinceStart / Math.max(duration, 1e-3), seed);
};
var resolveSonnetShotTransitionFrame = (shots, activeShotIndex, time, enabled, seed) => {
  if (!enabled || shots.length < 2) return IDLE_SONNET_TRANSITION_FRAME;
  const current = shots[activeShotIndex];
  if (!current) return IDLE_SONNET_TRANSITION_FRAME;
  if (activeShotIndex > 0) {
    const previous = shots[activeShotIndex - 1];
    const duration2 = Math.min(0.24, Math.max(0.14, (current.startTime - previous.startTime) * 0.18));
    if (time <= current.startTime + duration2) {
      return resolveSonnetEnterTransitionFrame(
        resolveBoundaryKind(seed, activeShotIndex - 1),
        time - current.startTime,
        duration2,
        true,
        seed + activeShotIndex * 97
      );
    }
  }
  const next = shots[activeShotIndex + 1];
  if (!next) return IDLE_SONNET_TRANSITION_FRAME;
  const duration = Math.min(0.24, Math.max(0.14, (next.startTime - current.startTime) * 0.18));
  const transitionStart = next.startTime - duration;
  if (time < transitionStart) return IDLE_SONNET_TRANSITION_FRAME;
  return resolveSonnetTransitionEffectFrame(
    resolveBoundaryKind(seed, activeShotIndex),
    "exit",
    (time - transitionStart) / duration,
    seed + (activeShotIndex + 1) * 97
  );
};

// src/components/visualizer/sonnet/sonnetSpatialMgGeometry.ts
var SONNET_GEO_VARIANT_COUNT = 36;
var resolveSonnetGeoVariant = (seed) => (Math.trunc(seed) % SONNET_GEO_VARIANT_COUNT + SONNET_GEO_VARIANT_COUNT) % SONNET_GEO_VARIANT_COUNT;
var resolveSonnetGeoCycle = (seed) => Math.floor(
  Math.trunc(seed) / SONNET_GEO_VARIANT_COUNT
);
var resolveSonnetGeoSubVariant = (seed, count) => {
  const cycle = resolveSonnetGeoCycle(seed);
  return (cycle % count + count) % count;
};
var resolveSonnetMoleculeVariant = (seed) => resolveSonnetGeoSubVariant(seed, 3);
var resolveSonnetHudRotationQuarterTurns = (seed) => resolveSonnetGeoSubVariant(seed, 4);
var tracePolygon = (target, points) => {
  target.moveTo(points[0][0], points[0][1]);
  for (let index = 1; index < points.length; index += 1) {
    target.lineTo(points[index][0], points[index][1]);
  }
  return target.lineTo(points[0][0], points[0][1]);
};
var drawFace = (target, points, color, fillAlpha) => {
  tracePolygon(target, points).fill({ color, alpha: fillAlpha });
};
var drawSonnetSolidCuboid = (target, x, y, width, height, depthX, depthY, color, alpha) => {
  const left = x - width / 2;
  const right = x + width / 2;
  const top = y - height / 2;
  const bottom = y + height / 2;
  const front = [[left, top], [right, top], [right, bottom], [left, bottom]];
  const topFace = [[left, top], [left + depthX, top + depthY], [right + depthX, top + depthY], [right, top]];
  const sideFace = [[right, top], [right + depthX, top + depthY], [right + depthX, bottom + depthY], [right, bottom]];
  drawFace(target, topFace, color, alpha * 0.42);
  drawFace(target, sideFace, color, alpha * 0.68);
  drawFace(target, front, color, alpha * 0.24);
};
var drawSonnetExtrudedPolygon = (target, front, depthX, depthY, color, alpha) => {
  const back = front.map(([x, y]) => [x + depthX, y + depthY]);
  for (let index = 0; index < front.length; index += 1) {
    const next = (index + 1) % front.length;
    drawFace(
      target,
      [front[index], back[index], back[next], front[next]],
      color,
      alpha * (0.34 + index % 3 * 0.12)
    );
  }
  drawFace(target, front, color, alpha * 0.22);
};
var drawSonnetTriangularPrism = (target, x, y, width, height, depthX, depthY, color, alpha) => drawSonnetExtrudedPolygon(target, [
  [x, y - height / 2],
  [x + width / 2, y + height / 2],
  [x - width / 2, y + height / 2]
], depthX, depthY, color, alpha);
var drawSonnetHexagonalPrism = (target, x, y, width, height, depthX, depthY, color, alpha) => drawSonnetExtrudedPolygon(target, [
  [x - width * 0.25, y - height / 2],
  [x + width * 0.25, y - height / 2],
  [x + width / 2, y],
  [x + width * 0.25, y + height / 2],
  [x - width * 0.25, y + height / 2],
  [x - width / 2, y]
], depthX, depthY, color, alpha);
var drawSonnetTrapezoidPrism = (target, x, y, topWidth, bottomWidth, height, depthX, depthY, color, alpha) => drawSonnetExtrudedPolygon(target, [
  [x - topWidth / 2, y - height / 2],
  [x + topWidth / 2, y - height / 2],
  [x + bottomWidth / 2, y + height / 2],
  [x - bottomWidth / 2, y + height / 2]
], depthX, depthY, color, alpha);

// src/components/visualizer/sonnet/sonnetThemedShotMgPrimitives.ts
var tracePolygon2 = (target, points) => {
  points.forEach(([x, y], index) => {
    if (index === 0) target.moveTo(x, y);
    else target.lineTo(x, y);
  });
  return target.lineTo(points[0][0], points[0][1]);
};
var fillPolygon = (target, points, color, alpha) => tracePolygon2(target, points).fill({ color, alpha });
var strokePolygon = (target, points, color, alpha, width = 1.5) => tracePolygon2(target, points).stroke({ color, alpha, width });
var drawLeaf = (target, x, y, length, width, angle, color, fillAlpha) => {
  const dx = Math.cos(angle);
  const dy = Math.sin(angle);
  const nx = -dy;
  const ny = dx;
  const tipX = x + dx * length;
  const tipY = y + dy * length;
  target.moveTo(x, y).quadraticCurveTo(x + dx * length * 0.45 + nx * width, y + dy * length * 0.45 + ny * width, tipX, tipY).quadraticCurveTo(x + dx * length * 0.45 - nx * width, y + dy * length * 0.45 - ny * width, x, y).fill({ color, alpha: fillAlpha });
  target.moveTo(x, y).quadraticCurveTo(x + dx * length * 0.45 + nx * width, y + dy * length * 0.45 + ny * width, tipX, tipY).quadraticCurveTo(x + dx * length * 0.45 - nx * width, y + dy * length * 0.45 - ny * width, x, y).stroke({ color, alpha: Math.min(0.8, fillAlpha * 3.2), width: 1.5 });
  target.moveTo(x, y).lineTo(tipX, tipY).stroke({ color, alpha: 0.32, width: 1 });
};
var drawPetal = (target, cx, cy, length, width, angle, color, fillAlpha) => drawLeaf(target, cx, cy, length, width, angle, color, fillAlpha);

// src/components/visualizer/sonnet/sonnetShotMgViewport.ts
var resolveSonnetShotMgBleed = (width, height, radius) => ({
  x: Math.max(radius * 0.92, width * 0.64),
  y: Math.max(radius * 0.92, height * 0.64)
});

// src/components/visualizer/sonnet/sonnetShotMgArchitecture.ts
var drawSonnetGreenhouseMg = ({ target, radius, width, height, seed, primary, secondary }) => {
  const direction = seed % 2 === 0 ? 1 : -1;
  const bleed = resolveSonnetShotMgBleed(width, height, radius);
  const shell = [
    [-radius * 0.7, radius * 0.58],
    [-radius * 0.7, -radius * 0.12],
    [0, -radius * 0.62],
    [radius * 0.7, -radius * 0.12],
    [radius * 0.7, radius * 0.58]
  ];
  fillPolygon(target, shell, primary, 0.055);
  strokePolygon(target, shell, primary, 0.68, 3);
  target.moveTo(0, -radius * 0.62).lineTo(0, radius * 0.58).stroke({ color: secondary, width: 2, alpha: 0.5 });
  for (let pane = -3; pane <= 3; pane += 1) {
    const x = pane * radius * 0.18;
    target.moveTo(x, radius * 0.58).lineTo(x * 0.38, -radius * (0.58 - Math.abs(pane) * 0.04)).stroke({ color: pane % 2 ? secondary : primary, width: 1, alpha: 0.32 });
  }
  const doorX = radius * 0.2 * direction;
  target.rect(doorX - radius * 0.13, radius * 0.08, radius * 0.26, radius * 0.5).fill({ color: secondary, alpha: 0.1 });
  target.rect(doorX - radius * 0.13, radius * 0.08, radius * 0.26, radius * 0.5).stroke({ color: secondary, width: 2, alpha: 0.62 });
  target.moveTo(-bleed.x, radius * 0.58).lineTo(-radius * 0.7, radius * 0.58).stroke({ color: primary, width: 1, alpha: 0.3 });
  target.moveTo(radius * 0.7, radius * 0.58).lineTo(bleed.x, radius * 0.58).stroke({ color: primary, width: 1, alpha: 0.3 });
};
var drawSonnetPagodaMg = ({ target, radius, width, height, seed, primary, secondary }) => {
  const lean = seed % 2 === 0 ? 1 : -1;
  const bleed = resolveSonnetShotMgBleed(width, height, radius);
  for (let floor = 0; floor < 4; floor += 1) {
    const y = radius * (0.47 - floor * 0.27);
    const halfWidth = radius * (0.5 - floor * 0.075);
    const roof = [
      [-halfWidth * 1.18, y],
      [-halfWidth, y - radius * 0.11],
      [0, y - radius * 0.19],
      [halfWidth, y - radius * 0.11],
      [halfWidth * 1.18, y]
    ];
    fillPolygon(target, roof, floor % 2 ? secondary : primary, 0.07 + floor * 0.025);
    strokePolygon(target, roof, floor % 2 ? secondary : primary, 0.58, 2);
    target.rect(-halfWidth * 0.68, y, halfWidth * 1.36, radius * 0.17).fill({ color: primary, alpha: 0.035 + floor * 0.018 });
    target.rect(-halfWidth * 0.68, y, halfWidth * 1.36, radius * 0.17).stroke({ color: primary, width: 1, alpha: 0.36 });
  }
  target.moveTo(0, -radius * 0.62).lineTo(radius * 0.035 * lean, -radius * 0.78).stroke({ color: secondary, width: 3, alpha: 0.65 });
  target.moveTo(-bleed.x, radius * 0.64).lineTo(-radius * 0.52, radius * 0.64).stroke({ color: secondary, width: 1, alpha: 0.22 });
  target.moveTo(radius * 0.52, radius * 0.64).lineTo(bleed.x, radius * 0.64).stroke({ color: secondary, width: 1, alpha: 0.22 });
};
var drawSonnetCityFacadeMg = ({ target, radius, width, height, seed, primary, secondary }) => {
  const bleed = resolveSonnetShotMgBleed(width, height, radius);
  const heights = [0.52, 0.88, 0.66, 1.08, 0.74, 0.94, 0.58];
  const buildingWidth = radius * 0.205;
  heights.forEach((heightRatio, index) => {
    const x = radius * (-0.73 + index * 0.24);
    const height2 = radius * heightRatio;
    const color = index % 3 === 1 ? secondary : primary;
    target.rect(x, radius * 0.62 - height2, buildingWidth, height2).fill({ color, alpha: 0.045 + index % 3 * 0.035 });
    target.rect(x, radius * 0.62 - height2, buildingWidth, height2).stroke({ color, width: index === 3 ? 3 : 1.5, alpha: 0.5 });
    for (let row = 0; row < Math.floor(heightRatio * 6); row += 1) {
      for (let column = 0; column < 2; column += 1) {
        if ((row + column + index + seed) % 3 === 0) {
          target.rect(x + radius * (0.035 + column * 0.085), radius * 0.53 - height2 + row * radius * 0.13, radius * 0.045, radius * 0.055).fill({ color: column ? secondary : primary, alpha: 0.2 });
        }
      }
    }
  });
  target.moveTo(-bleed.x, radius * 0.63).lineTo(bleed.x, radius * 0.63).stroke({ color: primary, width: 4, alpha: 0.48 });
};

// src/components/visualizer/sonnet/sonnetShotMgBotanical.ts
var drawSonnetFernMg = ({ target, radius, seed, primary, secondary }) => {
  const tilt = (seed % 2 ? 1 : -1) * 0.18;
  target.moveTo(-radius * 0.12, radius * 0.72).bezierCurveTo(-radius * 0.04, radius * 0.2, radius * 0.12, -radius * 0.24, radius * 0.02, -radius * 0.72).stroke({ color: primary, width: 3, alpha: 0.62 });
  for (let index = 0; index < 13; index += 1) {
    const ratio = index / 13;
    const x = -radius * 0.12 + radius * 0.14 * ratio;
    const y = radius * (0.63 - ratio * 1.23);
    const length = radius * (0.3 - Math.abs(ratio - 0.5) * 0.18);
    drawLeaf(target, x, y, length, length * 0.22, Math.PI + tilt - ratio * 0.25, index % 3 ? primary : secondary, 0.09 + ratio * 0.05);
    drawLeaf(target, x, y, length, length * 0.22, -tilt + ratio * 0.25, index % 3 ? secondary : primary, 0.07 + ratio * 0.04);
  }
};
var drawSonnetGinkgoMg = ({ target, radius, seed, primary, secondary }) => {
  const direction = seed % 2 === 0 ? 1 : -1;
  target.moveTo(-radius * 0.72 * direction, radius * 0.55).bezierCurveTo(-radius * 0.25 * direction, radius * 0.16, radius * 0.08 * direction, -radius * 0.12, radius * 0.65 * direction, -radius * 0.5).stroke({ color: primary, width: 5, alpha: 0.38 });
  for (let index = 0; index < 8; index += 1) {
    const ratio = index / 7;
    const x = (-0.58 + ratio * 1.12) * radius * direction;
    const y = (0.4 - ratio * 0.78 + Math.sin(index * 1.8) * 0.08) * radius;
    const angle = -1.1 + index % 3 * 0.7;
    const size = radius * (0.13 + index % 4 * 0.018);
    target.moveTo(x, y).lineTo(x + Math.cos(angle) * size * 0.7, y + Math.sin(angle) * size * 0.7).stroke({ color: secondary, width: 1.5, alpha: 0.42 });
    const cx = x + Math.cos(angle) * size;
    const cy = y + Math.sin(angle) * size;
    target.moveTo(cx, cy).arc(cx, cy, size, angle + Math.PI * 0.1, angle + Math.PI * 0.9).lineTo(cx, cy).fill({ color: index % 2 ? primary : secondary, alpha: 0.09 + index % 3 * 0.04 });
    target.moveTo(cx, cy).arc(cx, cy, size, angle + Math.PI * 0.1, angle + Math.PI * 0.9).lineTo(cx, cy).stroke({ color: primary, width: 1.5, alpha: 0.58 });
  }
};
var drawSonnetClimbingVineMg = ({ target, radius, seed, primary, secondary }) => {
  const mirror = seed % 2 === 0 ? 1 : -1;
  for (let vine = 0; vine < 3; vine += 1) {
    const offset = (vine - 1) * radius * 0.3;
    target.moveTo(offset, radius * 0.76).bezierCurveTo(offset + radius * 0.5 * mirror, radius * 0.38, offset - radius * 0.48 * mirror, -radius * 0.1, offset + radius * 0.22 * mirror, -radius * 0.76).stroke({ color: vine === 1 ? secondary : primary, width: vine === 1 ? 3 : 1.5, alpha: 0.46 });
    for (let leaf = 0; leaf < 5; leaf += 1) {
      const ratio = (leaf + 1) / 6;
      const x = offset + Math.sin(ratio * Math.PI * 4 + vine) * radius * 0.16;
      const y = radius * (0.7 - ratio * 1.36);
      drawLeaf(target, x, y, radius * 0.2, radius * 0.055, leaf % 2 ? -0.25 : Math.PI + 0.25, leaf % 2 ? secondary : primary, 0.08 + vine * 0.035);
    }
  }
};

// src/components/visualizer/sonnet/sonnetShotMgFlora.ts
var drawSonnetCamelliaMg = ({ target, radius, seed, primary, secondary }) => {
  const turn = seed % 12 * Math.PI / 72;
  for (let ring = 0; ring < 3; ring += 1) {
    const count = 7 + ring * 4;
    for (let index = 0; index < count; index += 1) {
      const angle = turn + index / count * Math.PI * 2 + ring * 0.12;
      drawPetal(
        target,
        0,
        0,
        radius * (0.28 + ring * 0.15),
        radius * (0.075 + ring * 0.018),
        angle,
        ring === 1 ? secondary : primary,
        0.07 + ring * 0.045
      );
    }
  }
  target.circle(0, 0, radius * 0.1).fill({ color: secondary, alpha: 0.22 });
  target.circle(0, 0, radius * 0.13).stroke({ color: primary, width: 3, alpha: 0.68 });
};
var drawSonnetTulipFieldMg = ({ target, radius, seed, primary, secondary }) => {
  const direction = seed % 2 === 0 ? 1 : -1;
  for (let index = 0; index < 7; index += 1) {
    const x = (-0.66 + index * 0.22) * radius;
    const top = (-0.3 + (seed + index * 5) % 5 * 0.085) * radius;
    const bottom = radius * 0.68;
    target.moveTo(x, bottom).bezierCurveTo(x + radius * 0.04 * direction, radius * 0.28, x - radius * 0.05 * direction, top + radius * 0.12, x, top).stroke({ color: index % 2 ? secondary : primary, width: 2, alpha: 0.5 });
    drawLeaf(target, x, radius * 0.24, radius * 0.26, radius * 0.055, index % 2 ? -2.7 : -0.45, primary, 0.1);
    const bloomColor = index % 3 === 0 ? secondary : primary;
    target.moveTo(x, top + radius * 0.14).quadraticCurveTo(x - radius * 0.18, top - radius * 0.04, x - radius * 0.11, top - radius * 0.2).lineTo(x, top - radius * 0.1).lineTo(x + radius * 0.11, top - radius * 0.2).quadraticCurveTo(x + radius * 0.18, top - radius * 0.04, x, top + radius * 0.14).fill({ color: bloomColor, alpha: 0.12 + index % 3 * 0.045 });
    target.moveTo(x, top + radius * 0.14).quadraticCurveTo(x - radius * 0.18, top - radius * 0.04, x - radius * 0.11, top - radius * 0.2).lineTo(x, top - radius * 0.1).lineTo(x + radius * 0.11, top - radius * 0.2).quadraticCurveTo(x + radius * 0.18, top - radius * 0.04, x, top + radius * 0.14).stroke({ color: bloomColor, width: 2, alpha: 0.65 });
  }
};
var drawSonnetWildflowerMg = ({ target, radius, seed, primary, secondary }) => {
  for (let stem = 0; stem < 9; stem += 1) {
    const x = (-0.72 + stem * 0.18) * radius;
    const lean = ((seed + stem * 7) % 9 - 4) * radius * 0.018;
    const flowerY = (-0.45 + (seed + stem * 3) % 6 * 0.08) * radius;
    target.moveTo(x, radius * 0.72).quadraticCurveTo(x - lean, radius * 0.12, x + lean, flowerY).stroke({ color: stem % 2 ? secondary : primary, width: 1.5, alpha: 0.42 });
    for (let petal = 0; petal < 5; petal += 1) {
      const angle = petal / 5 * Math.PI * 2 - Math.PI / 2;
      drawPetal(target, x + lean, flowerY, radius * 0.105, radius * 0.032, angle, stem % 3 ? primary : secondary, 0.1);
    }
    target.circle(x + lean, flowerY, radius * 0.025).fill({ color: secondary, alpha: 0.48 });
  }
};

// src/components/visualizer/sonnet/sonnetShotMgLandscape.ts
var drawSonnetTerracesMg = ({ target, radius, width, height, seed, primary, secondary }) => {
  const direction = seed % 2 === 0 ? 1 : -1;
  const bleed = resolveSonnetShotMgBleed(width, height, radius);
  for (let band = 0; band < 7; band += 1) {
    const y = radius * (-0.5 + band * 0.16);
    const amplitude = radius * (0.09 + band * 0.012);
    target.moveTo(-bleed.x, y).bezierCurveTo(-radius * 0.35, y + amplitude * direction, radius * 0.08, y - amplitude * direction, bleed.x, y + amplitude * 0.35).lineTo(bleed.x, y + radius * 0.12).bezierCurveTo(radius * 0.12, y + radius * 0.04, -radius * 0.3, y + radius * 0.2, -bleed.x, y + radius * 0.12).fill({ color: band % 2 ? secondary : primary, alpha: 0.025 + band * 0.018 });
    target.moveTo(-bleed.x, y).bezierCurveTo(-radius * 0.35, y + amplitude * direction, radius * 0.08, y - amplitude * direction, bleed.x, y + amplitude * 0.35).stroke({ color: band % 2 ? secondary : primary, width: band % 3 === 0 ? 2.5 : 1, alpha: 0.34 + band * 0.04 });
  }
};
var drawSonnetMountainLakeMg = ({ target, radius, width, height, seed, primary, secondary }) => {
  const bleed = resolveSonnetShotMgBleed(width, height, radius);
  const shift = (seed % 7 - 3) / 3 * radius * 0.04;
  const back = [
    [-bleed.x, radius * 0.1],
    [-radius * 0.42, -radius * 0.46],
    [-radius * 0.14, -radius * 0.16],
    [radius * 0.22, -radius * 0.62],
    [bleed.x, radius * 0.1]
  ];
  const front = [
    [-bleed.x, radius * 0.22],
    [-radius * 0.28 + shift, -radius * 0.2],
    [radius * 0.06, radius * 0.05],
    [radius * 0.48 + shift, -radius * 0.28],
    [bleed.x, radius * 0.22]
  ];
  fillPolygon(target, back, secondary, 0.07);
  strokePolygon(target, back, secondary, 0.46, 1.5);
  fillPolygon(target, front, primary, 0.12);
  strokePolygon(target, front, primary, 0.66, 2.5);
  for (let line = 0; line < 7; line += 1) {
    const y = radius * (0.28 + line * 0.07);
    const inset = radius * (0.08 + line % 3 * 0.08);
    target.moveTo(-bleed.x + inset, y).lineTo(bleed.x - inset, y).stroke({ color: line % 2 ? secondary : primary, width: 1, alpha: 0.2 + line * 0.035 });
  }
  target.circle(-radius * 0.48, -radius * 0.48, radius * 0.1).fill({ color: secondary, alpha: 0.14 });
  target.circle(-radius * 0.48, -radius * 0.48, radius * 0.13).stroke({ color: secondary, width: 2, alpha: 0.5 });
};
var drawSonnetCoastalCliffMg = ({ target, radius, width, height, seed, primary, secondary }) => {
  const direction = seed % 2 === 0 ? 1 : -1;
  const bleed = resolveSonnetShotMgBleed(width, height, radius);
  const cliff = [
    [-bleed.x * direction, bleed.y],
    [-radius * 0.78 * direction, -radius * 0.1],
    [-radius * 0.5 * direction, -radius * 0.34],
    [-radius * 0.18 * direction, radius * 0.08],
    [radius * 0.08 * direction, radius * 0.58]
  ];
  fillPolygon(target, cliff, primary, 0.11);
  strokePolygon(target, cliff, primary, 0.62, 2.5);
  const towerX = -radius * 0.5 * direction;
  target.rect(towerX - radius * 0.09, -radius * 0.42, radius * 0.18, radius * 0.45).fill({ color: secondary, alpha: 0.12 });
  target.rect(towerX - radius * 0.09, -radius * 0.42, radius * 0.18, radius * 0.45).stroke({ color: secondary, width: 2, alpha: 0.7 });
  target.moveTo(towerX - radius * 0.14, -radius * 0.42).lineTo(towerX, -radius * 0.56).lineTo(towerX + radius * 0.14, -radius * 0.42).fill({ color: secondary, alpha: 0.2 });
  target.moveTo(towerX - radius * 0.14, -radius * 0.42).lineTo(towerX, -radius * 0.56).lineTo(towerX + radius * 0.14, -radius * 0.42).lineTo(towerX - radius * 0.14, -radius * 0.42).stroke({ color: secondary, width: 2, alpha: 0.72 });
  for (let wave = 0; wave < 6; wave += 1) {
    const y = radius * (0.14 + wave * 0.1);
    target.moveTo(-radius * 0.05 * direction, y).quadraticCurveTo(radius * 0.35 * direction, y - radius * 0.08, bleed.x * direction, y).stroke({ color: wave % 2 ? secondary : primary, width: wave % 3 === 0 ? 2 : 1, alpha: 0.26 + wave * 0.045 });
  }
};

// src/components/visualizer/sonnet/sonnetThemedShotMg.ts
var SONNET_THEMED_GEO_VARIANT_START = 24;
var THEMED_DRAWERS = [
  drawSonnetCamelliaMg,
  drawSonnetTulipFieldMg,
  drawSonnetWildflowerMg,
  drawSonnetFernMg,
  drawSonnetGinkgoMg,
  drawSonnetClimbingVineMg,
  drawSonnetGreenhouseMg,
  drawSonnetPagodaMg,
  drawSonnetCityFacadeMg,
  drawSonnetTerracesMg,
  drawSonnetMountainLakeMg,
  drawSonnetCoastalCliffMg
];
var drawThemedSonnetShotMg = (options) => {
  const index = options.variant - SONNET_THEMED_GEO_VARIANT_START;
  const drawer = THEMED_DRAWERS[index];
  if (!drawer) return false;
  drawer(options);
  return true;
};

// src/components/visualizer/sonnet/sonnetAdditionalShotMg.ts
var drawContourAtlas = ({ target, radius, seed, primary, secondary }) => {
  const steps = 48;
  for (let ring = 0; ring < 8; ring += 1) {
    const baseRadius = radius * (0.16 + ring * 0.075);
    for (let step = 0; step <= steps; step += 1) {
      const angle = step / steps * Math.PI * 2;
      const ripple = Math.sin(angle * 3 + seed * 0.07 + ring) * radius * 0.018 + Math.cos(angle * 5 - seed * 0.03 + ring * 0.7) * radius * 0.012;
      const x = Math.cos(angle) * (baseRadius + ripple) + Math.sin(ring * 1.7) * radius * 0.055;
      const y = Math.sin(angle) * (baseRadius + ripple) * 0.72 + Math.cos(ring * 1.3) * radius * 0.035;
      if (step === 0) target.moveTo(x, y);
      else target.lineTo(x, y);
    }
    target.stroke({
      color: ring % 3 === 0 ? secondary : primary,
      width: ring % 3 === 0 ? 2 : 1,
      alpha: 0.2 + ring * 0.045
    });
  }
  target.moveTo(-radius * 0.78, radius * 0.52).lineTo(-radius * 0.58, radius * 0.52).lineTo(-radius * 0.58, radius * 0.46).lineTo(-radius * 0.38, radius * 0.46).stroke({ color: primary, width: 3, alpha: 0.64 });
};
var drawRadialWave = ({ target, radius, seed, primary, secondary }) => {
  const bars = 64;
  for (let index = 0; index < bars; index += 1) {
    const angle = index / bars * Math.PI * 2;
    const signal = 0.5 + 0.5 * Math.sin(index * 1.83 + seed * 0.11);
    const inner = radius * (0.29 + signal * 0.035);
    const outer = radius * (0.43 + signal * 0.21);
    target.moveTo(Math.cos(angle) * inner, Math.sin(angle) * inner).lineTo(Math.cos(angle) * outer, Math.sin(angle) * outer).stroke({
      color: index % 8 === 0 ? secondary : primary,
      width: index % 8 === 0 ? 3 : 1,
      alpha: index % 2 === 0 ? 0.58 : 0.32
    });
  }
  target.circle(0, 0, radius * 0.24).stroke({ color: primary, width: 5, alpha: 0.7 });
  target.circle(0, 0, radius * 0.68).stroke({ color: secondary, width: 1, alpha: 0.18 });
  target.circle(0, 0, radius * 0.72).stroke({ color: primary, width: 2, alpha: 0.12 });
};
var drawTransitBlueprint = ({ target, radius, width, height, seed, primary, secondary }) => {
  const direction = seed % 2 === 0 ? 1 : -1;
  const bleed = resolveSonnetShotMgBleed(width, height, radius);
  const routes = [
    [[-0.72, -0.38], [-0.38, -0.38], [-0.38, 0.08], [0.08, 0.08], [0.08, 0.52], [0.68, 0.52]],
    [[-0.62, 0.58], [-0.62, 0.24], [-0.14, 0.24], [-0.14, -0.52], [0.5, -0.52], [0.5, -0.2], [0.74, -0.2]],
    [[-0.78, -0.06], [-0.5, -0.06], [-0.5, -0.62], [0.24, -0.62], [0.24, 0.3], [0.72, 0.3]]
  ];
  routes.forEach((route, routeIndex) => {
    const first = route[0];
    const last = route[route.length - 1];
    target.moveTo(-bleed.x * direction, first[1] * radius);
    route.forEach(([x, y]) => {
      const px = x * radius * direction;
      const py = y * radius;
      target.lineTo(px, py);
    });
    target.lineTo(bleed.x * direction, last[1] * radius);
    target.stroke({
      color: routeIndex === 1 ? secondary : primary,
      width: routeIndex === 0 ? 5 : 2,
      alpha: 0.34 + routeIndex * 0.12
    });
    route.forEach(([x, y], pointIndex) => {
      if (pointIndex === 0 || pointIndex === route.length - 1 || (pointIndex + routeIndex) % 2 === 0) {
        const px = x * radius * direction;
        const py = y * radius;
        target.circle(px, py, pointIndex === 0 ? 10 : 6).fill({ color: pointIndex % 2 === 0 ? primary : secondary, alpha: 0.72 });
        target.circle(px, py, pointIndex === 0 ? 16 : 11).stroke({ color: primary, width: 1, alpha: 0.36 });
      }
    });
  });
};
var drawChronograph = ({ target, radius, seed, primary, secondary }) => {
  const rings = [0.2, 0.38, 0.62];
  rings.forEach((scale, index) => {
    target.circle(0, 0, radius * scale).stroke({
      color: index === 1 ? secondary : primary,
      width: index === 2 ? 4 : 2,
      alpha: 0.3 + index * 0.13
    });
  });
  for (let tick = 0; tick < 48; tick += 1) {
    const angle = tick / 48 * Math.PI * 2;
    const outer = radius * 0.72;
    const inner = outer - radius * (tick % 4 === 0 ? 0.11 : 0.045);
    target.moveTo(Math.cos(angle) * inner, Math.sin(angle) * inner).lineTo(Math.cos(angle) * outer, Math.sin(angle) * outer).stroke({ color: tick % 4 === 0 ? secondary : primary, width: tick % 4 === 0 ? 3 : 1, alpha: 0.5 });
  }
  const handAngle = seed % 60 / 60 * Math.PI * 2 - Math.PI / 2;
  const secondAngle = seed * 7 % 60 / 60 * Math.PI * 2 - Math.PI / 2;
  target.moveTo(-Math.cos(handAngle) * radius * 0.12, -Math.sin(handAngle) * radius * 0.12).lineTo(Math.cos(handAngle) * radius * 0.55, Math.sin(handAngle) * radius * 0.55).stroke({ color: primary, width: 6, alpha: 0.74 });
  target.moveTo(0, 0).lineTo(Math.cos(secondAngle) * radius * 0.65, Math.sin(secondAngle) * radius * 0.65).stroke({ color: secondary, width: 2, alpha: 0.72 });
  target.circle(0, 0, radius * 0.055).fill({ color: primary, alpha: 0.85 });
};
var drawFoldedRibbons = ({ target, radius, width, height, seed, primary, secondary }) => {
  const direction = seed % 2 === 0 ? 1 : -1;
  const bleed = resolveSonnetShotMgBleed(width, height, radius);
  for (let band = 0; band < 5; band += 1) {
    const y = (-0.5 + band * 0.25) * radius;
    const offset = (band % 2 === 0 ? 1 : -1) * direction;
    target.moveTo(-bleed.x, y).bezierCurveTo(
      -radius * 0.38,
      y - radius * 0.28 * offset,
      radius * 0.18,
      y + radius * 0.28 * offset,
      bleed.x,
      y
    ).stroke({ color: band % 2 === 0 ? primary : secondary, width: band === 2 ? 12 : 5, alpha: 0.24 + band * 0.08 });
    target.moveTo(-bleed.x, y + radius * 0.055).bezierCurveTo(
      -radius * 0.38,
      y - radius * 0.28 * offset + radius * 0.055,
      radius * 0.18,
      y + radius * 0.28 * offset + radius * 0.055,
      bleed.x,
      y + radius * 0.055
    ).stroke({ color: primary, width: 1, alpha: 0.3 });
  }
  target.moveTo(-radius * 0.34, -bleed.y).lineTo(-radius * 0.34, -radius * 0.18).stroke({ color: primary, width: 2, alpha: 0.18 });
  target.moveTo(radius * 0.34, radius * 0.18).lineTo(radius * 0.34, bleed.y).stroke({ color: primary, width: 2, alpha: 0.18 });
};
var drawHalftonePoster = ({ target, radius, width, height, seed, primary, secondary }) => {
  const spacing = radius * 0.17;
  const bleed = resolveSonnetShotMgBleed(width, height, radius);
  const columns = Math.ceil(bleed.x * 2 / spacing) + 2;
  const rows = Math.ceil(bleed.y * 2 / spacing) + 2;
  for (let row = 0; row < rows; row += 1) {
    for (let column = 0; column < columns; column += 1) {
      const x = (column - (columns - 1) / 2) * spacing;
      const y = (row - (rows - 1) / 2) * spacing;
      const distance = Math.hypot(x, y) / radius;
      const pulse = 0.5 + 0.5 * Math.sin(column * 0.9 + row * 1.4 + seed * 0.08);
      const dotRadius = radius * (9e-3 + Math.max(0, 0.72 - distance) * 0.034 + pulse * 0.012);
      target.circle(x, y, dotRadius).fill({
        color: (row + column) % 5 === 0 ? secondary : primary,
        alpha: 0.24 + pulse * 0.5
      });
    }
  }
  target.moveTo(-bleed.x, -bleed.y * 0.72).lineTo(bleed.x, -bleed.y * 0.72).stroke({ color: secondary, width: 2, alpha: 0.28 });
  target.moveTo(-bleed.x * 0.58, -bleed.y).lineTo(-bleed.x * 0.58, bleed.y).stroke({ color: primary, width: 1, alpha: 0.2 });
};
var drawAdditionalSonnetShotMg = (options) => {
  switch (options.variant) {
    case 18:
      drawContourAtlas(options);
      return true;
    case 19:
      drawRadialWave(options);
      return true;
    case 20:
      drawTransitBlueprint(options);
      return true;
    case 21:
      drawChronograph(options);
      return true;
    case 22:
      drawFoldedRibbons(options);
      return true;
    case 23:
      drawHalftonePoster(options);
      return true;
    default:
      return drawThemedSonnetShotMg(options);
  }
};

// src/components/visualizer/sonnet/sonnetShotMg.ts
var colorNumber = (pixi, color) => pixi.Color.shared.setValue(color).toNumber();
var normalizeAudioLevel = (value) => Math.min(1, Math.max(0, value > 1 ? value / 255 : value));
var AnimatedGraphics = class {
  constructor(pixi) {
    this.commands = [];
    this.currentPath = [];
    this.currentLength = 0;
    this.lastX = 0;
    this.lastY = 0;
    this.display = new pixi.Graphics();
  }
  get rotation() {
    return this.display.rotation;
  }
  set rotation(v) {
    this.display.rotation = v;
  }
  get mask() {
    return this.display.mask;
  }
  set mask(v) {
    this.display.mask = v;
  }
  moveTo(x, y) {
    this.currentPath.push({ type: "moveTo", x, y });
    this.lastX = x;
    this.lastY = y;
    return this;
  }
  lineTo(x, y) {
    const len = Math.hypot(x - this.lastX, y - this.lastY);
    this.currentPath.push({ type: "lineTo", x, y, len, lastX: this.lastX, lastY: this.lastY });
    this.currentLength += len;
    this.lastX = x;
    this.lastY = y;
    return this;
  }
  quadraticCurveTo(cx, cy, tx, ty) {
    const len = Math.hypot(cx - this.lastX, cy - this.lastY) + Math.hypot(tx - cx, ty - cy);
    this.currentPath.push({ type: "quadraticCurveTo", cx, cy, tx, ty, len, lastX: this.lastX, lastY: this.lastY });
    this.currentLength += len;
    this.lastX = tx;
    this.lastY = ty;
    return this;
  }
  bezierCurveTo(c1x, c1y, c2x, c2y, tx, ty) {
    const len = Math.hypot(c1x - this.lastX, c1y - this.lastY) + Math.hypot(c2x - c1x, c2y - c1y) + Math.hypot(tx - c2x, ty - c2y);
    this.currentPath.push({ type: "bezierCurveTo", c1x, c1y, c2x, c2y, tx, ty, len, lastX: this.lastX, lastY: this.lastY });
    this.currentLength += len;
    this.lastX = tx;
    this.lastY = ty;
    return this;
  }
  arc(cx, cy, r, start, end, anticlockwise = false) {
    let diff = end - start;
    if (anticlockwise && diff > 0) diff -= Math.PI * 2;
    else if (!anticlockwise && diff < 0) diff += Math.PI * 2;
    const len = Math.abs(diff) * r;
    this.currentPath.push({ type: "arc", cx, cy, r, start, end, anticlockwise, len, diff });
    this.currentLength += len;
    this.lastX = cx + Math.cos(end) * r;
    this.lastY = cy + Math.sin(end) * r;
    return this;
  }
  circle(x, y, r) {
    const start = Math.random() * Math.PI * 2;
    const anticlockwise = Math.random() > 0.5;
    const diff = anticlockwise ? -Math.PI * 2 : Math.PI * 2;
    const len = Math.PI * 2 * r;
    const startX = x + Math.cos(start) * r;
    const startY = y + Math.sin(start) * r;
    this.moveTo(startX, startY);
    this.currentPath.push({ type: "arc", cx: x, cy: y, r, start, end: start + diff, anticlockwise, len, diff });
    this.currentLength += len;
    this.lastX = x + Math.cos(start + diff) * r;
    this.lastY = y + Math.sin(start + diff) * r;
    return this;
  }
  rect(x, y, w, h) {
    this.currentPath.push({ type: "rect_hint", x, y, w, h });
    this.moveTo(x, y).lineTo(x + w, y).lineTo(x + w, y + h).lineTo(x, y + h).lineTo(x, y);
    return this;
  }
  stroke(options) {
    if (this.currentPath.length > 0) {
      this.commands.push({ type: "stroke", path: [...this.currentPath], length: this.currentLength, options });
      this.currentPath = [];
      this.currentLength = 0;
    }
    return this;
  }
  fill(options) {
    if (this.currentPath.length > 0) {
      this.commands.push({ type: "fill", path: [...this.currentPath], length: this.currentLength, options });
      this.currentPath = [];
      this.currentLength = 0;
    }
    return this;
  }
  update(rawProgress) {
    this.display.clear();
    let strokeIndex = 0;
    for (const cmd of this.commands) {
      if (cmd.type === "fill") {
        this.display.moveTo(0, 0);
        let isRectWipe = false;
        if (cmd.path.length === 6 && cmd.path[0].type === "rect_hint") {
          isRectWipe = true;
          const r = cmd.path[0];
          const wipeProgress = 1 - Math.pow(1 - rawProgress, 3);
          this.display.rect(r.x, r.y, r.w * wipeProgress, r.h);
        }
        if (!isRectWipe) {
          for (const p of cmd.path) {
            if (p.type === "rect_hint") continue;
            if (p.type === "moveTo") this.display.moveTo(p.x, p.y);
            else if (p.type === "lineTo") this.display.lineTo(p.x, p.y);
            else if (p.type === "circle") this.display.circle(p.x, p.y, p.r);
            else if (p.type === "arc") this.display.arc(p.cx, p.cy, p.r, p.start, p.end, p.anticlockwise);
            else if (p.type === "quadraticCurveTo") this.display.quadraticCurveTo(p.cx, p.cy, p.tx, p.ty);
            else if (p.type === "bezierCurveTo") this.display.bezierCurveTo(p.c1x, p.c1y, p.c2x, p.c2y, p.tx, p.ty);
          }
        }
        const alphaProgress = 1 - Math.pow(1 - Math.min(1, rawProgress * 2), 3);
        const alpha = (cmd.options.alpha ?? 1) * alphaProgress;
        this.display.fill({ ...cmd.options, alpha });
      } else if (cmd.type === "stroke") {
        if (cmd.length <= 0) continue;
        const delay = strokeIndex * 0.23 % 0.4;
        const finishTime = 0.5 + strokeIndex * 0.31 % 0.5;
        const localRaw = Math.min(1, Math.max(0, (rawProgress - delay) / (finishTime - delay)));
        const localProgress = 1 - Math.pow(1 - localRaw, 3);
        strokeIndex++;
        const targetLen = cmd.length * localProgress;
        let currentLen = 0;
        for (const p of cmd.path) {
          if (p.type === "rect_hint") continue;
          if (p.type === "moveTo") {
            this.display.moveTo(p.x, p.y);
          } else {
            if (currentLen >= targetLen) break;
            if (currentLen + p.len <= targetLen) {
              if (p.type === "lineTo") this.display.lineTo(p.x, p.y);
              else if (p.type === "circle") this.display.circle(p.x, p.y, p.r);
              else if (p.type === "arc") this.display.arc(p.cx, p.cy, p.r, p.start, p.end, p.anticlockwise);
              else if (p.type === "quadraticCurveTo") this.display.quadraticCurveTo(p.cx, p.cy, p.tx, p.ty);
              else if (p.type === "bezierCurveTo") this.display.bezierCurveTo(p.c1x, p.c1y, p.c2x, p.c2y, p.tx, p.ty);
              currentLen += p.len;
            } else {
              const ratio = (targetLen - currentLen) / p.len;
              if (p.type === "lineTo") {
                const x = p.lastX + (p.x - p.lastX) * ratio;
                const y = p.lastY + (p.y - p.lastY) * ratio;
                this.display.lineTo(x, y);
              } else if (p.type === "circle") {
                this.display.arc(p.x, p.y, p.r, 0, Math.PI * 2 * ratio);
              } else if (p.type === "arc") {
                this.display.arc(p.cx, p.cy, p.r, p.start, p.start + p.diff * ratio, p.anticlockwise);
              } else if (p.type === "quadraticCurveTo") {
                const newCpX = p.lastX + ratio * (p.cx - p.lastX);
                const newCpY = p.lastY + ratio * (p.cy - p.lastY);
                const newTx = (1 - ratio) * (1 - ratio) * p.lastX + 2 * (1 - ratio) * ratio * p.cx + ratio * ratio * p.tx;
                const newTy = (1 - ratio) * (1 - ratio) * p.lastY + 2 * (1 - ratio) * ratio * p.cy + ratio * ratio * p.ty;
                this.display.quadraticCurveTo(newCpX, newCpY, newTx, newTy);
              } else if (p.type === "bezierCurveTo") {
                const q0x = p.lastX + ratio * (p.c1x - p.lastX);
                const q0y = p.lastY + ratio * (p.c1y - p.lastY);
                const q1x = p.c1x + ratio * (p.c2x - p.c1x);
                const q1y = p.c1y + ratio * (p.c2y - p.c1y);
                const q2x = p.c2x + ratio * (p.tx - p.c2x);
                const q2y = p.c2y + ratio * (p.ty - p.c2y);
                const r0x = q0x + ratio * (q1x - q0x);
                const r0y = q0y + ratio * (q1y - q0y);
                const r1x = q1x + ratio * (q2x - q1x);
                const r1y = q1y + ratio * (q2y - q1y);
                const bx = r0x + ratio * (r1x - r0x);
                const by = r0y + ratio * (r1y - r0y);
                this.display.bezierCurveTo(q0x, q0y, r0x, r0y, bx, by);
              }
              currentLen = targetLen;
              break;
            }
          }
        }
        this.display.stroke(cmd.options);
      }
    }
  }
};
var buildSonnetShotMg = (pixi, kind, theme, width, height, seed, iconTextures) => {
  const { Container, Graphics, Sprite } = pixi;
  const container = new Container();
  const primary = colorNumber(pixi, theme.primaryColor);
  const secondary = colorNumber(pixi, theme.secondaryColor);
  const radius = Math.min(width, height);
  const bg = new AnimatedGraphics(pixi);
  const drawCross = (x, y, size2, color, alpha = 0.5) => {
    bg.moveTo(x - size2, y - size2).lineTo(x + size2, y + size2).stroke({ color, width: 1, alpha });
    bg.moveTo(x + size2, y - size2).lineTo(x - size2, y + size2).stroke({ color, width: 1, alpha });
  };
  const drawHatching = (x, y, w, h, spacing = 8, target = bg.display) => {
    const lines = new Graphics();
    lines.rect(x, y, w, h);
    target.addChild(lines);
    const hatch = new Graphics();
    for (let i = -w; i < w + h; i += spacing) {
      hatch.moveTo(x + i, y).lineTo(x + i + h, y + h).stroke({ color: primary, width: 1, alpha: 0.15 });
    }
    const mask = new Graphics();
    mask.rect(x, y, w, h).fill({ color: 16777215 });
    hatch.mask = mask;
    target.addChild(hatch);
    target.addChild(mask);
  };
  const hw = width / 2;
  const hh = height / 2;
  const marginX = width * 0.05;
  const marginY = height * 0.05;
  const size = 4;
  drawCross(-hw + marginX, -hh + marginY, size, primary, 0.4);
  drawCross(hw - marginX, -hh + marginY, size, primary, 0.4);
  drawCross(-hw + marginX, hh - marginY, size, primary, 0.4);
  drawCross(hw - marginX, hh - marginY, size, primary, 0.4);
  for (let i = 0; i < 8; i++) {
    drawCross(-hw + marginX, -hh + marginY + i * 20 + 30, 3, primary, 0.3);
  }
  const barY = hh - marginY - 10;
  bg.moveTo(-hw + marginX + 20, barY).lineTo(hw - marginX - 20, barY).stroke({ color: primary, width: 1, alpha: 0.3 });
  drawCross(-hw + marginX + 10, barY, 3, primary, 0.5);
  drawCross(-hw + marginX + 30, barY, 3, primary, 0.5);
  drawCross(hw - marginX - 10, barY, 3, primary, 0.5);
  bg.circle(0, barY, 2).fill({ color: secondary, alpha: 0.8 });
  let geo;
  let fixedGeoLayer;
  if (kind === "type-impact" || kind === "fragment-collage") {
    geo = new AnimatedGraphics(pixi);
    if (!geo) throw new Error("Unreachable");
    const geoVariant = resolveSonnetGeoVariant(seed);
    if (geoVariant === 0) {
      geo.circle(0, 0, radius * 0.6).stroke({ color: primary, width: 6, alpha: 0.8 });
      geo.circle(0, 0, radius * 0.58).stroke({ color: primary, width: 2, alpha: 0.4 });
      for (let i = 0; i < 32; i++) {
        const angle = i / 32 * Math.PI * 2;
        const r1 = radius * (0.3 + i % 3 * 0.05);
        const r2 = radius * 0.55;
        geo.moveTo(Math.cos(angle) * r1, Math.sin(angle) * r1).lineTo(Math.cos(angle) * r2, Math.sin(angle) * r2).stroke({ color: primary, width: 1, alpha: 0.2 + i % 2 * 0.1 });
      }
    } else if (geoVariant === 1) {
      const r = radius * 0.7;
      geo.moveTo(0, -r).lineTo(r, 0).lineTo(0, r).lineTo(-r, 0).lineTo(0, -r).stroke({ color: primary, width: 6, alpha: 0.8 });
      geo.moveTo(0, -r * 0.96).lineTo(r * 0.96, 0).lineTo(0, r * 0.96).lineTo(-r * 0.96, 0).lineTo(0, -r * 0.96).stroke({ color: primary, width: 2, alpha: 0.4 });
      geo.moveTo(0, -r * 0.4).lineTo(r * 0.4, 0).lineTo(0, r * 0.4).lineTo(-r * 0.4, 0).lineTo(0, -r * 0.4).stroke({ color: primary, width: 1, alpha: 0.6 });
      geo.moveTo(-r, 0).lineTo(r, 0).stroke({ color: primary, width: 1, alpha: 0.3 });
      geo.moveTo(0, -r).lineTo(0, r).stroke({ color: primary, width: 1, alpha: 0.3 });
    } else if (geoVariant === 2) {
      const drawHex = (x, y, r, w, a) => {
        geo.moveTo(x + r * Math.sin(0), y - r * Math.cos(0));
        for (let j = 1; j <= 6; j++) geo.lineTo(x + r * Math.sin(j * Math.PI / 3), y - r * Math.cos(j * Math.PI / 3));
        geo.stroke({ color: primary, width: w, alpha: a });
      };
      drawHex(0, 0, radius * 0.6, 6, 0.8);
      drawHex(0, 0, radius * 0.57, 2, 0.4);
      drawHex(0, 0, radius * 0.25, 1, 0.5);
      for (let j = 0; j < 6; j++) {
        const angle = j * Math.PI / 3 - Math.PI / 6;
        geo.moveTo(Math.cos(angle) * radius * 0.25, Math.sin(angle) * radius * 0.25).lineTo(Math.cos(angle) * radius * 0.57, Math.sin(angle) * radius * 0.57).stroke({ color: primary, width: 2, alpha: 0.4 });
      }
    } else if (geoVariant === 3) {
      const molVariant = resolveSonnetMoleculeVariant(seed);
      if (molVariant === 0) {
        const hexR = radius * 0.22;
        const drawBenzene = (cx, cy, scale, rotationOffset = 0) => {
          const r = hexR * scale;
          geo.moveTo(cx + r * Math.sin(rotationOffset), cy - r * Math.cos(rotationOffset));
          for (let j = 1; j <= 6; j++) {
            geo.lineTo(cx + r * Math.sin(j * Math.PI / 3 + rotationOffset), cy - r * Math.cos(j * Math.PI / 3 + rotationOffset));
          }
          geo.stroke({ color: primary, width: 3, alpha: 0.8 });
          for (let j = 0; j < 6; j += 2) {
            const innerR = r * 0.82;
            geo.moveTo(cx + innerR * Math.sin(j * Math.PI / 3 + rotationOffset), cy - innerR * Math.cos(j * Math.PI / 3 + rotationOffset)).lineTo(cx + innerR * Math.sin((j + 1) * Math.PI / 3 + rotationOffset), cy - innerR * Math.cos((j + 1) * Math.PI / 3 + rotationOffset)).stroke({ color: primary, width: 2, alpha: 0.5 });
          }
        };
        const rMain = hexR * 1.2;
        drawBenzene(0, 0, 1.2);
        const dx = Math.sin(Math.PI / 3) * rMain * 2;
        drawBenzene(dx, 0, 1.2);
        const branchDist = Math.sin(Math.PI / 3) * rMain * 2;
        drawBenzene(-Math.sin(Math.PI / 6) * branchDist, -Math.cos(Math.PI / 6) * branchDist, 1.2);
        geo.moveTo(0, rMain).lineTo(0, rMain + radius * 0.2).lineTo(radius * 0.15, rMain + radius * 0.35).stroke({ color: primary, width: 2, alpha: 0.6 });
      } else if (molVariant === 1) {
        const hexR = radius * 0.22;
        geo.moveTo(0, -hexR);
        for (let j = 1; j <= 6; j++) geo.lineTo(hexR * Math.sin(j * Math.PI / 3), -hexR * Math.cos(j * Math.PI / 3));
        geo.stroke({ color: primary, width: 3, alpha: 0.8 });
        geo.moveTo(hexR * 0.8 * Math.sin(Math.PI / 3), -hexR * 0.8 * Math.cos(Math.PI / 3)).lineTo(hexR * 0.8 * Math.sin(2 * Math.PI / 3), -hexR * 0.8 * Math.cos(2 * Math.PI / 3)).stroke({ color: primary, width: 2, alpha: 0.5 });
        geo.moveTo(hexR * 0.8 * Math.sin(4 * Math.PI / 3), -hexR * 0.8 * Math.cos(4 * Math.PI / 3)).lineTo(hexR * 0.8 * Math.sin(5 * Math.PI / 3), -hexR * 0.8 * Math.cos(5 * Math.PI / 3)).stroke({ color: primary, width: 2, alpha: 0.5 });
        const px1 = hexR * Math.sqrt(3) / 2;
        const py1 = -hexR / 2;
        const px2 = hexR * Math.sqrt(3) / 2;
        const py2 = hexR / 2;
        const pentTopX = px1 + hexR * 0.8;
        const pentTopY = py1 - hexR * 0.1;
        const pentMidX = px1 + hexR * 1.2;
        const pentMidY = 0;
        const pentBotX = px2 + hexR * 0.8;
        const pentBotY = py2 + hexR * 0.1;
        geo.moveTo(px1, py1).lineTo(pentTopX, pentTopY).lineTo(pentMidX, pentMidY).lineTo(pentBotX, pentBotY).lineTo(px2, py2).stroke({ color: primary, width: 3, alpha: 0.8 });
        const drawBranch = (sx, sy, angle, len, node) => {
          const ex = sx + Math.cos(angle) * len;
          const ey = sy + Math.sin(angle) * len;
          geo.moveTo(sx, sy).lineTo(ex, ey).stroke({ color: primary, width: 2, alpha: 0.6 });
          if (node) geo.circle(ex, ey, 6).stroke({ color: primary, width: 2, alpha: 0.8 });
        };
        drawBranch(0, -hexR, -Math.PI / 2, radius * 0.15, true);
        drawBranch(-hexR * Math.sqrt(3) / 2, hexR / 2, Math.PI * 0.8, radius * 0.2, true);
        drawBranch(-hexR * Math.sqrt(3) / 2, -hexR / 2, -Math.PI * 0.8, radius * 0.15, false);
        drawBranch(pentMidX, pentMidY, 0, radius * 0.18, false);
        drawBranch(pentMidX + radius * 0.18, pentMidY, Math.PI / 4, radius * 0.1, true);
      } else {
        const segLen = radius * 0.18;
        const steps = 7;
        const startX = -segLen * (steps / 2) * Math.cos(Math.PI / 6);
        const pts = [];
        let cx = startX;
        let cy = 0;
        pts.push({ x: cx, y: cy });
        for (let i = 0; i < steps; i++) {
          cx += segLen * Math.cos(Math.PI / 6);
          cy = (i % 2 === 0 ? 1 : -1) * segLen * Math.sin(Math.PI / 6);
          pts.push({ x: cx, y: cy });
        }
        geo.moveTo(pts[0].x, pts[0].y);
        for (let i = 1; i <= steps; i++) geo.lineTo(pts[i].x, pts[i].y);
        geo.stroke({ color: primary, width: 3, alpha: 0.8 });
        const nx = -Math.sin(Math.PI / 6) * 6;
        const ny = Math.cos(Math.PI / 6) * 6;
        geo.moveTo(pts[1].x + nx, pts[1].y + ny).lineTo(pts[2].x + nx, pts[2].y + ny).stroke({ color: primary, width: 2, alpha: 0.5 });
        for (let i = 1; i < steps; i++) {
          const angle = i % 2 === 0 ? Math.PI / 2 : -Math.PI / 2;
          const bx = pts[i].x;
          const by = pts[i].y + Math.sin(angle) * segLen * 0.6;
          geo.moveTo(pts[i].x, pts[i].y).lineTo(bx, by).stroke({ color: primary, width: 2, alpha: 0.5 });
          if (i % 2 !== 0) {
            geo.circle(bx, by, 5).stroke({ color: primary, width: 2, alpha: 0.8 });
          } else {
            geo.moveTo(bx, by).lineTo(bx + segLen * 0.5, by - segLen * 0.3).stroke({ color: primary, width: 2, alpha: 0.5 });
          }
        }
      }
    } else if (geoVariant === 4) {
      const ellR = radius * 0.7;
      for (let i = 0; i < 3; i++) {
        const angle = i * Math.PI / 3;
        const steps = 60;
        for (let j = 0; j <= steps; j++) {
          const t = j * Math.PI * 2 / steps;
          const ex = Math.cos(t) * ellR;
          const ey = Math.sin(t) * ellR * 0.18;
          const rx = ex * Math.cos(angle) - ey * Math.sin(angle);
          const ry = ex * Math.sin(angle) + ey * Math.cos(angle);
          if (j === 0) geo.moveTo(rx, ry);
          else geo.lineTo(rx, ry);
        }
        geo.stroke({ color: primary, width: 1, alpha: 0.3 });
      }
      geo.circle(0, 0, radius * 0.05).fill({ color: primary, alpha: 0.8 });
    } else if (geoVariant === 5) {
      const planetR = radius * 0.25;
      geo.circle(0, 0, planetR).fill({ color: primary, alpha: 0.15 }).stroke({ color: primary, width: 2, alpha: 0.8 });
      geo.moveTo(-planetR * 0.7, -planetR * 0.5).quadraticCurveTo(0, -planetR * 0.2, planetR * 0.7, -planetR * 0.5).stroke({ color: primary, width: 1, alpha: 0.4 });
      geo.moveTo(-planetR * 0.9, 0).quadraticCurveTo(0, planetR * 0.3, planetR * 0.9, 0).stroke({ color: primary, width: 1, alpha: 0.4 });
      const ringRx = radius * 0.6;
      const ringRy = radius * 0.15;
      const angle = Math.PI / 6;
      const drawTiltedEllipse = (rx, ry, w, a, segments = 60) => {
        for (let j = 0; j <= segments; j++) {
          const t = j * Math.PI * 2 / segments;
          const ex = Math.cos(t) * rx;
          const ey = Math.sin(t) * ry;
          const rotX = ex * Math.cos(angle) - ey * Math.sin(angle);
          const rotY = ex * Math.sin(angle) + ey * Math.cos(angle);
          if (j === 0) geo.moveTo(rotX, rotY);
          else geo.lineTo(rotX, rotY);
        }
        geo.stroke({ color: primary, width: w, alpha: a });
      };
      drawTiltedEllipse(ringRx, ringRy, 4, 0.5);
      drawTiltedEllipse(ringRx * 1.1, ringRy * 1.15, 1, 0.3);
      drawTiltedEllipse(ringRx * 1.25, ringRy * 1.3, 2, 0.2);
      geo.circle(0, 0, radius * 0.7).stroke({ color: primary, width: 1, alpha: 0.2 });
      geo.circle(Math.cos(Math.PI / 4) * radius * 0.7, Math.sin(Math.PI / 4) * radius * 0.7, 8).fill({ color: primary, alpha: 0.6 });
    } else if (geoVariant === 6) {
      const bleed = resolveSonnetShotMgBleed(width, height, radius);
      const w = bleed.x * 1.08;
      const h = radius * 0.8;
      const baseY = radius * 0.2;
      geo.circle(0, baseY - h * 0.6, radius * 0.3).stroke({ color: primary, width: 2, alpha: 0.4 });
      for (let i = 0; i < 5; i++) {
        geo.moveTo(-bleed.x, baseY - h * 0.6 + i * 15).lineTo(bleed.x, baseY - h * 0.6 + i * 15).stroke({ color: primary, width: 1, alpha: 0.3 });
      }
      const peaks = 7;
      for (let layer = 0; layer < 3; layer++) {
        const layerW = w * (1 + layer * 0.2);
        const layerH = h * (0.5 + layer * 0.25);
        geo.moveTo(-layerW / 2, baseY);
        for (let i = 1; i < peaks; i++) {
          const px = -layerW / 2 + layerW / peaks * i;
          const py = baseY - layerH * (0.3 + 0.7 * Math.abs(Math.sin(seed + layer * 11 + i * 7)));
          geo.lineTo(px, py);
        }
        geo.lineTo(layerW / 2, baseY);
        geo.stroke({ color: primary, width: 3 - layer, alpha: 0.6 - layer * 0.15 });
      }
      geo.moveTo(-w, baseY).lineTo(w, baseY).stroke({ color: primary, width: 4, alpha: 0.8 });
      for (let i = 0; i < 5; i++) {
        const gridY = baseY + Math.pow(i, 1.5) * 12;
        geo.moveTo(-w, gridY).lineTo(w, gridY).stroke({ color: primary, width: 1, alpha: 0.4 - i * 0.08 });
      }
      for (let i = -4; i <= 4; i++) {
        geo.moveTo(i * radius * 0.2, baseY).lineTo(i * bleed.x * 0.32, bleed.y).stroke({ color: primary, width: 1, alpha: 0.3 });
      }
    } else if (geoVariant === 7) {
      for (let i = 1; i <= 6; i++) {
        const r = radius * 0.15 * i;
        geo.circle(0, 0, r).stroke({ color: primary, width: i % 2 === 0 ? 2 : 1, alpha: 0.2 + i % 3 * 0.1 });
      }
      geo.moveTo(-radius * 0.9, 0).lineTo(radius * 0.9, 0).stroke({ color: primary, width: 1, alpha: 0.4 });
      geo.moveTo(0, -radius * 0.9).lineTo(0, radius * 0.9).stroke({ color: primary, width: 1, alpha: 0.4 });
      geo.moveTo(0, 0);
      geo.arc(0, 0, radius * 0.75, 0, Math.PI / 4);
      geo.lineTo(0, 0);
      geo.fill({ color: primary, alpha: 0.1 });
      geo.stroke({ color: primary, width: 2, alpha: 0.5 });
      const rOuter = radius * 0.8;
      for (let i = 0; i < 72; i++) {
        const angle = i / 72 * Math.PI * 2;
        const len = i % 18 === 0 ? 20 : i % 6 === 0 ? 10 : 5;
        geo.moveTo(Math.cos(angle) * rOuter, Math.sin(angle) * rOuter).lineTo(Math.cos(angle) * (rOuter + len), Math.sin(angle) * (rOuter + len)).stroke({ color: primary, width: 1, alpha: 0.4 });
      }
      const lockAngle = seed % 360 * Math.PI / 180;
      const lockR = radius * 0.45;
      const lx = Math.cos(lockAngle) * lockR;
      const ly = Math.sin(lockAngle) * lockR;
      geo.rect(lx - 15, ly - 15, 30, 30).stroke({ color: primary, width: 2, alpha: 0.8 });
      geo.moveTo(lx, ly - 20).lineTo(lx, ly + 20).stroke({ color: primary, width: 1, alpha: 0.6 });
      geo.moveTo(lx - 20, ly).lineTo(lx + 20, ly).stroke({ color: primary, width: 1, alpha: 0.6 });
    } else if (geoVariant === 8) {
      const fw = radius * 0.85;
      const fh = radius * 0.65;
      const bracketSize = radius * 0.15;
      const drawBracket = (cx, cy, sx, sy) => {
        geo.moveTo(cx - sx * bracketSize, cy).lineTo(cx, cy).lineTo(cx, cy - sy * bracketSize).stroke({ color: primary, width: 3, alpha: 0.7 });
        geo.moveTo(cx - sx * bracketSize * 0.8, cy - sy * 8).lineTo(cx - sx * 8, cy - sy * 8).lineTo(cx - sx * 8, cy - sy * bracketSize * 0.8).stroke({ color: primary, width: 1, alpha: 0.4 });
      };
      drawBracket(-fw, -fh, -1, -1);
      drawBracket(fw, -fh, 1, -1);
      drawBracket(-fw, fh, -1, 1);
      drawBracket(fw, fh, 1, 1);
      for (let i = -fw + 20; i < fw - 20; i += 20) {
        geo.moveTo(i, -fh).lineTo(i, -fh - (i % 60 === 0 ? 12 : 6)).stroke({ color: primary, width: 1, alpha: 0.5 });
        geo.moveTo(i, fh).lineTo(i, fh + (i % 60 === 0 ? 12 : 6)).stroke({ color: primary, width: 1, alpha: 0.5 });
      }
      geo.circle(0, 0, radius * 0.1).stroke({ color: primary, width: 2, alpha: 0.4 });
      geo.moveTo(-radius * 0.15, 0).lineTo(radius * 0.15, 0).stroke({ color: primary, width: 1, alpha: 0.4 });
      geo.moveTo(0, -radius * 0.15).lineTo(0, radius * 0.15).stroke({ color: primary, width: 1, alpha: 0.4 });
      geo.rect(-fw, -fh + 20, 10, 40).fill({ color: primary, alpha: 0.5 });
      geo.rect(-fw, -fh + 65, 10, 15).fill({ color: primary, alpha: 0.3 });
      geo.rect(fw - 10, fh - 60, 10, 40).fill({ color: primary, alpha: 0.5 });
    } else if (geoVariant === 9) {
      const drawCube = (cx, cy, size2, alpha) => {
        const dy = size2 * 0.5;
        const dx = size2 * 0.866;
        geo.moveTo(cx, cy - size2).lineTo(cx + dx, cy - dy).lineTo(cx, cy).lineTo(cx - dx, cy - dy).lineTo(cx, cy - size2).fill({ color: primary, alpha: alpha * 0.15 }).stroke({ color: primary, width: 2, alpha: alpha * 0.8 });
        geo.moveTo(cx, cy).lineTo(cx + dx, cy - dy).lineTo(cx + dx, cy + size2 - dy).lineTo(cx, cy + size2).lineTo(cx, cy).fill({ color: primary, alpha: alpha * 0.3 }).stroke({ color: primary, width: 2, alpha: alpha * 0.8 });
        geo.moveTo(cx, cy).lineTo(cx - dx, cy - dy).lineTo(cx - dx, cy + size2 - dy).lineTo(cx, cy + size2).lineTo(cx, cy).fill({ color: primary, alpha: alpha * 0.05 }).stroke({ color: primary, width: 2, alpha: alpha * 0.8 });
      };
      drawCube(0, 0, radius * 0.35, 0.8);
      drawCube(radius * 0.4, -radius * 0.15, radius * 0.2, 0.5);
      drawCube(-radius * 0.45, radius * 0.25, radius * 0.25, 0.6);
      drawCube(0, radius * 0.45, radius * 0.15, 0.4);
      geo.moveTo(0, 0).lineTo(radius * 0.4, -radius * 0.15).stroke({ color: primary, width: 1, alpha: 0.3 });
      geo.moveTo(0, 0).lineTo(-radius * 0.45, radius * 0.25).stroke({ color: primary, width: 1, alpha: 0.3 });
    } else if (geoVariant === 10) {
      geo.circle(0, 0, radius * 0.75).stroke({ color: primary, width: 1, alpha: 0.2 });
      geo.circle(0, 0, radius * 0.73).stroke({ color: primary, width: 2, alpha: 0.1 });
      const nodes = [];
      for (let i = 0; i < 18; i++) {
        const r = radius * (0.1 + (seed * 17 + i * 23) % 65 / 100);
        const angle = (seed * 11 + i * 37) % 360 * Math.PI / 180;
        nodes.push({ x: Math.cos(angle) * r, y: Math.sin(angle) * r });
      }
      for (let i = 0; i < nodes.length; i++) {
        geo.circle(nodes[i].x, nodes[i].y, 3).fill({ color: primary, alpha: 0.7 });
        geo.circle(nodes[i].x, nodes[i].y, 6).stroke({ color: primary, width: 1, alpha: 0.3 });
        for (let j = i + 1; j < nodes.length; j++) {
          const dist = Math.hypot(nodes[i].x - nodes[j].x, nodes[i].y - nodes[j].y);
          if (dist < radius * 0.45) {
            geo.moveTo(nodes[i].x, nodes[i].y).lineTo(nodes[j].x, nodes[j].y).stroke({ color: primary, width: 1, alpha: 0.4 * (1 - dist / (radius * 0.45)) });
          }
        }
      }
    } else if (geoVariant === 11) {
      const moonR = radius * 0.4;
      geo.moveTo(0, -moonR);
      geo.arc(0, 0, moonR, -Math.PI / 2, Math.PI / 2, false);
      geo.quadraticCurveTo(-moonR * 0.4, 0, 0, -moonR);
      geo.fill({ color: primary, alpha: 0.8 });
      geo.circle(0, 0, moonR).stroke({ color: primary, width: 1, alpha: 0.3 });
      const orbitR = radius * 0.65;
      geo.circle(0, 0, orbitR).stroke({ color: primary, width: 1, alpha: 0.2 });
      for (let i = 0; i < 8; i++) {
        const angle = i / 8 * Math.PI * 2 - Math.PI / 2;
        const mx = Math.cos(angle) * orbitR;
        const my = Math.sin(angle) * orbitR;
        geo.circle(mx, my, 8).stroke({ color: primary, width: 1, alpha: 0.5 });
        if (i === 0) {
        } else if (i === 4) {
          geo.circle(mx, my, 6).fill({ color: primary, alpha: 0.8 });
        } else {
          geo.moveTo(mx, my - 6).arc(mx, my, 6, -Math.PI / 2, Math.PI / 2, i > 4).lineTo(mx, my - 6).fill({ color: primary, alpha: 0.5 });
        }
      }
      const drawStar = (sx, sy, sr) => {
        geo.moveTo(sx, sy - sr).lineTo(sx + sr * 0.2, sy - sr * 0.2).lineTo(sx + sr, sy).lineTo(sx + sr * 0.2, sy + sr * 0.2).lineTo(sx, sy + sr).lineTo(sx - sr * 0.2, sy + sr * 0.2).lineTo(sx - sr, sy).lineTo(sx - sr * 0.2, sy - sr * 0.2).fill({ color: primary, alpha: 0.7 });
      };
      drawStar(-radius * 0.4, -radius * 0.5, 12);
      drawStar(radius * 0.5, -radius * 0.3, 8);
      drawStar(-radius * 0.2, radius * 0.5, 15);
    } else if (geoVariant === 12) {
      const petalLen = radius * 0.5;
      const drawPetal2 = (angle, length, width2, alpha) => {
        const cx = 0, cy = 0;
        const endX = cx + Math.cos(angle) * length;
        const endY = cy + Math.sin(angle) * length;
        const ctrlDist = length * 0.5;
        const lca = angle - width2;
        const c1x = cx + Math.cos(lca) * ctrlDist;
        const c1y = cy + Math.sin(lca) * ctrlDist;
        const rca = angle + width2;
        const c2x = cx + Math.cos(rca) * ctrlDist;
        const c2y = cy + Math.sin(rca) * ctrlDist;
        geo.moveTo(cx, cy);
        geo.quadraticCurveTo(c1x, c1y, endX, endY);
        geo.quadraticCurveTo(c2x, c2y, cx, cy);
        geo.stroke({ color: primary, width: 1, alpha });
        if (alpha > 0.5) geo.fill({ color: primary, alpha: alpha * 0.2 });
      };
      for (let layer = 0; layer < 4; layer++) {
        const petals = 8 + layer * 4;
        const currentLen = petalLen * (1 - layer * 0.2);
        const currentWidth = 0.3 - layer * 0.05;
        const offset = layer * (Math.PI / petals);
        for (let i = 0; i < petals; i++) {
          const angle = i / petals * Math.PI * 2 + offset;
          drawPetal2(angle, currentLen, currentWidth, 0.8 - layer * 0.15);
        }
      }
      geo.circle(0, 0, radius * 0.08).stroke({ color: primary, width: 2, alpha: 0.8 });
      geo.circle(0, 0, radius * 0.03).fill({ color: primary, alpha: 0.9 });
      const stemR = radius * 0.65;
      for (let i = 0; i < 3; i++) {
        const startAngle = i / 3 * Math.PI * 2;
        geo.moveTo(Math.cos(startAngle) * stemR, Math.sin(startAngle) * stemR);
        geo.bezierCurveTo(
          Math.cos(startAngle + 1) * stemR * 1.2,
          Math.sin(startAngle + 1) * stemR * 1.2,
          Math.cos(startAngle + 2) * stemR * 0.8,
          Math.sin(startAngle + 2) * stemR * 0.8,
          Math.cos(startAngle + 3) * stemR,
          Math.sin(startAngle + 3) * stemR
        ).stroke({ color: primary, width: 1, alpha: 0.4 });
        const lx = Math.cos(startAngle + 1.5) * stemR * 1.05;
        const ly = Math.sin(startAngle + 1.5) * stemR * 1.05;
        geo.circle(lx, ly, 4).fill({ color: primary, alpha: 0.6 });
      }
    } else if (geoVariant === 13) {
      const soflR = radius * 0.25;
      const drawIntersectingCircle = (cx, cy, alpha) => {
        geo.circle(cx, cy, soflR).stroke({ color: primary, width: 1.5, alpha });
      };
      drawIntersectingCircle(0, 0, 0.35);
      for (let i = 0; i < 6; i++) {
        const angle = i / 6 * Math.PI * 2;
        const cx = Math.cos(angle) * soflR;
        const cy = Math.sin(angle) * soflR;
        drawIntersectingCircle(cx, cy, 0.2);
      }
      for (let i = 0; i < 12; i++) {
        const angle = i / 12 * Math.PI * 2;
        const dist = i % 2 === 0 ? soflR * 2 : soflR * Math.sqrt(3);
        const cx = Math.cos(angle) * dist;
        const cy = Math.sin(angle) * dist;
        geo.circle(cx, cy, soflR).stroke({ color: secondary, width: 1, alpha: 0.1 });
      }
      geo.circle(0, 0, soflR * 3).stroke({ color: primary, width: 1.5, alpha: 0.2 });
      geo.circle(0, 0, soflR * 3.1).stroke({ color: secondary, width: 1, alpha: 0.08 });
      for (let i = 0; i < 12; i++) {
        const angle = i / 12 * Math.PI * 2;
        geo.moveTo(Math.cos(angle) * soflR * 0.5, Math.sin(angle) * soflR * 0.5).lineTo(Math.cos(angle) * soflR * 3, Math.sin(angle) * soflR * 3).stroke({ color: secondary, width: 1, alpha: 0.05 });
      }
    } else if (geoVariant === 14) {
      const direction = seed % 2 === 0 ? 1 : -1;
      drawSonnetSolidCuboid(geo, radius * 0.18 * direction, radius * 0.03, radius * 0.62, radius * 0.7, radius * 0.22 * direction, -radius * 0.16, primary, 0.34);
      drawSonnetSolidCuboid(geo, -radius * 0.48 * direction, radius * 0.24, radius * 0.28, radius * 0.38, radius * 0.12 * direction, -radius * 0.09, primary, 0.24);
      drawSonnetSolidCuboid(geo, radius * 0.55 * direction, -radius * 0.3, radius * 0.2, radius * 0.26, radius * 0.09 * direction, -radius * 0.07, primary, 0.2);
    } else if (geoVariant === 15) {
      const direction = seed % 2 === 0 ? 1 : -1;
      drawSonnetTriangularPrism(geo, -radius * 0.12 * direction, radius * 0.02, radius * 0.72, radius * 0.68, radius * 0.18 * direction, -radius * 0.13, primary, 0.34);
      drawSonnetTriangularPrism(geo, radius * 0.48 * direction, radius * 0.26, radius * 0.28, radius * 0.25, -radius * 0.08 * direction, -radius * 0.06, primary, 0.22);
      drawSonnetTriangularPrism(geo, -radius * 0.5 * direction, -radius * 0.3, radius * 0.2, radius * 0.18, radius * 0.06 * direction, -radius * 0.05, primary, 0.18);
    } else if (geoVariant === 16) {
      const direction = seed % 2 === 0 ? 1 : -1;
      drawSonnetHexagonalPrism(geo, radius * 0.12 * direction, 0, radius * 0.68, radius * 0.72, radius * 0.2 * direction, -radius * 0.14, primary, 0.32);
      drawSonnetHexagonalPrism(geo, -radius * 0.48 * direction, radius * 0.27, radius * 0.25, radius * 0.28, radius * 0.07 * direction, -radius * 0.05, primary, 0.2);
      drawSonnetHexagonalPrism(geo, radius * 0.52 * direction, -radius * 0.3, radius * 0.18, radius * 0.2, -radius * 0.06 * direction, -radius * 0.045, primary, 0.17);
    } else if (geoVariant === 17) {
      const direction = seed % 2 === 0 ? 1 : -1;
      drawSonnetTrapezoidPrism(geo, radius * 0.12 * direction, radius * 0.04, radius * 0.3, radius * 0.68, radius * 0.62, radius * 0.18 * direction, -radius * 0.13, primary, 0.34);
      drawSonnetTrapezoidPrism(geo, -radius * 0.42 * direction, radius * 0.28, radius * 0.2, radius * 0.38, radius * 0.22, radius * 0.08 * direction, -radius * 0.06, primary, 0.21);
      drawSonnetTrapezoidPrism(geo, radius * 0.5 * direction, -radius * 0.3, radius * 0.2, radius * 0.12, radius * 0.22, -radius * 0.06 * direction, -radius * 0.05, primary, 0.18);
    } else {
      drawAdditionalSonnetShotMg({
        target: geo,
        variant: geoVariant,
        radius,
        width,
        height,
        seed,
        primary,
        secondary
      });
    }
    const keepsUpright = [6, 8, 9, 14, 15, 16, 17, 20, 22, 23].includes(geoVariant) || geoVariant >= SONNET_THEMED_GEO_VARIANT_START;
    if (!keepsUpright) {
      geo.rotation = seed * 13 % 360 * (Math.PI / 180);
    } else if (geoVariant === 8) {
      geo.rotation = resolveSonnetHudRotationQuarterTurns(seed) * (Math.PI / 2);
    }
    container.addChild(geo.display);
    fixedGeoLayer = new Container();
    const fixedGeo = new Graphics();
    fixedGeo.rect(-radius * 0.4, -radius * 0.2, radius * 0.6, radius * 0.15).fill({ color: primary, alpha: 0.7 });
    fixedGeo.rect(-radius * 0.1, radius * 0.1, radius * 0.5, radius * 0.3).stroke({ color: primary, width: 2, alpha: 0.6 });
    fixedGeoLayer.addChild(fixedGeo);
    drawHatching(-radius * 0.3, -radius * 0.4, radius * 0.4, radius * 0.25, 6, fixedGeoLayer);
    container.addChild(fixedGeoLayer);
  } else if (kind === "editorial-column") {
    for (let i = 1; i <= 6; i++) {
      const x = -hw + width * (i / 7);
      bg.moveTo(x, -hh).lineTo(x, hh).stroke({ color: primary, width: 1, alpha: 0.15 });
    }
    for (let i = 1; i <= 4; i++) {
      const y = -hh + height * (i / 5);
      bg.moveTo(-hw, y).lineTo(hw, y).stroke({ color: primary, width: 1, alpha: 0.15 });
    }
    bg.rect(-hw + width * 0.2, -hh + height * 0.2, width * 0.6, height * 0.6).stroke({ color: primary, width: 4, alpha: 0.5 });
  } else {
    for (let i = 0; i < 5; i++) {
      const size2 = 10 + seed % (i + 1) * 5;
      bg.rect(
        -hw + width * (0.2 + (seed * 11 + i) % 60 / 100),
        -hh + height * (0.2 + (seed * 17 + i) % 60 / 100),
        size2,
        size2
      ).fill({ color: primary, alpha: 0.4 });
    }
  }
  container.bg = bg;
  container.bgLayer = bg.display;
  if (geo) {
    container.geo = geo;
    container.geoLayer = geo.display;
  }
  container.fixedGeoLayer = fixedGeoLayer;
  container.addChild(bg.display);
  const particleLayer = new Container();
  const particleCount = kind === "type-impact" ? 24 : 12;
  const availableIconTextures = Array.from(iconTextures.values());
  const iconParticleIndices = buildSonnetIconParticleIndices(
    availableIconTextures.length,
    particleCount,
    seed
  );
  const hasIcons = availableIconTextures.length > 0;
  const iconAnimations = [];
  let smoothedIconAudio = 0;
  for (let i = 0; i < particleCount; i++) {
    const pSize = 4 + (seed + i) % 12;
    const iconTextureIndex = iconParticleIndices[i];
    const type = iconTextureIndex === null ? (seed + i) % 3 : 3;
    let p;
    if (type === 3 && hasIcons && iconTextureIndex !== null) {
      const tex = availableIconTextures[iconTextureIndex];
      if (tex) {
        const s = new Sprite(tex);
        s.anchor.set(0.5);
        s.width = pSize * 7;
        s.height = pSize * 7;
        const iconSeed = Math.abs(seed + i * 17);
        const baseScale = s.scale.x;
        s.alpha = 0;
        iconAnimations.push({
          node: s,
          baseScale,
          baseAlpha: 0.85,
          entryPhase: 0,
          preferredDuration: 0.62 + iconSeed % 4 * 0.08,
          phase: iconSeed % 31 * 0.2
        });
        p = s;
      } else {
        const g = new Graphics();
        g.rect(-pSize / 2, -pSize / 2, pSize, pSize).fill({ color: primary, alpha: 0.6 });
        p = g;
      }
    } else {
      const g = new Graphics();
      if (type === 0 || type === 3 && !hasIcons) {
        g.rect(-pSize / 2, -pSize / 2, pSize, pSize).fill({ color: i % 2 === 0 ? primary : secondary, alpha: 0.6 });
      } else if (type === 1) {
        g.moveTo(0, -pSize).lineTo(pSize, 0).lineTo(0, pSize).lineTo(-pSize, 0).fill({ color: primary, alpha: 0.5 });
      } else {
        g.moveTo(0, -pSize * 1.5).quadraticCurveTo(0, 0, pSize * 1.5, 0).quadraticCurveTo(0, 0, 0, pSize * 1.5).quadraticCurveTo(0, 0, -pSize * 1.5, 0).quadraticCurveTo(0, 0, 0, -pSize * 1.5).fill({ color: primary, alpha: 0.8 });
      }
      p = g;
    }
    p.position.set(
      -hw + width * ((seed * 31 + i * 47) % 100 / 100),
      -hh + height * ((seed * 73 + i * 19) % 100 / 100)
    );
    p.rotation = (seed + i * 13) % 360 * Math.PI / 180;
    particleLayer.addChild(p);
  }
  iconAnimations.forEach((icon, index) => {
    icon.entryPhase = resolveSonnetIconEntryPhase(index, iconAnimations.length);
  });
  container.addChild(particleLayer);
  container.particleLayer = particleLayer;
  container.updateTime = (time, cues, startTime, endTime, audioBass = 0, audioPower = 0, audioVocal = 0) => {
    let targetFinishTime = endTime;
    if (cues.length > 0) {
      const lastWordStart = cues[cues.length - 1].at;
      targetFinishTime = Math.max(
        lastWordStart,
        startTime + (endTime - startTime) * 0.95
      );
    } else {
      targetFinishTime = startTime + (endTime - startTime) * 0.95;
    }
    targetFinishTime = Math.min(endTime, targetFinishTime);
    const drawDuration = Math.max(1, targetFinishTime - startTime);
    const rawProgress = Math.min(1, Math.max(0, (time - startTime) / drawDuration));
    const c = container;
    if (c.geo) c.geo.update(rawProgress);
    if (c.bg) c.bg.update(rawProgress);
    const audioEnergy = normalizeAudioLevel(audioBass) * 0.34 + normalizeAudioLevel(audioVocal) * 0.52 + normalizeAudioLevel(audioPower) * 0.14;
    const gatedEnergy = Math.max(0, (audioEnergy - 0.08) / 0.92);
    const targetIconAudio = Math.min(1, Math.pow(gatedEnergy, 0.68) * 1.35);
    const smoothing = targetIconAudio > smoothedIconAudio ? 0.34 : 0.16;
    smoothedIconAudio += (targetIconAudio - smoothedIconAudio) * smoothing;
    const sceneDuration = Math.max(0.01, endTime - startTime);
    iconAnimations.forEach((icon) => {
      const entryDuration = resolveSonnetIconEntryDuration(sceneDuration, icon.preferredDuration);
      const entryDelay = resolveSonnetIconEntryDelay(icon.entryPhase, sceneDuration, entryDuration);
      const entryProgress = Math.min(
        1,
        Math.max(0, (time - startTime - entryDelay) / entryDuration)
      );
      const entryEased = 1 - Math.pow(1 - entryProgress, 3);
      const loopPulse = (Math.sin((time - startTime) * Math.PI * 0.7 + icon.phase) + 1) * 0.5;
      const audioScale = 1 + smoothedIconAudio * 0.42;
      const loopScale = 1 + loopPulse * 0.025;
      icon.node.alpha = Math.min(
        1,
        icon.baseAlpha * entryEased * (0.72 + smoothedIconAudio * 0.38 + loopPulse * 0.03)
      );
      icon.node.scale.set(
        icon.baseScale * (0.72 + entryEased * 0.28) * audioScale * loopScale
      );
    });
  };
  return container;
};

// src/components/visualizer/sonnet/sonnetPostProcess.ts
var resolveSonnetPostProcessProfile = (_theme, tuning, staticMode) => {
  if (staticMode) return { glowStrength: 0, glowAlpha: 0, noise: 0, contrast: 1, glitchIntensity: 0 };
  return {
    glowStrength: 2.8 + tuning.typographyMotion * 1.8,
    glowAlpha: Math.min(0.62, 0.28 + tuning.typographyMotion * 0.12),
    noise: 0,
    // Removed noise per user request
    contrast: 1.2,
    // High contrast
    glitchIntensity: 1
    // Used during transitions
  };
};
var createSonnetHaloLayer = (pixi, profile) => {
  const layer = new pixi.Container();
  const filters = [];
  if (profile.glowStrength > 0) {
    const blur = new pixi.BlurFilter({
      strength: profile.glowStrength,
      quality: 2,
      kernelSize: 5,
      resolution: 0.75
    });
    layer.filters = [blur];
    layer.alpha = profile.glowAlpha;
    layer.blendMode = "screen";
    filters.push(blur);
  }
  return { layer, filters };
};
var applySonnetScenePostProcess = (pixi, container, profile, seed) => {
  const filters = [];
  if (profile.noise > 0) {
    const noise = new pixi.NoiseFilter({
      noise: profile.noise,
      seed: seed % 1e4 / 1e4,
      resolution: 0.75
    });
    filters.push(noise);
  }
  if (filters.length > 0) {
    container.filters = filters;
  }
  return filters;
};

// src/components/visualizer/sonnet/sonnetTypographyRoles.ts
var isSonnetEmphasisRole = (role) => role === "hero" || role === "semi-hero";
var getSonnetVisibleSegmentLength = (segment) => segment.graphemes.filter((item) => item.char.trim().length > 0).length;
var scoreSonnetHeroSegment = (segment) => {
  const lengthScore = Math.min(getSonnetVisibleSegmentLength(segment), 8) * 14;
  const durationScore = Math.min(2.5, Math.max(0, segment.endTime - segment.startTime)) * 18;
  return lengthScore + durationScore;
};
var findSonnetHeroSegmentIndex = (segments) => {
  let bestIndex = segments.findIndex((segment) => segment.isWordLike);
  let bestScore = -Infinity;
  segments.forEach((segment, index) => {
    if (!segment.isWordLike || getSonnetVisibleSegmentLength(segment) === 0) return;
    const score = scoreSonnetHeroSegment(segment);
    if (score > bestScore) {
      bestScore = score;
      bestIndex = index;
    }
  });
  return Math.max(0, bestIndex);
};
var findSonnetSemiHeroSegmentIndex = (segments, heroIndex) => {
  const beforeHero = segments.map((segment, index) => ({ segment, index })).filter(({ segment, index }) => index < heroIndex && segment.isWordLike && getSonnetVisibleSegmentLength(segment) > 0);
  if (beforeHero.length < 6) return -1;
  const candidates = beforeHero.slice(0, -2);
  let bestIndex = -1;
  let bestScore = -Infinity;
  candidates.forEach(({ segment, index }) => {
    const score = scoreSonnetHeroSegment(segment);
    if (score > bestScore) {
      bestScore = score;
      bestIndex = index;
    }
  });
  return bestIndex;
};

// src/components/visualizer/sonnet/sonnetPosterBlocksLayout.ts
var clamp = (value, min, max) => Math.min(max, Math.max(min, value));
var rectRight = (rect) => rect.x + rect.width;
var rectBottom = (rect) => rect.y + rect.height;
var intersects = (first, second) => first.x < rectRight(second) && rectRight(first) > second.x && first.y < rectBottom(second) && rectBottom(first) > second.y;
var contains = (outer, inner) => inner.x >= outer.x && inner.y >= outer.y && rectRight(inner) <= rectRight(outer) && rectBottom(inner) <= rectBottom(outer);
var subtractPlacement = (freeRects, placed, gap) => {
  const padded = {
    x: placed.x - gap,
    y: placed.y - gap,
    width: placed.width + gap * 2,
    height: placed.height + gap * 2
  };
  const split = [];
  freeRects.forEach((rect) => {
    if (!intersects(rect, padded)) {
      split.push(rect);
      return;
    }
    if (padded.x > rect.x) split.push({ ...rect, width: padded.x - rect.x });
    if (rectRight(padded) < rectRight(rect)) {
      split.push({ ...rect, x: rectRight(padded), width: rectRight(rect) - rectRight(padded) });
    }
    if (padded.y > rect.y) split.push({ ...rect, height: padded.y - rect.y });
    if (rectBottom(padded) < rectBottom(rect)) {
      split.push({ ...rect, y: rectBottom(padded), height: rectBottom(rect) - rectBottom(padded) });
    }
  });
  return split.filter((rect) => rect.width > gap && rect.height > gap).filter((rect, index, all) => !all.some((other, otherIndex) => otherIndex !== index && contains(other, rect)));
};
var candidateNoise = (seed, boxIndex, rect) => hashSonnetSeed(`${seed}:${boxIndex}:${Math.round(rect.x)}:${Math.round(rect.y)}`) / 4294967295;
var findPlacement = (box, boxIndex, freeRects, canvas, seed, scaleLimit, minimumScale) => {
  let best = null;
  for (const rect of freeRects) {
    const orientations = [{ width: box.measuredWidth, height: box.measuredHeight, vertical: false }];
    if (box.verticalMeasuredWidth && box.verticalMeasuredHeight) {
      orientations.push({
        width: box.verticalMeasuredWidth,
        height: box.verticalMeasuredHeight,
        vertical: true
      });
    }
    for (const orientation of orientations) {
      const scale = Math.min(scaleLimit, rect.width / orientation.width, rect.height / orientation.height);
      if (scale < minimumScale) continue;
      const width = orientation.width * scale;
      const height = orientation.height * scale;
      const readingOrder = (rect.y - canvas.y) * (canvas.width + 1) + (rect.x - canvas.x);
      const unusedRatio = (rect.width * rect.height - width * height) / Math.max(1, canvas.width * canvas.height);
      const orientationPenalty = orientation.vertical ? 0.04 : 0;
      const score = readingOrder + unusedRatio * 0.5 + orientationPenalty + candidateNoise(seed, boxIndex, rect) * 0.1;
      const placement = {
        box,
        scale,
        x: rect.x,
        y: rect.y,
        width,
        height,
        vertical: orientation.vertical
      };
      if (!best || score < best.score) best = { placement, score };
    }
  }
  return best?.placement ?? null;
};
var tryLayout = (boxes, canvas, gap, seed, scaleLimit) => {
  const primaryHero = boxes.filter((box) => box.isHero).sort((first, second) => second.measuredWidth * second.measuredHeight - first.measuredWidth * first.measuredHeight)[0] ?? boxes[0];
  const heroScale = Math.min(
    scaleLimit,
    canvas.width * 0.58 / primaryHero.measuredWidth,
    canvas.height * 0.46 / primaryHero.measuredHeight
  );
  const heroWidth = primaryHero.measuredWidth * heroScale;
  const heroHeight = primaryHero.measuredHeight * heroScale;
  const heroOnRight = (seed & 1) === 0;
  const hero = {
    box: primaryHero,
    scale: heroScale,
    x: heroOnRight ? rectRight(canvas) - heroWidth : canvas.x,
    y: canvas.y,
    width: heroWidth,
    height: heroHeight,
    vertical: false
  };
  const placements = [hero];
  let freeRects = subtractPlacement([canvas], hero, gap);
  const anchors = boxes.filter((box) => box !== primaryHero && (box.isHero || box.isSemiHero));
  const supports = boxes.filter((box) => box !== primaryHero && !box.isHero && !box.isSemiHero);
  const minimumScale = Math.max(0.16, scaleLimit * 0.56);
  for (const box of anchors) {
    const placement = findPlacement(
      box,
      boxes.indexOf(box),
      freeRects,
      canvas,
      seed,
      scaleLimit * 0.96,
      minimumScale
    );
    if (!placement) return null;
    placements.push(placement);
    freeRects = subtractPlacement(freeRects, placement, gap);
  }
  for (const box of supports) {
    const placement = findPlacement(
      box,
      boxes.indexOf(box),
      freeRects,
      canvas,
      seed,
      scaleLimit * 1.08,
      minimumScale
    );
    if (!placement) return null;
    placements.push(placement);
    freeRects = subtractPlacement(freeRects, placement, gap);
  }
  return placements;
};
var layoutSonnetPosterBlocks = (boxes, width, height, baseFontSize, seed = 0) => {
  if (boxes.length === 0) return { placements: [], width: 0, height: 0, gap: 0 };
  const gap = clamp(baseFontSize * 0.08, 2, 8);
  const canvas = {
    x: -width * 0.39,
    y: -height * 0.36,
    width: width * 0.78,
    height: height * 0.72
  };
  let planned = null;
  for (const scaleLimit of [1.12, 1, 0.88, 0.76, 0.64, 0.52, 0.42, 0.34, 0.28]) {
    planned = tryLayout(boxes, canvas, gap, seed, scaleLimit);
    if (planned) break;
  }
  if (!planned) planned = tryLayout(boxes, canvas, 0, seed, 0.5) ?? [];
  planned.forEach((placement) => {
    const { box } = placement;
    box.fontScale *= placement.scale;
    box.measuredWidth = placement.width;
    box.measuredHeight = placement.height;
    if (placement.vertical && box.verticalDisplayText) box.displayText = box.verticalDisplayText;
    box.x = placement.x + placement.width / 2;
    box.y = placement.y + placement.height / 2;
    box.rotation = 0;
    box.vertical = placement.vertical;
    box.layoutDirection = placement.vertical ? "vertical" : "horizontal";
    box.enterX = (placement.x < 0 ? -1 : 1) * Math.min(28, baseFontSize * 0.45);
    box.enterY = Math.min(18, baseFontSize * 0.25);
  });
  return { placements: boxes, width: canvas.width, height: canvas.height, gap };
};

// src/components/visualizer/sonnet/sonnetTypographyLayout.ts
var isSonnetLayoutSegment = (segment) => segment.text.trim().length > 0;
var CJK_TEXT = /[\u4e00-\u9fff\u3040-\u30ff\uac00-\ud7af]/u;
var shouldRotateNonCjkSegment = (segment, vertical) => vertical && segment.graphemes.filter((item) => item.char.trim().length > 0).length > 1 && !CJK_TEXT.test(segment.text);
var verticalText = (segment) => (segment.graphemes.length ? segment.graphemes.map((item) => item.char) : Array.from(segment.text)).join("\n");
var measureText = (text, fontSpec, fontSize) => {
  try {
    const layout = layoutWithLines(prepareWithSegments(text || " ", fontSpec), 99999, fontSize * 1.2);
    return layout.lines[0]?.width ?? text.length * fontSize * 0.6;
  } catch {
    return text.length * fontSize * 0.6;
  }
};
var resolveSonnetTypographyLayout = ({
  lines,
  shotKind,
  width,
  height,
  baseFontSize,
  fontFamily
}) => {
  const segments = lines.flat();
  let offset = 0;
  const heroIndices = [];
  const semiHeroIndices = [];
  lines.forEach((lineSegs) => {
    const localHero = findSonnetHeroSegmentIndex(lineSegs);
    const globalHero = offset + localHero;
    const localSemiHero = findSonnetSemiHeroSegmentIndex(lineSegs, localHero);
    heroIndices.push(globalHero);
    if (localSemiHero >= 0) semiHeroIndices.push(offset + localSemiHero);
    offset += lineSegs.length;
  });
  const heroIndex = findSonnetHeroSegmentIndex(segments);
  const midpoints = segments.map((segment) => (segment.startTime + segment.endTime) / 2);
  const timelineStart = Math.min(...midpoints);
  const timelineEnd = Math.max(...midpoints);
  const timelineDuration = timelineEnd - timelineStart;
  const phases = midpoints.map((midpoint, index) => timelineDuration > 1e-3 ? (midpoint - timelineStart) / timelineDuration : index / Math.max(1, segments.length - 1));
  const heroPhase = phases[heroIndex] ?? 0.5;
  const layoutVariantSeed = segments.reduce((acc, seg) => acc + (seg.text.trim().length || 1), 0) + segments.length;
  const posterLayoutSeed = hashSonnetSeed(segments.map((segment) => segment.text).join("\u241F"));
  let editorialVariant = layoutVariantSeed % 5;
  const ribbonVariant = layoutVariantSeed % 3;
  const tableauVariant = layoutVariantSeed % 4;
  const collageVariant = layoutVariantSeed % 3;
  let secondaryHeroIndex = -1;
  if (editorialVariant === 3 && segments.length > 2) {
    let bestScore = -Infinity;
    segments.forEach((segment, index) => {
      if (index === heroIndex || !segment.isWordLike || getSonnetVisibleSegmentLength(segment) === 0) return;
      const distanceBonus = Math.abs(index - heroIndex) > 1 ? 50 : 0;
      const score = scoreSonnetHeroSegment(segment) + distanceBonus;
      if (score > bestScore) {
        bestScore = score;
        secondaryHeroIndex = index;
      }
    });
    if (secondaryHeroIndex === -1) editorialVariant = 0;
  } else if (editorialVariant === 3) {
    editorialVariant = 0;
  } else if (editorialVariant === 4 && segments.length < 2) {
    editorialVariant = 2;
  }
  const boxes = segments.map((segment, index) => {
    const isHero = heroIndices.includes(index) || index === secondaryHeroIndex && shotKind === "editorial-column" && editorialVariant === 3;
    const isSemiHero = semiHeroIndices.includes(index) && !isHero;
    const isEmphasized = isHero || isSemiHero;
    let heroFontScale = 1;
    let supportFontScale = 1;
    let vertical = false;
    let rotation = 0;
    switch (shotKind) {
      case "editorial-column":
        if (editorialVariant === 3) {
          heroFontScale = 3.8;
          supportFontScale = 1.3;
          vertical = false;
        } else if (editorialVariant === 4) {
          heroFontScale = 4.2;
          supportFontScale = 1.25;
          vertical = isEmphasized;
        } else {
          heroFontScale = editorialVariant === 2 ? 3.2 : 4;
          supportFontScale = 1.2;
          vertical = isEmphasized && editorialVariant !== 2;
        }
        break;
      case "type-impact":
        heroFontScale = 5.5;
        supportFontScale = 1.5;
        break;
      case "fragment-collage":
        heroFontScale = 3.2;
        supportFontScale = 1.35;
        vertical = isSemiHero || index % 4 === 0;
        break;
      case "tracking-ribbon":
        heroFontScale = 3.5;
        supportFontScale = 1.5;
        break;
      case "mask-reveal":
        heroFontScale = 4.5;
        supportFontScale = 1.6;
        vertical = isEmphasized;
        break;
      case "poster-blocks":
        heroFontScale = 4.4;
        supportFontScale = 1.15;
        break;
      case "quiet-tableau":
      default:
        heroFontScale = 3;
        supportFontScale = 1.15;
        vertical = isEmphasized && (tableauVariant === 0 || tableauVariant === 1);
        break;
    }
    let fontScale = isHero ? heroFontScale : isSemiHero ? Math.max(supportFontScale * 1.35, heroFontScale * 0.72) : supportFontScale;
    const rotatesNonCjkSegment = shouldRotateNonCjkSegment(segment, vertical);
    if (rotatesNonCjkSegment) {
      vertical = false;
      rotation += Math.PI / 2;
    }
    const displayText = vertical ? verticalText(segment) : segment.text;
    const renderWeight = isEmphasized ? "900" : "700";
    let targetFontSize = baseFontSize * fontScale;
    const fontSpec = `${renderWeight} ${targetFontSize}px ${fontFamily}`;
    const horizontalAdvance = rotatesNonCjkSegment ? segment.graphemes.reduce((sum, item) => item.char.trim().length > 0 ? sum + Math.max(targetFontSize * 0.2, measureText(item.char, fontSpec, targetFontSize)) : sum, 0) : measureText(displayText, fontSpec, targetFontSize);
    let measuredWidth = rotatesNonCjkSegment ? targetFontSize * 1.2 : vertical ? targetFontSize * 1.1 : horizontalAdvance;
    let measuredHeight = rotatesNonCjkSegment ? horizontalAdvance : vertical ? targetFontSize * 1.1 * displayText.split("\n").length : targetFontSize * 1.2;
    const maxW = width * 0.82;
    const maxH = height * 0.82;
    let fitScale = 1;
    if (measuredWidth > maxW) fitScale = Math.min(fitScale, maxW / measuredWidth);
    if (measuredHeight > maxH) fitScale = Math.min(fitScale, maxH / measuredHeight);
    if (fitScale < 1) {
      targetFontSize *= fitScale;
      fontScale *= fitScale;
      measuredWidth *= fitScale;
      measuredHeight *= fitScale;
    }
    const posterVerticalDisplayText = shotKind === "poster-blocks" && CJK_TEXT.test(segment.text) ? verticalText(segment) : void 0;
    const posterVerticalMeasuredWidth = posterVerticalDisplayText ? targetFontSize * 1.1 : void 0;
    const posterVerticalMeasuredHeight = posterVerticalDisplayText ? targetFontSize * 1.1 * posterVerticalDisplayText.split("\n").length : void 0;
    return {
      index,
      isHero,
      isSemiHero,
      displayText,
      verticalDisplayText: posterVerticalDisplayText,
      verticalMeasuredWidth: posterVerticalMeasuredWidth,
      verticalMeasuredHeight: posterVerticalMeasuredHeight,
      fontScale,
      vertical,
      layoutDirection: "horizontal",
      rotation,
      measuredWidth,
      measuredHeight,
      timingPhase: phases[index],
      relativePhase: phases[index] - heroPhase,
      role: void 0,
      x: 0,
      y: 0,
      enterX: 0,
      enterY: 0
    };
  });
  const heroBox = boxes[heroIndex];
  if (heroBox) {
    const verticalStackGap = Math.max(24, baseFontSize * 0.32);
    if (shotKind === "editorial-column") {
      if (editorialVariant === 0) heroBox.x = -width * 0.15;
      else if (editorialVariant === 1) heroBox.x = width * 0.15;
      else if (editorialVariant === 4) heroBox.x = -width * 0.2;
      else heroBox.x = 0;
      heroBox.y = editorialVariant === 2 ? -height * 0.25 : 0;
    } else if (shotKind === "quiet-tableau") {
      heroBox.x = 0;
      heroBox.y = tableauVariant === 2 || tableauVariant === 3 ? 0 : -height * 0.1;
    } else {
      heroBox.x = 0;
      heroBox.y = 0;
    }
    if (shotKind === "poster-blocks") {
      layoutSonnetPosterBlocks(boxes, width, height, baseFontSize, posterLayoutSeed);
    } else if (shotKind === "quiet-tableau") {
      if (tableauVariant === 0) {
        boxes.forEach((box) => {
          box.layoutDirection = "vertical";
        });
        let currentY = heroBox.y - heroBox.measuredHeight / 2 - verticalStackGap;
        for (let i = heroIndex - 1; i >= 0; i--) {
          const box = boxes[i];
          box.x = heroBox.x;
          box.y = currentY - box.measuredHeight / 2;
          currentY -= box.measuredHeight + verticalStackGap;
          box.enterX = 0;
          box.enterY = 20;
        }
        currentY = heroBox.y + heroBox.measuredHeight / 2 + verticalStackGap;
        for (let i = heroIndex + 1; i < boxes.length; i++) {
          const box = boxes[i];
          box.x = heroBox.x;
          box.y = currentY + box.measuredHeight / 2;
          currentY += box.measuredHeight + verticalStackGap;
          box.enterX = 0;
          box.enterY = -20;
        }
      } else if (tableauVariant === 1) {
        boxes.forEach((box) => {
          box.layoutDirection = "vertical";
        });
        let currentY = heroBox.y - heroBox.measuredHeight / 2 - verticalStackGap;
        for (let i = heroIndex - 1; i >= 0; i--) {
          const box = boxes[i];
          box.x = heroBox.x - heroBox.measuredWidth / 2 + box.measuredWidth / 2;
          box.y = currentY - box.measuredHeight / 2;
          currentY -= box.measuredHeight + verticalStackGap;
          box.enterX = 20;
          box.enterY = 0;
        }
        currentY = heroBox.y + heroBox.measuredHeight / 2 + verticalStackGap;
        for (let i = heroIndex + 1; i < boxes.length; i++) {
          const box = boxes[i];
          box.x = heroBox.x - heroBox.measuredWidth / 2 + box.measuredWidth / 2;
          box.y = currentY + box.measuredHeight / 2;
          currentY += box.measuredHeight + verticalStackGap;
          box.enterX = -20;
          box.enterY = 0;
        }
      } else if (tableauVariant === 2) {
        boxes.forEach((box) => {
          box.layoutDirection = "horizontal";
        });
        let currentY = heroBox.y - heroBox.measuredHeight / 2 - verticalStackGap;
        for (let i = heroIndex - 1; i >= 0; i--) {
          const box = boxes[i];
          box.x = heroBox.x;
          box.y = currentY - box.measuredHeight / 2;
          currentY -= box.measuredHeight + verticalStackGap;
          box.enterX = 0;
          box.enterY = 20;
        }
        currentY = heroBox.y + heroBox.measuredHeight / 2 + verticalStackGap;
        for (let i = heroIndex + 1; i < boxes.length; i++) {
          const box = boxes[i];
          box.x = heroBox.x;
          box.y = currentY + box.measuredHeight / 2;
          currentY += box.measuredHeight + verticalStackGap;
          box.enterX = 0;
          box.enterY = -20;
        }
      } else {
        boxes.forEach((box) => {
          box.layoutDirection = "horizontal";
        });
        let currentY = heroBox.y - heroBox.measuredHeight / 2 - verticalStackGap;
        for (let i = heroIndex - 1; i >= 0; i--) {
          const box = boxes[i];
          const offsetX = (i % 2 === 0 ? 1 : -1) * 35;
          box.x = heroBox.x + offsetX;
          box.y = currentY - box.measuredHeight / 2;
          currentY -= box.measuredHeight + verticalStackGap;
          box.enterX = offsetX > 0 ? 30 : -30;
          box.enterY = 0;
        }
        currentY = heroBox.y + heroBox.measuredHeight / 2 + verticalStackGap;
        for (let i = heroIndex + 1; i < boxes.length; i++) {
          const box = boxes[i];
          const offsetX = (i % 2 === 0 ? 1 : -1) * 35;
          box.x = heroBox.x + offsetX;
          box.y = currentY + box.measuredHeight / 2;
          currentY += box.measuredHeight + verticalStackGap;
          box.enterX = offsetX > 0 ? 30 : -30;
          box.enterY = 0;
        }
      }
    } else if (shotKind === "tracking-ribbon") {
      boxes.forEach((box) => {
        box.layoutDirection = "horizontal";
      });
      if (ribbonVariant === 0) {
        let currentX = heroBox.x - heroBox.measuredWidth / 2 - 15;
        for (let i = heroIndex - 1; i >= 0; i--) {
          const box = boxes[i];
          box.x = currentX - box.measuredWidth / 2;
          box.y = heroBox.y + (i % 2 === 0 ? 10 : -10);
          currentX -= box.measuredWidth + 15;
          box.enterX = 30;
          box.enterY = 0;
        }
        currentX = heroBox.x + heroBox.measuredWidth / 2 + 15;
        for (let i = heroIndex + 1; i < boxes.length; i++) {
          const box = boxes[i];
          box.x = currentX + box.measuredWidth / 2;
          box.y = heroBox.y + (i % 2 === 0 ? 10 : -10);
          currentX += box.measuredWidth + 15;
          box.enterX = -30;
          box.enterY = 0;
        }
      } else if (ribbonVariant === 1) {
        let currentX = heroBox.x - heroBox.measuredWidth / 2 - 25;
        for (let i = heroIndex - 1; i >= 0; i--) {
          const box = boxes[i];
          box.x = currentX - box.measuredWidth / 2;
          box.y = heroBox.y + heroBox.measuredHeight / 2 - box.measuredHeight / 2;
          currentX -= box.measuredWidth + 25;
          box.enterX = 30;
          box.enterY = 0;
        }
        currentX = heroBox.x + heroBox.measuredWidth / 2 + 25;
        for (let i = heroIndex + 1; i < boxes.length; i++) {
          const box = boxes[i];
          box.x = currentX + box.measuredWidth / 2;
          box.y = heroBox.y + heroBox.measuredHeight / 2 - box.measuredHeight / 2;
          currentX += box.measuredWidth + 25;
          box.enterX = -30;
          box.enterY = 0;
        }
      } else {
        let currentX = heroBox.x - heroBox.measuredWidth / 2 - 35;
        for (let i = heroIndex - 1; i >= 0; i--) {
          const box = boxes[i];
          box.x = currentX - box.measuredWidth / 2;
          box.y = heroBox.y - heroBox.measuredHeight / 2 + box.measuredHeight / 2;
          currentX -= box.measuredWidth + 35;
          box.enterX = 20;
          box.enterY = 0;
        }
        currentX = heroBox.x + heroBox.measuredWidth / 2 + 35;
        for (let i = heroIndex + 1; i < boxes.length; i++) {
          const box = boxes[i];
          box.x = currentX + box.measuredWidth / 2;
          box.y = heroBox.y - heroBox.measuredHeight / 2 + box.measuredHeight / 2;
          currentX += box.measuredWidth + 35;
          box.enterX = -20;
          box.enterY = 0;
        }
      }
    } else if (shotKind === "editorial-column") {
      if (editorialVariant === 0) {
        boxes.forEach((box) => {
          box.layoutDirection = "vertical";
        });
        let currentYLeft = heroBox.y - heroBox.measuredHeight / 2 + 20;
        let currentYRight = heroBox.y - heroBox.measuredHeight / 2 + 20;
        for (let i = heroIndex - 1; i >= 0; i--) {
          const box = boxes[i];
          box.x = heroBox.x - heroBox.measuredWidth / 2 - box.measuredWidth / 2 - 25;
          box.y = currentYLeft + box.measuredHeight / 2;
          currentYLeft += box.measuredHeight + verticalStackGap;
          box.enterX = 20;
          box.enterY = 0;
        }
        for (let i = heroIndex + 1; i < boxes.length; i++) {
          const box = boxes[i];
          box.x = heroBox.x + heroBox.measuredWidth / 2 + box.measuredWidth / 2 + 25;
          box.y = currentYRight + box.measuredHeight / 2;
          currentYRight += box.measuredHeight + verticalStackGap;
          box.enterX = -20;
          box.enterY = 0;
        }
      } else if (editorialVariant === 1) {
        boxes.forEach((box) => {
          box.layoutDirection = "vertical";
        });
        let currentY = heroBox.y - heroBox.measuredHeight / 2 + 10;
        for (let i = 0; i < boxes.length; i++) {
          if (i === heroIndex) continue;
          const box = boxes[i];
          box.x = heroBox.x - heroBox.measuredWidth / 2 - box.measuredWidth / 2 - 40;
          box.y = currentY + box.measuredHeight / 2;
          currentY += box.measuredHeight + verticalStackGap;
          box.enterX = -20;
          box.enterY = 0;
        }
      } else if (editorialVariant === 2) {
        boxes.forEach((box) => {
          box.layoutDirection = "vertical";
        });
        let currentXLeft = heroBox.x - heroBox.measuredWidth * 0.25 - 10;
        let currentXRight = heroBox.x + heroBox.measuredWidth * 0.25 + 10;
        let leftY = heroBox.y + heroBox.measuredHeight / 2 + 40;
        let rightY = heroBox.y + heroBox.measuredHeight / 2 + 40;
        for (let i = 0; i < boxes.length; i++) {
          if (i === heroIndex) continue;
          const box = boxes[i];
          if (i % 2 === 0) {
            box.x = currentXLeft - box.measuredWidth / 2;
            box.y = leftY + box.measuredHeight / 2;
            leftY += box.measuredHeight + verticalStackGap;
            box.enterX = -20;
            box.enterY = 0;
          } else {
            box.x = currentXRight + box.measuredWidth / 2;
            box.y = rightY + box.measuredHeight / 2;
            rightY += box.measuredHeight + verticalStackGap;
            box.enterX = 20;
            box.enterY = 0;
          }
        }
      } else if (editorialVariant === 3) {
        boxes.forEach((box) => {
          box.layoutDirection = "horizontal";
        });
        const firstHero = Math.min(heroIndex, secondaryHeroIndex);
        let currentX1 = 0;
        let line1Y = heroBox.y - heroBox.measuredHeight * 0.45 - 15;
        for (let i = 0; i <= firstHero; i++) {
          const box = boxes[i];
          box.y = line1Y;
          box.x = currentX1 + box.measuredWidth / 2;
          currentX1 += box.measuredWidth + 20;
          box.enterX = 30;
          box.enterY = 0;
        }
        const line1Width = currentX1 - 20;
        for (let i = 0; i <= firstHero; i++) {
          boxes[i].x -= line1Width / 2;
        }
        let currentX2 = 0;
        let line2Y = heroBox.y + heroBox.measuredHeight * 0.45 + 15;
        for (let i = firstHero + 1; i < boxes.length; i++) {
          const box = boxes[i];
          box.y = line2Y;
          box.x = currentX2 + box.measuredWidth / 2;
          currentX2 += box.measuredWidth + 20;
          box.enterX = -30;
          box.enterY = 0;
        }
        const line2Width = currentX2 > 0 ? currentX2 - 20 : 0;
        for (let i = firstHero + 1; i < boxes.length; i++) {
          boxes[i].x -= line2Width / 2;
        }
        const offsetAmount = Math.max(line1Width, line2Width) * 0.12;
        for (let i = 0; i <= firstHero; i++) boxes[i].x -= offsetAmount;
        for (let i = firstHero + 1; i < boxes.length; i++) boxes[i].x += offsetAmount;
      } else if (editorialVariant === 4) {
        boxes.forEach((box, i) => {
          box.layoutDirection = i === heroIndex ? "vertical" : "horizontal";
        });
        const heroOnRight = heroIndex === boxes.length - 1;
        heroBox.x = heroOnRight ? width * 0.2 : -width * 0.2;
        const startX = heroOnRight ? heroBox.x - heroBox.measuredWidth / 2 - 35 : heroBox.x + heroBox.measuredWidth / 2 + 35;
        const startY = heroBox.y - heroBox.measuredHeight / 2 + 10;
        let currentX = startX;
        let currentY = startY;
        const maxRowWidth = width * 0.38;
        for (let i = 0; i < boxes.length; i++) {
          if (i === heroIndex) continue;
          const box = boxes[i];
          if (heroOnRight) {
            if (currentX < startX && startX - currentX + box.measuredWidth > maxRowWidth) {
              currentX = startX;
              currentY += baseFontSize * 1.5 + 10;
            }
            box.x = currentX - box.measuredWidth / 2;
            box.y = currentY + box.measuredHeight / 2;
            currentX -= box.measuredWidth + 18;
            box.enterX = -25;
            box.enterY = 0;
          } else {
            if (currentX > startX && currentX + box.measuredWidth - startX > maxRowWidth) {
              currentX = startX;
              currentY += baseFontSize * 1.5 + 10;
            }
            box.x = currentX + box.measuredWidth / 2;
            box.y = currentY + box.measuredHeight / 2;
            currentX += box.measuredWidth + 18;
            box.enterX = 25;
            box.enterY = 0;
          }
        }
      }
    } else if (shotKind === "fragment-collage") {
      const count = Math.max(1, boxes.length - 1);
      let supportIndex = 0;
      for (let i = 0; i < boxes.length; i++) {
        if (i === heroIndex) continue;
        const box = boxes[i];
        const baseRadius = Math.max(heroBox.measuredWidth, heroBox.measuredHeight) / 2;
        let radius = baseRadius + 45;
        let angle = supportIndex / count * Math.PI * 2 + Math.PI / 4;
        if (collageVariant === 1) {
          radius += 35 + supportIndex / count * 150;
          angle += supportIndex * 0.18;
        } else if (collageVariant === 2) {
          const isOuter = supportIndex % 2 === 1;
          const ringIndex = Math.floor(supportIndex / 2);
          const ringCount = Math.max(1, Math.ceil(count / 2));
          const basePhase = ringIndex / ringCount * Math.PI * 2;
          angle = isOuter ? basePhase + Math.PI / ringCount + Math.PI / 4 : basePhase + Math.PI / 4;
          radius += isOuter ? 140 : 50;
        } else {
          radius += 45 + supportIndex * 23 % 90;
        }
        supportIndex += 1;
        box.x = heroBox.x + Math.cos(angle) * (radius + box.measuredWidth / 2);
        box.y = heroBox.y + Math.sin(angle) * (radius * 0.65 + box.measuredHeight / 2);
        box.rotation = 0;
        box.layoutDirection = Math.abs(Math.cos(angle)) >= Math.abs(Math.sin(angle)) ? "vertical" : "horizontal";
        box.enterX = Math.cos(angle) * -60;
        box.enterY = Math.sin(angle) * -60;
      }
    } else {
      const beforeCount = heroIndex;
      const topCount = Math.floor(beforeCount / 2);
      const afterCount = boxes.length - 1 - heroIndex;
      const rightCount = Math.ceil(afterCount / 2);
      let currentXLeft = heroBox.x - heroBox.measuredWidth / 2 - 25;
      for (let i = heroIndex - 1; i >= topCount; i--) {
        const box = boxes[i];
        box.layoutDirection = "horizontal";
        box.x = currentXLeft - box.measuredWidth / 2;
        box.y = heroBox.y + (i % 2 === 0 ? 10 : -10);
        currentXLeft -= box.measuredWidth + 25;
        box.enterX = -30;
        box.enterY = 0;
      }
      let currentYTop = heroBox.y - heroBox.measuredHeight / 2 - 20;
      for (let i = topCount - 1; i >= 0; i--) {
        const box = boxes[i];
        box.layoutDirection = "vertical";
        box.x = heroBox.x + (i % 2 === 0 ? 15 : -15);
        box.y = currentYTop - box.measuredHeight / 2;
        currentYTop -= box.measuredHeight + verticalStackGap;
        box.enterX = 0;
        box.enterY = -30;
      }
      let currentXRight = heroBox.x + heroBox.measuredWidth / 2 + 25;
      for (let i = heroIndex + 1; i <= heroIndex + rightCount; i++) {
        const box = boxes[i];
        box.layoutDirection = "horizontal";
        box.x = currentXRight + box.measuredWidth / 2;
        box.y = heroBox.y + (i % 2 === 0 ? 10 : -10);
        currentXRight += box.measuredWidth + 25;
        box.enterX = 30;
        box.enterY = 0;
      }
      let currentYBottom = heroBox.y + heroBox.measuredHeight / 2 + 20;
      for (let i = heroIndex + rightCount + 1; i < boxes.length; i++) {
        const box = boxes[i];
        box.layoutDirection = "vertical";
        box.x = heroBox.x + (i % 2 === 0 ? 15 : -15);
        box.y = currentYBottom + box.measuredHeight / 2;
        currentYBottom += box.measuredHeight + verticalStackGap;
        box.enterX = 0;
        box.enterY = 30;
      }
    }
    heroBox.enterX = 0;
    heroBox.enterY = height * 0.15;
    const decorations = [];
    if (shotKind !== "quiet-tableau" && shotKind !== "poster-blocks") {
      const allHeroes = boxes.filter((b) => b.isHero);
      allHeroes.forEach((hBox, idx) => {
        decorations.push({
          ...hBox,
          isHero: false,
          role: "decoration",
          fontScale: Math.max(2.8, Math.min(hBox.fontScale * 3.5, 5.5)),
          vertical: false,
          x: hBox.x - width * (0.1 - idx * 0.03),
          y: hBox.y - height * (0.05 - idx * 0.02),
          rotation: -0.15 + (idx % 2 === 0 ? 0 : 0.05),
          enterX: -width * 0.05,
          enterY: -height * 0.05
        });
      });
      if (boxes.length > 1 && allHeroes.length > 0) {
        const dec2 = boxes[boxes.length - 1].isHero ? boxes[0] : boxes[boxes.length - 1];
        decorations.push({
          ...dec2,
          isHero: false,
          role: "decoration",
          fontScale: Math.max(1.8, Math.min(allHeroes[0].fontScale * 2.2, 3.5)),
          vertical: false,
          x: allHeroes[0].x + width * 0.25,
          y: allHeroes[0].y + height * 0.15,
          rotation: 0.08,
          enterX: width * 0.05,
          enterY: height * 0.05
        });
      }
    }
    boxes.unshift(...decorations);
  }
  return boxes.map((box) => ({
    segmentIndex: box.index,
    displayText: box.displayText,
    role: box.role || (box.isHero ? "hero" : box.isSemiHero ? "semi-hero" : "support"),
    fontScale: box.fontScale,
    x: box.x,
    y: box.y,
    rotation: box.rotation,
    enterX: box.enterX,
    enterY: box.enterY,
    vertical: box.vertical,
    layoutDirection: box.layoutDirection,
    timingPhase: box.timingPhase
  }));
};

// node_modules/pixi.js/lib/advanced-blend-modes/init.mjs
extensions.add(
  ColorBlend,
  ColorBurnBlend,
  ColorDodgeBlend,
  DarkenBlend,
  DifferenceBlend,
  DivideBlend,
  ExclusionBlend,
  HardLightBlend,
  HardMixBlend,
  LightenBlend,
  LinearBurnBlend,
  LinearLightBlend,
  LinearDodgeBlend,
  LuminosityBlend,
  NegationBlend,
  OverlayBlend,
  PinLightBlend,
  SaturationBlend,
  SoftLightBlend,
  SubtractBlend,
  VividLightBlend
);

// src/components/visualizer/sonnet/sonnetGlyphLayout.ts
var resolveSonnetGlyphMotionDuration = (window) => {
  const shotDuration = Math.max(1e-3, window.endTime - window.startTime);
  const preferred = Math.min(1.8, Math.max(0.65, shotDuration * 0.42));
  return Math.min(preferred, shotDuration * 0.72);
};
var buildSonnetGlyphLayout = (segment, placement, fontSize, measureGlyph, motionWindow) => {
  const fallbackChars = Array.from(segment.text);
  const graphemes = segment.graphemes.length ? segment.graphemes : fallbackChars.map((char, index) => ({
    char,
    startTime: segment.startTime + (segment.endTime - segment.startTime) * index / Math.max(1, fallbackChars.length),
    endTime: segment.startTime + (segment.endTime - segment.startTime) * (index + 1) / Math.max(1, fallbackChars.length)
  }));
  const advances = graphemes.map((item) => placement.vertical ? fontSize * 0.9 : Math.max(fontSize * 0.2, measureGlyph(item.char)));
  const totalAdvance = advances.reduce((sum, advance) => sum + advance, 0);
  const motionDuration = resolveSonnetGlyphMotionDuration(motionWindow);
  let cursor = -totalAdvance / 2;
  return graphemes.map((grapheme, index) => {
    const advance = advances[index];
    const localX = placement.vertical ? 0 : cursor + advance / 2;
    const localY = placement.vertical ? cursor + advance / 2 : 0;
    cursor += advance;
    const cosine = Math.cos(placement.rotation);
    const sine = Math.sin(placement.rotation);
    const stagger = index % 2 === 0 ? -1 : 1;
    const startTime = grapheme.startTime;
    const settleTime = startTime + motionDuration;
    return {
      char: grapheme.char,
      baseX: placement.x + localX * cosine - localY * sine,
      baseY: placement.y + localX * sine + localY * cosine,
      enterX: placement.enterX + (placement.vertical ? stagger * fontSize * 0.28 : 0),
      enterY: placement.enterY + (placement.vertical ? 0 : stagger * fontSize * 0.24),
      entryRotation: stagger * (isSonnetEmphasisRole(placement.role) ? 0.055 : 0.035),
      startTime,
      settleTime: Math.max(startTime, settleTime)
    };
  });
};

// src/components/visualizer/sonnet/sonnetStaffNotation.ts
var LA_FOLIA_STAFF_NOTES = [
  { pitch: "D5", staffStep: 6, beats: 1 },
  { pitch: "D5", staffStep: 6, beats: 1.5 },
  { pitch: "E5", staffStep: 7, beats: 0.5 },
  { pitch: "C#5", staffStep: 5, beats: 1, accidental: "sharp" },
  { pitch: "C#5", staffStep: 5, beats: 1, accidental: "sharp" },
  { pitch: "C#5", staffStep: 5, beats: 1, accidental: "sharp" },
  { pitch: "D5", staffStep: 6, beats: 1 },
  { pitch: "D5", staffStep: 6, beats: 1.5 },
  { pitch: "D5", staffStep: 6, beats: 0.5 },
  { pitch: "E5", staffStep: 7, beats: 1 },
  { pitch: "E5", staffStep: 7, beats: 1 },
  { pitch: "E5", staffStep: 7, beats: 1 },
  { pitch: "F5", staffStep: 8, beats: 1 },
  { pitch: "F5", staffStep: 8, beats: 1.5 },
  { pitch: "F5", staffStep: 8, beats: 0.5 },
  { pitch: "E5", staffStep: 7, beats: 1 },
  { pitch: "E5", staffStep: 7, beats: 1 },
  { pitch: "E5", staffStep: 7, beats: 1 },
  { pitch: "D5", staffStep: 6, beats: 1 },
  { pitch: "D5", staffStep: 6, beats: 1.5 },
  { pitch: "C#5", staffStep: 5, beats: 0.5, accidental: "sharp" },
  { pitch: "D5", staffStep: 6, beats: 3 }
];
var LA_FOLIA_TOTAL_BEATS = LA_FOLIA_STAFF_NOTES.reduce(
  (total, note) => total + note.beats,
  0
);
var LA_FOLIA_CYCLE_SECONDS = 8;

// src/components/visualizer/sonnet/sonnetStaffView.ts
var positiveModulo = (value, divisor) => (value % divisor + divisor) % divisor;
var buildSonnetStaffView = (pixi, placement, theme, baseFontSize, shotStartTime, width, containerLayer) => {
  const wrapper = new pixi.Container();
  wrapper.rotation = placement.rotation;
  wrapper.position.set(placement.x, placement.y);
  wrapper.alpha = 0;
  const staffWidth = Math.max(300, width * 0.6);
  const lineSpacing = baseFontSize * 0.25;
  const totalHeight = lineSpacing * 4;
  const halfWidth = staffWidth / 2;
  const halfHeight = totalHeight / 2;
  const playableWidth = staffWidth * 0.92;
  const beatWidth = playableWidth / LA_FOLIA_TOTAL_BEATS;
  const timedNotes = [];
  let beatCursor = 0;
  for (const note of LA_FOLIA_STAFF_NOTES) {
    timedNotes.push({ ...note, startBeat: beatCursor });
    beatCursor += note.beats;
  }
  const primaryColor = pixi.Color.shared.setValue(theme.primaryColor).toNumber();
  const accentColor = pixi.Color.shared.setValue(theme.accentColor).toNumber();
  const staffGraphics = new pixi.Graphics();
  for (let i = 0; i < 5; i++) {
    const y = -halfHeight + i * lineSpacing;
    staffGraphics.moveTo(-halfWidth, y);
    staffGraphics.lineTo(halfWidth, y);
  }
  staffGraphics.stroke({ color: primaryColor, width: 2, alpha: 0.3 });
  for (let bar = 1; bar < 8; bar += 1) {
    const x = -playableWidth / 2 + beatWidth * bar * 3;
    staffGraphics.moveTo(x, -halfHeight);
    staffGraphics.lineTo(x, halfHeight);
  }
  staffGraphics.stroke({ color: primaryColor, width: 1, alpha: 0.16 });
  staffGraphics.moveTo(-halfWidth + 10, -halfHeight);
  staffGraphics.lineTo(-halfWidth + 10, halfHeight);
  staffGraphics.stroke({ color: primaryColor, width: 4, alpha: 0.5 });
  staffGraphics.moveTo(halfWidth - 10, -halfHeight);
  staffGraphics.lineTo(halfWidth - 10, halfHeight);
  staffGraphics.stroke({ color: primaryColor, width: 2, alpha: 0.5 });
  staffGraphics.moveTo(halfWidth - 4, -halfHeight);
  staffGraphics.lineTo(halfWidth - 4, halfHeight);
  staffGraphics.stroke({ color: primaryColor, width: 6, alpha: 0.5 });
  const noteGraphics = new pixi.Graphics();
  wrapper.addChild(staffGraphics, noteGraphics);
  containerLayer.addChild(wrapper);
  const updateAnimation = (time) => {
    const cycleElapsed = positiveModulo(time - shotStartTime, LA_FOLIA_CYCLE_SECONDS);
    const beatPosition = cycleElapsed / LA_FOLIA_CYCLE_SECONDS * LA_FOLIA_TOTAL_BEATS;
    const cursorX = -playableWidth / 2 + beatWidth * beatPosition;
    noteGraphics.clear();
    noteGraphics.moveTo(cursorX, -halfHeight - lineSpacing * 0.8);
    noteGraphics.lineTo(cursorX, halfHeight + lineSpacing * 0.8);
    noteGraphics.stroke({ color: accentColor, width: 1.5, alpha: 0.34 });
    timedNotes.forEach((note, index) => {
      const isActive = beatPosition >= note.startBeat && beatPosition < note.startBeat + note.beats;
      const pulse = isActive ? (Math.sin(cycleElapsed * Math.PI * 5 + index * 0.4) + 1) * 0.5 : 0;
      const noteScale = isActive ? 1 + pulse * 0.12 : 1;
      const noteRadiusX = lineSpacing * 0.42 * noteScale;
      const noteRadiusY = lineSpacing * 0.29 * noteScale;
      const x = -playableWidth / 2 + beatWidth * (note.startBeat + note.beats * 0.5);
      const y = halfHeight - note.staffStep * lineSpacing * 0.5;
      const alpha = isActive ? 0.78 + pulse * 0.16 : 0.28 + index % 3 * 0.03;
      const stemDown = note.staffStep >= 6;
      const stemX = x + (stemDown ? -noteRadiusX : noteRadiusX);
      const stemEndY = y + (stemDown ? lineSpacing * 3.1 : -lineSpacing * 3.1);
      noteGraphics.ellipse(x, y, noteRadiusX, noteRadiusY).fill({ color: accentColor, alpha });
      noteGraphics.moveTo(stemX, y);
      noteGraphics.lineTo(stemX, stemEndY);
      noteGraphics.stroke({ color: primaryColor, width: 1.6, alpha: Math.min(0.9, alpha + 0.08) });
      if (note.beats <= 0.5) {
        const flagY = stemEndY;
        const flagDirection = stemDown ? -1 : 1;
        noteGraphics.moveTo(stemX, flagY).quadraticCurveTo(
          stemX + lineSpacing * 1.1,
          flagY + lineSpacing * 0.55 * flagDirection,
          stemX + lineSpacing * 0.1,
          flagY + lineSpacing * flagDirection
        ).stroke({ color: primaryColor, width: 1.6, alpha: Math.min(0.9, alpha + 0.08) });
      }
      if (note.accidental === "sharp") {
        const sharpX = x - noteRadiusX * 2.3;
        const sharpHeight = lineSpacing * 1.15;
        noteGraphics.moveTo(sharpX - lineSpacing * 0.16, y - sharpHeight * 0.5).lineTo(sharpX - lineSpacing * 0.16, y + sharpHeight * 0.5).moveTo(sharpX + lineSpacing * 0.16, y - sharpHeight * 0.5).lineTo(sharpX + lineSpacing * 0.16, y + sharpHeight * 0.5).moveTo(sharpX - lineSpacing * 0.34, y - lineSpacing * 0.12).lineTo(sharpX + lineSpacing * 0.34, y - lineSpacing * 0.28).moveTo(sharpX - lineSpacing * 0.34, y + lineSpacing * 0.28).lineTo(sharpX + lineSpacing * 0.34, y + lineSpacing * 0.12).stroke({ color: primaryColor, width: 1.2, alpha: Math.min(0.86, alpha + 0.08) });
      }
    });
  };
  return {
    display: wrapper,
    halo: null,
    baseX: placement.x,
    baseY: placement.y,
    enterX: placement.enterX,
    enterY: placement.enterY,
    entryRotation: 0,
    finalRotation: placement.rotation,
    startTime: shotStartTime,
    settleTime: shotStartTime + 0.5,
    zDepth: 0,
    isTextGlyph: false,
    updateAnimation
  };
};

// src/components/visualizer/sonnet/sonnetTextFixedGeo.ts
var HOLLOW_VARIANTS = ["straight-frame", "rotated-frame", "orbit-crosshair", "split-arches"];
var resolveHollowVariant = (seed, divisor, offset) => {
  const index = Math.floor(seed / divisor) + offset;
  return HOLLOW_VARIANTS[(index % HOLLOW_VARIANTS.length + HOLLOW_VARIANTS.length) % HOLLOW_VARIANTS.length];
};
var resolveSonnetTextFixedGeoPlan = (seed, isChorusEffect) => {
  if (isChorusEffect) {
    const chorusSeed = (seed % 10 + 10) % 10;
    if (chorusSeed < 9) {
      return { category: "hollow", variant: resolveHollowVariant(seed, 10, chorusSeed) };
    }
    const solidVariants2 = ["orb-hatch", "music-steps", "bent-lines"];
    return { category: "solid", variant: solidVariants2[Math.floor(seed / 10) % solidVariants2.length] };
  }
  const legacyType = (seed % 4 + 4) % 4;
  if (legacyType === 1 || legacyType === 2) {
    return { category: "hollow", variant: resolveHollowVariant(seed, 4, legacyType) };
  }
  const solidVariants = ["orb-hatch", "music-steps", "bent-lines"];
  return { category: "solid", variant: solidVariants[Math.floor(seed / 4) % solidVariants.length] };
};
var drawMusicSteps = (graphic, width, height, alpha, theme) => {
  const heights = [0.24, 0.35, 0.2, 0.82, 0.3, 0.1, 0.23, 0.16];
  const spacing = width / (heights.length + 1);
  heights.forEach((heightRatio, index) => {
    const x = -width / 2 + spacing * (index + 1);
    const baseline = height * (0.12 - index * 0.035);
    const color = index % 2 === 0 ? theme.accentColor : theme.secondaryColor;
    graphic.moveTo(x - spacing * 0.12, baseline - height * heightRatio * 0.5).lineTo(x + spacing * 0.12, baseline + height * heightRatio * 0.5).stroke({ color, width: Math.max(2, height * 0.025), alpha: alpha * 0.52 });
  });
};
var drawBentLines = (graphic, width, height, alpha, theme) => {
  const lineCount = 5;
  for (let index = 0; index < lineCount; index += 1) {
    const x = -width * 0.34 + index * width * 0.17;
    const topY = -height * (0.42 - index * 0.035);
    const elbowY = -height * (0.08 - index * 0.025);
    const bottomY = height * (0.35 + index * 0.035);
    const color = index % 2 === 0 ? theme.accentColor : theme.secondaryColor;
    graphic.moveTo(x - width * 0.16, topY).lineTo(x, elbowY).lineTo(x - width * 0.015, bottomY).stroke({ color, width: Math.max(2, height * 0.022), alpha: alpha * 0.52 });
  }
};
var drawOrbitCrosshair = (graphic, width, height, alpha, color, secondaryColor) => {
  const radius = Math.min(width, height) * 0.46;
  graphic.circle(0, 0, radius).stroke({ color, width: 1.5, alpha });
  graphic.circle(-width * 0.17, 0, radius * 0.72).stroke({ color: secondaryColor, width: 1, alpha: alpha * 0.72 });
  graphic.circle(width * 0.17, 0, radius * 0.72).stroke({ color: secondaryColor, width: 1, alpha: alpha * 0.72 });
  graphic.moveTo(-width * 0.62, 0).lineTo(width * 0.62, 0).stroke({ color, width: 1, alpha: alpha * 0.64 });
  graphic.moveTo(0, -height * 0.62).lineTo(0, height * 0.62).stroke({ color, width: 1, alpha: alpha * 0.64 });
};
var drawSplitArches = (graphic, width, height, alpha, color, secondaryColor) => {
  const halfWidth = width * 0.46;
  const archRadius = Math.min(width * 0.34, height * 0.52);
  [-1, 1].forEach((direction, index) => {
    const x = direction * halfWidth * 0.42;
    graphic.moveTo(x - archRadius * 0.72, height * 0.42).lineTo(x - archRadius * 0.72, 0).arc(x, 0, archRadius * 0.72, Math.PI, 0).lineTo(x + archRadius * 0.72, height * 0.42).stroke({ color: index === 0 ? color : secondaryColor, width: 1.5, alpha });
    graphic.moveTo(x - archRadius * 0.48, height * 0.42).lineTo(x - archRadius * 0.48, 0).arc(x, 0, archRadius * 0.48, Math.PI, 0).lineTo(x + archRadius * 0.48, height * 0.42).stroke({ color: index === 0 ? secondaryColor : color, width: 1, alpha: alpha * 0.58 });
  });
  graphic.moveTo(-halfWidth, height * 0.42).lineTo(halfWidth, height * 0.42).stroke({ color, width: 2, alpha: alpha * 0.72 });
};
var buildSonnetTextFixedGeo = (pixi, options) => {
  const { seed, isChorusEffect, fontSize, layoutWidth, theme } = options;
  const plan = resolveSonnetTextFixedGeoPlan(seed, isChorusEffect);
  const graphic = new pixi.Graphics();
  const color = seed % 2 === 0 ? theme.primaryColor : theme.secondaryColor;
  const alpha = (isChorusEffect ? 0.4 : 0.25) + seed % 10 * 0.03;
  const scaleMultiplier = isChorusEffect ? 1.5 + seed % 5 * 0.3 : 1;
  const width = Math.max(fontSize * 2.5 * scaleMultiplier, layoutWidth * 0.12 * scaleMultiplier);
  const height = Math.max(fontSize * 1.8 * scaleMultiplier, layoutWidth * 0.08 * scaleMultiplier);
  if (plan.category === "hollow") {
    if (plan.variant === "orbit-crosshair") {
      drawOrbitCrosshair(graphic, width, height, alpha, color, theme.secondaryColor);
      return graphic;
    }
    if (plan.variant === "split-arches") {
      drawSplitArches(graphic, width, height, alpha, color, theme.secondaryColor);
      return graphic;
    }
    const frameWidth = plan.variant === "rotated-frame" ? width * 0.8 : width;
    const frameHeight = plan.variant === "rotated-frame" ? height * 0.8 : height;
    graphic.rect(-frameWidth / 2, -frameHeight / 2, frameWidth, frameHeight).stroke({ color, width: Math.max(1.5, fontSize * 0.02), alpha });
    if (isChorusEffect && seed % 2 === 0) {
      graphic.rect(-frameWidth * 0.6, -frameHeight * 0.6, frameWidth * 1.2, frameHeight * 1.2).stroke({ color, width: 1, alpha: alpha * 0.5 });
    }
    if (plan.variant === "rotated-frame") graphic.rotation = Math.PI / 4;
    return graphic;
  }
  if (plan.variant === "music-steps") {
    drawMusicSteps(graphic, width, height, alpha, theme);
    return graphic;
  }
  if (plan.variant === "bent-lines") {
    drawBentLines(graphic, width, height, alpha, theme);
    return graphic;
  }
  const radius = width * 0.5;
  graphic.circle(0, 0, radius).fill({ color, alpha: alpha * 0.15 });
  const hatch = new pixi.Graphics();
  const hatchSpacing = Math.max(4, width * 0.05);
  for (let offset = -radius; offset < radius; offset += hatchSpacing) {
    const lineHeight = Math.sqrt(Math.max(0, radius * radius - offset * offset));
    hatch.moveTo(offset + radius * 0.4, -lineHeight + radius * 0.4);
    hatch.lineTo(offset + radius * 0.4, lineHeight + radius * 0.4);
  }
  hatch.stroke({ color, width: 1.5, alpha: alpha * 0.6 });
  graphic.addChild(hatch);
  return graphic;
};

// src/components/visualizer/sonnet/sonnetGuides.ts
var colorNumber2 = (pixi, color) => pixi.Color.shared.setValue(color).toNumber();
var resolveSonnetGuideCue = (segment, textStartTime = segment.startTime) => {
  const leadDuration = Math.min(
    0.38,
    Math.max(0.2, 0.18 + (segment.endTime - segment.startTime) * 0.1)
  );
  return {
    startTime: textStartTime - leadDuration,
    endTime: textStartTime + 0.65
    // Extended duration to let decorations burst and curve fade
  };
};
var createSonnetGuide = (pixi, segment, placement, theme, fontSize, textStartTime = segment.startTime) => {
  const container = new pixi.Container();
  const graphics = new pixi.Graphics();
  const shapesContainer = new pixi.Container();
  const isHero = isSonnetEmphasisRole(placement.role);
  const fallbackDirection = placement.timingPhase < 0.5 ? -1 : 1;
  const startX = placement.enterX || fallbackDirection * fontSize * 1.8;
  const startY = placement.enterY || -fontSize * 0.9;
  const color = colorNumber2(
    pixi,
    isHero ? theme.accentColor : theme.secondaryColor
  );
  const strokeProps = {
    color,
    width: isHero ? 1.8 : 1,
    alpha: isHero ? 0.82 : 0.55
  };
  const p0 = { x: startX, y: startY };
  const p1 = { x: startX * 0.6, y: startY * 0.4 };
  const p2 = { x: startX * 0.2, y: startY * 0.1 };
  const p3 = { x: 0, y: 0 };
  const getBezier = (p_0, p_1, p_2, p_3, t) => {
    const mt = 1 - t;
    return {
      x: mt * mt * mt * p_0.x + 3 * mt * mt * t * p_1.x + 3 * mt * t * t * p_2.x + t * t * t * p_3.x,
      y: mt * mt * mt * p_0.y + 3 * mt * mt * t * p_1.y + 3 * mt * t * t * p_2.y + t * t * t * p_3.y
    };
  };
  const numShapes = isHero ? 6 : 3;
  const shapeData = [];
  for (let i = 0; i < numShapes; i++) {
    const shape = new pixi.Graphics();
    const type = Math.floor(Math.random() * 4);
    const size = (isHero ? 3 : 1.5) + Math.random() * 3.5;
    const sAlpha = isHero ? 0.8 : 0.6;
    if (type === 0) {
      shape.circle(0, 0, size).fill({ color, alpha: sAlpha });
    } else if (type === 1) {
      shape.rect(-size, -size, size * 2, size * 2).fill({ color, alpha: sAlpha });
    } else if (type === 2) {
      shape.moveTo(-size, 0).lineTo(size, 0).moveTo(0, -size).lineTo(0, size).stroke({ color, width: 2, alpha: sAlpha });
    } else {
      shape.moveTo(0, -size).lineTo(size, 0).lineTo(0, size).lineTo(-size, 0).fill({ color, alpha: sAlpha });
    }
    shapesContainer.addChild(shape);
    shapeData.push({
      obj: shape,
      angle: Math.random() * Math.PI * 2,
      speed: (15 + Math.random() * 45) * (isHero ? 1 : 0.7),
      rotSpeed: (Math.random() - 0.5) * 8
    });
  }
  const trackingTrails = [];
  if (isHero || Math.random() > 0.4) {
    const d = Math.random() > 0.5 ? 1 : -1;
    const yOffset = (Math.random() - 0.5) * fontSize * 0.8;
    trackingTrails.push({
      p0: { x: -d * fontSize * 2.5, y: yOffset + fontSize * 1.5 },
      p1: { x: -d * fontSize * 0.8, y: yOffset - fontSize * 2 },
      p2: { x: d * fontSize * 0.8, y: yOffset + fontSize * 2 },
      p3: { x: d * fontSize * 2.5, y: yOffset - fontSize * 1.5 },
      delay: Math.random() * 0.15
    });
  }
  if (isHero || Math.random() > 0.6) {
    const d = Math.random() > 0.5 ? 1 : -1;
    trackingTrails.push({
      p0: { x: -fontSize * 2, y: -d * fontSize * 1.8 },
      p1: { x: fontSize * 2, y: d * fontSize * 1.8 },
      p2: { x: -fontSize * 2, y: d * fontSize * 1.8 },
      p3: { x: fontSize * 2, y: -d * fontSize * 1.8 },
      delay: Math.random() * 0.1
    });
  }
  let rectSpline = null;
  if (Math.random() > 0.4) {
    const length = fontSize * (1.2 + Math.random() * 1.5);
    const thickness = (isHero ? 6 : 3) + Math.random() * 8;
    const angle = (Math.random() - 0.5) * Math.PI;
    const rx = (Math.random() - 0.5) * fontSize * 1.2;
    const ry = (Math.random() - 0.5) * fontSize * 1.2;
    rectSpline = {
      p0: { x: rx, y: ry },
      p1: { x: rx + Math.cos(angle) * length, y: ry + Math.sin(angle) * length },
      thickness,
      delay: Math.random() * 0.15,
      duration: 0.25 + Math.random() * 0.2
    };
  }
  container.addChild(graphics, shapesContainer);
  container.position.set(placement.x, placement.y);
  container.alpha = 0;
  const cue = resolveSonnetGuideCue(segment, textStartTime);
  const update = (progress) => {
    const drawProgress = Math.min(1, Math.max(0, progress / 0.35));
    const fadeOut = 1 - Math.min(1, Math.max(0, (progress - 0.4) / 0.3));
    graphics.clear();
    if (drawProgress > 0 && fadeOut > 0) {
      const steps = 20;
      let prevP = p0;
      for (let i = 1; i <= steps; i++) {
        const t = i / steps * drawProgress;
        const p = getBezier(p0, p1, p2, p3, t);
        const intensity = Math.pow(i / steps, 2);
        const segmentAlpha = Math.min(1, intensity * strokeProps.alpha * fadeOut * 1.6);
        const segmentWidth = (isHero ? 2.5 : 1.5) + intensity * (isHero ? 5 : 3);
        graphics.moveTo(prevP.x, prevP.y);
        graphics.lineTo(p.x, p.y);
        graphics.stroke({ color, width: segmentWidth, alpha: segmentAlpha });
        prevP = p;
      }
      const head = getBezier(p0, p1, p2, p3, drawProgress);
      graphics.circle(head.x, head.y, isHero ? 14 : 9).fill({ color, alpha: 0.5 * fadeOut });
      graphics.circle(head.x, head.y, isHero ? 4.5 : 3).fill({ color: 16777215, alpha: 1 * fadeOut });
    }
    trackingTrails.forEach((trail) => {
      const localProg = (progress - trail.delay) / 0.55;
      if (localProg > -0.15 && fadeOut > 0) {
        if (localProg > 0 && localProg < 1.3) {
          const headT = Math.min(1, localProg);
          const tailT = Math.max(0, localProg - 0.35);
          if (headT > tailT) {
            const steps = 25;
            let prevP = getBezier(trail.p0, trail.p1, trail.p2, trail.p3, tailT);
            for (let i = 1; i <= steps; i++) {
              const stepT = tailT + i / steps * (headT - tailT);
              const pos = getBezier(trail.p0, trail.p1, trail.p2, trail.p3, stepT);
              const intensity = Math.pow(i / steps, 2);
              const alpha = intensity * strokeProps.alpha * fadeOut * 0.9;
              const width = (isHero ? 2 : 1) + intensity * (isHero ? 5 : 2.5);
              graphics.moveTo(prevP.x, prevP.y);
              graphics.lineTo(pos.x, pos.y);
              graphics.stroke({ color, width, alpha });
              prevP = pos;
            }
          }
          if (headT > 0 && headT < 1) {
            const headPos = getBezier(trail.p0, trail.p1, trail.p2, trail.p3, headT);
            graphics.circle(headPos.x, headPos.y, isHero ? 7 : 4).fill({ color, alpha: 0.9 * fadeOut });
            graphics.circle(headPos.x, headPos.y, isHero ? 2.5 : 1.5).fill({ color: 16777215, alpha: 1 * fadeOut });
            graphics.circle(headPos.x, headPos.y, isHero ? 20 : 12).stroke({ color, width: isHero ? 2 : 1, alpha: 0.4 * fadeOut });
          }
        }
      }
    });
    if (rectSpline) {
      const localProg = (progress - rectSpline.delay) / rectSpline.duration;
      if (localProg > 0 && localProg < 1.3 && fadeOut > 0) {
        const headT = Math.min(1, Math.max(0, localProg * 1.5));
        const tailT = Math.min(1, Math.max(0, (localProg - 0.3) * 1.5));
        if (headT > tailT) {
          const hx = rectSpline.p0.x + (rectSpline.p1.x - rectSpline.p0.x) * headT;
          const hy = rectSpline.p0.y + (rectSpline.p1.y - rectSpline.p0.y) * headT;
          const tx = rectSpline.p0.x + (rectSpline.p1.x - rectSpline.p0.x) * tailT;
          const ty = rectSpline.p0.y + (rectSpline.p1.y - rectSpline.p0.y) * tailT;
          graphics.moveTo(tx, ty);
          graphics.lineTo(hx, hy);
          graphics.stroke({ color, width: rectSpline.thickness, alpha: strokeProps.alpha * fadeOut * 0.7 });
          graphics.moveTo(tx, ty);
          graphics.lineTo(hx, hy);
          graphics.stroke({ color: 16777215, width: rectSpline.thickness * 0.3, alpha: strokeProps.alpha * fadeOut * 0.9 });
        }
      }
    }
    const burstProgress = Math.min(1, Math.max(0, (progress - 0.3) / 0.7));
    shapeData.forEach((s) => {
      const ease = 1 - Math.pow(1 - burstProgress, 3);
      s.obj.x = p3.x + Math.cos(s.angle) * s.speed * ease;
      s.obj.y = p3.y + Math.sin(s.angle) * s.speed * ease;
      s.obj.rotation = s.rotSpeed * burstProgress;
      s.obj.alpha = (1 - burstProgress) * (isHero ? 1 : 0.8);
      s.obj.scale.set(1 - burstProgress * 0.4);
    });
  };
  return {
    container,
    ...cue,
    maxAlpha: isHero ? 0.95 : 0.7,
    update
  };
};

// src/components/visualizer/sonnet/sonnetTextViewBuilder.ts
var measureText2 = (text, fontSpec, fontSize) => {
  try {
    const layout = layoutWithLines(prepareWithSegments(text || " ", fontSpec), 99999, fontSize * 1.2);
    return layout.lines[0]?.width ?? text.length * fontSize * 0.6;
  } catch {
    return text.length * fontSize * 0.6;
  }
};
var buildSonnetTextView = (pixi, options) => {
  const { Text, TextStyle } = pixi;
  const { segment, placement: originalPlacement } = options;
  const placement = { ...originalPlacement };
  const fontSize = options.baseFontSize * placement.fontScale;
  const normalOffsetSeed = hashSonnetSeed([
    segment.text,
    segment.startOffset,
    segment.endOffset,
    options.segmentIndex,
    "normal-offset"
  ].join(":"));
  const normalOffset = resolveSonnetSegmentNormalOffset(
    placement.role,
    placement.layoutDirection,
    placement.rotation,
    fontSize,
    normalOffsetSeed / 4294967295
  );
  placement.x += normalOffset.x;
  placement.y += normalOffset.y;
  const isKeyword = options.theme.wordColors?.find((w) => w.word.toLowerCase() === segment.text.toLowerCase());
  const bodyColor = options.theme.primaryColor;
  const glowColor = isKeyword ? isKeyword.color : isSonnetEmphasisRole(placement.role) ? options.theme.primaryColor : options.theme.accentColor;
  const isDecoration = placement.role === "decoration";
  const renderWeight = isSonnetEmphasisRole(placement.role) ? "900" : isDecoration ? "300" : "700";
  const fontSpec = `${renderWeight} ${fontSize}px ${options.fontFamily}`;
  const zDepth = resolveSonnetSegmentDepth(placement.role);
  const baseDropShadow = options.glowEnabled && !isDecoration ? {
    color: glowColor,
    alpha: 0.8,
    blur: Math.max(12, fontSize * 0.18),
    distance: 0
  } : void 0;
  const style = new TextStyle({
    fontFamily: options.fontFamily,
    fontWeight: renderWeight,
    fontSize,
    fill: isDecoration ? "transparent" : bodyColor,
    stroke: isDecoration ? { color: glowColor, width: Math.max(1, Math.min(8, fontSize * 6e-3)) } : void 0,
    align: "center",
    dropShadow: baseDropShadow,
    padding: baseDropShadow ? Math.max(20, baseDropShadow.blur * 2.5) : 0
  });
  if (segment.text === "\u266A") {
    const staffView = buildSonnetStaffView(
      pixi,
      placement,
      options.theme,
      options.baseFontSize,
      options.shotStartTime,
      options.width,
      options.textLayer
    );
    const guide2 = createSonnetGuide(
      pixi,
      segment,
      placement,
      options.theme,
      fontSize,
      staffView.startTime
    );
    if (!isDecoration) {
      options.guideLayer.addChild(guide2.container);
    }
    return {
      segmentIndex: options.segmentIndex,
      displayText: segment.text,
      role: placement.role,
      fontScale: placement.fontScale,
      x: placement.x,
      y: placement.y,
      rotation: placement.rotation,
      enterX: placement.enterX,
      enterY: placement.enterY,
      vertical: placement.vertical,
      timingPhase: placement.timingPhase,
      guide: guide2,
      glyphs: [staffView]
    };
  }
  const glyphs = buildSonnetGlyphLayout(
    segment,
    placement,
    fontSize,
    (char) => measureText2(char, fontSpec, fontSize),
    {
      startTime: options.shotStartTime,
      endTime: options.shotEndTime
    }
  ).map((glyph) => {
    const display = new Text({ text: glyph.char, style });
    display.anchor.set(0.5);
    if (isDecoration) display.alpha = 0.2;
    const wrapper = new pixi.Container();
    wrapper.rotation = placement.rotation;
    wrapper.position.set(glyph.baseX, glyph.baseY);
    wrapper.alpha = 0;
    let caCyanNode;
    let caRedNode;
    let caOffsetValue;
    if (!isDecoration) {
      const isHero = isSonnetEmphasisRole(placement.role);
      const offset = fontSize * (isHero ? 0.025 : 0.01);
      caOffsetValue = offset;
      const caCyan = new Text({ text: glyph.char, style });
      caCyan.tint = 65535;
      caCyan.blendMode = "screen";
      caCyan.anchor.set(0.5);
      caCyan.alpha = isHero ? 0.8 : 0.5;
      const caRed = new Text({ text: glyph.char, style });
      caRed.tint = 16711748;
      caRed.blendMode = "screen";
      caRed.anchor.set(0.5);
      caRed.alpha = isHero ? 0.8 : 0.5;
      wrapper.addChild(caCyan, caRed);
      caCyanNode = caCyan;
      caRedNode = caRed;
    }
    wrapper.addChild(display);
    options.textLayer.addChild(wrapper);
    return {
      display: wrapper,
      halo: null,
      caCyan: caCyanNode,
      caRed: caRedNode,
      caOffset: caOffsetValue,
      baseX: glyph.baseX,
      baseY: glyph.baseY,
      enterX: glyph.enterX,
      enterY: glyph.enterY,
      entryRotation: glyph.entryRotation,
      finalRotation: placement.rotation,
      startTime: glyph.startTime,
      settleTime: glyph.settleTime,
      zDepth,
      isTextGlyph: true
    };
  });
  const isChorusParagraph = options.paragraphKind === "chorus";
  const textSeed = segment.text.split("").reduce((a, b) => a + b.charCodeAt(0), 0) + options.segmentIndex * 13;
  const isChorusEffect = isChorusParagraph || textSeed % 100 < 35;
  const shapeThreshold = isChorusEffect ? 40 : 25;
  const shouldAddBgShape = options.showFixedGeo && textSeed % 100 < shapeThreshold && !isDecoration && segment.isWordLike && glyphs.length > 0;
  if (shouldAddBgShape) {
    const bgWrapper = new pixi.Container();
    bgWrapper.position.set(placement.x, placement.y);
    bgWrapper.rotation = placement.rotation;
    bgWrapper.alpha = 0;
    const bgShape = buildSonnetTextFixedGeo(pixi, {
      seed: textSeed,
      isChorusEffect,
      fontSize,
      layoutWidth: options.width,
      theme: options.theme
    });
    bgWrapper.addChild(bgShape);
    options.textLayer.addChildAt(bgWrapper, 0);
    const firstGlyph = glyphs[0];
    const bgGlyph = {
      display: bgWrapper,
      halo: null,
      baseX: placement.x,
      baseY: placement.y,
      enterX: placement.enterX,
      enterY: placement.enterY,
      entryRotation: 0,
      finalRotation: placement.rotation,
      startTime: firstGlyph.startTime,
      settleTime: firstGlyph.settleTime,
      zDepth: -0.5 - textSeed % 5 * 0.1,
      // background depth for parallax
      isBackgroundShape: true,
      isTextGlyph: false
    };
    glyphs.unshift(bgGlyph);
  }
  const guide = createSonnetGuide(
    pixi,
    segment,
    placement,
    options.theme,
    fontSize,
    glyphs[0]?.startTime ?? options.shotStartTime
  );
  if (!isDecoration) {
    options.guideLayer.addChild(guide.container);
  }
  return {
    segmentIndex: options.segmentIndex,
    displayText: segment.text,
    role: placement.role,
    fontScale: placement.fontScale,
    x: placement.x,
    y: placement.y,
    rotation: placement.rotation,
    enterX: placement.enterX,
    enterY: placement.enterY,
    vertical: placement.vertical,
    timingPhase: placement.timingPhase,
    guide,
    glyphs
  };
};

// src/components/visualizer/sonnet/sonnetGlitchFilter.ts
var vertex = `
in vec2 aPosition;
out vec2 vTextureCoord;

uniform vec4 uInputSize;
uniform vec4 uOutputFrame;
uniform vec4 uOutputTexture;

void main(void) {
    vec2 position = aPosition * uOutputFrame.zw + uOutputFrame.xy;
    position.x = position.x * (2.0 / uOutputTexture.x) - 1.0;
    position.y = position.y * (2.0 * uOutputTexture.z / uOutputTexture.y) - uOutputTexture.z;
    gl_Position = vec4(position, 0.0, 1.0);
    vTextureCoord = aPosition * (uOutputFrame.zw * uInputSize.zw);
}
`;
var fragment = `
in vec2 vTextureCoord;
out vec4 finalColor;

uniform sampler2D uTexture;
uniform vec4 uInputClamp;
uniform float uAmount;
uniform float uSeed;

float hash(vec2 value) {
    return fract(sin(dot(value, vec2(12.9898, 78.233))) * 43758.5453);
}

void main(void) {
    float coarseBand = floor(vTextureCoord.y * 26.0);
    float fineBand = floor(vTextureCoord.y * 110.0);
    float coarseNoise = hash(vec2(coarseBand, uSeed));
    float fineNoise = hash(vec2(fineBand + 41.0, uSeed * 1.37));
    float coarseGate = step(0.58, coarseNoise);
    float fineGate = step(0.88, fineNoise);
    float coarseShift = (hash(vec2(coarseBand + 17.0, uSeed)) * 2.0 - 1.0)
        * coarseGate * uAmount * 0.095;
    float fineShift = (hash(vec2(fineBand + 73.0, uSeed)) * 2.0 - 1.0)
        * fineGate * uAmount * 0.035;
    vec2 sampleUv = vec2(vTextureCoord.x + coarseShift + fineShift, vTextureCoord.y);
    vec4 color = texture(uTexture, clamp(sampleUv, uInputClamp.xy, uInputClamp.zw));

    // Brightness tears affect every color channel equally: glitch structure without RGB dispersion.
    float tear = (coarseGate * (coarseNoise - 0.58) + fineGate * 0.12) * uAmount;
    color.rgb *= 1.0 + tear * 0.42;
    finalColor = color;
}
`;
var createSonnetGlitchEffect = (pixi) => {
  const uniforms = new pixi.UniformGroup({
    uAmount: { value: 0, type: "f32" },
    uSeed: { value: 0, type: "f32" }
  });
  const filter = new pixi.Filter({
    glProgram: pixi.GlProgram.from({ vertex, fragment, name: "sonnet-mono-glitch" }),
    resources: { glitchUniforms: uniforms },
    padding: 40
  });
  filter.enabled = false;
  return {
    filter,
    update: (amount, seed) => {
      uniforms.uniforms.uAmount = amount;
      uniforms.uniforms.uSeed = seed;
    }
  };
};

// src/components/visualizer/sonnet/sonnetSceneBuilder.ts
var colorNumber3 = (pixi, color) => pixi.Color.shared.setValue(color).toNumber();
var buildSonnetScene = (pixi, options, iconTextures, paragraph) => {
  const { Container, Graphics } = pixi;
  const width = Math.max(options.host.clientWidth, 320);
  const height = Math.max(options.host.clientHeight, 240);
  const container = new Container();
  const sceneBackgroundLayer = new Container();
  const showOnlyText = options.tuning.showOnlyText;
  const showBackgroundMg = !showOnlyText && options.tuning.showBackgroundMg;
  const showFixedGeo = !showOnlyText && options.tuning.showFixedGeo;
  const showBackgroundDecor = !showOnlyText && options.tuning.showBackgroundDecor;
  const showGuide = !showOnlyText && options.tuning.showGuide;
  const showOuterMetadata = !showOnlyText && options.tuning.outerFrameMode === "full";
  const sceneSeed = hashSonnetSeed(`${options.programSeed}:${paragraph.id}`);
  const postProcessProfile = resolveSonnetPostProcessProfile(
    options.theme,
    options.tuning,
    options.staticMode
  );
  const postProcessFilters = [];
  if (showBackgroundMg) {
    const density = Math.round(4 + options.tuning.mgDensity * 5);
    if (!options.shellOwnsWallpaper) {
      sceneBackgroundLayer.addChild(new Graphics().rect(0, 0, width, height).fill({ color: colorNumber3(pixi, options.theme.backgroundColor), alpha: 0.1 }));
    }
    for (let index = 0; index < density; index += 1) {
      const x = (sceneSeed + index * 97) % 997 / 997 * width;
      const y = (sceneSeed + index * 193) % 991 / 991 * height;
      const length = 32 + (sceneSeed + index * 43) % 180;
      sceneBackgroundLayer.addChild(new Graphics().moveTo(x, y).lineTo(Math.min(width, x + length), y).stroke({
        color: colorNumber3(pixi, index % 2 ? options.theme.secondaryColor : options.theme.accentColor),
        width: index % 3 === 0 ? 2 : 1,
        alpha: 0.12 + index % 4 * 0.04
      }));
    }
  }
  const { Text, TextStyle } = pixi;
  if (options.theme.name) {
    const nameText = new Text({
      text: `[ THEME ] ${options.theme.name.toUpperCase()}`,
      style: new TextStyle({
        fontFamily: resolveThemeFontStack(options.theme),
        fontWeight: "bold",
        fontSize: 14,
        fill: options.theme.primaryColor,
        letterSpacing: 4
      })
    });
    nameText.alpha = 0.2;
    nameText.rotation = -Math.PI / 2;
    nameText.position.set(20, height - 20);
    nameText.anchor.set(0, 1);
    if (showOuterMetadata) sceneBackgroundLayer.addChild(nameText);
  }
  if (options.theme.description) {
    const descText = new Text({
      text: options.theme.description,
      style: new TextStyle({
        fontFamily: resolveThemeFontStack(options.theme),
        fontSize: 12,
        fill: options.theme.secondaryColor,
        wordWrap: true,
        wordWrapWidth: width * 0.3
      })
    });
    descText.alpha = 0.3;
    descText.position.set(width - 20, 20);
    descText.anchor.set(1, 0);
    if (showOuterMetadata) sceneBackgroundLayer.addChild(descText);
  }
  container.addChild(sceneBackgroundLayer);
  const fontFamily = resolveThemeFontStack(options.theme);
  const fontWeight = resolveThemeFontWeight(options.theme, 600);
  const shots = paragraph.shots.map((shot, shotIndex) => {
    const shotContainer = new Container();
    const compiledLines = shot.lineIndices.map((lineIndex) => paragraph.lines.find((item) => item.sourceIndex === lineIndex)).filter(Boolean);
    const linesSegments = compiledLines.map((line) => line.segments.filter(isSonnetLayoutSegment)).filter((segs) => segs.length > 0);
    const segments = linesSegments.flat();
    const wordCount = Math.max(1, segments.filter((segment) => segment.isWordLike).length);
    const heroScale = shot.kind === "type-impact" ? 1.55 : shot.kind === "quiet-tableau" ? 0.82 : 1;
    const fontSize = Math.max(24, Math.min(112, width / Math.max(7, wordCount * 2.15) * heroScale * options.lyricsFontScale));
    const views = [];
    const placements = resolveSonnetTypographyLayout({
      lines: linesSegments,
      shotKind: shot.kind,
      paragraphKind: paragraph.kind,
      width,
      height,
      baseFontSize: fontSize,
      fontFamily,
      fontWeight
    });
    const mgLayer = buildSonnetShotMg(
      pixi,
      shot.kind,
      options.theme,
      width,
      height,
      sceneSeed + shotIndex * 97,
      iconTextures
    );
    shotContainer.addChild(mgLayer);
    const mgBackgroundLayer = mgLayer.bgLayer;
    const mgGeoLayer = mgLayer.geoLayer;
    const mgParticleLayer = mgLayer.particleLayer;
    const mgFixedGeoLayer = mgLayer.fixedGeoLayer;
    mgLayer.visible = showBackgroundMg || showFixedGeo || showBackgroundDecor;
    if (mgBackgroundLayer) mgBackgroundLayer.visible = showBackgroundMg;
    if (mgGeoLayer) mgGeoLayer.visible = showBackgroundMg;
    if (mgParticleLayer) mgParticleLayer.visible = showBackgroundDecor;
    if (mgFixedGeoLayer) mgFixedGeoLayer.visible = showFixedGeo;
    const { layer: haloLayer, filters: haloFilters } = createSonnetHaloLayer(
      pixi,
      postProcessProfile
    );
    const guideLayer = new Container();
    const textLayer = new Container();
    guideLayer.visible = showGuide;
    haloLayer.visible = !showOnlyText;
    shotContainer.addChild(guideLayer, haloLayer, textLayer);
    postProcessFilters.push(...haloFilters);
    let staffViewAdded = false;
    placements.forEach((placement) => {
      const segment = segments[placement.segmentIndex];
      if (segment.text === "\u266A") {
        if (staffViewAdded) return;
        staffViewAdded = true;
      }
      views.push(buildSonnetTextView(
        pixi,
        {
          segment,
          placement,
          segmentIndex: placement.segmentIndex,
          baseFontSize: fontSize,
          shotStartTime: shot.startTime,
          shotEndTime: shot.endTime,
          paragraphKind: paragraph.kind,
          width,
          fontFamily,
          fontWeight,
          theme: options.theme,
          glowEnabled: postProcessProfile.glowStrength > 0,
          showFixedGeo,
          guideLayer,
          haloLayer,
          textLayer
        }
      ));
    });
    const bounds = shotContainer.getLocalBounds();
    if (shot.kind === "mask-reveal") {
      const mask = new Graphics().rect(bounds.x - 6, bounds.y - 6, bounds.width + 12, bounds.height + 12).fill(16777215);
      shotContainer.addChild(mask);
      shotContainer.mask = mask;
    }
    const heroPlacement = placements.find((p) => p.role === "hero");
    const focusX = shot.kind === "poster-blocks" ? 0 : heroPlacement ? heroPlacement.x : bounds.x + bounds.width / 2;
    const focusY = shot.kind === "poster-blocks" ? 0 : heroPlacement ? heroPlacement.y : bounds.y + bounds.height / 2;
    shotContainer.pivot.set(focusX, focusY);
    shotContainer.position.set(
      width * (shot.kind === "poster-blocks" ? 0.5 : 0.5 + shot.camera.x),
      height * (shot.kind === "poster-blocks" ? 0.5 : 0.48 + shot.camera.y + (shotIndex % 2 ? 0.025 : -0.025))
    );
    container.addChild(shotContainer);
    return {
      shot,
      container: shotContainer,
      segments: views,
      baseX: shotContainer.x,
      baseY: shotContainer.y,
      basePivotX: focusX,
      basePivotY: focusY,
      haloLayer,
      mgLayer,
      mgBackgroundLayer,
      mgGeoLayer,
      mgParticleLayer,
      mgFixedGeoLayer
    };
  });
  if (!showOnlyText) {
    postProcessFilters.push(...applySonnetScenePostProcess(
      pixi,
      container,
      postProcessProfile,
      sceneSeed
    ));
  }
  const transitionBlurFilter = options.tuning.enableTransitions && !options.staticMode ? new pixi.BlurFilter({ strength: 0, quality: 1, kernelSize: 5, resolution: 0.5 }) : null;
  if (transitionBlurFilter) {
    transitionBlurFilter.enabled = false;
    container.filters = [...container.filters ?? [], transitionBlurFilter];
    postProcessFilters.push(transitionBlurFilter);
  }
  const transitionGlitchEffect = options.tuning.enableTransitions && !options.staticMode ? createSonnetGlitchEffect(pixi) : null;
  if (transitionGlitchEffect) {
    container.filters = [...container.filters ?? [], transitionGlitchEffect.filter];
    postProcessFilters.push(transitionGlitchEffect.filter);
  }
  container.visible = false;
  return {
    paragraph,
    container,
    shots,
    shotTimeline: shots.map((shot) => shot.shot),
    postProcessFilters,
    transitionBlurFilter,
    transitionGlitchEffect,
    activeShotIndex: -1
  };
};

// src/components/visualizer/sonnet/sonnetTexturePool.ts
var SonnetTexturePool = class {
  constructor(load, unload, unloadDelayMs = 750) {
    this.load = load;
    this.unload = unload;
    this.unloadDelayMs = unloadDelayMs;
    this.entries = /* @__PURE__ */ new Map();
  }
  async acquire(url) {
    let entry = this.entries.get(url);
    if (!entry) {
      entry = {
        refs: 0,
        promise: this.load(url),
        unloadTimer: null
      };
      this.entries.set(url, entry);
    }
    entry.refs += 1;
    if (entry.unloadTimer) {
      clearTimeout(entry.unloadTimer);
      entry.unloadTimer = null;
    }
    try {
      return await entry.promise;
    } catch (error) {
      if (this.entries.get(url) === entry) this.entries.delete(url);
      throw error;
    }
  }
  release(url) {
    const entry = this.entries.get(url);
    if (!entry) return;
    entry.refs = Math.max(0, entry.refs - 1);
    if (entry.refs > 0 || entry.unloadTimer) return;
    entry.unloadTimer = setTimeout(() => {
      if (entry.refs > 0 || this.entries.get(url) !== entry) return;
      this.entries.delete(url);
      void this.unload(url).catch(() => void 0);
    }, this.unloadDelayMs);
  }
};
var pixiPools = /* @__PURE__ */ new WeakMap();
var getSonnetTexturePool = (pixi) => {
  const assets = pixi.Assets;
  const cached = pixiPools.get(assets);
  if (cached) return cached;
  const pool = new SonnetTexturePool(
    (url) => pixi.Assets.load(url),
    (url) => pixi.Assets.unload(url)
  );
  pixiPools.set(assets, pool);
  return pool;
};

// src/components/visualizer/sonnet/sonnetPixiResources.ts
var unloadSonnetDisplayTree = (root) => {
  const stack = [...root.children ?? []];
  while (stack.length > 0) {
    const node = stack.pop();
    if (node.children?.length) stack.push(...node.children);
    node.unload?.();
  }
};
var destroySonnetDisplayTree = (root) => {
  const order = [];
  const stack = [root];
  while (stack.length > 0) {
    const node = stack.pop();
    order.push(node);
    if (node.children?.length) stack.push(...node.children);
  }
  for (let index = order.length - 1; index >= 0; index -= 1) order[index].destroy?.();
};
var destroySonnetContainerChildren = (container) => {
  const children = container.removeChildren();
  children.forEach((child) => {
    unloadSonnetDisplayTree(child);
    child.unload?.();
    destroySonnetDisplayTree(child);
  });
};

// src/components/visualizer/sonnet/sonnetCredits.ts
var normalizeMetadata = (value) => value?.trim() ?? "";
var resolveSonnetCreditsFrame = (time, finalLyricEndTime) => {
  const elapsed = time - finalLyricEndTime;
  if (elapsed <= 0) {
    return {
      active: false,
      lyricAlpha: 1,
      lyricBlur: 0,
      posterAlpha: 0,
      posterOffsetY: 0.04,
      posterScale: 0.965
    };
  }
  const lyricExit = easeSonnetInOut(clamp01(elapsed / 1.25));
  const posterEnter = easeSonnetInOut(clamp01((elapsed - 0.38) / 1.55));
  return {
    active: true,
    lyricAlpha: 1 - lyricExit,
    lyricBlur: lyricExit * 18,
    posterAlpha: posterEnter,
    posterOffsetY: (1 - posterEnter) * 0.04,
    posterScale: 0.965 + posterEnter * 0.035
  };
};
var hasSonnetCreditsMetadata = (metadata) => Boolean(
  normalizeMetadata(metadata.title) || normalizeMetadata(metadata.artist) || normalizeMetadata(metadata.album)
);
var buildSonnetCreditsPoster = (pixi, theme, metadata, width, height, lyricsFontScale) => {
  const container = new pixi.Container();
  const title = normalizeMetadata(metadata.title);
  const artist = normalizeMetadata(metadata.artist);
  const album = normalizeMetadata(metadata.album);
  const fontFamily = resolveThemeFontStack(theme);
  const titleWeight = resolveThemeFontWeight(theme, 700);
  const detailWeight = resolveThemeFontWeight(theme, 500);
  const left = Math.max(38, width * 0.105);
  const right = Math.max(38, width * 0.09);
  const contentWidth = Math.max(220, width - left - right);
  const titleSize = Math.max(42, Math.min(118, width * 0.088 * lyricsFontScale));
  const detailSize = Math.max(13, Math.min(25, width * 0.018 * lyricsFontScale));
  const accent = pixi.Color.shared.setValue(theme.accentColor).toNumber();
  const primary = pixi.Color.shared.setValue(theme.primaryColor).toNumber();
  const secondary = pixi.Color.shared.setValue(theme.secondaryColor).toNumber();
  const geometry = new pixi.Graphics();
  geometry.rect(left, height * 0.155, Math.max(42, width * 0.075), 5).fill({ color: accent, alpha: 0.95 }).rect(left, height * 0.155, 2, height * 0.57).fill({ color: primary, alpha: 0.22 }).rect(width - right - 8, height * 0.225, 8, height * 0.34).fill({ color: secondary, alpha: 0.32 }).moveTo(left, height * 0.79).lineTo(width - right, height * 0.79).stroke({ color: primary, width: 1, alpha: 0.36 });
  container.addChild(geometry);
  if (artist) {
    const artistText = new pixi.Text({
      text: artist.toLocaleUpperCase(),
      style: new pixi.TextStyle({
        fontFamily,
        fontWeight: String(detailWeight),
        fontSize: detailSize,
        fill: theme.accentColor,
        letterSpacing: Math.max(2, detailSize * 0.18),
        wordWrap: true,
        wordWrapWidth: contentWidth * 0.72
      })
    });
    artistText.position.set(left + 20, height * 0.205);
    container.addChild(artistText);
  }
  if (title) {
    const titleText = new pixi.Text({
      text: title,
      style: new pixi.TextStyle({
        fontFamily,
        fontWeight: String(titleWeight),
        fontSize: titleSize,
        fill: theme.primaryColor,
        leading: -Math.max(2, titleSize * 0.08),
        letterSpacing: -Math.max(0.5, titleSize * 0.018),
        wordWrap: true,
        wordWrapWidth: contentWidth * 0.88,
        breakWords: true
      })
    });
    titleText.position.set(left + 16, height * 0.285);
    container.addChild(titleText);
  }
  if (album) {
    const albumText = new pixi.Text({
      text: `\u2014 ${album}`,
      style: new pixi.TextStyle({
        fontFamily,
        fontWeight: String(detailWeight),
        fontSize: detailSize * 0.92,
        fill: theme.secondaryColor,
        letterSpacing: Math.max(1, detailSize * 0.1),
        wordWrap: true,
        wordWrapWidth: contentWidth * 0.72
      })
    });
    albumText.position.set(left + 18, height * 0.825);
    container.addChild(albumText);
  }
  container.pivot.set(width / 2, height / 2);
  container.position.set(width / 2, height / 2);
  return container;
};

// src/components/visualizer/sonnet/createSonnetPixiRuntime.ts
var animationScale = (theme) => theme.animationIntensity === "calm" ? 0.65 : theme.animationIntensity === "chaotic" ? 1.35 : 1;
var SonnetPixiRuntime = class _SonnetPixiRuntime {
  constructor(pixi, options, app) {
    this.pixi = pixi;
    this.options = options;
    this.app = app;
    this.sceneCache = /* @__PURE__ */ new Map();
    this.iconTextures = /* @__PURE__ */ new Map();
    this.iconUrls = /* @__PURE__ */ new Set();
    this.activeParagraphIndex = -1;
    this.destroyed = false;
    this.themeRevision = 0;
    this.resizeObserver = null;
    this.lastWidth = 0;
    this.lastHeight = 0;
    this.outroBlurFilter = null;
    this.outroBlurScene = null;
    this.renderFrame = () => {
      if (this.destroyed || this.options.program.paragraphs.length === 0) return;
      const time = this.options.currentTime.get();
      const paragraphIndex = findSonnetParagraphIndexAtTime(this.options.program, time);
      if (paragraphIndex !== this.activeParagraphIndex) {
        this.activeParagraphIndex = paragraphIndex;
        this.ensureScene(paragraphIndex - 1);
        this.ensureScene(paragraphIndex);
        this.ensureScene(paragraphIndex + 1);
        this.pruneScenes(paragraphIndex);
      }
      const width = Math.max(this.options.host.clientWidth, 320);
      const height = Math.max(this.options.host.clientHeight, 240);
      const finalParagraph = this.options.program.paragraphs.at(-1);
      const creditsFrame = resolveSonnetCreditsFrame(
        time,
        finalParagraph?.endTime ?? Number.POSITIVE_INFINITY
      );
      const hasCredits = this.creditsContainer.children.length > 0;
      this.sceneCache.forEach((scene, index) => {
        const isActive = index === paragraphIndex;
        scene.container.visible = isActive;
        if (!isActive) {
          const previousShot = scene.shots[scene.activeShotIndex];
          if (previousShot) unloadSonnetDisplayTree(previousShot.container);
          scene.activeShotIndex = -1;
          return;
        }
        const transitionsEnabled = this.options.tuning.enableTransitions && !this.options.staticMode;
        const transitionSeed = hashSonnetSeed(`${this.options.program.seed}:${scene.paragraph.id}:transition-frame`);
        const previousTransition = index > 0 ? this.options.program.paragraphs[index - 1]?.transitionOut : null;
        const enterDuration = previousTransition ? Math.max(0.16, Math.min(0.3, previousTransition.endTime - previousTransition.startTime)) : 0;
        const entering = transitionsEnabled && previousTransition !== null && time >= scene.paragraph.startTime && time <= scene.paragraph.startTime + enterDuration;
        const paragraphTransitionFrame = entering ? resolveSonnetEnterTransitionFrame(
          previousTransition.kind,
          time - scene.paragraph.startTime,
          enterDuration,
          true,
          transitionSeed
        ) : resolveSonnetExitTransitionFrame(
          scene.paragraph,
          time,
          transitionsEnabled,
          transitionSeed
        );
        let activeShotIndex = 0;
        for (let i = scene.shots.length - 1; i >= 0; i--) {
          if (time >= scene.shots[i].shot.startTime) {
            activeShotIndex = i;
            break;
          }
        }
        const visibleShotIndex = activeShotIndex;
        const shotTransitionFrame = resolveSonnetShotTransitionFrame(
          scene.shotTimeline,
          visibleShotIndex,
          time,
          transitionsEnabled,
          transitionSeed
        );
        const transitionFrame = shotTransitionFrame !== IDLE_SONNET_TRANSITION_FRAME ? shotTransitionFrame : paragraphTransitionFrame;
        scene.shots.forEach((shot, shotIndex) => {
          const isShotActive = shotIndex === visibleShotIndex;
          shot.container.visible = isShotActive;
          if (!isShotActive) return;
          this.updateShot(shot, time, width, height, 0);
        });
        if (scene.activeShotIndex !== visibleShotIndex) {
          const previousShot = scene.shots[scene.activeShotIndex];
          if (previousShot) unloadSonnetDisplayTree(previousShot.container);
          scene.activeShotIndex = visibleShotIndex;
        }
        const isFinalScene = index === this.options.program.paragraphs.length - 1;
        const lyricAlpha = isFinalScene && hasCredits ? creditsFrame.lyricAlpha : 1;
        scene.container.alpha = transitionFrame.alpha * lyricAlpha;
        scene.container.pivot.set(width / 2, height / 2);
        scene.container.position.set(
          width / 2 + transitionFrame.x * width,
          height / 2 + transitionFrame.y * height
        );
        scene.container.scale.set(transitionFrame.scale);
        scene.container.rotation = transitionFrame.rotation;
        if (scene.transitionBlurFilter) {
          scene.transitionBlurFilter.strength = transitionFrame.blur;
          scene.transitionBlurFilter.enabled = transitionFrame.blur > 0.01;
        }
        if (scene.transitionGlitchEffect) {
          scene.transitionGlitchEffect.update(transitionFrame.glitch, transitionFrame.glitchSeed);
          scene.transitionGlitchEffect.filter.enabled = transitionFrame.glitch > 0.01;
        }
        if (isFinalScene && hasCredits) {
          this.updateOutroBlur(scene, creditsFrame.lyricBlur);
        }
      });
      if (!creditsFrame.active || !hasCredits) this.clearOutroBlur();
      this.creditsContainer.visible = creditsFrame.active && hasCredits && !this.options.tuning.showOnlyText;
      this.creditsContainer.alpha = creditsFrame.posterAlpha;
      this.creditsContainer.position.set(
        width / 2,
        height / 2 + creditsFrame.posterOffsetY * height
      );
      this.creditsContainer.scale.set(creditsFrame.posterScale);
    };
    this.publishVisualFrame = () => {
      this.options.onVisualFrame?.();
    };
  }
  static async create(options) {
    const pixi = await import("./lib-BXMSOICW.js");
    const app = new pixi.Application();
    const width = Math.max(options.host.clientWidth, 320);
    const height = Math.max(options.host.clientHeight, 240);
    await app.init({
      width,
      height,
      backgroundAlpha: 0,
      antialias: true,
      autoDensity: true,
      resolution: options.tuning.textureResolution,
      autoStart: false,
      sharedTicker: false,
      preference: "webgl",
      powerPreference: "high-performance",
      preserveDrawingBuffer: true
    });
    const runtime = new _SonnetPixiRuntime(pixi, options, app);
    runtime.sceneContainer = new pixi.Container();
    runtime.creditsContainer = new pixi.Container();
    runtime.overlayContainer = new pixi.Container();
    app.stage.addChild(runtime.sceneContainer, runtime.creditsContainer, runtime.overlayContainer);
    if (options.signal?.aborted) {
      runtime.destroy();
      throw new DOMException("Sonnet runtime creation was cancelled", "AbortError");
    }
    options.host.appendChild(app.canvas);
    app.canvas.style.cssText = "width:100%;height:100%;display:block";
    await runtime.preloadIcons();
    if (options.signal?.aborted) {
      runtime.destroy();
      throw new DOMException("Sonnet runtime creation was cancelled", "AbortError");
    }
    runtime.install();
    return runtime;
  }
  install() {
    this.resizeToHost();
    this.app.ticker.add(this.renderFrame);
    this.app.ticker.add(this.publishVisualFrame, void 0, this.pixi.UPDATE_PRIORITY.UTILITY);
    this.resizeObserver = new ResizeObserver(() => {
      if (this.destroyed || !this.resizeToHost()) return;
      if (this.options.paused) this.renderOnce();
    });
    this.resizeObserver.observe(this.options.host);
    this.renderOnce();
    if (!this.options.paused) this.app.start();
  }
  resizeToHost() {
    if (this.destroyed) return false;
    const width = Math.max(this.options.host.clientWidth, 320);
    const height = Math.max(this.options.host.clientHeight, 240);
    if (width === this.lastWidth && height === this.lastHeight) return false;
    this.lastWidth = width;
    this.lastHeight = height;
    this.app.renderer.resize(width, height);
    this.clearScenes();
    this.drawCredits(width, height);
    this.drawOverlay(width, height);
    return true;
  }
  drawCredits(width, height) {
    destroySonnetContainerChildren(this.creditsContainer);
    if (this.options.tuning.showOnlyText) return;
    const metadata = {
      title: this.options.songTitle,
      artist: this.options.songArtist,
      album: this.options.songAlbum
    };
    if (!hasSonnetCreditsMetadata(metadata)) return;
    this.creditsContainer.addChild(buildSonnetCreditsPoster(
      this.pixi,
      this.options.theme,
      metadata,
      width,
      height,
      this.options.lyricsFontScale
    ));
    this.creditsContainer.pivot.set(width / 2, height / 2);
    this.creditsContainer.position.set(width / 2, height / 2);
    this.creditsContainer.visible = false;
  }
  setTheme(theme) {
    if (this.destroyed || this.options.theme === theme) return;
    this.options.theme = theme;
    const revision = ++this.themeRevision;
    this.clearScenes();
    this.iconTextures.clear();
    const pool = getSonnetTexturePool(this.pixi);
    this.iconUrls.forEach((url) => pool.release(url));
    this.iconUrls.clear();
    this.drawCredits(this.lastWidth, this.lastHeight);
    this.drawOverlay(this.lastWidth, this.lastHeight);
    this.renderOnce();
    void this.preloadIcons().then(() => {
      if (this.destroyed || revision !== this.themeRevision) return;
      this.clearScenes();
      this.renderOnce();
    });
  }
  setSongMetadata(metadata) {
    if (this.destroyed) return;
    const changed = this.options.songTitle !== metadata.title || this.options.songArtist !== metadata.artist || this.options.songAlbum !== metadata.album;
    if (!changed) return;
    this.options.songTitle = metadata.title;
    this.options.songArtist = metadata.artist;
    this.options.songAlbum = metadata.album;
    if (this.lastWidth > 0 && this.lastHeight > 0) {
      this.drawCredits(this.lastWidth, this.lastHeight);
      if (this.options.paused) this.renderOnce();
    }
  }
  clearOutroBlur() {
    if (this.outroBlurFilter && this.outroBlurScene) {
      this.outroBlurScene.container.filters = (this.outroBlurScene.container.filters ?? []).filter((filter) => filter !== this.outroBlurFilter);
      this.outroBlurFilter.destroy();
    }
    this.outroBlurFilter = null;
    this.outroBlurScene = null;
  }
  updateOutroBlur(scene, strength) {
    if (strength <= 0) {
      this.clearOutroBlur();
      return;
    }
    if (this.outroBlurScene !== scene) this.clearOutroBlur();
    if (!this.outroBlurFilter) {
      this.outroBlurFilter = new this.pixi.BlurFilter({
        strength: 0,
        quality: 2,
        kernelSize: 5,
        resolution: 0.75
      });
      scene.container.filters = [...scene.container.filters ?? [], this.outroBlurFilter];
      this.outroBlurScene = scene;
    }
    this.outroBlurFilter.strength = strength;
  }
  drawOverlay(width, height) {
    destroySonnetContainerChildren(this.overlayContainer);
    if (this.options.tuning.showOnlyText || this.options.tuning.outerFrameMode === "none") return;
    const g = new this.pixi.Graphics();
    const paddingX = Math.max(30, width * 0.05);
    const paddingY = Math.max(30, height * 0.05);
    const primary = this.pixi.Color.shared.setValue(this.options.theme.primaryColor).toNumber();
    const alpha = 0.5;
    g.rect(paddingX, paddingY, 30, 4).fill({ color: primary, alpha: 0.8 });
    g.moveTo(paddingX, paddingY + 16).lineTo(paddingX, paddingY + 120).stroke({ color: primary, width: 1, alpha });
    g.rect(width - paddingX - 4, height - paddingY - 16, 4, 16).fill({ color: primary, alpha: 0.8 });
    g.moveTo(width - paddingX - 160, height - paddingY).lineTo(width - paddingX - 20, height - paddingY).stroke({ color: primary, width: 1, alpha });
    g.moveTo(width - paddingX, height - paddingY - 180).lineTo(width - paddingX, height - paddingY - 30).stroke({ color: primary, width: 1, alpha });
    const drawCross = (cx, cy, size) => {
      g.moveTo(cx - size, cy).lineTo(cx + size, cy).stroke({ color: primary, width: 1, alpha: 0.8 });
      g.moveTo(cx, cy - size).lineTo(cx, cy + size).stroke({ color: primary, width: 1, alpha: 0.8 });
    };
    drawCross(width - paddingX, paddingY + 20, 6);
    g.moveTo(paddingX, height - paddingY - 4).lineTo(paddingX + 4, height - paddingY).lineTo(paddingX, height - paddingY + 4).lineTo(paddingX - 4, height - paddingY).fill({ color: primary, alpha: 0.7 });
    const starStyle = new this.pixi.TextStyle({
      fontFamily: "sans-serif",
      fontSize: 12,
      fill: primary
    });
    const starText = new this.pixi.Text({ text: "\u2726", style: starStyle });
    starText.alpha = 0.6;
    starText.position.set(width - paddingX - 10, height - paddingY);
    starText.anchor.set(1, 0.5);
    this.overlayContainer.addChild(g, starText);
  }
  async preloadIcons() {
    const revision = this.themeRevision, theme = this.options.theme;
    if (this.options.tuning.showOnlyText || !this.options.tuning.showBackgroundDecor) return;
    const names = resolveSonnetIconNames(theme.lyricsIcons);
    const resolution = this.options.tuning.textureResolution;
    const texturePool = getSonnetTexturePool(this.pixi);
    await Promise.all(names.map(async (name, index) => {
      const size = 192 + index % 4 * 32;
      const colors = [
        theme.accentColor,
        theme.secondaryColor,
        theme.primaryColor
      ];
      const color = colors[index % colors.length];
      const key = buildSonnetIconTextureKey(name, color, 3.5, size, resolution);
      const url = buildSonnetIconDataUrl(name, color, 3.5, size);
      if (!url) return;
      try {
        const texture = await texturePool.acquire(url);
        if (this.destroyed || revision !== this.themeRevision) {
          texturePool.release(url);
          return;
        }
        this.iconTextures.set(key, texture);
        this.iconUrls.add(url);
      } catch {
      }
    }));
  }
  clearScenes() {
    this.clearOutroBlur();
    this.sceneCache.forEach((scene) => {
      this.destroyScene(scene);
    });
    this.sceneCache.clear();
    this.activeParagraphIndex = -1;
  }
  destroyScene(scene) {
    if (this.outroBlurScene === scene) this.clearOutroBlur();
    this.sceneContainer.removeChild(scene.container);
    unloadSonnetDisplayTree(scene.container);
    scene.container.filters = null;
    scene.shots.forEach((shot) => {
      shot.haloLayer.filters = null;
    });
    scene.postProcessFilters.forEach((filter) => filter.destroy());
    destroySonnetDisplayTree(scene.container);
  }
  ensureScene(index) {
    if (index < 0 || index >= this.options.program.paragraphs.length) return null;
    const cached = this.sceneCache.get(index);
    if (cached) return cached;
    const scene = buildSonnetScene(this.pixi, {
      programSeed: this.options.program.seed,
      host: this.options.host,
      theme: this.options.theme,
      tuning: this.options.tuning,
      lyricsFontScale: this.options.lyricsFontScale,
      staticMode: this.options.staticMode,
      shellOwnsWallpaper: this.options.shellOwnsWallpaper
    }, this.iconTextures, this.options.program.paragraphs[index]);
    this.sceneCache.set(index, scene);
    this.sceneContainer.addChild(scene.container);
    return scene;
  }
  pruneScenes(index) {
    this.sceneCache.forEach((scene, sceneIndex) => {
      if (Math.abs(sceneIndex - index) <= 1) return;
      this.destroyScene(scene);
      this.sceneCache.delete(sceneIndex);
    });
  }
  updateShot(view, time, width, height, shakeIntensity) {
    const progress = resolveShotProgress(view.shot, time);
    const motion = this.options.tuning.typographyMotion * animationScale(this.options.theme);
    const camera = this.options.tuning.cameraIntensity * animationScale(this.options.theme);
    const cameraFrame = resolveShotMotionFrame(view.shot.kind, progress);
    const gapTime = Math.max(0, time - view.shot.endTime);
    if (gapTime > 0) {
      const tailStart = resolveShotMotionFrame(view.shot.kind, 0.8);
      const dx = cameraFrame.x - tailStart.x;
      const dy = cameraFrame.y - tailStart.y;
      const dScale = cameraFrame.scale - tailStart.scale;
      const dRot = cameraFrame.rotation - tailStart.rotation;
      const maxDrift = 2;
      const driftSpeed = (1 - Math.exp(-gapTime * 0.4)) * maxDrift;
      cameraFrame.x += dx * driftSpeed;
      cameraFrame.y += dy * driftSpeed;
      cameraFrame.scale += dScale * driftSpeed;
      cameraFrame.rotation += dRot * driftSpeed;
    }
    const shake = resolveTimelineShake(time, shakeIntensity);
    let trackSegments = view.segments.filter((s) => s.role !== "decoration" && s.glyphs.length > 0);
    if (trackSegments.length === 0) {
      trackSegments = view.segments.filter((s) => s.glyphs.length > 0);
    }
    let currentFocusX = view.basePivotX;
    let currentFocusY = view.basePivotY;
    if (trackSegments.length > 0) {
      const getSegmentFocus = (seg, t) => {
        if (seg.glyphs.length === 0) return { x: 0, y: 0 };
        const first = seg.glyphs[0];
        const last = seg.glyphs[seg.glyphs.length - 1];
        const trackingFactor = 0.35;
        const segCenterX = (first.baseX + last.baseX) / 2;
        const segCenterY = (first.baseY + last.baseY) / 2;
        const applyFactor = (exactX, exactY) => ({
          x: segCenterX + (exactX - segCenterX) * trackingFactor,
          y: segCenterY + (exactY - segCenterY) * trackingFactor
        });
        if (t <= first.startTime) return applyFactor(first.baseX, first.baseY);
        if (t >= last.startTime) return applyFactor(last.baseX, last.baseY);
        for (let i = 0; i < seg.glyphs.length - 1; i++) {
          if (t >= seg.glyphs[i].startTime && t <= seg.glyphs[i + 1].startTime) {
            const g1 = seg.glyphs[i];
            const g2 = seg.glyphs[i + 1];
            const p = (t - g1.startTime) / Math.max(1e-3, g2.startTime - g1.startTime);
            const exactX = g1.baseX + (g2.baseX - g1.baseX) * p;
            const exactY = g1.baseY + (g2.baseY - g1.baseY) * p;
            return applyFactor(exactX, exactY);
          }
        }
        return applyFactor(first.baseX, first.baseY);
      };
      const focusRanges = trackSegments.map((segment) => ({
        startTime: segment.glyphs[0]?.startTime ?? view.shot.startTime,
        endTime: segment.glyphs.at(-1)?.startTime ?? view.shot.endTime
      }));
      const resolveFocusAtTime = (focusTime2) => {
        let focusX = 0;
        let focusY = 0;
        const focusWeights = resolveSonnetFocusWeights(focusRanges, focusTime2);
        for (let i = 0; i < trackSegments.length; i++) {
          const seg = trackSegments[i];
          if (seg.glyphs.length === 0) continue;
          const weight = focusWeights[i] ?? 0;
          const pos = getSegmentFocus(seg, focusTime2);
          focusX += pos.x * weight;
          focusY += pos.y * weight;
        }
        return { x: focusX, y: focusY };
      };
      const focusTime = Math.max(view.shot.startTime, Math.min(time, view.shot.endTime));
      const smoothedFocus = resolveSonnetSmoothedCameraFocus(
        focusTime,
        view.shot.startTime,
        view.shot.endTime,
        resolveFocusAtTime
      );
      currentFocusX = smoothedFocus.x;
      currentFocusY = smoothedFocus.y;
    }
    view.container.pivot.set(
      view.basePivotX + (currentFocusX - view.basePivotX) * camera,
      view.basePivotY + (currentFocusY - view.basePivotY) * camera
    );
    view.container.scale.set(
      view.shot.camera.zoom * (1 + (cameraFrame.scale - 1) * camera)
    );
    view.container.rotation = (view.shot.camera.rotation + cameraFrame.rotation + shake.rotation) * camera;
    view.container.x = view.baseX + (cameraFrame.x * width + shake.x * width) * camera;
    view.container.y = view.baseY + (cameraFrame.y * height + shake.y * height) * camera;
    if (view.mgParticleLayer) {
      const particleParallaxX = (cameraFrame.x * width + shake.x * width) * camera * 0.4;
      const particleParallaxY = (cameraFrame.y * height + shake.y * height) * camera * 0.4;
      view.mgParticleLayer.position.set(particleParallaxX, particleParallaxY);
      view.mgParticleLayer.rotation = (time - view.shot.startTime) * 0.05;
      view.mgParticleLayer.scale.set(1 + (cameraFrame.scale - 1) * 0.3);
    }
    if (view.mgFixedGeoLayer) {
      view.mgFixedGeoLayer.rotation = -view.container.rotation;
    }
    const audioBass = this.options.audioBands?.bass?.get() ?? 0;
    const audioPower = this.options.audioPower?.get() ?? 0;
    const audioVocal = this.options.audioBands?.vocal?.get() ?? 0;
    if (view.mgLayer.updateTime) {
      view.mgLayer.updateTime(
        time,
        view.shot.cues,
        view.shot.startTime,
        view.shot.endTime,
        audioBass,
        audioPower,
        audioVocal
      );
    }
    view.segments.forEach((segmentView) => {
      const guide = segmentView.guide;
      const guideActive = time >= guide.startTime && time <= guide.endTime;
      guide.container.visible = guideActive && this.options.tuning.showGuide && !this.options.tuning.showOnlyText;
      if (guideActive) {
        const guideProgress = clamp01(
          (time - guide.startTime) / Math.max(1e-3, guide.endTime - guide.startTime)
        );
        if (guide.update) {
          guide.container.alpha = guide.maxAlpha;
          guide.update(guideProgress);
        } else {
          const eased = easeSonnetInOut(guideProgress);
          guide.container.alpha = Math.sin(eased * Math.PI) * guide.maxAlpha;
          guide.container.scale.set(0.76 + eased * 0.24);
        }
      }
      segmentView.glyphs.forEach((glyph) => {
        const glyphProgress = resolveSegmentProgress(
          glyph.startTime,
          glyph.settleTime,
          time
        );
        const waiting = time < glyph.startTime;
        const offset = (1 - glyphProgress) * motion;
        const coreAlpha = waiting ? 0 : 0.16 + glyphProgress * 0.84;
        const haloAlpha = waiting ? 0 : 1 - glyphProgress * 0.28;
        const scale = isSonnetEmphasisRole(segmentView.role) && view.shot.kind === "type-impact" ? 0.52 + glyphProgress * 0.48 : 0.86 + glyphProgress * 0.14;
        const x = glyph.baseX + glyph.enterX * offset;
        const y = glyph.baseY + glyph.enterY * offset;
        const rotation = glyph.finalRotation + glyph.entryRotation * offset;
        const isGiantDecorativeText = segmentView.role === "decoration";
        const showTextGlyph = glyph.isTextGlyph !== false;
        const glyphVisible = this.options.tuning.showOnlyText ? showTextGlyph && (!isGiantDecorativeText || this.options.tuning.showGiantDecorativeText) : (!glyph.isBackgroundShape || this.options.tuning.showBackgroundDecor) && (!isGiantDecorativeText || this.options.tuning.showGiantDecorativeText);
        const depth = glyph.zDepth || 0;
        const parallaxX = (cameraFrame.x * width + shake.x * width) * camera * depth * 2.5;
        const parallaxY = (cameraFrame.y * height + shake.y * height) * camera * depth * 2.5;
        const depthScale = 1 + depth * 0.45;
        glyph.display.alpha = coreAlpha;
        glyph.display.visible = glyphVisible;
        glyph.display.scale.set(scale * depthScale);
        glyph.display.position.set(x + parallaxX, y + parallaxY);
        glyph.display.rotation = rotation;
        if (glyph.halo) {
          glyph.halo.alpha = haloAlpha;
          glyph.halo.scale.set(scale * (1.08 - glyphProgress * 0.08));
          glyph.halo.position.set(x, y);
          glyph.halo.rotation = rotation;
        }
        if (glyph.caCyan && glyph.caRed && glyph.caOffset) {
          glyph.caCyan.visible = glyphVisible && !this.options.tuning.showOnlyText;
          glyph.caRed.visible = glyphVisible && !this.options.tuning.showOnlyText;
          const mergeEased = easeSonnetInOut(glyphProgress);
          const currentOffset = glyph.caOffset * (1 - mergeEased * 0.8);
          glyph.caCyan.position.set(-currentOffset, currentOffset * 0.5);
          glyph.caRed.position.set(currentOffset, -currentOffset * 0.5);
        }
        glyph.updateAnimation?.(time);
      });
    });
  }
  renderOnce() {
    if (this.destroyed || !this.app.canvas.isConnected) return;
    this.renderFrame();
    if (this.destroyed) return;
    this.app.renderer.render(this.app.stage);
    this.options.onVisualFrame?.();
  }
  getCanvas() {
    return this.app.canvas;
  }
  setPaused(paused) {
    if (this.destroyed) return;
    this.options.paused = paused;
    if (paused) {
      this.app.stop();
      this.renderOnce();
    } else {
      this.app.start();
    }
  }
  destroy() {
    if (this.destroyed) return;
    this.destroyed = true;
    this.resizeObserver?.disconnect();
    this.resizeObserver = null;
    this.app.stop();
    this.app.ticker.remove(this.renderFrame);
    this.app.ticker.remove(this.publishVisualFrame);
    this.clearScenes();
    destroySonnetContainerChildren(this.creditsContainer);
    this.iconTextures.clear();
    const texturePool = getSonnetTexturePool(this.pixi);
    this.iconUrls.forEach((url) => {
      texturePool.release(url);
    });
    this.iconUrls.clear();
    this.app.destroy({ removeView: true }, { children: true, texture: true });
  }
};
export {
  SonnetPixiRuntime
};
/*! Bundled license information:

react-dom/cjs/react-dom-server-legacy.browser.production.js:
  (**
   * @license React
   * react-dom-server-legacy.browser.production.js
   *
   * Copyright (c) Meta Platforms, Inc. and affiliates.
   *
   * This source code is licensed under the MIT license found in the
   * LICENSE file in the root directory of this source tree.
   *)

react-dom/cjs/react-dom-server.browser.production.js:
  (**
   * @license React
   * react-dom-server.browser.production.js
   *
   * Copyright (c) Meta Platforms, Inc. and affiliates.
   *
   * This source code is licensed under the MIT license found in the
   * LICENSE file in the root directory of this source tree.
   *)
*/
