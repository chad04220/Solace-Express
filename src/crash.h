#pragma once
// Crash reports. On Windows (crash_win.cpp) an unhandled exception, std::terminate, abort() or a pure virtual call
// writes a minidump, a summary and a copy of startup.log to <save folder>\crash, and leaves a marker there; the next
// launch takes the marker and the main menu says where the report is. Nothing is uploaded (PRIVACY.md).
#include <string>

constexpr int kCrashReportsKept = 10;   // the newest reports kept; older ones are deleted at launch

// The report the last session left (the summary's path), or empty; the marker is cleared, so it is said once
std::string crashTakePending(const std::string& crashDir);
// Deletes all but the newest `keep` reports (each report's files share its "crash-<time>" stem)
void crashPrune(const std::string& crashDir, int keep = kCrashReportsKept);

#ifdef _WIN32
// Installs the handlers. version names the build in the summary. Call again after the graphics driver has loaded:
// some drivers install their own unhandled-exception filter, and this puts ours back
void crashInstall(const std::string& crashDir, const std::string& logPath, const char* version);
#endif
