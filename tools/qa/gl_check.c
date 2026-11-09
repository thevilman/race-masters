// The GL renderer's shaders without the game: draws a tg_headless TG_SHOWCASE_DUMP file (the
// triangles of a showcase picture, TgV3Vertex as drawn, batch by batch with their pictures: the
// cars' textures with their mip levels, the race number's font) with src/gl3d.c's own GLSL and
// draw state on an offscreen EGL context (Mesa: the GPU's driver, or llvmpipe with
// LIBGL_ALWAYS_SOFTWARE=1), and writes the picture as a PPM, to compare with tg_v3_raster's.
// Textures as gl3d.c's texture_of sets them (mipmapped: LINEAR_MIPMAP_LINEAR; smooth: LINEAR;
// else NEAREST), but without its anisotropic filtering (tg_v3_raster has none). Built and run by tools/qa/gl_check.py (which pulls the shader sources out of
// src/gl3d.c into gl_check_shaders.h).
//
//   gl_check IN.v3 OUT.ppm [mat|basic]
//
// mat: the shaded materials' program (per pixel); basic: the fallback program, the materials
// lit at the vertices on the CPU first (tg_v3_vertex_colour), as gl3d.c does when the first
// won't build. Drawn batch by batch with each batch's screen door (uDoor, TgV3Batch.door).
#define GL_GLEXT_PROTOTYPES 1
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gl_check_shaders.h"
#include "mod3d/view3d.h"

static GLuint shader(GLenum kind, const char* src) {
  GLuint s = glCreateShader(kind);
  glShaderSource(s, 1, &src, NULL);
  glCompileShader(s);
  GLint ok = 0;
  char log[4096] = "";
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  glGetShaderInfoLog(s, sizeof(log), NULL, log);
  if(log[0]) fprintf(stderr, "%s shader log:\n%s\n", kind == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
  if(!ok) exit(2);
  return s;
}

static bool solid(const TgV3Vertex* v) { return (v[0].argb >> 24) == 255 && (v[1].argb >> 24) == 255 && (v[2].argb >> 24) == 255; }

int main(int argc, char** argv) {
  if(argc < 3) return fprintf(stderr, "gl_check IN.v3 OUT.ppm [mat|basic]\n"), 1;
  bool mat = argc < 4 || strcmp(argv[3], "basic") != 0;
  FILE* f = fopen(argv[1], "rb");
  uint32_t hd[5];
  if(!f || fread(hd, sizeof(hd), 1, f) != 1 || hd[3] != sizeof(TgV3Vertex) || hd[4] > 1000000)
    return fprintf(stderr, "%s: not a dump of this build\n", argv[1]), 1;
  int w = (int) hd[0], h = (int) hd[1], nb = (int) hd[4];
  size_t n = 0;
  TgV3Vertex* vs = NULL;
  // each batch: its picture's slot, its vertex count, its screen door; then its vertices
  int32_t* bslot = calloc((size_t) nb + 1, sizeof(int32_t));
  int32_t* bfirst = calloc((size_t) nb + 1, sizeof(int32_t));
  int32_t* bcount = calloc((size_t) nb + 1, sizeof(int32_t));
  int32_t* bdoor = calloc((size_t) nb + 1, sizeof(int32_t));
  if(!bslot || !bfirst || !bcount || !bdoor) return 1;
  for(int b = 0; b < nb; b++) {
    int32_t bh[3];
    if(fread(bh, sizeof(bh), 1, f) != 1 || bh[1] < 0) return fprintf(stderr, "%s: cut short\n", argv[1]), 1;
    vs = realloc(vs, (n + (size_t) bh[1] + 1) * sizeof(TgV3Vertex));
    if(!vs || fread(&vs[n], sizeof(TgV3Vertex), (size_t) bh[1], f) != (size_t) bh[1]) return 1;
    bslot[b] = bh[0], bfirst[b] = (int32_t) n, bcount[b] = bh[1] - bh[1] % 3, bdoor[b] = bh[2];
    n += (size_t) bh[1];
  }
  // the pictures: w, h, mips, flags (1 smooth, 2 mask), pixels with their mip levels
  enum { MAXP = 64 };
  uint32_t ph[MAXP][4];
  uint32_t* ppx[MAXP];
  int np = 0;
  while(np < MAXP && fread(ph[np], sizeof(ph[np]), 1, f) == 1) {
    size_t k = 0;
    for(uint32_t l = 0, lw = ph[np][0], lh = ph[np][1]; l <= ph[np][2]; l++, lw = lw > 1 ? lw / 2 : 1, lh = lh > 1 ? lh / 2 : 1) k += (size_t) lw * lh;
    ppx[np] = malloc(k * 4);
    if(!ppx[np] || fread(ppx[np], 4, k, f) != k) return fprintf(stderr, "%s: picture cut short\n", argv[1]), 1;
    np++;
  }
  fclose(f);
  if(!vs) return 1;
  if(!mat)
    for(size_t i = 0; i < n; i++) vs[i].argb = tg_v3_vertex_colour(&vs[i], hd[2]);

  PFNEGLGETPLATFORMDISPLAYEXTPROC getDisplay = (PFNEGLGETPLATFORMDISPLAYEXTPROC) eglGetProcAddress("eglGetPlatformDisplayEXT");
  EGLDisplay dpy = getDisplay ? getDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL) : EGL_NO_DISPLAY;
  if(dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, NULL, NULL)) return fprintf(stderr, "no EGL display\n"), 3;
  eglBindAPI(EGL_OPENGL_API);
  EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3, EGL_CONTEXT_OPENGL_PROFILE_MASK,
                      EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
  EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ctxAttr);
  if(ctx == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) return fprintf(stderr, "no GL 3.3 context\n"), 3;
  fprintf(stderr, "renderer: %s, GL %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));

  GLuint fbo, rb[2];
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glGenRenderbuffers(2, rb);
  glBindRenderbuffer(GL_RENDERBUFFER, rb[0]);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, w, h);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb[0]);
  glBindRenderbuffer(GL_RENDERBUFFER, rb[1]);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rb[1]);

  GLuint prog = glCreateProgram();
  glAttachShader(prog, shader(GL_VERTEX_SHADER, mat ? kVertMat : kVert));
  glAttachShader(prog, shader(GL_FRAGMENT_SHADER, mat ? kFragMat : kFrag));
  glLinkProgram(prog);
  GLint linked = 0;
  glGetProgramiv(prog, GL_LINK_STATUS, &linked);
  if(!linked) return fprintf(stderr, "the program did not link\n"), 2;
  // As gl3d.c: the vertex layout, depth from w, alpha blending, solid and see-through runs.
  GLuint vao, vbo;
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr) (n * sizeof(TgV3Vertex)), vs, GL_STATIC_DRAW);
  GLsizei st = sizeof(TgV3Vertex);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, st, (void*) offsetof(TgV3Vertex, x));
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, st, (void*) offsetof(TgV3Vertex, u));
  glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, st, (void*) offsetof(TgV3Vertex, argb));
  glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, st, (void*) offsetof(TgV3Vertex, w));
  glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, st, (void*) offsetof(TgV3Vertex, nrm));
  glVertexAttribPointer(5, 4, GL_UNSIGNED_BYTE, GL_TRUE, st, (void*) offsetof(TgV3Vertex, eye));
  for(int i = 0; i < (mat ? 6 : 4); i++) glEnableVertexAttribArray((GLuint) i);
  glViewport(0, 0, w, h);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glUseProgram(prog);
  glUniform2f(glGetUniformLocation(prog, "uView"), (float) w, (float) h);
  GLint uHasTex = glGetUniformLocation(prog, "uHasTex");
  glUniform1i(glGetUniformLocation(prog, "uTex"), 0);
  GLint uHaze = glGetUniformLocation(prog, "uHaze");
  if(uHaze >= 0) glUniform3f(uHaze, (hd[2] >> 16 & 255) / 255.0f, (hd[2] >> 8 & 255) / 255.0f, (hd[2] & 255) / 255.0f);
  GLint uDoor = glGetUniformLocation(prog, "uDoor");
  GLuint tex[MAXP];
  glGenTextures(np, tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  for(int i = 0; i < np; i++) {
    glBindTexture(GL_TEXTURE_2D, tex[i]);
    const uint32_t* px = ppx[i];
    for(uint32_t l = 0, lw = ph[i][0], lh = ph[i][1]; l <= ph[i][2]; l++, px += (size_t) lw * lh, lw = lw > 1 ? lw / 2 : 1, lh = lh > 1 ? lh / 2 : 1)
      glTexImage2D(GL_TEXTURE_2D, (GLint) l, GL_RGBA8, (GLsizei) lw, (GLsizei) lh, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    GLint fl = (ph[i][3] & 1) ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, ph[i][2] ? GL_LINEAR_MIPMAP_LINEAR : fl);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, fl);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, (GLint) ph[i][2]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
  for(int b = 0; b < nb; b++) {
    int sl = bslot[b] >= 0 && bslot[b] < np ? bslot[b] : -1;
    if(sl >= 0) glBindTexture(GL_TEXTURE_2D, tex[sl]);
    glUniform1i(uHasTex, sl < 0 ? 0 : (ph[sl][3] & 2) ? 2 : 1);
    glUniform1i(uDoor, bdoor[b]);  // its screen door (as gl3d.c)
    for(int t = bfirst[b]; t < bfirst[b] + bcount[b];) {
      bool s = solid(&vs[t]);
      int e = t + 3;
      while(e < bfirst[b] + bcount[b] && solid(&vs[e]) == s) e += 3;
      glDepthMask(s ? GL_TRUE : GL_FALSE);
      glDrawArrays(GL_TRIANGLES, (GLint) t, (GLsizei) (e - t));
      t = e;
    }
  }
  glFinish();
  if(glGetError() != GL_NO_ERROR) return fprintf(stderr, "GL error\n"), 4;
  uint8_t* px = malloc((size_t) w * h * 4);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
  FILE* o = fopen(argv[2], "wb");
  if(!o || !px) return 1;
  fprintf(o, "P6\n%d %d\n255\n", w, h);
  for(int y = h - 1; y >= 0; y--)
    for(int x = 0; x < w; x++) fwrite(&px[((size_t) y * w + x) * 4], 1, 3, o);
  fclose(o);
  eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroyContext(dpy, ctx);
  eglTerminate(dpy);
  return 0;
}
