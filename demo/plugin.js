/**
 * Carousel — browser demo.
 *
 * Two Kodak slide projectors on a dissolve unit, as an FFGL mixer (SW
 * Carousel, CA01). The layer below is projector A's slide, this layer
 * projector B's, and the layer's fader is the dissolve fader. Each lamp is a
 * 300 W tungsten filament that follows the dissolve unit's voltage through
 * its thermal lag, so a dissolve sags and turns amber in the middle and even
 * a cut passes through a dim, warm moment; a slide change is the shutter's
 * one-second cycle, and a cold card-mounted slide drifts into focus as it
 * warms. Nothing is drawn.
 *
 * ---------------------------------------------------------------------------
 * What runs here, and what does not
 * ---------------------------------------------------------------------------
 *
 * **This page does not port the plugin. It runs it.** `carousel-core.wasm` is
 * the plugin's own C++ compiled UNMODIFIED with emscripten
 * (demo/tools/build-wasm.sh): `source/Carousel.cpp` — the plugin class, with
 * its constructor, its clock and seconds-or-milliseconds vote, the buttons'
 * latches and every uniform it sets — and everything it calls (Controls,
 * Lamp, Colour with the CIE table in Cmf.h, Clock, Model, BlurBuffer, Diag,
 * Shaders), with the FFGL SDK's CFFGLPlugin, its parameter bookkeeping,
 * FFGLShader and FFGLScreenQuad. The two lamps' filament ODE (RK4 in double),
 * the dissolve unit, the shutters, the slides' heat, the Planck sum and the
 * Gaussian's weights are that C++, on this page's thread. Its GL calls go to
 * this page's WebGL2 context through emscripten's GL library.
 *
 * What is the page's and not the plugin's:
 *
 *   - **The host.** demo/wasm/glue.cpp constructs the plugin (what the SDK's
 *     factory does for a host), reads its parameter declarations back through
 *     the SDK's host getters — the panel below is built from them, not from a
 *     list typed here — and forwards SetFloatParameter, SetTime and
 *     ProcessOpenGL with two inputs, as tools/catest's MixRig does.
 *   - **The two pictures.** The kit hands a demo one input; a mixer takes two.
 *     Dest (projector A) is the kit's clip, Src (projector B) a second copy of
 *     the kit's clip renderer, the page's. "Next slide on Change" (the tray)
 *     is the page's too: see below.
 *   - **Four GL entry points** (demo/wasm/gl_shim.cpp, which says why for
 *     each): glShaderSource, for the check and the port below; glEnable,
 *     glDisable and glIsEnabled, for GL_PROGRAM_POINT_SIZE.
 *   - **The port.** WebGL2 compiles GLSL ES 3.00. `shaderSource()` below
 *     refuses any text that is not byte for byte one of this page's
 *     assemblies of its copy (shaders.js, held to source/Shaders.cpp by
 *     demo/tools/check_shaders.py), applies the kit's `port()` — the version
 *     line and the ES precision defaults — and checks, on port()'s actual
 *     output, that those are the only changes. A stage that fails either
 *     check is refused, never compiled.
 *   - **The panel's read-outs.** The number beside a slider is Controls.cpp's
 *     conversion, compiled in (glue.cpp's ca_convert); its unit and the
 *     tooltips are this page's words.
 *   - **The note at the top, the caution and the line under the picture.**
 *
 * ---------------------------------------------------------------------------
 * Flash safety
 * ---------------------------------------------------------------------------
 *
 * Carousel does not flash on its own. A Change on the lit projector is one
 * dark gap of about 0.9 s in its one-second cycle, and a press during the
 * cycle is ignored, so a projector goes dark at most once a second (two a
 * second alternating A and B). What can flash is cutting: Take, or the
 * fader, at a Time of 0 to 0.1 s, pressed faster than about five times a
 * second, crosses three flashes a second (measured on the plugin's output,
 * white against black: 5 cuts a second is 3 flashes, 6 is 3.5, 10 is 5.5;
 * two of Resolume's demo clips cross at 6 cuts a second); at a Time of
 * 0.25 s or more even 10 presses a second stayed under. So:
 *
 *   - a short note at the top says so (index.html); nothing is gated, since
 *     nothing flashes unless a visitor makes it;
 *   - nothing on this page presses Take or Change by itself, ever: every
 *     press is a click. The tray changes a picture only during a shutter's
 *     dark gap that a click began;
 *   - every control keeps the plugin's own range and the page starts at the
 *     plugin's own defaults (Time 2 s); the caution under the picture says
 *     when Time is under 0.25 s.
 */

import { mountDemo } from './vendor/demo.js';
import { port, GLError } from './vendor/gl.js';
import { SOURCES, SourceRenderer } from './vendor/sources.js';
import * as S from './shaders.js';
import createCarousel from './carousel-core.js';

//===========================================================================
// FFGL.h's parameter types, by number: what GetParamType returns.
//===========================================================================

const FF_TYPE_BOOLEAN = 0;
const FF_TYPE_EVENT = 1;
const FF_TYPE_STANDARD = 10;
const FF_TYPE_OPTION = 11;
const FF_TYPE_TEXT = 100;

const REPO = 'https://github.com/stoatworks-labs/carousel';

//===========================================================================
// The GLSL: the page's checked copy, assembled as the plugin assembles it
// (ASSEMBLY is read out of AssembleVertex(), AssembleFragment() and InitGL by
// check_shaders.py): the one vertex stage and the two fragment stages.
//===========================================================================

const PAGE_SHADERS = Object.entries(S.ASSEMBLY).map(([name, pieces]) => ({
  name,
  stage: name === 'vertex' ? 'vertex' : 'fragment',
  text: pieces.map((p) => S[p]).join(''),
}));

/** Which stages the compiled plugin handed over, and what became of each. */
const shaderCheck = { matched: new Set(), accepted: 0, refused: 0, portRefused: [] };

const VERSION_LINE = '#version 410 core\n';

/**
 * port()'s output for one of the page's stages, checked line by line against
 * the plugin's text: after `#version 300 es` and the precisions (exactly what
 * port() puts in front of an empty text), every line must be the plugin's,
 * but for the blank line(s) port()'s version pattern takes with the version
 * line, and the word `precise ` off exactly the declarations S.PRECISE lists
 * for that stage (none, for carousel). Null if anything else differs.
 * check_shaders.py's check_port() is the same check, at verify time.
 */
function portChecked(ours) {
  const ported = port(ours.text, { stage: ours.stage });
  const head = port('', { stage: ours.stage });
  if (!ours.text.startsWith(VERSION_LINE) || !ported.startsWith(head)) return null;
  const before = ours.text.slice(VERSION_LINE.length).split('\n');
  while (before.length > 1 && before[0].trim() === '') before.shift();
  const after = ported.slice(head.length).split('\n');
  if (before.length !== after.length) return null;
  const stripped = [];
  for (let i = 0; i < before.length; i += 1) {
    if (before[i] === after[i]) continue;
    const m = /^(\s*)precise (float (\w+) = .*)$/.exec(before[i]);
    if (m && after[i] === m[1] + m[2]) {
      stripped.push(m[3]);
      continue;
    }
    return null;
  }
  const want = S.PRECISE[ours.name] ?? [];
  if (stripped.join(',') !== want.join(',')) return null;
  return ported;
}

/**
 * Called by gl_shim.cpp's glShaderSource with the text the compiled plugin
 * handed to GL. Anything but one of this page's own assemblies, or a port
 * that changed more than it may, is refused (an empty source, so the
 * plugin's InitGL fails and logs which pass) rather than compiled.
 */
function shaderSource(stage, text) {
  const ours = PAGE_SHADERS.find((s) => s.stage === stage && s.text === text);
  if (!ours) {
    shaderCheck.refused += 1;
    return '';
  }
  const ported = portChecked(ours);
  if (!ported) {
    shaderCheck.portRefused.push(ours.name);
    return '';
  }
  shaderCheck.matched.add(ours.name);
  shaderCheck.accepted += 1;
  return ported;
}

//===========================================================================
// The module.
//===========================================================================

const printed = [];
let core;
try {
  core = await createCarousel({
    carouselShaderSource: shaderSource,
    print: (text) => { printed.push(text); console.log(text); },
    printErr: (text) => { printed.push(text); console.warn(text); },
  });
} catch (error) {
  const root = document.querySelector('#demo');
  root.textContent = `The demo could not start: this browser would not run its WebAssembly (${error.message}). The plugin, its source and its downloads are at ${REPO}.`;
  throw error;
}

const text = (pointer) => core.UTF8ToString(pointer);

// The plugin's log (Diag.cpp) opens with the first instance, which reading
// the declarations makes: give it a directory in this page's memory first.
core._ca_prepare_log();

//===========================================================================
// The declarations, read from the plugin's constructor through the SDK.
//===========================================================================

function declarations() {
  const h = core._ca_new();
  const list = [];
  const n = core._ca_param_count(h);
  for (let i = 0; i < n; i += 1) {
    const type = core._ca_param_type(h, i);
    const d = { index: i, name: text(core._ca_param_name(h, i)), type, group: text(core._ca_param_group(h, i)) };
    d.default = type === FF_TYPE_TEXT ? null : core._ca_param_default(h, i);
    if (type === FF_TYPE_OPTION) {
      d.elements = [];
      d.values = [];
      for (let e = 0; e < core._ca_param_element_count(h, i); e += 1) {
        d.elements.push(text(core._ca_param_element_name(h, i, e)));
        d.values.push(core._ca_param_element_value(h, i, e));
      }
    }
    list.push(d);
  }
  const inputs = [core._ca_min_inputs(h), core._ca_max_inputs(h)];
  core._ca_delete(h);
  return { list, inputs };
}

const DECLARED = declarations();

//===========================================================================
// The read-outs: Controls.cpp's number, this page's unit.
//===========================================================================

const UNITS = {
  Opacity: (x) => `${x.toFixed(2)}${x <= 0 ? ', A' : x >= 1 ? ', B' : ''}`,
  Time: (x) => (x <= 0 ? '0 s, a cut' : `${x.toFixed(x < 1 ? 2 : 1)} s`),
  Pop: (x) => `${x.toFixed(2)} mm`,
};

/** Controls.cpp's conversion of a host value, or NaN where there is none. */
const convert = (d, v) => core._ca_convert(d.index, Math.fround(v));

function readout(d) {
  return (v) => {
    const x = convert(d, v);
    if (Number.isNaN(x)) return v.toFixed(2);
    return (UNITS[d.name] ?? ((y) => y.toFixed(3)))(x);
  };
}

/** Tooltips: this page's words, from the README's description of each control. */
const HINTS = {
  'Linear Light': 'On, the two projectors’ light adds in linear light, as on a screen; off, it adds code values. Resolume Arena hides a mixer’s first parameter, so there it stays on.',
  Opacity: 'The dissolve fader: 0 is projector A (Slide A, the layer below), 1 projector B (Slide B, this layer). In Resolume Arena the layer’s fader is bound to it, so a layer transition is a dissolve. The lamps follow it at the dissolve unit’s rate, Time.',
  Take: 'A button: swaps which projector the fader means, so one press dissolves to the other projector in Time. FF_TYPE_EVENT: the button sends the press and the release. At a Time under 0.25 s, pressing it quickly cuts back and forth: see the note at the top.',
  Time: 'The dissolve unit’s rate: 0 to 16 s for a full travel (2 s, the default). At 0 it cuts, and even a cut passes through a dim, warm moment while one filament cools and the other heats.',
  Curve: 'Voltage: the plain rheostat, each lamp’s voltage linear in the fader, so the middle of a 2 s dissolve sags to 0.11 of one lamp’s light, at 2457 K. Light: the shaped unit, each lamp’s light linear, so the two lamps give one lamp’s light between them at the midpoint, but at 3077 K: still warmer.',
  Lamp: 'High, or Low: the Ektagraphic’s LO position, 87% of the voltage, dimmer and warmer.',
  Mounts: 'Card mounts pop: a cold slide sits out of focus and drifts in as it warms in the beam. Glass mounts hold the film flat, so a changed slide is sharp at once.',
  Pop: 'How far a warm card-mounted slide bows, 0 to 1 mm (0.55 mm, measured on Resolume’s demo clips). The lens is focused on a warm slide, so a freshly changed, cold one opens this far out of focus.',
  'Change A': 'A slide change on projector A: the shutter’s one-second cycle, then a cold slide drifting into focus. On the dark projector nobody sees the gap. A press during a cycle is ignored, as on a real projector. FF_TYPE_EVENT: the button sends the press and the release.',
  'Change B': 'A slide change on projector B: the shutter’s one-second cycle, then a cold slide drifting into focus. On the dark projector nobody sees the gap. A press during a cycle is ignored, as on a real projector. FF_TYPE_EVENT: the button sends the press and the release.',
};

//===========================================================================
// Kit parameters from the declarations. The id is the name as Arena
// addresses it (lower case, spaces removed).
//===========================================================================

const KIT_TYPES = {
  [FF_TYPE_STANDARD]: 'standard',
  [FF_TYPE_OPTION]: 'option',
  [FF_TYPE_BOOLEAN]: 'boolean',
  // The kit has no button: a boolean row, its toggle replaced by a button
  // that sends the press and the release (below).
  [FF_TYPE_EVENT]: 'boolean',
};

/** The kit reads these from the query string itself; slideb is the page's. */
const RESERVED_QUERY = new Set(['embed', 'size', 'clip', 'bg', 'slideb']);
const addressOf = (name) => {
  const address = name.toLowerCase().replace(/\s+/g, '');
  return RESERVED_QUERY.has(address) ? `param-${address}` : address;
};

// The About block (a text line and its buttons) is the plugin's, declared
// last, and left off the panel as on every page in the suite.
const onPanel = (d) => d.group !== 'About' && KIT_TYPES[d.type] !== undefined;

function kitDefault(d) {
  switch (d.type) {
    case FF_TYPE_OPTION: {
      const i = d.values.findIndex((v) => v === d.default);
      return i >= 0 ? i : Math.round(d.default);
    }
    case FF_TYPE_BOOLEAN: return d.default > 0.5 ? 1 : 0;
    case FF_TYPE_EVENT: return 0;
    default: return d.default;
  }
}

const PARAMS = DECLARED.list.filter(onPanel).map((d) => {
  const p = { id: addressOf(d.name), name: d.name, type: KIT_TYPES[d.type], group: d.group, hint: HINTS[d.name], ff: d };
  if (d.type === FF_TYPE_OPTION) p.elements = d.elements;
  if (d.type === FF_TYPE_STANDARD) p.display = readout(d);
  p.default = kitDefault(d);
  return p;
});

const byName = (name) => PARAMS.find((p) => p.name === name);

/** The value the host would send for a kit control, or null for none. */
function hostValue(d, v) {
  switch (d.type) {
    case FF_TYPE_OPTION: return Math.fround(d.values[Math.round(v)] ?? 0);
    case FF_TYPE_BOOLEAN: return v > 0.5 ? 1 : 0;
    case FF_TYPE_EVENT: return null;
    default: return Math.fround(v);
  }
}

/** Controls.cpp's inverse (ParamForTime, ParamForPop), for the presets. */
const hostFor = (name, x) => core._ca_param_from(byName(name).ff.index, x);
const optionIndex = (name, element) => byName(name).elements.indexOf(element);

//===========================================================================
// The pictures. Both are the kit's generated clips (or a picture of your
// own); which one each projector shows is the page's choice, never the
// plugin's. The kit's shape-on-transparency clip is left off (see the
// notes at the foot).
//===========================================================================

// The order of the page's tray. The first is Slide A's first picture (the
// kit's ?clip= picks another), the second Slide B's (?slideb=).
const TRAY = ['grid', 'scene', 'bars', 'ramp', 'detail', 'spot'];
const SLIDE_B_DEFAULT = 'scene';
const clipName = (id) => SOURCES.find((s) => s.id === id)?.name ?? id;

const query = new URLSearchParams(window.location.search);
const embed = query.has('embed') && query.get('embed') !== '0';

/** The page's own state: what projector B shows, and the tray. */
const page = {
  slideB: TRAY.includes(query.get('slideb')) ? query.get('slideb') : SLIDE_B_DEFAULT,
  tray: query.get('tray') !== '0',
  // A tray move waiting for the shutter to shut, per projector.
  armed: [false, false],
  moves: 0,
};

//===========================================================================
// The instant: what the plugin's last frame rendered (glue.cpp, ca_instant).
//===========================================================================

function readInstant(instance) {
  const at = core._ca_instant(instance) >> 3;
  const v = core.HEAPF64.subarray(at, at + 23);
  const projector = (p) => {
    const q = 3 + 10 * p;
    return {
      kelvin: v[q], drive: v[q + 1], shutter: v[q + 2], heat: v[q + 3], sigmaPx: v[q + 4],
      cycling: v[q + 5] === 1, gain: [v[q + 6], v[q + 7], v[q + 8]], light: v[q + 9],
    };
  };
  return { position: v[0], target: v[1], clock: v[2], p: [projector(0), projector(1)] };
}

//===========================================================================
// The host: one plugin instance, driven as a host drives a mixer.
//===========================================================================

/** A kit texture under a GL name the plugin can bind, in emscripten's tables. */
class GLNames {
  constructor() {
    this.slots = new Map();
  }

  name(slot, texture) {
    const held = this.slots.get(slot);
    if (held && held.texture === texture) return held.name;
    if (held) core.GL.textures[held.name] = null;
    const name = core.GL.getNewId(core.GL.textures);
    core.GL.textures[name] = texture;
    texture.name = name;
    this.slots.set(slot, { texture, name });
    return name;
  }

  release() {
    for (const { name } of this.slots.values()) core.GL.textures[name] = null;
    this.slots.clear();
  }
}

let renderer = null;
let mounted = null;

function createRenderer(gl, quad) {
  // The blur buffers are RGBA32F render targets (BlurBuffer.cpp). WebGL2
  // renders into a float texture only with this extension; the kit is not
  // asked to enable it (needFloat) so that the refusal can say why here.
  if (!gl.getExtension('EXT_color_buffer_float')) {
    throw new GLError('This browser cannot render to a 32-bit float texture (EXT_color_buffer_float is missing). The plugin blurs an out-of-focus slide through two RGBA32F buffers; an 8-bit stand-in would band the blur and give a plausible wrong picture, so the demo stops here. Desktop Chrome, Edge, Firefox and Safari have it.');
  }
  // emscripten's GL library, pointed at the kit's context. Nothing is turned
  // on behind the kit's back: no extensions are enabled on its behalf.
  const handle = core.GL.registerContext(gl, { majorVersion: 2, minorVersion: 0, enableExtensionsByDefault: false });
  core.GL.makeContextCurrent(handle);

  // Src, projector B: a second copy of the kit's clip renderer, the page's.
  const sourcesB = new SourceRenderer(gl, quad);
  const names = new GLNames();

  let instance = 0;
  let size = [0, 0];
  let pushed = new Map();
  let frames = 0;
  let presses = 0;
  let last = null;

  function start(width, height) {
    instance = core._ca_new();
    pushed = new Map();
    frames = 0;
    size = [width, height];
    if (!core._ca_init_gl(instance, width, height)) {
      let why;
      if (shaderCheck.refused > 0) why = 'the GLSL the compiled plugin handed to GL is not this page’s checked copy of source/Shaders.cpp, so it was not compiled';
      else if (shaderCheck.portRefused.length) why = `the kit’s port() changed ${shaderCheck.portRefused.join(', ')} beyond the version line and the precisions, so it was not compiled`;
      else why = `the plugin’s InitGL failed${printed.length ? `: ${printed.slice(-3).join(' ')}` : ''}`;
      throw new GLError(`SW Carousel could not start: ${why}.`);
    }
  }

  function push(params) {
    for (const p of PARAMS) {
      const value = hostValue(p.ff, params.get(p.id));
      if (value === null || pushed.get(p.id) === value) continue;
      pushed.set(p.id, value);
      core._ca_set_float(instance, p.ff.index, value);
    }
  }

  /** The tray (the page's): a projector's picture moves on while its shutter is shut. */
  function trayStep(instant) {
    for (let p = 0; p < 2; p += 1) {
      if (!page.armed[p]) continue;
      const o = instant.p[p];
      if (!o.cycling) {
        page.armed[p] = false;// the cycle ended (or never began) without the tray
        continue;
      }
      if (o.shutter > 0) continue;
      page.armed[p] = false;
      if (page.tray) advance(p);
    }
  }

  return {
    render({ input, params, width, height, time }) {
      core.GL.makeContextCurrent(handle);
      if (!instance) start(width, height);
      else if (size[0] !== width || size[1] !== height) {
        // FF_RESIZE, as a host sends it: the projectors keep their state.
        core._ca_resize(instance, width, height);
        size = [width, height];
      }
      push(params);

      // Src, projector B, at the output's size (or your own, at its own).
      const b = page.slideB === 'user' && sourcesB.userTexture
        ? sourcesB.render('user', width, height, time)
        : sourcesB.render(SOURCES.find((s) => s.id === page.slideB) ?? SOURCES[0], width, height, time);
      gl.bindFramebuffer(gl.FRAMEBUFFER, null);
      gl.viewport(0, 0, width, height);
      gl.disable(gl.BLEND);

      const dest = names.name('dest', input.texture);
      const src = names.name('src', b.texture);
      if (!core._ca_process(instance, time, dest, input.width, input.height, src, b.width, b.height, 0)) {
        throw new GLError(`The plugin’s ProcessOpenGL failed${printed.length ? `: ${printed.slice(-2).join(' ')}` : ''}.`);
      }
      last = readInstant(instance);
      trayStep(last);

      // For anything checking the page from outside.
      frames += 1;
      const canvas = gl.canvas;
      canvas.dataset.frames = String(frames);
      canvas.dataset.shaders = String(shaderCheck.accepted);
      canvas.dataset.clock = String(last.clock);
      canvas.dataset.presses = String(presses);
    },

    /** FF_TYPE_EVENT: the press and the release, as the harness's Press() and Release() send them. */
    press(name) {
      const p = byName(name);
      if (!instance || !p) return;
      // The tray waits for this press's dark gap. A press during a cycle is
      // ignored by the plugin, and so by the tray.
      const which = name === 'Change A' ? 0 : name === 'Change B' ? 1 : -1;
      if (which >= 0 && last && !last.p[which].cycling) page.armed[which] = true;
      core._ca_set_float(instance, p.ff.index, 1);
      core._ca_set_float(instance, p.ff.index, 0);
      presses += 1;
    },

    instant: () => last,
    sourcesB,

    log() {
      try {
        return core.FS.readFile(text(core._ca_log_path()), { encoding: 'utf8' });
      } catch {
        return '';
      }
    },

    /**
     * For demo/tools/crosscheck ONLY: a fresh instance, run frame by frame
     * as catest --pipe runs one — the host clock declared in milliseconds at
     * `fps` from 0 (MixRig), the cue sheet's values set every frame by
     * catest's own rules (valueAt: a standard parameter ramps between keys,
     * anything else steps, and a stepped track says nothing before its first
     * key), Dest and Src the same two unpadded 8-bit pictures every frame —
     * into an RGBA8 framebuffer the page owns, read back top row first.
     */
    sequence({ width, height, dest, src, srcWidth, srcHeight, fps, cues }) {
      core.GL.makeContextCurrent(handle);
      const h = core._ca_new();
      if (!core._ca_init_gl(h, width, height)) throw new Error('InitGL failed in the crosscheck instance');
      core._ca_clock_scale_for_test(h, 0.001);

      const texture = (pixels, w, hh) => {
        const t = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, t);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, w, hh, 0, gl.RGBA, gl.UNSIGNED_BYTE, flip(pixels, w, hh));
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
        gl.bindTexture(gl.TEXTURE_2D, null);
        return t;
      };
      const local = new GLNames();
      const tDest = texture(dest, width, height);
      const tSrc = texture(src, srcWidth, srcHeight);
      const nDest = local.name('dest', tDest);
      const nSrc = local.name('src', tSrc);
      const target = gl.createTexture();
      gl.bindTexture(gl.TEXTURE_2D, target);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, width, height, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
      gl.bindTexture(gl.TEXTURE_2D, null);
      const fbo = gl.createFramebuffer();
      gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
      gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, target, 0);
      const fboName = core.GL.getNewId(core.GL.framebuffers);
      core.GL.framebuffers[fboName] = fbo;
      fbo.name = fboName;

      const tracks = cueTracks(cues);
      const bottomUp = new Uint8Array(width * height * 4);
      let frame = 0;
      const step = (count) => {
        const out = new Uint8Array(count * width * height * 4);
        for (let i = 0; i < count; i += 1, frame += 1) {
          for (const t of tracks) {
            const v = valueAt(t.keys, frame, t.ramps);
            if (!Number.isNaN(v)) core._ca_set_float(h, t.index, v);
          }
          gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
          gl.viewport(0, 0, width, height);
          gl.clearColor(0, 0, 0, 0);
          gl.clear(gl.COLOR_BUFFER_BIT);
          // MixRig: SetTime( seconds / unitScale ), seconds = k / fps.
          if (!core._ca_process(h, (frame / fps) / 0.001, nDest, width, height, nSrc, srcWidth, srcHeight, fboName)) {
            throw new Error(`ProcessOpenGL failed at frame ${frame}`);
          }
          gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
          gl.readPixels(0, 0, width, height, gl.RGBA, gl.UNSIGNED_BYTE, bottomUp);
          out.set(flip(bottomUp, width, height), i * width * height * 4);
        }
        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
        return out;
      };
      const end = () => {
        core._ca_delete(h);
        local.release();
        core.GL.framebuffers[fboName] = null;
        gl.deleteFramebuffer(fbo);
        gl.deleteTexture(target);
        gl.deleteTexture(tDest);
        gl.deleteTexture(tSrc);
        core.GL.makeContextCurrent(handle);
      };
      return { step, end, instant: () => readInstant(h), error: () => gl.getError() };
    },
  };
}

/** Rows reversed: top row first <-> bottom row first. */
function flip(pixels, width, height) {
  const row = width * 4;
  const out = new Uint8Array(pixels.length);
  for (let y = 0; y < height; y += 1) out.set(pixels.subarray(y * row, (y + 1) * row), (height - 1 - y) * row);
  return out;
}

/** catest's loadScript: `frame Name value` lines, '#' a comment, by parameter name. */
function cueTracks(cues) {
  const byParam = new Map();
  for (const raw of cues.split('\n')) {
    const line = raw.replace(/#.*/, '').trim();
    if (!line) continue;
    const words = line.split(/\s+/);
    const frame = Number.parseInt(words[0], 10);
    const value = Math.fround(Number.parseFloat(words[words.length - 1]));
    const name = words.slice(1, -1).join(' ');
    const d = DECLARED.list.find((x) => x.name === name);
    if (!d || !Number.isFinite(value) || !(frame >= 0)) throw new Error(`cue line not understood: ${raw}`);
    if (!byParam.has(name)) byParam.set(name, { index: d.index, ramps: d.type === FF_TYPE_STANDARD, keys: [] });
    byParam.get(name).keys.push([frame, value]);
  }
  const tracks = [...byParam.values()];
  for (const t of tracks) t.keys.sort((a, b) => a[0] - b[0] || a[1] - b[1]);
  return tracks;
}

/** catest's valueAt, in floats where it works in floats. */
function valueAt(keys, frame, ramps) {
  if (!keys.length) return NaN;
  if (!ramps && frame < keys[0][0]) return NaN;
  if (frame <= keys[0][0]) return keys[0][1];
  if (frame >= keys[keys.length - 1][0]) return keys[keys.length - 1][1];
  for (let i = 1; i < keys.length; i += 1) {
    if (frame < keys[i][0]) {
      const [fa, va] = keys[i - 1];
      const [fb, vb] = keys[i];
      if (!ramps) return va;
      const f = Math.fround;
      const span = f(fb - fa);
      const t = span > 0 ? f(f(frame - fa) / span) : 1;
      return f(va + f(f(vb - va) * t));
    }
    if (frame === keys[i][0]) return keys[i][1];
  }
  return keys[keys.length - 1][1];
}

//===========================================================================
// The tray: the page's, never the plugin's. With "Next slide on Change" on,
// a projector's picture moves to the next clip on the page's list during the
// dark gap of a change the visitor pressed, the moment the plugin's own
// shutter (read back from the plugin) is fully shut, as an operator would
// trigger the next clip in the dark. A picture of your own stays put.
//===========================================================================

function nextOf(id) {
  const i = TRAY.indexOf(id);
  return TRAY[(i + 1) % TRAY.length];
}

function advance(p) {
  if (p === 0) {
    const state = mounted?.state;
    if (!state || state.sourceId === 'user') return;
    state.sourceId = nextOf(state.sourceId);
    const select = fieldSelect('Slide A');
    if (select) select.value = state.sourceId;
  } else {
    if (page.slideB === 'user') return;
    page.slideB = nextOf(page.slideB);
    const select = fieldSelect('Slide B');
    if (select) select.value = page.slideB;
  }
  page.moves += 1;
}

function fieldSelect(label) {
  return [...document.querySelectorAll('.transport__label')]
    .find((l) => l.textContent === label)?.closest('.transport__field')?.querySelector('select') ?? null;
}

//===========================================================================
// What this page does not reproduce. The kit adds its four.
//===========================================================================

const DIFFERENCES = [
  'This page runs the plugin rather than a port of it. carousel-core.wasm is source/Carousel.cpp — the plugin class: its constructor, its clock and seconds-or-milliseconds vote, the buttons’ latches and every uniform it sets — and everything it calls (Controls, Lamp, Colour, Clock, Model, BlurBuffer, Diag, Shaders), with the FFGL SDK’s CFFGLPlugin, parameter bookkeeping, CFFGLPluginInfo, FFGLShader and FFGLScreenQuad, all compiled unmodified by emscripten (demo/tools/build-wasm.sh). The two filaments’ heat balance (RK4 in double), the dissolve unit, the shutters, the slides’ heat, the Planck sum against the CIE table and the Gaussian’s weights are that C++. Only MixerEntry.cpp, the bundle’s build stamp, is left out; the page constructs the plugin itself, as its harness does. The panel is read back from the plugin’s own declarations through the SDK’s host getters, and the numbers beside the sliders are Controls.cpp’s conversions, compiled in; their units and every tooltip are this page’s.',
  'The CIE 1931 colour-matching functions are compiled into carousel-core.wasm: source/Cmf.h, the table the plugin sums Planck’s law against, generated from CVRL’s copy of the CIE’s data. That data and the table are © International Commission on Illumination (CIE), CIE 015:2018, under the Creative Commons Attribution-ShareAlike 4.0 licence (CC BY-SA 4.0), not MIT like the rest of the code (the repository’s ATTRIBUTIONS.md and tools/data/README.md).',
  'The GLSL is the plugin’s: demo/shaders.js is spliced from source/Shaders.cpp, with the order AssembleVertex() and AssembleFragment() join the pieces in, and the repository’s verify script fails if a character differs. At start-up the page also requires each stage the compiled plugin hands to glShaderSource (the vertex shader twice, the blur and the screen pass) to equal its copy, and refuses to compile anything else. WebGL2 compiles GLSL ES 3.00, not the plugin’s GLSL 4.10, so the kit’s port() then makes two changes, and the page checks on its actual output that they are the only ones (as the verify script does): the version line becomes #version 300 es, and the ES precision defaults (highp) go in front.',
  'GL is emscripten’s WebGL2 library on this page’s context, not a GL 4.1 driver, and four entry points are the page’s (demo/wasm/gl_shim.cpp): glShaderSource, for the check and the port; and glEnable, glDisable and glIsEnabled, because GL_PROGRAM_POINT_SIZE — state the plugin saves and restores each frame, as a plugin in Resolume must — does not exist in WebGL2; it reads as off here, and the plugin draws no points. The blur buffers are RGBA32F render targets, so the page needs EXT_color_buffer_float, and a browser without it is refused rather than handed 8 bits.',
  'Two inputs, both the page’s. The kit this page is built on hands a demo one input; SW Carousel is a mixer. Slide A (Dest, the layer below: projector A) is the kit’s clip picker, renamed, and its “Use my own…” replaces it with a picture of yours, which also sets the composition to that picture’s size. Slide B (Src, this layer: projector B) is a second copy of the kit’s clip renderer, at the output’s size, with its own “Use my own for B…”, kept at its own size so the plugin resamples it. Pictures of yours are decoded in the page and never uploaded. Neither input is padded: Resolume hands a mixer textures larger than the picture, and the plugin’s handling of that (catest --mixer) is never exercised here.',
  '“Next slide on Change” is the page’s, not the plugin’s. The plugin never changes a picture: in Resolume a slide is whatever its layer’s clip is doing (README, Status: the slide is live). With it on, the page moves a projector’s picture to the next clip on its list at the moment the plugin’s own shutter, read back from the plugin, is fully shut after a Change you pressed, as an operator would trigger the next clip in the dark; a press the plugin ignores moves nothing, and a picture of your own stays put. With it off, the same picture comes back, out of focus, and drifts in.',
  'The kit hands over premultiplied colour, and the plugin takes its inputs as straight colour and composites each over black itself (rgb × a). The page’s clips are all opaque, where the two are the same, which is why the kit’s shape-on-transparency clip is not among them; a picture of your own with transparency would come out darker at its semi-transparent edges (rgb × a²) than in a host that hands straight colour. Whether Resolume hands Dest premultiplied is not measured (README, Not established).',
  'Opacity and Linear Light are on the panel, Opacity as a slider. In Resolume Arena a mixer parameter named Opacity is bound to the layer’s fader, and a mixer’s first parameter — Linear Light, which is why it is first and on — is hidden. That was measured on five other Stoatworks mixers in Arena 7.27.1, not on SW Carousel, which has never been loaded into Resolume.',
  'Take, Change A and Change B are FF_TYPE_EVENT; each button sends the press and the release, 1 then 0, as the harness’s Press() and Release() do, and the plugin acts on the press at the next frame. Nothing on this page presses them by itself.',
  'The clock is the page’s: seconds from frame deltas, capped at a tenth of a second a frame, handed to SetTime. The plugin’s own vote against the wall clock settles the unit (seconds, here, within four frames); until it does the plugin runs on the wall clock. Only the difference between readings is used, so: Pause holds the clock still and the projectors stop where they are — the lamps, the dissolve, a shutter mid-cycle and the slides’ heat — and a Take or a Change pressed while paused waits for Play; Restart is the clock jumping back, which the plugin reads as no time passing, so it carries on from where it was rather than starting again; Step moves it 1/60 s. A tab in the background is not drawn, and when it comes back its first frame is capped at 0.1 s, so the lamps do not catch up the time away.',
  'A frame here is one of the display’s refreshes (60 or 120 a second, and whenever the browser gets round to it), not Resolume’s output frame. The plugin integrates continuous time between frames, so the lamps, the shutter and the focus do not depend on the rate, but which instants are drawn does.',
  'WebAssembly is not the Mac’s arm64. It has no fused multiply-add, and its exp, pow and cbrt are musl’s, not Apple’s, so the lamps’ doubles can differ from the plugin built for a Mac in the last digits. What reaches the GPU is rounded to float, as in the plugin, and rasterised by this browser’s GPU through WebGL2, not by a GL 4.1 driver. On 18 November 2026, on an M4 Max, demo/tools/crosscheck ran this page’s code and catest, the plugin’s harness built for the Mac, through the same 480 frames — a dissolve, a change on the lit projector and a second press it ignores, a Light-shaped dissolve, a cut, Lamp Low, Linear Light off and the fader at Time 0, at 320 × 180 with slide B resampled from 240 × 135 — and every frame came out identical to the byte, Chrome’s WebGL2 on Metal against GL 4.1 on the same GPU. Other browsers, other GPUs and the other compositions were not compared.',
  'The line under the picture is not the plugin. It reports the plugin’s own state for the last frame, read through the accessors its harness reads (the dissolve’s position, each filament’s temperature and voltage, the shutter, the slide’s heat and its blur), and the plugin’s own log, kept in this page’s memory. The plugin draws no such thing. The About block (a credit line and its buttons) is declared by the plugin and left off this panel, and the harness’s negative controls are off, as shipped.',
  'The presets are the page’s. The plugin declares none; each is only a combination of the plugin’s own parameters.',
  'The note at the top and the caution under the picture are the page’s. The plugin does not flash on its own; what they warn of is cutting quickly at a short Time, measured on the plugin’s output (white against black, and two of Resolume’s demo clips), not on this page’s picture.',
  'Nothing here is measured. The plugin itself has never been loaded into Resolume on any platform; oxbow, the fleet’s test host, reads its bundle. Its harness, catest, drives the same class headlessly with two inputs and reads the light straight off its output against the harness’s own model of the lamps: every frame of a cut and a dissolve within a derived tolerance (the closest 2.4e-7 in gain), the colour temperature to 0.0005 K, a voltage dissolve’s middle at 0.1139 of one lamp’s light, one projector alone at rest its slide back with 0 values different, 19 wrong versions and 35 one-character mutants caught. That harness, not this page, is the reason to believe the projectors.',
];

//===========================================================================
// The page.
//===========================================================================

mounted = mountDemo({
  name: 'SW Carousel',
  pluginId: 'CA01',
  kind: 'mixer',
  tagline:
    'Two Kodak slide projectors on a dissolve unit. The layer below is projector A’s slide, this layer projector B’s, and the layer’s fader is the dissolve fader. Each lamp is a 300 W tungsten filament that follows the dissolve unit’s voltage through its thermal lag, so a dissolve sags and turns amber in the middle, and even a cut passes through a dim, warm moment. A slide change is the shutter’s one-second cycle, and a cold card-mounted slide drifts into focus as it warms. Nothing is drawn.',
  repo: REPO,
  blurb:
    'It is the plugin’s own C++ — the plugin class, the two lamps and the dissolve in double, the shutters, the slides and its clock — compiled unmodified to WebAssembly, drawing with its own GLSL in WebGL2. SW Carousel is a mixer, so it needs two pictures: Slide A (Dest, the layer below) and Slide B (Src, this layer), both generated in this page or pictures of your own. The host is this page, and in Resolume the dissolve fader would be the layer’s fader; here it is the Opacity slider.',

  // The blur buffers' float targets are asked for in createRenderer, which
  // says why when a browser has none.
  needFloat: false,
  // The output is opaque: a screen is lit, not transparent.
  showBackdrop: false,

  sources: TRAY,
  params: PARAMS,
  presets: {
    'Voltage dissolve, 2 s (the defaults): the middle sags and warms': { curve: optionIndex('Curve', 'Voltage'), time: hostFor('Time', 2) },
    'Light-shaped dissolve: no sag, still warmer midway': { curve: optionIndex('Curve', 'Light'), time: hostFor('Time', 2) },
    'Slow dissolve, 8 s': { time: hostFor('Time', 8) },
    'A cut (Time 0): still a dim, warm moment': { time: 0 },
    'Lamp Low: 87% of the voltage, dimmer and warmer': { lamp: optionIndex('Lamp', 'Low') },
    'Pop 1 mm: a changed slide opens well out of focus': { mounts: optionIndex('Mounts', 'Card'), pop: hostFor('Pop', 1) },
    'Glass mounts: a changed slide is sharp at once': { mounts: optionIndex('Mounts', 'Glass') },
    'Gamma-space arithmetic (Linear Light off)': { linearlight: 0 },
  },
  differences: DIFFERENCES,

  createRenderer: (gl, quad) => {
    renderer = createRenderer(gl, quad);
    return renderer;
  },
});

if (mounted && !embed) {
  buildPictures();
  buildButtons();
  buildStatusLine();
}

//---------------------------------------------------------------------------
// Slide A is the kit's clip picker, renamed for what it feeds; Slide B and
// its own file button, and the tray, are the page's, added beside it.
//---------------------------------------------------------------------------
function buildPictures() {
  const bar = document.querySelector('.transport');
  const clipField = [...document.querySelectorAll('.transport__label')].find((l) => l.textContent === 'Clip')?.closest('.transport__field');
  if (!bar || !clipField) return;
  clipField.querySelector('.transport__label').textContent = 'Slide A';
  clipField.title = 'Dest, the layer below: projector A’s slide. The page’s choice, not the plugin’s.';
  const selectA = clipField.querySelector('select');
  if (selectA) selectA.value = mounted.state.sourceId;

  const fieldB = document.createElement('label');
  fieldB.className = 'transport__field';
  fieldB.title = 'Src, this layer: projector B’s slide. The page’s choice, not the plugin’s.';
  const labelB = document.createElement('span');
  labelB.className = 'transport__label';
  labelB.textContent = 'Slide B';
  const selectB = document.createElement('select');
  selectB.className = 'transport__select';
  for (const id of TRAY) {
    const option = new Option(clipName(id), id);
    option.title = SOURCES.find((s) => s.id === id)?.hint ?? '';
    selectB.append(option);
  }
  selectB.value = page.slideB;
  selectB.addEventListener('change', () => {
    page.slideB = selectB.value;
    mounted.redraw();
  });
  fieldB.append(labelB, selectB);
  clipField.after(fieldB);

  const fileA = document.querySelector('.transport__file');
  if (fileA) {
    fileA.querySelector('.btn').textContent = 'Use my own for A…';
    fileA.title = 'A picture or a video from this machine as Slide A (Dest), at its own size, which becomes the composition’s. It is decoded in the page and never uploaded anywhere.';
    fileA.querySelector('input')?.addEventListener('change', () => {
      if (selectA && !selectA.querySelector('option[value="user"]')) selectA.append(new Option('Your own picture', 'user'));
      // The kit switches to it once it is decoded.
      setTimeout(() => { if (selectA && mounted.state.sourceId === 'user') selectA.value = 'user'; }, 400);
    });
  }
  const fileB = document.createElement('label');
  fileB.className = 'transport__file';
  fileB.title = 'A picture or a video from this machine as Slide B (Src), at its own size: the plugin resamples it to the composition. It is decoded in the page and never uploaded anywhere.';
  const buttonB = document.createElement('span');
  buttonB.className = 'btn';
  buttonB.textContent = 'Use my own for B…';
  const inputB = document.createElement('input');
  inputB.type = 'file';
  inputB.accept = 'image/*,video/*';
  inputB.hidden = true;
  fileB.append(buttonB, inputB);
  (fileA ?? fieldB).after(fileB);
  inputB.addEventListener('change', async () => {
    if (!inputB.files?.length || !renderer) return;
    try {
      await renderer.sourcesB.adopt(inputB.files[0]);
      if (!selectB.querySelector('option[value="user"]')) selectB.append(new Option('Your own picture', 'user'));
      page.slideB = 'user';
      selectB.value = 'user';
      mounted.redraw();
    } catch (error) {
      buttonB.textContent = 'Could not decode it';
      setTimeout(() => { buttonB.textContent = 'Use my own for B…'; }, 2000);
    }
  });

  const tray = document.createElement('label');
  tray.className = 'transport__field ca-tray';
  tray.title = 'The page’s, not the plugin’s: after a Change you press, the page moves that projector to the next picture on its list while the plugin’s shutter is shut, as an operator would trigger the next clip in the dark. Off, the same picture comes back, out of focus, and drifts in.';
  const check = document.createElement('input');
  check.type = 'checkbox';
  check.checked = page.tray;
  check.addEventListener('change', () => { page.tray = check.checked; });
  const trayLabel = document.createElement('span');
  trayLabel.className = 'transport__label';
  trayLabel.textContent = 'Next slide on Change (the page’s)';
  tray.append(check, trayLabel);
  fileB.after(tray);
}

//---------------------------------------------------------------------------
// FF_TYPE_EVENT: Take, Change A and Change B are buttons that send the press
// and the release. Nothing presses them but a click.
//---------------------------------------------------------------------------
function buildButtons() {
  const rowOf = (p) => document.getElementById(`p-${p.id}`)?.closest('.prow')
    ?? [...document.querySelectorAll('.prow')].find((row) => row.querySelector('.prow__name')?.textContent === p.name);
  for (const p of PARAMS) {
    if (p.ff.type !== FF_TYPE_EVENT) continue;
    const row = rowOf(p);
    const toggle = row?.querySelector('.prow__toggle');
    if (!toggle) continue;
    const press = document.createElement('button');
    press.type = 'button';
    press.className = 'prow__toggle ca-press';
    press.textContent = p.name;
    press.title = p.hint ?? '';
    press.dataset.param = p.id;
    press.addEventListener('click', () => {
      renderer?.press(p.name);
      mounted.redraw();
    });
    toggle.replaceWith(press);
  }
}

//---------------------------------------------------------------------------
// The line under the picture: the plugin's own state, through the accessors
// its harness reads, and the caution. It reports; it measures nothing.
//---------------------------------------------------------------------------
function buildStatusLine() {
  const stage = document.querySelector('.stage');
  if (!stage) return;
  const caution = document.createElement('p');
  caution.className = 'stage__status ca-caution';
  caution.id = 'ca-caution';
  caution.setAttribute('role', 'status');
  caution.hidden = true;
  const line = document.createElement('p');
  line.className = 'stage__status ca-status';
  line.id = 'ca-status';
  line.setAttribute('aria-live', 'off');
  const log = document.createElement('p');
  log.className = 'stage__status ca-log';
  log.id = 'ca-log';
  log.setAttribute('aria-live', 'off');
  stage.append(caution, line, log);

  const params = mounted.params;
  const time = byName('Time');
  const updateCaution = () => {
    const seconds = convert(time.ff, params.get(time.id));
    caution.hidden = !(seconds < 0.25);
    caution.textContent = caution.hidden
      ? ''
      : `Time ${UNITS.Time(seconds)}: under 0.25 s, Take (or the fader) cuts rather than dissolves, and pressed faster than about five times a second it flashes more than three times a second, which can trigger seizures in people with photosensitive epilepsy. Nothing on this page presses it for you.`;
  };
  params.addEventListener('change', updateCaution);
  updateCaution();

  const shutter = (o) => (o.shutter >= 1 ? 'shutter open' : o.shutter <= 0 ? 'shutter shut' : `shutter ${(100 * o.shutter).toFixed(0)}% open`);
  const focus = (o) => (o.sigmaPx > 0 ? `${o.sigmaPx.toFixed(2)} px out of focus` : 'in focus');
  const projector = (name, o) => `${name}: ${o.kelvin.toFixed(0)} K at ${(100 * o.drive).toFixed(1)}% of its voltage, light ${o.light.toFixed(3)}${o.cycling ? `, changing (${shutter(o)})` : `, ${shutter(o)}`}, slide ${(100 * o.heat).toFixed(0)}% warm, ${focus(o)}`;

  let lastLog = '';
  setInterval(() => {
    const s = renderer?.instant();
    if (!s) return;
    const unit = s.clock === 1 ? 'seconds' : s.clock === 0.001 ? 'milliseconds' : 'not settled yet';
    const moving = Math.abs(s.target - s.position) > 1e-9;
    line.textContent = `Dissolve unit at ${s.position.toFixed(3)}${moving ? `, going to ${s.target.toFixed(3)}` : ''} (0 is A, 1 is B)`
      + ` · ${projector('A', s.p[0])}`
      + ` · ${projector('B', s.p[1])}`
      + ` · host clock read in ${unit}`
      + (page.moves ? ` · the page’s tray has moved ${page.moves} time${page.moves === 1 ? '' : 's'}` : '');
    const textLog = renderer.log();
    if (textLog === lastLog) return;
    lastLog = textLog;
    const lines = textLog.trim().split('\n').filter(Boolean).map((l) => l.replace(/^\S+ (INFO |WARN |ERROR) carousel: /, '$1 '));
    log.textContent = lines.length ? `The plugin’s own log (Diag.cpp, in this page’s memory):\n${lines.slice(-2).join('\n')}` : '';
  }, 250);
}

//---------------------------------------------------------------------------
// For a headless check, and for demo/tools/crosscheck.
//---------------------------------------------------------------------------
window.carouselDemo = {
  mounted,
  page,
  shaders: shaderCheck,
  inputs: DECLARED.inputs,
  // The panel's table, as `catest --list` prints it (the About block left off).
  table: DECLARED.list.filter((d) => d.group !== 'About').map((d) => ({
    name: d.name,
    kind: { [FF_TYPE_BOOLEAN]: 'boolean', [FF_TYPE_EVENT]: 'event', [FF_TYPE_STANDARD]: 'standard', [FF_TYPE_OPTION]: 'option' }[d.type] ?? String(d.type),
    value: d.default,
    elements: d.elements ?? [],
  })),
  instant: () => renderer?.instant() ?? null,
  press: (name) => { renderer?.press(name); mounted?.redraw(); },
  sequence: (job) => renderer.sequence(job),
};

export { renderer, mounted, PARAMS, DECLARED, shaderCheck };
