// The 3D races drawn with OpenGL and a depth buffer (see gl3d.h).
#include "gl3d.h"

#include <SDL3/SDL_opengl.h>
#include <stdlib.h>
#include <string.h>

#define NEAR_Z 0.25f
#define FAR_Z 2500.0f
#define MAX_PICS 1024

typedef char GLchar_;
static GLuint (*pCreateShader)(GLenum);
static void (*pShaderSource)(GLuint, GLsizei, const GLchar_* const*, const GLint*);
static void (*pCompileShader)(GLuint);
static void (*pGetShaderiv)(GLuint, GLenum, GLint*);
static void (*pGetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar_*);
static GLuint (*pCreateProgram)(void);
static void (*pAttachShader)(GLuint, GLuint);
static void (*pBindAttribLocation)(GLuint, GLuint, const GLchar_*);
static void (*pLinkProgram)(GLuint);
static void (*pGetProgramiv)(GLuint, GLenum, GLint*);
static void (*pUseProgram)(GLuint);
static GLint (*pGetUniformLocation)(GLuint, const GLchar_*);
static void (*pUniform2f)(GLint, GLfloat, GLfloat);
static void (*pUniform3f)(GLint, GLfloat, GLfloat, GLfloat);
static void (*pUniform1i)(GLint, GLint);
static void (*pGenVertexArrays)(GLsizei, GLuint*);
static void (*pBindVertexArray)(GLuint);
static void (*pGenBuffers)(GLsizei, GLuint*);
static void (*pBindBuffer)(GLenum, GLuint);
static void (*pBufferData)(GLenum, ptrdiff_t, const void*, GLenum);
static void (*pVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
static void (*pEnableVertexAttribArray)(GLuint);
static void (*pActiveTexture)(GLenum);
static void (*pBlendFuncSeparate)(GLenum, GLenum, GLenum, GLenum);
static void (*pGenTextures)(GLsizei, GLuint*);
static void (*pBindTexture)(GLenum, GLuint);
static void (*pTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
static void (*pTexParameteri)(GLenum, GLenum, GLint);
static void (*pDeleteTextures)(GLsizei, const GLuint*);
static void (*pDrawArrays)(GLenum, GLint, GLsizei);
static void (*pEnable)(GLenum);
static void (*pDisable)(GLenum);
static void (*pDepthFunc)(GLenum);
static void (*pDepthMask)(GLboolean);
static void (*pViewport)(GLint, GLint, GLsizei, GLsizei);
static void (*pScissor)(GLint, GLint, GLsizei, GLsizei);
static void (*pClear)(GLbitfield);
static void (*pClearDepth)(GLdouble);
static void (*pTexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*);
static void (*pPixelStorei)(GLenum, GLint);
static void (*pBufferSubData)(GLenum, ptrdiff_t, ptrdiff_t, const void*);
// Timer queries (core in 3.3), for the frame-time log: the GPU's time per view.
static void (*pGenQueries)(GLsizei, GLuint*);
static void (*pBeginQuery)(GLenum, GLuint);
static void (*pEndQuery)(GLenum);
static void (*pGetQueryObjectiv)(GLuint, GLenum, GLint*);
static void (*pGetQueryObjectui64v)(GLuint, GLenum, uint64_t*);
static void (*pTexParameterf)(GLenum, GLenum, GLfloat);
static void (*pGetFloatv)(GLenum, GLfloat*);
static GLenum (*pGetError)(void);
static void (*pDepthRange)(GLdouble, GLdouble);
static void (*pColorMask)(GLboolean, GLboolean, GLboolean, GLboolean);

static SDL_Window* g_window;
static SDL_GLContext g_ours, g_theirs;
static GLuint g_prog, g_vao, g_vbo, g_vaoSmall, g_vboSmall;
#define SMALL_VBO (256 * 1024)  // bytes: lists up to this go to the small buffer
static GLint g_uView, g_uTex, g_uHasTex, g_uHaze, g_uDoor;
static bool g_matShader;  // the materials' program is in use (else the basic one)
static float g_aniso;     // anisotropic filtering for the mipmapped textures (0: the driver has none)
static TgV3Vertex* g_baked;  // (basic program: the vertices with the materials lit)
static size_t g_bakedCap;
static GLuint g_tex[MAX_PICS];
static uint32_t g_texVer[MAX_PICS];
static int g_texW[MAX_PICS], g_texH[MAX_PICS];
static bool g_ok;
static size_t g_vboCap;  // bytes the vertex buffer has (it grows, never shrinks: no reallocation each frame)
static TgGl3dStats g_stats;
static bool g_perf;
#define QUERIES 8
static GLuint g_query[QUERIES];
static int g_qHead, g_qTail;  // queries begun (head) and read back (tail)
static char g_info[160];   // the driver, for the session's recording
static GLenum g_error;     // the first GL error since tg_gl3d_take_error
static bool g_inPicture;   // between tg_gl3d_begin and tg_gl3d_end
static size_t g_smallUsed;  // bytes of the small vertex buffer this picture has used
// While a mask is up (tg_gl3d_mask): the views' depths go to [MASK_FLOOR, 1], so the mask's 0
// is nearer than anything they draw, their HUD (depth 0 otherwise) too.
#define MASK_FLOOR (1.0 / 1024)
static double g_depthNear;

// The basic shader: vertex colours, a picture, a depth from w (the fallback, see kFragMat). A
// car's texture (uHasTex 2, TgV3Pic.mask: its alpha a tint mask): the paint lit at the vertices
// times its shading, the rest as painted.
// Both programs leave out a screen-door batch's pixels (TgV3Batch.door, uDoor): those whose
// cell of the 4 x 4 ordered dither (TG_V3_BAYER4, by gl_FragCoord: rows from the bottom) is
// below it, as tg_v3_raster does - a rival seen through without blending (a car hiding the
// player's).
static const char* kVert =
    "#version 330 core\n"
    "layout(location=0) in vec2 aPos; layout(location=1) in vec2 aUv; layout(location=2) in vec4 aCol;"
    "layout(location=3) in float aW;\n"
    "uniform vec2 uView; out vec2 vUv; out vec4 vCol;\n"
    "void main() {\n"
    "  vec2 ndc = vec2(aPos.x / uView.x * 2.0 - 1.0, 1.0 - aPos.y / uView.y * 2.0);\n"
    "  vUv = aUv; vCol = aCol.zyxw;\n"
    "  if(aW > 0.0) {\n"
    "    const float n = 0.25, f = 2500.0;\n"
    "    float z = (f + n) / (f - n) - 2.0 * f * n / ((f - n) * aW);\n"
    "    gl_Position = vec4(ndc * aW, z * aW, aW);\n"
    "  } else gl_Position = vec4(ndc, aW < 0.0 ? -1.0 : 0.99999, 1.0);\n"
    "}\n";
static const char* kFrag =
    "#version 330 core\n"
    "in vec2 vUv; in vec4 vCol; uniform sampler2D uTex; uniform int uHasTex; out vec4 oCol;\n"
    "uniform int uDoor;\n"
    "const int kBayer[16] = int[16](0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5);\n"
    "bool left_out() { ivec2 p = ivec2(gl_FragCoord.xy) & 3; return uDoor > 0 && kBayer[p.y * 4 + p.x] < uDoor; }\n"
    "void main() {\n"
    "  if(left_out()) discard;\n"
    "  vec4 c = vCol;\n"
    "  if(uHasTex == 2) {\n"
    "    vec4 t = texture(uTex, vUv).bgra;\n"
    "    c.rgb = mix(t.rgb * 0.85, c.rgb * t.rgb, t.a);\n"
    "  } else if(uHasTex != 0) c *= texture(uTex, vUv).bgra;\n"
    "  if(c.a < 0.02) discard;\n"
    "  oCol = c;\n"
    "}\n";

// The shaded materials (the car models: TgV3Vertex.nrm / eye): lit per pixel. This is
// shade_mat / env_light in src/mod3d/view3d.c line for line (tg_v3_raster draws the same
// for headless renders): change both together. A car's texture (uHasTex 2, TgV3Pic.mask) gives
// the colour before the light: its own, tinted by the vertex's (the paint) where its alpha
// says; see-through glass grows opaque as it turns away (Fresnel); textured paint has a metallic
// flake (paintSpark: paint_spark in view3d.c, the same hash), as in tg_v3_raster.
static const char* kVertMat =
    "#version 330 core\n"
    "layout(location=0) in vec2 aPos; layout(location=1) in vec2 aUv; layout(location=2) in vec4 aCol;"
    "layout(location=3) in float aW; layout(location=4) in vec4 aNrm; layout(location=5) in vec4 aEye;\n"
    "uniform vec2 uView; out vec2 vUv; out vec4 vCol;\n"
    "out vec3 vN; out vec3 vE; out float vAo; out float vFog; flat out int vMat; flat out int vNight;\n"
    "void main() {\n"
    "  vec2 ndc = vec2(aPos.x / uView.x * 2.0 - 1.0, 1.0 - aPos.y / uView.y * 2.0);\n"
    "  vUv = aUv; vCol = aCol.zyxw;\n"
    "  vec4 nb = floor(aNrm * 255.0 + 0.5), eb = floor(aEye * 255.0 + 0.5);\n"
    "  vN = (nb.xyz - 128.0) / 127.0; vE = (eb.xyz - 128.0) / 127.0;\n"
    "  int m = int(nb.w), e = int(eb.w);\n"
    "  vMat = m & 7; vAo = float(m >> 3) / 31.0; vFog = float(e & 127) / 127.0; vNight = e >> 7;\n"
    "  if(aW > 0.0) {\n"
    "    const float n = 0.25, f = 2500.0;\n"
    "    float z = (f + n) / (f - n) - 2.0 * f * n / ((f - n) * aW);\n"
    "    gl_Position = vec4(ndc * aW, z * aW, aW);\n"
    "  } else {\n"
    "    gl_Position = vec4(ndc, aW < 0.0 ? -1.0 : 0.99999, 1.0);\n"
    "    vMat = 0;\n"
    "  }\n"
    "}\n";
static const char* kFragMat =
    "#version 330 core\n"
    "in vec2 vUv; in vec4 vCol; in vec3 vN; in vec3 vE; in float vAo; in float vFog;\n"
    "flat in int vMat; flat in int vNight;\n"
    "uniform sampler2D uTex; uniform int uHasTex; uniform vec3 uHaze; out vec4 oCol;\n"
    "uniform int uDoor;\n"
    "const int kBayer[16] = int[16](0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5);\n"
    "bool left_out() { ivec2 p = ivec2(gl_FragCoord.xy) & 3; return uDoor > 0 && kBayer[p.y * 4 + p.x] < uDoor; }\n"
    "vec3 envLight(float ry, bool night) {\n"
    "  vec3 c;\n"
    "  if(ry >= 0.0) {\n"
    "    float t = sqrt(ry), band = 1.0 - smoothstep(0.0, 0.1, ry);\n"
    "    c = night ? mix(vec3(0.13, 0.15, 0.24), vec3(0.02, 0.03, 0.08), t) : mix(vec3(0.92, 0.95, 1.0), vec3(0.27, 0.47, 0.86), t);\n"
    "    c += vec3(band * (night ? 0.03 : 0.16));\n"
    "  } else {\n"
    "    float t = clamp(-ry * 1.6, 0.0, 1.0), band = 1.0 - smoothstep(0.0, 0.06, -ry);\n"
    "    c = night ? mix(vec3(0.06, 0.06, 0.07), vec3(0.02), t) : mix(vec3(0.66, 0.64, 0.60), vec3(0.17, 0.17, 0.18), t);\n"
    "    c += vec3(band * (night ? 0.02 : 0.12));\n"
    "  }\n"
    "  return c;\n"
    "}\n"
    "vec3 shadeMat(vec3 a, int mat, float ao, bool night, vec3 N, vec3 E, float spark) {\n"
    "  vec3 L = normalize(vec3(0.33, 0.82, 0.46));\n"
    "  float ne = dot(N, E);\n"
    "  if(ne < 0.0) { N = -N; ne = -ne; }\n"
    "  float nl = dot(N, L);\n"
    "  float amb = (0.36 + 0.24 * (0.5 + 0.5 * N.y)) * (0.7 + 0.3 * ao), dif = 0.62 * max(nl, 0.0) * (0.55 + 0.45 * ao);\n"
    "  if(night) { amb *= 0.55; dif *= 0.3; }\n"
    "  float k = amb + dif;\n"
    "  vec3 e = envLight(2.0 * ne * N.y - E.y, night);\n"
    "  float fr = 1.0 - ne, fr3 = fr * fr * fr, fr5 = fr3 * fr * fr;\n"
    "  float nh = max(dot(N, normalize(L + E)), 0.0);\n"
    "  float p8 = nh * nh; p8 *= p8; p8 *= p8;\n"
    "  float p16 = p8 * p8, p32 = p16 * p16, p256 = p32 * p32; p256 *= p256; p256 *= p256;\n"
    "  float sun = night ? 0.25 : 1.0;\n"
    "  vec3 o;\n"
    "  if(mat == 1) {\n"
    "    float F = 0.04 + 0.7 * fr5;\n"
    "    o = a * k * 1.08 * (1.0 - F) + e * (F + 0.03) * ao + vec3((p256 * 2.4 + p32 * 0.12) * sun) + a * p16 * 0.25 * sun;\n"
    "    o += spark * (p16 * 1.5 * sun + e * 0.025) * (0.6 + 0.4 * a);\n"
    "    if(night) o += a * 0.1 + vec3(0.012);\n"
    "  } else if(mat == 2) {\n"
    "    float F = 0.1 + 0.9 * fr5;\n"
    "    o = a * 0.35 * k * (1.0 - F) + e * (F * 0.9 + 0.06) + vec3(p256 * 3.0 * sun);\n"
    "  } else if(mat == 3) {\n"
    "    o = a * k * 0.55 + e * (0.4 + 0.5 * fr5) * (0.5 + 0.5 * a) * ao + vec3(p32 * p8 * 1.8 * sun);\n"
    "  } else if(mat == 4) {\n"
    "    o = a * k * (0.7 + 0.3 * clamp(N.y + 0.7, 0.0, 1.0)) + e * fr5 * 0.12 * ao + vec3(p8 * 0.035 * sun);\n"
    "  } else if(mat == 5) {\n"
    "    o = a * (night ? 1.15 : 1.0) + e * (0.04 + 0.5 * fr5) + vec3(p256 * 2.0 * sun);\n"
    "  } else {\n"
    "    float F = 0.04 + 0.96 * fr5;\n"
    "    o = a * k * (1.0 - F * 0.5) + e * F * 0.35 * ao + vec3(p32 * 0.25 * sun);\n"
    "  }\n"
    "  return min(o, vec3(1.0));\n"
    "}\n"
    "vec3 unit(vec3 v, vec3 dflt) { float l = dot(v, v); return l > 1e-8 ? v * inversesqrt(l) : dflt; }\n"
    "float paintSpark(vec2 uv) {\n"
    "  vec2 q = uv * 1024.0;\n"
    "  float fp = max(length(dFdx(q)), length(dFdy(q)));\n"
    "  float fade = 1.0 - smoothstep(0.5, 1.5, fp);\n"
    "  uint h = uint(q.x) * 1597334677u ^ uint(q.y) * 3812015801u;\n"
    "  h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;\n"
    "  return (h & 255u) < 13u ? fade : 0.0;\n"
    "}\n"
    "void main() {\n"
    "  if(left_out()) discard;\n"
    "  vec4 c = vCol;\n"
    "  float spark = 0.0;\n"
    "  if(uHasTex == 2) {\n"
    "    vec4 t = texture(uTex, vUv).bgra;\n"
    "    c.rgb = t.rgb * mix(vec3(1.0), c.rgb, t.a);\n"
    "    spark = paintSpark(vUv) * t.a;\n"
    "  } else if(uHasTex != 0) c *= texture(uTex, vUv).bgra;\n"
    "  if(c.a < 0.02) discard;\n"
    "  if(vMat != 0) {\n"
    "    vec3 N = unit(vN, vec3(0.0, 1.0, 0.0)), E = unit(vE, vec3(0.0, 0.0, 1.0));\n"
    "    if(vMat == 2 && c.a < 0.998) {\n"
    "      float fr = 1.0 - abs(dot(N, E)), fr2 = fr * fr;\n"
    "      c.a += (1.0 - c.a) * (0.1 + 0.9 * fr2 * fr2 * fr);\n"
    "    }\n"
    "    c.rgb = shadeMat(c.rgb, vMat, vAo, vNight != 0, N, E, vMat == 1 ? spark : 0.0);\n"
    "    c.rgb = mix(c.rgb, uHaze, vFog);\n"
    "  }\n"
    "  oCol = c;\n"
    "}\n";

void tg_gl3d_prepare(void) {
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
  SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
}

static bool load(void) {
#define L(p, name) if(!(*(void**) &p = (void*) SDL_GL_GetProcAddress(name))) return false
  L(pCreateShader, "glCreateShader"); L(pShaderSource, "glShaderSource"); L(pCompileShader, "glCompileShader");
  L(pGetShaderiv, "glGetShaderiv"); L(pGetShaderInfoLog, "glGetShaderInfoLog"); L(pCreateProgram, "glCreateProgram");
  L(pAttachShader, "glAttachShader"); L(pBindAttribLocation, "glBindAttribLocation"); L(pLinkProgram, "glLinkProgram");
  L(pGetProgramiv, "glGetProgramiv"); L(pUseProgram, "glUseProgram"); L(pGetUniformLocation, "glGetUniformLocation");
  L(pUniform2f, "glUniform2f"); L(pUniform3f, "glUniform3f"); L(pUniform1i, "glUniform1i"); L(pGenVertexArrays, "glGenVertexArrays");
  L(pBindVertexArray, "glBindVertexArray"); L(pGenBuffers, "glGenBuffers"); L(pBindBuffer, "glBindBuffer");
  L(pBufferData, "glBufferData"); L(pVertexAttribPointer, "glVertexAttribPointer");
  L(pEnableVertexAttribArray, "glEnableVertexAttribArray"); L(pActiveTexture, "glActiveTexture");
  L(pBlendFuncSeparate, "glBlendFuncSeparate"); L(pGenTextures, "glGenTextures"); L(pBindTexture, "glBindTexture");
  L(pTexImage2D, "glTexImage2D"); L(pTexParameteri, "glTexParameteri"); L(pDeleteTextures, "glDeleteTextures");
  L(pDrawArrays, "glDrawArrays"); L(pEnable, "glEnable"); L(pDisable, "glDisable"); L(pDepthFunc, "glDepthFunc");
  L(pDepthMask, "glDepthMask"); L(pViewport, "glViewport"); L(pScissor, "glScissor"); L(pClear, "glClear");
  L(pClearDepth, "glClearDepth"); L(pTexSubImage2D, "glTexSubImage2D"); L(pPixelStorei, "glPixelStorei");
  L(pBufferSubData, "glBufferSubData");
  L(pGenQueries, "glGenQueries"); L(pBeginQuery, "glBeginQuery"); L(pEndQuery, "glEndQuery");
  L(pGetQueryObjectiv, "glGetQueryObjectiv"); L(pGetQueryObjectui64v, "glGetQueryObjectui64v");
  L(pTexParameterf, "glTexParameterf"); L(pGetFloatv, "glGetFloatv"); L(pGetError, "glGetError");
  L(pDepthRange, "glDepthRange"); L(pColorMask, "glColorMask");
#undef L
  return true;
}

static GLuint shader(GLenum kind, const char* src) {
  GLuint s = pCreateShader(kind);
  pShaderSource(s, 1, &src, NULL);
  pCompileShader(s);
  GLint ok = 0;
  pGetShaderiv(s, 0x8B81 /* GL_COMPILE_STATUS */, &ok);
  if(!ok) {
    char log[1024];
    pGetShaderInfoLog(s, sizeof(log), NULL, log);
    SDL_Log("gl3d shader: %s", log);
    return 0;
  }
  return s;
}

// A linked program from two sources (0 if either won't compile or they won't link).
static GLuint program(const char* vsrc, const char* fsrc) {
  GLuint vs = shader(0x8B31 /* GL_VERTEX_SHADER */, vsrc), fs = shader(0x8B30 /* GL_FRAGMENT_SHADER */, fsrc);
  if(!vs || !fs) return 0;
  GLuint p = pCreateProgram();
  pAttachShader(p, vs);
  pAttachShader(p, fs);
  pLinkProgram(p);
  GLint linked = 0;
  pGetProgramiv(p, 0x8B82 /* GL_LINK_STATUS */, &linked);
  if(!linked) {
    SDL_Log("gl3d: program did not link");
    return 0;
  }
  return p;
}

bool tg_gl3d_init(SDL_Window* window, SDL_Renderer* renderer) {
  const char* name = SDL_GetRendererName(renderer);
  if(!name || strcmp(name, "opengl") != 0) return false;
  g_window = window;
  g_theirs = SDL_GL_GetCurrentContext();
  if(!g_theirs) return false;
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  g_ours = SDL_GL_CreateContext(window);
  if(!g_ours) {
    SDL_GL_MakeCurrent(window, g_theirs);
    return false;
  }
  bool ok = load();
  if(ok) {
    const GLubyte* (*getString)(GLenum) = (const GLubyte* (*)(GLenum)) SDL_GL_GetProcAddress("glGetString");
    void (*getIntegerv)(GLenum, GLint*) = (void (*)(GLenum, GLint*)) SDL_GL_GetProcAddress("glGetIntegerv");
    GLint samples = 0;
    if(getIntegerv) getIntegerv(0x80A9 /* GL_SAMPLES */, &samples);
    const char* r = getString ? (const char*) getString(GL_RENDERER) : NULL;
    const char* ver = getString ? (const char*) getString(GL_VERSION) : NULL;
    SDL_snprintf(g_info, sizeof(g_info), "%s, %s, %dx MSAA", r ? r : "?", ver ? ver : "?", (int) samples);
  }
  // The shaded materials' program; if this driver won't build it, the basic one, and the
  // materials are lit at the vertices on the CPU (tg_v3_vertex_colour) before they are sent.
  g_matShader = ok && (g_prog = program(kVertMat, kFragMat)) != 0;
  if(ok && !g_matShader) {
    SDL_Log("gl3d: material shader unavailable, cars lit per vertex");
    g_prog = program(kVert, kFrag);
  }
  ok = ok && g_prog != 0;
  if(ok) {
    g_uHaze = pGetUniformLocation(g_prog, "uHaze");
    g_uView = pGetUniformLocation(g_prog, "uView");
    g_uTex = pGetUniformLocation(g_prog, "uTex");
    g_uHasTex = pGetUniformLocation(g_prog, "uHasTex");
    g_uDoor = pGetUniformLocation(g_prog, "uDoor");
    // Two vertex buffers, each with its array: the views' (megabytes a picture) and a small one
    // for small lists (the transition's mask and clock), so these don't orphan the big one.
    for(int k = 0; k < 2; k++) {
      GLuint* vao = k ? &g_vaoSmall : &g_vao;
      GLuint* vbo = k ? &g_vboSmall : &g_vbo;
      pGenVertexArrays(1, vao);
      pBindVertexArray(*vao);
      pGenBuffers(1, vbo);
      pBindBuffer(GL_ARRAY_BUFFER, *vbo);
      GLsizei st = sizeof(TgV3Vertex);
      pVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, st, (void*) offsetof(TgV3Vertex, x));
      pVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, st, (void*) offsetof(TgV3Vertex, u));
      pVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, st, (void*) offsetof(TgV3Vertex, argb));
      pVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, st, (void*) offsetof(TgV3Vertex, w));
      pVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, st, (void*) offsetof(TgV3Vertex, nrm));
      pVertexAttribPointer(5, 4, GL_UNSIGNED_BYTE, GL_TRUE, st, (void*) offsetof(TgV3Vertex, eye));
      for(int i = 0; i < (g_matShader ? 6 : 4); i++) pEnableVertexAttribArray((GLuint) i);
    }
    // Anisotropic filtering (EXT / ARB_texture_filter_anisotropic, core in 4.6): the cars'
    // sides and decks are seen at grazing angles. A driver without it leaves an error: none.
    while(pGetError() != 0) {}
    GLfloat mx = 0;
    pGetFloatv(0x84FF /* GL_MAX_TEXTURE_MAX_ANISOTROPY */, &mx);
    if(pGetError() == 0 && mx >= 2) g_aniso = mx < 8 ? mx : 8;
  }
  SDL_GL_MakeCurrent(window, g_theirs);
  g_ok = ok;
  if(!ok) {
    SDL_GL_DestroyContext(g_ours);
    g_ours = NULL;
  }
  return ok;
}

static bool g_texSmooth[MAX_PICS];

// A picture's texture, sent when its pixels changed: only the rows that changed when the
// texture holds the version before (glTexSubImage2D into the storage it has); the whole
// picture when it is new or its size changed.
static GLuint texture_of(const TgView3D* v, int i) {
  const TgV3Pic* p = tg_view3d_pic(v, i);
  if(!p || !p->w || !p->px || i >= MAX_PICS) return 0;
  bool fresh = !g_tex[i] || g_texW[i] != p->w || g_texH[i] != p->h;
  if(fresh || g_texVer[i] != p->version) {
    if(!g_tex[i]) pGenTextures(1, &g_tex[i]);
    pBindTexture(GL_TEXTURE_2D, g_tex[i]);
    pPixelStorei(0x0CF5 /* GL_UNPACK_ALIGNMENT */, 4);
    if(p->mips) {  // a car's texture: its mip levels follow it (TgV3Pic.mips), all sent
      const uint32_t* px = p->px;
      for(int l = 0, lw = p->w, lh = p->h; l <= p->mips; l++) {
        pTexImage2D(GL_TEXTURE_2D, l, GL_RGBA8, lw, lh, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        g_stats.upload_bytes += (size_t) lw * lh * 4;
        px += (size_t) lw * lh;
        lw = lw > 1 ? lw / 2 : 1, lh = lh > 1 ? lh / 2 : 1;
      }
    } else if(fresh) {
      pTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, p->w, p->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, p->px);
      g_stats.upload_bytes += (size_t) p->w * p->h * 4;
    } else {
      bool part = g_texVer[i] == p->dirty_from && p->dirty_from + 1 == p->version && p->dirty_y0 >= 0 &&
                  p->dirty_y1 <= p->h && p->dirty_y0 < p->dirty_y1;
      int y0 = part ? p->dirty_y0 : 0, y1 = part ? p->dirty_y1 : p->h;
      pTexSubImage2D(GL_TEXTURE_2D, 0, 0, y0, p->w, y1 - y0, GL_RGBA, GL_UNSIGNED_BYTE, p->px + (size_t) y0 * p->w);
      g_stats.upload_bytes += (size_t) p->w * (y1 - y0) * 4;
    }
    g_stats.uploads++;
    if(fresh || g_texSmooth[i] != p->smooth) {
      GLint f = p->smooth ? GL_LINEAR : GL_NEAREST;
      pTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, p->mips ? 0x2703 /* GL_LINEAR_MIPMAP_LINEAR */ : f);
      pTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
      if(p->mips) {
        pTexParameteri(GL_TEXTURE_2D, 0x813D /* GL_TEXTURE_MAX_LEVEL */, p->mips);
        if(g_aniso > 0) pTexParameterf(GL_TEXTURE_2D, 0x84FE /* GL_TEXTURE_MAX_ANISOTROPY */, g_aniso);
      }
      pTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F /* GL_CLAMP_TO_EDGE */);
      pTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
      g_texSmooth[i] = p->smooth;
    }
    g_texVer[i] = p->version, g_texW[i] = p->w, g_texH[i] = p->h;
  }
  return g_tex[i];
}

// GPU times of earlier views, read back without waiting (results lag a frame or two).
static void read_queries(void) {
  while(g_qTail < g_qHead) {
    GLuint q = g_query[g_qTail % QUERIES];
    GLint ready = 0;
    pGetQueryObjectiv(q, 0x8867 /* GL_QUERY_RESULT_AVAILABLE */, &ready);
    if(!ready) break;
    uint64_t ns = 0;
    pGetQueryObjectui64v(q, 0x8866 /* GL_QUERY_RESULT */, &ns);
    g_stats.gpu_ms += ns / 1e6;
    g_stats.gpu_views++;
    g_qTail++;
  }
}

void tg_gl3d_set_perf(bool on) { g_perf = on; }

void tg_gl3d_take_stats(TgGl3dStats* out) {
  if(g_ok && g_perf) {
    SDL_GL_MakeCurrent(g_window, g_ours);
    read_queries();
    SDL_GL_MakeCurrent(g_window, g_theirs);
  }
  *out = g_stats;
  memset(&g_stats, 0, sizeof(g_stats));
}

// Every picture the views can draw, sent now (the loading card before a race): the race's
// first frames then send nothing big. At most `budget` bytes a call; true when all are sent.
bool tg_gl3d_warm(const TgView3D* v, size_t budget) {
  if(!g_ok) return true;
  SDL_GL_MakeCurrent(g_window, g_ours);
  size_t before = g_stats.upload_bytes;
  bool all = true;
  int n = tg_view3d_pic_count(v);
  for(int i = 0; i < n && i < MAX_PICS; i++) {
    const TgV3Pic* p = tg_view3d_pic(v, i);
    if(!p || !p->w || !p->px || !tg_view3d_pic_warm(v, i)) continue;
    if(g_tex[i] && g_texVer[i] == p->version && g_texW[i] == p->w && g_texH[i] == p->h) continue;
    if(g_stats.upload_bytes - before >= budget) {
      all = false;
      break;
    }
    texture_of(v, i);
  }
  SDL_GL_MakeCurrent(g_window, g_theirs);
  return all;
}

// A picture of several views: this context made current once, the window's depth cleared whole
// (each view then draws only in its own rectangle), the renderer's context back at the end.
void tg_gl3d_begin(int windowW, int windowH) {
  if(!g_ok || g_inPicture) return;
  SDL_GL_MakeCurrent(g_window, g_ours);
  g_inPicture = true;
  g_smallUsed = 0;
  g_depthNear = 0;
  pDepthRange(0.0, 1.0);
  pDisable(GL_SCISSOR_TEST);
  pViewport(0, 0, windowW, windowH);
  pClearDepth(1.0);
  pDepthMask(GL_TRUE);
  pClear(GL_DEPTH_BUFFER_BIT);
}

// The vertices into a buffer, its vertex array bound for the draws; returns the index of the
// first. The views' buffer grows by half again when too small, else is orphaned and refilled
// (the driver hands a fresh one while the GPU still reads the last frame's). A small list goes
// to the small buffer: orphaned at a picture's first, the picture's next ones after it.
static int upload_vertices(const TgV3Vertex* src, int n) {
  size_t bytes = sizeof(TgV3Vertex) * (size_t) n, at = 0;
  bool small = bytes <= SMALL_VBO;
  pBindVertexArray(small ? g_vaoSmall : g_vao);
  pBindBuffer(GL_ARRAY_BUFFER, small ? g_vboSmall : g_vbo);
  if(small) {
    if(!g_inPicture || g_smallUsed == 0 || g_smallUsed + bytes > SMALL_VBO) {
      pBufferData(GL_ARRAY_BUFFER, SMALL_VBO, NULL, 0x88E0 /* GL_STREAM_DRAW */);
      g_smallUsed = 0;
    }
    at = g_smallUsed;
    g_smallUsed = g_inPicture ? at + bytes : 0;
  } else {
    if(bytes > g_vboCap) g_vboCap = bytes + bytes / 2;
    pBufferData(GL_ARRAY_BUFFER, (ptrdiff_t) g_vboCap, NULL, 0x88E0);
  }
  pBufferSubData(GL_ARRAY_BUFFER, (ptrdiff_t) at, (ptrdiff_t) bytes, src);
  g_stats.vertex_bytes += bytes;
  return (int) (at / sizeof(TgV3Vertex));
}

void tg_gl3d_mask(const TgV3List* keep, int windowW, int windowH) {
  if(!g_ok || !g_inPicture) return;
  pDisable(GL_SCISSOR_TEST);
  pViewport(0, 0, windowW, windowH);
  pUseProgram(g_prog);
  pUniform2f(g_uView, (float) windowW, (float) windowH);
  pUniform1i(g_uHasTex, 0);
  pUniform1i(g_uDoor, 0);
  pDisable(GL_BLEND);
  pEnable(GL_DEPTH_TEST);
  pEnable(0x809D /* GL_MULTISAMPLE */);
  // The part to keep (opaque, over everything: TgV3Vertex.w < 0) as depth 0, no colour.
  if(keep && keep->nv > 0) {
    pDepthRange(0.0, 1.0);
    pDepthFunc(0x0207 /* GL_ALWAYS */);
    pDepthMask(GL_TRUE);
    pColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    pDrawArrays(GL_TRIANGLES, upload_vertices(keep->v, keep->nv), keep->nv);
    g_stats.draws++;
    pColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  }
  // The rest black, as the renderer's clear under a 3D picture (the line between two views),
  // at the background's depth without writing it: the views then start from a clear depth.
  g_depthNear = MASK_FLOOR;
  pDepthRange(g_depthNear, 1.0);
  pDepthFunc(GL_LEQUAL);
  pDepthMask(GL_FALSE);
  float w = (float) windowW, h = (float) windowH;
  TgV3Vertex q[6];
  memset(q, 0, sizeof(q));
  const float xy[6][2] = {{0, 0}, {w, 0}, {w, h}, {0, 0}, {w, h}, {0, h}};
  for(int i = 0; i < 6; i++) q[i].x = xy[i][0], q[i].y = xy[i][1], q[i].argb = 0xFF000000u;  // (w 0: the background)
  pDrawArrays(GL_TRIANGLES, upload_vertices(q, 6), 6);
  g_stats.draws++;
  pDepthMask(GL_TRUE);
  pDisable(GL_DEPTH_TEST);
}

void tg_gl3d_unmask(void) {
  if(!g_ok || !g_inPicture) return;
  g_depthNear = 0;
  pDepthRange(0.0, 1.0);
}

void tg_gl3d_end(void) {
  if(!g_inPicture) return;
  g_inPicture = false;
  if(g_depthNear != 0) {
    g_depthNear = 0;
    pDepthRange(0.0, 1.0);
  }
  if(!g_error) g_error = pGetError();
  SDL_GL_MakeCurrent(g_window, g_theirs);
}

const char* tg_gl3d_info(void) { return g_ok ? g_info : ""; }

unsigned tg_gl3d_take_error(void) {
  unsigned e = g_error;
  g_error = 0;
  return e;
}

void tg_gl3d_draw(const TgView3D* v, const TgV3List* l, int x, int y, int w, int h, int windowH) {
  if(!g_ok || l->nv == 0) return;
  if(!g_inPicture) SDL_GL_MakeCurrent(g_window, g_ours);
  int gy = windowH - (y + h);
  pViewport(x, gy, w, h);
  pEnable(GL_SCISSOR_TEST);
  pScissor(x, gy, w, h);
  if(!g_inPicture) {  // (in a picture of several views the whole window's depth was cleared once)
    pClearDepth(1.0);
    pDepthMask(GL_TRUE);
    pClear(GL_DEPTH_BUFFER_BIT);
  }
  pEnable(GL_DEPTH_TEST);
  pDepthFunc(GL_LEQUAL);
  if(g_inPicture) pDepthRange(g_depthNear, 1.0);  // (in [MASK_FLOOR, 1] while a mask is up)
  pEnable(GL_BLEND);
  pBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  pEnable(0x809D /* GL_MULTISAMPLE */);
  pDisable(GL_CULL_FACE);
  pUseProgram(g_prog);
  pUniform2f(g_uView, (float) w, (float) h);
  if(g_uHaze >= 0) pUniform3f(g_uHaze, (l->haze >> 16 & 255) / 255.0f, (l->haze >> 8 & 255) / 255.0f, (l->haze & 255) / 255.0f);
  pUniform1i(g_uTex, 0);
  int door = 0;
  pUniform1i(g_uDoor, 0);
  pActiveTexture(0x84C0 /* GL_TEXTURE0 */);  // (the vertex array: upload_vertices binds it)
  if(g_perf && g_qHead - g_qTail < QUERIES) {
    if(!g_query[0]) pGenQueries(QUERIES, g_query);
    pBeginQuery(0x88BF /* GL_TIME_ELAPSED */, g_query[g_qHead % QUERIES]);
  }
  bool timing = g_perf && g_query[0] && g_qHead - g_qTail < QUERIES;
  const TgV3Vertex* src = l->v;
  if(!g_matShader) {  // the basic program: the materials lit at the vertices here
    if((size_t) l->nv > g_bakedCap) {
      TgV3Vertex* nb = realloc(g_baked, sizeof(TgV3Vertex) * (size_t) l->nv);
      if(nb) g_baked = nb, g_bakedCap = (size_t) l->nv;
    }
    if(g_baked && g_bakedCap >= (size_t) l->nv) {
      for(int i = 0; i < l->nv; i++) {
        g_baked[i] = l->v[i];
        g_baked[i].argb = tg_v3_vertex_colour(&l->v[i], l->haze);
      }
      src = g_baked;
    }
  }
  int base = upload_vertices(src, l->nv);
  g_stats.triangles += l->nv / 3;
  for(int b = 0; b < l->nb; b++) {
    const TgV3Batch* bt = &l->b[b];
    GLuint tex = bt->pic >= 0 ? texture_of(v, bt->pic) : 0;
    if(bt->pic >= 0 && !tex) continue;
    if(tex) pBindTexture(GL_TEXTURE_2D, tex);
    pUniform1i(g_uHasTex, tex ? (tg_view3d_pic(v, bt->pic)->mask ? 2 : 1) : 0);  // (2: a tint mask, TgV3Pic.mask)
    if(bt->door != door) pUniform1i(g_uDoor, door = bt->door);  // (what is drawn of it is solid: depth written)
    // See-through things (shadows, glow, haze bands) don't hide what is drawn after them:
    // runs of solid and of see-through triangles, each with its depth writes.
    for(int t = 0; t < bt->count;) {
      const TgV3Vertex* tv = &l->v[bt->first + t];
      bool solid = (tv[0].argb >> 24) == 255 && (tv[1].argb >> 24) == 255 && (tv[2].argb >> 24) == 255;
      int e = t + 3;
      while(e < bt->count) {
        const TgV3Vertex* q = &l->v[bt->first + e];
        bool s2 = (q[0].argb >> 24) == 255 && (q[1].argb >> 24) == 255 && (q[2].argb >> 24) == 255;
        if(s2 != solid) break;
        e += 3;
      }
      pDepthMask(solid ? GL_TRUE : GL_FALSE);
      pDrawArrays(GL_TRIANGLES, base + bt->first + t, e - t);
      g_stats.draws++;
      t = e;
    }
  }
  pDepthMask(GL_TRUE);
  pDisable(GL_SCISSOR_TEST);
  pDisable(GL_DEPTH_TEST);
  if(timing) {
    pEndQuery(0x88BF);
    g_qHead++;
  }
  if(g_perf) read_queries();
  if(!g_inPicture) {
    if(!g_error) g_error = pGetError();
    SDL_GL_MakeCurrent(g_window, g_theirs);
  }
}

void tg_gl3d_shutdown(void) {
  if(!g_ours) return;
  SDL_GL_MakeCurrent(g_window, g_ours);
  for(int i = 0; i < MAX_PICS; i++)
    if(g_tex[i]) pDeleteTextures(1, &g_tex[i]);
  SDL_GL_MakeCurrent(g_window, g_theirs);
  SDL_GL_DestroyContext(g_ours);
  g_ours = NULL;
  g_ok = false;
  free(g_baked);
  g_baked = NULL, g_bakedCap = 0;
}
