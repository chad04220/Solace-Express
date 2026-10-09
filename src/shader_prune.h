#pragma once
// A program's GLSL as it is assembled (shaders.h) carries the whole shared library: every program's source changed
// with any edit anywhere in it, so a new version recompiled nearly every program, and the aircraft bodies' cache
// (keyed by the library's sources) rebuilt them all for a comment. pruneShader keeps what the program can run: the
// comments go, and every function nothing reachable from main() calls (and its prototypes), repeated until nothing
// more goes. The program cache's key and the driver's compiler both see only that, so an edit reaches only the
// programs that run the code it touched. Everything else - #version, preprocessor lines, uniforms, globals, structs,
// constants - stays as it was, and a top-level declaration it can't read plainly is kept whole.
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace shaderPrune {

inline bool idStart(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
inline bool idChar(char c) { return idStart(c) || (c >= '0' && c <= '9'); }

// the source without its comments (a comment that ends a line takes its trailing blanks with it; a block comment
// becomes one space, or its newlines, so the line numbers past it stay put)
inline std::string stripComments(const std::string& s) {
  std::string o; o.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '/') {
      while (!o.empty() && (o.back() == ' ' || o.back() == '\t')) o.pop_back();
      while (i < s.size() && s[i] != '\n') i++;
    } else if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '*') {
      size_t e = s.find("*/", i + 2); if (e == std::string::npos) e = s.size(); else e += 2;
      bool nl = false; for (size_t k = i; k < e; k++) if (s[k] == '\n') { o += '\n'; nl = true; }
      if (!nl) o += ' ';
      i = e;
    } else o += s[i++];
  }
  return o;
}

// One top-level function definition or prototype: its text [b, e) and its name
struct Decl { size_t b, e; std::string name; };

inline std::string prune(const std::string& src) {
  const std::string s = stripComments(src);
  const size_t n = s.size();
  // ---- the top-level functions: at brace depth 0, a statement of plain identifiers, then "name(" ... ")" and a body
  // or ';'. (A statement with '=' or anything else before its '(' - a constant, layout(...), a struct - is not one;
  // nor one a preprocessor line cuts through.)
  std::vector<Decl> decls;
  {
    size_t i = 0, stmt = 0; bool lineStart = true, ppInStmt = false;
    auto skipSpace = [&](size_t k) { while (k < n && (s[k] == ' ' || s[k] == '\t' || s[k] == '\n' || s[k] == '\r')) k++; return k; };
    while (i < n) {
      char c = s[i];
      if (lineStart && c == '#') {   // a preprocessor line (with its continuations)
        const size_t p0 = i;
        while (i < n && s[i] != '\n') { if (s[i] == '\\' && i + 1 < n && s[i + 1] == '\n') i++; i++; }
        if (i < n) i++;
        // (between statements it is a statement of its own; inside an open one, that one straddles it: kept whole)
        if (skipSpace(stmt) >= p0) { stmt = i; ppInStmt = false; } else ppInStmt = true;
        continue;
      }
      if (c == '\n') { lineStart = true; i++; continue; }
      if (c != ' ' && c != '\t' && c != '\r') lineStart = false;
      if (c == ';' || c == '}') { i++; stmt = i; ppInStmt = false; continue; }
      if (c == '{') {   // a block at the top level: a struct's, or a body without a plain header - skipped whole
        int d = 0; size_t k = i;
        for (; k < n; k++) { if (s[k] == '{') d++; else if (s[k] == '}' && --d == 0) break; }
        i = k < n ? k + 1 : n; stmt = i; ppInStmt = false;
        while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;   // ("struct X {...} name;" ends at its ';')
        if (i < n && s[i] != '\n' && s[i] != ';') stmt = i;   // (the declarators of "struct {...} a, b;" stay with it)
        continue;
      }
      if (c == '(') {
        // the statement so far: identifiers only, at least a type and a name
        std::vector<std::pair<size_t, size_t>> ids; bool plain = !ppInStmt;
        for (size_t k = stmt; k < i && plain;) {
          char ck = s[k];
          if (ck == ' ' || ck == '\t' || ck == '\n' || ck == '\r') { k++; continue; }
          if (idStart(ck)) { size_t a = k; while (k < i && idChar(s[k])) k++; ids.push_back({a, k}); continue; }
          plain = false;
        }
        // the parameter list, then a body or ';'
        int d = 0; size_t k = i;
        for (; k < n; k++) { if (s[k] == '(') d++; else if (s[k] == ')' && --d == 0) break; }
        if (k >= n) break;
        size_t after = skipSpace(k + 1);
        if (plain && ids.size() >= 2 && after < n && (s[after] == '{' || s[after] == ';')) {
          bool pp = false;
          for (size_t q = i; q < after && !pp; q++) if (s[q] == '#' && (q == 0 || s[q - 1] == '\n')) pp = true;
          size_t e = after;
          if (s[after] == '{') {
            int bd = 0;
            for (; e < n; e++) { if (s[e] == '{') bd++; else if (s[e] == '}' && --bd == 0) break; }
            if (e >= n) break;
            e++;
            // (a preprocessor conditional opened or closed inside the body and not both: kept whole)
            int cond = 0; bool lead = true;
            for (size_t q = after; q < e; q++) {
              if (s[q] == '\n') { lead = true; continue; }
              if (lead && s[q] == '#') {
                size_t a = skipSpace(q + 1);
                if (s.compare(a, 2, "if") == 0) cond++; else if (s.compare(a, 5, "endif") == 0) cond--;
              }
              if (s[q] != ' ' && s[q] != '\t') lead = false;
            }
            if (cond != 0) pp = true;
          } else e = after + 1;
          if (!pp) decls.push_back({stmt, e, s.substr(ids.back().first, ids.back().second - ids.back().first)});
          i = e; stmt = e; ppInStmt = false; continue;
        }
        i = k + 1; continue;
      }
      i++;
    }
  }
  if (decls.empty()) return s;
  // ---- reachability: a function stays while any identifier outside its own declarations names it
  std::unordered_map<std::string, std::vector<size_t>> byName;
  for (size_t d = 0; d < decls.size(); d++) byName[decls[d].name].push_back(d);
  std::vector<bool> gone(decls.size(), false);
  // the identifiers in each declaration, and in the text outside every declaration (always kept)
  auto idsIn = [&](size_t b, size_t e, std::unordered_map<std::string, int>& out) {
    for (size_t k = b; k < e;) {
      if (idStart(s[k]) && (k == b || !idChar(s[k - 1]))) { size_t a = k; while (k < e && idChar(s[k])) k++; out[s.substr(a, k - a)]++; }
      else k++;
    }
  };
  std::unordered_map<std::string, int> refs;   // name -> how many times it appears in what is kept
  std::vector<std::unordered_map<std::string, int>> own(decls.size());
  {
    size_t at = 0;
    for (size_t d = 0; d < decls.size(); d++) { idsIn(at, decls[d].b, refs); idsIn(decls[d].b, decls[d].e, own[d]); at = decls[d].e; }
    idsIn(at, n, refs);
    for (auto& o : own) for (auto& kv : o) refs[kv.first] += kv.second;
  }
  // a name's own declarations name it once each (and a recursive body more: GLSL has no recursion)
  auto selfRefs = [&](const std::string& nm) { int k = 0; for (size_t d : byName[nm]) if (!gone[d]) { auto it = own[d].find(nm); if (it != own[d].end()) k += it->second; } return k; };
  for (bool changed = true; changed;) {
    changed = false;
    for (auto& kv : byName) {
      const std::string& nm = kv.first;
      if (nm == "main") continue;
      bool any = false; for (size_t d : kv.second) if (!gone[d]) any = true;
      if (!any || refs[nm] > selfRefs(nm)) continue;
      for (size_t d : kv.second) if (!gone[d]) { gone[d] = true; for (auto& r : own[d]) refs[r.first] -= r.second; }
      changed = true;
    }
  }
  // ---- the kept text, blank lines folded
  std::string o; o.reserve(n);
  size_t at = 0;
  for (size_t d = 0; d < decls.size(); d++) { if (!gone[d]) continue; o.append(s, at, decls[d].b - at); at = decls[d].e; }
  o.append(s, at, n - at);
  std::string f; f.reserve(o.size());
  int nl = 0;
  for (char c : o) { if (c == '\n') { if (++nl > 1) continue; } else if (c != ' ' && c != '\t' && c != '\r') nl = 0; f += c; }
  return f;
}

}  // namespace shaderPrune
