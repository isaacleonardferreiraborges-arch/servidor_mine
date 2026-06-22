/**
 * NativeTextApp - main.cpp
 *
 * Renders "oi amigos tudo bem" using:
 *   - NativeActivity (android_native_app_glue)
 *   - OpenGL ES 2.0
 *   - FreeType for glyph rasterization
 *   - Touch to cycle text colors
 *   - Smooth scale-pulse animation
 */

#include <android/log.h>
#include <android_native_app_glue.h>
#include <android/asset_manager.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <cmath>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>
#include <map>
#include <algorithm>

// ─── Logging ────────────────────────────────────────────────────────────────
#define TAG "NativeTextApp"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

// ─── Glyph ──────────────────────────────────────────────────────────────────
struct Glyph {
    GLuint  textureID;
    int     width, height;
    int     bearingX, bearingY;
    long    advance;          // 26.6 fixed point
};

// ─── Color palette for touch cycling ────────────────────────────────────────
struct Color3 { float r, g, b; };

static const Color3 PALETTE[] = {
    {0.129f, 0.588f, 0.953f},   // Material Blue   #2196F3
    {0.000f, 0.784f, 0.588f},   // Teal            #00C896
    {1.000f, 0.341f, 0.133f},   // Deep Orange     #FF5722
    {0.612f, 0.153f, 0.690f},   // Purple          #9C27B0
    {1.000f, 0.757f, 0.027f},   // Amber           #FFC107
};
static const int PALETTE_SIZE = 5;

// ─── Application state ──────────────────────────────────────────────────────
struct AppState {
    // EGL
    EGLDisplay  display = EGL_NO_DISPLAY;
    EGLSurface  surface = EGL_NO_SURFACE;
    EGLContext  context = EGL_NO_CONTEXT;
    int32_t     screenW = 0, screenH = 0;

    // OpenGL
    GLuint      shaderProgram = 0;
    GLuint      vbo = 0;
    GLint       uTexLoc    = -1;
    GLint       uColorLoc  = -1;
    GLint       uProjLoc   = -1;

    // FreeType
    FT_Library  ftLib  = nullptr;
    FT_Face     ftFace = nullptr;
    std::map<uint32_t, Glyph> glyphs;

    // Text
    std::string text = "oi amigos tudo bem";
    int         colorIdx  = 0;
    float       targetScale = 1.0f;
    float       currentScale= 1.0f;
    float       pulseTime  = 0.0f;

    // Timing
    struct timespec lastTime{};

    bool        initialized = false;
    bool        hasFocus    = false;
};

// ─── Vertex / Fragment shaders (GLSL ES 1.00) ────────────────────────────────
static const char* VERT_SRC = R"GLSL(
attribute vec4 aPos;       // xy = position, zw = texcoord
uniform   mat4 uProj;
varying   vec2 vTex;
void main(){
    gl_Position = uProj * vec4(aPos.xy, 0.0, 1.0);
    vTex = aPos.zw;
}
)GLSL";

static const char* FRAG_SRC = R"GLSL(
precision mediump float;
uniform sampler2D uTex;
uniform vec3      uColor;
varying vec2      vTex;
void main(){
    float alpha = texture2D(uTex, vTex).r;
    gl_FragColor = vec4(uColor, alpha);
}
)GLSL";

// ─── Shader helpers ──────────────────────────────────────────────────────────
static GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[512]; glGetShaderInfoLog(s, 512, nullptr, buf);
        LOGE("Shader error: %s", buf);
    }
    return s;
}

static GLuint buildProgram(const char* vSrc, const char* fSrc) {
    GLuint v = compileShader(GL_VERTEX_SHADER,   vSrc);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fSrc);
    GLuint p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[512]; glGetProgramInfoLog(p, 512, nullptr, buf);
        LOGE("Link error: %s", buf);
    }
    glDeleteShader(v); glDeleteShader(f);
    return p;
}

// Build orthographic projection matrix (column-major for OpenGL)
static void ortho(float* m, float l, float r, float b, float t) {
    memset(m, 0, 16 * sizeof(float));
    m[0]  =  2.0f / (r - l);
    m[5]  =  2.0f / (t - b);
    m[10] = -1.0f;
    m[12] = -(r + l) / (r - l);
    m[13] = -(t + b) / (t - b);
    m[15] =  1.0f;
}

// ─── FreeType: load font from assets ─────────────────────────────────────────
static bool loadFont(AppState& s, android_app* app) {
    AAsset* asset = AAssetManager_open(
        app->activity->assetManager, "Roboto-Regular.ttf", AASSET_MODE_BUFFER);
    if (!asset) { LOGE("Font asset not found"); return false; }

    size_t size = (size_t)AAsset_getLength(asset);
    std::vector<unsigned char> buf(size);
    AAsset_read(asset, buf.data(), size);
    AAsset_close(asset);

    if (FT_Init_FreeType(&s.ftLib)) { LOGE("FT_Init failed"); return false; }

    // FT_New_Memory_Face needs the buffer to stay alive → keep in static
    static std::vector<unsigned char> fontBuf;
    fontBuf = std::move(buf);

    if (FT_New_Memory_Face(s.ftLib, fontBuf.data(), (FT_Long)fontBuf.size(), 0, &s.ftFace)) {
        LOGE("FT_New_Memory_Face failed"); return false;
    }
    LOGI("Font loaded: %s", s.ftFace->family_name);
    return true;
}

// Build glyph atlas: one texture per glyph (simple approach, works well for short strings)
static void buildGlyphs(AppState& s, int pixelSize) {
    FT_Set_Pixel_Sizes(s.ftFace, 0, (FT_UInt)pixelSize);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    const std::string& text = s.text;
    for (size_t i = 0; i < text.size(); ) {
        // Decode UTF-8 (simple ASCII assumed, but handle the loop correctly)
        unsigned char c = (unsigned char)text[i];
        uint32_t cp = c;
        i++;

        if (s.glyphs.count(cp)) continue;

        if (FT_Load_Char(s.ftFace, cp, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL)) {
            LOGE("FT_Load_Char failed for '%c'", (char)cp);
            continue;
        }
        FT_GlyphSlot g = s.ftFace->glyph;

        GLuint tex;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE,
                     (GLsizei)g->bitmap.width, (GLsizei)g->bitmap.rows,
                     0, GL_LUMINANCE, GL_UNSIGNED_BYTE, g->bitmap.buffer);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        Glyph glyph;
        glyph.textureID = tex;
        glyph.width     = (int)g->bitmap.width;
        glyph.height    = (int)g->bitmap.rows;
        glyph.bearingX  = g->bitmap_left;
        glyph.bearingY  = g->bitmap_top;
        glyph.advance   = g->advance.x;   // 26.6
        s.glyphs[cp] = glyph;
    }
}

// ─── EGL init ────────────────────────────────────────────────────────────────
static bool initDisplay(AppState& s, android_app* app) {
    s.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(s.display, nullptr, nullptr);

    const EGLint attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_BLUE_SIZE,  8, EGL_GREEN_SIZE, 8, EGL_RED_SIZE, 8,
        EGL_NONE
    };
    EGLConfig config; EGLint numCfg;
    eglChooseConfig(s.display, attribs, &config, 1, &numCfg);

    const EGLint ctxAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    s.context = eglCreateContext(s.display, config, EGL_NO_CONTEXT, ctxAttribs);
    s.surface = eglCreateWindowSurface(s.display, config, app->window, nullptr);
    eglMakeCurrent(s.display, s.surface, s.surface, s.context);

    eglQuerySurface(s.display, s.surface, EGL_WIDTH,  &s.screenW);
    eglQuerySurface(s.display, s.surface, EGL_HEIGHT, &s.screenH);
    LOGI("Screen: %d x %d", s.screenW, s.screenH);
    return true;
}

// ─── GL resource init ────────────────────────────────────────────────────────
static void initGL(AppState& s, android_app* app) {
    s.shaderProgram = buildProgram(VERT_SRC, FRAG_SRC);
    glUseProgram(s.shaderProgram);

    s.uTexLoc   = glGetUniformLocation(s.shaderProgram, "uTex");
    s.uColorLoc = glGetUniformLocation(s.shaderProgram, "uColor");
    s.uProjLoc  = glGetUniformLocation(s.shaderProgram, "uProj");

    glGenBuffers(1, &s.vbo);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    // Choose font size based on screen width: ~10% of screen width
    int fontSize = std::max(32, (int)(s.screenW * 0.10f));
    buildGlyphs(s, fontSize);

    clock_gettime(CLOCK_MONOTONIC, &s.lastTime);
    s.initialized = true;
    LOGI("GL initialized, font size = %d", fontSize);
}

// ─── Measure total text width ─────────────────────────────────────────────────
static float measureText(AppState& s) {
    float w = 0;
    for (unsigned char c : s.text) {
        auto it = s.glyphs.find((uint32_t)c);
        if (it != s.glyphs.end())
            w += (it->second.advance >> 6);
    }
    return w;
}

// ─── Draw a single frame ──────────────────────────────────────────────────────
static void drawFrame(AppState& s) {
    if (!s.initialized) return;

    // Delta time
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double dt = (now.tv_sec  - s.lastTime.tv_sec)
              + (now.tv_nsec - s.lastTime.tv_nsec) * 1e-9;
    s.lastTime = now;

    // Pulse animation: gentle breathing scale
    s.pulseTime += (float)dt;
    float pulse = 1.0f + 0.025f * sinf(s.pulseTime * 1.8f);
    // Smooth towards pulse
    s.currentScale += (pulse - s.currentScale) * (float)(dt * 8.0);

    glViewport(0, 0, s.screenW, s.screenH);
    glClearColor(0.06f, 0.06f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(s.shaderProgram);

    // Orthographic: origin bottom-left
    float proj[16];
    ortho(proj, 0, (float)s.screenW, 0, (float)s.screenH);
    glUniformMatrix4fv(s.uProjLoc, 1, GL_FALSE, proj);

    const Color3& col = PALETTE[s.colorIdx];
    glUniform3f(s.uColorLoc, col.r, col.g, col.b);
    glUniform1i(s.uTexLoc, 0);
    glActiveTexture(GL_TEXTURE0);

    // Measure text to center it
    float textW = measureText(s) * s.currentScale;

    // Vertical center: use ascender of the face
    // bearingY max ≈ font size * 0.7 for most fonts
    float ascent = 0;
    for (unsigned char c : s.text) {
        auto it = s.glyphs.find((uint32_t)c);
        if (it != s.glyphs.end())
            ascent = std::max(ascent, (float)it->second.bearingY);
    }
    ascent *= s.currentScale;

    float startX = (s.screenW - textW) * 0.5f;
    float baseY  = s.screenH * 0.5f - ascent * 0.5f;

    GLint aPosLoc = glGetAttribLocation(s.shaderProgram, "aPos");
    glEnableVertexAttribArray(aPosLoc);
    glBindBuffer(GL_ARRAY_BUFFER, s.vbo);

    for (unsigned char c : s.text) {
        auto it = s.glyphs.find((uint32_t)c);
        if (it == s.glyphs.end()) {
            // Space or unknown: advance
            startX += 10.0f * s.currentScale;
            continue;
        }
        const Glyph& g = it->second;

        float xpos = startX + g.bearingX * s.currentScale;
        float ypos = baseY  + (g.bearingY - g.height) * s.currentScale;
        float w    = g.width  * s.currentScale;
        float h    = g.height * s.currentScale;

        // Two triangles, each vertex = (x, y, u, v)
        float verts[6][4] = {
            { xpos,     ypos + h,  0, 0 },
            { xpos,     ypos,      0, 1 },
            { xpos + w, ypos,      1, 1 },
            { xpos,     ypos + h,  0, 0 },
            { xpos + w, ypos,      1, 1 },
            { xpos + w, ypos + h,  1, 0 },
        };

        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
        glVertexAttribPointer(aPosLoc, 4, GL_FLOAT, GL_FALSE, 0, nullptr);

        glBindTexture(GL_TEXTURE_2D, g.textureID);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        startX += (float)(g.advance >> 6) * s.currentScale;
    }

    glDisableVertexAttribArray(aPosLoc);
    eglSwapBuffers(s.display, s.surface);
}

// ─── Cleanup ─────────────────────────────────────────────────────────────────
static void destroyDisplay(AppState& s) {
    if (s.display == EGL_NO_DISPLAY) return;

    for (auto& kv : s.glyphs)
        glDeleteTextures(1, &kv.second.textureID);
    s.glyphs.clear();

    if (s.vbo) { glDeleteBuffers(1, &s.vbo); s.vbo = 0; }
    if (s.shaderProgram) { glDeleteProgram(s.shaderProgram); s.shaderProgram = 0; }

    if (s.ftFace)  { FT_Done_Face(s.ftFace);      s.ftFace = nullptr; }
    if (s.ftLib)   { FT_Done_FreeType(s.ftLib);   s.ftLib  = nullptr; }

    eglMakeCurrent(s.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (s.context != EGL_NO_CONTEXT) eglDestroyContext(s.display, s.context);
    if (s.surface != EGL_NO_SURFACE) eglDestroySurface(s.display, s.surface);
    eglTerminate(s.display);

    s.display = EGL_NO_DISPLAY;
    s.context = EGL_NO_CONTEXT;
    s.surface = EGL_NO_SURFACE;
    s.initialized = false;
}

// ─── Input handler ───────────────────────────────────────────────────────────
static int32_t handleInput(android_app* app, AInputEvent* event) {
    AppState* s = (AppState*)app->userData;
    if (!s) return 0;

    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
        int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        if (action == AMOTION_EVENT_ACTION_UP) {
            // Cycle color on finger lift
            s->colorIdx = (s->colorIdx + 1) % PALETTE_SIZE;
            // Trigger a stronger pulse on touch
            s->pulseTime = 0.0f;
            s->currentScale = 1.15f;
            LOGI("Touch → color %d", s->colorIdx);
            return 1;
        }
    }
    return 0;
}

// ─── App command handler ──────────────────────────────────────────────────────
static void handleCmd(android_app* app, int32_t cmd) {
    AppState* s = (AppState*)app->userData;
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (app->window) {
                initDisplay(*s, app);
                loadFont(*s, app);
                initGL(*s, app);
            }
            break;
        case APP_CMD_TERM_WINDOW:
            destroyDisplay(*s);
            break;
        case APP_CMD_GAINED_FOCUS:
            s->hasFocus = true;
            break;
        case APP_CMD_LOST_FOCUS:
            s->hasFocus = false;
            break;
        case APP_CMD_CONFIG_CHANGED:
            // Handle rotation / resize: rebuild everything
            if (s->initialized && app->window) {
                destroyDisplay(*s);
                initDisplay(*s, app);
                loadFont(*s, app);
                initGL(*s, app);
            }
            break;
        default: break;
    }
}

// ─── Entry point ─────────────────────────────────────────────────────────────
void android_main(android_app* app) {
    AppState state;
    app->userData      = &state;
    app->onAppCmd      = handleCmd;
    app->onInputEvent  = handleInput;

    LOGI("NativeTextApp started");

    while (true) {
        int events;
        android_poll_source* source;

        // Poll events — non-blocking when we have a window, blocking when we don't
        int timeout = (state.initialized && state.hasFocus) ? 0 : -1;
        while (ALooper_pollAll(timeout, nullptr, &events, (void**)&source) >= 0) {
            if (source) source->process(app, source);
            if (app->destroyRequested) { destroyDisplay(state); return; }
        }

        if (state.initialized) drawFrame(state);
    }
}
