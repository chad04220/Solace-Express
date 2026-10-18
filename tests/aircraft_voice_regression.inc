// Included inside GameTest; also usable by a focused standalone resolver harness.
  static int aircraftVoiceRegressions(const std::string& voiceDir) {
    AtcVoice voice;
    if (!voice.load(voiceDir)) { printf("Aircraft voice regression: voice assets missing: FAIL\n"); return 1; }
    int fails = 0;
    auto check = [&](bool ok, const char* label) { printf("Aircraft voice regression: %s: %s\n", label, ok ? "ok" : "FAIL"); fails += !ok; };
    bool roster = true, named = true, decoded = true;
    for (int i = 0; i < kAircraftCount; ++i) {
      const std::string message = "SPLASH " + std::to_string(i + 1) + " - " + kAircraft[i].name + " down";
      AtcVoice::Tx tx;
      const bool ok = voice.resolve(message, "", false, tx);
      roster &= ok && tx.text == message && tx.prio == 45 && !tx.radio;
      named &= ok && tx.ids.size() == 4 && tx.ids[2] == std::string("fragment.nyx.craft_") + kAircraft[i].id && voice.text(tx.ids[2]) == kAircraft[i].name;
      for (const auto& id : tx.ids) decoded &= voice.has(id) && voice.decodes(id);
    }
    check(roster, "all 15 aircraft retain exact displayed identity, count and Nyx priority");
    check(named && decoded, "all 15 aircraft speak their own recorded names, every clip decoded");

    // A partial/broken pack must still fail closed: fallback is for absent name metadata,
    // never permission to pretend a required clip (or an indexed name) was available.
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / ("somnium_aircraft_voice_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    bool required = true;
    for (const char* missing : {"fragment.nyx.splash", "fragment.nyx.n14", "fragment.nyx.down", "fragment.nyx.craft_ambler"}) {
      std::ifstream in(voiceDir + "/voice_index.txt");
      std::ofstream out(root / "voice_index.txt");
      std::string row; bool removed = false;
      while (std::getline(in, row)) {
        if (row.rfind(std::string(missing) + "\t", 0) == 0) removed = true;
        else out << row << '\n';
      }
      out.close();
      AtcVoice partial; AtcVoice::Tx tx;
      const std::string message = std::string(missing) == "fragment.nyx.craft_ambler" ? "SPLASH 2 - Ambler 180 down" : "SPLASH 14 - Thistledown L4 down";
      required &= removed && partial.load(root.string()) && !partial.resolve(message, "", false, tx) && tx.ids.empty();
    }
    // an aircraft with no name in the index (one added after a voice pack): Splash <count> down, never another's name
    bool fallback = false;
    {
      std::ifstream in(voiceDir + "/voice_index.txt");
      std::ofstream out(root / "voice_index.txt");
      std::string row; int removed = 0;
      while (std::getline(in, row)) {
        if (row.rfind("@craft.Thistledown L4\t", 0) == 0) removed++;
        else out << row << '\n';
      }
      out.close();
      AtcVoice partial; AtcVoice::Tx tx;
      const std::vector<std::string> want = {"fragment.nyx.splash", "fragment.nyx.n14", "fragment.nyx.down"};
      fallback = removed == 1 && partial.load(root.string()) && partial.resolve("SPLASH 14 - Thistledown L4 down", "", false, tx) && tx.ids == want && tx.text == "SPLASH 14 - Thistledown L4 down";
    }
    std::error_code ec; fs::remove_all(root, ec);
    check(required, "missing base/count/down clips and broken indexed aircraft names still reject speech");
    check(fallback, "an aircraft without an indexed name says Splash <count> down, the full name on screen");
    return fails;
  }
