// Included inside GameTest so these checks exercise the production private hint/comms paths.
  static int controlVoiceRegressions() {
#ifdef SOLACE_ASSETS
    Game q; q.initHeadless();
    if (!q.atc.load(std::string(SOLACE_ASSETS) + "/voice")) { printf("Control voice regression: voice assets missing: FAIL\n"); return 1; }
    int fails = 0, covered = 0;
    auto check = [&](bool ok, const char* name) { printf("Control voice regression: %s: %s\n", name, ok ? "ok" : "FAIL"); fails += !ok; };
    bool all = true, neutral = true;
    for (int lesson = 0; lesson < 4; lesson++) for (size_t phase = 0; phase < g_story[lesson].hints.size(); phase++) {
      if (g_story[lesson].hints[phase].empty()) continue;
      const auto& c = g_story[lesson];
      for (int layout = 0; layout < 4; layout++) {
        q.set.resetBindings();
        if (layout >= 2) { q.set.keyBind[ACT_PARK] = 'P'; q.set.keyBind[ACT_MAP] = 'M'; q.set.keyBind[ACT_BRAKE] = 'J'; q.set.padBind[ACT_PARK] = PAD_Y; q.set.padBind[ACT_BRAKE] = PAD_X; }
        const std::string shown = q.expandHint(c.hints[phase], layout % 2 != 0);
        AtcVoice::Tx tx; bool ok = q.atc.resolveLesson(c.id, (int)phase, shown, tx);
        const std::string expected = "lesson." + c.id + ".phase" + std::to_string(phase);
        all &= ok && tx.ids.size() == 1 && tx.ids[0] == expected && q.atc.decodes(expected);
        const std::string speech = q.atc.text(expected);
        for (const char* key : {"D-pad", "D-PAD", " RT", " LT", "LB/RB", "left stick", "right stick", "Press N", "press B", "Hold A", "SHIFT", "CTRL"}) neutral &= speech.find(key) == std::string::npos;
      }
      covered++;
    }
    check(covered == 25 && all, "all 25 instructor/examiner phases resolve and decode on keyboard, pad and both remapped layouts");
    check(neutral, "recorded transcripts contain no fixed keyboard/controller instruction names");
    size_t indexed = 0, undecodable = 0;
    for (const auto& id : q.atc.indexed()) { indexed++; if (!q.atc.decodes(id)) undecodable++; }
    printf("Control voice regression: all indexed recordings: %zu decoded, %zu failed\n", indexed - undecodable, undecodable);
    check(indexed > 0 && undecodable == 0, "lesson changes preserve decoding of the complete voice bank");
    q.set.resetBindings(); q.startFlight(g_story[0], 0, Career::SRC_LESSON); q.commsPending.clear();
    q.updateLessonHint();
    const bool queued = q.commsPending.size() == 1 && q.commsPending[0].mission == "L1" && q.commsPending[0].lessonPhase == 0 && q.hint.find("Press B ") != std::string::npos;
    q.in.pad = true; q.padActive = true; q.updateLessonHint();
    const bool defaultPad = q.hint.find("D-PAD LEFT") != std::string::npos && q.hint.find("hold RT") != std::string::npos;
    q.set.padBind[ACT_PARK] = PAD_Y; q.updateLessonHint();
    const bool remapPad = q.hint.find("Press Y ") != std::string::npos && q.commsPending.size() == 1;
    q.padActive = false; q.set.keyBind[ACT_PARK] = 'P'; q.updateLessonHint();
    const bool remapKey = q.hint.find("Press P ") != std::string::npos && q.commsPending.size() == 1;
    const std::string queuedText = q.commsPending.empty() ? "" : q.commsPending[0].text;
    q.screen = SCR_LOADING;   // same comms pipeline, before the tower begins its departure exchange
    q.updateComms(0.7f);   // consumes the semantic lesson message and actually starts its decoded transmission
    const bool spoken = q.commsPending.empty() && q.atc.busy() && !q.atc.history.empty() && q.atc.history.back() == queuedText;
    check(queued && defaultPad && remapPad && remapKey && spoken, "production hint queue speaks after mid-phase device switch and rebinding, without duplicate prompts");
    const std::string brakeKey = q.expandHint(g_story[1].hints[6]), brakePad = q.expandHint(g_story[2].hints[6], true);
    check(brakeKey.find(q.actLabel(ACT_BRAKE, false)) != std::string::npos && brakePad.find(q.actLabel(ACT_BRAKE, true)) != std::string::npos, "landing hints name the held wheel brake action");
    q.atc.cancel();
    AtcVoice::Tx invalid;
    check(!q.atc.resolveLesson("L1", -1, "", invalid) && !q.atc.resolveLesson("L1", 99, "", invalid)
          && !q.atc.resolveLesson("", 0, "", invalid) && !q.atc.resolveLesson("UNKNOWN", 0, "", invalid),
          "unknown mission or phase cannot select another lesson recording");
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / ("solace_lesson_voice_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    bool rejectsLegacy = true, warnsOnce = true, resetsWarning = true;
    for (int mode = 0; mode < 5; mode++) {
      const fs::path dir = root / std::to_string(mode); fs::create_directories(dir);
      if (mode < 4) {
        std::ofstream out(dir / "voice_index.txt");
        out << "lesson.L1.phase0\tbroken.wav\tTap D-pad Left to release the parking brake.\tinstructor\t65\t"
            << (mode == 0 ? "line" : "lesson") << "\tPress B to release the parking brake.\t" << (mode == 1 ? "L2" : "L1") << "\n";
      }
      if (mode == 3) { std::ofstream out(dir / "broken.wav"); out << "This is not a WAV file."; }
      q.atc.load(dir.string());
      if (mode == 0) {
        for (bool pad : {false, true}) rejectsLegacy &= !q.atc.resolve("Tap D-pad Left to release the parking brake.", "L1", pad, invalid)
          && !q.atc.resolve("Press B to release the parking brake.", "L1", pad, invalid);
        rejectsLegacy &= !q.atc.resolveLesson("L1", 0, "Current controls", invalid);
      }
      if (mode == 1) rejectsLegacy &= !q.atc.resolveLesson("L1", 0, "Current controls", invalid);
      q.startFlight(g_story[0], 0, Career::SRC_LESSON);
      resetsWarning &= !q.lessonVoiceWarned;
      q.screen = SCR_LOADING; q.toasts.clear(); q.commsPending.clear(); q.updateLessonHint(); q.updateComms(0.7f);
      const auto isWarning = [](const Game::Toast& t) { return t.text.find("Lesson recording unavailable.") == 0; };
      warnsOnce &= q.lessonVoiceWarned && q.commsPending.empty() && !q.atc.busy()
        && std::count_if(q.toasts.begin(), q.toasts.end(), isWarning) == 1;
      q.commsPending.push_back({q.hint, "L1", 0}); q.updateComms(0.7f);
      warnsOnce &= q.commsPending.empty() && std::count_if(q.toasts.begin(), q.toasts.end(), isWarning) == 1;
      q.atc.cancel();
    }
    check(rejectsLegacy, "legacy fixed-control rows and mismatched lesson identity never resolve");
    check(warnsOnce, "legacy, mismatched, missing, corrupt and absent voice assets warn once without false speech");
    check(resetsWarning, "a new flight resets the missing-lesson warning");
    std::error_code ec; fs::remove_all(root, ec);
    return fails;
#else
    printf("Control voice regression: voice assets unavailable: FAIL\n"); return 1;
#endif
  }
