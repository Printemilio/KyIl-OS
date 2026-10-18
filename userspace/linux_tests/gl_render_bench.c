/* Software GLES renderer benchmark (host build, see scripts/bench-gl.sh).
 *
 * Draws the kinds of passes a WPE compositor frame is made of — a solid fill,
 * a textured layer with opacity, the same blended, and a screenful of small UI
 * quads — and prints milliseconds per frame, so changes to the shader
 * interpreter / rasterizer in libvex_graphics can be measured and compared.
 *
 *   gl_render_bench [frames] [size]
 */
#define _GNU_SOURCE
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <signal.h>
#include <string.h>
#include <sys/time.h>
#include <ucontext.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "bench setup failed line %d: %s (GL %x EGL %x)\n", \
                     __LINE__, #x, glGetError(), eglGetError()); exit(1); } } while (0)

/* VEX_BENCH_PROFILE=1: sample the instruction pointer every millisecond and print
 * the hottest "module+offset" lines (feed them to addr2line -f -e <module>). */
#define SAMPLE_MAX 200000
static uintptr_t samples[SAMPLE_MAX];
static volatile unsigned sample_count;
static volatile int profile_enabled = 1;

static void on_profile_tick(int signal_number, siginfo_t *info, void *context) {
    (void)signal_number;
    (void)info;
    if (profile_enabled && sample_count < SAMPLE_MAX) {
        samples[sample_count++] = (uintptr_t)((ucontext_t *)context)->uc_mcontext.gregs[REG_RIP];
    }
}

static void start_profile(void) {
    struct sigaction action;
    struct itimerval timer = { { 0, 1000 }, { 0, 1000 } };

    memset(&action, 0, sizeof(action));
    action.sa_sigaction = on_profile_tick;
    action.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction(SIGPROF, &action, NULL);
    setitimer(ITIMER_PROF, &timer, NULL);
}

static int compare_addresses(const void *a, const void *b) {
    const uintptr_t x = *(const uintptr_t *)a, y = *(const uintptr_t *)b;
    return x < y ? -1 : x > y;
}

static void dump_profile(void) {
    char line[512];
    FILE *maps = fopen("/proc/self/maps", "r");
    unsigned total = sample_count;

    qsort(samples, total, sizeof(*samples), compare_addresses);
    for (unsigned i = 0; i < total;) {
        unsigned j = i;
        while (j < total && samples[j] == samples[i]) {
            ++j;
        }
        rewind(maps);
        while (fgets(line, sizeof(line), maps)) {
            unsigned long start, end, offset;
            char permissions[8], path[256] = "";

            if (sscanf(line, "%lx-%lx %7s %lx %*s %*s %255[^\n]", &start, &end, permissions, &offset, path) >= 4 &&
                samples[i] >= start && samples[i] < end) {
                printf("PROFSAMPLE %u %s 0x%lx\n", j - i, path, (unsigned long)(samples[i] - start + offset));
                break;
            }
        }
        i = j;
    }
    fclose(maps);
}

static double now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static GLuint compile(GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    GLint ok = 0;

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    CHECK(ok);
    return shader;
}

static GLuint program(const char *vertex, const char *fragment) {
    GLuint id = glCreateProgram();
    GLint ok = 0;

    glAttachShader(id, compile(GL_VERTEX_SHADER, vertex));
    glAttachShader(id, compile(GL_FRAGMENT_SHADER, fragment));
    glBindAttribLocation(id, 0, "position");
    glBindAttribLocation(id, 1, "texcoord");
    glLinkProgram(id);
    glGetProgramiv(id, GL_LINK_STATUS, &ok);
    CHECK(ok);
    return id;
}

static const char *vertex_source =
    "attribute vec2 position; attribute vec2 texcoord; varying vec2 v_texcoord;\n"
    "void main() { gl_Position = vec4(position, 0.0, 1.0); v_texcoord = texcoord; }\n";
static const char *solid_source =
    "precision mediump float; uniform vec4 color;\n"
    "void main() { gl_FragColor = color; }\n";
static const char *texture_source =
    "precision mediump float; uniform sampler2D tex; uniform float opacity; varying vec2 v_texcoord;\n"
    "void main() { gl_FragColor = texture2D(tex, v_texcoord) * opacity; }\n";

static void quad(float x0, float y0, float x1, float y1) {
    const float vertices[] = { x0, y0, 0, 0,  x1, y0, 1, 0,  x0, y1, 0, 1,  x1, y1, 1, 1 };

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, vertices);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, vertices + 2);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

static void report(const char *name, double started, int frames, double pixels_per_frame) {
    const double per_frame = (now_ms() - started) / frames;

    printf("bench %-34s %9.2f ms/frame  %8.2f Mpx/s\n", name, per_frame, pixels_per_frame / per_frame / 1e3);
    fflush(stdout);
}

/* ---- fast-path equivalence ------------------------------------------------
 * The Skia programs with a native implementation must draw exactly what the
 * interpreter draws. Each is rendered twice, with and without the fast paths,
 * magnified, minified and rotated, opaque and blended. */
extern void vex_render_set_fast_paths(int enabled);

static const char *skia_vertex_texture =
    "#version 100\nprecision mediump float;precision mediump sampler2D;uniform highp vec4 sk_RTAdjust;\n"
    "attribute highp vec2 position;attribute highp vec2 localCoord;varying highp vec2 vlocalCoord_S0;\n"
    "void main(){vlocalCoord_S0=localCoord;gl_Position=vec4(position,0.0,1.0);\n"
    "gl_Position=vec4(gl_Position.xy*sk_RTAdjust.xz+gl_Position.ww*sk_RTAdjust.yw,0.0,gl_Position.w);}\n";
static const char *skia_fragment_texture =
    "#version 100\nprecision mediump float;precision mediump sampler2D;uniform sampler2D uTextureSampler_0_S0;\n"
    "varying highp vec2 vlocalCoord_S0;\n"
    "void main(){mediump vec4 outputColor_S0=vec4(1.0);highp vec2 texCoord=vlocalCoord_S0;\n"
    "outputColor_S0=texture2D(uTextureSampler_0_S0,texCoord,-0.475);{gl_FragColor=outputColor_S0;}}\n";
static const char *skia_vertex_solid =
    "#version 100\nprecision mediump float;precision mediump sampler2D;uniform highp vec4 sk_RTAdjust;\n"
    "attribute highp vec2 position;attribute mediump vec4 color;varying mediump vec4 vcolor_S0;\n"
    "void main(){vcolor_S0=color;gl_Position=vec4(position,0.0,1.0);\n"
    "gl_Position=vec4(gl_Position.xy*sk_RTAdjust.xz+gl_Position.ww*sk_RTAdjust.yw,0.0,gl_Position.w);}\n";
static const char *skia_fragment_solid =
    "#version 100\nprecision mediump float;precision mediump sampler2D;varying mediump vec4 vcolor_S0;\n"
    "void main(){mediump vec4 outputColor_S0=vcolor_S0;{gl_FragColor=outputColor_S0;}}\n";
static const char *skia_vertex_atlas =
    "#version 100\nprecision mediump float;precision mediump sampler2D;uniform highp vec4 sk_RTAdjust;\n"
    "uniform highp vec2 uAtlasSizeInv_S0;attribute highp vec2 inPosition;attribute mediump vec4 inColor;\n"
    "attribute highp vec2 inTextureCoords;varying highp vec2 vTextureCoords_S0;varying highp float vTexIndex_S0;\n"
    "varying mediump vec4 vinColor_S0;\n"
    "void main(){highp float texIdx=0.0;highp vec2 unormTexCoords=vec2(inTextureCoords.x,inTextureCoords.y);\n"
    "vTextureCoords_S0=unormTexCoords*uAtlasSizeInv_S0;vTexIndex_S0=texIdx;vinColor_S0=inColor;\n"
    "gl_Position=vec4(inPosition,0.0,1.0);\n"
    "gl_Position=vec4(gl_Position.xy*sk_RTAdjust.xz+gl_Position.ww*sk_RTAdjust.yw,0.0,gl_Position.w);}\n";
static const char *skia_fragment_atlas =
    "#version 100\nprecision mediump float;precision mediump sampler2D;uniform sampler2D uTextureSampler_0_S0;\n"
    "varying highp vec2 vTextureCoords_S0;varying highp float vTexIndex_S0;varying mediump vec4 vinColor_S0;\n"
    "void main(){mediump vec4 outputColor_S0=vinColor_S0;\n"
    "mediump vec4 texColor=texture2D(uTextureSampler_0_S0,vTextureCoords_S0,-0.475).wwww;\n"
    "mediump vec4 outputCoverage_S0=texColor;{gl_FragColor=outputColor_S0*outputCoverage_S0;}}\n";

static GLuint skia_program(const char *vertex, const char *fragment, const char *const *attributes) {
    GLuint id = glCreateProgram();
    GLint ok = 0;

    glAttachShader(id, compile(GL_VERTEX_SHADER, vertex));
    glAttachShader(id, compile(GL_FRAGMENT_SHADER, fragment));
    for (int i = 0; attributes[i]; ++i) {
        glBindAttribLocation(id, i, attributes[i]);
    }
    glLinkProgram(id);
    glGetProgramiv(id, GL_LINK_STATUS, &ok);
    CHECK(ok);
    return id;
}

/* One quad as two triangles: clip-space corners, then `extra_stride` floats per
 * corner split over the attributes after position. */
static void draw_quad(const float corners[4][2], const float *extra, int extra_stride, int attribute_count,
                      const int *component_counts) {
    float vertices[6 * 16];
    const int order[6] = { 0, 1, 2, 2, 1, 3 };
    const int floats = 2 + extra_stride;

    for (int v = 0; v < 6; ++v) {
        vertices[v * floats] = corners[order[v]][0];
        vertices[v * floats + 1] = corners[order[v]][1];
        for (int e = 0; e < extra_stride; ++e) {
            vertices[v * floats + 2 + e] = extra[order[v] * extra_stride + e];
        }
    }
    const int stride = floats * (int)sizeof(float);
    int offset = 2;
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, vertices);
    glEnableVertexAttribArray(0);
    for (int a = 1; a < attribute_count; ++a) {
        glVertexAttribPointer(a, component_counts[a - 1], GL_FLOAT, GL_FALSE, stride, (const float *)vertices + offset);
        glEnableVertexAttribArray(a);
        offset += component_counts[a - 1];
    }
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

static int render_case(GLuint program, int kind, int blended, int size, uint32_t *out) {
    glClearColor(0.25f, 0.5f, 0.75f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(program);
    glUniform4f(glGetUniformLocation(program, "sk_RTAdjust"), 1, 0, 1, 0);
    if (blended) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glDisable(GL_BLEND);
    }
    if (kind != 0) {
        glUniform1i(glGetUniformLocation(program, "uTextureSampler_0_S0"), 0);
    }
    if (kind == 2) {
        glUniform2f(glGetUniformLocation(program, "uAtlasSizeInv_S0"), 1.0f / 64.0f, 1.0f / 64.0f);
    }
    /* magnified, minified and rotated placements of the same quad */
    const float scales[3] = { 0.9f, 0.08f, 0.6f };
    for (int placement = 0; placement < 3; ++placement) {
        const float s = scales[placement], angle = placement == 2 ? 0.55f : 0.0f;
        const float c = cosf(angle) * s, n = sinf(angle) * s;
        const float cx = placement == 1 ? 0.6f : 0.0f, cy = placement == 1 ? 0.6f : 0.0f;
        const float corners[4][2] = { { cx - c + n, cy - n - c }, { cx + c + n, cy + n - c },
                                      { cx - c - n, cy - n + c }, { cx + c - n, cy + n + c } };
        if (kind == 0) {
            const float extra[16] = { 1, 0, 0, 0.5f,  0, 1, 0, 0.5f,  0, 0, 1, 0.5f,  1, 1, 0, 0.5f };
            const int counts[1] = { 4 };
            draw_quad(corners, extra, 4, 2, counts);
        } else if (kind == 1) {
            const float extra[8] = { 0, 0,  1, 0,  0, 1,  1, 1 };
            const int counts[1] = { 2 };
            draw_quad(corners, extra, 2, 2, counts);
        } else {
            const float extra[24] = { 0.9f, 0.4f, 0.2f, 1,  0, 0,   0.2f, 0.9f, 0.4f, 1,  64, 0,
                                      0.4f, 0.2f, 0.9f, 1,  0, 64,  0.9f, 0.9f, 0.2f, 1,  64, 64 };
            const int counts[2] = { 4, 2 };
            draw_quad(corners, extra, 6, 3, counts);
        }
    }
    glFinish();
    glReadPixels(0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, out);
    return glGetError() == GL_NO_ERROR;
}

static int equivalence(int size) {
    static const char *const solid_attributes[] = { "position", "color", NULL };
    static const char *const texture_attributes[] = { "position", "localCoord", NULL };
    static const char *const atlas_attributes[] = { "inPosition", "inColor", "inTextureCoords", NULL };
    const char *names[3] = { "solid colour", "texture", "atlas coverage" };
    GLuint programs[3] = { skia_program(skia_vertex_solid, skia_fragment_solid, solid_attributes),
                           skia_program(skia_vertex_texture, skia_fragment_texture, texture_attributes),
                           skia_program(skia_vertex_atlas, skia_fragment_atlas, atlas_attributes) };
    uint32_t *fast = malloc((size_t)size * size * 4), *slow = malloc((size_t)size * size * 4);
    uint32_t texels[64 * 64];
    int failures = 0;

    for (int i = 0; i < 64 * 64; ++i) {
        texels[i] = ((uint32_t)(i * 2654435761u >> 24) << 24) | ((uint32_t)(i * 40503u) & 0xffffffu);
    }
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    for (int kind = 0; kind < 3; ++kind) {
        for (int filter = 0; filter < 2; ++filter) {
            const GLint mode = filter ? GL_LINEAR : GL_NEAREST;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mode);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mode);
            for (int blended = 0; blended < 2; ++blended) {
                vex_render_set_fast_paths(1);
                const int ok_fast = render_case(programs[kind], kind, blended, size, fast);
                vex_render_set_fast_paths(0);
                const int ok_slow = render_case(programs[kind], kind, blended, size, slow);
                vex_render_set_fast_paths(1);
                unsigned differing = 0;
                for (int i = 0; i < size * size; ++i) {
                    differing += fast[i] != slow[i];
                }
                const int pass = ok_fast && ok_slow && differing == 0;
                uint32_t checksum = 2166136261u;
                for (int i = 0; i < size * size; ++i) {
                    checksum = (checksum ^ fast[i]) * 16777619u;
                }
                printf("equivalence %-15s %-7s %-7s checksum %08x %s", names[kind], filter ? "linear" : "nearest",
                       blended ? "blended" : "opaque", checksum, pass ? "PASS\n" : "FAIL");
                if (!pass) {
                    printf(" (%u pixels differ)\n", differing);
                }
                failures += !pass;
            }
        }
    }
    free(fast);
    free(slow);
    return failures;
}

/* The three Skia programs drawing one screen-sized quad, interpreter vs native. */
static void skia_speed(int size, int frames) {
    static const char *const solid_attributes[] = { "position", "color", NULL };
    static const char *const texture_attributes[] = { "position", "localCoord", NULL };
    static const char *const atlas_attributes[] = { "inPosition", "inColor", "inTextureCoords", NULL };
    const char *names[3] = { "skia solid colour", "skia texture", "skia atlas coverage" };
    GLuint programs[3] = { skia_program(skia_vertex_solid, skia_fragment_solid, solid_attributes),
                           skia_program(skia_vertex_texture, skia_fragment_texture, texture_attributes),
                           skia_program(skia_vertex_atlas, skia_fragment_atlas, atlas_attributes) };
    uint32_t *out = malloc((size_t)size * size * 4);
    uint32_t texels[64 * 64];

    for (int i = 0; i < 64 * 64; ++i) {
        texels[i] = (uint32_t)(i * 2654435761u);
    }
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    for (int kind = 0; kind < 3; ++kind) {
        for (int fast = 1; fast >= 0; --fast) {
            vex_render_set_fast_paths(fast);
            profile_enabled = fast;                                    /* sample only the native runs */
            render_case(programs[kind], kind, 1, size, out);          /* warm up */
            const double started = now_ms();
            for (int i = 0; i < frames; ++i) {
                render_case(programs[kind], kind, 1, size, out);
            }
            char label[64];
            snprintf(label, sizeof(label), "%s, %s", names[kind], fast ? "native" : "interpreter");
            report(label, started, frames * 3, (double)size * size * 0.4);
        }
    }
    vex_render_set_fast_paths(1);
    free(out);
}

int main(int argc, char **argv) {
    const int frames = argc > 1 ? atoi(argv[1]) : 6;
    const int size = argc > 2 ? atoi(argv[2]) : 512;
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLConfig config;
    EGLint count;
    const EGLint choose[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE };
    const EGLint attributes[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    const EGLint surface_size[] = { EGL_WIDTH, size, EGL_HEIGHT, size, EGL_NONE };

    CHECK(display != EGL_NO_DISPLAY && eglInitialize(display, NULL, NULL));
    CHECK(eglChooseConfig(display, choose, &config, 1, &count) && count == 1);
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, attributes);
    EGLSurface surface = eglCreatePbufferSurface(display, config, surface_size);
    CHECK(context != EGL_NO_CONTEXT && surface != EGL_NO_SURFACE);
    CHECK(eglMakeCurrent(display, surface, surface, context));
    glViewport(0, 0, size, size);
    if (argc > 3 && !strcmp(argv[3], "equivalence")) {
        return equivalence(size) ? 1 : 0;
    }
    if (argc > 3 && !strcmp(argv[3], "skia")) {
        if (getenv("VEX_BENCH_PROFILE")) {
            start_profile();
        }
        skia_speed(size, frames);
        if (getenv("VEX_BENCH_PROFILE")) {
            dump_profile();
        }
        return 0;
    }

    GLuint solid = program(vertex_source, solid_source);
    GLuint textured = program(vertex_source, texture_source);
    uint32_t *texels = malloc((size_t)256 * 256 * 4);
    for (int i = 0; i < 256 * 256; ++i) {
        texels[i] = 0xff000000u | (uint32_t)(i * 2654435761u >> 8);
    }
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    CHECK(glGetError() == GL_NO_ERROR);
    const double pixels = (double)size * size;
    if (getenv("VEX_BENCH_PROFILE")) {
        start_profile();
    }

    glUseProgram(solid);
    glUniform4f(glGetUniformLocation(solid, "color"), 0.2f, 0.4f, 0.8f, 1.0f);
    quad(-1, -1, 1, 1);                                   /* warm up */
    double started = now_ms();
    for (int i = 0; i < frames; ++i) {
        quad(-1, -1, 1, 1);
    }
    glFinish();
    report("solid fullscreen quad", started, frames, pixels);

    glUseProgram(textured);
    glUniform1i(glGetUniformLocation(textured, "tex"), 0);
    glUniform1f(glGetUniformLocation(textured, "opacity"), 1.0f);
    started = now_ms();
    for (int i = 0; i < frames; ++i) {
        quad(-1, -1, 1, 1);
    }
    glFinish();
    report("textured fullscreen quad", started, frames, pixels);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUniform1f(glGetUniformLocation(textured, "opacity"), 0.5f);
    started = now_ms();
    for (int i = 0; i < frames; ++i) {
        quad(-1, -1, 1, 1);
    }
    glFinish();
    report("textured fullscreen quad, blended", started, frames, pixels);

    started = now_ms();
    for (int i = 0; i < frames; ++i) {
        for (int row = 0; row < 12; ++row) {
            for (int column = 0; column < 16; ++column) {
                const float x = -1.0f + 2.0f * column / 16.0f, y = -1.0f + 2.0f * row / 12.0f;
                quad(x, y, x + 1.8f / 16.0f, y + 1.8f / 12.0f);
            }
        }
    }
    glFinish();
    report("192 small blended textured quads", started, frames, pixels * 0.81);

    glDisable(GL_BLEND);
    glUseProgram(solid);
    started = now_ms();
    for (int i = 0; i < frames; ++i) {
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glFinish();
    report("glClear", started, frames, pixels);
    CHECK(glGetError() == GL_NO_ERROR);
    if (getenv("VEX_BENCH_PROFILE")) {
        dump_profile();
    }
    return 0;
}
