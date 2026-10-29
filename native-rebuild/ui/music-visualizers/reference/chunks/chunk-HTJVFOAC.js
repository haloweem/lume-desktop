import {
  __commonJS,
  __toESM
} from "./chunk-XCBH6NLF.js";

// node_modules/eventemitter3/index.js
var require_eventemitter3 = __commonJS({
  "node_modules/eventemitter3/index.js"(exports, module) {
    "use strict";
    var has = Object.prototype.hasOwnProperty;
    var prefix = "~";
    function Events() {
    }
    if (Object.create) {
      Events.prototype = /* @__PURE__ */ Object.create(null);
      if (!new Events().__proto__) prefix = false;
    }
    function EE(fn, context2, once) {
      this.fn = fn;
      this.context = context2;
      this.once = once || false;
    }
    function addListener(emitter, event, fn, context2, once) {
      if (typeof fn !== "function") {
        throw new TypeError("The listener must be a function");
      }
      var listener = new EE(fn, context2 || emitter, once), evt = prefix ? prefix + event : event;
      if (!emitter._events[evt]) emitter._events[evt] = listener, emitter._eventsCount++;
      else if (!emitter._events[evt].fn) emitter._events[evt].push(listener);
      else emitter._events[evt] = [emitter._events[evt], listener];
      return emitter;
    }
    function clearEvent(emitter, evt) {
      if (--emitter._eventsCount === 0) emitter._events = new Events();
      else delete emitter._events[evt];
    }
    function EventEmitter2() {
      this._events = new Events();
      this._eventsCount = 0;
    }
    EventEmitter2.prototype.eventNames = function eventNames() {
      var names = [], events, name;
      if (this._eventsCount === 0) return names;
      for (name in events = this._events) {
        if (has.call(events, name)) names.push(prefix ? name.slice(1) : name);
      }
      if (Object.getOwnPropertySymbols) {
        return names.concat(Object.getOwnPropertySymbols(events));
      }
      return names;
    };
    EventEmitter2.prototype.listeners = function listeners(event) {
      var evt = prefix ? prefix + event : event, handlers = this._events[evt];
      if (!handlers) return [];
      if (handlers.fn) return [handlers.fn];
      for (var i = 0, l = handlers.length, ee = new Array(l); i < l; i++) {
        ee[i] = handlers[i].fn;
      }
      return ee;
    };
    EventEmitter2.prototype.listenerCount = function listenerCount(event) {
      var evt = prefix ? prefix + event : event, listeners = this._events[evt];
      if (!listeners) return 0;
      if (listeners.fn) return 1;
      return listeners.length;
    };
    EventEmitter2.prototype.emit = function emit(event, a1, a2, a3, a4, a5) {
      var evt = prefix ? prefix + event : event;
      if (!this._events[evt]) return false;
      var listeners = this._events[evt], len = arguments.length, args, i;
      if (listeners.fn) {
        if (listeners.once) this.removeListener(event, listeners.fn, void 0, true);
        switch (len) {
          case 1:
            return listeners.fn.call(listeners.context), true;
          case 2:
            return listeners.fn.call(listeners.context, a1), true;
          case 3:
            return listeners.fn.call(listeners.context, a1, a2), true;
          case 4:
            return listeners.fn.call(listeners.context, a1, a2, a3), true;
          case 5:
            return listeners.fn.call(listeners.context, a1, a2, a3, a4), true;
          case 6:
            return listeners.fn.call(listeners.context, a1, a2, a3, a4, a5), true;
        }
        for (i = 1, args = new Array(len - 1); i < len; i++) {
          args[i - 1] = arguments[i];
        }
        listeners.fn.apply(listeners.context, args);
      } else {
        var length = listeners.length, j;
        for (i = 0; i < length; i++) {
          if (listeners[i].once) this.removeListener(event, listeners[i].fn, void 0, true);
          switch (len) {
            case 1:
              listeners[i].fn.call(listeners[i].context);
              break;
            case 2:
              listeners[i].fn.call(listeners[i].context, a1);
              break;
            case 3:
              listeners[i].fn.call(listeners[i].context, a1, a2);
              break;
            case 4:
              listeners[i].fn.call(listeners[i].context, a1, a2, a3);
              break;
            default:
              if (!args) for (j = 1, args = new Array(len - 1); j < len; j++) {
                args[j - 1] = arguments[j];
              }
              listeners[i].fn.apply(listeners[i].context, args);
          }
        }
      }
      return true;
    };
    EventEmitter2.prototype.on = function on(event, fn, context2) {
      return addListener(this, event, fn, context2, false);
    };
    EventEmitter2.prototype.once = function once(event, fn, context2) {
      return addListener(this, event, fn, context2, true);
    };
    EventEmitter2.prototype.removeListener = function removeListener(event, fn, context2, once) {
      var evt = prefix ? prefix + event : event;
      if (!this._events[evt]) return this;
      if (!fn) {
        clearEvent(this, evt);
        return this;
      }
      var listeners = this._events[evt];
      if (listeners.fn) {
        if (listeners.fn === fn && (!once || listeners.once) && (!context2 || listeners.context === context2)) {
          clearEvent(this, evt);
        }
      } else {
        for (var i = 0, events = [], length = listeners.length; i < length; i++) {
          if (listeners[i].fn !== fn || once && !listeners[i].once || context2 && listeners[i].context !== context2) {
            events.push(listeners[i]);
          }
        }
        if (events.length) this._events[evt] = events.length === 1 ? events[0] : events;
        else clearEvent(this, evt);
      }
      return this;
    };
    EventEmitter2.prototype.removeAllListeners = function removeAllListeners(event) {
      var evt;
      if (event) {
        evt = prefix ? prefix + event : event;
        if (this._events[evt]) clearEvent(this, evt);
      } else {
        this._events = new Events();
        this._eventsCount = 0;
      }
      return this;
    };
    EventEmitter2.prototype.off = EventEmitter2.prototype.removeListener;
    EventEmitter2.prototype.addListener = EventEmitter2.prototype.on;
    EventEmitter2.prefixed = prefix;
    EventEmitter2.EventEmitter = EventEmitter2;
    if ("undefined" !== typeof module) {
      module.exports = EventEmitter2;
    }
  }
});

// node_modules/pixi.js/lib/extensions/Extensions.mjs
var ExtensionType = /* @__PURE__ */ ((ExtensionType2) => {
  ExtensionType2["Application"] = "application";
  ExtensionType2["WebGLPipes"] = "webgl-pipes";
  ExtensionType2["WebGLPipesAdaptor"] = "webgl-pipes-adaptor";
  ExtensionType2["WebGLSystem"] = "webgl-system";
  ExtensionType2["WebGPUPipes"] = "webgpu-pipes";
  ExtensionType2["WebGPUPipesAdaptor"] = "webgpu-pipes-adaptor";
  ExtensionType2["WebGPUSystem"] = "webgpu-system";
  ExtensionType2["CanvasSystem"] = "canvas-system";
  ExtensionType2["CanvasPipesAdaptor"] = "canvas-pipes-adaptor";
  ExtensionType2["CanvasPipes"] = "canvas-pipes";
  ExtensionType2["Asset"] = "asset";
  ExtensionType2["LoadParser"] = "load-parser";
  ExtensionType2["ResolveParser"] = "resolve-parser";
  ExtensionType2["CacheParser"] = "cache-parser";
  ExtensionType2["DetectionParser"] = "detection-parser";
  ExtensionType2["MaskEffect"] = "mask-effect";
  ExtensionType2["BlendMode"] = "blend-mode";
  ExtensionType2["TextureSource"] = "texture-source";
  ExtensionType2["TextureUploaderWebGL"] = "texture-uploader-webgl";
  ExtensionType2["TextureUploaderWebGPU"] = "texture-uploader-webgpu";
  ExtensionType2["Environment"] = "environment";
  ExtensionType2["ShapeBuilder"] = "shape-builder";
  ExtensionType2["Batcher"] = "batcher";
  return ExtensionType2;
})(ExtensionType || {});
var normalizeExtension = (ext) => {
  if (typeof ext === "function" || typeof ext === "object" && ext.extension) {
    if (!ext.extension) {
      throw new Error("Extension class must have an extension object");
    }
    const metadata = typeof ext.extension !== "object" ? { type: ext.extension } : ext.extension;
    ext = { ...metadata, ref: ext };
  }
  if (typeof ext === "object") {
    ext = { ...ext };
  } else {
    throw new Error("Invalid extension type");
  }
  if (typeof ext.type === "string") {
    ext.type = [ext.type];
  }
  return ext;
};
var normalizeExtensionPriority = (ext, defaultPriority) => normalizeExtension(ext).priority ?? defaultPriority;
var extensions = {
  /** @ignore */
  _addHandlers: {},
  /** @ignore */
  _removeHandlers: {},
  /** @ignore */
  _queue: {},
  /**
   * Remove extensions from PixiJS.
   * @param extensions - Extensions to be removed. Can be:
   * - Extension class with static `extension` property
   * - Extension format object with `type` and `ref`
   * - Multiple extensions as separate arguments
   * @returns {extensions} this for chaining
   * @example
   * ```ts
   * // Remove a single extension
   * extensions.remove(MyRendererPlugin);
   *
   * // Remove multiple extensions
   * extensions.remove(
   *     MyRendererPlugin,
   *     MySystemPlugin
   * );
   * ```
   * @see {@link ExtensionType} For available extension types
   * @see {@link ExtensionFormat} For extension format details
   */
  remove(...extensions2) {
    extensions2.map(normalizeExtension).forEach((ext) => {
      ext.type.forEach((type) => this._removeHandlers[type]?.(ext));
    });
    return this;
  },
  /**
   * Register new extensions with PixiJS. Extensions can be registered in multiple formats:
   * - As a class with a static `extension` property
   * - As an extension format object
   * - As multiple extensions passed as separate arguments
   * @param extensions - Extensions to add to PixiJS. Each can be:
   * - A class with static `extension` property
   * - An extension format object with `type` and `ref`
   * - Multiple extensions as separate arguments
   * @returns This extensions instance for chaining
   * @example
   * ```ts
   * // Register a simple extension
   * extensions.add(MyRendererPlugin);
   *
   * // Register multiple extensions
   * extensions.add(
   *     MyRendererPlugin,
   *     MySystemPlugin,
   * });
   * ```
   * @see {@link ExtensionType} For available extension types
   * @see {@link ExtensionFormat} For extension format details
   * @see {@link extensions.remove} For removing registered extensions
   */
  add(...extensions2) {
    extensions2.map(normalizeExtension).forEach((ext) => {
      ext.type.forEach((type) => {
        const handlers = this._addHandlers;
        const queue = this._queue;
        if (!handlers[type]) {
          queue[type] = queue[type] || [];
          queue[type]?.push(ext);
        } else {
          handlers[type]?.(ext);
        }
      });
    });
    return this;
  },
  /**
   * Internal method to handle extensions by name.
   * @param type - The extension type.
   * @param onAdd  - Function handler when extensions are added/registered {@link StrictExtensionFormat}.
   * @param onRemove  - Function handler when extensions are removed/unregistered {@link StrictExtensionFormat}.
   * @returns this for chaining.
   * @internal
   * @ignore
   */
  handle(type, onAdd, onRemove) {
    const addHandlers = this._addHandlers;
    const removeHandlers = this._removeHandlers;
    if (addHandlers[type] || removeHandlers[type]) {
      throw new Error(`Extension type ${type} already has a handler`);
    }
    addHandlers[type] = onAdd;
    removeHandlers[type] = onRemove;
    const queue = this._queue;
    if (queue[type]) {
      queue[type]?.forEach((ext) => onAdd(ext));
      delete queue[type];
    }
    return this;
  },
  /**
   * Handle a type, but using a map by `name` property.
   * @param type - Type of extension to handle.
   * @param map - The object map of named extensions.
   * @returns this for chaining.
   * @ignore
   */
  handleByMap(type, map) {
    return this.handle(
      type,
      (extension) => {
        if (extension.name) {
          map[extension.name] = extension.ref;
        }
      },
      (extension) => {
        if (extension.name) {
          delete map[extension.name];
        }
      }
    );
  },
  /**
   * Handle a type, but using a list of extensions with a `name` property.
   * @param type - Type of extension to handle.
   * @param map - The array of named extensions.
   * @param defaultPriority - Fallback priority if none is defined.
   * @returns this for chaining.
   * @ignore
   */
  handleByNamedList(type, map, defaultPriority = -1) {
    return this.handle(
      type,
      (extension) => {
        const index = map.findIndex((item) => item.name === extension.name);
        if (index >= 0) return;
        map.push({ name: extension.name, value: extension.ref });
        map.sort((a, b) => normalizeExtensionPriority(b.value, defaultPriority) - normalizeExtensionPriority(a.value, defaultPriority));
      },
      (extension) => {
        const index = map.findIndex((item) => item.name === extension.name);
        if (index !== -1) {
          map.splice(index, 1);
        }
      }
    );
  },
  /**
   * Handle a type, but using a list of extensions.
   * @param type - Type of extension to handle.
   * @param list - The list of extensions.
   * @param defaultPriority - The default priority to use if none is specified.
   * @returns this for chaining.
   * @ignore
   */
  handleByList(type, list, defaultPriority = -1) {
    return this.handle(
      type,
      (extension) => {
        if (list.includes(extension.ref)) {
          return;
        }
        list.push(extension.ref);
        list.sort((a, b) => normalizeExtensionPriority(b, defaultPriority) - normalizeExtensionPriority(a, defaultPriority));
      },
      (extension) => {
        const index = list.indexOf(extension.ref);
        if (index !== -1) {
          list.splice(index, 1);
        }
      }
    );
  },
  /**
   * Mixin the source object(s) properties into the target class's prototype.
   * Copies all property descriptors from source objects to the target's prototype.
   * @param Target - The target class to mix properties into
   * @param sources - One or more source objects containing properties to mix in
   * @example
   * ```ts
   * // Create a mixin with shared properties
   * const moveable = {
   *     x: 0,
   *     y: 0,
   *     move(x: number, y: number) {
   *         this.x += x;
   *         this.y += y;
   *     }
   * };
   *
   * // Create a mixin with computed properties
   * const scalable = {
   *     scale: 1,
   *     get scaled() {
   *         return this.scale > 1;
   *     }
   * };
   *
   * // Apply mixins to a class
   * extensions.mixin(Sprite, moveable, scalable);
   *
   * // Use mixed-in properties
   * const sprite = new Sprite();
   * sprite.move(10, 20);
   * console.log(sprite.x, sprite.y); // 10, 20
   * ```
   * @remarks
   * - Copies all properties including getters/setters
   * - Does not modify source objects
   * - Preserves property descriptors
   * @see {@link Object.defineProperties} For details on property descriptors
   * @see {@link Object.getOwnPropertyDescriptors} For details on property copying
   */
  mixin(Target, ...sources) {
    for (const source of sources) {
      Object.defineProperties(Target.prototype, Object.getOwnPropertyDescriptors(source));
    }
  }
};

// node_modules/pixi.js/lib/rendering/renderers/shared/utils/createIdFromString.mjs
var idCounts = /* @__PURE__ */ Object.create(null);
var idHash = /* @__PURE__ */ Object.create(null);
function createIdFromString(value, groupId) {
  let id = idHash[value];
  if (id === void 0) {
    if (idCounts[groupId] === void 0) {
      idCounts[groupId] = 1;
    }
    idHash[value] = id = idCounts[groupId]++;
  }
  return id;
}

// node_modules/pixi.js/lib/environment-browser/BrowserAdapter.mjs
var BrowserAdapter = {
  createCanvas: (width, height) => {
    const canvas = document.createElement("canvas");
    canvas.width = width;
    canvas.height = height;
    return canvas;
  },
  createImage: () => new Image(),
  getCanvasRenderingContext2D: () => CanvasRenderingContext2D,
  getWebGLRenderingContext: () => WebGLRenderingContext,
  getNavigator: () => navigator,
  getBaseUrl: () => document.baseURI ?? window.location.href,
  getFontFaceSet: () => document.fonts,
  fetch: (url, options) => fetch(url, options),
  parseXML: (xml) => {
    const parser = new DOMParser();
    return parser.parseFromString(xml, "text/xml");
  }
};

// node_modules/pixi.js/lib/environment/adapter.mjs
var currentAdapter = BrowserAdapter;
var DOMAdapter = {
  /**
   * Returns the current adapter.
   * @returns {environment.Adapter} The current adapter.
   */
  get() {
    return currentAdapter;
  },
  /**
   * Sets the current adapter.
   * @param adapter - The new adapter.
   */
  set(adapter) {
    currentAdapter = adapter;
  }
};

// node_modules/pixi.js/lib/rendering/renderers/gl/shader/program/getTestContext.mjs
var context;
function getTestContext() {
  if (!context || context?.isContextLost()) {
    const canvas = DOMAdapter.get().createCanvas();
    context = canvas.getContext("webgl", {});
  }
  return context;
}

// node_modules/pixi.js/lib/rendering/renderers/gl/shader/program/getMaxFragmentPrecision.mjs
var maxFragmentPrecision;
function getMaxFragmentPrecision() {
  if (!maxFragmentPrecision) {
    maxFragmentPrecision = "mediump";
    const gl = getTestContext();
    if (gl) {
      if (gl.getShaderPrecisionFormat) {
        const shaderFragment = gl.getShaderPrecisionFormat(gl.FRAGMENT_SHADER, gl.HIGH_FLOAT);
        maxFragmentPrecision = shaderFragment.precision ? "highp" : "mediump";
      }
    }
  }
  return maxFragmentPrecision;
}

// node_modules/pixi.js/lib/rendering/renderers/gl/shader/program/preprocessors/addProgramDefines.mjs
function addProgramDefines(src, isES300, isFragment) {
  if (isES300) return src;
  if (isFragment) {
    src = src.replace("out vec4 finalColor;", "");
    return `

        #ifdef GL_ES // This checks if it is WebGL1
        #define in varying
        #define finalColor gl_FragColor
        #define texture texture2D
        #endif
        ${src}
        `;
  }
  return `

        #ifdef GL_ES // This checks if it is WebGL1
        #define in attribute
        #define out varying
        #endif
        ${src}
        `;
}

// node_modules/pixi.js/lib/rendering/renderers/gl/shader/program/preprocessors/ensurePrecision.mjs
function ensurePrecision(src, options, isFragment) {
  const maxSupportedPrecision = isFragment ? options.maxSupportedFragmentPrecision : options.maxSupportedVertexPrecision;
  if (src.substring(0, 9) !== "precision") {
    let precision = isFragment ? options.requestedFragmentPrecision : options.requestedVertexPrecision;
    if (precision === "highp" && maxSupportedPrecision !== "highp") {
      precision = "mediump";
    }
    return `precision ${precision} float;
${src}`;
  } else if (maxSupportedPrecision !== "highp" && src.substring(0, 15) === "precision highp") {
    return src.replace("precision highp", "precision mediump");
  }
  return src;
}

// node_modules/pixi.js/lib/rendering/renderers/gl/shader/program/preprocessors/insertVersion.mjs
function insertVersion(src, isES300) {
  if (!isES300) return src;
  return `#version 300 es
${src}`;
}

// node_modules/pixi.js/lib/rendering/renderers/gl/shader/program/preprocessors/setProgramName.mjs
var fragmentNameCache = {};
var VertexNameCache = {};
function setProgramName(src, { name = `pixi-program` }, isFragment = true) {
  name = name.replace(/\s+/g, "-");
  name += isFragment ? "-fragment" : "-vertex";
  const nameCache = isFragment ? fragmentNameCache : VertexNameCache;
  if (nameCache[name]) {
    nameCache[name]++;
    name += `-${nameCache[name]}`;
  } else {
    nameCache[name] = 1;
  }
  if (src.indexOf("#define SHADER_NAME") !== -1) return src;
  const shaderName = `#define SHADER_NAME ${name}`;
  return `${shaderName}
${src}`;
}

// node_modules/pixi.js/lib/rendering/renderers/gl/shader/program/preprocessors/stripVersion.mjs
function stripVersion(src, isES300) {
  if (!isES300) return src;
  return src.replace("#version 300 es", "");
}

// node_modules/pixi.js/lib/rendering/renderers/gl/shader/GlProgram.mjs
var processes = {
  // strips any version headers..
  stripVersion,
  // adds precision string if not already present
  ensurePrecision,
  // add some defines if WebGL1 to make it more compatible with WebGL2 shaders
  addProgramDefines,
  // add the program name to the shader
  setProgramName,
  // add the version string to the shader header
  insertVersion
};
var programCache = /* @__PURE__ */ Object.create(null);
var _GlProgram = class _GlProgram2 {
  /**
   * Creates a shiny new GlProgram. Used by WebGL renderer.
   * @param options - The options for the program.
   */
  constructor(options) {
    options = { ..._GlProgram2.defaultOptions, ...options };
    const isES300 = options.fragment.indexOf("#version 300 es") !== -1;
    const preprocessorOptions = {
      stripVersion: isES300,
      ensurePrecision: {
        requestedFragmentPrecision: options.preferredFragmentPrecision,
        requestedVertexPrecision: options.preferredVertexPrecision,
        maxSupportedVertexPrecision: "highp",
        maxSupportedFragmentPrecision: getMaxFragmentPrecision()
      },
      setProgramName: {
        name: options.name
      },
      addProgramDefines: isES300,
      insertVersion: isES300
    };
    let fragment = options.fragment;
    let vertex = options.vertex;
    Object.keys(processes).forEach((processKey) => {
      const processOptions = preprocessorOptions[processKey];
      fragment = processes[processKey](fragment, processOptions, true);
      vertex = processes[processKey](vertex, processOptions, false);
    });
    this.fragment = fragment;
    this.vertex = vertex;
    this.transformFeedbackVaryings = options.transformFeedbackVaryings;
    this._key = createIdFromString(`${this.vertex}:${this.fragment}`, "gl-program");
  }
  /** destroys the program */
  destroy() {
    this.fragment = null;
    this.vertex = null;
    this._attributeData = null;
    this._uniformData = null;
    this._uniformBlockData = null;
    this.transformFeedbackVaryings = null;
    programCache[this._cacheKey] = null;
  }
  /**
   * Helper function that creates a program for a given source.
   * It will check the program cache if the program has already been created.
   * If it has that one will be returned, if not a new one will be created and cached.
   * @param options - The options for the program.
   * @returns A program using the same source
   */
  static from(options) {
    const key = `${options.vertex}:${options.fragment}`;
    if (!programCache[key]) {
      programCache[key] = new _GlProgram2(options);
      programCache[key]._cacheKey = key;
    }
    return programCache[key];
  }
};
_GlProgram.defaultOptions = {
  preferredVertexPrecision: "highp",
  preferredFragmentPrecision: "mediump"
};
var GlProgram = _GlProgram;

// node_modules/pixi.js/lib/rendering/renderers/shared/geometry/utils/getAttributeInfoFromFormat.mjs
var attributeFormatData = {
  uint8x2: { size: 2, stride: 2, normalised: false },
  uint8x4: { size: 4, stride: 4, normalised: false },
  sint8x2: { size: 2, stride: 2, normalised: false },
  sint8x4: { size: 4, stride: 4, normalised: false },
  unorm8x2: { size: 2, stride: 2, normalised: true },
  unorm8x4: { size: 4, stride: 4, normalised: true },
  snorm8x2: { size: 2, stride: 2, normalised: true },
  snorm8x4: { size: 4, stride: 4, normalised: true },
  uint16x2: { size: 2, stride: 4, normalised: false },
  uint16x4: { size: 4, stride: 8, normalised: false },
  sint16x2: { size: 2, stride: 4, normalised: false },
  sint16x4: { size: 4, stride: 8, normalised: false },
  unorm16x2: { size: 2, stride: 4, normalised: true },
  unorm16x4: { size: 4, stride: 8, normalised: true },
  snorm16x2: { size: 2, stride: 4, normalised: true },
  snorm16x4: { size: 4, stride: 8, normalised: true },
  float16x2: { size: 2, stride: 4, normalised: false },
  float16x4: { size: 4, stride: 8, normalised: false },
  float32: { size: 1, stride: 4, normalised: false },
  float32x2: { size: 2, stride: 8, normalised: false },
  float32x3: { size: 3, stride: 12, normalised: false },
  float32x4: { size: 4, stride: 16, normalised: false },
  uint32: { size: 1, stride: 4, normalised: false },
  uint32x2: { size: 2, stride: 8, normalised: false },
  uint32x3: { size: 3, stride: 12, normalised: false },
  uint32x4: { size: 4, stride: 16, normalised: false },
  sint32: { size: 1, stride: 4, normalised: false },
  sint32x2: { size: 2, stride: 8, normalised: false },
  sint32x3: { size: 3, stride: 12, normalised: false },
  sint32x4: { size: 4, stride: 16, normalised: false }
};
function getAttributeInfoFromFormat(format) {
  return attributeFormatData[format] ?? attributeFormatData.float32;
}

// node_modules/pixi.js/lib/rendering/renderers/gpu/shader/utils/extractAttributesFromGpuProgram.mjs
var WGSL_TO_VERTEX_TYPES = {
  f32: "float32",
  "vec2<f32>": "float32x2",
  "vec3<f32>": "float32x3",
  "vec4<f32>": "float32x4",
  vec2f: "float32x2",
  vec3f: "float32x3",
  vec4f: "float32x4",
  i32: "sint32",
  "vec2<i32>": "sint32x2",
  "vec3<i32>": "sint32x3",
  "vec4<i32>": "sint32x4",
  vec2i: "sint32x2",
  vec3i: "sint32x3",
  vec4i: "sint32x4",
  u32: "uint32",
  "vec2<u32>": "uint32x2",
  "vec3<u32>": "uint32x3",
  "vec4<u32>": "uint32x4",
  vec2u: "uint32x2",
  vec3u: "uint32x3",
  vec4u: "uint32x4",
  bool: "uint32",
  "vec2<bool>": "uint32x2",
  "vec3<bool>": "uint32x3",
  "vec4<bool>": "uint32x4"
};
var LOCATION_REGEX = /@location\((\d+)\)\s+([a-zA-Z0-9_]+)\s*:\s*([a-zA-Z0-9_<>]+)(?:,|\s|\)|$)/g;
function parseLocations(str, results) {
  let match;
  while ((match = LOCATION_REGEX.exec(str)) !== null) {
    const format = WGSL_TO_VERTEX_TYPES[match[3]] ?? "float32";
    results[match[2]] = {
      location: parseInt(match[1], 10),
      format,
      stride: getAttributeInfoFromFormat(format).stride,
      offset: 0,
      instance: false,
      start: 0
    };
  }
  LOCATION_REGEX.lastIndex = 0;
}
function stripComments(source) {
  return source.replace(/\/\/.*$/gm, "").replace(/\/\*[\s\S]*?\*\//g, "");
}
function extractAttributesFromGpuProgram({ source, entryPoint }) {
  const results = {};
  const cleanSource = stripComments(source);
  const mainVertStart = cleanSource.indexOf(`fn ${entryPoint}(`);
  if (mainVertStart === -1) {
    return results;
  }
  const arrowFunctionStart = cleanSource.indexOf("->", mainVertStart);
  if (arrowFunctionStart === -1) {
    return results;
  }
  const functionArgsSubstring = cleanSource.substring(mainVertStart, arrowFunctionStart);
  parseLocations(functionArgsSubstring, results);
  if (Object.keys(results).length === 0) {
    const structMatch = functionArgsSubstring.match(/\(\s*\w+\s*:\s*(\w+)/);
    if (structMatch) {
      const structName = structMatch[1];
      const structRegex = new RegExp(`struct\\s+${structName}\\s*\\{([^}]+)\\}`, "s");
      const structBody = cleanSource.match(structRegex);
      if (structBody) {
        parseLocations(structBody[1], results);
      }
    }
  }
  return results;
}

// node_modules/pixi.js/lib/rendering/renderers/gpu/shader/utils/extractStructAndGroups.mjs
function extractStructAndGroups(wgsl) {
  const linePattern = /(^|[^/])@(group|binding)\(\d+\)[^;]+;/g;
  const groupPattern = /@group\((\d+)\)/;
  const bindingPattern = /@binding\((\d+)\)/;
  const namePattern = /var(<[^>]+>)? (\w+)/;
  const typePattern = /:\s*([\w<>]+)/;
  const structPattern = /struct\s+(\w+)\s*{([^}]+)}/g;
  const structMemberPattern = /(\w+)\s*:\s*([\w\<\>]+)/g;
  const structName = /struct\s+(\w+)/;
  const groups = wgsl.match(linePattern)?.map((item) => {
    const varMatch = item.match(namePattern);
    const varQualifier = varMatch?.[1] ?? "";
    let accessMode;
    if (varQualifier === "<uniform>") {
      accessMode = "uniform";
    } else if (varQualifier.startsWith("<storage")) {
      accessMode = "storage";
    }
    return {
      group: parseInt(item.match(groupPattern)[1], 10),
      binding: parseInt(item.match(bindingPattern)[1], 10),
      name: varMatch[2],
      accessMode,
      type: item.match(typePattern)[1]
    };
  });
  if (!groups) {
    return {
      groups: [],
      structs: []
    };
  }
  const structs = wgsl.match(structPattern)?.map((struct) => {
    const name = struct.match(structName)[1];
    const members = struct.match(structMemberPattern).reduce((acc, member) => {
      const [name2, type] = member.split(":");
      acc[name2.trim()] = type.trim();
      return acc;
    }, {});
    if (!members) {
      return null;
    }
    return { name, members };
  }).filter(({ name }) => groups.some(
    (group) => (
      // Handle both direct type matches and generic types like array<StructName>
      group.type === name || group.type.includes(`<${name}>`)
    )
  )) ?? [];
  return {
    groups,
    structs
  };
}

// node_modules/pixi.js/lib/rendering/renderers/shared/shader/const.mjs
var ShaderStage = /* @__PURE__ */ ((ShaderStage2) => {
  ShaderStage2[ShaderStage2["VERTEX"] = 1] = "VERTEX";
  ShaderStage2[ShaderStage2["FRAGMENT"] = 2] = "FRAGMENT";
  ShaderStage2[ShaderStage2["COMPUTE"] = 4] = "COMPUTE";
  return ShaderStage2;
})(ShaderStage || {});

// node_modules/pixi.js/lib/rendering/renderers/gpu/shader/utils/generateGpuLayoutGroups.mjs
function generateGpuLayoutGroups({ groups }) {
  const layout = [];
  for (let i = 0; i < groups.length; i++) {
    const group = groups[i];
    if (!layout[group.group]) {
      layout[group.group] = [];
    }
    if (group.accessMode === "uniform") {
      layout[group.group].push({
        binding: group.binding,
        visibility: ShaderStage.VERTEX | ShaderStage.FRAGMENT,
        buffer: {
          type: "uniform"
        }
      });
    } else if (group.accessMode === "storage") {
      layout[group.group].push({
        binding: group.binding,
        visibility: ShaderStage.VERTEX | ShaderStage.FRAGMENT,
        buffer: {
          type: "read-only-storage"
        }
      });
    } else if (group.type === "sampler") {
      layout[group.group].push({
        binding: group.binding,
        visibility: ShaderStage.VERTEX | ShaderStage.FRAGMENT,
        sampler: {
          type: "filtering"
        }
      });
    } else if (group.type === "sampler_comparison") {
      layout[group.group].push({
        binding: group.binding,
        visibility: ShaderStage.VERTEX | ShaderStage.FRAGMENT,
        sampler: {
          type: "comparison"
        }
      });
    } else if (group.type === "texture_2d" || group.type.startsWith("texture_2d<")) {
      layout[group.group].push({
        binding: group.binding,
        visibility: ShaderStage.VERTEX | ShaderStage.FRAGMENT,
        texture: {
          sampleType: "float",
          viewDimension: "2d",
          multisampled: false
        }
      });
    } else if (group.type === "texture_depth_2d") {
      layout[group.group].push({
        binding: group.binding,
        visibility: ShaderStage.VERTEX | ShaderStage.FRAGMENT,
        texture: {
          sampleType: "depth",
          viewDimension: "2d",
          multisampled: false
        }
      });
    } else if (group.type === "texture_depth_2d_array") {
      layout[group.group].push({
        binding: group.binding,
        visibility: ShaderStage.VERTEX | ShaderStage.FRAGMENT,
        texture: {
          sampleType: "depth",
          viewDimension: "2d-array",
          multisampled: false
        }
      });
    } else if (group.type === "texture_2d_array" || group.type.startsWith("texture_2d_array<")) {
      layout[group.group].push({
        binding: group.binding,
        visibility: ShaderStage.VERTEX | ShaderStage.FRAGMENT,
        texture: {
          sampleType: "float",
          viewDimension: "2d-array",
          multisampled: false
        }
      });
    } else if (group.type === "texture_cube" || group.type.startsWith("texture_cube<")) {
      layout[group.group].push({
        binding: group.binding,
        visibility: ShaderStage.VERTEX | ShaderStage.FRAGMENT,
        texture: {
          sampleType: "float",
          viewDimension: "cube",
          multisampled: false
        }
      });
    }
  }
  for (let i = 0; i < layout.length; i++) {
    layout[i] || (layout[i] = []);
  }
  return layout;
}

// node_modules/pixi.js/lib/rendering/renderers/gpu/shader/utils/generateLayoutHash.mjs
function generateLayoutHash({ groups }) {
  const layout = [];
  for (let i = 0; i < groups.length; i++) {
    const group = groups[i];
    if (!layout[group.group]) {
      layout[group.group] = {};
    }
    layout[group.group][group.name] = group.binding;
  }
  return layout;
}

// node_modules/pixi.js/lib/rendering/renderers/gpu/shader/utils/removeStructAndGroupDuplicates.mjs
function removeStructAndGroupDuplicates(vertexStructsAndGroups, fragmentStructsAndGroups) {
  const structNameSet = /* @__PURE__ */ new Set();
  const dupeGroupKeySet = /* @__PURE__ */ new Set();
  const structs = [...vertexStructsAndGroups.structs, ...fragmentStructsAndGroups.structs].filter((struct) => {
    if (structNameSet.has(struct.name)) {
      return false;
    }
    structNameSet.add(struct.name);
    return true;
  });
  const groups = [...vertexStructsAndGroups.groups, ...fragmentStructsAndGroups.groups].filter((group) => {
    const key = `${group.name}-${group.binding}`;
    if (dupeGroupKeySet.has(key)) {
      return false;
    }
    dupeGroupKeySet.add(key);
    return true;
  });
  return { structs, groups };
}

// node_modules/pixi.js/lib/rendering/renderers/gpu/shader/GpuProgram.mjs
var programCache2 = /* @__PURE__ */ Object.create(null);
var GpuProgram = class _GpuProgram {
  /**
   * Create a new GpuProgram
   * @param options - The options for the gpu program
   */
  constructor(options) {
    this._layoutKey = 0;
    this._attributeLocationsKey = 0;
    const { fragment, vertex, layout, gpuLayout, name } = options;
    this.name = name;
    this.fragment = fragment;
    this.vertex = vertex;
    if (fragment.source === vertex.source) {
      const structsAndGroups = extractStructAndGroups(fragment.source);
      this.structsAndGroups = structsAndGroups;
    } else {
      const vertexStructsAndGroups = extractStructAndGroups(vertex.source);
      const fragmentStructsAndGroups = extractStructAndGroups(fragment.source);
      this.structsAndGroups = removeStructAndGroupDuplicates(vertexStructsAndGroups, fragmentStructsAndGroups);
    }
    this.layout = layout ?? generateLayoutHash(this.structsAndGroups);
    this.gpuLayout = gpuLayout ?? generateGpuLayoutGroups(this.structsAndGroups);
    this.autoAssignGlobalUniforms = !!(this.layout[0]?.globalUniforms !== void 0);
    this.autoAssignLocalUniforms = !!(this.layout[1]?.localUniforms !== void 0);
    this._generateProgramKey();
  }
  // TODO maker this pure
  _generateProgramKey() {
    const { vertex, fragment } = this;
    const bigKey = vertex.source + fragment.source + vertex.entryPoint + fragment.entryPoint;
    this._layoutKey = createIdFromString(bigKey, "program");
  }
  get attributeData() {
    this._attributeData ?? (this._attributeData = extractAttributesFromGpuProgram(this.vertex));
    return this._attributeData;
  }
  /** destroys the program */
  destroy() {
    this.gpuLayout = null;
    this.layout = null;
    this.structsAndGroups = null;
    this.fragment = null;
    this.vertex = null;
    programCache2[this._cacheKey] = null;
  }
  /**
   * Helper function that creates a program for a given source.
   * It will check the program cache if the program has already been created.
   * If it has that one will be returned, if not a new one will be created and cached.
   * @param options - The options for the program.
   * @returns A program using the same source
   */
  static from(options) {
    const key = `${options.vertex.source}:${options.fragment.source}:${options.fragment.entryPoint}:${options.vertex.entryPoint}`;
    if (!programCache2[key]) {
      programCache2[key] = new _GpuProgram(options);
      programCache2[key]._cacheKey = key;
    }
    return programCache2[key];
  }
};

// node_modules/eventemitter3/index.mjs
var import_index = __toESM(require_eventemitter3(), 1);
var eventemitter3_default = import_index.default;

// node_modules/pixi.js/lib/utils/data/uid.mjs
var uidCache = {
  default: -1
};
function uid(name = "default") {
  if (uidCache[name] === void 0) {
    uidCache[name] = -1;
  }
  return ++uidCache[name];
}
function resetUids() {
  for (const key in uidCache) {
    delete uidCache[key];
  }
}

// node_modules/pixi.js/lib/rendering/renderers/shared/shader/types.mjs
var UNIFORM_TYPES_VALUES = [
  "f32",
  "i32",
  "u32",
  "vec2<f32>",
  "vec3<f32>",
  "vec4<f32>",
  "mat2x2<f32>",
  "mat3x3<f32>",
  "mat4x4<f32>",
  "mat3x2<f32>",
  "mat4x2<f32>",
  "mat2x3<f32>",
  "mat4x3<f32>",
  "mat2x4<f32>",
  "mat3x4<f32>",
  "vec2<i32>",
  "vec3<i32>",
  "vec4<i32>",
  "vec2<u32>",
  "vec3<u32>",
  "vec4<u32>"
];
var UNIFORM_TYPES_MAP = UNIFORM_TYPES_VALUES.reduce((acc, type) => {
  acc[type] = true;
  return acc;
}, {});

// node_modules/pixi.js/lib/rendering/renderers/shared/shader/utils/getDefaultUniformValue.mjs
function getDefaultUniformValue(type, size) {
  switch (type) {
    case "f32":
      return 0;
    case "vec2<f32>":
      return new Float32Array(2 * size);
    case "vec3<f32>":
      return new Float32Array(3 * size);
    case "vec4<f32>":
      return new Float32Array(4 * size);
    case "mat2x2<f32>":
      return new Float32Array([
        1,
        0,
        0,
        1
      ]);
    case "mat3x3<f32>":
      return new Float32Array([
        1,
        0,
        0,
        0,
        1,
        0,
        0,
        0,
        1
      ]);
    case "mat4x4<f32>":
      return new Float32Array([
        1,
        0,
        0,
        0,
        0,
        1,
        0,
        0,
        0,
        0,
        1,
        0,
        0,
        0,
        0,
        1
      ]);
  }
  return null;
}

// node_modules/pixi.js/lib/rendering/renderers/shared/shader/UniformGroup.mjs
var _UniformGroup = class _UniformGroup2 extends eventemitter3_default {
  /**
   * Create a new Uniform group
   * @param uniformStructures - The structures of the uniform group
   * @param options - The optional parameters of this uniform group
   */
  constructor(uniformStructures, options) {
    super();
    this._touched = 0;
    this.uid = uid("uniform");
    this._resourceType = "uniformGroup";
    this._resourceId = uid("resource");
    this.isUniformGroup = true;
    this._dirtyId = 0;
    this.destroyed = false;
    options = { ..._UniformGroup2.defaultOptions, ...options };
    this.uniformStructures = uniformStructures;
    const uniforms = {};
    for (const i in uniformStructures) {
      const uniformData = uniformStructures[i];
      uniformData.name = i;
      uniformData.size = uniformData.size ?? 1;
      if (!UNIFORM_TYPES_MAP[uniformData.type]) {
        const arrayMatch = uniformData.type.match(/^array<(\w+(?:<\w+>)?),\s*(\d+)>$/);
        if (arrayMatch) {
          const [, innerType, size] = arrayMatch;
          throw new Error(
            `Uniform type ${uniformData.type} is not supported. Use type: '${innerType}', size: ${size} instead.`
          );
        }
        throw new Error(`Uniform type ${uniformData.type} is not supported. Supported uniform types are: ${UNIFORM_TYPES_VALUES.join(", ")}`);
      }
      uniformData.value ?? (uniformData.value = getDefaultUniformValue(uniformData.type, uniformData.size));
      uniforms[i] = uniformData.value;
    }
    this.uniforms = uniforms;
    this._dirtyId = 1;
    this.ubo = options.ubo;
    this.isStatic = options.isStatic;
    this._signature = createIdFromString(Object.keys(uniforms).map(
      (i) => `${i}-${uniformStructures[i].type}`
    ).join("-"), "uniform-group");
  }
  /**
   * an underlying buffer that will be uploaded to the GPU when using this UniformGroup.
   * It is created lazily by the renderer's ubo system on first use.
   */
  get buffer() {
    return this._buffer;
  }
  set buffer(value) {
    if (this._buffer === value) return;
    this._buffer?.off("change", this.onBufferChange, this);
    this._buffer = value;
    value?.on("change", this.onBufferChange, this);
  }
  /**
   * The GC tracks this group's underlying buffer, not the group itself — a GC stamp on the
   * group (see BindGroup._touch) must land on the buffer, or the GC collects it while
   * cached bind groups still reference it.
   * @internal
   */
  get _gcLastUsed() {
    return this._buffer?._gcLastUsed ?? -1;
  }
  set _gcLastUsed(value) {
    if (this._buffer) this._buffer._gcLastUsed = value;
  }
  /**
   * Bind group keys are built from this group's _resourceId, not the buffer's — so when the
   * buffer re-keys (it resized, or the GC unloaded its GPU copy), this group must re-key too,
   * or cached GPUBindGroups keep referencing the destroyed GPU buffer.
   */
  onBufferChange() {
    this._resourceId = uid("resource");
    this.emit("change", this);
  }
  /** Call this if you want the uniform groups data to be uploaded to the GPU only useful if `isStatic` is true. */
  update() {
    this._dirtyId++;
  }
};
_UniformGroup.defaultOptions = {
  /** if true the UniformGroup is handled as an Uniform buffer object. */
  ubo: false,
  /** if true, then you are responsible for when the data is uploaded to the GPU by calling `update()` */
  isStatic: false
};
var UniformGroup = _UniformGroup;

// node_modules/pixi.js/lib/maths/misc/const.mjs
var PI_2 = Math.PI * 2;
var RAD_TO_DEG = 180 / Math.PI;
var DEG_TO_RAD = Math.PI / 180;

// node_modules/pixi.js/lib/maths/point/Point.mjs
var Point = class _Point {
  /**
   * Creates a new `Point`
   * @param {number} [x=0] - position of the point on the x axis
   * @param {number} [y=0] - position of the point on the y axis
   */
  constructor(x = 0, y = 0) {
    this.x = 0;
    this.y = 0;
    this.x = x;
    this.y = y;
  }
  /**
   * Creates a clone of this point, which is a new instance with the same `x` and `y` values.
   * @example
   * ```ts
   * // Basic point cloning
   * const original = new Point(100, 200);
   * const copy = original.clone();
   *
   * // Clone and modify
   * const modified = original.clone();
   * modified.set(300, 400);
   *
   * // Verify independence
   * console.log(original); // Point(100, 200)
   * console.log(modified); // Point(300, 400)
   * ```
   * @remarks
   * - Creates new Point instance
   * - Deep copies x and y values
   * - Independent from original
   * - Useful for preserving values
   * @returns A clone of this point
   * @see {@link Point.copyFrom} For copying into existing point
   * @see {@link Point.copyTo} For copying to existing point
   */
  clone() {
    return new _Point(this.x, this.y);
  }
  /**
   * Copies x and y from the given point into this point.
   * @example
   * ```ts
   * // Basic copying
   * const source = new Point(100, 200);
   * const target = new Point();
   * target.copyFrom(source);
   *
   * // Copy and chain operations
   * const point = new Point()
   *     .copyFrom(source)
   *     .set(x + 50, y + 50);
   *
   * // Copy from any PointData
   * const data = { x: 10, y: 20 };
   * point.copyFrom(data);
   * ```
   * @param p - The point to copy from
   * @returns The point instance itself
   * @see {@link Point.copyTo} For copying to another point
   * @see {@link Point.clone} For creating new point copy
   */
  copyFrom(p) {
    this.set(p.x, p.y);
    return this;
  }
  /**
   * Copies this point's x and y into the given point.
   * @example
   * ```ts
   * // Basic copying
   * const source = new Point(100, 200);
   * const target = new Point();
   * source.copyTo(target);
   * ```
   * @param p - The point to copy to. Can be any type that is or extends `PointLike`
   * @returns The point (`p`) with values updated
   * @see {@link Point.copyFrom} For copying from another point
   * @see {@link Point.clone} For creating new point copy
   */
  copyTo(p) {
    p.set(this.x, this.y);
    return p;
  }
  /**
   * Checks if another point is equal to this point.
   *
   * Compares x and y values using strict equality.
   * @example
   * ```ts
   * // Basic equality check
   * const p1 = new Point(100, 200);
   * const p2 = new Point(100, 200);
   * console.log(p1.equals(p2)); // true
   *
   * // Compare with PointData
   * const data = { x: 100, y: 200 };
   * console.log(p1.equals(data)); // true
   *
   * // Check different points
   * const p3 = new Point(200, 300);
   * console.log(p1.equals(p3)); // false
   * ```
   * @param p - The point to check
   * @returns `true` if both `x` and `y` are equal
   * @see {@link Point.copyFrom} For making points equal
   * @see {@link PointData} For point data interface
   */
  equals(p) {
    return p.x === this.x && p.y === this.y;
  }
  /**
   * Sets the point to a new x and y position.
   *
   * If y is omitted, both x and y will be set to x.
   * @example
   * ```ts
   * // Basic position setting
   * const point = new Point();
   * point.set(100, 200);
   *
   * // Set both x and y to same value
   * point.set(50); // x=50, y=50
   *
   * // Chain with other operations
   * point
   *     .set(10, 20)
   *     .copyTo(otherPoint);
   * ```
   * @param x - Position on the x axis
   * @param y - Position on the y axis, defaults to x
   * @returns The point instance itself
   * @see {@link Point.copyFrom} For copying from another point
   * @see {@link Point.equals} For comparing positions
   */
  set(x = 0, y = x) {
    this.x = x;
    this.y = y;
    return this;
  }
  toString() {
    return `[pixi.js/math:Point x=${this.x} y=${this.y}]`;
  }
  /**
   * A static Point object with `x` and `y` values of `0`.
   *
   * This shared instance is reset to zero values when accessed.
   *
   * > [!IMPORTANT] This point is shared and temporary. Do not store references to it.
   * @example
   * ```ts
   * // Use for temporary calculations
   * const tempPoint = Point.shared;
   * tempPoint.set(100, 200);
   * matrix.apply(tempPoint);
   *
   * // Will be reset to (0,0) on next access
   * const fresh = Point.shared; // x=0, y=0
   * ```
   * @readonly
   * @returns A fresh zeroed point for temporary use
   * @see {@link Point.constructor} For creating new points
   * @see {@link PointData} For basic point interface
   */
  static get shared() {
    tempPoint.x = 0;
    tempPoint.y = 0;
    return tempPoint;
  }
};
var tempPoint = new Point();

// node_modules/pixi.js/lib/maths/matrix/Matrix.mjs
var Matrix = class _Matrix {
  /**
   * @param a - x scale
   * @param b - y skew
   * @param c - x skew
   * @param d - y scale
   * @param tx - x translation
   * @param ty - y translation
   */
  constructor(a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0) {
    this.array = null;
    this.a = a;
    this.b = b;
    this.c = c;
    this.d = d;
    this.tx = tx;
    this.ty = ty;
  }
  /**
   * Creates a Matrix object based on the given array.
   * Populates matrix components from a flat array in column-major order.
   *
   * > [!NOTE] Array mapping order:
   * > ```
   * > array[0] = a  (x scale)
   * > array[1] = b  (y skew)
   * > array[2] = tx (x translation)
   * > array[3] = c  (x skew)
   * > array[4] = d  (y scale)
   * > array[5] = ty (y translation)
   * > ```
   * @example
   * ```ts
   * // Create matrix from array
   * const matrix = new Matrix();
   * matrix.fromArray([
   *     2, 0,  100,  // a, b, tx
   *     0, 2,  100   // c, d, ty
   * ]);
   *
   * // Create matrix from typed array
   * const float32Array = new Float32Array([
   *     1, 0, 0,     // Scale x1, no skew
   *     0, 1, 0      // No skew, scale x1
   * ]);
   * matrix.fromArray(float32Array);
   * ```
   * @param array - The array to populate the matrix from
   * @see {@link Matrix.toArray} For converting matrix to array
   * @see {@link Matrix.set} For setting values directly
   */
  fromArray(array) {
    this.a = array[0];
    this.b = array[1];
    this.c = array[3];
    this.d = array[4];
    this.tx = array[2];
    this.ty = array[5];
  }
  /**
   * Sets the matrix properties directly.
   * All matrix components can be set in one call.
   * @example
   * ```ts
   * // Set to identity matrix
   * matrix.set(1, 0, 0, 1, 0, 0);
   *
   * // Set to scale matrix
   * matrix.set(2, 0, 0, 2, 0, 0); // Scale 2x
   *
   * // Set to translation matrix
   * matrix.set(1, 0, 0, 1, 100, 50); // Move 100,50
   * ```
   * @param a - Scale on x axis
   * @param b - Shear on y axis
   * @param c - Shear on x axis
   * @param d - Scale on y axis
   * @param tx - Translation on x axis
   * @param ty - Translation on y axis
   * @returns This matrix. Good for chaining method calls.
   * @see {@link Matrix.identity} For resetting to identity
   * @see {@link Matrix.fromArray} For setting from array
   */
  set(a, b, c, d, tx, ty) {
    this.a = a;
    this.b = b;
    this.c = c;
    this.d = d;
    this.tx = tx;
    this.ty = ty;
    return this;
  }
  /**
   * Creates an array from the current Matrix object.
   *
   * > [!NOTE] The array format is:
   * > ```
   * > Non-transposed:
   * > [a, c, tx,
   * > b, d, ty,
   * > 0, 0, 1]
   * >
   * > Transposed:
   * > [a, b, 0,
   * > c, d, 0,
   * > tx,ty,1]
   * > ```
   * @example
   * ```ts
   * // Basic array conversion
   * const matrix = new Matrix(2, 0, 0, 2, 100, 100);
   * const array = matrix.toArray();
   *
   * // Using existing array
   * const float32Array = new Float32Array(9);
   * matrix.toArray(false, float32Array);
   *
   * // Get transposed array
   * const transposed = matrix.toArray(true);
   * ```
   * @param transpose - Whether to transpose the matrix
   * @param out - Optional Float32Array to store the result
   * @returns The array containing the matrix values
   * @see {@link Matrix.fromArray} For creating matrix from array
   * @see {@link Matrix.array} For cached array storage
   */
  toArray(transpose, out) {
    if (!this.array) {
      this.array = new Float32Array(9);
    }
    const array = out || this.array;
    if (transpose) {
      array[0] = this.a;
      array[1] = this.b;
      array[2] = 0;
      array[3] = this.c;
      array[4] = this.d;
      array[5] = 0;
      array[6] = this.tx;
      array[7] = this.ty;
      array[8] = 1;
    } else {
      array[0] = this.a;
      array[1] = this.c;
      array[2] = this.tx;
      array[3] = this.b;
      array[4] = this.d;
      array[5] = this.ty;
      array[6] = 0;
      array[7] = 0;
      array[8] = 1;
    }
    return array;
  }
  /**
   * Get a new position with the current transformation applied.
   *
   * Can be used to go from a child's coordinate space to the world coordinate space. (e.g. rendering)
   * @example
   * ```ts
   * // Basic point transformation
   * const matrix = new Matrix().translate(100, 50).rotate(Math.PI / 4);
   * const point = new Point(10, 20);
   * const transformed = matrix.apply(point);
   *
   * // Reuse existing point
   * const output = new Point();
   * matrix.apply(point, output);
   * ```
   * @param pos - The origin point to transform
   * @param newPos - Optional point to store the result
   * @returns The transformed point
   * @see {@link Matrix.applyInverse} For inverse transformation
   * @see {@link Point} For point operations
   */
  apply(pos, newPos) {
    newPos = newPos || new Point();
    const x = pos.x;
    const y = pos.y;
    newPos.x = this.a * x + this.c * y + this.tx;
    newPos.y = this.b * x + this.d * y + this.ty;
    return newPos;
  }
  /**
   * Get a new position with the inverse of the current transformation applied.
   *
   * Can be used to go from the world coordinate space to a child's coordinate space. (e.g. input)
   * @example
   * ```ts
   * // Basic inverse transformation
   * const matrix = new Matrix().translate(100, 50).rotate(Math.PI / 4);
   * const worldPoint = new Point(150, 100);
   * const localPoint = matrix.applyInverse(worldPoint);
   *
   * // Reuse existing point
   * const output = new Point();
   * matrix.applyInverse(worldPoint, output);
   *
   * // Convert mouse position to local space
   * const mousePoint = new Point(mouseX, mouseY);
   * const localMouse = matrix.applyInverse(mousePoint);
   * ```
   * @param pos - The origin point to inverse-transform
   * @param newPos - Optional point to store the result
   * @returns The inverse-transformed point
   * @see {@link Matrix.apply} For forward transformation
   * @see {@link Matrix.invert} For getting inverse matrix
   */
  applyInverse(pos, newPos) {
    newPos = newPos || new Point();
    const a = this.a;
    const b = this.b;
    const c = this.c;
    const d = this.d;
    const tx = this.tx;
    const ty = this.ty;
    const id = 1 / (a * d + c * -b);
    const x = pos.x;
    const y = pos.y;
    newPos.x = d * id * x + -c * id * y + (ty * c - tx * d) * id;
    newPos.y = a * id * y + -b * id * x + (-ty * a + tx * b) * id;
    return newPos;
  }
  /**
   * Translates the matrix on the x and y axes.
   * Adds to the position values while preserving scale, rotation and skew.
   * @example
   * ```ts
   * // Basic translation
   * const matrix = new Matrix();
   * matrix.translate(100, 50); // Move right 100, down 50
   *
   * // Chain with other transformations
   * matrix
   *     .scale(2, 2)
   *     .translate(100, 0)
   *     .rotate(Math.PI / 4);
   * ```
   * @param x - How much to translate on the x axis
   * @param y - How much to translate on the y axis
   * @returns This matrix. Good for chaining method calls.
   * @see {@link Matrix.set} For setting position directly
   * @see {@link Matrix.setTransform} For complete transform setup
   */
  translate(x, y) {
    this.tx += x;
    this.ty += y;
    return this;
  }
  /**
   * Applies a scale transformation to the matrix.
   * Multiplies the scale values with existing matrix components.
   * @example
   * ```ts
   * // Basic scaling
   * const matrix = new Matrix();
   * matrix.scale(2, 3); // Scale 2x horizontally, 3x vertically
   *
   * // Chain with other transformations
   * matrix
   *     .translate(100, 100)
   *     .scale(2, 2)     // Scales after translation
   *     .rotate(Math.PI / 4);
   * ```
   * @param x - The amount to scale horizontally
   * @param y - The amount to scale vertically
   * @returns This matrix. Good for chaining method calls.
   * @see {@link Matrix.setTransform} For setting scale directly
   * @see {@link Matrix.append} For combining transformations
   */
  scale(x, y) {
    this.a *= x;
    this.d *= y;
    this.c *= x;
    this.b *= y;
    this.tx *= x;
    this.ty *= y;
    return this;
  }
  /**
   * Applies a rotation transformation to the matrix.
   *
   * Rotates around the origin (0,0) by the given angle in radians.
   * @example
   * ```ts
   * // Basic rotation
   * const matrix = new Matrix();
   * matrix.rotate(Math.PI / 4); // Rotate 45 degrees
   *
   * // Chain with other transformations
   * matrix
   *     .translate(100, 100) // Move to rotation center
   *     .rotate(Math.PI)     // Rotate 180 degrees
   *     .scale(2, 2);        // Scale after rotation
   *
   * // Common angles
   * matrix.rotate(Math.PI / 2);  // 90 degrees
   * matrix.rotate(Math.PI);      // 180 degrees
   * matrix.rotate(Math.PI * 2);  // 360 degrees
   * ```
   * @remarks
   * - Rotates around origin point (0,0)
   * - Affects position if translation was set
   * - Uses counter-clockwise rotation
   * - Order of operations matters when chaining
   * @param angle - The angle in radians
   * @returns This matrix. Good for chaining method calls.
   * @see {@link Matrix.setTransform} For setting rotation directly
   * @see {@link Matrix.append} For combining transformations
   */
  rotate(angle) {
    const cos = Math.cos(angle);
    const sin = Math.sin(angle);
    const a1 = this.a;
    const c1 = this.c;
    const tx1 = this.tx;
    this.a = a1 * cos - this.b * sin;
    this.b = a1 * sin + this.b * cos;
    this.c = c1 * cos - this.d * sin;
    this.d = c1 * sin + this.d * cos;
    this.tx = tx1 * cos - this.ty * sin;
    this.ty = tx1 * sin + this.ty * cos;
    return this;
  }
  /**
   * Appends the given Matrix to this Matrix.
   * Combines two matrices by multiplying them together: this = this * matrix
   * @example
   * ```ts
   * // Basic matrix combination
   * const matrix = new Matrix();
   * const other = new Matrix().translate(100, 0).rotate(Math.PI / 4);
   * matrix.append(other);
   * ```
   * @remarks
   * - Order matters: A.append(B) !== B.append(A)
   * - Modifies current matrix
   * - Preserves transformation order
   * - Commonly used for combining transforms
   * @param matrix - The matrix to append
   * @returns This matrix. Good for chaining method calls.
   * @see {@link Matrix.prepend} For prepending transformations
   * @see {@link Matrix.appendFrom} For appending two external matrices
   */
  append(matrix) {
    const a1 = this.a;
    const b1 = this.b;
    const c1 = this.c;
    const d1 = this.d;
    this.a = matrix.a * a1 + matrix.b * c1;
    this.b = matrix.a * b1 + matrix.b * d1;
    this.c = matrix.c * a1 + matrix.d * c1;
    this.d = matrix.c * b1 + matrix.d * d1;
    this.tx = matrix.tx * a1 + matrix.ty * c1 + this.tx;
    this.ty = matrix.tx * b1 + matrix.ty * d1 + this.ty;
    return this;
  }
  /**
   * Appends two matrices and sets the result to this matrix.
   * Performs matrix multiplication: this = A * B
   * @example
   * ```ts
   * // Basic matrix multiplication
   * const result = new Matrix();
   * const matrixA = new Matrix().scale(2, 2);
   * const matrixB = new Matrix().rotate(Math.PI / 4);
   * result.appendFrom(matrixA, matrixB);
   * ```
   * @remarks
   * - Order matters: A * B !== B * A
   * - Creates a new transformation from two others
   * - More efficient than append() for multiple operations
   * - Does not modify input matrices
   * @param a - The first matrix to multiply
   * @param b - The second matrix to multiply
   * @returns This matrix. Good for chaining method calls.
   * @see {@link Matrix.append} For single matrix combination
   * @see {@link Matrix.prepend} For reverse order multiplication
   */
  appendFrom(a, b) {
    const a1 = a.a;
    const b1 = a.b;
    const c1 = a.c;
    const d1 = a.d;
    const tx = a.tx;
    const ty = a.ty;
    const a2 = b.a;
    const b2 = b.b;
    const c2 = b.c;
    const d2 = b.d;
    this.a = a1 * a2 + b1 * c2;
    this.b = a1 * b2 + b1 * d2;
    this.c = c1 * a2 + d1 * c2;
    this.d = c1 * b2 + d1 * d2;
    this.tx = tx * a2 + ty * c2 + b.tx;
    this.ty = tx * b2 + ty * d2 + b.ty;
    return this;
  }
  /**
   * Sets the matrix based on all the available properties.
   * Combines position, scale, rotation, skew and pivot in a single operation.
   * @example
   * ```ts
   * // Basic transform setup
   * const matrix = new Matrix();
   * matrix.setTransform(
   *     100, 100,    // position
   *     0, 0,        // pivot
   *     2, 2,        // scale
   *     Math.PI / 4, // rotation (45 degrees)
   *     0, 0         // skew
   * );
   * ```
   * @remarks
   * - Updates all matrix components at once
   * - More efficient than separate transform calls
   * - Uses radians for rotation and skew
   * - Pivot affects rotation center
   * @param x - Position on the x axis
   * @param y - Position on the y axis
   * @param pivotX - Pivot on the x axis
   * @param pivotY - Pivot on the y axis
   * @param scaleX - Scale on the x axis
   * @param scaleY - Scale on the y axis
   * @param rotation - Rotation in radians
   * @param skewX - Skew on the x axis
   * @param skewY - Skew on the y axis
   * @returns This matrix. Good for chaining method calls.
   * @see {@link Matrix.decompose} For extracting transform properties
   * @see {@link TransformableObject} For transform data structure
   */
  setTransform(x, y, pivotX, pivotY, scaleX, scaleY, rotation, skewX, skewY) {
    this.a = Math.cos(rotation + skewY) * scaleX;
    this.b = Math.sin(rotation + skewY) * scaleX;
    this.c = -Math.sin(rotation - skewX) * scaleY;
    this.d = Math.cos(rotation - skewX) * scaleY;
    this.tx = x - (pivotX * this.a + pivotY * this.c);
    this.ty = y - (pivotX * this.b + pivotY * this.d);
    return this;
  }
  /**
   * Prepends the given Matrix to this Matrix.
   * Combines two matrices by multiplying them together: this = matrix * this
   * @example
   * ```ts
   * // Basic matrix prepend
   * const matrix = new Matrix().scale(2, 2);
   * const other = new Matrix().translate(100, 0);
   * matrix.prepend(other); // Translation happens before scaling
   * ```
   * @remarks
   * - Order matters: A.prepend(B) !== B.prepend(A)
   * - Modifies current matrix
   * - Reverses transformation order compared to append()
   * @param matrix - The matrix to prepend
   * @returns This matrix. Good for chaining method calls.
   * @see {@link Matrix.append} For appending transformations
   * @see {@link Matrix.appendFrom} For combining external matrices
   */
  prepend(matrix) {
    const tx1 = this.tx;
    if (matrix.a !== 1 || matrix.b !== 0 || matrix.c !== 0 || matrix.d !== 1) {
      const a1 = this.a;
      const c1 = this.c;
      this.a = a1 * matrix.a + this.b * matrix.c;
      this.b = a1 * matrix.b + this.b * matrix.d;
      this.c = c1 * matrix.a + this.d * matrix.c;
      this.d = c1 * matrix.b + this.d * matrix.d;
    }
    this.tx = tx1 * matrix.a + this.ty * matrix.c + matrix.tx;
    this.ty = tx1 * matrix.b + this.ty * matrix.d + matrix.ty;
    return this;
  }
  /**
   * Decomposes the matrix into its individual transform components.
   * Extracts position, scale, rotation and skew values from the matrix.
   * @example
   * ```ts
   * // Basic decomposition
   * const matrix = new Matrix()
   *     .translate(100, 100)
   *     .rotate(Math.PI / 4)
   *     .scale(2, 2);
   *
   * const transform = {
   *     position: new Point(),
   *     scale: new Point(),
   *     pivot: new Point(),
   *     skew: new Point(),
   *     rotation: 0
   * };
   *
   * matrix.decompose(transform);
   * console.log(transform.position); // Point(100, 100)
   * console.log(transform.rotation); // ~0.785 (PI/4)
   * console.log(transform.scale); // Point(2, 2)
   * ```
   * @remarks
   * - Handles combined transformations
   * - Accounts for pivot points
   * - Chooses between rotation/skew based on transform type
   * - Uses radians for rotation and skew
   * @param transform - The transform object to store the decomposed values
   * @returns The transform with the newly applied properties
   * @see {@link Matrix.setTransform} For composing from components
   * @see {@link TransformableObject} For transform structure
   */
  decompose(transform) {
    const a = this.a;
    const b = this.b;
    const c = this.c;
    const d = this.d;
    const pivot = transform.pivot;
    const skewX = -Math.atan2(-c, d);
    const skewY = Math.atan2(b, a);
    const delta = Math.abs(skewX + skewY);
    if (delta < 1e-5 || Math.abs(PI_2 - delta) < 1e-5) {
      transform.rotation = skewY;
      transform.skew.x = transform.skew.y = 0;
    } else {
      transform.rotation = 0;
      transform.skew.x = skewX;
      transform.skew.y = skewY;
    }
    transform.scale.x = Math.sqrt(a * a + b * b);
    transform.scale.y = Math.sqrt(c * c + d * d);
    transform.position.x = this.tx + (pivot.x * a + pivot.y * c);
    transform.position.y = this.ty + (pivot.x * b + pivot.y * d);
    return transform;
  }
  /**
   * Inverts this matrix.
   * Creates the matrix that when multiplied with this matrix results in an identity matrix.
   * @example
   * ```ts
   * // Basic matrix inversion
   * const matrix = new Matrix()
   *     .translate(100, 50)
   *     .scale(2, 2);
   *
   * matrix.invert(); // Now transforms in opposite direction
   *
   * // Verify inversion
   * const point = new Point(50, 50);
   * const transformed = matrix.apply(point);
   * const original = matrix.invert().apply(transformed);
   * // original ≈ point
   * ```
   * @remarks
   * - Modifies the current matrix
   * - Useful for reversing transformations
   * - Cannot invert matrices with zero determinant
   * @returns This matrix. Good for chaining method calls.
   * @see {@link Matrix.identity} For resetting to identity
   * @see {@link Matrix.applyInverse} For inverse transformations
   */
  invert() {
    const a1 = this.a;
    const b1 = this.b;
    const c1 = this.c;
    const d1 = this.d;
    const tx1 = this.tx;
    const n = a1 * d1 - b1 * c1;
    this.a = d1 / n;
    this.b = -b1 / n;
    this.c = -c1 / n;
    this.d = a1 / n;
    this.tx = (c1 * this.ty - d1 * tx1) / n;
    this.ty = -(a1 * this.ty - b1 * tx1) / n;
    return this;
  }
  /**
   * Checks if this matrix is an identity matrix.
   *
   * An identity matrix has no transformations applied (default state).
   * @example
   * ```ts
   * // Check if matrix is identity
   * const matrix = new Matrix();
   * console.log(matrix.isIdentity()); // true
   *
   * // Check after transformations
   * matrix.translate(100, 0);
   * console.log(matrix.isIdentity()); // false
   *
   * // Reset and verify
   * matrix.identity();
   * console.log(matrix.isIdentity()); // true
   * ```
   * @remarks
   * - Verifies a = 1, d = 1 (no scale)
   * - Verifies b = 0, c = 0 (no skew)
   * - Verifies tx = 0, ty = 0 (no translation)
   * @returns True if matrix has no transformations
   * @see {@link Matrix.identity} For resetting to identity
   * @see {@link Matrix.IDENTITY} For constant identity matrix
   */
  isIdentity() {
    return this.a === 1 && this.b === 0 && this.c === 0 && this.d === 1 && this.tx === 0 && this.ty === 0;
  }
  /**
   * Resets this Matrix to an identity (default) matrix.
   * Sets all components to their default values: scale=1, no skew, no translation.
   * @example
   * ```ts
   * // Reset transformed matrix
   * const matrix = new Matrix()
   *     .scale(2, 2)
   *     .rotate(Math.PI / 4);
   * matrix.identity(); // Back to default state
   *
   * // Chain after reset
   * matrix
   *     .identity()
   *     .translate(100, 100)
   *     .scale(2, 2);
   *
   * // Compare with identity constant
   * const isDefault = matrix.equals(Matrix.IDENTITY);
   * ```
   * @remarks
   * - Sets a=1, d=1 (default scale)
   * - Sets b=0, c=0 (no skew)
   * - Sets tx=0, ty=0 (no translation)
   * @returns This matrix. Good for chaining method calls.
   * @see {@link Matrix.IDENTITY} For constant identity matrix
   * @see {@link Matrix.isIdentity} For checking identity state
   */
  identity() {
    this.a = 1;
    this.b = 0;
    this.c = 0;
    this.d = 1;
    this.tx = 0;
    this.ty = 0;
    return this;
  }
  /**
   * Creates a new Matrix object with the same values as this one.
   * @returns A copy of this matrix. Good for chaining method calls.
   */
  clone() {
    const matrix = new _Matrix();
    matrix.a = this.a;
    matrix.b = this.b;
    matrix.c = this.c;
    matrix.d = this.d;
    matrix.tx = this.tx;
    matrix.ty = this.ty;
    return matrix;
  }
  /**
   * Creates a new Matrix object with the same values as this one.
   * @param matrix
   * @example
   * ```ts
   * // Basic matrix cloning
   * const matrix = new Matrix()
   *     .translate(100, 100)
   *     .rotate(Math.PI / 4);
   * const copy = matrix.clone();
   *
   * // Clone and modify
   * const modified = matrix.clone()
   *     .scale(2, 2);
   *
   * // Compare matrices
   * console.log(matrix.equals(copy));     // true
   * console.log(matrix.equals(modified)); // false
   * ```
   * @returns A copy of this matrix. Good for chaining method calls.
   * @see {@link Matrix.copyTo} For copying to existing matrix
   * @see {@link Matrix.copyFrom} For copying from another matrix
   */
  copyTo(matrix) {
    matrix.a = this.a;
    matrix.b = this.b;
    matrix.c = this.c;
    matrix.d = this.d;
    matrix.tx = this.tx;
    matrix.ty = this.ty;
    return matrix;
  }
  /**
   * Changes the values of the matrix to be the same as the ones in given matrix.
   * @example
   * ```ts
   * // Basic matrix copying
   * const source = new Matrix()
   *     .translate(100, 100)
   *     .rotate(Math.PI / 4);
   * const target = new Matrix();
   * target.copyFrom(source);
   * ```
   * @param matrix - The matrix to copy from
   * @returns This matrix. Good for chaining method calls.
   * @see {@link Matrix.clone} For creating new matrix copy
   * @see {@link Matrix.copyTo} For copying to another matrix
   */
  copyFrom(matrix) {
    this.a = matrix.a;
    this.b = matrix.b;
    this.c = matrix.c;
    this.d = matrix.d;
    this.tx = matrix.tx;
    this.ty = matrix.ty;
    return this;
  }
  /**
   * Checks if this matrix equals another matrix.
   * Compares all components for exact equality.
   * @example
   * ```ts
   * // Basic equality check
   * const m1 = new Matrix();
   * const m2 = new Matrix();
   * console.log(m1.equals(m2)); // true
   *
   * // Compare transformed matrices
   * const transform = new Matrix()
   *     .translate(100, 100)
   * const clone = new Matrix()
   *     .scale(2, 2);
   * console.log(transform.equals(clone)); // false
   * ```
   * @param matrix - The matrix to compare to
   * @returns True if matrices are identical
   * @see {@link Matrix.copyFrom} For copying matrix values
   * @see {@link Matrix.isIdentity} For identity comparison
   */
  equals(matrix) {
    return matrix.a === this.a && matrix.b === this.b && matrix.c === this.c && matrix.d === this.d && matrix.tx === this.tx && matrix.ty === this.ty;
  }
  toString() {
    return `[pixi.js:Matrix a=${this.a} b=${this.b} c=${this.c} d=${this.d} tx=${this.tx} ty=${this.ty}]`;
  }
  /**
   * A default (identity) matrix with no transformations applied.
   *
   * > [!IMPORTANT] This is a shared read-only object. Create a new Matrix if you need to modify it.
   * @example
   * ```ts
   * // Get identity matrix reference
   * const identity = Matrix.IDENTITY;
   * console.log(identity.isIdentity()); // true
   *
   * // Compare with identity
   * const matrix = new Matrix();
   * console.log(matrix.equals(Matrix.IDENTITY)); // true
   *
   * // Create new matrix instead of modifying IDENTITY
   * const transform = new Matrix()
   *     .copyFrom(Matrix.IDENTITY)
   *     .translate(100, 100);
   * ```
   * @readonly
   * @returns A read-only identity matrix
   * @see {@link Matrix.shared} For temporary calculations
   * @see {@link Matrix.identity} For resetting matrices
   */
  static get IDENTITY() {
    return identityMatrix.identity();
  }
  /**
   * A static Matrix that can be used to avoid creating new objects.
   * Will always ensure the matrix is reset to identity when requested.
   *
   * > [!IMPORTANT] This matrix is shared and temporary. Do not store references to it.
   * @example
   * ```ts
   * // Use for temporary calculations
   * const tempMatrix = Matrix.shared;
   * tempMatrix.translate(100, 100).rotate(Math.PI / 4);
   * const point = tempMatrix.apply({ x: 10, y: 20 });
   *
   * // Will be reset to identity on next access
   * const fresh = Matrix.shared; // Back to identity
   * ```
   * @remarks
   * - Always returns identity matrix
   * - Safe to modify temporarily
   * - Not safe to store references
   * - Useful for one-off calculations
   * @readonly
   * @returns A fresh identity matrix for temporary use
   * @see {@link Matrix.IDENTITY} For immutable identity matrix
   * @see {@link Matrix.identity} For resetting matrices
   */
  static get shared() {
    return tempMatrix.identity();
  }
};
var tempMatrix = new Matrix();
var identityMatrix = new Matrix();

// node_modules/pixi.js/lib/maths/matrix/groupD8.mjs
var ux = [1, 1, 0, -1, -1, -1, 0, 1, 1, 1, 0, -1, -1, -1, 0, 1];
var uy = [0, 1, 1, 1, 0, -1, -1, -1, 0, 1, 1, 1, 0, -1, -1, -1];
var vx = [0, -1, -1, -1, 0, 1, 1, 1, 0, 1, 1, 1, 0, -1, -1, -1];
var vy = [1, 1, 0, -1, -1, -1, 0, 1, -1, -1, 0, 1, 1, 1, 0, -1];
var rotationCayley = [];
var rotationMatrices = [];
var signum = Math.sign;
function init() {
  for (let i = 0; i < 16; i++) {
    const row = [];
    rotationCayley.push(row);
    for (let j = 0; j < 16; j++) {
      const _ux = signum(ux[i] * ux[j] + vx[i] * uy[j]);
      const _uy = signum(uy[i] * ux[j] + vy[i] * uy[j]);
      const _vx = signum(ux[i] * vx[j] + vx[i] * vy[j]);
      const _vy = signum(uy[i] * vx[j] + vy[i] * vy[j]);
      for (let k = 0; k < 16; k++) {
        if (ux[k] === _ux && uy[k] === _uy && vx[k] === _vx && vy[k] === _vy) {
          row.push(k);
          break;
        }
      }
    }
  }
  for (let i = 0; i < 16; i++) {
    const mat = new Matrix();
    mat.set(ux[i], uy[i], vx[i], vy[i], 0, 0);
    rotationMatrices.push(mat);
  }
}
init();
var groupD8 = {
  /**
   * | Rotation | Direction |
   * |----------|-----------|
   * | 0°       | East      |
   * @group groupD8
   * @type {GD8Symmetry}
   */
  E: 0,
  /**
   * | Rotation | Direction |
   * |----------|-----------|
   * | 45°↻     | Southeast |
   * @group groupD8
   * @type {GD8Symmetry}
   */
  SE: 1,
  /**
   * | Rotation | Direction |
   * |----------|-----------|
   * | 90°↻     | South     |
   * @group groupD8
   * @type {GD8Symmetry}
   */
  S: 2,
  /**
   * | Rotation | Direction |
   * |----------|-----------|
   * | 135°↻    | Southwest |
   * @group groupD8
   * @type {GD8Symmetry}
   */
  SW: 3,
  /**
   * | Rotation | Direction |
   * |----------|-----------|
   * | 180°     | West      |
   * @group groupD8
   * @type {GD8Symmetry}
   */
  W: 4,
  /**
   * | Rotation    | Direction    |
   * |-------------|--------------|
   * | -135°/225°↻ | Northwest    |
   * @group groupD8
   * @type {GD8Symmetry}
   */
  NW: 5,
  /**
   * | Rotation    | Direction    |
   * |-------------|--------------|
   * | -90°/270°↻  | North        |
   * @group groupD8
   * @type {GD8Symmetry}
   */
  N: 6,
  /**
   * | Rotation    | Direction    |
   * |-------------|--------------|
   * | -45°/315°↻  | Northeast    |
   * @group groupD8
   * @type {GD8Symmetry}
   */
  NE: 7,
  /**
   * Reflection about Y-axis.
   * @group groupD8
   * @type {GD8Symmetry}
   */
  MIRROR_VERTICAL: 8,
  /**
   * Reflection about the main diagonal.
   * @group groupD8
   * @type {GD8Symmetry}
   */
  MAIN_DIAGONAL: 10,
  /**
   * Reflection about X-axis.
   * @group groupD8
   * @type {GD8Symmetry}
   */
  MIRROR_HORIZONTAL: 12,
  /**
   * Reflection about reverse diagonal.
   * @group groupD8
   * @type {GD8Symmetry}
   */
  REVERSE_DIAGONAL: 14,
  /**
   * @group groupD8
   * @param {GD8Symmetry} ind - sprite rotation angle.
   * @returns {GD8Symmetry} The X-component of the U-axis
   *    after rotating the axes.
   */
  uX: (ind) => ux[ind],
  /**
   * @group groupD8
   * @param {GD8Symmetry} ind - sprite rotation angle.
   * @returns {GD8Symmetry} The Y-component of the U-axis
   *    after rotating the axes.
   */
  uY: (ind) => uy[ind],
  /**
   * @group groupD8
   * @param {GD8Symmetry} ind - sprite rotation angle.
   * @returns {GD8Symmetry} The X-component of the V-axis
   *    after rotating the axes.
   */
  vX: (ind) => vx[ind],
  /**
   * @group groupD8
   * @param {GD8Symmetry} ind - sprite rotation angle.
   * @returns {GD8Symmetry} The Y-component of the V-axis
   *    after rotating the axes.
   */
  vY: (ind) => vy[ind],
  /**
   * @group groupD8
   * @param {GD8Symmetry} rotation - symmetry whose opposite
   *   is needed. Only rotations have opposite symmetries while
   *   reflections don't.
   * @returns {GD8Symmetry} The opposite symmetry of `rotation`
   */
  inv: (rotation) => {
    if (rotation & 8) {
      return rotation & 15;
    }
    return -rotation & 7;
  },
  /**
   * Composes the two D8 operations.
   *
   * Taking `^` as reflection:
   *
   * |       | E=0 | S=2 | W=4 | N=6 | E^=8 | S^=10 | W^=12 | N^=14 |
   * |-------|-----|-----|-----|-----|------|-------|-------|-------|
   * | E=0   | E   | S   | W   | N   | E^   | S^    | W^    | N^    |
   * | S=2   | S   | W   | N   | E   | S^   | W^    | N^    | E^    |
   * | W=4   | W   | N   | E   | S   | W^   | N^    | E^    | S^    |
   * | N=6   | N   | E   | S   | W   | N^   | E^    | S^    | W^    |
   * | E^=8  | E^  | N^  | W^  | S^  | E    | N     | W     | S     |
   * | S^=10 | S^  | E^  | N^  | W^  | S    | E     | N     | W     |
   * | W^=12 | W^  | S^  | E^  | N^  | W    | S     | E     | N     |
   * | N^=14 | N^  | W^  | S^  | E^  | N    | W     | S     | E     |
   *
   * [This is a Cayley table]{@link https://en.wikipedia.org/wiki/Cayley_table}
   * @group groupD8
   * @param {GD8Symmetry} rotationSecond - Second operation, which
   *   is the row in the above cayley table.
   * @param {GD8Symmetry} rotationFirst - First operation, which
   *   is the column in the above cayley table.
   * @returns {GD8Symmetry} Composed operation
   */
  add: (rotationSecond, rotationFirst) => rotationCayley[rotationSecond][rotationFirst],
  /**
   * Reverse of `add`.
   * @group groupD8
   * @param {GD8Symmetry} rotationSecond - Second operation
   * @param {GD8Symmetry} rotationFirst - First operation
   * @returns {GD8Symmetry} Result
   */
  sub: (rotationSecond, rotationFirst) => rotationCayley[rotationSecond][groupD8.inv(rotationFirst)],
  /**
   * Adds 180 degrees to rotation, which is a commutative
   * operation.
   * @group groupD8
   * @param {number} rotation - The number to rotate.
   * @returns {number} Rotated number
   */
  rotate180: (rotation) => rotation ^ 4,
  /**
   * Checks if the rotation angle is vertical, i.e. south
   * or north. It doesn't work for reflections.
   * @group groupD8
   * @param {GD8Symmetry} rotation - The number to check.
   * @returns {boolean} Whether or not the direction is vertical
   */
  isVertical: (rotation) => (rotation & 3) === 2,
  // rotation % 4 === 2
  /**
   * Approximates the vector `V(dx,dy)` into one of the
   * eight directions provided by `groupD8`.
   * @group groupD8
   * @param {number} dx - X-component of the vector
   * @param {number} dy - Y-component of the vector
   * @returns {GD8Symmetry} Approximation of the vector into
   *  one of the eight symmetries.
   */
  byDirection: (dx, dy) => {
    if (Math.abs(dx) * 2 <= Math.abs(dy)) {
      if (dy >= 0) {
        return groupD8.S;
      }
      return groupD8.N;
    } else if (Math.abs(dy) * 2 <= Math.abs(dx)) {
      if (dx > 0) {
        return groupD8.E;
      }
      return groupD8.W;
    } else if (dy > 0) {
      if (dx > 0) {
        return groupD8.SE;
      }
      return groupD8.SW;
    } else if (dx > 0) {
      return groupD8.NE;
    }
    return groupD8.NW;
  },
  /**
   * Helps sprite to compensate texture packer rotation.
   * @group groupD8
   * @param {Matrix} matrix - sprite world matrix
   * @param {GD8Symmetry} rotation - The rotation factor to use.
   * @param {number} tx - sprite anchoring
   * @param {number} ty - sprite anchoring
   * @param {number} dw - sprite width
   * @param {number} dh - sprite height
   */
  matrixAppendRotationInv: (matrix, rotation, tx = 0, ty = 0, dw = 0, dh = 0) => {
    const mat = rotationMatrices[groupD8.inv(rotation)];
    const a = mat.a;
    const b = mat.b;
    const c = mat.c;
    const d = mat.d;
    const finalTx = tx - Math.min(0, a * dw, c * dh, a * dw + c * dh);
    const finalTy = ty - Math.min(0, b * dw, d * dh, b * dw + d * dh);
    const a1 = matrix.a;
    const b1 = matrix.b;
    const c1 = matrix.c;
    const d1 = matrix.d;
    matrix.a = a * a1 + b * c1;
    matrix.b = a * b1 + b * d1;
    matrix.c = c * a1 + d * c1;
    matrix.d = c * b1 + d * d1;
    matrix.tx = finalTx * a1 + finalTy * c1 + matrix.tx;
    matrix.ty = finalTx * b1 + finalTy * d1 + matrix.ty;
  },
  /**
   * Transforms rectangle coordinates based on texture packer rotation.
   * Used when texture atlas pages are rotated and coordinates need to be adjusted.
   * @group groupD8
   * @param {RectangleLike} rect - Rectangle with original coordinates to transform
   * @param {RectangleLike} sourceFrame - Source texture frame (includes offset and dimensions)
   * @param {GD8Symmetry} rotation - The groupD8 rotation value
   * @param {Rectangle} out - Rectangle to store the result
   * @returns {Rectangle} Transformed coordinates (includes source frame offset)
   */
  transformRectCoords: (rect, sourceFrame, rotation, out) => {
    const { x, y, width, height } = rect;
    const { x: frameX, y: frameY, width: frameWidth, height: frameHeight } = sourceFrame;
    if (rotation === groupD8.E) {
      out.set(x + frameX, y + frameY, width, height);
      return out;
    } else if (rotation === groupD8.S) {
      return out.set(
        frameWidth - y - height + frameX,
        x + frameY,
        height,
        width
      );
    } else if (rotation === groupD8.W) {
      return out.set(
        frameWidth - x - width + frameX,
        frameHeight - y - height + frameY,
        width,
        height
      );
    } else if (rotation === groupD8.N) {
      return out.set(
        y + frameX,
        frameHeight - x - width + frameY,
        height,
        width
      );
    }
    return out.set(x + frameX, y + frameY, width, height);
  }
};

// node_modules/pixi.js/lib/maths/shapes/Rectangle.mjs
var tempPoints = [new Point(), new Point(), new Point(), new Point()];
var Rectangle = class _Rectangle {
  /**
   * @param x - The X coordinate of the upper-left corner of the rectangle
   * @param y - The Y coordinate of the upper-left corner of the rectangle
   * @param width - The overall width of the rectangle
   * @param height - The overall height of the rectangle
   */
  constructor(x = 0, y = 0, width = 0, height = 0) {
    this.type = "rectangle";
    this.x = Number(x);
    this.y = Number(y);
    this.width = Number(width);
    this.height = Number(height);
  }
  /**
   * Returns the left edge (x-coordinate) of the rectangle.
   * @example
   * ```ts
   * // Get left edge position
   * const rect = new Rectangle(100, 100, 200, 150);
   * console.log(rect.left); // 100
   *
   * // Use in alignment calculations
   * sprite.x = rect.left + padding;
   *
   * // Compare positions
   * if (point.x > rect.left) {
   *     console.log('Point is right of rectangle');
   * }
   * ```
   * @readonly
   * @returns The x-coordinate of the left edge
   * @see {@link Rectangle.right} For right edge position
   * @see {@link Rectangle.x} For direct x-coordinate access
   */
  get left() {
    return this.x;
  }
  /**
   * Returns the right edge (x + width) of the rectangle.
   * @example
   * ```ts
   * // Get right edge position
   * const rect = new Rectangle(100, 100, 200, 150);
   * console.log(rect.right); // 300
   *
   * // Align to right edge
   * sprite.x = rect.right - sprite.width;
   *
   * // Check boundaries
   * if (point.x < rect.right) {
   *     console.log('Point is inside right bound');
   * }
   * ```
   * @readonly
   * @returns The x-coordinate of the right edge
   * @see {@link Rectangle.left} For left edge position
   * @see {@link Rectangle.width} For width value
   */
  get right() {
    return this.x + this.width;
  }
  /**
   * Returns the top edge (y-coordinate) of the rectangle.
   * @example
   * ```ts
   * // Get top edge position
   * const rect = new Rectangle(100, 100, 200, 150);
   * console.log(rect.top); // 100
   *
   * // Position above rectangle
   * sprite.y = rect.top - sprite.height;
   *
   * // Check vertical position
   * if (point.y > rect.top) {
   *     console.log('Point is below top edge');
   * }
   * ```
   * @readonly
   * @returns The y-coordinate of the top edge
   * @see {@link Rectangle.bottom} For bottom edge position
   * @see {@link Rectangle.y} For direct y-coordinate access
   */
  get top() {
    return this.y;
  }
  /**
   * Returns the bottom edge (y + height) of the rectangle.
   * @example
   * ```ts
   * // Get bottom edge position
   * const rect = new Rectangle(100, 100, 200, 150);
   * console.log(rect.bottom); // 250
   *
   * // Stack below rectangle
   * sprite.y = rect.bottom + margin;
   *
   * // Check vertical bounds
   * if (point.y < rect.bottom) {
   *     console.log('Point is above bottom edge');
   * }
   * ```
   * @readonly
   * @returns The y-coordinate of the bottom edge
   * @see {@link Rectangle.top} For top edge position
   * @see {@link Rectangle.height} For height value
   */
  get bottom() {
    return this.y + this.height;
  }
  /**
   * Determines whether the Rectangle is empty (has no area).
   * @example
   * ```ts
   * // Check zero dimensions
   * const rect = new Rectangle(100, 100, 0, 50);
   * console.log(rect.isEmpty()); // true
   * ```
   * @returns True if the rectangle has no area
   * @see {@link Rectangle.width} For width value
   * @see {@link Rectangle.height} For height value
   */
  isEmpty() {
    return this.left === this.right || this.top === this.bottom;
  }
  /**
   * A constant empty rectangle. This is a new object every time the property is accessed.
   * @example
   * ```ts
   * // Get fresh empty rectangle
   * const empty = Rectangle.EMPTY;
   * console.log(empty.isEmpty()); // true
   * ```
   * @returns A new empty rectangle instance
   * @see {@link Rectangle.isEmpty} For empty state testing
   */
  static get EMPTY() {
    return new _Rectangle(0, 0, 0, 0);
  }
  /**
   * Creates a clone of this Rectangle
   * @example
   * ```ts
   * // Basic cloning
   * const original = new Rectangle(100, 100, 200, 150);
   * const copy = original.clone();
   *
   * // Clone and modify
   * const modified = original.clone();
   * modified.width *= 2;
   * modified.height += 50;
   *
   * // Verify independence
   * console.log(original.width);  // 200
   * console.log(modified.width);  // 400
   * ```
   * @returns A copy of the rectangle
   * @see {@link Rectangle.copyFrom} For copying into existing rectangle
   * @see {@link Rectangle.copyTo} For copying to another rectangle
   */
  clone() {
    return new _Rectangle(this.x, this.y, this.width, this.height);
  }
  /**
   * Converts a Bounds object to a Rectangle object.
   * @example
   * ```ts
   * // Convert bounds to rectangle
   * const bounds = container.getBounds();
   * const rect = new Rectangle().copyFromBounds(bounds);
   * ```
   * @param bounds - The bounds to copy and convert to a rectangle
   * @returns Returns itself
   * @see {@link Bounds} For bounds object structure
   * @see {@link Rectangle.getBounds} For getting rectangle bounds
   */
  copyFromBounds(bounds) {
    this.x = bounds.minX;
    this.y = bounds.minY;
    this.width = bounds.maxX - bounds.minX;
    this.height = bounds.maxY - bounds.minY;
    return this;
  }
  /**
   * Copies another rectangle to this one.
   * @example
   * ```ts
   * // Basic copying
   * const source = new Rectangle(100, 100, 200, 150);
   * const target = new Rectangle();
   * target.copyFrom(source);
   *
   * // Chain with other operations
   * const rect = new Rectangle()
   *     .copyFrom(source)
   *     .pad(10);
   * ```
   * @param rectangle - The rectangle to copy from
   * @returns Returns itself
   * @see {@link Rectangle.copyTo} For copying to another rectangle
   * @see {@link Rectangle.clone} For creating new rectangle copy
   */
  copyFrom(rectangle) {
    this.x = rectangle.x;
    this.y = rectangle.y;
    this.width = rectangle.width;
    this.height = rectangle.height;
    return this;
  }
  /**
   * Copies this rectangle to another one.
   * @example
   * ```ts
   * // Basic copying
   * const source = new Rectangle(100, 100, 200, 150);
   * const target = new Rectangle();
   * source.copyTo(target);
   *
   * // Chain with other operations
   * const result = source
   *     .copyTo(new Rectangle())
   *     .getBounds();
   * ```
   * @param rectangle - The rectangle to copy to
   * @returns Returns given parameter
   * @see {@link Rectangle.copyFrom} For copying from another rectangle
   * @see {@link Rectangle.clone} For creating new rectangle copy
   */
  copyTo(rectangle) {
    rectangle.copyFrom(this);
    return rectangle;
  }
  /**
   * Checks whether the x and y coordinates given are contained within this Rectangle
   * @example
   * ```ts
   * // Basic containment check
   * const rect = new Rectangle(100, 100, 200, 150);
   * const isInside = rect.contains(150, 125); // true
   * // Check edge cases
   * console.log(rect.contains(100, 100)); // true (on edge)
   * console.log(rect.contains(300, 250)); // false (outside)
   * ```
   * @param x - The X coordinate of the point to test
   * @param y - The Y coordinate of the point to test
   * @returns Whether the x/y coordinates are within this Rectangle
   * @see {@link Rectangle.containsRect} For rectangle containment
   * @see {@link Rectangle.strokeContains} For checking stroke intersection
   */
  contains(x, y) {
    if (this.width <= 0 || this.height <= 0) {
      return false;
    }
    if (x >= this.x && x < this.x + this.width) {
      if (y >= this.y && y < this.y + this.height) {
        return true;
      }
    }
    return false;
  }
  /**
   * Checks whether the x and y coordinates given are contained within this rectangle including the stroke.
   * @example
   * ```ts
   * // Basic stroke check
   * const rect = new Rectangle(100, 100, 200, 150);
   * const isOnStroke = rect.strokeContains(150, 100, 4); // 4px line width
   *
   * // Check with different alignments
   * const innerStroke = rect.strokeContains(150, 100, 4, 1);   // Inside
   * const centerStroke = rect.strokeContains(150, 100, 4, 0.5); // Centered
   * const outerStroke = rect.strokeContains(150, 100, 4, 0);   // Outside
   * ```
   * @param x - The X coordinate of the point to test
   * @param y - The Y coordinate of the point to test
   * @param strokeWidth - The width of the line to check
   * @param alignment - The alignment of the stroke (1 = inner, 0.5 = centered, 0 = outer)
   * @returns Whether the x/y coordinates are within this rectangle's stroke
   * @see {@link Rectangle.contains} For checking fill containment
   * @see {@link Rectangle.getBounds} For getting stroke bounds
   */
  strokeContains(x, y, strokeWidth, alignment = 0.5) {
    const { width, height } = this;
    if (width <= 0 || height <= 0) return false;
    const _x = this.x;
    const _y = this.y;
    const strokeWidthOuter = strokeWidth * (1 - alignment);
    const strokeWidthInner = strokeWidth - strokeWidthOuter;
    const outerLeft = _x - strokeWidthOuter;
    const outerRight = _x + width + strokeWidthOuter;
    const outerTop = _y - strokeWidthOuter;
    const outerBottom = _y + height + strokeWidthOuter;
    const innerLeft = _x + strokeWidthInner;
    const innerRight = _x + width - strokeWidthInner;
    const innerTop = _y + strokeWidthInner;
    const innerBottom = _y + height - strokeWidthInner;
    return x >= outerLeft && x <= outerRight && y >= outerTop && y <= outerBottom && !(x > innerLeft && x < innerRight && y > innerTop && y < innerBottom);
  }
  /**
   * Determines whether the `other` Rectangle transformed by `transform` intersects with `this` Rectangle object.
   * Returns true only if the area of the intersection is >0, this means that Rectangles
   * sharing a side are not overlapping. Another side effect is that an arealess rectangle
   * (width or height equal to zero) can't intersect any other rectangle.
   * @param {Rectangle} other - The Rectangle to intersect with `this`.
   * @param {Matrix} transform - The transformation matrix of `other`.
   * @returns {boolean} A value of `true` if the transformed `other` Rectangle intersects with `this`; otherwise `false`.
   */
  /**
   * Determines whether the `other` Rectangle transformed by `transform` intersects with `this` Rectangle object.
   *
   * Returns true only if the area of the intersection is greater than 0.
   * This means that rectangles sharing only a side are not considered intersecting.
   * @example
   * ```ts
   * // Basic intersection check
   * const rect1 = new Rectangle(0, 0, 100, 100);
   * const rect2 = new Rectangle(50, 50, 100, 100);
   * console.log(rect1.intersects(rect2)); // true
   *
   * // With transformation matrix
   * const matrix = new Matrix();
   * matrix.rotate(Math.PI / 4); // 45 degrees
   * console.log(rect1.intersects(rect2, matrix)); // Checks with rotation
   *
   * // Edge cases
   * const zeroWidth = new Rectangle(0, 0, 0, 100);
   * console.log(rect1.intersects(zeroWidth)); // false (no area)
   * ```
   * @remarks
   * - Returns true only if intersection area is > 0
   * - Rectangles sharing only a side are not intersecting
   * - Zero-area rectangles cannot intersect anything
   * - Supports optional transformation matrix
   * @param other - The Rectangle to intersect with `this`
   * @param transform - Optional transformation matrix of `other`
   * @returns True if the transformed `other` Rectangle intersects with `this`
   * @see {@link Rectangle.containsRect} For containment testing
   * @see {@link Rectangle.contains} For point testing
   */
  intersects(other, transform) {
    if (!transform) {
      const x02 = this.x < other.x ? other.x : this.x;
      const x12 = this.right > other.right ? other.right : this.right;
      if (x12 <= x02) {
        return false;
      }
      const y02 = this.y < other.y ? other.y : this.y;
      const y12 = this.bottom > other.bottom ? other.bottom : this.bottom;
      return y12 > y02;
    }
    const x0 = this.left;
    const x1 = this.right;
    const y0 = this.top;
    const y1 = this.bottom;
    if (x1 <= x0 || y1 <= y0) {
      return false;
    }
    const lt = tempPoints[0].set(other.left, other.top);
    const lb = tempPoints[1].set(other.left, other.bottom);
    const rt = tempPoints[2].set(other.right, other.top);
    const rb = tempPoints[3].set(other.right, other.bottom);
    if (rt.x <= lt.x || lb.y <= lt.y) {
      return false;
    }
    const s = Math.sign(transform.a * transform.d - transform.b * transform.c);
    if (s === 0) {
      return false;
    }
    transform.apply(lt, lt);
    transform.apply(lb, lb);
    transform.apply(rt, rt);
    transform.apply(rb, rb);
    if (Math.max(lt.x, lb.x, rt.x, rb.x) <= x0 || Math.min(lt.x, lb.x, rt.x, rb.x) >= x1 || Math.max(lt.y, lb.y, rt.y, rb.y) <= y0 || Math.min(lt.y, lb.y, rt.y, rb.y) >= y1) {
      return false;
    }
    const nx = s * (lb.y - lt.y);
    const ny = s * (lt.x - lb.x);
    const n00 = nx * x0 + ny * y0;
    const n10 = nx * x1 + ny * y0;
    const n01 = nx * x0 + ny * y1;
    const n11 = nx * x1 + ny * y1;
    if (Math.max(n00, n10, n01, n11) <= nx * lt.x + ny * lt.y || Math.min(n00, n10, n01, n11) >= nx * rb.x + ny * rb.y) {
      return false;
    }
    const mx = s * (lt.y - rt.y);
    const my = s * (rt.x - lt.x);
    const m00 = mx * x0 + my * y0;
    const m10 = mx * x1 + my * y0;
    const m01 = mx * x0 + my * y1;
    const m11 = mx * x1 + my * y1;
    if (Math.max(m00, m10, m01, m11) <= mx * lt.x + my * lt.y || Math.min(m00, m10, m01, m11) >= mx * rb.x + my * rb.y) {
      return false;
    }
    return true;
  }
  /**
   * Pads the rectangle making it grow in all directions.
   *
   * If paddingY is omitted, both paddingX and paddingY will be set to paddingX.
   * @example
   * ```ts
   * // Basic padding
   * const rect = new Rectangle(100, 100, 200, 150);
   * rect.pad(10); // Adds 10px padding on all sides
   *
   * // Different horizontal and vertical padding
   * const uiRect = new Rectangle(0, 0, 100, 50);
   * uiRect.pad(20, 10); // 20px horizontal, 10px vertical
   * ```
   * @remarks
   * - Adjusts x/y by subtracting padding
   * - Increases width/height by padding * 2
   * - Common in UI layout calculations
   * - Chainable with other methods
   * @param paddingX - The horizontal padding amount
   * @param paddingY - The vertical padding amount
   * @returns Returns itself
   * @see {@link Rectangle.enlarge} For growing to include another rectangle
   * @see {@link Rectangle.fit} For shrinking to fit within another rectangle
   */
  pad(paddingX = 0, paddingY = paddingX) {
    this.x -= paddingX;
    this.y -= paddingY;
    this.width += paddingX * 2;
    this.height += paddingY * 2;
    return this;
  }
  /**
   * Fits this rectangle around the passed one.
   * @example
   * ```ts
   * // Basic fitting
   * const container = new Rectangle(0, 0, 100, 100);
   * const content = new Rectangle(25, 25, 200, 200);
   * content.fit(container); // Clips to container bounds
   * ```
   * @param rectangle - The rectangle to fit around
   * @returns Returns itself
   * @see {@link Rectangle.enlarge} For growing to include another rectangle
   * @see {@link Rectangle.pad} For adding padding around the rectangle
   */
  fit(rectangle) {
    const x1 = Math.max(this.x, rectangle.x);
    const x2 = Math.min(this.x + this.width, rectangle.x + rectangle.width);
    const y1 = Math.max(this.y, rectangle.y);
    const y2 = Math.min(this.y + this.height, rectangle.y + rectangle.height);
    this.x = x1;
    this.width = Math.max(x2 - x1, 0);
    this.y = y1;
    this.height = Math.max(y2 - y1, 0);
    return this;
  }
  /**
   * Enlarges rectangle so that its corners lie on a grid defined by resolution.
   * @example
   * ```ts
   * // Basic grid alignment
   * const rect = new Rectangle(10.2, 10.6, 100.8, 100.4);
   * rect.ceil(); // Aligns to whole pixels
   *
   * // Custom resolution grid
   * const uiRect = new Rectangle(5.3, 5.7, 50.2, 50.8);
   * uiRect.ceil(0.5); // Aligns to half pixels
   *
   * // Use with precision value
   * const preciseRect = new Rectangle(20.001, 20.999, 100.001, 100.999);
   * preciseRect.ceil(1, 0.01); // Handles small decimal variations
   * ```
   * @param resolution - The grid size to align to (1 = whole pixels)
   * @param eps - Small number to prevent floating point errors
   * @returns Returns itself
   * @see {@link Rectangle.fit} For constraining to bounds
   * @see {@link Rectangle.enlarge} For growing dimensions
   */
  ceil(resolution = 1, eps = 1e-3) {
    const x2 = Math.ceil((this.x + this.width - eps) * resolution) / resolution;
    const y2 = Math.ceil((this.y + this.height - eps) * resolution) / resolution;
    this.x = Math.floor((this.x + eps) * resolution) / resolution;
    this.y = Math.floor((this.y + eps) * resolution) / resolution;
    this.width = x2 - this.x;
    this.height = y2 - this.y;
    return this;
  }
  /**
   * Scales the rectangle's dimensions and position by the specified factors.
   * @example
   * ```ts
   * const rect = new Rectangle(50, 50, 100, 100);
   *
   * // Scale uniformly
   * rect.scale(0.5, 0.5);
   * // rect is now: x=25, y=25, width=50, height=50
   *
   * // non-uniformly
   * rect.scale(0.5, 1);
   * // rect is now: x=25, y=50, width=50, height=100
   * ```
   * @param x - The factor by which to scale the horizontal properties (x, width).
   * @param y - The factor by which to scale the vertical properties (y, height).
   * @returns Returns itself
   */
  scale(x, y = x) {
    this.x *= x;
    this.y *= y;
    this.width *= x;
    this.height *= y;
    return this;
  }
  /**
   * Enlarges this rectangle to include the passed rectangle.
   * @example
   * ```ts
   * // Basic enlargement
   * const rect = new Rectangle(50, 50, 100, 100);
   * const other = new Rectangle(0, 0, 200, 75);
   * rect.enlarge(other);
   * // rect is now: x=0, y=0, width=200, height=150
   *
   * // Use for bounding box calculation
   * const bounds = new Rectangle();
   * objects.forEach((obj) => {
   *     bounds.enlarge(obj.getBounds());
   * });
   * ```
   * @param rectangle - The rectangle to include
   * @returns Returns itself
   * @see {@link Rectangle.fit} For shrinking to fit within another rectangle
   * @see {@link Rectangle.pad} For adding padding around the rectangle
   */
  enlarge(rectangle) {
    const x1 = Math.min(this.x, rectangle.x);
    const x2 = Math.max(this.x + this.width, rectangle.x + rectangle.width);
    const y1 = Math.min(this.y, rectangle.y);
    const y2 = Math.max(this.y + this.height, rectangle.y + rectangle.height);
    this.x = x1;
    this.width = x2 - x1;
    this.y = y1;
    this.height = y2 - y1;
    return this;
  }
  /**
   * Returns the framing rectangle of the rectangle as a Rectangle object
   * @example
   * ```ts
   * // Basic bounds retrieval
   * const rect = new Rectangle(100, 100, 200, 150);
   * const bounds = rect.getBounds();
   *
   * // Reuse existing rectangle
   * const out = new Rectangle();
   * rect.getBounds(out);
   * ```
   * @param out - Optional rectangle to store the result
   * @returns The framing rectangle
   * @see {@link Rectangle.copyFrom} For direct copying
   * @see {@link Rectangle.clone} For creating new copy
   */
  getBounds(out) {
    out || (out = new _Rectangle());
    out.copyFrom(this);
    return out;
  }
  /**
   * Determines whether another Rectangle is fully contained within this Rectangle.
   *
   * Rectangles that occupy the same space are considered to be containing each other.
   *
   * Rectangles without area (width or height equal to zero) can't contain anything,
   * not even other arealess rectangles.
   * @example
   * ```ts
   * // Check if one rectangle contains another
   * const container = new Rectangle(0, 0, 100, 100);
   * const inner = new Rectangle(25, 25, 50, 50);
   *
   * console.log(container.containsRect(inner)); // true
   *
   * // Check overlapping rectangles
   * const partial = new Rectangle(75, 75, 50, 50);
   * console.log(container.containsRect(partial)); // false
   *
   * // Zero-area rectangles can't contain anything
   * const empty = new Rectangle(0, 0, 0, 100);
   * console.log(empty.containsRect(inner)); // false
   * ```
   * @param other - The Rectangle to check for containment
   * @returns True if other is fully contained within this Rectangle
   * @see {@link Rectangle.contains} For point containment
   * @see {@link Rectangle.intersects} For overlap testing
   */
  containsRect(other) {
    if (this.width <= 0 || this.height <= 0) return false;
    const x1 = other.x;
    const y1 = other.y;
    const x2 = other.x + other.width;
    const y2 = other.y + other.height;
    return x1 >= this.x && x1 < this.x + this.width && y1 >= this.y && y1 < this.y + this.height && x2 >= this.x && x2 <= this.x + this.width && y2 >= this.y && y2 <= this.y + this.height;
  }
  /**
   * Sets the position and dimensions of the rectangle.
   * @example
   * ```ts
   * // Basic usage
   * const rect = new Rectangle();
   * rect.set(100, 100, 200, 150);
   *
   * // Chain with other operations
   * const bounds = new Rectangle()
   *     .set(0, 0, 100, 100)
   *     .pad(10);
   * ```
   * @param x - The X coordinate of the upper-left corner of the rectangle
   * @param y - The Y coordinate of the upper-left corner of the rectangle
   * @param width - The overall width of the rectangle
   * @param height - The overall height of the rectangle
   * @returns Returns itself for method chaining
   * @see {@link Rectangle.copyFrom} For copying from another rectangle
   * @see {@link Rectangle.clone} For creating a new copy
   */
  set(x, y, width, height) {
    this.x = x;
    this.y = y;
    this.width = width;
    this.height = height;
    return this;
  }
  toString() {
    return `[pixi.js/math:Rectangle x=${this.x} y=${this.y} width=${this.width} height=${this.height}]`;
  }
};

// node_modules/pixi.js/lib/utils/logging/deprecation.mjs
var warnings = /* @__PURE__ */ new Set();
var v8_0_0 = "8.0.0";
var v8_3_4 = "8.3.4";
var deprecationState = {
  quiet: false,
  noColor: false
};
var deprecation = ((version, message, ignoreDepth = 3) => {
  if (deprecationState.quiet || warnings.has(message)) return;
  let stack = new Error().stack;
  const deprecationMessage = `${message}
Deprecated since v${version}`;
  const useGroup = typeof console.groupCollapsed === "function" && !deprecationState.noColor;
  if (typeof stack === "undefined") {
    console.warn("PixiJS Deprecation Warning: ", deprecationMessage);
  } else {
    stack = stack.split("\n").splice(ignoreDepth).join("\n");
    if (useGroup) {
      console.groupCollapsed(
        "%cPixiJS Deprecation Warning: %c%s",
        "color:#614108;background:#fffbe6",
        "font-weight:normal;color:#614108;background:#fffbe6",
        deprecationMessage
      );
      console.warn(stack);
      console.groupEnd();
    } else {
      console.warn("PixiJS Deprecation Warning: ", deprecationMessage);
      console.warn(stack);
    }
  }
  warnings.add(message);
});
Object.defineProperties(deprecation, {
  quiet: {
    get: () => deprecationState.quiet,
    set: (value) => {
      deprecationState.quiet = value;
    },
    enumerable: true,
    configurable: false
  },
  noColor: {
    get: () => deprecationState.noColor,
    set: (value) => {
      deprecationState.noColor = value;
    },
    enumerable: true,
    configurable: false
  }
});

// node_modules/pixi.js/lib/utils/misc/NOOP.mjs
var NOOP = () => {
};

// node_modules/pixi.js/lib/maths/misc/pow2.mjs
function nextPow2(v) {
  v += v === 0 ? 1 : 0;
  --v;
  v |= v >>> 1;
  v |= v >>> 2;
  v |= v >>> 4;
  v |= v >>> 8;
  v |= v >>> 16;
  return v + 1;
}
function isPow2(v) {
  return !(v & v - 1) && !!v;
}
function log2(v) {
  let r = (v > 65535 ? 1 : 0) << 4;
  v >>>= r;
  let shift = (v > 255 ? 1 : 0) << 3;
  v >>>= shift;
  r |= shift;
  shift = (v > 15 ? 1 : 0) << 2;
  v >>>= shift;
  r |= shift;
  shift = (v > 3 ? 1 : 0) << 1;
  v >>>= shift;
  r |= shift;
  return r | v >> 1;
}

// node_modules/pixi.js/lib/scene/container/utils/definedProps.mjs
function definedProps(obj) {
  const result = {};
  for (const key in obj) {
    if (obj[key] !== void 0) {
      result[key] = obj[key];
    }
  }
  return result;
}

// node_modules/pixi.js/lib/rendering/renderers/shared/texture/TextureStyle.mjs
var idHash2 = /* @__PURE__ */ Object.create(null);
function createResourceIdFromString(value) {
  const id = idHash2[value];
  if (id === void 0) {
    idHash2[value] = uid("resource");
  }
  return id;
}
var _TextureStyle = class _TextureStyle2 extends eventemitter3_default {
  /**
   * @param options - options for the style
   */
  constructor(options = {}) {
    super();
    this._resourceType = "textureSampler";
    this._touched = 0;
    this._maxAnisotropy = 1;
    this.destroyed = false;
    options = { ..._TextureStyle2.defaultOptions, ...options };
    this.addressMode = options.addressMode;
    this.addressModeU = options.addressModeU ?? this.addressModeU;
    this.addressModeV = options.addressModeV ?? this.addressModeV;
    this.addressModeW = options.addressModeW ?? this.addressModeW;
    this.scaleMode = options.scaleMode;
    this.magFilter = options.magFilter ?? this.magFilter;
    this.minFilter = options.minFilter ?? this.minFilter;
    this.mipmapFilter = options.mipmapFilter ?? this.mipmapFilter;
    this.lodMinClamp = options.lodMinClamp;
    this.lodMaxClamp = options.lodMaxClamp;
    this.compare = options.compare;
    this.maxAnisotropy = options.maxAnisotropy ?? 1;
  }
  set addressMode(value) {
    this.addressModeU = value;
    this.addressModeV = value;
    this.addressModeW = value;
  }
  /** setting this will set wrapModeU,wrapModeV and wrapModeW all at once! */
  get addressMode() {
    return this.addressModeU;
  }
  set wrapMode(value) {
    deprecation(v8_0_0, "TextureStyle.wrapMode is now TextureStyle.addressMode");
    this.addressMode = value;
  }
  get wrapMode() {
    return this.addressMode;
  }
  set scaleMode(value) {
    this.magFilter = value;
    this.minFilter = value;
    this.mipmapFilter = value;
  }
  /** setting this will set magFilter,minFilter and mipmapFilter all at once!  */
  get scaleMode() {
    return this.magFilter;
  }
  /** Specifies the maximum anisotropy value clamp used by the sampler. */
  set maxAnisotropy(value) {
    this._maxAnisotropy = Math.min(value, 16);
    if (this._maxAnisotropy > 1) {
      this.scaleMode = "linear";
    }
  }
  get maxAnisotropy() {
    return this._maxAnisotropy;
  }
  // TODO - move this to WebGL?
  get _resourceId() {
    return this._sharedResourceId || this._generateResourceId();
  }
  update() {
    this._sharedResourceId = null;
    this.emit("change", this);
  }
  _generateResourceId() {
    const bigKey = `${this.addressModeU}-${this.addressModeV}-${this.addressModeW}-${this.magFilter}-${this.minFilter}-${this.mipmapFilter}-${this.lodMinClamp}-${this.lodMaxClamp}-${this.compare}-${this._maxAnisotropy}`;
    this._sharedResourceId = createResourceIdFromString(bigKey);
    return this._resourceId;
  }
  /** Destroys the style */
  destroy() {
    this.destroyed = true;
    this.emit("destroy", this);
    this.emit("change", this);
    this.removeAllListeners();
  }
};
_TextureStyle.defaultOptions = {
  addressMode: "clamp-to-edge",
  scaleMode: "linear"
};
var TextureStyle = _TextureStyle;

// node_modules/pixi.js/lib/rendering/renderers/shared/texture/sources/TextureSource.mjs
var _TextureSource = class _TextureSource2 extends eventemitter3_default {
  /**
   * @param options - options for creating a new TextureSource
   */
  constructor(options = {}) {
    super();
    this.options = options;
    this._gpuData = /* @__PURE__ */ Object.create(null);
    this._gcLastUsed = -1;
    this.uid = uid("textureSource");
    this._resourceType = "textureSource";
    this._resourceId = uid("resource");
    this.uploadMethodId = "unknown";
    this._resolution = 1;
    this.pixelWidth = 1;
    this.pixelHeight = 1;
    this.width = 1;
    this.height = 1;
    this.sampleCount = 1;
    this.mipLevelCount = 1;
    this.autoGenerateMipmaps = false;
    this.format = "rgba8unorm";
    this.dimension = "2d";
    this.viewDimension = "2d";
    this.arrayLayerCount = 1;
    this._ownsStyle = false;
    this.antialias = false;
    this.transient = false;
    this._touched = 0;
    this._batchTick = -1;
    this._textureBindLocation = -1;
    options = { ..._TextureSource2.defaultOptions, ...options };
    this.label = options.label ?? "";
    this.resource = options.resource;
    this.autoGarbageCollect = options.autoGarbageCollect;
    this._resolution = options.resolution;
    if (options.width) {
      this.pixelWidth = options.width * this._resolution;
    } else {
      this.pixelWidth = this.resource ? this.resourceWidth ?? 1 : 1;
    }
    if (options.height) {
      this.pixelHeight = options.height * this._resolution;
    } else {
      this.pixelHeight = this.resource ? this.resourceHeight ?? 1 : 1;
    }
    this.width = this.pixelWidth / this._resolution;
    this.height = this.pixelHeight / this._resolution;
    this.format = options.format;
    this.dimension = options.dimensions;
    this.viewDimension = options.viewDimension ?? options.dimensions;
    this.arrayLayerCount = options.arrayLayerCount;
    this.mipLevelCount = options.mipLevelCount;
    this.autoGenerateMipmaps = options.autoGenerateMipmaps;
    this.sampleCount = options.sampleCount;
    this.antialias = options.antialias;
    this.transient = options.transient ?? false;
    this.alphaMode = options.alphaMode;
    this.style = new TextureStyle(definedProps(options));
    this._ownsStyle = true;
    this.destroyed = false;
    this._refreshPOT();
  }
  /** returns itself */
  get source() {
    return this;
  }
  /** the style of the texture */
  get style() {
    return this._style;
  }
  set style(value) {
    if (this.style === value) return;
    this._ownsStyle = false;
    this._style?.off("change", this._onStyleChange, this);
    this._style = value;
    this._style?.on("change", this._onStyleChange, this);
    this._onStyleChange();
  }
  /** Specifies the maximum anisotropy value clamp used by the sampler. */
  set maxAnisotropy(value) {
    this._style.maxAnisotropy = value;
  }
  get maxAnisotropy() {
    return this._style.maxAnisotropy;
  }
  /** setting this will set wrapModeU, wrapModeV and wrapModeW all at once! */
  get addressMode() {
    return this._style.addressMode;
  }
  set addressMode(value) {
    this._style.addressMode = value;
  }
  /** setting this will set wrapModeU, wrapModeV and wrapModeW all at once! */
  get repeatMode() {
    return this._style.addressMode;
  }
  set repeatMode(value) {
    this._style.addressMode = value;
  }
  /** Specifies the sampling behavior when the sample footprint is smaller than or equal to one texel. */
  get magFilter() {
    return this._style.magFilter;
  }
  set magFilter(value) {
    this._style.magFilter = value;
  }
  /** Specifies the sampling behavior when the sample footprint is larger than one texel. */
  get minFilter() {
    return this._style.minFilter;
  }
  set minFilter(value) {
    this._style.minFilter = value;
  }
  /** Specifies behavior for sampling between mipmap levels. */
  get mipmapFilter() {
    return this._style.mipmapFilter;
  }
  set mipmapFilter(value) {
    this._style.mipmapFilter = value;
  }
  /** Specifies the minimum and maximum levels of detail, respectively, used internally when sampling a texture. */
  get lodMinClamp() {
    return this._style.lodMinClamp;
  }
  set lodMinClamp(value) {
    this._style.lodMinClamp = value;
  }
  /** Specifies the minimum and maximum levels of detail, respectively, used internally when sampling a texture. */
  get lodMaxClamp() {
    return this._style.lodMaxClamp;
  }
  set lodMaxClamp(value) {
    this._style.lodMaxClamp = value;
  }
  _onStyleChange() {
    this.emit("styleChange", this);
  }
  /** call this if you have modified the texture outside of the constructor */
  update() {
    if (this.resource) {
      const resolution = this._resolution;
      const didResize = this.resize(this.resourceWidth / resolution, this.resourceHeight / resolution);
      if (didResize) return;
    }
    this.emit("update", this);
  }
  /** Destroys this texture source */
  destroy() {
    this.destroyed = true;
    this.unload();
    this.emit("destroy", this);
    if (this._style) {
      if (this._ownsStyle) this._style.destroy();
      this._style = null;
    }
    this.uploadMethodId = null;
    this.resource = null;
    this.removeAllListeners();
  }
  /**
   * This will unload the Texture source from the GPU. This will free up the GPU memory
   * As soon as it is required fore rendering, it will be re-uploaded.
   */
  unload() {
    this._resourceId = uid("resource");
    this.emit("change", this);
    this.emit("unload", this);
    for (const key in this._gpuData) {
      this._gpuData[key]?.destroy?.();
    }
    this._gpuData = /* @__PURE__ */ Object.create(null);
  }
  /** the width of the resource. This is the REAL pure number, not accounting resolution   */
  get resourceWidth() {
    const { resource } = this;
    return resource.naturalWidth || resource.videoWidth || resource.displayWidth || resource.width;
  }
  /** the height of the resource. This is the REAL pure number, not accounting resolution */
  get resourceHeight() {
    const { resource } = this;
    return resource.naturalHeight || resource.videoHeight || resource.displayHeight || resource.height;
  }
  /**
   * the resolution of the texture. Changing this number, will not change the number of pixels in the actual texture
   * but will the size of the texture when rendered.
   *
   * changing the resolution of this texture to 2 for example will make it appear twice as small when rendered (as pixel
   * density will have increased)
   */
  get resolution() {
    return this._resolution;
  }
  set resolution(resolution) {
    if (this._resolution === resolution) return;
    this._resolution = resolution;
    this.width = this.pixelWidth / resolution;
    this.height = this.pixelHeight / resolution;
  }
  /**
   * Resize the texture, this is handy if you want to use the texture as a render texture
   * @param width - the new width of the texture
   * @param height - the new height of the texture
   * @param resolution - the new resolution of the texture
   * @returns - if the texture was resized
   */
  resize(width, height, resolution) {
    resolution || (resolution = this._resolution);
    width || (width = this.width);
    height || (height = this.height);
    const newPixelWidth = Math.round(width * resolution);
    const newPixelHeight = Math.round(height * resolution);
    this.width = newPixelWidth / resolution;
    this.height = newPixelHeight / resolution;
    this._resolution = resolution;
    if (this.pixelWidth === newPixelWidth && this.pixelHeight === newPixelHeight) {
      return false;
    }
    this._refreshPOT();
    this.pixelWidth = newPixelWidth;
    this.pixelHeight = newPixelHeight;
    this.emit("resize", this);
    this._resourceId = uid("resource");
    this.emit("change", this);
    return true;
  }
  /**
   * Lets the renderer know that this texture has been updated and its mipmaps should be re-generated.
   * This is only important for RenderTexture instances, as standard Texture instances will have their
   * mipmaps generated on upload. You should call this method after you make any change to the texture
   *
   * The reason for this is is can be quite expensive to update mipmaps for a texture. So by default,
   * We want you, the developer to specify when this action should happen.
   *
   * Generally you don't want to have mipmaps generated on Render targets that are changed every frame,
   */
  updateMipmaps() {
    if (this.autoGenerateMipmaps && this.mipLevelCount > 1) {
      this.emit("updateMipmaps", this);
    }
  }
  set wrapMode(value) {
    this._style.wrapMode = value;
  }
  get wrapMode() {
    return this._style.wrapMode;
  }
  set scaleMode(value) {
    this._style.scaleMode = value;
  }
  /** setting this will set magFilter,minFilter and mipmapFilter all at once!  */
  get scaleMode() {
    return this._style.scaleMode;
  }
  /**
   * Refresh check for isPowerOfTwo texture based on size
   * @private
   */
  _refreshPOT() {
    this.isPowerOfTwo = isPow2(this.pixelWidth) && isPow2(this.pixelHeight);
  }
  static test(_resource) {
    throw new Error("Unimplemented");
  }
};
_TextureSource.defaultOptions = {
  resolution: 1,
  format: "bgra8unorm",
  alphaMode: "premultiply-alpha-on-upload",
  dimensions: "2d",
  viewDimension: "2d",
  arrayLayerCount: 1,
  mipLevelCount: 1,
  autoGenerateMipmaps: false,
  sampleCount: 1,
  antialias: false,
  autoGarbageCollect: false
};
var TextureSource = _TextureSource;

// node_modules/pixi.js/lib/rendering/renderers/shared/texture/sources/BufferImageSource.mjs
var BufferImageSource = class extends TextureSource {
  constructor(options) {
    const buffer = options.resource || new Float32Array(options.width * options.height * 4);
    let format = options.format;
    if (!format) {
      if (buffer instanceof Float32Array) {
        format = "rgba32float";
      } else if (buffer instanceof Int32Array) {
        format = "rgba32uint";
      } else if (buffer instanceof Uint32Array) {
        format = "rgba32uint";
      } else if (buffer instanceof Int16Array) {
        format = "rgba16uint";
      } else if (buffer instanceof Uint16Array) {
        format = "rgba16uint";
      } else if (buffer instanceof Int8Array) {
        format = "bgra8unorm";
      } else {
        format = "bgra8unorm";
      }
    }
    super({
      ...options,
      resource: buffer,
      format
    });
    this.uploadMethodId = "buffer";
  }
  static test(resource) {
    return resource instanceof Int8Array || resource instanceof Uint8Array || resource instanceof Uint8ClampedArray || resource instanceof Int16Array || resource instanceof Uint16Array || resource instanceof Int32Array || resource instanceof Uint32Array || resource instanceof Float32Array;
  }
};
BufferImageSource.extension = ExtensionType.TextureSource;

// node_modules/pixi.js/lib/rendering/renderers/shared/texture/TextureMatrix.mjs
var tempMat = new Matrix();
var TextureMatrix = class {
  /**
   * @param texture - observed texture
   * @param clampMargin - Changes frame clamping, 0.5 by default. Use -0.5 for extra border.
   */
  constructor(texture, clampMargin) {
    this.mapCoord = new Matrix();
    this.uClampFrame = new Float32Array(4);
    this.uClampOffset = new Float32Array(2);
    this._updateID = 0;
    this.clampOffset = 0;
    if (typeof clampMargin === "undefined") {
      this.clampMargin = texture.width < 10 ? 0 : 0.5;
    } else {
      this.clampMargin = clampMargin;
    }
    this.isSimple = false;
    this.texture = texture;
  }
  /** Texture property. */
  get texture() {
    return this._texture;
  }
  set texture(value) {
    if (this._texture !== value) {
      this._texture?.removeListener("update", this.update, this);
      this._texture = value;
      this._texture.addListener("update", this.update, this);
    }
    this.update();
  }
  /** Releases the observed texture, removing the `update` listener from it. */
  destroy() {
    this._texture?.removeListener("update", this.update, this);
    this._texture = null;
  }
  /**
   * Multiplies uvs array to transform
   * @param uvs - mesh uvs
   * @param [out=uvs] - output
   * @returns - output
   */
  multiplyUvs(uvs, out) {
    if (out === void 0) {
      out = uvs;
    }
    const mat = this.mapCoord;
    for (let i = 0; i < uvs.length; i += 2) {
      const x = uvs[i];
      const y = uvs[i + 1];
      out[i] = x * mat.a + y * mat.c + mat.tx;
      out[i + 1] = x * mat.b + y * mat.d + mat.ty;
    }
    return out;
  }
  /**
   * Updates matrices if texture was changed
   * @returns - whether or not it was updated
   */
  update() {
    const tex = this._texture;
    this._updateID++;
    const uvs = tex.uvs;
    this.mapCoord.set(uvs.x1 - uvs.x0, uvs.y1 - uvs.y0, uvs.x3 - uvs.x0, uvs.y3 - uvs.y0, uvs.x0, uvs.y0);
    const orig = tex.orig;
    const trim = tex.trim;
    if (trim) {
      tempMat.set(
        orig.width / trim.width,
        0,
        0,
        orig.height / trim.height,
        -trim.x / trim.width,
        -trim.y / trim.height
      );
      this.mapCoord.append(tempMat);
    }
    const texBase = tex.source;
    const frame = this.uClampFrame;
    const margin = this.clampMargin / texBase._resolution;
    const offset = this.clampOffset / texBase._resolution;
    frame[0] = (tex.frame.x + margin + offset) / texBase.width;
    frame[1] = (tex.frame.y + margin + offset) / texBase.height;
    frame[2] = (tex.frame.x + tex.frame.width - margin + offset) / texBase.width;
    frame[3] = (tex.frame.y + tex.frame.height - margin + offset) / texBase.height;
    this.uClampOffset[0] = this.clampOffset / texBase.pixelWidth;
    this.uClampOffset[1] = this.clampOffset / texBase.pixelHeight;
    this.isSimple = tex.frame.width === texBase.width && tex.frame.height === texBase.height && tex.rotate === 0;
    return true;
  }
};

// node_modules/pixi.js/lib/rendering/renderers/shared/texture/Texture.mjs
var Texture = class extends eventemitter3_default {
  /**
   * @param {TextureOptions} options - Options for the texture
   */
  constructor({
    source,
    label,
    frame,
    orig,
    trim,
    defaultAnchor,
    defaultBorders,
    rotate,
    dynamic
  } = {}) {
    super();
    this.uid = uid("texture");
    this.uvs = { x0: 0, y0: 0, x1: 0, y1: 0, x2: 0, y2: 0, x3: 0, y3: 0 };
    this.frame = new Rectangle();
    this.noFrame = false;
    this.dynamic = false;
    this.isTexture = true;
    this.label = label;
    this.source = source?.source ?? new TextureSource();
    this.noFrame = !frame;
    if (frame) {
      this.frame.copyFrom(frame);
    } else {
      const { width, height } = this._source;
      this.frame.width = width;
      this.frame.height = height;
    }
    this.orig = orig || this.frame;
    this.trim = trim;
    this.rotate = rotate ?? 0;
    this.defaultAnchor = defaultAnchor;
    this.defaultBorders = defaultBorders;
    this.destroyed = false;
    this.dynamic = dynamic || false;
    this.updateUvs();
  }
  set source(value) {
    if (this._source) {
      this._source.off("resize", this.update, this);
    }
    this._source = value;
    value.on("resize", this.update, this);
    this.emit("update", this);
  }
  /** the underlying source of the texture (equivalent of baseTexture in v7) */
  get source() {
    return this._source;
  }
  /** returns a TextureMatrix instance for this texture. By default, that object is not created because its heavy. */
  get textureMatrix() {
    if (!this._textureMatrix) {
      this._textureMatrix = new TextureMatrix(this);
    }
    return this._textureMatrix;
  }
  /** The width of the Texture in pixels. */
  get width() {
    return this.orig.width;
  }
  /** The height of the Texture in pixels. */
  get height() {
    return this.orig.height;
  }
  /** Call this function when you have modified the frame of this texture. */
  updateUvs() {
    const { uvs, frame } = this;
    const { width, height } = this._source;
    const nX = frame.x / width;
    const nY = frame.y / height;
    const nW = frame.width / width;
    const nH = frame.height / height;
    let rotate = this.rotate;
    if (rotate) {
      const w2 = nW / 2;
      const h2 = nH / 2;
      const cX = nX + w2;
      const cY = nY + h2;
      rotate = groupD8.add(rotate, groupD8.NW);
      uvs.x0 = cX + w2 * groupD8.uX(rotate);
      uvs.y0 = cY + h2 * groupD8.uY(rotate);
      rotate = groupD8.add(rotate, 2);
      uvs.x1 = cX + w2 * groupD8.uX(rotate);
      uvs.y1 = cY + h2 * groupD8.uY(rotate);
      rotate = groupD8.add(rotate, 2);
      uvs.x2 = cX + w2 * groupD8.uX(rotate);
      uvs.y2 = cY + h2 * groupD8.uY(rotate);
      rotate = groupD8.add(rotate, 2);
      uvs.x3 = cX + w2 * groupD8.uX(rotate);
      uvs.y3 = cY + h2 * groupD8.uY(rotate);
    } else {
      uvs.x0 = nX;
      uvs.y0 = nY;
      uvs.x1 = nX + nW;
      uvs.y1 = nY;
      uvs.x2 = nX + nW;
      uvs.y2 = nY + nH;
      uvs.x3 = nX;
      uvs.y3 = nY + nH;
    }
  }
  /**
   * Destroys this texture
   * @param destroySource - Destroy the source when the texture is destroyed.
   */
  destroy(destroySource = false) {
    if (this._source) {
      this._source.off("resize", this.update, this);
      if (destroySource) {
        this._source.destroy();
        this._source = null;
      }
    }
    this._textureMatrix = null;
    this.destroyed = true;
    this.emit("destroy", this);
    this.removeAllListeners();
  }
  /**
   * Call this if you have modified the `texture outside` of the constructor.
   *
   * If you have modified this texture's source, you must separately call `texture.source.update()` to see those changes.
   */
  update() {
    if (this.noFrame) {
      this.frame.width = this._source.width;
      this.frame.height = this._source.height;
    }
    this.updateUvs();
    this.emit("update", this);
  }
  /** @deprecated since 8.0.0 */
  get baseTexture() {
    deprecation(v8_0_0, "Texture.baseTexture is now Texture.source");
    return this._source;
  }
};
Texture.EMPTY = new Texture({
  label: "EMPTY",
  source: new TextureSource({
    label: "EMPTY"
  })
});
Texture.EMPTY.destroy = NOOP;
Texture.WHITE = new Texture({
  source: new BufferImageSource({
    resource: new Uint8Array([255, 255, 255, 255]),
    width: 1,
    height: 1,
    alphaMode: "premultiply-alpha-on-upload",
    label: "WHITE"
  }),
  label: "WHITE"
});
Texture.WHITE.destroy = NOOP;

// node_modules/pixi.js/lib/utils/logging/warn.mjs
var warnCount = 0;
var maxWarnings = 500;
function warn(...args) {
  if (warnCount === maxWarnings) return;
  warnCount++;
  if (warnCount === maxWarnings) {
    console.warn("PixiJS Warning: too many warnings, no more warnings will be reported to the console by PixiJS.");
  } else {
    console.warn("PixiJS Warning: ", ...args);
  }
}

// node_modules/pixi.js/lib/rendering/renderers/gpu/shader/BindGroup.mjs
var BindGroup = class {
  /**
   * Create a new instance of the Bind Group.
   * @param resources - The resources that are bound together for use by a shader.
   */
  constructor(resources) {
    this.resources = /* @__PURE__ */ Object.create(null);
    this._dirty = true;
    let index = 0;
    for (const i in resources) {
      const resource = resources[i];
      this.setResource(resource, index++);
    }
  }
  /**
   * A key used internally to match it up to a WebGPU BindGroup.
   * Lazily rebuilt from resource IDs when dirty.
   * @internal
   */
  get _key() {
    if (this._dirty) {
      this._dirty = false;
      const keyParts = [];
      let index = 0;
      for (const i in this.resources) {
        keyParts[index++] = this.resources[i] ? this.resources[i]._resourceId : -1;
      }
      this._keyValue = keyParts.join("|");
    }
    return this._keyValue;
  }
  /**
   * Set a resource at a given index. This function will
   * ensure that listeners will be removed from the current resource
   * and added to the new resource.
   * @param resource - The resource to set.
   * @param index - The index to set the resource at.
   */
  setResource(resource, index) {
    const currentResource = this.resources[index];
    if (resource === currentResource) return;
    if (currentResource) {
      currentResource.off?.("change", this.onResourceChange, this);
    }
    resource.on?.("change", this.onResourceChange, this);
    this.resources[index] = resource;
    this._dirty = true;
  }
  /**
   * Returns the resource at the current specified index.
   * @param index - The index of the resource to get.
   * @returns - The resource at the specified index.
   */
  getResource(index) {
    return this.resources[index];
  }
  /**
   * Used internally to 'touch' each resource, to ensure that the GC
   * knows that all resources in this bind group are still being used.
   * @param now - The current time in milliseconds.
   * @param tick - The current tick.
   * @internal
   */
  _touch(now, tick) {
    const resources = this.resources;
    for (const i in resources) {
      const resource = resources[i];
      if (!resource) continue;
      resource._gcLastUsed = now;
      resource._touched = tick;
    }
  }
  /** Destroys this bind group and removes all listeners. */
  destroy() {
    const resources = this.resources;
    for (const i in resources) {
      const resource = resources[i];
      resource?.off?.("change", this.onResourceChange, this);
    }
    this.resources = null;
  }
  onResourceChange(resource) {
    this._dirty = true;
    if (resource.destroyed) {
      const resources = this.resources;
      for (const i in resources) {
        if (resources[i] === resource) {
          resources[i] = null;
        }
      }
      warn(`[BindGroup] a '${resource._resourceType}' was destroyed while still bound to a shader. Remove it from the shader before destroying it.`);
    }
  }
};

// node_modules/pixi.js/lib/rendering/renderers/types.mjs
var RendererType = /* @__PURE__ */ ((RendererType2) => {
  RendererType2[RendererType2["WEBGL"] = 1] = "WEBGL";
  RendererType2[RendererType2["WEBGPU"] = 2] = "WEBGPU";
  RendererType2[RendererType2["CANVAS"] = 4] = "CANVAS";
  RendererType2[RendererType2["BOTH"] = 3] = "BOTH";
  return RendererType2;
})(RendererType || {});

// node_modules/pixi.js/lib/rendering/renderers/shared/shader/ShaderOverrides.mjs
var ShaderOverrides = class _ShaderOverrides {
  /**
   * @param data - A dictionary of constants to set on the shader.
   * Keys should match the constant names in the WGSL shader.
   */
  constructor(data) {
    this.data = { ...data };
    const key = Object.keys(data).sort().map((k) => `${k}:${data[k]}`).join("|");
    this.id = createIdFromString(key, "shader-overrides");
  }
  /**
   * Creates a ShaderOverrides instance from a plain object or existing instance.
   * @param overrides - The overrides to convert.
   * @returns A ShaderOverrides instance.
   */
  static from(overrides) {
    if (overrides instanceof _ShaderOverrides) {
      return overrides;
    }
    return new _ShaderOverrides(overrides);
  }
};

// node_modules/pixi.js/lib/rendering/renderers/shared/shader/Shader.mjs
var Shader = class _Shader extends eventemitter3_default {
  constructor(options) {
    super();
    this.uid = uid("shader");
    this._uniformBindMap = /* @__PURE__ */ Object.create(null);
    this._ownedBindGroups = [];
    this._destroyed = false;
    let {
      gpuProgram,
      glProgram,
      groups,
      resources,
      compatibleRenderers,
      groupMap,
      overrides
    } = options;
    this._overrides = overrides ? ShaderOverrides.from(overrides) : null;
    this.gpuProgram = gpuProgram;
    this.glProgram = glProgram;
    if (compatibleRenderers === void 0) {
      compatibleRenderers = 0;
      if (gpuProgram) compatibleRenderers |= RendererType.WEBGPU;
      if (glProgram) compatibleRenderers |= RendererType.WEBGL;
    }
    this.compatibleRenderers = compatibleRenderers;
    const nameHash = {};
    if (groupMap) {
      for (const i in groupMap) {
        for (const j in groupMap[i]) {
          const uniformName = groupMap[i][j];
          nameHash[uniformName] = {
            group: i,
            binding: j,
            name: uniformName
          };
        }
      }
    }
    if (!resources && !groups) {
      resources = {};
    }
    if (resources && groups) {
      throw new Error("[Shader] Cannot have both resources and groups");
    } else if (!gpuProgram && groups && !groupMap) {
      throw new Error("[Shader] No group map or WebGPU shader provided - consider using resources instead.");
    } else if (gpuProgram && groups && !groupMap) {
      const groupData = gpuProgram.structsAndGroups.groups;
      groupMap = {};
      groupData.forEach((data) => {
        groupMap[data.group] = groupMap[data.group] || {};
        groupMap[data.group][data.binding] = data.name;
        nameHash[data.name] = data;
      });
    } else if (resources) {
      groups = {};
      groupMap || (groupMap = {});
      if (gpuProgram) {
        const groupData = gpuProgram.structsAndGroups.groups;
        groupData.forEach((data) => {
          groupMap[data.group] = groupMap[data.group] || {};
          groupMap[data.group][data.binding] = data.name;
          nameHash[data.name] = data;
        });
      }
      let bindTick = 0;
      for (const i in resources) {
        if (nameHash[i]) continue;
        if (gpuProgram && !glProgram) {
          warn(`[Shader] the resource '${i}' matches no binding in the WGSL source \u2014 is the name correct?`);
        }
        if (!groups[99]) {
          groups[99] = new BindGroup();
          this._ownedBindGroups.push(groups[99]);
        }
        nameHash[i] = { group: 99, binding: bindTick, name: i };
        groupMap[99] = groupMap[99] || {};
        groupMap[99][bindTick] = i;
        bindTick++;
      }
      for (const i in resources) {
        const name = i;
        let value = resources[i];
        if (!value.source && !value._resourceType) {
          value = new UniformGroup(value);
        }
        const data = nameHash[name];
        if (data) {
          if (!groups[data.group]) {
            groups[data.group] = new BindGroup();
            this._ownedBindGroups.push(groups[data.group]);
          }
          groups[data.group].setResource(value, data.binding);
        }
      }
    }
    this.groups = groups;
    this._uniformBindMap = groupMap;
    this.resources = this._buildResourceAccessor(groups, nameHash);
  }
  /**
   * Sometimes a resource group will be provided later (for example global uniforms)
   * In such cases, this method can be used to let the shader know about the group.
   * @param name - the name of the resource group
   * @param groupIndex - the index of the group (should match the webGPU shader group location)
   * @param bindIndex - the index of the bind point (should match the webGPU shader bind point)
   */
  addResource(name, groupIndex, bindIndex) {
    var _a, _b;
    (_a = this._uniformBindMap)[groupIndex] || (_a[groupIndex] = {});
    (_b = this._uniformBindMap[groupIndex])[bindIndex] || (_b[bindIndex] = name);
    if (!this.groups[groupIndex]) {
      this.groups[groupIndex] = new BindGroup();
      this._ownedBindGroups.push(this.groups[groupIndex]);
    }
  }
  _buildResourceAccessor(groups, nameHash) {
    const uniformsOut = {};
    for (const i in nameHash) {
      const data = nameHash[i];
      Object.defineProperty(uniformsOut, data.name, {
        get() {
          return groups[data.group].getResource(data.binding);
        },
        set(value) {
          groups[data.group].setResource(value, data.binding);
        }
      });
    }
    return uniformsOut;
  }
  /**
   * Use to destroy the shader when its not longer needed.
   * It will destroy the resources and remove listeners.
   * @param destroyPrograms - if the programs should be destroyed as well.
   * Make sure its not being used by other shaders!
   */
  destroy(destroyPrograms = false) {
    if (this._destroyed) return;
    this._destroyed = true;
    this.emit("destroy", this);
    if (destroyPrograms) {
      this.gpuProgram?.destroy();
      this.glProgram?.destroy();
    }
    this.gpuProgram = null;
    this.glProgram = null;
    this.removeAllListeners();
    this._uniformBindMap = null;
    this._ownedBindGroups.forEach((bindGroup) => {
      bindGroup.destroy();
    });
    this._ownedBindGroups = null;
    this.resources = null;
    this.groups = null;
    this._overrides = null;
  }
  static from(options) {
    const { gpu, gl, ...rest } = options;
    let gpuProgram;
    let glProgram;
    if (gpu) {
      gpuProgram = GpuProgram.from(gpu);
    }
    if (gl) {
      glProgram = GlProgram.from(gl);
    }
    return new _Shader({
      gpuProgram,
      glProgram,
      ...rest
    });
  }
};

export {
  ExtensionType,
  normalizeExtensionPriority,
  extensions,
  createIdFromString,
  BrowserAdapter,
  DOMAdapter,
  getTestContext,
  getMaxFragmentPrecision,
  addProgramDefines,
  ensurePrecision,
  insertVersion,
  setProgramName,
  stripVersion,
  GlProgram,
  getAttributeInfoFromFormat,
  extractAttributesFromGpuProgram,
  extractStructAndGroups,
  ShaderStage,
  generateGpuLayoutGroups,
  generateLayoutHash,
  removeStructAndGroupDuplicates,
  GpuProgram,
  eventemitter3_default,
  uid,
  resetUids,
  UNIFORM_TYPES_VALUES,
  UNIFORM_TYPES_MAP,
  getDefaultUniformValue,
  UniformGroup,
  PI_2,
  RAD_TO_DEG,
  DEG_TO_RAD,
  Point,
  Matrix,
  groupD8,
  Rectangle,
  v8_0_0,
  v8_3_4,
  deprecation,
  NOOP,
  nextPow2,
  isPow2,
  log2,
  definedProps,
  TextureStyle,
  TextureSource,
  BufferImageSource,
  TextureMatrix,
  Texture,
  warn,
  BindGroup,
  RendererType,
  ShaderOverrides,
  Shader
};
