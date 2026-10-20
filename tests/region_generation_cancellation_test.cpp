// Cooperative cancellation of the real CPU World pipeline. The default suite
// is short; --full exercises every heavy stage and cache I/O, and --all selects
// all five archetypes. Run serially with other full-world/GL tests.
#include "../src/procedural_regions.h"
#include "../src/scenery.h"
#include <array>
#include <chrono>
#include <filesystem>
#include <future>
#include <set>
#include <stdexcept>

namespace {
using Clock = std::chrono::steady_clock;
using Phase = GenerationPhase;
int checks = 0, failures = 0;
double worstCancelMs = 0;
void check(bool ok, const char* message) {
  ++checks;
  if (!ok) { ++failures; std::printf("FAIL: %s\n", message); }
}
const char* phaseName(Phase p) {
  switch (p) {
    case Phase::Configuring: return "configuring";
    case Phase::CacheRead: return "cache-read";
    case Phase::Terrain: return "terrain";
    case Phase::Roads: return "road-grid-sampling";
    case Phase::RoadRouting: return "road-routing";
    case Phase::Settlements: return "settlements";
    case Phase::RoadGrid: return "road-index";
    case Phase::CommunityLots: return "community-lots";
    case Phase::Mask: return "mask";
    case Phase::HeightBounds: return "height-bounds";
    case Phase::Envelope: return "envelope";
    case Phase::Scenery: return "road-furniture";
    case Phase::Landmarks: return "landmarks";
    case Phase::CacheWrite: return "cache-write";
    case Phase::Complete: return "complete";
  }
  return "unknown";
}
struct Scratch {
  std::filesystem::path dir = std::filesystem::temp_directory_path() /
    ("somnium-cancellation-" + std::to_string(Clock::now().time_since_epoch().count()));
  Scratch() { std::filesystem::create_directory(dir); }
  ~Scratch() { std::error_code ec; std::filesystem::remove_all(dir, ec); }
  std::string path(const char* name) const { return (dir / name).string(); }
};
struct Digest {
  uint64_t value = 1469598103934665603ull;
  template<class T> void add(const T& v) {
    const auto* p = reinterpret_cast<const unsigned char*>(&v);
    for (size_t i = 0; i < sizeof(v); ++i) { value ^= p[i]; value *= 1099511628211ull; }
  }
};
uint64_t sceneryDigest(const World& w) {
  Digest d;
  const auto& lots = settlementLots(w); d.add(lots.size());
  for (const Lot& l : lots) {
    d.add(l.present); d.add(l.cx); d.add(l.cz); d.add(l.hw); d.add(l.hd);
    d.add(l.wallH); d.add(l.roofH); d.add(l.ground); d.add(l.type); d.add(l.ridgeX);
    d.add(l.seed); d.add(l.yaw); d.add(l.town); d.add(l.kind); d.add(l.street); d.add(l.share);
  }
  const auto& trees = settlementTrees(w); d.add(trees.size());
  for (const auto& t : trees) { d.add(t.x); d.add(t.z); d.add(t.scale); d.add(t.kind); d.add(t.seed); }
  for (const auto* items : {&civicLandmarks(w), &roadFurniture(w), &landmarks(w)}) {
    d.add(items->size()); for (const auto& p : *items) { d.add(p.kind); d.add(p.e); }
  }
  for (const auto& c : landmarkClearings(w)) { d.add(c.x); d.add(c.y); d.add(c.z); }
  for (const auto& r : ferryRoutes(w)) {
    d.add(r.townA); d.add(r.townB); d.add(r.pts.size());
    for (const auto& p : r.pts) { d.add(p.x); d.add(p.y); }
    for (float a : r.along) d.add(a);
  }
  for (const auto* c : {&sceneryState(w).furniture.chunk, &sceneryState(w).landmarks.chunk})
    for (int i : *c) d.add(i);
  return d.value;
}
bool sameFile(const std::string& a, const std::string& b) {
  std::unique_ptr<FILE, decltype(&fclose)> left(fopen(a.c_str(), "rb"), &fclose), right(fopen(b.c_str(), "rb"), &fclose);
  if (!left || !right) return false;
  std::array<unsigned char, 65536> x{}, y{};
  for (;;) {
    const size_t nx = fread(x.data(), 1, x.size(), left.get()), ny = fread(y.data(), 1, y.size(), right.get());
    if (nx != ny || memcmp(x.data(), y.data(), nx)) return false;
    if (nx < x.size()) return !ferror(left.get()) && !ferror(right.get());
  }
}
void cachePublicationTests() {
  Scratch scratch;
  const auto cache = scratch.path("replace.bin"), previous = scratch.path("previous.bin");
  World small; small.hm = {11.f}; small.saveCache(cache, "first");
  std::filesystem::copy_file(cache, previous);
  small.hm = {22.f}; small.saveCache(cache, "second");
  bool intendedReplacement = false;
  if (FILE* f = fopen(cache.c_str(), "rb")) {
    uint32_t magic = 0; char stamp[64] = {}; uint64_t count = 0; float payload = 0.f;
    intendedReplacement = fread(&magic, sizeof(magic), 1, f) == 1 && magic == 0x574c4439u &&
      fread(stamp, 1, sizeof(stamp), f) == sizeof(stamp) && memcmp(stamp, "second", 7) == 0 &&
      fread(&count, sizeof(count), 1, f) == 1 && count == 1 &&
      fread(&payload, sizeof(payload), 1, f) == 1 && payload == 22.f;
    fclose(f);
  }
  check(std::filesystem::is_regular_file(cache) && intendedReplacement && !sameFile(cache, previous) &&
      !std::filesystem::exists(cache + ".tmp"),
    "successful cache publication installs the intended second header and payload and leaves no temp");
  const auto blocked = scratch.path("blocked.bin");
  std::filesystem::create_directory(blocked);
  const auto marker = (std::filesystem::path(blocked) / "existing-data").string();
  std::filesystem::copy_file(previous, marker);
  small.saveCache(blocked, "cannot-replace-directory");
  check(std::filesystem::is_directory(blocked) && sameFile(marker, previous),
    "failed cache publication preserves the existing destination and its contents");
  check(!std::filesystem::exists(blocked + ".tmp"), "failed cache publication removes only its temp file");
#ifdef _WIN32
  // Windows refuses replacement of a read-only destination. Exercise failure
  // against an actual previous regular cache, not only the directory case.
  std::filesystem::permissions(previous, std::filesystem::perms::owner_read);
  small.saveCache(previous, "blocked-read-only");
  check(sameFile(previous, marker), "failed Windows replacement preserves the completed old cache");
  std::filesystem::permissions(previous, std::filesystem::perms::owner_all);
#endif
}
void unitTests(const procedural::Descriptor& descriptor) {
  auto canceled = std::make_shared<GenerationCancelState>(); canceled->requested = true;
  World untouched; untouched.regionId = "unchanged"; untouched.hm = {42.f};
  bool threw = false;
  {
    ScopedGenerationCancellation scope(canceled);
    try { untouched.buildProcedural(descriptor); } catch (const GenerationCancelled&) { threw = true; }
  }
  check(threw && untouched.regionId == "unchanged" && untouched.hm == std::vector<float>{42.f}, "pre-canceled build does not mutate its candidate");
  check(!currentGenerationCancellation(), "cancellation scope restores absent token");
  auto outer = std::make_shared<GenerationCancelState>();
  {
    ScopedGenerationCancellation scope(outer);
    { ScopedGenerationCancellation inner(canceled); }
    check(currentGenerationCancellation() == outer, "nested scope restores its prior token");
  }
  std::weak_ptr<GenerationCancelState> lifetime;
  std::atomic<bool> inherited{true};
  {
    auto owned = std::make_shared<GenerationCancelState>(); lifetime = owned;
    ScopedGenerationCancellation scope(owned); owned.reset();
    check(!lifetime.expired(), "scope owns token after caller releases its shared_ptr");
    ScopedParallelLimit limit(2);
    parallelFor(31, [&](int) {
      const auto expected = lifetime.lock();
      if (!expected || currentGenerationCancellation() != expected) inherited = false;
      {
        auto nested = std::make_shared<GenerationCancelState>();
        ScopedGenerationCancellation inner(nested); nested.reset();
        if (!currentGenerationCancellation()) inherited = false;
      }
      if (currentGenerationCancellation() != expected) inherited = false;
    });
    check(inherited && !lifetime.expired(), "workers inherit owning token and restore nested scopes before join");
  }
  check(lifetime.expired(), "joined workers and completed scope release every token owner");
  std::atomic<int> started{0}, exited{0};
  auto token = std::make_shared<GenerationCancelState>();
  auto future = std::async(std::launch::async, [&] {
    ScopedParallelLimit limit(2); ScopedGenerationCancellation scope(token);
    try {
      parallelFor(64, [&](int) {
        struct Exit { std::atomic<int>& n; ~Exit() { ++n; } } exit{exited};
        ++started;
        while (!token->requested.load()) std::this_thread::yield();
        generationCheckpoint(); // must see the token on a parallelFor worker
      });
    } catch (const GenerationCancelled&) { return true; }
    return false;
  });
  while (!started.load()) std::this_thread::yield();
  token->requested = true;
  const bool ready = future.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
  check(ready && future.get(), "worker cancellation reaches the owning future without terminating");
  check(started == exited && started <= 2, "cancellation stops row dispatch and joins only its owned workers");
  bool ordinary = false;
  try { ScopedParallelLimit limit(2); parallelFor(100, [](int i) { if (i == 3) throw std::runtime_error("worker failure"); }); }
  catch (const std::runtime_error&) { ordinary = true; }
  check(ordinary, "ordinary worker exception is rethrown on the caller after join");
  std::array<std::atomic<int>, 31> visits{};
  { ScopedParallelLimit limit(2); parallelFor((int)visits.size(), [&](int i) { ++visits[i]; }); }
  bool once = true; for (auto& v : visits) once &= v == 1;
  check(once, "uncanceled parallel loop still visits each row exactly once");
  {
    ScopedParallelLimit limit(1); ScopedGenerationCancellation scope(canceled);
    int visits = 0; bool stopped = false;
    try { parallelFor(4, [&](int) { ++visits; }); } catch (const GenerationCancelled&) { stopped = true; }
    check(stopped && visits == 0, "single-thread fallback obeys pre-cancellation");
  }
}

const WorldSceneryState* activeState = nullptr;
uint64_t activeDigest = 0;
void seedActiveWorld() {
  g_world.regionId = "active-flight-sentinel"; g_world.hm = {61.f, 0.f, .5f, 0.f}; g_worldStage = 83;
  auto& state = sceneryStateForBuild(g_world);
  Lot lot{}; lot.present = true; lot.cx = 16.f; lot.cz = 24.f; lot.kind = EK_HOUSE;
  state.lots.push_back(lot); state.lotCells[0] = {0};
  const PlacedEnt placed{EK_STADIUM, {32.f, 60.f, 48.f, 0.f, 1.f, 1.f, 1.f, .75f}};
  state.civic.push_back(placed); state.landmarks.build({placed}); state.furniture.build({placed});
  activeState = g_world.sceneryState.get(); activeDigest = sceneryDigest(g_world);
}
bool activeUnchanged() {
  return g_world.regionId == "active-flight-sentinel" && g_worldStage == 83 && g_world.hm.front() == 61.f &&
    g_world.sceneryState.get() == activeState && sceneryDigest(g_world) == activeDigest &&
    &settlementLots() == &settlementLots(g_world) && &landmarks() == &landmarks(g_world);
}
void cancelBuild(const procedural::Descriptor& descriptor, Phase target, const std::string& cachePath = {},
                 const std::string& stamp = "cancellation-test-v1") {
  const auto token = std::make_shared<GenerationCancelState>(); token->trackCheckpoints = true;
  auto future = std::async(std::launch::async, [=] {
    ScopedParallelLimit limit(2); ScopedGenerationCancellation scope(token);
    try { World candidate; candidate.buildProcedural(descriptor, cachePath, stamp, true); }
    catch (const GenerationCancelled&) { return true; }
    return false;
  });
  const auto begin = Clock::now();
  bool reached = false, isolated = true; uint64_t startingChecks = 0; size_t reads = 0;
  // Atomic telemetry avoids peeking at unfinished World vectors. Let at least
  // eight real checkpoints run in the requested stage before requesting cancel.
  while (future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
    const Phase phase = token->phase.load(std::memory_order_acquire);
    if (phase == target) {
      if (!reached) { reached = true; startingChecks = token->checkpoints.load(); }
      if (token->checkpoints.load() >= startingChecks + 8) break;
    } else if (reached) { check(false, "target phase ended before cancellation synchronization"); break; }
    isolated &= activeUnchanged(); ++reads;
    if (Clock::now() - begin > std::chrono::minutes(3)) { check(false, "build did not reach target phase within test safety timeout"); break; }
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }
  check(reached, "real full build reaches the requested cancellation phase");
  const Phase actual = token->phase.load();
  const auto request = Clock::now(); token->requested.store(true, std::memory_order_relaxed);
  while (future.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready) {
    isolated &= activeUnchanged(); ++reads;
  }
  const double ms = std::chrono::duration<double, std::milli>(Clock::now() - request).count();
  worstCancelMs = std::max(worstCancelMs, ms);
  check(future.get(), "real candidate future finishes with cancellation rather than publishing");
  check(reads > 0 && isolated && activeUnchanged(), "active flight remains unchanged while canceled candidate drains");
  // This is a deliberately loose regression guard, not a real-time guarantee.
  // An OS I/O call, allocation or destructor cannot itself be interrupted.
  check(ms < 2000., "canceled candidate drains within the two-second regression allowance");
  std::printf("CANCEL archetype=%s target=%s actual=%s latency_ms=%.3f active_reads=%zu\n",
    procedural::archetypeName(descriptor.archetype), phaseName(target), phaseName(actual), ms, reads);
  fflush(stdout);
}
void fullTests(const procedural::Descriptor& descriptor) {
  Scratch scratch; const auto original = scratch.path("baseline.bin"), recovered = scratch.path("recovered.bin");
  uint64_t sceneryBefore = 0;
  {
    World world; ScopedParallelLimit limit(2);
    world.buildProcedural(descriptor, original, "cancellation-test-v1", true);
    sceneryBefore = sceneryDigest(world);
  }
  cancelBuild(descriptor, Phase::CacheRead, original);
  const Phase phases[] = {Phase::Terrain, Phase::Roads, Phase::RoadRouting, Phase::Settlements,
    Phase::RoadGrid, Phase::CommunityLots, Phase::Mask, Phase::HeightBounds, Phase::Envelope,
    Phase::Scenery, Phase::Landmarks};
  for (Phase phase : phases) cancelBuild(descriptor, phase);
  const auto destination = scratch.path("preserved.bin");
  std::filesystem::copy_file(original, destination);
  // A changed stamp starts regeneration over a real, previously completed cache.
  cancelBuild(descriptor, Phase::CacheWrite, destination, "cancellation-replacement-v2");
  check(sameFile(destination, original), "canceling cache serialization preserves the previous completed file");
  check(!std::filesystem::exists(destination + ".tmp"), "canceled partial cache temp file is removed");
  {
    auto fresh = std::make_shared<GenerationCancelState>();
    ScopedParallelLimit limit(2); ScopedGenerationCancellation scope(fresh);
    World world; world.buildProcedural(descriptor, recovered, "cancellation-test-v1", true);
    check(fresh->phase == Phase::Complete && sceneryDigest(world) == sceneryBefore,
      "fresh uncanceled generation after cancellation has identical scenery");
  }
  check(sameFile(original, recovered), "fresh uncanceled generation has byte-identical complete world cache");
  check(activeUnchanged(), "all builds and cache cancellations preserve active World state");
  std::printf("PARITY archetype=%s cache=identical scenery=identical\n", procedural::archetypeName(descriptor.archetype)); fflush(stdout);
}
} // namespace

int main(int argc, char** argv) {
  bool full = false, all = false, skipFirst = false;
  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--full")) full = true;
    else if (!strcmp(argv[i], "--all")) all = true;
    else if (!strcmp(argv[i], "--skip-first")) skipFirst = true;
    else { std::fprintf(stderr, "usage: %s [--full] [--all] [--skip-first]\n", argv[0]); return 2; }
  }
  std::vector<procedural::Descriptor> regions; std::set<procedural::Archetype> found;
  for (const auto& d : procedural::recommendations())
    if (!d.authored && found.insert(d.archetype).second) regions.push_back(d);
  check(regions.size() == 5, "test catalog covers all five archetypes");
  if (regions.empty()) return 1;
  unitTests(regions.front()); cachePublicationTests(); seedActiveWorld();
  for (size_t i = skipFirst ? 1 : 0; i < regions.size(); ++i) {
    const auto& d = regions[i];
    if (full) fullTests(d); else cancelBuild(d, Phase::Terrain);
    if (!all) break;
  }
  std::printf("region generation cancellation: %d checks, %d failures; worst_observed_cancel_ms=%.3f\n", checks, failures, worstCancelMs);
  return failures ? 1 : 0;
}
