// Canonical offline aircraft bake. Links the unmodified production renderer/builders.
// No world, career, player save, shader binary cache or menu rendering is initialized.
#include "renderer.h"
#include "models.h"
#include "aircraft_mesh_asset.h"
#include "aircraft_geometry_source.h"
#include "mesh_validation.h"
#include <dlfcn.h>
#include <unistd.h>
#include <spawn.h>
#include <sys/wait.h>
#include <cerrno>
#include <cstring>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;
namespace {
void* eglLibrary = nullptr;
void* (*eglGetProc)(const char*) = nullptr;
void* proc(const char* name) {
  void* value = eglGetProc(name);
  return value ? value : dlsym(eglLibrary, name);
}
template<class Function> Function symbol(const char* name) {
  auto value = reinterpret_cast<Function>(dlsym(eglLibrary, name));
  if (!value) throw std::runtime_error(std::string("Missing EGL entry point: ") + name);
  return value;
}
struct Context {
  void* display = nullptr;
  void* surface = nullptr;
  void* context = nullptr;
  Context() {
    eglLibrary = dlopen("libEGL.so.1", RTLD_NOW | RTLD_GLOBAL);
    if (!eglLibrary) throw std::runtime_error("libEGL.so.1 is unavailable");
    eglGetProc = symbol<void*(*)(const char*)>("eglGetProcAddress");
    auto platformDisplay = reinterpret_cast<void*(*)(unsigned, void*, const int*)>(eglGetProc("eglGetPlatformDisplayEXT"));
    if (!platformDisplay) throw std::runtime_error("EGL surfaceless platform is unavailable");
    display = platformDisplay(0x31DD, nullptr, nullptr);
    int major = 0, minor = 0;
    if (!display || !symbol<unsigned(*)(void*, int*, int*)>("eglInitialize")(display, &major, &minor))
      throw std::runtime_error("Could not initialize surfaceless EGL");
    if (!symbol<unsigned(*)(unsigned)>("eglBindAPI")(0x30A2)) throw std::runtime_error("EGL OpenGL API unavailable");
    const int attributes[] = {0x3033, 1, 0x3040, 8, 0x3024, 8, 0x3023, 8, 0x3022, 8, 0x3038};
    void* config = nullptr; int count = 0;
    if (!symbol<unsigned(*)(void*, const int*, void**, int, int*)>("eglChooseConfig")(display, attributes, &config, 1, &count) || !count)
      throw std::runtime_error("No compatible EGL pbuffer configuration");
    const int surfaceAttributes[] = {0x3057, 32, 0x3056, 32, 0x3038};
    surface = symbol<void*(*)(void*, void*, const int*)>("eglCreatePbufferSurface")(display, config, surfaceAttributes);
    const int contextAttributes[] = {0x3098, 3, 0x30FB, 3, 0x30FD, 1, 0x3038};
    context = symbol<void*(*)(void*, void*, void*, const int*)>("eglCreateContext")(display, config, nullptr, contextAttributes);
    if (!surface || !context || !symbol<unsigned(*)(void*, void*, void*, void*)>("eglMakeCurrent")(display, surface, surface, context))
      throw std::runtime_error("Could not make OpenGL 3.3 context current");
    const char* missing = nullptr;
    if (!glLoad(proc, &missing)) throw std::runtime_error(std::string("Missing OpenGL entry point: ") + (missing ? missing : "unknown"));
    GLint textureUnits = 0;
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &textureUnits);
    if (textureUnits < 22) throw std::runtime_error("Exporter requires at least 22 fragment texture units");
  }
  ~Context() {
    // Destroy the context last: its own GL resources are released by the driver.
    if (display) {
      symbol<unsigned(*)(void*, void*, void*, void*)>("eglMakeCurrent")(display, nullptr, nullptr, nullptr);
      if (context) symbol<unsigned(*)(void*, void*)>("eglDestroyContext")(display, context);
      if (surface) symbol<unsigned(*)(void*, void*)>("eglDestroySurface")(display, surface);
      symbol<unsigned(*)(void*)>("eglTerminate")(display);
    }
    if (eglLibrary) dlclose(eglLibrary);
  }
};
std::string jsonString(const std::string& value) {
  std::ostringstream out; out << '"';
  for (unsigned char c : value) {
    if (c == '"' || c == '\\') out << '\\' << c;
    else if (c < 32 || c > 126) { char escaped[7]; std::snprintf(escaped, sizeof escaped, "\\u%04x", unsigned(c)); out << escaped; }
    else out << c;
  }
  out << '"'; return out.str();
}
std::string glString(GLenum name) {
  const GLubyte* value = glGetString(name);
  if (!value || !*value) throw std::runtime_error("Driver did not report complete GL provenance");
  return reinterpret_cast<const char*>(value);
}
}

extern char** environ;

namespace {
struct Producer {
  std::string vendor, renderer, version;
};
Producer producer() { return {glString(GL_VENDOR), glString(GL_RENDERER), glString(GL_VERSION)}; }
std::string receipt(int model, int slot, const Producer& gl) {
  // Fixed wire representation lets the coordinator verify every worker's source,
  // model/view, quality contract and actual GL producer without a JSON library.
  std::ostringstream out;
  out << "{\"schema\":1,\"implementation\":\"production-strict-fresh-v1\",\"full_quality\":true,"
      << "\"model_index\":" << model << ",\"slot\":" << slot
      << ",\"source_digest\":" << jsonString(kAircraftBuildSourceDigest)
      << ",\"geometry_digest\":" << jsonString(kAircraftGeometrySourceDigest)
      << ",\"gl\":{\"vendor\":" << jsonString(gl.vendor) << ",\"renderer\":" << jsonString(gl.renderer)
      << ",\"version\":" << jsonString(gl.version) << "}}\n";
  return out.str();
}
std::string receiptName(int model, int slot) {
  return ".bake-" + std::to_string(model) + "-" + std::to_string(slot) + ".json";
}
void rejectDebugOverrides() {
  for (const char* variable : {"AF_ALL", "CLIPDBG", "NV_SAFE_GEAR"})
    if (std::getenv(variable)) throw std::runtime_error(std::string("Unset diagnostic geometry override before export: ") + variable);
}
void writeText(const fs::path& path, const std::string& text) {
  const fs::path temporary = path.string() + ".tmp";
  std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
  stream << text; stream.close();
  if (!stream) throw std::runtime_error("Could not finish export report");
  fs::rename(temporary, path);
}
void bakeOne(int model, int slot, const fs::path& directory) {
  if (model < 0 || model >= kAircraftCount || slot < 0 || slot > 1 || !fs::is_directory(directory))
    throw std::runtime_error("Invalid worker model/view/output directory");
  rejectDebugOverrides();
  Context context;
  const Producer gl = producer();
  g_ren.W = g_ren.H = 32; // Keep setRT aspect finite without allocating scene targets.
  g_shaderCacheDir.clear(); // Driver programs are deliberately compiled and never bundled.
  FrameParams fp{};
  fp.plane.on = true; fp.plane.model = model;
  packModelOf(model, fp.plane.M);
  fp.plane.rot[0] = fp.plane.rot[4] = fp.plane.rot[8] = 1.f;
  fp.plane.PS[3] = float(slot);
  // Match the first frame built by Game::prewarm -> update(1/60) -> menuTour,
  // including the rotating fan phase used by part-local ambient occlusion.
  // These startup-state rules are pinned by test_canonical_startup_contract.
  // They do not replace the original hullStateList gear/control animation sweep.
  const float firstPrewarmTime = 3.f + 1.f / 60.f;
  fp.plane.Pr[0] = firstPrewarmTime * 250.f;
  fp.plane.Pr[1] = 1.f; // menuTour runs every engine at 2400 RPM / full visual blur.
  fp.plane.Pr[2] = float(std::max(kAircraft[model].blades, 2));
  fp.plane.Pr[3] = 0.f; // no asymmetric flap failure at startup.
  for (int engine = 0; engine < 4; ++engine)
    fp.plane.engineHealth[engine] = engine < kAircraft[model].engines ? 1.f : 0.f;
  static_assert((3.f + 1.f / 60.f) * 250.f == 754.16668701171875f, "Canonical prewarm phase changed");
  fp.camRight = vec3(1, 0, 0); fp.camUp = vec3(0, 1, 0); fp.camBack = vec3(0, 0, 1);
  const auto filename = g_ren.prebuiltAircraftFilename(model, slot);
  if (fs::exists(directory / filename) || fs::exists(directory / receiptName(model, slot)))
    throw std::runtime_error("Worker refuses an existing model/view output");
  std::string error;
  if (!g_ren.exportPrebuiltAircraft(fp, slot, directory.string(), error))
    throw std::runtime_error(std::string(kAircraft[model].name) + (slot ? " cockpit: " : " exterior: ") + error);
  glFinish();
  if (glGetError() != GL_NO_ERROR) throw std::runtime_error("OpenGL reported an error after export; publication refused");
  if (g_ren.bakeBuilt != 1) throw std::runtime_error("Worker did not perform exactly one fresh production bake");
  writeText(directory / receiptName(model, slot), receipt(model, slot, gl));
}
void runWorker(const std::string& executable, int model, int slot, const fs::path& directory) {
  const std::string modelText = std::to_string(model), slotText = std::to_string(slot), output = directory.string();
  char* const args[] = {const_cast<char*>(executable.c_str()), const_cast<char*>("--bake-one"),
                       const_cast<char*>(modelText.c_str()), const_cast<char*>(slotText.c_str()),
                       const_cast<char*>(output.c_str()), nullptr};
  pid_t pid = 0;
  const int result = posix_spawn(&pid, executable.c_str(), nullptr, nullptr, args, environ);
  if (result != 0) throw std::runtime_error(std::string("Could not start bake worker: ") + std::strerror(result));
  int status = 0; pid_t waited;
  do { waited = waitpid(pid, &status, 0); } while (waited < 0 && errno == EINTR);
  if (waited != pid || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
    throw std::runtime_error(std::string("Bake worker failed: ") + kAircraft[model].name + (slot ? " cockpit" : " exterior"));
}
int integer(const char* value) {
  char* end = nullptr; errno = 0; const long result = std::strtol(value, &end, 10);
  if (errno || end == value || *end || result < 0 || result > 1000) throw std::runtime_error("Invalid numeric worker argument");
  return int(result);
}
}

int main(int argc, char** argv) {
  if (argc == 2 && std::string(argv[1]) == "--help") {
    std::printf("Usage: aircraft_mesh_export OUTPUT_DIRECTORY\n"
                "Creates a new directory with all 30 full-quality production aircraft meshes.\n"
                "Existing directories and diagnostic geometry overrides are refused.\n"
                "Diagnostic worker: aircraft_mesh_export --bake-one MODEL SLOT EXISTING_DIRECTORY\n"
                "A single worker result is incomplete and cannot be packaged.\n");
    return 0;
  }
  if (argc != 2 && !(argc == 5 && std::string(argv[1]) == "--bake-one")) {
    std::fprintf(stderr, "Usage: aircraft_mesh_export OUTPUT_DIRECTORY\n"); return 2;
  }
  fs::path staging;
  try {
    static_assert(kAircraftCount == 15, "Update export/bundle contract for a roster change");
    rejectDebugOverrides();
    if (argc == 5) { bakeOne(integer(argv[2]), integer(argv[3]), fs::absolute(argv[4])); return 0; }
    const fs::path output = fs::absolute(argv[1]).lexically_normal();
    if (fs::exists(output)) throw std::runtime_error("Output already exists; choose a new directory (no cache imports or in-place resumes)");
    fs::create_directories(output.parent_path());
    staging = output.parent_path() / ("." + output.filename().string() + ".baking." + std::to_string(getpid()) + "." +
                                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (!fs::create_directory(staging)) throw std::runtime_error("Could not create exclusive export staging directory");
    Producer gl;
    { Context context; gl = producer(); } // Release the probe context before starting any worker.
    const std::string executable = fs::read_symlink("/proc/self/exe").string();
    std::printf("Canonical producer: %s | %s | %s\nBuild source: %s\n", gl.vendor.c_str(), gl.renderer.c_str(), gl.version.c_str(), kAircraftBuildSourceDigest);
    std::ostringstream files;
    for (int model = 0; model < kAircraftCount; ++model) for (int slot = 0; slot < 2; ++slot) {
      std::printf("Baking %d/30: %s %s\n", model * 2 + slot + 1, kAircraft[model].name, slot ? "cockpit" : "exterior");
      std::fflush(stdout);
      const auto started = std::chrono::steady_clock::now();
      runWorker(executable, model, slot, staging);
      std::ifstream receiptFile(staging / receiptName(model, slot), std::ios::binary);
      std::ostringstream text; text << receiptFile.rdbuf();
      if (!receiptFile || text.str() != receipt(model, slot, gl))
        throw std::runtime_error("Worker producer/source/quality receipt mismatch");
      const auto id = g_ren.prebuiltAircraftIdentity(model, slot);
      const auto name = aircraftAsset::filename(id);
      aircraftAsset::MeshData checked; std::string error;
      if (name.empty() || !aircraftAsset::read((staging / name).string(), id, checked, error))
        throw std::runtime_error("Worker exported an invalid or incompatible mesh: " + error);
      fs::remove(staging / receiptName(model, slot));
      std::printf("Finished %s %s in %.3f s (%llu bytes)\n", kAircraft[model].name, slot ? "cockpit" : "exterior",
                  std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count(),
                  (unsigned long long)fs::file_size(staging / name));
      if (model || slot) files << ",\n";
      files << "    {\"model_index\":" << model << ",\"slot\":" << slot << ",\"model_id\":" << jsonString(kAircraft[model].id)
            << ",\"model_name\":" << jsonString(kAircraft[model].name) << ",\"file\":" << jsonString(name) << "}";
    }
    std::ostringstream report;
    report << "{\n  \"schema\":1,\n  \"implementation\":\"production-strict-fresh-v1\",\n"
           << "  \"complete\":true,\n  \"full_quality\":true,\n  \"asset_count\":30,\n"
           << "  \"format_version\":" << aircraftAsset::kFormatVersion << ",\n  \"algorithm_version\":" << aircraftMesh::kAlgorithmVersion << ",\n"
           << "  \"source_digest\":" << jsonString(kAircraftBuildSourceDigest) << ",\n"
           << "  \"geometry_digest\":" << jsonString(kAircraftGeometrySourceDigest) << ",\n"
           << "  \"gl\":{\"vendor\":" << jsonString(gl.vendor) << ",\"renderer\":" << jsonString(gl.renderer) << ",\"version\":" << jsonString(gl.version) << "},\n"
           << "  \"files\":[\n" << files.str() << "\n  ]\n}\n";
    writeText(staging / "bake-report.json", report.str());
    // Complete publication requires every worker receipt and decoded mesh. Python
    // independently revalidates every binary, source input and count before ZIP.
    if (fs::exists(output)) throw std::runtime_error("Output appeared during export; refusing replacement");
    fs::rename(staging, output); staging.clear();
    std::printf("Exported 30 full-quality meshes to %s\nValidate and bundle with tools/mesh_assets/mesh_assets.py.\n", output.string().c_str());
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "Aircraft mesh export failed: %s\nNo release bundle was published.\n", error.what());
    if (!staging.empty()) { std::error_code ignored; fs::remove_all(staging, ignored); }
    return 1;
  }
}
