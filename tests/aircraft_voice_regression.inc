// Included inside GameTest; also usable by a focused standalone resolver harness.
  static int aircraftVoiceRegressions(const std::string& voiceDir) {
    AtcVoice voice;
    if (!voice.load(voiceDir)) { printf("Aircraft voice regression: voice assets missing: FAIL\n"); return 1; }
    int fails = 0;
    auto check = [&](bool ok, const char* label) { printf("Aircraft voice regression: %s: %s\n", label, ok ? "ok" : "FAIL"); fails += !ok; };
    bool roster = true, legacy = true, fallback = true, decoded = true;
    int named = 0, generic = 0;
    for (int i = 0; i < kAircraftCount; ++i) {
      const std::string message = "SPLASH " + std::to_string(i + 1) + " - " + kAircraft[i].name + " down";
      AtcVoice::Tx tx;
      const bool ok = voice.resolve(message, "", false, tx);
      roster &= ok && tx.text == message && tx.prio == 45 && !tx.radio;
      if (i == kLarkspur || i == kAtlas) {
        const std::vector<std::string> want = {"fragment.nyx.splash", "fragment.nyx.n" + std::to_string(i + 1), "fragment.nyx.down"};
        fallback &= ok && tx.ids == want;
        generic++;
      } else {
        legacy &= ok && tx.ids.size() == 4 && voice.text(tx.ids[2]) == kAircraft[i].name;
        named++;
      }
      for (const auto& id : tx.ids) decoded &= voice.has(id) && voice.decodes(id);
    }
    check(roster && named == 13 && generic == 2, "all 15 aircraft retain exact displayed identity, count and Nyx priority");
    check(legacy, "all 13 existing aircraft keep their actual recorded names");
    check(fallback && decoded, "Larkspur and Atlas use only decoded Splash/count/down recordings, without false aircraft names");

    // A partial/broken pack must still fail closed: fallback is for absent name metadata,
    // never permission to pretend a required clip (or an indexed name) was available.
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / ("solace_aircraft_voice_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    bool required = true;
    for (const char* missing : {"fragment.nyx.splash", "fragment.nyx.n14", "fragment.nyx.down", "fragment.nyx.craft_wren"}) {
      std::ifstream in(voiceDir + "/voice_index.txt");
      std::ofstream out(root / "voice_index.txt");
      std::string row; bool removed = false;
      while (std::getline(in, row)) {
        if (row.rfind(std::string(missing) + "\t", 0) == 0) removed = true;
        else out << row << '\n';
      }
      out.close();
      AtcVoice partial; AtcVoice::Tx tx;
      const std::string message = std::string(missing) == "fragment.nyx.craft_wren" ? "SPLASH 2 - Wren 180 down" : "SPLASH 14 - Larkspur L4 down";
      required &= removed && partial.load(root.string()) && !partial.resolve(message, "", false, tx) && tx.ids.empty();
    }
    std::error_code ec; fs::remove_all(root, ec);
    check(required, "missing base/count/down clips and broken indexed aircraft names still reject speech");
    return fails;
  }
