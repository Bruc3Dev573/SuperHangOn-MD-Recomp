/*
 * OpenGL 3.3 core presentation (see video.h). GL functions are resolved at
 * run time through SDL, so no GL headers or loader library are needed.
 *
 * Passes: game frame -> [CRT shader into an offscreen target at the chosen
 * processing resolution] -> window, then the UI layer with alpha blending.
 * Wide screen formats: the race picture is first composed at 224 lines from
 * the rebuilt scene (layers, road quads, sprite images) and then treated as
 * the game frame; other frames are drawn 4:3 in the middle.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "video.h"

#ifdef _WIN32
#define GLAPI __stdcall
#else
#define GLAPI
#endif
#ifdef __EMSCRIPTEN__
#define GLSL_VERSION "#version 300 es\nprecision highp float;\nprecision highp int;\n"
#define GL_PIXEL_FORMAT GL_RGBA
#else
#define GLSL_VERSION "#version 330 core\n"
#define GL_PIXEL_FORMAT GL_BGRA
#endif

typedef unsigned int GLenum, GLuint, GLbitfield;
typedef int GLint, GLsizei;
typedef float GLfloat;
typedef unsigned char GLboolean;
typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;

#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_TRIANGLES 0x0004
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_RGBA 0x1908
#define GL_RGBA8 0x8058
#define GL_BGRA 0x80E1
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_INT_8_8_8_8_REV 0x8367
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_FRAMEBUFFER 0x8D40
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_BLEND 0x0BE2
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_ARRAY_BUFFER 0x8892
#define GL_STREAM_DRAW 0x88E4
#define GL_FLOAT 0x1406

static struct {
  void (GLAPI *Viewport)(GLint, GLint, GLsizei, GLsizei);
  void (GLAPI *ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
  void (GLAPI *Clear)(GLbitfield);
  GLuint (GLAPI *CreateShader)(GLenum);
  void (GLAPI *ShaderSource)(GLuint, GLsizei, const GLchar *const *, const GLint *);
  void (GLAPI *CompileShader)(GLuint);
  void (GLAPI *GetShaderiv)(GLuint, GLenum, GLint *);
  void (GLAPI *GetShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
  GLuint (GLAPI *CreateProgram)(void);
  void (GLAPI *AttachShader)(GLuint, GLuint);
  void (GLAPI *LinkProgram)(GLuint);
  void (GLAPI *GetProgramiv)(GLuint, GLenum, GLint *);
  void (GLAPI *GetProgramInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
  void (GLAPI *UseProgram)(GLuint);
  GLint (GLAPI *GetUniformLocation)(GLuint, const GLchar *);
  void (GLAPI *Uniform1i)(GLint, GLint);
  void (GLAPI *Uniform1f)(GLint, GLfloat);
  void (GLAPI *Uniform2f)(GLint, GLfloat, GLfloat);
  void (GLAPI *GenTextures)(GLsizei, GLuint *);
  void (GLAPI *DeleteTextures)(GLsizei, const GLuint *);
  void (GLAPI *BindTexture)(GLenum, GLuint);
  void (GLAPI *ActiveTexture)(GLenum);
  void (GLAPI *TexParameteri)(GLenum, GLenum, GLint);
  void (GLAPI *TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
  void (GLAPI *TexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *);
  void (GLAPI *GenVertexArrays)(GLsizei, GLuint *);
  void (GLAPI *BindVertexArray)(GLuint);
  void (GLAPI *DrawArrays)(GLenum, GLint, GLsizei);
  void (GLAPI *GenFramebuffers)(GLsizei, GLuint *);
  void (GLAPI *BindFramebuffer)(GLenum, GLuint);
  void (GLAPI *FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
  GLenum (GLAPI *CheckFramebufferStatus)(GLenum);
  void (GLAPI *Enable)(GLenum);
  void (GLAPI *Disable)(GLenum);
  void (GLAPI *BlendFunc)(GLenum, GLenum);
  void (GLAPI *ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *);
  void (GLAPI *PixelStorei)(GLenum, GLint);
  void (GLAPI *GenBuffers)(GLsizei, GLuint *);
  void (GLAPI *BindBuffer)(GLenum, GLuint);
  void (GLAPI *BufferData)(GLenum, GLsizeiptr, const void *, GLenum);
  void (GLAPI *VertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *);
  void (GLAPI *EnableVertexAttribArray)(GLuint);
} gl;

static int capture_wanted;
static void capture(int dw, int dh);

static SDL_Window *win;
static SDL_GLContext ctx;
static GLuint vao, prog_crt, prog_blit, tex_game, tex_ui, tex_fbo, fbo;
static int game_tex_w, game_tex_h, ui_tex_w, ui_tex_h, fbo_w, fbo_h;
/* wide picture composed into tex_scene; sprite images packed in rows into
 * tex_atlas (by SceneImage id) */
static GLuint prog_flat, prog_sprite, vao_quads, vbo_quads, vao_sprites, vbo_sprites;
static GLuint tex_back, tex_plane_lo, tex_plane_hi, tex_scene, fbo_scene, tex_atlas;
static int scene_w = -1;
#define ATLAS_SIZE 2048
#define ATLAS_PAD 2
#define ATLAS_MAX 512
static struct { int id, x, y; } atlas[ATLAS_MAX];
#ifdef __EMSCRIPTEN__
static uint8_t pixel_pack[1024 * 1024 * 4];
static const void *pixel_data(const uint32_t *src, size_t count)
{
  for (size_t i = 0; i < count; i++) {
    uint32_t p = src[i];
    pixel_pack[i * 4 + 0] = (uint8_t)(p >> 16);
    pixel_pack[i * 4 + 1] = (uint8_t)(p >> 8);
    pixel_pack[i * 4 + 2] = (uint8_t)p;
    pixel_pack[i * 4 + 3] = (uint8_t)(p >> 24);
  }
  return pixel_pack;
}
#else
static const void *pixel_data(const uint32_t *src, size_t count)
{
  (void)count;
  return src;
}
#endif

static int atlas_count, atlas_x, atlas_y, atlas_row_h;

static const char *vs_src =
  GLSL_VERSION
  "out vec2 vUv;\n"
  "void main() {\n"
  "  vec2 p = vec2((gl_VertexID == 1) ? 3.0 : -1.0, (gl_VertexID == 2) ? 3.0 : -1.0);\n"
  "  vUv = p * 0.5 + 0.5;\n"
  "  gl_Position = vec4(p, 0.0, 1.0);\n"
  "}\n";

/* road quads: pos in pixels of the wide picture (x from viewX0), top row first */
static const char *vs_flat_src =
  GLSL_VERSION
  "layout(location = 0) in vec2 pos;\n"
  "layout(location = 1) in vec3 colour;\n"
  "uniform float viewX0, viewW;\n"
  "out vec3 vColour;\n"
  "void main() {\n"
  "  vColour = colour;\n"
  "  gl_Position = vec4((pos.x - viewX0) / viewW * 2.0 - 1.0, pos.y / 112.0 - 1.0, 0.0, 1.0);\n"
  "}\n";

static const char *fs_flat_src =
  GLSL_VERSION
  "in vec3 vColour;\n"
  "out vec4 fragColor;\n"
  "void main() { fragColor = vec4(vColour, 1.0); }\n";

/* sprite images drawn pixel for pixel: uv in atlas texels */
static const char *vs_sprite_src =
  GLSL_VERSION
  "layout(location = 0) in vec2 pos;\n"
  "layout(location = 1) in vec2 uv;\n"
  "uniform float viewX0, viewW;\n"
  "out vec2 vUv;\n"
  "void main() {\n"
  "  vUv = uv;\n"
  "  gl_Position = vec4((pos.x - viewX0) / viewW * 2.0 - 1.0, pos.y / 112.0 - 1.0, 0.0, 1.0);\n"
  "}\n";

static const char *fs_sprite_src =
  GLSL_VERSION
  "uniform sampler2D atlas;\n"
  "in vec2 vUv;\n"
  "out vec4 fragColor;\n"
  "void main() {\n"
  "  vec4 c = texelFetch(atlas, ivec2(floor(vUv)), 0);\n"
  "  if (c.a < 0.5) discard;\n"
  "  fragColor = c;\n"
  "}\n";

/* CRT / pixel shader: draws the source picture into rectPos/rectSize of the target */
static const char *fs_crt_src =
  GLSL_VERSION
  "uniform sampler2D src;\n"
  "uniform vec2 srcSize, rectPos, rectSize;\n"
  "uniform int crt;\n"
  "uniform float scanlines, maskStrength, maskPx, triadPx, glow, curvature, vignette, sharpness, brightness;\n"
  "out vec4 fragColor;\n"
  "\n"
  "vec3 fetch(vec2 texel) {\n"
  "  vec2 t = clamp(texel, vec2(0.0), srcSize - 1.0);\n"
  "  vec3 c = texture(src, (t + 0.5) / srcSize).rgb;\n"
  "  return c * c;                      /* approximately linear light (gamma 2) */\n"
  "}\n"
  "\n"
  "/* Barrel distortion of a TV tube */\n"
  "vec2 warp(vec2 uv) {\n"
  "  vec2 c = uv * 2.0 - 1.0;\n"
  "  vec2 k = vec2(0.030, 0.042) * curvature;\n"
  "  c *= 1.0 + k * (c.yx * c.yx) * 4.0;\n"
  "  c /= 1.0 + (k.x + k.y) * 2.0;\n"
  "  return c * 0.5 + 0.5;\n"
  "}\n"
  "\n"
  "/* coverage of a phosphor band of width 1/3 centred at c in [0,1) by a pixel of\n"
  " * width soft (triad units) */\n"
  "float band(float t, float c, float soft) {\n"
  "  float d = abs(fract(t - c + 0.5) - 0.5);\n"
  "  return clamp((1.0 / 6.0 + soft * 0.5 - d) / soft, 0.0, 1.0);\n"
  "}\n"
  "\n"
  "/* Phosphor masks in target pixel units; values average about 1. triadPx = 0:\n"
  " * whole-pixel phosphor columns (maskPx wide); else a continuous pattern with\n"
  " * triads of triadPx pixels (TVL setting) */\n"
  "vec3 mask(vec2 frag) {\n"
  "  float lo = 1.0 - maskStrength, hi = 1.0 + maskStrength * 2.0;\n"
  "  if (crt < 2) return vec3(1.0);\n"
  "  if (triadPx <= 0.0) {\n"
  "    float s = maskPx;\n"
  "    vec3 m = vec3(lo);\n"
  "    if (crt == 2) {                  /* aperture grille: continuous RGB stripes */\n"
  "      int i = int(mod(floor(frag.x / s), 3.0));\n"
  "      m[i] = hi;\n"
  "    } else if (crt == 3) {           /* slot mask: stripes broken by staggered gaps */\n"
  "      float triad = floor(frag.x / (3.0 * s));\n"
  "      int i = int(mod(floor(frag.x / s), 3.0));\n"
  "      float row = mod(frag.y + mod(triad, 2.0) * 2.0 * s, 4.0 * s);\n"
  "      if (row >= s) m[i] = hi;\n"
  "    } else {                         /* shadow mask: delta arrangement of dots */\n"
  "      float bandy = floor(frag.y / (2.0 * s));\n"
  "      float x = frag.x + mod(bandy, 2.0) * 1.5 * s;\n"
  "      int i = int(mod(floor(x / s), 3.0));\n"
  "      float dy = mod(frag.y, 2.0 * s) / (2.0 * s) - 0.5;\n"
  "      m[i] = mix(lo, hi, clamp(1.2 - abs(dy) * 1.6, 0.0, 1.0));\n"
  "    }\n"
  "    return m;\n"
  "  }\n"
  "  float P = triadPx, soft = 1.0 / P;\n"
  "  float x = frag.x, gap = 1.0;\n"
  "  if (crt == 3) {                    /* slot mask: slots 4/3 triad tall, staggered */\n"
  "    float tri = floor(x / P);\n"
  "    float ph = fract((frag.y + mod(tri, 2.0) * P * 2.0 / 3.0) / (P * 4.0 / 3.0));\n"
  "    gap = smoothstep(0.0, soft * 0.75, ph) * (1.0 - smoothstep(0.75 - soft * 0.75, 0.75, ph));\n"
  "  } else if (crt == 4) {             /* shadow mask: rows of dots, alternate rows shifted */\n"
  "    float rowh = P;\n"
  "    float r = floor(frag.y / rowh);\n"
  "    x += mod(r, 2.0) * P * 0.5;\n"
  "    float dy = fract(frag.y / rowh) - 0.5;\n"
  "    gap = clamp(1.3 - abs(dy) * 2.2, 0.0, 1.0);\n"
  "  }\n"
  "  float t = x / P;\n"
  "  vec3 cover = vec3(band(t, 1.0 / 6.0, soft), band(t, 0.5, soft), band(t, 5.0 / 6.0, soft)) * gap;\n"
  "  /* keep the average: each channel is lit for 1/3 of the triad times mean(gap) */\n"
  "  float g = crt == 2 ? 1.0 : (crt == 3 ? 0.72 : 0.62);\n"
  "  return mix(vec3(lo), vec3(lo + (hi - lo) / g), cover);\n"
  "}\n"
  "\n"
  "void main() {\n"
  "  vec2 uv = (gl_FragCoord.xy - rectPos) / rectSize;\n"
  "  uv.y = 1.0 - uv.y;\n"
  "  if (crt > 0) uv = warp(uv);\n"
  "  if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) { fragColor = vec4(0.0, 0.0, 0.0, 1.0); return; }\n"
  "  vec2 p = uv * srcSize;\n"
  "  vec3 col;\n"
  "  if (crt == 0) {\n"
  "    /* sharp pixels (linear filtering): the texel centre inside each texel,\n"
  "     * a blend only over the output pixel on a texel edge */\n"
  "    vec2 scale = rectSize / srcSize;\n"
  "    vec2 region = max(0.5 - 0.5 / scale, 0.0);\n"
  "    vec2 d = fract(p) - 0.5;\n"
  "    vec2 t = floor(p) + 0.5 + (d - clamp(d, -region, region)) * scale;\n"
  "    col = texture(src, t / srcSize).rgb;\n"
  "    fragColor = vec4(col, 1.0);\n"
  "    return;\n"
  "  }\n"
  "  /* horizontal: interpolate between neighbours, sharpened */\n"
  "  float x = p.x - 0.5, ix = floor(x), fx = x - ix;\n"
  "  float edge = 0.5 * (1.0 - sharpness);\n"
  "  fx = smoothstep(0.5 - edge - 0.001, 0.5 + edge + 0.001, fx);\n"
  "  float y = p.y - 0.5, iy = floor(y), fy = y - iy;\n"
  "  vec3 r0 = mix(fetch(vec2(ix, iy)), fetch(vec2(ix + 1.0, iy)), fx);\n"
  "  vec3 r1 = mix(fetch(vec2(ix, iy + 1.0)), fetch(vec2(ix + 1.0, iy + 1.0)), fx);\n"
  "  /* vertical: scanline beams, brighter lines are wider */\n"
  "  vec3 w0 = mix(vec3(0.30), vec3(0.55), sqrt(r0));\n"
  "  vec3 w1 = mix(vec3(0.30), vec3(0.55), sqrt(r1));\n"
  "  vec3 b0 = exp(-(fy * fy) / (w0 * w0));\n"
  "  vec3 b1 = exp(-((1.0 - fy) * (1.0 - fy)) / (w1 * w1));\n"
  "  /* same average light as a flat picture: a beam of width w carries sqrt(pi) w */\n"
  "  vec3 beams = (r0 * b0 + r1 * b1) / (1.7725 * mix(w0, w1, fy));\n"
  "  vec3 plain = mix(r0, r1, fy);\n"
  "  col = mix(plain, beams, scanlines);\n"
  "  /* glow: wide blur of the picture added on top */\n"
  "  if (glow > 0.0) {\n"
  "    vec3 g = vec3(0.0);\n"
  "    for (int dy = -2; dy <= 2; dy++)\n"
  "      for (int dx = -2; dx <= 2; dx++)\n"
  "        g += fetch(floor(p) + vec2(dx, dy) * 1.5);\n"
  "    col += g / 25.0 * glow * 0.6;\n"
  "  }\n"
  "  col *= mask(gl_FragCoord.xy) * brightness;\n"
  "  float peak = max(max(col.r, col.g), col.b);\n"
  "  if (peak > 1.0) col /= peak;        /* clip keeping the hue */\n"
  "  if (vignette > 0.0) {\n"
  "    vec2 v = uv * (1.0 - uv.yx);\n"
  "    col *= pow(clamp(v.x * v.y * 16.0, 0.0, 1.0), 0.25 * vignette);\n"
  "  }\n"
  "  fragColor = vec4(sqrt(max(col, 0.0)), 1.0);\n"
  "}\n";

/* texture to rectangle, used for the offscreen target and the UI layer */
static const char *fs_blit_src =
  GLSL_VERSION
  "uniform sampler2D src;\n"
  "uniform vec2 rectPos, rectSize;\n"
  "out vec4 fragColor;\n"
  "void main() {\n"
  "  vec2 uv = (gl_FragCoord.xy - rectPos) / rectSize;\n"
  "  if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) discard;\n"
  "  fragColor = texture(src, uv);\n"
  "}\n";

void video_default_settings(VideoSettings *s)
{
  memset(s, 0, sizeof *s);
  s->render_height = 0;
  s->scale_mode = SCALE_FIT;
  s->aspect_43 = 1;
  s->crt = CRT_OFF;
  s->scanlines = 0.6f;
  s->mask_strength = 0.30f;
  s->glow = 0.10f;
  s->curvature = 0.0f;
  s->vignette = 0.3f;
  s->sharpness = 0.6f;
  s->brightness = 1.10f;
}

static int load_gl(void)
{
#define L(name) if (!(gl.name = SDL_GL_GetProcAddress("gl" #name))) { fprintf(stderr, "video: missing gl" #name "\n"); return -1; }
  L(Viewport) L(ClearColor) L(Clear) L(CreateShader) L(ShaderSource) L(CompileShader) L(GetShaderiv)
  L(GetShaderInfoLog) L(CreateProgram) L(AttachShader) L(LinkProgram) L(GetProgramiv) L(GetProgramInfoLog)
  L(UseProgram) L(GetUniformLocation) L(Uniform1i) L(Uniform1f) L(Uniform2f) L(GenTextures) L(DeleteTextures)
  L(BindTexture) L(ActiveTexture) L(TexParameteri) L(TexImage2D) L(TexSubImage2D) L(GenVertexArrays)
  L(BindVertexArray) L(DrawArrays) L(GenFramebuffers) L(BindFramebuffer) L(FramebufferTexture2D)
  L(CheckFramebufferStatus) L(Enable) L(Disable) L(BlendFunc) L(ReadPixels) L(PixelStorei)
  L(GenBuffers) L(BindBuffer) L(BufferData) L(VertexAttribPointer) L(EnableVertexAttribArray)
#undef L
  return 0;
}

static GLuint shader(GLenum type, const char *src)
{
  GLuint s = gl.CreateShader(type);
  gl.ShaderSource(s, 1, &src, NULL);
  gl.CompileShader(s);
  GLint ok = 0;
  gl.GetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[2048];
    gl.GetShaderInfoLog(s, sizeof log, NULL, log);
    fprintf(stderr, "video: shader: %s\n", log);
    return 0;
  }
  return s;
}

static GLuint program_vs(const char *vs, const char *fs)
{
  GLuint v = shader(GL_VERTEX_SHADER, vs), f = shader(GL_FRAGMENT_SHADER, fs);
  if (!v || !f)
    return 0;
  GLuint p = gl.CreateProgram();
  gl.AttachShader(p, v);
  gl.AttachShader(p, f);
  gl.LinkProgram(p);
  GLint ok = 0;
  gl.GetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[2048];
    gl.GetProgramInfoLog(p, sizeof log, NULL, log);
    fprintf(stderr, "video: link: %s\n", log);
    return 0;
  }
  return p;
}

static GLuint program(const char *fs)
{
  return program_vs(vs_src, fs);
}

static void texture_params(GLint filter)
{
  gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
  gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
  gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

int video_init(SDL_Window *window, int vsync)
{
  win = window;
  ctx = SDL_GL_CreateContext(window);
  if (!ctx) {
    fprintf(stderr, "video: no OpenGL 3.3 context (%s)\n", SDL_GetError());
    return -1;
  }
  if (load_gl() < 0)
    return -1;
  SDL_GL_SetSwapInterval(vsync ? 1 : 0);
  prog_crt = program(fs_crt_src);
  prog_blit = program(fs_blit_src);
  prog_flat = program_vs(vs_flat_src, fs_flat_src);
  prog_sprite = program_vs(vs_sprite_src, fs_sprite_src);
  if (!prog_crt || !prog_blit || !prog_flat || !prog_sprite)
    return -1;
  gl.GenVertexArrays(1, &vao);
  gl.BindVertexArray(vao);
  gl.GenTextures(1, &tex_game);
  gl.GenTextures(1, &tex_ui);
  gl.GenTextures(1, &tex_fbo);
  gl.GenFramebuffers(1, &fbo);
  gl.GenTextures(1, &tex_back);
  gl.GenTextures(1, &tex_plane_lo);
  gl.GenTextures(1, &tex_plane_hi);
  gl.GenTextures(1, &tex_scene);
  gl.GenTextures(1, &tex_atlas);
  gl.GenFramebuffers(1, &fbo_scene);
  gl.GenVertexArrays(1, &vao_quads);
  gl.GenBuffers(1, &vbo_quads);
  gl.BindVertexArray(vao_quads);
  gl.BindBuffer(GL_ARRAY_BUFFER, vbo_quads);
  gl.VertexAttribPointer(0, 2, GL_FLOAT, 0, 5 * sizeof(float), (void *)0);
  gl.VertexAttribPointer(1, 3, GL_FLOAT, 0, 5 * sizeof(float), (void *)(2 * sizeof(float)));
  gl.EnableVertexAttribArray(0);
  gl.EnableVertexAttribArray(1);
  gl.GenVertexArrays(1, &vao_sprites);
  gl.GenBuffers(1, &vbo_sprites);
  gl.BindVertexArray(vao_sprites);
  gl.BindBuffer(GL_ARRAY_BUFFER, vbo_sprites);
  gl.VertexAttribPointer(0, 2, GL_FLOAT, 0, 4 * sizeof(float), (void *)0);
  gl.VertexAttribPointer(1, 2, GL_FLOAT, 0, 4 * sizeof(float), (void *)(2 * sizeof(float)));
  gl.EnableVertexAttribArray(0);
  gl.EnableVertexAttribArray(1);
  gl.BindVertexArray(vao);
  gl.BindTexture(GL_TEXTURE_2D, tex_atlas);
  gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, ATLAS_SIZE, ATLAS_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  texture_params(GL_NEAREST);
  gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
  gl.PixelStorei(GL_PACK_ALIGNMENT, 1);
  return 0;
}

void video_shutdown(void)
{
  if (ctx)
    SDL_GL_DeleteContext(ctx);
  ctx = NULL;
}

int video_wide_ext(const VideoSettings *s)
{
  if (s->screen_format <= FORMAT_4_3 || s->screen_format >= FORMAT_COUNT)
    return 0;
  double pixel = s->aspect_43 ? (4.0 / 3.0) / (320.0 / 224.0) : 1.0;
  double ratio = s->screen_format == FORMAT_16_9 ? 16.0 / 9.0 : 64.0 / 27.0;
  int ext = (int)((224.0 * ratio / pixel - 320.0) / 2.0 + 0.5);
  return ext < 0 ? 0 : ext > (RENDER_WIDE_MAX - 320) / 2 ? (RENDER_WIDE_MAX - 320) / 2 : ext;
}

/* picture rectangle inside a target of tw x th (src_w: 320 + 2 ext for wide
 * pictures) */
static void picture_rect(int tw, int th, int src_w, int src_h, const VideoSettings *s, int *x, int *y, int *w, int *h)
{
  double aspect = s->aspect_43 ? 4.0 / 3.0 : (double)src_w / src_h;
  if (s->aspect_43 && src_w > 320)
    aspect = 4.0 / 3.0 * src_w / 320.0;
  if (s->scale_mode == SCALE_STRETCH) {
    *w = tw;
    *h = th;
  } else {
    double hh = th, ww = hh * aspect;
    if (ww > tw) {
      ww = tw;
      hh = ww / aspect;
    }
    if (s->scale_mode == SCALE_INTEGER && hh >= src_h) {
      hh = floor(hh / src_h) * src_h;
      ww = hh * aspect;
    }
    *w = (int)(ww + 0.5);
    *h = (int)(hh + 0.5);
  }
  *x = (tw - *w) / 2;
  *y = (th - *h) / 2;
}

static void draw_crt(GLuint target, int tw, int th, int rx, int ry, int rw, int rh, GLuint src, int src_w, int src_h,
                     const VideoSettings *s)
{
  gl.BindFramebuffer(GL_FRAMEBUFFER, target);
  gl.Viewport(0, 0, tw, th);
  gl.ClearColor(0, 0, 0, 1);
  gl.Clear(GL_COLOR_BUFFER_BIT);
  gl.UseProgram(prog_crt);
  gl.ActiveTexture(GL_TEXTURE0);
  gl.BindTexture(GL_TEXTURE_2D, src);
  texture_params(GL_LINEAR);
  gl.Uniform1i(gl.GetUniformLocation(prog_crt, "src"), 0);
  gl.Uniform2f(gl.GetUniformLocation(prog_crt, "srcSize"), (float)src_w, (float)src_h);
  gl.Uniform2f(gl.GetUniformLocation(prog_crt, "rectPos"), (float)rx, (float)ry);
  gl.Uniform2f(gl.GetUniformLocation(prog_crt, "rectSize"), (float)rw, (float)rh);
  gl.Uniform1i(gl.GetUniformLocation(prog_crt, "crt"), s->crt);
  gl.Uniform1f(gl.GetUniformLocation(prog_crt, "scanlines"), s->crt == CRT_OFF ? 0.0f : s->scanlines);
  gl.Uniform1f(gl.GetUniformLocation(prog_crt, "maskStrength"), s->crt >= CRT_APERTURE_GRILLE ? s->mask_strength : 0.0f);
  /* one phosphor column per pixel at 1080 lines, scaled with the picture height */
  float mask_px = (float)floor(rh / 1080.0 + 0.5);
  gl.Uniform1f(gl.GetUniformLocation(prog_crt, "maskPx"), mask_px < 1.0f ? 1.0f : mask_px);
  /* TVL: one phosphor triad per TV line across the picture height */
  gl.Uniform1f(gl.GetUniformLocation(prog_crt, "triadPx"), s->mask_tvl > 0 ? (float)rh / (float)s->mask_tvl : 0.0f);
  gl.Uniform1f(gl.GetUniformLocation(prog_crt, "glow"), s->glow);
  gl.Uniform1f(gl.GetUniformLocation(prog_crt, "curvature"), s->curvature);
  gl.Uniform1f(gl.GetUniformLocation(prog_crt, "vignette"), s->vignette);
  gl.Uniform1f(gl.GetUniformLocation(prog_crt, "sharpness"), s->sharpness);
  gl.Uniform1f(gl.GetUniformLocation(prog_crt, "brightness"), s->crt >= CRT_APERTURE_GRILLE ? s->brightness : 1.0f);
  gl.DrawArrays(GL_TRIANGLES, 0, 3);
}

static void blit(GLuint tex, int filter, int wx, int wy, int ww, int wh, int blend)
{
  gl.UseProgram(prog_blit);
  gl.ActiveTexture(GL_TEXTURE0);
  gl.BindTexture(GL_TEXTURE_2D, tex);
  texture_params(filter);
  gl.Uniform1i(gl.GetUniformLocation(prog_blit, "src"), 0);
  gl.Uniform2f(gl.GetUniformLocation(prog_blit, "rectPos"), (float)wx, (float)wy);
  gl.Uniform2f(gl.GetUniformLocation(prog_blit, "rectSize"), (float)ww, (float)wh);
  if (blend) {
    gl.Enable(GL_BLEND);
    gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  }
  gl.DrawArrays(GL_TRIANGLES, 0, 3);
  if (blend)
    gl.Disable(GL_BLEND);
}

/* rows of the wide layers, packed for upload */
static void upload_layer(GLuint tex, const uint32_t *pixels, int w)
{
  static uint32_t packed[RENDER_WIDE_MAX * 224];
  for (int y = 0; y < 224; y++)
    memcpy(packed + y * w, pixels + y * RENDER_WIDE_MAX, (size_t)w * sizeof *pixels);
  gl.BindTexture(GL_TEXTURE_2D, tex);
  gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, 224, 0, GL_PIXEL_FORMAT, GL_UNSIGNED_BYTE, pixel_data(packed, (size_t)w * 224));
  texture_params(GL_NEAREST);
}

/* places the scene's images in the atlas (uploading new ones); rect[i] = x, y */
static int atlas_place(const Scene *scene, int rect[][2])
{
  for (int pass = 0; pass < 2; pass++) {
    int full = 0;
    for (int i = 0; i < scene->nimages && !full; i++) {
      const SceneImage *im = &scene->image[i];
      int k = 0;
      while (k < atlas_count && atlas[k].id != im->id) k++;
      if (k == atlas_count) {
        if (atlas_x + im->w + ATLAS_PAD > ATLAS_SIZE) {
          atlas_x = 0;
          atlas_y += atlas_row_h;
          atlas_row_h = 0;
        }
        if (atlas_count == ATLAS_MAX || atlas_y + im->h + ATLAS_PAD > ATLAS_SIZE || im->w + ATLAS_PAD > ATLAS_SIZE) {
          full = 1;
          break;
        }
        gl.BindTexture(GL_TEXTURE_2D, tex_atlas);
        atlas[k].id = im->id;
        atlas[k].x = atlas_x;
        atlas[k].y = atlas_y;
        gl.TexSubImage2D(GL_TEXTURE_2D, 0, atlas_x, atlas_y, im->w, im->h, GL_PIXEL_FORMAT, GL_UNSIGNED_BYTE,
                         pixel_data(im->pixels, (size_t)im->w * im->h));
        atlas_count++;
        atlas_x += im->w + ATLAS_PAD;
        if (im->h + ATLAS_PAD > atlas_row_h) atlas_row_h = im->h + ATLAS_PAD;
      }
      rect[i][0] = atlas[k].x;
      rect[i][1] = atlas[k].y;
    }
    if (!full)
      return 1;
    atlas_count = atlas_x = atlas_y = atlas_row_h = 0;  /* start again with an empty atlas */
  }
  return 0;
}

/* composes the wide picture into tex_scene (320 + 2 ext columns, 224 lines,
 * top row first) */
static void compose_scene(const VideoWide *wide)
{
  const Scene *scene = wide->scene;
  int width = 320 + 2 * scene->ext;
  if (width != scene_w) {
    gl.BindTexture(GL_TEXTURE_2D, tex_scene);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, 224, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    texture_params(GL_LINEAR);
    gl.BindFramebuffer(GL_FRAMEBUFFER, fbo_scene);
    gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex_scene, 0);
    scene_w = width;
  }
  upload_layer(tex_back, wide->back, width);
  upload_layer(tex_plane_lo, wide->plane_lo, width);
  upload_layer(tex_plane_hi, wide->plane_hi, width);
  gl.BindFramebuffer(GL_FRAMEBUFFER, fbo_scene);
  gl.Viewport(0, 0, width, 224);
  blit(tex_back, GL_NEAREST, 0, 0, width, 224, 0);

  /* road */
  static float *verts;
  static int verts_cap;
  if (verts_cap < scene->nquads) {
    free(verts);
    verts = malloc((size_t)scene->nquads * 6 * 5 * sizeof *verts);
    verts_cap = verts ? scene->nquads : 0;
  }
  if (verts) {
    float *v = verts;
    for (int i = 0; i < scene->nquads; i++) {
      const SceneQuad *q = &scene->quad[i];
      float r = (q->rgb >> 16 & 0xff) / 255.0f, g = (q->rgb >> 8 & 0xff) / 255.0f, b = (q->rgb & 0xff) / 255.0f;
      const float corner[6][2] = {{q->x00, q->y0}, {q->x01, q->y0}, {q->x11, q->y1},
                                  {q->x00, q->y0}, {q->x11, q->y1}, {q->x10, q->y1}};
      for (int k = 0; k < 6; k++) {
        *v++ = corner[k][0]; *v++ = corner[k][1];
        *v++ = r; *v++ = g; *v++ = b;
      }
    }
    gl.UseProgram(prog_flat);
    gl.Uniform1f(gl.GetUniformLocation(prog_flat, "viewX0"), (float)-scene->ext);
    gl.Uniform1f(gl.GetUniformLocation(prog_flat, "viewW"), (float)width);
    gl.BindVertexArray(vao_quads);
    gl.BindBuffer(GL_ARRAY_BUFFER, vbo_quads);
    gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(v - verts) * (GLsizeiptr)sizeof *verts, verts, GL_STREAM_DRAW);
    gl.DrawArrays(GL_TRIANGLES, 0, (GLsizei)((v - verts) / 5));
    gl.BindVertexArray(vao);
  }

  blit(tex_plane_lo, GL_NEAREST, 0, 0, width, 224, 1);

  /* sprites, far to near */
  static int rect[SCENE_MAX_IMAGES][2];
  if (atlas_place(scene, rect)) {
    static float sv[SCENE_MAX_SPRITES * 6 * 4];
    float *v = sv;
    for (int i = 0; i < scene->nsprites; i++) {
      const SceneSprite *sp = &scene->sprite[i];
      const SceneImage *im = &scene->image[sp->image];
      float u0 = (float)rect[sp->image][0], u1 = u0 + im->w;
      float v0 = rect[sp->image][1] + sp->v0 * im->h, v1 = rect[sp->image][1] + sp->v1 * im->h;
      const float c[6][4] = {{sp->x0, sp->y0, u0, v0}, {sp->x1, sp->y0, u1, v0}, {sp->x1, sp->y1, u1, v1},
                             {sp->x0, sp->y0, u0, v0}, {sp->x1, sp->y1, u1, v1}, {sp->x0, sp->y1, u0, v1}};
      for (int k = 0; k < 6; k++) {
        *v++ = c[k][0]; *v++ = c[k][1]; *v++ = c[k][2]; *v++ = c[k][3];
      }
    }
    gl.UseProgram(prog_sprite);
    gl.Uniform1f(gl.GetUniformLocation(prog_sprite, "viewX0"), (float)-scene->ext);
    gl.Uniform1f(gl.GetUniformLocation(prog_sprite, "viewW"), (float)width);
    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindTexture(GL_TEXTURE_2D, tex_atlas);
    gl.Uniform1i(gl.GetUniformLocation(prog_sprite, "atlas"), 0);
    gl.BindVertexArray(vao_sprites);
    gl.BindBuffer(GL_ARRAY_BUFFER, vbo_sprites);
    gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(v - sv) * (GLsizeiptr)sizeof *sv, sv, GL_STREAM_DRAW);
    gl.DrawArrays(GL_TRIANGLES, 0, (GLsizei)((v - sv) / 4));
    gl.BindVertexArray(vao);
  }

  blit(tex_plane_hi, GL_NEAREST, 0, 0, width, 224, 1);
}

void video_present(const uint32_t *frame, int w, int h, int stride, const VideoSettings *s,
                   const VideoWide *wide, const uint32_t *ui, int ui_w, int ui_h)
{
  /* game frame upload (0x00RRGGBB little endian = B, G, R, X bytes) */
  static uint32_t packed[1024 * 1024];
  for (int y = 0; y < h; y++)
    memcpy(packed + y * w, frame + y * stride, (size_t)w * 4);
  gl.BindTexture(GL_TEXTURE_2D, tex_game);
  if (w != game_tex_w || h != game_tex_h) {
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_PIXEL_FORMAT, GL_UNSIGNED_BYTE, NULL);
    game_tex_w = w;
    game_tex_h = h;
  }
  texture_params(GL_NEAREST);
  gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_PIXEL_FORMAT, GL_UNSIGNED_BYTE,
                   pixel_data(packed, (size_t)w * h));

  int dw, dh;
  SDL_GL_GetDrawableSize(win, &dw, &dh);
  int ext = w == 320 && h == 224 ? video_wide_ext(s) : 0;
  int pic_w = 320 + 2 * ext;
  int rx, ry, rw, rh;
  picture_rect(dw, dh, ext ? pic_w : w, h, s, &rx, &ry, &rw, &rh);

  /* the source: the wide picture (composed now, or the last one again) or
   * the game frame, which a wide picture shows in its middle columns */
  GLuint src = tex_game;
  int src_w = w;
  if (ext && wide && wide->scene && wide->scene->ext == ext) {
    compose_scene(wide);
    src = tex_scene;
    src_w = pic_w;
  } else if (ext && wide && !wide->scene && scene_w == pic_w) {
    src = tex_scene;
    src_w = pic_w;
  }
  int middle = ext && src == tex_game;

  if (s->crt != CRT_OFF && s->render_height > 0 && s->render_height != rh) {
    /* CRT processing at the chosen resolution, then scaled to the window */
    int th = s->render_height, tw = (int)((double)rw * th / rh + 0.5);
    if (tw != fbo_w || th != fbo_h) {
      gl.BindTexture(GL_TEXTURE_2D, tex_fbo);
      gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      texture_params(GL_LINEAR);
      gl.BindFramebuffer(GL_FRAMEBUFFER, fbo);
      gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex_fbo, 0);
      if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        fprintf(stderr, "video: offscreen target incomplete\n");
      fbo_w = tw;
      fbo_h = th;
    }
    VideoSettings inner = *s;
    inner.scale_mode = SCALE_STRETCH;
    if (middle)
      draw_crt(fbo, tw, th, (int)((double)tw * ext / pic_w + 0.5), 0, (int)((double)tw * 320 / pic_w + 0.5), th,
               src, src_w, h, &inner);
    else
      draw_crt(fbo, tw, th, 0, 0, tw, th, src, src_w, h, &inner);
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl.Viewport(0, 0, dw, dh);
    gl.ClearColor(0, 0, 0, 1);
    gl.Clear(GL_COLOR_BUFFER_BIT);
    blit(tex_fbo, GL_LINEAR, rx, ry, rw, rh, 0);
  } else if (middle) {
    draw_crt(0, dw, dh, rx + (int)((double)rw * ext / pic_w + 0.5), ry, (int)((double)rw * 320 / pic_w + 0.5), rh,
             src, src_w, h, s);
  } else {
    draw_crt(0, dw, dh, rx, ry, rw, rh, src, src_w, h, s);
  }

  if (ui) {
    gl.BindTexture(GL_TEXTURE_2D, tex_ui);
    if (ui_w != ui_tex_w || ui_h != ui_tex_h) {
      gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, ui_w, ui_h, 0, GL_PIXEL_FORMAT, GL_UNSIGNED_BYTE, NULL);
      ui_tex_w = ui_w;
      ui_tex_h = ui_h;
    }
    texture_params(GL_NEAREST);
    /* UI layer over the picture rectangle; rows are uploaded top first */
    gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, ui_w, ui_h, GL_PIXEL_FORMAT, GL_UNSIGNED_BYTE,
                     pixel_data(ui, (size_t)ui_w * ui_h));
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl.Viewport(0, 0, dw, dh);
    /* over the 4:3 part of a wide picture */
    int ux = rx + (int)((double)rw * ext / pic_w + 0.5), uw = (int)((double)rw * 320 / pic_w + 0.5);
    blit(tex_ui, GL_NEAREST, ux, ry + rh, uw, -rh, 1);
  }
  if (capture_wanted)
    capture(dw, dh);
  SDL_GL_SwapWindow(win);
}

static int capture_ready, capture_w, capture_h;
static uint32_t *capture_buf;

void video_capture_next(void)
{
  capture_wanted = 1;
  capture_ready = 0;
}

const uint32_t *video_captured(int *w, int *h)
{
  if (!capture_ready)
    return NULL;
  capture_ready = 0;
  *w = capture_w;
  *h = capture_h;
  return capture_buf;
}

static void capture(int dw, int dh)
{
  static size_t cap;
  size_t need = (size_t)dw * dh;
  if (need > cap) {
    SDL_free(capture_buf);
    capture_buf = SDL_malloc(need * 4);
    cap = capture_buf ? need : 0;
    if (!capture_buf)
      return;
  }
  gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
  gl.ReadPixels(0, 0, dw, dh, GL_PIXEL_FORMAT, GL_UNSIGNED_BYTE, capture_buf);
  for (int y = 0; y < dh / 2; y++)                  /* bottom row first -> top row first */
    for (int x = 0; x < dw; x++) {
      uint32_t t = capture_buf[y * dw + x];
      capture_buf[y * dw + x] = capture_buf[(dh - 1 - y) * dw + x];
      capture_buf[(dh - 1 - y) * dw + x] = t;
    }
#ifdef __EMSCRIPTEN__
  for (size_t i = 0; i < need; i++) {
    uint8_t *p = (uint8_t *)&capture_buf[i];
    capture_buf[i] = (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
  }
#else
  for (size_t i = 0; i < need; i++)
    capture_buf[i] &= 0xffffff;
#endif
  capture_w = dw;
  capture_h = dh;
  capture_ready = 1;
  capture_wanted = 0;
}
