// Solace Express - OpenGL renderer: deferred rasterizer + sprites + post + UI
#include "renderer.h"
#include "materials.h"
#include "shaders.h"
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "third_party/stb_image.h"   // public domain (Sean Barrett): the loading-screen pictures
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#include "shaders_wraith_cockpit.h"
#include "entity_shaders.h"
#include "font_data.h"
#include "scenery.h"
#include <unordered_map>
#include <complex>
#include <cstring>
#include <mutex>

Renderer g_ren;

// per thread: the intro thread draws with its own GL context, whose program names can repeat the main one's. Keyed by
// the program and the name's address: every caller passes a string literal (static storage, so the address names the
// spelling for the program's life), and a frame's few thousand lookups build no strings
struct UniKey { GLuint p; const char* n; bool operator==(const UniKey& o) const { return p == o.p && n == o.n; } };
struct UniKeyHash { size_t operator()(const UniKey& k) const { return std::hash<const void*>()(k.n) ^ ((size_t)k.p * 0x9E3779B97F4A7C15ull); } };
static thread_local std::unordered_map<UniKey, GLint, UniKeyHash> s_uniCache;
GLint U(GLuint prog, const char* name) {
  const UniKey k{prog, name};
  auto it = s_uniCache.find(k);
  if (it != s_uniCache.end()) return it->second;
  GLint l = glGetUniformLocation(prog, name);
  s_uniCache[k] = l;
  return l;
}

static GLuint compile(GLenum type, const std::string& src, std::string& err) {
  // (debug: NVFAIL - a fragment shader containing that text fails as NVIDIA's compiler does, unless it also contains
  // NVFAIL_OK's; to rehearse linkProgramCached's retries and the reduced builds on any driver)
  static const char* nvFail = getenv("NVFAIL"); static const char* nvOk = getenv("NVFAIL_OK");
  if (nvFail && type == GL_FRAGMENT_SHADER && src.find(nvFail) != std::string::npos && !(nvOk && src.find(nvOk) != std::string::npos)) {
    err += "Fragment info\n-------------\n(0) : fatal error C9999: simulated (NVFAIL)\n"; return 0;
  }
  GLuint s = glCreateShader(type);
  const char* c = src.c_str();
  glShaderSource(s, 1, &c, nullptr);
  glCompileShader(s);
  GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) { char log[8192]; glGetShaderInfoLog(s, sizeof(log), nullptr, log); err += log; return 0; }
  return s;
}
// ------------------------------------------------------------------ shader program cache
// Linked programs are saved as the driver's own binaries (ARB_get_program_binary) in g_shaderCacheDir, one file per
// program, named by a hash of its source and the GPU / driver strings: later launches load them instead of compiling,
// and a new game version, driver or GPU simply misses the cache and rebuilds it. A binary the driver rejects (or a
// driver without the extension) falls back to compiling.
std::string g_shaderCacheDir;
std::atomic<int> g_shaderCacheHits{0}, g_shaderCacheMisses{0};
static uint64_t fnv1a(const std::string& s, uint64_t h = 1469598103934665603ull) {
  for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
  return h;
}
static bool binaryCacheUsable() {
  if (g_shaderCacheDir.empty() || !glGetProgramBinary || !glProgramBinary || !glProgramParameteri || !glGetIntegerv) return false;
  GLint n = 0; glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &n);
  return n > 0;
}
static std::string cachePath(const std::string& vs, const std::string& fs) {
  auto str = [](GLenum e) { const GLubyte* s = glGetString(e); return std::string(s ? (const char*)s : "?"); };
  uint64_t h = fnv1a(vs); h = fnv1a("\x1f" + fs, h);
  h = fnv1a(str(GL_VENDOR) + "|" + str(GL_RENDERER) + "|" + str(GL_VERSION), h);
  char name[40]; snprintf(name, sizeof name, "%016llx.bin", (unsigned long long)h);
  return g_shaderCacheDir + "/" + name;
}
// <cache>/compile.log: a line as each program's build starts and ends, written as it happens (the start-up child
// process and the game append to the same file). On a driver whose compiler fails or never finishes, its last lines
// say on what, and how long each took.
static std::mutex s_logMu;
static void compileLog(const std::string& line) {
  if (g_shaderCacheDir.empty()) return;
  static const auto t0 = std::chrono::steady_clock::now();
  std::lock_guard<std::mutex> lk(s_logMu);
  const std::string path = g_shaderCacheDir + "/compile.log";
  static bool first = true;
  if (first) {   // (one launch's worth at a time: the file starts again when it has grown past 256 KB)
    first = false;
    if (FILE* f = fopen(path.c_str(), "rb")) { fseek(f, 0, SEEK_END); long n = ftell(f); fclose(f); if (n > 256 * 1024) remove(path.c_str()); }
  }
  static bool head = true;
  if (FILE* f = fopen(path.c_str(), "a")) {
    if (head) {   // (each process's lines start with the driver they ran on)
      head = false;
      const GLubyte* r = glGetString(GL_RENDERER); const GLubyte* v = glGetString(GL_VERSION);
      fprintf(f, "---- %s / %s\n", r ? (const char*)r : "?", v ? (const char*)v : "?");
    }
    fprintf(f, "%7.1f s  %s\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), line.c_str());
    fclose(f);
  }
}
static thread_local std::string g_compileWhat;   // what linkProgramCached is building (for compile.log)
static GLuint linkOnce(const std::string& vs, const std::string& fs, std::string& err, bool& rejectedBefore, const char* variant) {
  bool cache = binaryCacheUsable();
  std::string path = cache ? cachePath(vs, fs) : std::string();
  if (cache) {
    if (FILE* f = fopen(path.c_str(), "rb")) {
      char magic[4] = {}; uint32_t fmt = 0, len = 0;
      bool ok = fread(magic, 1, 4, f) == 4 && memcmp(magic, "AXSC", 4) == 0 && fread(&fmt, 4, 1, f) == 1 && fread(&len, 4, 1, f) == 1 && len > 0 && len < (64u << 20);
      std::vector<char> data(ok ? len : 0);
      ok = ok && fread(data.data(), 1, len, f) == len;
      fclose(f);
      if (ok) {
        GLuint p = glCreateProgram();
        glProgramBinary(p, (GLenum)fmt, data.data(), (GLsizei)len);
        GLint linked = 0; glGetProgramiv(p, GL_LINK_STATUS, &linked);
        if (linked) { g_shaderCacheHits++; return p; }
        glDeleteProgram(p);   // stale or rejected: rebuild below
      }
    }
  }
  // (a source this driver's compiler rejected before: its failure is not compiled again - see linkProgramCached)
  if (cache) {
    if (FILE* f = fopen((path + ".rej").c_str(), "rb")) {
      char log[8192]; size_t n = fread(log, 1, sizeof(log) - 1, f); log[n] = 0; fclose(f);
      rejectedBefore = true; err += log; return 0;
    }
    // (a retry an earlier attempt started and never finished - the driver hung on it, or the process was stopped: not again)
    if (variant) {
      if (FILE* f = fopen((path + ".try").c_str(), "rb")) {
        fclose(f); rejectedBefore = true; err += "(0) : fatal error C9999: an earlier attempt at this build did not finish\n";
        compileLog("skipped " + g_compileWhat + " with " + variant + ": an earlier attempt did not finish");
        return 0;
      }
      if (FILE* f = fopen((path + ".try").c_str(), "wb")) fclose(f);
    }
  }
  static const bool timed = getenv("SHADERTIME") != nullptr;   // (debug: each program's compile and link time, and the start of its entry point)
  if (const char* dd = getenv("SHADERDUMP")) {   // (debug: each program's sources, numbered, to time and cut down outside the game)
    static int dumped = 0; char nm[512];
    for (int k = 0; k < 2; k++) {
      snprintf(nm, sizeof(nm), "%s/%02d.%s", dd, dumped, k ? "fs" : "vs");
      if (FILE* fo = fopen(nm, "wb")) { const std::string& src = k ? fs : vs; fwrite(src.data(), 1, src.size(), fo); fclose(fo); }
    }
    dumped++;
  }
  auto t0 = std::chrono::steady_clock::now();
  const size_t err0 = err.size();
  const std::string what = g_compileWhat + (variant ? std::string(" with ") + variant : std::string());
  compileLog("building " + what);
  GLuint v = compile(GL_VERTEX_SHADER, vs, err), f = compile(GL_FRAGMENT_SHADER, fs, err);
  auto secs = [&]() { char b[32]; snprintf(b, sizeof b, "%.1f s", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count()); return std::string(b); };
  auto reject = [&]() {   // (an internal error of the driver's compiler: remembered, see linkProgramCached)
    std::string l = err.substr(err0); size_t c = l.find("error"); l = c == std::string::npos ? l.substr(0, 160) : l.substr(c, 160);
    for (char& ch : l) if (ch == '\n' || ch == '\r') ch = ' ';
    compileLog("FAILED " + what + " after " + secs() + ": " + l);
    if (cache && variant) remove((path + ".try").c_str());
    if (!cache || err.find("C9999", err0) == std::string::npos) return;
    if (FILE* fo = fopen((path + ".rej").c_str(), "wb")) { fwrite(err.data() + err0, 1, err.size() - err0, fo); fclose(fo); }
  };
  if (!v || !f) { if (v) glDeleteShader(v); if (f) glDeleteShader(f); reject(); return 0; }
  GLuint p = glCreateProgram(); glAttachShader(p, v); glAttachShader(p, f);
  if (cache) glProgramParameteri(p, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, 1);
  glLinkProgram(p);
  GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (timed) {
    size_t m = fs.rfind("void main()"); std::string tag = m == std::string::npos ? fs.substr(0, 60) : fs.substr(m, 90);
    for (char& c : tag) if (c == '\n') c = ' ';
    fprintf(stderr, "shader %6.1f s  fs %7zu bytes  %s\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), fs.size(), tag.c_str());
  }
  if (!ok) { char log[8192]; glGetProgramInfoLog(p, sizeof(log), nullptr, log); err += log; glDeleteProgram(p); glDeleteShader(v); glDeleteShader(f); reject(); return 0; }
  glDeleteShader(v); glDeleteShader(f);
  g_shaderCacheMisses++;
  compileLog("built " + what + " in " + secs());
  if (cache && variant) remove((path + ".try").c_str());
  if (cache) {
    GLint len = 0; glGetProgramiv(p, GL_PROGRAM_BINARY_LENGTH, &len);
    if (len > 0) {
      std::vector<char> data(len); GLsizei got = 0; GLenum fmt = 0;
      glGetProgramBinary(p, len, &got, &fmt, data.data());
      if (got > 0) {
        std::string tmp = path + ".tmp";   // written whole, then renamed: a crash never leaves a torn file
        if (FILE* fo = fopen(tmp.c_str(), "wb")) {
          uint32_t f32 = fmt, l32 = (uint32_t)got;
          bool w = fwrite("AXSC", 1, 4, fo) == 4 && fwrite(&f32, 4, 1, fo) == 1 && fwrite(&l32, 4, 1, fo) == 1 && fwrite(data.data(), 1, got, fo) == (size_t)got;
          fclose(fo);
          remove(path.c_str());
          if (!w || rename(tmp.c_str(), path.c_str()) != 0) remove(tmp.c_str());
        }
      }
    }
  }
  return p;
}
// A program the driver's compiler fails on with an internal error is built again with that compiler's own options, one
// set after another, until one builds. NVIDIA's has failed so ("fatal error C9999: Unhandled expr op assign/(182) in
// CreateDag", v3.35.0 and v3.36.0) on programs every other driver builds; #pragma optionNV is its own (the options of
// its offline compiler, cgc) and other drivers ignore it. The cache keeps the set that built under its own source, and
// remembers the rejection (<hash>.rej, the driver's log), so a later launch loads that build without compiling the
// failure again. g_shaderNotes says what was rejected and what built instead (startup.log).
std::string g_shaderNotes;
static std::mutex s_notesMu;
static const char* const kNvOptionSets[] = {
  "#pragma optionNV(ifcvt none)\n",
  "#pragma optionNV(unroll none)\n",
  "#pragma optionNV(inline all)\n",
  "#pragma optionNV(inline all)\n#pragma optionNV(ifcvt none)\n#pragma optionNV(unroll none)\n",
};
static std::atomic<int> s_nvFirstSet{0};      // the set that last built: tried first for the next rejected program
static std::atomic<bool> s_nvUseless{false};  // every set failed on a program, or the retries ran out of time: no more
static std::atomic<int> s_nvSpentMs{0};       // the time this process has spent on retries (at most kNvBudgetMs)
static const int kNvBudgetMs = 150000, kNvSlowMs = 60000;   // (a retry slower than kNvSlowMs ends them too)
void shaderNote(const std::string& s) { std::lock_guard<std::mutex> lk(s_notesMu); g_shaderNotes += s; if (s.empty() || s.back() != '\n') g_shaderNotes += "\n"; }
static std::string firstLine(const std::string& s) { size_t n = s.find('\n'); return n == std::string::npos ? s : s.substr(0, n); }
static std::string oneLine(std::string s) { for (char& c : s) if (c == '\n') c = ' '; while (!s.empty() && s.back() == ' ') s.pop_back(); return s; }
static std::string programName(const std::string& fs) {   // (the build stage, or the start of the entry point, to name it)
  const std::string stage = g_ren.compileStage();
  if (!stage.empty()) return stage;
  size_t m = fs.rfind("void main()"); std::string t = m == std::string::npos ? fs.substr(0, 60) : fs.substr(m, 70);
  for (char& c : t) if (c == '\n') c = ' ';
  for (size_t k; (k = t.find("  ")) != std::string::npos;) t.erase(k, 1);
  return t;
}
GLuint linkProgramCached(const std::string& vs, const std::string& fs, std::string& err) {
  bool before = false;
  const size_t err0 = err.size();
  const std::string name = programName(fs);
  g_compileWhat = name;
  GLuint p = linkOnce(vs, fs, err, before, nullptr);
  if (p || err.find("C9999", err0) == std::string::npos) return p;
  const std::string log = err.substr(err0);
  const size_t c = log.find("C9999"), ls = c == std::string::npos ? std::string::npos : log.rfind('\n', c);
  const std::string why = firstLine(c == std::string::npos ? log : log.substr(ls == std::string::npos ? 0 : ls + 1));   // (the driver's error line)
  if (s_nvUseless) { shaderNote("Shader [" + name + "]: the driver's compiler failed" + (before ? " (an earlier launch)" : "") + ": " + why); return 0; }
  const size_t at = fs.find('\n') + 1;   // (after the #version line)
  const int n = (int)(sizeof(kNvOptionSets) / sizeof(kNvOptionSets[0])), first = s_nvFirstSet;
  const std::string stage = g_ren.compileStage();
  for (int k = 0; k < n && !s_nvUseless; k++) {
    const int set = (first + k) % n;
    const std::string opts = oneLine(kNvOptionSets[set]);
    g_ren.setCompileStage((stage + " - the driver's compiler failed, retrying with its options (" + std::to_string(k + 1) + " of " + std::to_string(n) + ")").c_str());
    std::string e2; bool b2 = false;
    const auto t0 = std::chrono::steady_clock::now();
    p = linkOnce(vs, fs.substr(0, at) + kNvOptionSets[set] + fs.substr(at), e2, b2, opts.c_str());
    const int ms = (int)(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() * 1000.0);
    s_nvSpentMs += ms;
    if (p) {
      s_nvFirstSet = set;
      g_ren.setCompileStage(stage.c_str());
      shaderNote("Shader [" + name + "]: the driver's compiler failed" + (before ? " (an earlier launch)" : "") + " - built with " + opts + "\n  " + why);
      err.resize(err0);
      return p;
    }
    if (ms > kNvSlowMs || s_nvSpentMs > kNvBudgetMs) { s_nvUseless = true; compileLog("retries stopped: too slow"); }
  }
  g_ren.setCompileStage(stage.c_str());
  s_nvUseless = true;
  shaderNote("Shader [" + name + "]: the driver's compiler failed" + (before ? " (an earlier launch)" : "") + ", also with its options: " + why);
  return 0;
}
static GLuint program(const std::string& vs, const std::string& fs, std::string& err) { return linkProgramCached(vs, fs, err); }
// Fingerprint of every shader source and of the driver (needs a current context): the platform layer stamps the
// shader cache with it, so it knows without compiling anything whether the cache holds this build's programs
std::string shaderCacheStamp() {
  uint64_t h = 1469598103934665603ull;
  for (const char* src : {kFullscreenVS, kCommonGLSL, kRtIO, kSceneUniforms, kPlaneCommon, kPlaneParts, kPlaneSDF, kPlaneTrace, kTerrainTrace, kMaterialCommon, kLightCommon, kClouds, kTerrainMaterial, kRaytraceUfo, kRaytraceText, kRaytraceDisplays, kRtPrims, kPlaneScreens, kFeeds, kPlaneFx, kWraithSDF, kWraithMaterial, kWraithFx, kWraithCockpitCommon, kCabinWindows, kWraithCockpitSDF, kWraithCockpitMaterial, kPlaneMaterial, kWater, kViewUniforms, kNoiseTex, kGBuffer, kGBWrite, kTerrainVS, kTerrainFS, kWaterVS, kWaterFS, kLightFS, kMapMain, kDispMain, kSpriteVS, kSpriteFS, kDownFS, kUpFS, kRayMaskFS, kRayFS, kFeedRaysFS, kTaaFS, kPostFS, kUIVS, kUIFS, kEntVS, kEntFS1, kEntFS2, kEntShadowFS, kCloudMain, kCloudCompFS, kHullBakeMain, kTShBakeMain, kAfShMap}) h = fnv1a(src, h);
  auto str = [](GLenum e) { const GLubyte* s = glGetString(e); return std::string(s ? (const char*)s : "?"); };
  h = fnv1a(str(GL_VENDOR) + "|" + str(GL_RENDERER) + "|" + str(GL_VERSION), h);
  char b[24]; snprintf(b, sizeof b, "%016llx", (unsigned long long)h);
  return b;
}

// The aircraft bodies' cache key: only the sources the mesh bake's results come from (the aircraft fields, their
// normals and cabin occlusion, the bake's own main) and the driver, so an update that changes the terrain, the
// lighting or the UI keeps every built body (each costs seconds on the GPU; a launch builds about twenty)
std::string meshCacheStamp() {
  uint64_t h = 1469598103934665603ull;
  for (const char* src : {kCommonGLSL, kRtIO, kViewUniforms, kSceneUniforms, kPlaneCommon, kPlaneParts, kPlaneSDF, kPlaneTrace, kWraithSDF, kWraithCockpitCommon, kWraithCockpitSDF, kHullBakeMain}) h = fnv1a(src, h);
  auto str = [](GLenum e) { const GLubyte* s = glGetString(e); return std::string(s ? (const char*)s : "?"); };
  h = fnv1a(str(GL_VENDOR) + "|" + str(GL_RENDERER) + "|" + str(GL_VERSION), h);
  char b[24]; snprintf(b, sizeof b, "%016llx", (unsigned long long)h);
  return b;
}

// Cloud noise baked once into tileable textures, so the cloud march samples them with the GPU's texture filtering
// instead of evaluating hashed value noise per sample: a 1024^2 coverage map (4 octaves, period 16 coverage units =
// 83 km) and a 128^3 smooth value-noise volume (period 32 lattice cells) used at every billow and detail scale.
void Renderer::genCloudNoise() {
  auto hp = [](int x, int y, int z, int P) {
    x = ((x % P) + P) % P; y = ((y % P) + P) % P; z = ((z % P) + P) % P;
    // Hash mixing deliberately wraps at 32 bits; signed products would be undefined at these cell sizes.
    uint32_t hx = uint32_t(x)*73856093u ^ (uint32_t(z)*19349663u) ^ (uint32_t(P)*7919u);
    uint32_t hy = uint32_t(y)*83492791u + uint32_t(z)*2971u;
    return hash2i(static_cast<int32_t>(hx), static_cast<int32_t>(hy));
  };
  auto s3 = [](float t) { return t * t * (3.f - 2.f * t); };
  auto vn2 = [&](float x, float y, int P) {
    int ix = (int)floorf(x), iy = (int)floorf(y); float fx = s3(x - ix), fy = s3(y - iy);
    return lerpf(lerpf(hp(ix, iy, 0, P), hp(ix + 1, iy, 0, P), fx), lerpf(hp(ix, iy + 1, 0, P), hp(ix + 1, iy + 1, 0, P), fx), fy);
  };
  const int CN = 1024;
  std::vector<uint8_t> cov((size_t)CN * CN);
  parallelFor(CN, [&](int y) {
    for (int x = 0; x < CN; x++) {
      float qx = (x + 0.5f) / CN * 16.f, qy = (y + 0.5f) / CN * 16.f, s = 0, a = 0.5f; int P = 16;
      for (int o = 0; o < 4; o++) { s += a * vn2(qx, qy, P); qx *= 2; qy *= 2; P *= 2; a *= 0.5f; }
      cov[(size_t)y * CN + x] = (uint8_t)std::min(255.f, s / 0.9375f * 255.f + 0.5f);
    }
  });
  if (!texCloudCov) glGenTextures(1, &texCloudCov);
  glBindTexture(GL_TEXTURE_2D, texCloudCov);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, CN, CN, 0, GL_RED, GL_UNSIGNED_BYTE, cov.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  const int VN = 128, VP = 32;   // 4 texels per lattice cell
  std::vector<uint8_t> vol((size_t)VN * VN * VN);
  parallelFor(VN, [&](int z) {
    for (int y = 0; y < VN; y++)
      for (int x = 0; x < VN; x++) {
        float px = (x + 0.5f) / VN * VP, py = (y + 0.5f) / VN * VP, pz = (z + 0.5f) / VN * VP;
        int ix = (int)floorf(px), iy = (int)floorf(py), iz = (int)floorf(pz);
        float fx = s3(px - ix), fy = s3(py - iy), fz = s3(pz - iz);
        auto h = [&](int a, int b, int c) { return hp(ix + a, iy + b, iz + c, VP); };
        float v = lerpf(lerpf(lerpf(h(0, 0, 0), h(1, 0, 0), fx), lerpf(h(0, 1, 0), h(1, 1, 0), fx), fy),
                        lerpf(lerpf(h(0, 0, 1), h(1, 0, 1), fx), lerpf(h(0, 1, 1), h(1, 1, 1), fx), fy), fz);
        vol[((size_t)z * VN + y) * VN + x] = (uint8_t)(v * 255.f + 0.5f);
      }
  });
  if (!texNoise3) glGenTextures(1, &texNoise3);
  glBindTexture(GL_TEXTURE_3D, texNoise3);
  glTexImage3D(GL_TEXTURE_3D, 0, GL_R8, VN, VN, VN, 0, GL_RED, GL_UNSIGNED_BYTE, vol.data());
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT); glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

// The sea's wave bands (water.glsl): three tileable maps of a wind-driven sea's slopes and heights. Each is a Phillips
// spectrum (for a 10 m/s wind along +x, cos^2 spread, the waves against the wind all but gone) laid on its tile's own
// lattice of wave vectors - so it tiles - and transformed by an inverse FFT; band b holds wavelengths from 1/16 to
// 1/2 of its tile (256, 32 and 4 m: 16-128 m swell, 2-16 m wind waves, 25 cm-2 m ripples). Stored per band as
// slope x, slope z (in units of 4x its rms, waveRms), height (in units of 4x its rms). The shader scrolls each band at
// its waves' phase speed down the wind and samples it twice at crossing angles, so the sea never slides as a sheet.
static void fft256(std::complex<float>* a, int stride) {   // in place, inverse (no 1/N), 256 points
  const int n = 256;
  for (int i = 1, j = 0; i < n; i++) {
    int bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(a[i * stride], a[j * stride]);
  }
  for (int len = 2; len <= n; len <<= 1) {
    const float ang = 2.f * PI / len;
    const std::complex<float> wl(cosf(ang), sinf(ang));
    for (int i = 0; i < n; i += len) {
      std::complex<float> w(1.f, 0.f);
      for (int k = 0; k < len / 2; k++) {
        std::complex<float> u = a[(i + k) * stride], v = a[(i + k + len / 2) * stride] * w;
        a[(i + k) * stride] = u + v; a[(i + k + len / 2) * stride] = u - v;
        w *= wl;
      }
    }
  }
}

void Renderer::genWaves() {
  const int N = 256;
  const float tiles[3] = {256.f, 32.f, 4.f}, U = 10.f, Lw = U * U / G0;
  std::vector<uint8_t> px((size_t)N * N * 4 * 3);
  parallelFor(3, [&](int b) {
    const float L = tiles[b];
    std::vector<std::complex<float>> sx((size_t)N * N), sz((size_t)N * N), hh((size_t)N * N);
    uint32_t rng = 0x9e3779b9u * (uint32_t)(b + 1);
    auto uni = [&]() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng & 0xffffff) / 16777216.f; };
    for (int j = 0; j < N; j++) for (int i = 0; i < N; i++) {
      const float g1 = sqrtf(-2.f * logf(std::max(uni(), 1e-7f))), g2 = 2.f * PI * uni();   // (a Gaussian pair, drawn for every
      const int m = i < N / 2 ? i : i - N, n = j < N / 2 ? j : j - N;                       //  texel so the stream stays fixed)
      const float r = sqrtf((float)(m * m + n * n));
      if (r < 2.f || r >= 16.f) continue;
      const float kx = 2.f * PI * m / L, kz = 2.f * PI * n / L, k = sqrtf(kx * kx + kz * kz), c = kx / k;
      const float P = expf(-1.f / (k * Lw * k * Lw)) / (k * k * k * k) * c * c * (c < 0.f ? 0.05f : 1.f);
      const std::complex<float> h = std::polar(g1 * sqrtf(P * 0.5f), g2);
      const size_t o = (size_t)j * N + i;
      hh[o] = h; sx[o] = std::complex<float>(0.f, kx) * h; sz[o] = std::complex<float>(0.f, kz) * h;
    }
    for (auto* f : {&sx, &sz, &hh}) {
      for (int j = 0; j < N; j++) fft256(&(*f)[(size_t)j * N], 1);
      for (int i = 0; i < N; i++) fft256(&(*f)[i], N);
    }
    double s2 = 0, h2 = 0;
    for (size_t o = 0; o < (size_t)N * N; o++) { s2 += sx[o].real() * sx[o].real() + sz[o].real() * sz[o].real(); h2 += hh[o].real() * hh[o].real(); }
    const float sr = (float)sqrt(s2 / (N * N)), hr = (float)sqrt(h2 / (N * N));
    waveRms[b] = sr;
    for (size_t o = 0; o < (size_t)N * N; o++) {
      uint8_t* q = &px[((size_t)b * N * N + o) * 4];
      auto enc = [](float v) { return (uint8_t)clampf(v * 0.5f * 255.f + 128.f, 0.f, 255.f); };
      q[0] = enc(sx[o].real() / (4.f * sr)); q[1] = enc(sz[o].real() / (4.f * sr)); q[2] = enc(hh[o].real() / (4.f * hr)); q[3] = 255;
    }
  });
  // (the bands' slope rms relative to the whole sea's: the shader scales them to the slope the wind gives)
  const float tot = sqrtf(waveRms[0] * waveRms[0] + waveRms[1] * waveRms[1] + waveRms[2] * waveRms[2]);
  for (float& r : waveRms) r /= std::max(tot, 1e-12f);
  if (!texWaves) glGenTextures(1, &texWaves);
  glBindTexture(GL_TEXTURE_2D_ARRAY, texWaves);
  glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, N, N, 3, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT); glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_ANISOTROPY, 8.0f);
  glGetError();   // (anisotropy may be unsupported)
}

// A scanned layer from assets/materials (tools/pack_materials.py): NN_name_c.jpg the albedo, NN_name_n.jpg the normal
// and height, NN_name_m.jpg the roughness (red) and ambient occlusion (green); false (the generator makes it) when
// it has none
static bool loadMaterialLayer(const std::string& dir, int l, uint8_t* alb, uint8_t* nrm) {
  if (dir.empty()) return false;
  const std::string base = dir + "/" + (l < 10 ? "0" : "") + std::to_string(l) + "_" + materialName(l) + "_";
  std::vector<uint8_t> c, n, m;
  auto rd = [&](const char* s, std::vector<uint8_t>& px) { int w = 0, h = 0; return readImage((base + s).c_str(), w, h, px) && w == kMatTS && h == kMatTS; };
  if (!rd("c.jpg", c) || !rd("n.jpg", n) || !rd("m.jpg", m)) return false;
  for (size_t i = 0; i < (size_t)kMatTS * kMatTS * 4; i += 4) {
    alb[i] = c[i]; alb[i + 1] = c[i + 1]; alb[i + 2] = c[i + 2]; alb[i + 3] = m[i];
    nrm[i] = n[i]; nrm[i + 1] = n[i + 1]; nrm[i + 2] = n[i + 2]; nrm[i + 3] = m[i + 1];
  }
  return true;
}

void Renderer::genMaterials() {
  const int L = kMatLayers, TS = kMatTS;
  std::vector<uint8_t> alb((size_t)TS * TS * 4 * L), nrm((size_t)TS * TS * 4 * L);
  std::atomic<int> scanned{0};
  parallelFor(L, [&](int l) {   // the layers are independent: one per core at a time
    uint8_t* a = &alb[(size_t)l * TS * TS * 4]; uint8_t* n = &nrm[(size_t)l * TS * TS * 4];
    if (loadMaterialLayer(matDir, l, a, n)) scanned++;
    else materialProcLayer(l, a, n);
  });
  matScanned = scanned;
  auto up = [&](GLuint& tex, std::vector<uint8_t>& d) {
    glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D_ARRAY, tex);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, TS, TS, L, 0, GL_RGBA, GL_UNSIGNED_BYTE, d.data());
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_ANISOTROPY, 8.0f);
    glGetError();  // anisotropy may be unsupported
  };
  up(texAlb, alb); up(texNrm, nrm);
}

void Renderer::genMinimap() {
  const int N = 1024;
  std::vector<uint8_t> img((size_t)N * N * 4);
  parallelFor(N, [&](int j) { for (int i = 0; i < N; i++) {
    float x = -WORLD_HALF + (i + 0.5f) * 2 * WORLD_HALF / N, z = -WORLD_HALF + (j + 0.5f) * 2 * WORLD_HALF / N;
    float h = g_world.groundHeight(x, z, 5);
    float hx = g_world.groundHeight(x + 80, z, 5) - h;
    float b[4]; g_world.sampleBase(x, z, b);
    vec3 c;
    if (h < 0) { float d = clampf(-h / 60.f, 0, 1); c = lerp(vec3(0.20f, 0.55f, 0.62f), vec3(0.05f, 0.16f, 0.30f), d); }
    else {
      c = lerp(vec3(0.38f, 0.58f, 0.30f), vec3(0.30f, 0.50f, 0.28f), b[2]);
      c = lerp(c, vec3(0.62f, 0.56f, 0.42f), smoothstepf(300, 1100, h));
      c = lerp(c, vec3(0.92f, 0.93f, 0.95f), smoothstepf(lerpf(1700, 400, b[3]), lerpf(1900, 600, b[3]), h));
      float shade = clampf(1.f - hx * 0.02f, 0.6f, 1.3f);
      c = c * shade;
      if (h < 4) c = lerp(c, vec3(0.85f, 0.80f, 0.62f), 0.6f);
    }
    float mk[4]; g_world.sampleMask(x, z, mk);
    if (h > 0 && mk[1] > 0.05f) c = lerp(c, vec3(0.78f, 0.72f, 0.66f), smoothstepf(0.05f, 0.4f, mk[1]));
    if (h > 0 && mk[3] > 0.2f) c = lerp(c, vec3(0.62f, 0.62f, 0.32f), mk[3] * 0.35f);
    if (mk[0] * ROAD_RANGE < 14.f) c = vec3(0.35f, 0.33f, 0.3f);
    if (g_world.onRunway(x, z, 40) >= 0) c = vec3(0.12f, 0.12f, 0.14f);
    size_t o = ((size_t)j * N + i) * 4;
    img[o] = (uint8_t)(clampf(c.x, 0, 1) * 255); img[o + 1] = (uint8_t)(clampf(c.y, 0, 1) * 255); img[o + 2] = (uint8_t)(clampf(c.z, 0, 1) * 255); img[o + 3] = 255;
  } });
  glGenTextures(1, &minimapTex); glBindTexture(GL_TEXTURE_2D, minimapTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, N, N, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.data());
  glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

// The UI program, its vertex array and the font: enough to draw the intro screen while everything else is built
bool Renderer::initUI(int w, int h) {
  if (progUI) return true;
  progUI = program(kUIVS, kUIFS, error);
  if (!progUI) { error = "UI shader: " + error; return false; }
  glGenVertexArrays(1, &vaoUI); glGenBuffers(1, &vboUI);
  glBindVertexArray(vaoUI); glBindBuffer(GL_ARRAY_BUFFER, vboUI);
  glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UIVert), (void*)0);
  glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(UIVert), (void*)8);
  glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(UIVert), (void*)16);
  glEnableVertexAttribArray(3); glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(UIVert), (void*)32);
  glBindVertexArray(0);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glGenTextures(1, &texFont); glBindTexture(GL_TEXTURE_2D, texFont);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, FONT_W, FONT_H, 0, GL_RED, GL_UNSIGNED_BYTE, FONT_PIX);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  W = w; H = h;
  return true;
}

GLuint Renderer::makeTexture(const uint8_t* rgba, int w, int h) {
  GLuint t = 0;
  glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return t;
}

// Exact pass timing for the analysis tool: wait for the GPU at every pass boundary
static_assert(Renderer::kPasses == 11, "passWall holds kPasses entries");
void Renderer::syncStamp(int i) {
  glFinish();
  auto now = std::chrono::steady_clock::now();
  if (i > 0 && i <= kPasses) passWall[i - 1] = std::chrono::duration<double, std::milli>(now - syncT).count();
  syncT = now;
}

// Compiles and links every scene program (or loads it from the binary cache). Touches only shader and program
// objects, which are shared between contexts, so the platform layer can run it on a worker thread with its own
// context while the intro screen animates. `done` counts finished programs (kProgramCount in all).
bool Renderer::compilePrograms(std::atomic<int>* done) {
  auto step = [&]() { if (done) done->fetch_add(1); };
  std::string vsFS = kFullscreenVS;
  std::string hdr = "#version 330 core\n";
  setCompileStage("scenery objects");
  progEnt = program(hdr + kEntVS, hdr + kEntFS1 + kEntFS2, error); step();
  progEntSh = program(hdr + kEntVS, hdr + kEntFS1 + kEntShadowFS, error); step();
  if (!progEnt || !progEntSh) { error = "Entity shader: " + error; return false; }
  setCompileStage("particles and sprites");
  progSprite = program(kSpriteVS, kSpriteFS, error); step();
  setCompileStage("bloom and light shafts");
  progDown = program(vsFS, kDownFS, error); step();
  progUp = program(vsFS, kUpFS, error); step();
  progRayMask = program(vsFS, kRayMaskFS, error); step();
  progRay = program(vsFS, kRayFS, error); step();
  progFeedRays = program(vsFS, kFeedRaysFS, error); step();
  setCompileStage("post-processing and anti-aliasing");
  progPost = program(vsFS, kPostFS, error); step();
  progTAA = program(vsFS, kTaaFS, error); step();
  if (!progSprite || !progDown || !progUp || !progRayMask || !progRay || !progPost || !progTAA) { error = "Shader: " + error; return false; }
  {   // the programs built on the shared scene library (shaders.h worldLibAssembly), each with its own main: the GPS
    // aerial imagery, the terrain-shadow bake, the clouds, the hull and mesh bakes, the cockpit display atlases
    std::string ms = worldLibAssembly(getenv("CLIPDBG") ? "#define WR_CLIPDEBUG\n" : "");
    setCompileStage("the GPS map");
    progMap = program(vsFS, ms + kMapMain, error); step();
    setCompileStage("terrain shadows");
    { std::string e; progTShBake = program(vsFS, ms + kTShBakeMain, e); step(); }   // optional: without it the terrain casts no sun shadow
    setCompileStage("clouds");
    { std::string e; progClouds = program(vsFS, ms + kCloudMain, e); step(); }       // optional: without them no clouds
    { std::string e; progCloudComp = program(vsFS, kCloudCompFS, e); step(); }
    if (!progMap) { error = "Map shader: " + error; return false; }
    setCompileStage("the aircraft mesh builder");
    compileHull(vsFS, worldLibAssembly(std::string(getenv("CLIPDBG") ? "#define WR_CLIPDEBUG\n" : "") + "#define PART_BAKE\n") + kHullBakeMain); step();   // (the bake alone evaluates a part by its id: PART_BAKE)
    setCompileStage("cockpit displays");
    progDisp = program(vsFS, ms + kDispMain, error); step();
    // not fatal: without it the cockpit screens stay dark, but the game still runs (the error goes to startup.log)
    if (!progDisp) { dispError = error; error.clear(); }
    setCompileStage("the renderer: aircraft, terrain, water and lighting");
    if (!compileRaster()) return false;   // (the renderer itself: its error names the program)
    step(); step(); step();
  }
  glFinish();   // everything complete before another context uses the programs
  return true;
}

// Terrain sun shadow, baked in world space for the current sun direction (see kTShBakeMain): a band of rows a frame
// into the back texture, swapped in when complete; a new bake starts only when the sun has moved ~0.25 degrees
void Renderer::bakeTerrainShadow(const FrameParams& fp) {
  if (!progTShBake || fp.sunDir.y < 0.02f) return;
  vec3 L = normalize(fp.sunDir);
  if (!tshBaking) {
    if (tshFront >= 0 && dot(L, tshSun) > 0.99999f) return;
    tshBaking = true; tshRow = 0; tshBakeSun = L; tshBack = tshFront < 0 ? 0 : 1 - tshFront;
  }
  GLuint& tex = texTSh[tshBack];
  if (!tex) {
    glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, kTShN, kTShN, 0, GL_RG, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
  if (!fboTSh) glGenFramebuffers(1, &fboTSh);
  glBindFramebuffer(GL_FRAMEBUFFER, fboTSh);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
  GLenum b0 = GL_COLOR_ATTACHMENT0; glDrawBuffers(1, &b0);
  glViewport(0, 0, kTShN, kTShN);
  // the very first bake in one go (it happens while loading), and the rest of one whenever the screen is black (the
  // menu tour's cuts), so a new place never shows the last one's shadows
  int rows = tshFront < 0 || fp.fade < 0.02f ? kTShN - tshRow : kTShRows;
  glEnable(GL_SCISSOR_TEST); glScissor(0, tshRow, kTShN, rows);
  glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
  GLuint p = progTShBake;
  glUseProgram(p);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texHM); glUniform1i(U(p, "uHM"), 0);
  glActiveTexture(GL_TEXTURE0 + 6); glBindTexture(GL_TEXTURE_2D, texHMax); glUniform1i(U(p, "uHMax"), 6);
  glActiveTexture(GL_TEXTURE0);
  glUniform1i(U(p, "uCraterN"), 0); glUniform1f(U(p, "uMaxH"), maxH);
  glUniform3f(U(p, "uBakeSun"), tshBakeSun.x, tshBakeSun.y, tshBakeSun.z);
  glUniform1f(U(p, "uBakeN"), (float)kTShN);
  glBindVertexArray(vaoEmpty);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glDisable(GL_SCISSOR_TEST);
  tshRow += rows;
  if (tshRow >= kTShN) { tshFront = tshBack; tshSun = tshBakeSun; tshBaking = false; }
}

// Cockpit display atlas for this frame: the research jets' display pages, or the light aircraft's instrument panel
void Renderer::renderDisplays(const FrameParams& fp, bool panel) {
  GLuint& tex = panel ? texPanel : texPages;
  if (!progDisp && tex) return;   // no display shader: the screens stay as cleared below (dark)
  // 1080 lines on every display: each research-jet page is a 1080 x 1080 cell of the 4 x 2 atlas, and the light
  // aircraft's instrument panel is 1080 texels tall (0.58 x 0.22 m)
  int w = panel ? 2848 : 4320, h = panel ? 1080 : 2160;
  if (!tex) {
    glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // (seen from the seat the panel is a few hundred pixels tall and at an angle: anisotropic filtering, and a little
    // sharper than the mip chain alone, so the readouts and dial markings stay crisp - the TAA settles the rest)
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, -0.5f);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, 8.0f);
    glGetError();  // anisotropy may be unsupported
    glGenerateMipmap(GL_TEXTURE_2D);
  }
  if (!fboDisp) glGenFramebuffers(1, &fboDisp);
  glBindFramebuffer(GL_FRAMEBUFFER, fboDisp);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
  glViewport(0, 0, w, h); glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
  if (!progDisp) {
    glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, tex); glGenerateMipmap(GL_TEXTURE_2D);
    return;
  }
  GLuint p = progDisp;
  glUseProgram(p);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texHM); glUniform1i(U(p, "uHM"), 0);
  glActiveTexture(GL_TEXTURE0 + 15); glBindTexture(GL_TEXTURE_2D, texFont); glUniform1i(U(p, "uFontTex"), 15);
  const PlaneVisual& pv = fp.plane;
  glUniform3f(U(p, "uPlanePos"), pv.pos.x, pv.pos.y, pv.pos.z);
  glUniformMatrix3fv(U(p, "uPlaneRot"), 1, GL_FALSE, pv.rot);
  glUniform4fv(U(p, "uHud"), 1, pv.hud); glUniform4fv(U(p, "uHud2"), 1, pv.hud2); glUniform4fv(U(p, "uHud3"), 1, pv.hud3);
  glUniform4fv(U(p, "uI0"), 1, pv.I0); glUniform4fv(U(p, "uI1"), 1, pv.I1); glUniform4fv(U(p, "uI2"), 1, pv.I2);
  glUniform1f(U(p, "uTime"), fp.time); glUniform1i(U(p, "uCraterN"), 0);
  glUniform4f(U(p, "uDispMode"), panel ? 1.f : 0.f, (float)fp.dispCk, 0, 0);
  glUniform1i(U(p, "uDisplayEngines"), pv.model == kMantis ? 1 : 2);
  glUniform4fv(U(p, "uFlame"), 1, pv.flame);
  glUniform2f(U(p, "uDispRes"), (float)w, (float)h);
  glBindVertexArray(vaoEmpty);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glActiveTexture(GL_TEXTURE0);   // (not unit 15, which holds the font)
  glBindTexture(GL_TEXTURE_2D, tex); glGenerateMipmap(GL_TEXTURE_2D);
}

// Renders the GPS aerial image: half x half metres around (cx, cz), north (-z) at the top, into texMap
void Renderer::renderMap(float cx, float cz, float half, int N) {
  if (!progMap) return;
  if (!texMap || mapN != N) {
    if (texMap) glDeleteTextures(1, &texMap);
    glGenTextures(1, &texMap); glBindTexture(GL_TEXTURE_2D, texMap);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, N, N, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_2D);
    if (!fboMap) glGenFramebuffers(1, &fboMap);
    glBindFramebuffer(GL_FRAMEBUFFER, fboMap);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texMap, 0);
    mapN = N;
  }
  glBindFramebuffer(GL_FRAMEBUFFER, fboMap);
  glViewport(0, 0, N, N); glDisable(GL_BLEND);
  GLuint p = progMap;
  glUseProgram(p);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texHM); glUniform1i(U(p, "uHM"), 0);
  glActiveTexture(GL_TEXTURE0 + 1); glBindTexture(GL_TEXTURE_2D_ARRAY, texAlb); glUniform1i(U(p, "uAlb"), 1);
  glActiveTexture(GL_TEXTURE0 + 2); glBindTexture(GL_TEXTURE_2D_ARRAY, texNrm); glUniform1i(U(p, "uNrm"), 2);
  glActiveTexture(GL_TEXTURE0 + 3); glBindTexture(GL_TEXTURE_2D, texMask); glUniform1i(U(p, "uMask"), 3);
  glActiveTexture(GL_TEXTURE0 + 4); glBindTexture(GL_TEXTURE_2D, texRoadId); glUniform1i(U(p, "uRoadId"), 4);
  glActiveTexture(GL_TEXTURE0 + 5); glBindTexture(GL_TEXTURE_2D, texData); glUniform1i(U(p, "uData"), 5);
  glActiveTexture(GL_TEXTURE0 + 6); glBindTexture(GL_TEXTURE_2D, texHMax); glUniform1i(U(p, "uHMax"), 6);
  glActiveTexture(GL_TEXTURE0 + 13); glBindTexture(GL_TEXTURE_2D, texCloudCov); glUniform1i(U(p, "uCloudCov"), 13);
  glActiveTexture(GL_TEXTURE0 + 14); glBindTexture(GL_TEXTURE_3D, texNoise3); glUniform1i(U(p, "uNoise3"), 14);
  {
    int n = std::min(16, (int)g_world.airports.size());
    float ap[64], dim[64];
    for (int i = 0; i < n; i++) {
      const Airport& a = g_world.airports[i];
      ap[i * 4] = a.x; ap[i * 4 + 1] = a.z; ap[i * 4 + 2] = a.elev; ap[i * 4 + 3] = a.heading * DEG;
      dim[i * 4] = a.length; dim[i * 4 + 1] = a.width; dim[i * 4 + 2] = (float)a.surface; dim[i * 4 + 3] = (float)a.size;
    }
    glUniform1i(U(p, "uApCount"), n); glUniform4fv(U(p, "uAp"), n, ap); glUniform4fv(U(p, "uApDim"), n, dim);
    glUniform1i(U(p, "uBoxCount"), std::min(128, (int)g_world.boxes.size()));
  }
  glUniform1i(U(p, "uCraterN"), 0); glUniform1f(U(p, "uMaxH"), maxH); glUniform1i(U(p, "uQuality"), 2);
  glUniform1f(U(p, "uTime"), 0.f); glUniform1f(U(p, "uWet"), 0.f); glUniform1f(U(p, "uSnow"), 0.f);
  glUniform4f(U(p, "uMapView"), cx, cz, half, 2.f * half / N);
  glUniform2f(U(p, "uMapRes"), (float)N, (float)N);
  glBindVertexArray(vaoEmpty);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindTexture(GL_TEXTURE_2D, texMap); glGenerateMipmap(GL_TEXTURE_2D);
  glActiveTexture(GL_TEXTURE0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0); glViewport(0, 0, W, H);
}

bool Renderer::init(int w, int h) {
  if (!initUI(w, h)) return false;
  if (!progTAA && !compilePrograms(nullptr)) return false;

  glGenVertexArrays(1, &vaoEmpty);
  glGenVertexArrays(1, &vaoSprite); glGenBuffers(1, &vboSprite);
  glBindVertexArray(vaoSprite); glBindBuffer(GL_ARRAY_BUFFER, vboSprite);
  glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SpriteVert), (void*)0);
  glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(SpriteVert), (void*)12);
  glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(SpriteVert), (void*)20);
  glEnableVertexAttribArray(3); glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(SpriteVert), (void*)36);
  glEnableVertexAttribArray(4); glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(SpriteVert), (void*)44);
  glBindVertexArray(0);

  // heightmap
  glGenTextures(1, &texHM); glBindTexture(GL_TEXTURE_2D, texHM);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, HM_N, HM_N, 0, GL_RGBA, GL_FLOAT, g_world.hm.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glGenTextures(1, &texMask); glBindTexture(GL_TEXTURE_2D, texMask);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, MASK_N, MASK_N, 0, GL_RGBA, GL_UNSIGNED_BYTE, g_world.mask.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glGenTextures(1, &texRoadId); glBindTexture(GL_TEXTURE_2D, texRoadId);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, MASK_N, MASK_N, 0, GL_RG, GL_UNSIGNED_BYTE, g_world.roadId.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  // max-height mip chain: lets the terrain ray march skip cells it flies over
  glGenTextures(1, &texHMax); glBindTexture(GL_TEXTURE_2D, texHMax);
  for (int L = 0; L < HMAX_LEVELS; L++) glTexImage2D(GL_TEXTURE_2D, L, GL_R32F, HMAX_N >> L, HMAX_N >> L, 0, GL_RED, GL_FLOAT, g_world.hmax[L].data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, HMAX_LEVELS - 1);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glBindTexture(GL_TEXTURE_2D, texHM);
  {
    std::vector<V4> d(384, V4{0, 0, 0, 0});
    for (int i = 0; i < (int)g_roads.size() && i < 64; i++) d[i] = {g_roads[i].ax, g_roads[i].az, g_roads[i].bx, g_roads[i].bz};
    for (int i = 0; i < (int)g_world.boxes.size() && i < 128; i++) {
      const Box& b = g_world.boxes[i];
      d[64 + i] = {b.c.x, b.c.y, b.c.z, (float)b.airport};
      d[192 + i] = {b.h.x, b.h.y, b.h.z, (float)b.kind};
    }
    // [384,400) / [400,416): world bounds of each airport's buildings + their box range (lets the shader skip airports)
    d.resize(416, V4{0, 0, 0, 0});
    int nb = std::min(128, (int)g_world.boxes.size());
    for (int ap = 0; ap < 16 && ap < (int)g_world.airports.size(); ap++) {
      const Airport& A = g_world.airports[ap];
      float s = sinf(A.heading * DEG), c = cosf(A.heading * DEG);
      vec3 lo(1e9f, 1e9f, 1e9f), hi(-1e9f, -1e9f, -1e9f); int first = -1, count = 0;
      for (int i = 0; i < nb; i++) {
        const Box& b = g_world.boxes[i];
        if (b.airport != ap) continue;
        if (first < 0) first = i;
        count = i - first + 1;
        float r = length(vec3(b.h.x * 2.f, 0, b.h.z * 2.f)) + 2.f;   // generous: towers/radomes, rotation
        for (int k = 0; k < 4; k++) {
          float lx = b.c.x + ((k & 1) ? r : -r), lz = b.c.z + ((k & 2) ? r : -r);
          // inverse of the shader's airport frame: local (x, z) -> world
          float wx = A.x + lx * c + lz * s, wz = A.z + lx * s - lz * c;
          lo.x = std::min(lo.x, wx); hi.x = std::max(hi.x, wx); lo.z = std::min(lo.z, wz); hi.z = std::max(hi.z, wz);
        }
        lo.y = std::min(lo.y, A.elev + b.c.y - b.h.y * 1.3f - 2.f); hi.y = std::max(hi.y, A.elev + b.c.y + b.h.y * 1.3f + 2.f);
      }
      if (first >= 0) { d[384 + ap] = {lo.x, lo.y, lo.z, (float)first}; d[400 + ap] = {hi.x, hi.y, hi.z, (float)count}; }
    }
    glGenTextures(1, &texData); glBindTexture(GL_TEXTURE_2D, texData);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, (GLsizei)d.size(), 1, 0, GL_RGBA, GL_FLOAT, d.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  }
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  maxH = 0;
  for (size_t i = 0; i < g_world.hm.size(); i += 4) maxH = std::max(maxH, g_world.hm[i] + g_world.hm[i + 1] * 1.5f);
  maxH += 20;
  genMaterials();
  genCloudNoise();
  genWaves();
  genMinimap();
  if (!initEntities()) return false;
  initTerrainMesh();
  W = w; H = h;
  createTargets();
  ok = true;
  return true;
}

static void makeTex(GLuint& t, int w, int h, GLenum ifmt, GLenum fmt, GLenum type, GLenum filter) {
  if (t) glDeleteTextures(1, &t);
  glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
  glTexImage2D(GL_TEXTURE_2D, 0, ifmt, w, h, 0, fmt, type, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

// Render-resolution targets: the lit colour (+ TAA class in alpha) and depth
// The render resolution's targets are allocated at the full view size once; a render scale below 1 draws into
// their lower-left rw x rh (setRenderScale only moves that corner: no reallocation, no hitch). Passes that read them
// by pixel need nothing; the ones that sample by normalized coordinates scale by rw/W (uUVS / uRawUVS).
void Renderer::scaleDims() {
  rw = std::max(64, std::min(W, (int)(W * renderScale))); rh = std::max(64, std::min(H, (int)(H * renderScale)));
  cw = (rw + 1) / 2; ch = (rh + 1) / 2;
}
void Renderer::createRenderTargets() {
  scaleDims();
  allocW = W; allocH = H;
  makeTex(texRaw, W, H, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR);
  makeTex(texDepth, W, H, GL_R32F, GL_RED, GL_FLOAT, GL_NEAREST);
  makeTex(texCloudMask, W, H, GL_R8, GL_RED, GL_UNSIGNED_BYTE, GL_NEAREST);
  depthValid = false;
  makeTex(texCloud, (W + 1) / 2, (H + 1) / 2, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_NEAREST);
  makeTex(texCloudD, (W + 1) / 2, (H + 1) / 2, GL_R32F, GL_RED, GL_FLOAT, GL_NEAREST);
  if (!fboCloud) glGenFramebuffers(1, &fboCloud);
  glBindFramebuffer(GL_FRAMEBUFFER, fboCloud);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texCloud, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, texCloudD, 0);
  if (!fboComp) glGenFramebuffers(1, &fboComp);   // the composite writes the lit colour only
  glBindFramebuffer(GL_FRAMEBUFFER, fboComp);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texRaw, 0);
  if (!fboScene) glGenFramebuffers(1, &fboScene);
  glBindFramebuffer(GL_FRAMEBUFFER, fboScene);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texRaw, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, texDepth, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, texCloudMask, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  createGBuffer();
  createHullTarget();
}

// Display-resolution targets: TAA history + the upscaled scene that sprites, bloom and the composite work on
void Renderer::createTargets() {
  createRenderTargets();
  makeTex(texColor, W, H, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR);
  for (int i = 0; i < 2; i++) {
    makeTex(texHist[i], W, H, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR); histW = W; histH = H;
    if (!fboTAA[i]) glGenFramebuffers(1, &fboTAA[i]);
    glBindFramebuffer(GL_FRAMEBUFFER, fboTAA[i]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texHist[i], 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, texColor, 0);
  }
  histValid = false;
  if (!fboSprite) glGenFramebuffers(1, &fboSprite);
  glBindFramebuffer(GL_FRAMEBUFFER, fboSprite);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texColor, 0);
  bw = std::max(16, W / 4); bh = std::max(16, H / 4);
  for (int i = 0; i < 2; i++) {   // light shafts at quarter resolution
    makeTex(texRay[i], bw, bh, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR);
    if (!fboRay[i]) glGenFramebuffers(1, &fboRay[i]);
    glBindFramebuffer(GL_FRAMEBUFFER, fboRay[i]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texRay[i], 0);
    glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT);
  }
  for (int i = 0; i < kBloomMips; i++) {   // bloom chain: 1/2 .. 1/64 resolution
    mipW[i] = std::max(2, W >> (i + 1)); mipH[i] = std::max(2, H >> (i + 1));
    makeTex(texMip[i], mipW[i], mipH[i], GL_R11F_G11F_B10F, GL_RGB, GL_FLOAT, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, texMip[i]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (!fboMip[i]) glGenFramebuffers(1, &fboMip[i]);
    glBindFramebuffer(GL_FRAMEBUFFER, fboMip[i]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texMip[i], 0);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::setRenderScale(float s) {
  if (fabsf(s - renderScale) < 1e-4f) return;
  renderScale = s;
  if (ok) { scaleDims(); depthValid = false; }   // (the targets stay: only the part drawn changes)
}

void Renderer::resize(int w, int h) {
  if (w < 16 || h < 16) return;
  W = w; H = h;
  if (ok) createTargets();
}

void Renderer::setOffscreen(bool on) {
  if (!on) { screenFbo = 0; return; }
  if (!fboOff || offW != W || offH != H) {
    if (!fboOff) { glGenFramebuffers(1, &fboOff); glGenTextures(1, &texOff); }
    glBindTexture(GL_TEXTURE_2D, texOff);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, fboOff);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texOff, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    offW = W; offH = H;
  }
  screenFbo = fboOff;
}

mat4 Renderer::viewProj(const FrameParams& fp, float zNear, float zFar) const {
  return perspective(fp.fovY, (float)W / H, zNear, zFar) * viewMat(fp);
}
// the same with the camera at the origin, for positions given relative to it: in world metres (floats) a position is
// held only to their spacing there - 4 mm at the map's edges - and so was everything the world matrix placed: the
// cockpits' vertices, an arm's length away, swam by several pixels as the aircraft flew
mat4 Renderer::viewProjRel(const FrameParams& fp, float zNear, float zFar) const {
  mat4 view = viewMat(fp);
  view(0, 3) = 0.f; view(1, 3) = 0.f; view(2, 3) = 0.f;
  return perspective(fp.fovY, (float)W / H, zNear, zFar) * view;
}
mat4 Renderer::viewMat(const FrameParams& fp) const {
  mat4 view;
  view(0, 0) = fp.camRight.x; view(0, 1) = fp.camRight.y; view(0, 2) = fp.camRight.z;
  view(1, 0) = fp.camUp.x; view(1, 1) = fp.camUp.y; view(1, 2) = fp.camUp.z;
  view(2, 0) = fp.camBack.x; view(2, 1) = fp.camBack.y; view(2, 2) = fp.camBack.z;
  view(0, 3) = -dot(fp.camRight, fp.camPos); view(1, 3) = -dot(fp.camUp, fp.camPos); view(2, 3) = -dot(fp.camBack, fp.camPos);
  return view;
}

bool Renderer::project(const FrameParams& fp, vec3 p, float& sx, float& sy) const {
  mat4 vp = viewProj(fp);
  float x = vp(0, 0) * p.x + vp(0, 1) * p.y + vp(0, 2) * p.z + vp(0, 3);
  float y = vp(1, 0) * p.x + vp(1, 1) * p.y + vp(1, 2) * p.z + vp(1, 3);
  float w = vp(3, 0) * p.x + vp(3, 1) * p.y + vp(3, 2) * p.z + vp(3, 3);
  if (w < 0.1f) return false;
  sx = (x / w * 0.5f + 0.5f) * W; sy = (1.f - (y / w * 0.5f + 0.5f)) * H;
  return true;
}

// ------------------------------------------------ the passes of a frame
// the scene's uniforms and textures for a program (the cloud pass, the raster passes) and a view: this
// frame's or a camera feed's
// (GLERR: a draw that fails validation - samplers of two types on one unit, say - draws nothing and says nothing)
void Renderer::reportGLError(int stampIdx) {
  for (GLenum e = glGetError(), n = 0; e != 0 && n < 8; e = glGetError(), n++)
    fprintf(stderr, "GL error 0x%04x in the passes before timestamp %d (frame %d)\n", (unsigned)e, stampIdx, frameNo);
}

void Renderer::setRT(GLuint p, const FrameParams& fp) {
  glUseProgram(p);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texHM); glUniform1i(U(p, "uHM"), 0);
  glActiveTexture(GL_TEXTURE0 + 1); glBindTexture(GL_TEXTURE_2D_ARRAY, texAlb); glUniform1i(U(p, "uAlb"), 1);
  glActiveTexture(GL_TEXTURE0 + 2); glBindTexture(GL_TEXTURE_2D_ARRAY, texNrm); glUniform1i(U(p, "uNrm"), 2);
  glActiveTexture(GL_TEXTURE0 + 3); glBindTexture(GL_TEXTURE_2D, texMask); glUniform1i(U(p, "uMask"), 3);
  glActiveTexture(GL_TEXTURE0 + 4); glBindTexture(GL_TEXTURE_2D, texRoadId); glUniform1i(U(p, "uRoadId"), 4);
  glActiveTexture(GL_TEXTURE0 + 6); glBindTexture(GL_TEXTURE_2D, texHMax); glUniform1i(U(p, "uHMax"), 6);
  glActiveTexture(GL_TEXTURE0 + 13); glBindTexture(GL_TEXTURE_2D, texCloudCov); glUniform1i(U(p, "uCloudCov"), 13);
  glActiveTexture(GL_TEXTURE0 + 14); glBindTexture(GL_TEXTURE_3D, texNoise3); glUniform1i(U(p, "uNoise3"), 14);
  glActiveTexture(GL_TEXTURE0 + 15); glBindTexture(GL_TEXTURE_2D, texFont); glUniform1i(U(p, "uFontTex"), 15);
  // units 0-15 are all taken (10 and 12 by the G-buffer and the far shadow cascade below): the display atlases use
  // 16 and 17 (every GL 3.3 GPU has at least 32)
  glActiveTexture(GL_TEXTURE0 + 16); glBindTexture(GL_TEXTURE_2D, texPages); glUniform1i(U(p, "uDispTex"), 16);
  glActiveTexture(GL_TEXTURE0 + 17); glBindTexture(GL_TEXTURE_2D, texPanel); glUniform1i(U(p, "uPanelTex"), 17);
  glActiveTexture(GL_TEXTURE0 + 18); glBindTexture(GL_TEXTURE_2D, tshFront >= 0 ? texTSh[tshFront] : 0); glUniform1i(U(p, "uTSh"), 18);
  static const bool tshOff = getenv("TSHOFF") != nullptr;   // (debug: compare with per-pixel shadow rays)
  glUniform1i(U(p, "uTShOn"), tshFront >= 0 && !tshOff ? 1 : 0);
  // (bound whenever any of its channels is in use: the hull channels are written whether or not the terrain envelope
  // was drawn this frame, and the terrain channel is only read under uEnvOn)
  glActiveTexture(GL_TEXTURE0 + 20); glBindTexture(GL_TEXTURE_2D, hullOn || trafHullOn ? texEnv : 0); glUniform1i(U(p, "uEnv"), 20);
  glUniform1i(U(p, "uEnvOn"), 0);   // (the terrain envelope is gone)
  glUniform1i(U(p, "uScrWin"), screenWindows ? 1 : 0);
  glUniform1i(U(p, "uAfShOn"), shOn);   // the airframe shadow maps (af_shmap.glsl), for the proxy and the airframe's own lighting
  if (shOn) glUniformMatrix4fv(U(p, "uAfShVP"), 4, GL_FALSE, shMapVP[0].m);
  // (the array samplers always on their own units, maps or not: left at unit 0 beside uHM - a 2D sampler - every draw
  // of the program fails validation and draws nothing: the objects pass lost wrecks, debris, the UFO and the march)
  glUniform1i(U(p, "uTrafShOn"), trafShOn);   // the traffic's sun shadow maps (layers 4 + k), for the proxy
  if (trafShOn) glUniformMatrix4fv(U(p, "uTrafShVP"), kMaxTrafficDrawn, GL_FALSE, trafShVP[0].m);
  glActiveTexture(GL_TEXTURE0 + 26); glBindTexture(GL_TEXTURE_2D_ARRAY, shOn || trafShOn ? texShMap : 0); glUniform1i(U(p, "uAfShMap"), 26);
  glActiveTexture(GL_TEXTURE0 + 27); glBindTexture(GL_TEXTURE_2D_ARRAY, shOn ? texShMov : 0); glUniform1i(U(p, "uAfShMov"), 27);
  // the cabin's sun map (cockpit view) on unit 22: 12 is the far scenery cascade's (bound below, it took this one's place);
  // 22 is otherwise only the lighting pass's (its G-buffer extras), which reads no cabin map, and this binds it again
  // before every pass that does
  glActiveTexture(GL_TEXTURE0 + 22); glBindTexture(GL_TEXTURE_2D, shCabOn ? texShCab : 0); glUniform1i(U(p, "uCabShMap"), 22);
  glUniform1i(U(p, "uCabShOn"), shCabOn ? 1 : 0);
  if (shCabOn) { glUniformMatrix4fv(U(p, "uCabShVP"), 1, GL_FALSE, shCabVP.m); glUniformMatrix4fv(U(p, "uCabShVPc"), 1, GL_FALSE, shCabVPc.m); glUniform1f(U(p, "uCabShBias"), shCabBias); }
  glUniform1i(U(p, "uHullOn"), hullOn ? 1 : 0); glUniform1f(U(p, "uHullNear"), hullOn ? hullNearNow : hullNear(fp)); glUniform1i(U(p, "uHullExitOn"), hullOn && hullExitOn ? 1 : 0);
  glUniform1i(U(p, "uTrafHullOn"), trafHullOn ? 1 : 0);
  {   // AI traffic: one row of 32 texels per aircraft
    if (!texTraffic) {
      glGenTextures(1, &texTraffic); glBindTexture(GL_TEXTURE_2D, texTraffic);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 32, kMaxTrafficDrawn, 0, GL_RGBA, GL_FLOAT, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    glActiveTexture(GL_TEXTURE0 + 7); glBindTexture(GL_TEXTURE_2D, texTraffic);
    int n = std::min(fp.trafficN, kMaxTrafficDrawn);
    if (n > 0) glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 32, n, GL_RGBA, GL_FLOAT, fp.traffic[0].t);
    glUniform1i(U(p, "uTraffic"), 7); glUniform1i(U(p, "uTrafficN"), n);
  }
  glUniform1i(U(p, "uUfoOn"), fp.ufoOn ? 1 : 0);
  if (fp.ufoOn) {
    glUniform3f(U(p, "uUfoPos"), fp.ufoPos.x, fp.ufoPos.y, fp.ufoPos.z);
    glUniformMatrix3fv(U(p, "uUfoRot"), 1, GL_FALSE, fp.ufoRot);
    glUniform4fv(U(p, "uUfoAnim"), 1, fp.ufoAnim);
  }
  glUniform2f(U(p, "uRes"), (float)rw, (float)rh);
  glUniform2f(U(p, "uJit"), jitX, jitY);
  glUniform1f(U(p, "uSeed"), fmodf(frameNo * 0.618034f, 1.f));
  glUniform3f(U(p, "uCamPos"), fp.camPos.x, fp.camPos.y, fp.camPos.z);
  float cr[9] = {fp.camRight.x, fp.camRight.y, fp.camRight.z, fp.camUp.x, fp.camUp.y, fp.camUp.z, fp.camBack.x, fp.camBack.y, fp.camBack.z};
  glUniformMatrix3fv(U(p, "uCamRot"), 1, GL_FALSE, cr);
  glUniform1f(U(p, "uTanHalf"), fp.pano > 0.f ? fp.panoTanY : tanf(fp.fovY * 0.5f));   // (a panorama: the pixel footprints)
  glUniform1f(U(p, "uAspect"), (float)W / H);
  glUniform2f(U(p, "uPano"), fp.pano, fp.panoTanY);
  glUniform1f(U(p, "uMaxH"), maxH);
  glUniform1i(U(p, "uQuality"), quality); glUniform1i(U(p, "uDbg"), dbgOff);
  glUniform1f(U(p, "uTime"), fp.time);
  glUniform3f(U(p, "uSunDir"), fp.sunDir.x, fp.sunDir.y, fp.sunDir.z); glUniform1f(U(p, "uPlaneTSh"), fp.planeTerrSh);
  glUniform3f(U(p, "uSunCol"), fp.sunCol.x, fp.sunCol.y, fp.sunCol.z);
  glUniform1f(U(p, "uNight"), fp.night);
  glUniform1f(U(p, "uCloudCover"), fp.cloudCover);
  glUniform1f(U(p, "uCloudBase"), fp.cloudBase);
  glUniform1f(U(p, "uFogB"), fp.fogB);
  glUniform1f(U(p, "uWet"), fp.wet);
  glUniform1f(U(p, "uSnow"), fp.snow);
  glUniform1f(U(p, "uLightning"), fp.lightning);
  glUniform1f(U(p, "uStorm"), fp.storm);
  glUniform2f(U(p, "uWindOff"), fp.windOff.x, fp.windOff.y);
  glUniform3f(U(p, "uWindV"), fp.wind.x, fp.wind.y, fp.wind.z);
  // airports + buildings
  {
    int n = std::min(16, (int)g_world.airports.size());
    float ap[64], dim[64];
    for (int i = 0; i < n; i++) {
      const Airport& a = g_world.airports[i];
      ap[i * 4] = a.x; ap[i * 4 + 1] = a.z; ap[i * 4 + 2] = a.elev; ap[i * 4 + 3] = a.heading * DEG;
      dim[i * 4] = a.length; dim[i * 4 + 1] = a.width; dim[i * 4 + 2] = (float)a.surface; dim[i * 4 + 3] = (float)a.size;
    }
    glUniform1i(U(p, "uApCount"), n);
    glUniform4fv(U(p, "uAp"), n, ap); glUniform4fv(U(p, "uApDim"), n, dim);
    glUniform1i(U(p, "uBoxCount"), std::min(128, (int)g_world.boxes.size()));
    glActiveTexture(GL_TEXTURE0 + 5); glBindTexture(GL_TEXTURE_2D, texData); glUniform1i(U(p, "uData"), 5);
  }
  const PlaneVisual& pv = fp.plane;
  glUniform1i(U(p, "uPlaneOn"), pv.on ? 1 : 0);
  if (pv.on) {
    glUniform3f(U(p, "uPlanePos"), pv.pos.x, pv.pos.y, pv.pos.z);
    glUniformMatrix3fv(U(p, "uPlaneRot"), 1, GL_FALSE, pv.rot);
    glUniform4fv(U(p, "uM"), 24, pv.M);
    glUniform4fv(U(p, "uPS"), 1, pv.PS); glUniform4fv(U(p, "uCtl"), 1, pv.Ctl); glUniform4fv(U(p, "uPr"), 1, pv.Pr); glUniform3f(U(p, "uWheel"), pv.wheel[0], pv.wheel[1], pv.wheel[2]); glUniform1i(U(p, "uModelId"), pv.model);
    glUniform4fv(U(p, "uI0"), 1, pv.I0); glUniform4fv(U(p, "uI1"), 1, pv.I1); glUniform4fv(U(p, "uI2"), 1, pv.I2);
    glUniform3f(U(p, "uColBase"), pv.colBase.x, pv.colBase.y, pv.colBase.z);
    glUniform3f(U(p, "uColStripe"), pv.colStripe.x, pv.colStripe.y, pv.colStripe.z);
    glUniform3f(U(p, "uReg"), pv.reg[0], pv.reg[1], pv.reg[2]);
    glUniform1i(U(p, "uPropCount"), pv.propCount);
    glUniform4fv(U(p, "uHud"), 1, pv.hud); glUniform4fv(U(p, "uHud2"), 1, pv.hud2); glUniform3f(U(p, "uHudV"), pv.hudV[0], pv.hudV[1], pv.hudV[2]); glUniform4fv(U(p, "uHud3"), 1, pv.hud3);
    if (pv.propCount) glUniform4fv(U(p, "uProp"), pv.propCount, &pv.prop[0][0]);
  }
  {
    const WreckVisual& wv = fp.wreck;
    glUniform1i(U(p, "uWreck"), pv.on ? wv.pieces : 0);
    if (pv.on && wv.pieces > 0) {
      float P[15], C[15], Hh[15];
      for (int i = 0; i < wv.pieces; i++) { P[i*3] = wv.pos[i].x; P[i*3+1] = wv.pos[i].y; P[i*3+2] = wv.pos[i].z; C[i*3] = wv.C[i].x; C[i*3+1] = wv.C[i].y; C[i*3+2] = wv.C[i].z; Hh[i*3] = wv.H[i].x; Hh[i*3+1] = wv.H[i].y; Hh[i*3+2] = wv.H[i].z; }
      glUniform3fv(U(p, "uPcPos"), wv.pieces, P);
      glUniform3fv(U(p, "uPcC"), wv.pieces, C);
      glUniform3fv(U(p, "uPcH"), wv.pieces, Hh);
      glUniformMatrix3fv(U(p, "uPcRot"), wv.pieces, GL_FALSE, &wv.rot[0][0]);
    }
    glUniform1i(U(p, "uDebN"), wv.debris);
    if (wv.debris > 0) { glUniform4fv(U(p, "uDeb"), wv.debris, &wv.deb[0][0]); glUniform4fv(U(p, "uDebQ"), wv.debris, &wv.debQ[0][0]); }
    glUniform1i(U(p, "uCraterN"), wv.craterN);
    if (wv.craterN > 0) {
      glUniform4fv(U(p, "uCrater"), wv.craterN, &wv.crater[0][0]);
      // a circle around them all lets the terrain skip the crater loop everywhere else
      float x0 = 1e9f, z0 = 1e9f, x1 = -1e9f, z1 = -1e9f;
      for (int i = 0; i < wv.craterN; i++) { const float* c = wv.crater[i]; float r = c[2] * 2.7f; x0 = std::min(x0, c[0] - r); x1 = std::max(x1, c[0] + r); z0 = std::min(z0, c[1] - r); z1 = std::max(z1, c[1] + r); }
      glUniform3f(U(p, "uCraterB"), (x0 + x1) * 0.5f, (z0 + z1) * 0.5f, 0.5f * sqrtf((x1 - x0) * (x1 - x0) + (z1 - z0) * (z1 - z0)));
    }
  }
  glUniform3f(U(p, "uLandLightPos"), fp.landLightPos.x, fp.landLightPos.y, fp.landLightPos.z);
  glUniform3f(U(p, "uLandLightDir"), fp.landLightDir.x, fp.landLightDir.y, fp.landLightDir.z);
  glUniform1f(U(p, "uLandLight"), fp.landLight);
  glUniform4fv(U(p, "uFlame"), 1, pv.flame);
  glUniform4fv(U(p, "uVapor"), 1, pv.vapor);
  glUniform1i(U(p, "uLensN"), pv.lensN);
  if (pv.lensN) { glUniform4fv(U(p, "uLensP"), pv.lensN, &pv.lensP[0][0]); glUniform4fv(U(p, "uLensC"), pv.lensN, &pv.lensC[0][0]); glUniform4fv(U(p, "uLensD"), pv.lensN, &pv.lensD[0][0]); }
  glUniform4fv(U(p, "uWr"), 7, &pv.wr[0][0]);
  {
    const FxVisual& fx = fp.fx;
    glUniform1i(U(p, "uFxBeams"), fx.beams); glUniform1i(U(p, "uFxBombs"), fx.bombs); glUniform1i(U(p, "uFxBlasts"), fx.blasts);
    if (fx.beams) { glUniform4fv(U(p, "uBeamA"), fx.beams, &fx.beamA[0][0]); glUniform4fv(U(p, "uBeamB"), fx.beams, &fx.beamB[0][0]); }
    if (fx.bombs) glUniform4fv(U(p, "uBombs"), fx.bombs, &fx.bomb[0][0]);
    if (fx.blasts) { glUniform4fv(U(p, "uBlast"), fx.blasts, &fx.blast[0][0]); glUniform4fv(U(p, "uBlastI"), fx.blasts, &fx.blastI[0][0]); }
    glUniform4fv(U(p, "uPip"), 1, fx.pip);
    glUniform4fv(U(p, "uFeed"), 1, fx.feed);
  }
  {   // entity G-buffer and the sun shadow cascades
    for (int i = 0; i < 3; i++) { glActiveTexture(GL_TEXTURE0 + 8 + i); glBindTexture(GL_TEXTURE_2D, texGB[i]); }
    glUniform1i(U(p, "uGB0"), 8); glUniform1i(U(p, "uGB1"), 9); glUniform1i(U(p, "uGB2"), 10);
    bool sh = fp.sunDir.y > 0.03f && shValid[0];
    glUniform1i(U(p, "uShOn"), sh ? (shValid[1] ? 2 : 1) : 0);
    for (int c = 0; c < 2; c++) { glActiveTexture(GL_TEXTURE0 + 11 + c); glBindTexture(GL_TEXTURE_2D, texSh[c]); }
    glUniform1i(U(p, "uShMap0"), 11); glUniform1i(U(p, "uShMap1"), 12);
    glUniformMatrix4fv(U(p, "uShM0"), 1, GL_FALSE, shVP[0].m); glUniformMatrix4fv(U(p, "uShM1"), 1, GL_FALSE, shVP[1].m);
    glUniform2f(U(p, "uShTexel"), shR[0] * 2.f / std::max(shRes, 1), shR[1] * 2.f / std::max(shRes, 1));
    glUniform4f(U(p, "uShFade"), shIdeal[0].x, shIdeal[0].z, shIdeal[1].x, shIdeal[1].z);
    glUniform4f(U(p, "uShFadeR"), shR[0] * kShFade0, shR[0] * kShFade1, shR[1] * kShFade0, shR[1] * kShFade1);
    glUniform1f(U(p, "uTreeFar"), entTreeFar);
  }
  {
    float P[12][4], C[12][4], D[12][4];
    for (int i = 0; i < fp.plN; i++) {
      const auto& L = fp.pl[i];
      P[i][0] = L.pos.x; P[i][1] = L.pos.y; P[i][2] = L.pos.z; P[i][3] = L.radius;
      C[i][0] = L.col.x; C[i][1] = L.col.y; C[i][2] = L.col.z; C[i][3] = L.cosCut;
      D[i][0] = L.dir.x; D[i][1] = L.dir.y; D[i][2] = L.dir.z; D[i][3] = L.shadow;
    }
    glUniform1i(U(p, "uPLN"), fp.plN);
    glUniform1f(U(p, "uRwyLights"), fp.rwyLights);
    if (fp.plN) { glUniform4fv(U(p, "uPLP"), fp.plN, &P[0][0]); glUniform4fv(U(p, "uPLC"), fp.plN, &C[0][0]); glUniform4fv(U(p, "uPLD"), fp.plN, &D[0][0]); }
  }
  glUniform3f(U(p, "uFlameLP"), fp.flameLightPos.x, fp.flameLightPos.y, fp.flameLightPos.z);
  glUniform3f(U(p, "uFlameLI"), fp.flameLight.x, fp.flameLight.y, fp.flameLight.z);
  glUniform1i(U(p, "uCloudSplit"), cloudSplit ? 1 : 0);
  {   // the research jets' displays: their cameras' pictures (camera_feeds.cpp)
    float tile[kMaxFeeds][4], R[kMaxFeeds][4], Up[kMaxFeeds][4], B[kMaxFeeds][4];
    bool any = false;
    for (int k = 0; k < kMaxFeeds; k++) {
      const FeedCamera& c = fp.feeds[k];
      bool v = fp.feedRig > 0 && c.on && feedValid[k];
      any = any || v;
      for (int i = 0; i < 4; i++) tile[k][i] = feedTile[k][i];
      R[k][0] = c.right.x; R[k][1] = c.right.y; R[k][2] = c.right.z; R[k][3] = c.pano > 0.f ? -c.pano : c.tanX;   // (negative: a panorama's half angle)
      Up[k][0] = c.up.x; Up[k][1] = c.up.y; Up[k][2] = c.up.z; Up[k][3] = c.tanY;
      B[k][0] = c.back.x; B[k][1] = c.back.y; B[k][2] = c.back.z; B[k][3] = v ? 1.f : 0.f;
    }
    glActiveTexture(GL_TEXTURE0 + 21); glBindTexture(GL_TEXTURE_2D, texFeed); glUniform1i(U(p, "uFeedTex"), 21);
    glUniform1i(U(p, "uFeedOn"), fp.feedRig > 0 && any ? 1 : 0);
    glUniform4fv(U(p, "uFeedTile"), kMaxFeeds, &tile[0][0]); glUniform4fv(U(p, "uFeedR"), kMaxFeeds, &R[0][0]);
    glUniform4fv(U(p, "uFeedU"), kMaxFeeds, &Up[0][0]); glUniform4fv(U(p, "uFeedB"), kMaxFeeds, &B[0][0]);
    glUniform1f(U(p, "uFeedSkip"), feedPass ? 0.03f : 0.f);   // a feed's camera sits just outside the skin
  }
}

// a view into the scene targets
// the clouds at a quarter of the pixels, along the rays of the depths just written, composited over the lit view
void Renderer::cloudPass(const FrameParams& fp) {
  if (!cloudSplit) return;
  // clouds at a quarter of the pixels, along the rays of the depths just traced
  glBindFramebuffer(GL_FRAMEBUFFER, fboCloud);
  GLenum cb[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
  glDrawBuffers(2, cb);
  glViewport(0, 0, cw, ch);
  setRT(progClouds, fp);
  glActiveTexture(GL_TEXTURE0 + 19); glBindTexture(GL_TEXTURE_2D, texDepth); glUniform1i(U(progClouds, "uSceneDepth"), 19);
  glUniform1i(U(progClouds, "uFrame"), (int)(frameNo & 3));
  glDrawArrays(GL_TRIANGLES, 0, 3);
  // composite over the lit colour: colour x transmittance + in-scatter (its alpha, the TAA class, is kept)
  glBindFramebuffer(GL_FRAMEBUFFER, fboComp);
  GLenum c0 = GL_COLOR_ATTACHMENT0; glDrawBuffers(1, &c0);
  glViewport(0, 0, rw, rh);
  glUseProgram(progCloudComp);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texCloud); glUniform1i(U(progCloudComp, "uCloud"), 0);
  glActiveTexture(GL_TEXTURE0 + 1); glBindTexture(GL_TEXTURE_2D, texCloudD); glUniform1i(U(progCloudComp, "uCloudD"), 1);
  glActiveTexture(GL_TEXTURE0 + 2); glBindTexture(GL_TEXTURE_2D, texDepth); glUniform1i(U(progCloudComp, "uDepthTex"), 2);
  glActiveTexture(GL_TEXTURE0 + 3); glBindTexture(GL_TEXTURE_2D, texCloudMask); glUniform1i(U(progCloudComp, "uMaskTex"), 3);
  glUniform2f(U(progCloudComp, "uCloudHi"), (float)(cw - 1), (float)(ch - 1));
  glEnable(GL_BLEND); glBlendFuncSeparate(GL_ONE, GL_SRC_ALPHA, GL_ZERO, GL_ONE);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glDisable(GL_BLEND);
  glActiveTexture(GL_TEXTURE0);
}

// the sprites (smoke, fire, sparks, rain, glints ...) over a view's picture; texW/texH: its depth texture's size
void Renderer::drawSprites(const FrameParams& fp, float texW, float texH, float uvsX, float uvsY) {
  glEnable(GL_BLEND);
  glUseProgram(progSprite);
  mat4 vp = viewProj(fp);
  glUniformMatrix4fv(U(progSprite, "uViewProj"), 1, GL_FALSE, vp.m);
  { mat4 v = viewMat(fp); glUniformMatrix4fv(U(progSprite, "uPanoView"), 1, GL_FALSE, v.m); glUniform2f(U(progSprite, "uPano"), fp.pano, fp.panoTanY); }
  glUniform3f(U(progSprite, "uCamPos"), fp.camPos.x, fp.camPos.y, fp.camPos.z);
  glUniform3f(U(progSprite, "uCamR"), fp.camRight.x, fp.camRight.y, fp.camRight.z);
  glUniform3f(U(progSprite, "uCamU"), fp.camUp.x, fp.camUp.y, fp.camUp.z);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texDepth); glUniform1i(U(progSprite, "uDepth"), 0);
  glUniform2f(U(progSprite, "uRes"), texW, texH);
  glUniform2f(U(progSprite, "uUVS"), uvsX, uvsY);
  glUniform3f(U(progSprite, "uSunDir"), fp.sunDir.x, fp.sunDir.y, fp.sunDir.z);
  glUniform3f(U(progSprite, "uSunCol"), fp.sunCol.x, fp.sunCol.y, fp.sunCol.z);
  float amb = 0.08f + 0.35f * clampf(fp.sunDir.y + 0.1f, 0, 1);
  glUniform3f(U(progSprite, "uAmb"), amb * 0.8f, amb * 0.9f, amb * 1.1f);
  glUniform1f(U(progSprite, "uFogB"), fp.fogB);
  glUniform1f(U(progSprite, "uTime"), fp.time);
  glBindVertexArray(vaoSprite);
  glBindBuffer(GL_ARRAY_BUFFER, vboSprite);
  if (!(*curAlpha).empty()) {
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);   // alpha keeps the pixel's class flag
    glBufferData(GL_ARRAY_BUFFER, (*curAlpha).size() * sizeof(SpriteVert), (*curAlpha).data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(*curAlpha).size());
  }
  if (!(*curAdd).empty()) {
    glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ZERO, GL_ONE);
    glBufferData(GL_ARRAY_BUFFER, (*curAdd).size() * sizeof(SpriteVert), (*curAdd).data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(*curAdd).size());
  }
  glDisable(GL_BLEND);
}

// a camera's picture gets the effects drawn after the lighting too: the sprites and the light shafts
void Renderer::feedEffects(const FrameParams& f) {
  glBindFramebuffer(GL_FRAMEBUFFER, fboComp);   // (writes the lit colour)
  GLenum c0 = GL_COLOR_ATTACHMENT0; glDrawBuffers(1, &c0);
  glViewport(0, 0, rw, rh);
  drawSprites(f, (float)kFeedMaxW, (float)kFeedMaxH, 1.f, 1.f);
  if (f.pano > 0.f) return;   // (the light shafts work in a flat picture)
  float rsx = 0, rsy = 0; vec3 rsp = f.camPos + f.sunDir * 10000.f;
  bool sunFront = dot(f.sunDir, -f.camBack) > 0.f && project(f, rsp, rsx, rsy);
  vec2 sunUV(rsx / W, 1.f - rsy / H);
  float k = sunFront ? smoothstepf(-0.03f, 0.06f, f.sunDir.y) * (1.f - 0.7f * smoothstepf(0.85f, 1.f, f.cloudCover))
          * (1.f - smoothstepf(0.6f, 1.6f, std::max(fabsf(sunUV.x - 0.5f), fabsf(sunUV.y - 0.5f)))) : 0.f;
  if (k <= 0.001f || !progFeedRays) return;
  int fw = std::min(std::max(16, rw / 4), bw), fh = std::min(std::max(16, rh / 4), bh);   // in a corner of the main view's shaft targets
  glBindVertexArray(vaoEmpty);
  glViewport(0, 0, fw, fh);
  glBindFramebuffer(GL_FRAMEBUFFER, fboRay[0]);
  glUseProgram(progRayMask);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texRaw); glUniform1i(U(progRayMask, "uScene"), 0);
  glActiveTexture(GL_TEXTURE0 + 1); glBindTexture(GL_TEXTURE_2D, texDepth); glUniform1i(U(progRayMask, "uDepthTex"), 1);
  glUniform2f(U(progRayMask, "uSun"), sunUV.x, sunUV.y); glUniform1f(U(progRayMask, "uAsp"), (float)W / H);
  glUniform2f(U(progRayMask, "uUVS"), (float)rw / kFeedMaxW, (float)rh / kFeedMaxH);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindFramebuffer(GL_FRAMEBUFFER, fboRay[1]);
  glUseProgram(progRay);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texRay[0]); glUniform1i(U(progRay, "uTex"), 0);
  glUniform2f(U(progRay, "uSun"), sunUV.x, sunUV.y); glUniform1f(U(progRay, "uJitter"), fmodf(f.time * 61.8f, 1.f));
  glUniform2f(U(progRay, "uUVS"), (float)fw / bw, (float)fh / bh);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindFramebuffer(GL_FRAMEBUFFER, fboComp);
  glViewport(0, 0, rw, rh);
  glUseProgram(progFeedRays);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texRay[1]); glUniform1i(U(progFeedRays, "uTex"), 0);
  glUniform2f(U(progFeedRays, "uUVS"), (float)fw / bw, (float)fh / bh);
  vec3 tint = normalize(f.sunCol + vec3(1e-3f)) * 0.55f * k;
  glUniform3f(U(progFeedRays, "uRayK"), tint.x, tint.y, tint.z);
  glEnable(GL_BLEND); glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ZERO, GL_ONE);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glDisable(GL_BLEND);
  glActiveTexture(GL_TEXTURE0);
}

void Renderer::renderScene(const FrameParams& fp, const std::vector<SpriteVert>& alphaSprites, const std::vector<SpriteVert>& addSprites) {
  curAlpha = &alphaSprites; curAdd = &addSprites;
  pickAfPrograms(fp);
  if (!gpuQ[0]) glGenQueries(4, gpuQ);
  {
    int rq = (gpuQi + 1) % 4;   // issued three frames ago
    if (gpuQUsed[rq]) {
      GLint avail = 0; glGetQueryObjectiv(gpuQ[rq], GL_QUERY_RESULT_AVAILABLE, &avail);
      if (avail) { GLuint64 ns = 0; glGetQueryObjectui64v(gpuQ[rq], GL_QUERY_RESULT, &ns); gpuMs = (float)(ns * 1e-6); gpuQUsed[rq] = false; }
    }
  }
  if (!stampQ[0][0] && glQueryCounter) for (int f = 0; f < 4; f++) glGenQueries(kPasses + 1, stampQ[f]);
  {   // per-pass timings from the frame issued three frames ago
    int rq = (gpuQi + 1) % 4;
    if (stampUsed[rq]) {
      GLint avail = 0; glGetQueryObjectiv(stampQ[rq][kPasses], GL_QUERY_RESULT_AVAILABLE, &avail);
      if (avail) {
        GLuint64 t[kPasses + 1];
        for (int i = 0; i <= kPasses; i++) glGetQueryObjectui64v(stampQ[rq][i], GL_QUERY_RESULT, &t[i]);
        for (int i = 0; i < kPasses; i++) passMs[i] = passMs[i] * 0.8f + (float)((t[i + 1] - t[i]) * 1e-6) * 0.2f;
        stampUsed[rq] = false;
      }
    }
  }
  glBeginQuery(GL_TIME_ELAPSED, gpuQ[gpuQi]);
  stamp(0);
  // TAA: Halton(2,3) sub-pixel jitter and a golden-ratio noise seed, both changing every frame
  frameNo++;
  {
    auto halton = [](int i, int b) { float f = 1, r = 0; while (i > 0) { f /= b; r += f * (i % b); i /= b; } return r; };
    int hi = (frameNo % 16) + 1;   // (16 points: the upscaler needs every output pixel visited)
    jitX = (halton(hi, 2) - 0.5f) / rw; jitY = (halton(hi, 3) - 0.5f) / rh;
  }
  // ------------------------------------------------ environment entities: shadow cascades + G-buffer
  static const bool cloudSplitOff = getenv("CLOUDSPLITOFF") != nullptr;   // (debug: no clouds)
  cloudSplit = !cloudSplitOff && progClouds && progCloudComp && fp.cloudCover >= 0.02f;
  GLenum bufs[3] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2};
  float cr[9] = {fp.camRight.x, fp.camRight.y, fp.camRight.z, fp.camUp.x, fp.camUp.y, fp.camUp.z, fp.camBack.x, fp.camBack.y, fp.camBack.z};   // (this frame's camera, for the TAA)
  // ------------------------------------------------ the raster renderer: scenery, terrain and sea into the G-buffer, then one lighting pass
  rasterWorld(fp);
  bakeTerrainShadow(fp);
  rasterShadowMaps(fp);   // (the airframe's shadow maps: the feeds' and the main view's proxy both read them)
  rasterTrafficShadowMaps(fp);
  stamp(1);
  if ((fp.dispMode & 1) && (!texPages || (frameNo & 1) == 0)) renderDisplays(fp, false);   // the cockpit display atlases, before the objects pass samples them (the research jets' pages at 30 Hz: 9 Mpx and their mips a frame)
  if (fp.dispMode & 2) renderDisplays(fp, true);
  stamp(2);
  // the research jets' cockpit cameras: the same passes on their own targets, before the objects pass draws the screens
  renderFeeds(fp, [this](GLuint p, const FrameParams& f) { setRT(p, f); }, [this](const FrameParams& f, GLuint) { rasterShadowProxy(f); rasterLight(f); cloudPass(f); rasterEffects(f); }, [this](const FrameParams& f) { feedEffects(f); });
  stamp(3);
  rasterObjects(fp);
  stamp(4);
  rasterShadowProxy(fp);
  stamp(5);
  rasterLight(fp);
  cloudPass(fp);
  rasterEffects(fp);
  stamp(6);
  // ------------------------------------------------ temporal AA resolve (before the sprites: particles never smear)
  {
    int cur = histIdx ^ 1;
    if (length(fp.camPos - prevCamPos) > 400.f) histValid = false;   // camera cut
    glBindFramebuffer(GL_FRAMEBUFFER, fboTAA[cur]);
    glDrawBuffers(2, bufs);
    glViewport(0, 0, W, H);
    glUseProgram(progTAA);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texRaw); glUniform1i(U(progTAA, "uRaw"), 0);
    glActiveTexture(GL_TEXTURE0 + 1); glBindTexture(GL_TEXTURE_2D, texDepth); glUniform1i(U(progTAA, "uDepth"), 1);
    glActiveTexture(GL_TEXTURE0 + 2); glBindTexture(GL_TEXTURE_2D, texHist[histIdx]); glUniform1i(U(progTAA, "uHist"), 2);
    glUniform2f(U(progTAA, "uRes"), (float)W, (float)H);
    glUniform2f(U(progTAA, "uRawRes"), (float)rw, (float)rh);
    glUniform2f(U(progTAA, "uRawUVS"), (float)rw / allocW, (float)rh / allocH);
    glUniform2f(U(progTAA, "uJit"), jitX, jitY);
    glUniform1f(U(progTAA, "uHistValid"), histValid ? 1.f : 0.f);
    glUniform1f(U(progTAA, "uDt"), fp.dt);
    const vec3 camD = fp.camPos - prevCamPos;   // (relative: taa_fs.glsl)
    glUniform3f(U(progTAA, "uCamDelta"), camD.x, camD.y, camD.z);
    glUniformMatrix3fv(U(progTAA, "uCamRot"), 1, GL_FALSE, cr);
    glUniformMatrix3fv(U(progTAA, "uPrevCamRot"), 1, GL_FALSE, prevCamRot);
    glUniform1f(U(progTAA, "uTanHalf"), tanf(fp.fovY * 0.5f));
    glUniform1f(U(progTAA, "uAspect"), (float)W / H);
    const PlaneVisual& pv2 = fp.plane;
    vec3 pp = pv2.on ? pv2.pos : prevPlanePos;
    const vec3 pRel = pp - fp.camPos, ppRel = prevPlanePos - prevCamPos;
    glUniform3f(U(progTAA, "uPlaneRel"), pRel.x, pRel.y, pRel.z);
    glUniformMatrix3fv(U(progTAA, "uPlaneRot"), 1, GL_FALSE, pv2.on ? pv2.rot : prevPlaneRot);
    glUniform3f(U(progTAA, "uPrevPlaneRel"), ppRel.x, ppRel.y, ppRel.z);
    glUniformMatrix3fv(U(progTAA, "uPrevPlaneRot"), 1, GL_FALSE, prevPlaneRot);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    histIdx = cur; histValid = true;
    prevCamPos = fp.camPos; memcpy(prevCamRot, cr, sizeof cr);
    if (pv2.on) { prevPlanePos = pv2.pos; memcpy(prevPlaneRot, pv2.rot, sizeof prevPlaneRot); }
  }

  stamp(7);
  // ------------------------------------------------ sprites
  glBindFramebuffer(GL_FRAMEBUFFER, fboSprite);
  GLenum one = GL_COLOR_ATTACHMENT0; glDrawBuffers(1, &one);
  glViewport(0, 0, W, H);
  drawSprites(fp, (float)W, (float)H, (float)rw / allocW, (float)rh / allocH);   // (the depth is in the render resolution's corner)

  stamp(8);
  // ------------------------------------------------ bloom
  glBindVertexArray(vaoEmpty);
  glActiveTexture(GL_TEXTURE0);
  glUseProgram(progDown);
  glUniform1i(U(progDown, "uTex"), 0);
  for (int i = 0; i < kBloomMips; i++) {
    glBindFramebuffer(GL_FRAMEBUFFER, fboMip[i]); glViewport(0, 0, mipW[i], mipH[i]);
    glBindTexture(GL_TEXTURE_2D, i == 0 ? texColor : texMip[i - 1]);
    int sw = i == 0 ? W : mipW[i - 1], sh = i == 0 ? H : mipH[i - 1];
    glUniform2f(U(progDown, "uTexel"), 1.f / sw, 1.f / sh); glUniform1i(U(progDown, "uFirst"), i == 0 ? 1 : 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
  }
  glUseProgram(progUp);
  glUniform1i(U(progUp, "uTex"), 0);
  glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE);
  for (int i = kBloomMips - 1; i > 0; i--) {   // each level adds its blurred self to the next larger one
    glBindFramebuffer(GL_FRAMEBUFFER, fboMip[i - 1]); glViewport(0, 0, mipW[i - 1], mipH[i - 1]);
    glBindTexture(GL_TEXTURE_2D, texMip[i]);
    glUniform2f(U(progUp, "uTexel"), 1.f / mipW[i], 1.f / mipH[i]);
    glDrawArrays(GL_TRIANGLES, 0, 3);
  }
  glDisable(GL_BLEND);
  stamp(9);
  // ------------------------------------------------ light shafts
  float rsx = 0, rsy = 0; vec3 rsp = fp.camPos + fp.sunDir * 10000.f;
  bool sunFront = dot(fp.sunDir, -fp.camBack) > 0.f && project(fp, rsp, rsx, rsy);
  vec2 sunUV(rsx / W, 1.f - rsy / H);
  float rayK = sunFront && !fp.sealedCockpit ? smoothstepf(-0.03f, 0.06f, fp.sunDir.y) * (1.f - 0.7f * smoothstepf(0.85f, 1.f, fp.cloudCover))
             * (1.f - smoothstepf(0.6f, 1.6f, std::max(fabsf(sunUV.x - 0.5f), fabsf(sunUV.y - 0.5f)))) : 0.f;
  if (rayK > 0.001f) {
    glViewport(0, 0, bw, bh);
    glBindFramebuffer(GL_FRAMEBUFFER, fboRay[0]);
    glUseProgram(progRayMask);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texColor); glUniform1i(U(progRayMask, "uScene"), 0);
    glActiveTexture(GL_TEXTURE0 + 1); glBindTexture(GL_TEXTURE_2D, texDepth); glUniform1i(U(progRayMask, "uDepthTex"), 1);
    glUniform2f(U(progRayMask, "uSun"), sunUV.x, sunUV.y); glUniform1f(U(progRayMask, "uAsp"), (float)W / H);
    glUniform2f(U(progRayMask, "uUVS"), (float)rw / allocW, (float)rh / allocH);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindFramebuffer(GL_FRAMEBUFFER, fboRay[1]);
    glUseProgram(progRay);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texRay[0]); glUniform1i(U(progRay, "uTex"), 0);
    glUniform2f(U(progRay, "uSun"), sunUV.x, sunUV.y); glUniform1f(U(progRay, "uJitter"), fmodf(fp.time * 61.8f, 1.f));
    glUniform2f(U(progRay, "uUVS"), 1.f, 1.f);
    glDrawArrays(GL_TRIANGLES, 0, 3);
  }

  stamp(10);
  // ------------------------------------------------ composite to backbuffer
  glBindFramebuffer(GL_FRAMEBUFFER, screenFbo);
  glViewport(0, 0, W, H);
  glUseProgram(progPost);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texColor); glUniform1i(U(progPost, "uScene"), 0);
  glActiveTexture(GL_TEXTURE0 + 1); glBindTexture(GL_TEXTURE_2D, texMip[0]); glUniform1i(U(progPost, "uBloom"), 1);
  glActiveTexture(GL_TEXTURE0 + 3); glBindTexture(GL_TEXTURE_2D, texRay[1]); glUniform1i(U(progPost, "uRays"), 3);
  glUniform1f(U(progPost, "uBloomK"), 0.05f);
  vec3 rayTint = normalize(fp.sunCol + vec3(1e-3f)) * 0.55f * rayK;
  glUniform3f(U(progPost, "uRayK"), rayTint.x, rayTint.y, rayTint.z);
  glUniform1f(U(progPost, "uExposure"), fp.exposure);
  glUniform1f(U(progPost, "uTime"), fp.time);
  glUniform2f(U(progPost, "uRes"), (float)W, (float)H);
  glUniform1f(U(progPost, "uRainLens"), fp.rainLens);
  glUniform1f(U(progPost, "uFade"), fp.fade);
  glUniform1f(U(progPost, "uVignette"), fp.vignette);
  glUniform1f(U(progPost, "uGLoad"), fp.gLoad);
  float sx = 0, sy = 0; vec3 sp = fp.camPos + fp.sunDir * 10000.f;
  bool vis = fp.sunDir.y > -0.02f && project(fp, sp, sx, sy) && sx > -0.2f * W && sx < 1.2f * W && sy > -0.2f * H && sy < 1.2f * H;
  glUniform2f(U(progPost, "uSunScreen"), sx / W, 1.f - sy / H);
  glActiveTexture(GL_TEXTURE0 + 2); glBindTexture(GL_TEXTURE_2D, texDepth); glUniform1i(U(progPost, "uDepthTex"), 2);
  glUniform2f(U(progPost, "uDepthUVS"), (float)rw / allocW, (float)rh / allocH);
  glUniform1f(U(progPost, "uSunVisible"), vis && !fp.sealedCockpit ? (1.f - smoothstepf(0.5f, 0.9f, fp.cloudCover)) * smoothstepf(-0.02f, 0.1f, fp.sunDir.y) : 0.f);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glActiveTexture(GL_TEXTURE0);
  stamp(kPasses);
  glEndQuery(GL_TIME_ELAPSED);
  stampUsed[gpuQi] = stampQ[gpuQi][0] != 0;
  gpuQUsed[gpuQi] = true; gpuQi = (gpuQi + 1) % 4;
}

// ------------------------------------------------------------------ UI
void Renderer::clearScreen() {
  glBindFramebuffer(GL_FRAMEBUFFER, 0); glViewport(0, 0, W, H);
  glDisable(GL_DEPTH_TEST); glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
}
void Renderer::uiBegin() { ui.clear(); curImg = 0; uiClipOn = false; }

static void quad(std::vector<UIVert>& v, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, vec3 c, float a, float mode, float hx = 0, float hy = 0, float p = 0, const vec3* c2 = nullptr) {
  vec3 b = c2 ? *c2 : c;
  UIVert q[4] = {{x0, y0, u0, v0, c.x, c.y, c.z, a, mode, hx, hy, p}, {x1, y0, u1, v0, c.x, c.y, c.z, a, mode, hx, hy, p},
                 {x1, y1, u1, v1, b.x, b.y, b.z, a, mode, hx, hy, p}, {x0, y1, u0, v1, b.x, b.y, b.z, a, mode, hx, hy, p}};
  v.push_back(q[0]); v.push_back(q[1]); v.push_back(q[2]); v.push_back(q[0]); v.push_back(q[2]); v.push_back(q[3]);
}

void Renderer::rect(float x, float y, float w, float h, vec3 c, float a, float radius) {
  if (radius <= 0) { quad(ui, x, y, x + w, y + h, 0, 0, 0, 0, c, a, 0); return; }
  quad(ui, x, y, x + w, y + h, -w * 0.5f, -h * 0.5f, w * 0.5f, h * 0.5f, c, a, 4 + std::min(radius, std::min(w, h) * 0.5f) * 0.001f, w * 0.5f, h * 0.5f);
}

void Renderer::rectGrad(float x, float y, float w, float h, vec3 top, vec3 bottom, float a, float radius) {
  float r = std::min(std::max(radius, 0.f), std::min(w, h) * 0.5f);
  quad(ui, x, y, x + w, y + h, -w * 0.5f, -h * 0.5f, w * 0.5f, h * 0.5f, top, a, 4 + r * 0.001f, w * 0.5f, h * 0.5f, 0, &bottom);
}

void Renderer::rectOutline(float x, float y, float w, float h, vec3 c, float a, float radius, float th) {
  float r = std::min(std::max(radius, 0.f), std::min(w, h) * 0.5f);
  quad(ui, x - 1, y - 1, x + w + 1, y + h + 1, -w * 0.5f - 1, -h * 0.5f - 1, w * 0.5f + 1, h * 0.5f + 1, c, a, 5 + r * 0.001f, w * 0.5f, h * 0.5f, th);
}

void Renderer::glow(float x, float y, float w, float h, vec3 c, float a, float radius, float soft) {
  float r = std::min(std::max(radius, 0.f), std::min(w, h) * 0.5f);
  quad(ui, x - soft, y - soft, x + w + soft, y + h + soft, -w * 0.5f - soft, -h * 0.5f - soft, w * 0.5f + soft, h * 0.5f + soft, c, a, 6 + r * 0.001f, w * 0.5f, h * 0.5f, soft);
}

void Renderer::line(float x0, float y0, float x1, float y1, float th, vec3 c, float a) {
  float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
  if (l < 1e-3f) return;
  float nx = -dy / l * th * 0.5f, ny = dx / l * th * 0.5f;
  UIVert q[4] = {{x0 + nx, y0 + ny, 0, 0, c.x, c.y, c.z, a, 0, 0, 0, 0}, {x1 + nx, y1 + ny, 0, 0, c.x, c.y, c.z, a, 0, 0, 0, 0},
                 {x1 - nx, y1 - ny, 0, 0, c.x, c.y, c.z, a, 0, 0, 0, 0}, {x0 - nx, y0 - ny, 0, 0, c.x, c.y, c.z, a, 0, 0, 0, 0}};
  ui.push_back(q[0]); ui.push_back(q[1]); ui.push_back(q[2]); ui.push_back(q[0]); ui.push_back(q[2]); ui.push_back(q[3]);
}

float Renderer::textWidth(const std::string& s, float size) const {
  float sc = size / FONT_EM, w = 0;
  for (unsigned char ch : s) { if (ch < 32 || ch > 126) ch = '?'; w += FONT_ADV[ch - 32] * sc; }
  return w;
}

float Renderer::text(float x, float y, float size, const std::string& s, vec3 c, float a, int align, bool shadow) {
  float sc = size / FONT_EM;
  float w = textWidth(s, size);
  if (align == 1) x -= w * 0.5f; else if (align == 2) x -= w;
  for (int pass = shadow ? 0 : 1; pass < 2; pass++) {
    float cx = x;
    for (unsigned char ch : s) {
      if (ch < 32 || ch > 126) ch = '?';
      int gi = ch - 32;
      if (ch != ' ') {
        int col = gi % 16, row = gi / 16;
        float u0 = (float)(col * FONT_CELLW) / FONT_W, v0 = (float)(row * FONT_CELLH) / FONT_H;
        float u1 = (float)((col + 1) * FONT_CELLW) / FONT_W, v1 = (float)((row + 1) * FONT_CELLH) / FONT_H;
        float x0 = cx - FONT_PAD * sc, y0 = y - FONT_PAD * sc;
        float off = pass == 0 ? size * 0.06f : 0;
        quad(ui, x0 + off, y0 + off, x0 + FONT_CELLW * sc + off, y0 + FONT_CELLH * sc + off, u0, v0, u1, v1, c, a, pass == 0 ? 2.f : 1.f);
      }
      cx += FONT_ADV[gi] * sc;
    }
  }
  return w;
}

void Renderer::image(GLuint tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1, float a) {
  if (curImg && curImg != tex) flushUI();
  curImg = tex;
  quad(ui, x, y, x + w, y + h, u0, v0, u1, v1, vec3(1, 1, 1), a, 3);
}

void Renderer::flushUI() {
  if (ui.empty()) return;
  glBindFramebuffer(GL_FRAMEBUFFER, screenFbo);
  glViewport(0, 0, W, H);
  glEnable(GL_BLEND); glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glUseProgram(progUI);
  glUniform2f(U(progUI, "uScreen"), (float)W, (float)H);
  glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texFont); glUniform1i(U(progUI, "uFont"), 0);
  glActiveTexture(GL_TEXTURE0 + 1); glBindTexture(GL_TEXTURE_2D, curImg ? curImg : texFont); glUniform1i(U(progUI, "uImg"), 1);
  glBindVertexArray(vaoUI); glBindBuffer(GL_ARRAY_BUFFER, vboUI);
  glBufferData(GL_ARRAY_BUFFER, ui.size() * sizeof(UIVert), ui.data(), GL_STREAM_DRAW);
  if (uiClipOn) {
    const int x0 = (int)floorf(uiClipBox[0]), x1 = (int)ceilf(uiClipBox[2]), y0 = (int)floorf(uiClipBox[1]), y1 = (int)ceilf(uiClipBox[3]);
    glEnable(GL_SCISSOR_TEST); glScissor(x0, H - y1, std::max(0, x1 - x0), std::max(0, y1 - y0));
  }
  glDrawArrays(GL_TRIANGLES, 0, (GLsizei)ui.size());
  glDisable(GL_SCISSOR_TEST);
  glActiveTexture(GL_TEXTURE0);
  ui.clear();
}

void Renderer::uiEnd() { flushUI(); glDisable(GL_BLEND); curImg = 0; }

// The current frame as a PNG (uncompressed deflate blocks: no zlib needed, every image viewer opens it)
bool Renderer::screenshotPNG(const char* path) {
  std::vector<uint8_t> px((size_t)W * H * 3);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, W, H, GL_RGB, GL_UNSIGNED_BYTE, px.data());
  return writePNG(path, W, H, px);
}

// RGB8 image, bottom row first (as glReadPixels returns it), saved as a PNG with stored (uncompressed) deflate blocks
bool writePNG(const char* path, int W, int H, const std::vector<uint8_t>& px) {
  std::vector<uint8_t> raw;   // filter byte 0 + RGB row, top row first
  raw.reserve((size_t)(W * 3 + 1) * H);
  for (int y = H - 1; y >= 0; y--) { raw.push_back(0); raw.insert(raw.end(), px.begin() + (size_t)y * W * 3, px.begin() + (size_t)(y + 1) * W * 3); }
  static uint32_t crcT[256]; static bool crcInit = false;
  if (!crcInit) { for (uint32_t n = 0; n < 256; n++) { uint32_t c = n; for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; crcT[n] = c; } crcInit = true; }
  auto crc = [&](const uint8_t* d, size_t n, uint32_t c) { c = ~c; for (size_t i = 0; i < n; i++) c = crcT[(c ^ d[i]) & 255] ^ (c >> 8); return ~c; };
  std::vector<uint8_t> z = {0x78, 0x01};
  for (size_t o = 0; o < raw.size(); o += 65535) {
    size_t n = std::min<size_t>(65535, raw.size() - o);
    z.push_back(o + n == raw.size() ? 1 : 0);
    z.push_back((uint8_t)(n & 255)); z.push_back((uint8_t)(n >> 8)); z.push_back((uint8_t)(~n & 255)); z.push_back((uint8_t)((~n >> 8) & 255));
    z.insert(z.end(), raw.begin() + o, raw.begin() + o + n);
  }
  uint32_t a = 1, b = 0; for (uint8_t v : raw) { a = (a + v) % 65521; b = (b + a) % 65521; }
  uint32_t ad = (b << 16) | a; z.push_back(ad >> 24); z.push_back(ad >> 16); z.push_back(ad >> 8); z.push_back(ad);
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  auto be32 = [&](uint32_t v) { uint8_t q[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v}; fwrite(q, 1, 4, f); };
  auto chunk = [&](const char* type, const std::vector<uint8_t>& d) {
    be32((uint32_t)d.size()); fwrite(type, 1, 4, f); if (!d.empty()) fwrite(d.data(), 1, d.size(), f);
    uint32_t c = crc((const uint8_t*)type, 4, 0); c = crc(d.data(), d.size(), c); be32(c);
  };
  const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10}; fwrite(sig, 1, 8, f);
  std::vector<uint8_t> ih = {(uint8_t)(W >> 24), (uint8_t)(W >> 16), (uint8_t)(W >> 8), (uint8_t)W, (uint8_t)(H >> 24), (uint8_t)(H >> 16), (uint8_t)(H >> 8), (uint8_t)H, 8, 2, 0, 0, 0};
  chunk("IHDR", ih); chunk("IDAT", z); chunk("IEND", {});
  fclose(f);
  return true;
}

// PNG or JPEG into RGBA, top row first (stb_image)
bool readImage(const char* path, int& W, int& H, std::vector<uint8_t>& rgba) {
  int n = 0;
  unsigned char* px = stbi_load(path, &W, &H, &n, 4);
  if (!px) return false;
  rgba.assign(px, px + (size_t)W * H * 4);
  stbi_image_free(px);
  return true;
}

bool Renderer::screenshot(const char* path) {
  std::vector<uint8_t> px((size_t)W * H * 3);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, W, H, GL_RGB, GL_UNSIGNED_BYTE, px.data());
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  fprintf(f, "P6 %d %d 255\n", W, H);
  for (int y = H - 1; y >= 0; y--) fwrite(&px[(size_t)y * W * 3], 1, (size_t)W * 3, f);
  fclose(f);
  return true;
}
