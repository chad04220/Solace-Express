#pragma once
// A program's GLSL as it is assembled (shaders.h) carries the whole shared library: every program's source changed
// with any edit anywhere in it, so a new version recompiled nearly every program, and the aircraft bodies' cache
// (keyed by the library's sources) rebuilt them all for a comment. pruneShader keeps what the program can run: the
// comments go, and every function nothing reachable from main() calls (and its prototypes), repeated until nothing
// more goes. The program cache's key and the driver's compiler both see only that, so an edit reaches only the
// programs that run the code it touched. Everything else - #version, preprocessor lines, uniforms, globals, structs,
// constants - stays as it was, and a top-level declaration it can't read plainly is kept whole. Before any of that,
// the conditionals the program's own #defines settle are cut to the branch it compiles (resolveConditionals).
#include <cstdlib>
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

// ---- the preprocessor's conditionals, settled where the program's own text settles them. A program's source is
// whole: the build's switches are #defines at its top, and the driver defines nothing else but GL_* and __*__ names.
// So "#ifdef PART_BAKE" in a program without it, or "#if AF_MODEL == 7" in the Kestrel's build, is known here, and
// the branch the driver would skip goes before anything is pruned - with the functions only it called. A condition
// it can't settle (a GL_ or __ name, a function-like macro, an undefined name, a float, anything it can't parse) keeps
// its group as written; the groups inside it are still settled, and a macro its branches define differently is
// unknown after it.
struct Macro { enum Kind { kUndef, kObj, kFunc, kUnknown } kind = kUndef; std::string body; };
using Macros = std::unordered_map<std::string, Macro>;

inline Macro::Kind macroKind(const Macros& m, const std::string& name) {
  auto it = m.find(name);
  if (it != m.end()) return it->second.kind;
  return name.compare(0, 3, "GL_") == 0 || name.compare(0, 2, "__") == 0 ? Macro::kUnknown : Macro::kUndef;   // (the driver's own)
}

// #if's expression: an integer, or not known (ok false)
struct PPExpr {
  const Macros& m;
  std::vector<std::string> t; size_t i = 0; bool ok = true;
  PPExpr(const Macros& macros, const std::string& expr) : m(macros) { lex(expr, 0); }
  void lex(const std::string& e, int depth) {   // tokens, object-like macros expanded in place ("defined X" left as it is)
    if (depth > 32) { ok = false; return; }
    for (size_t k = 0; k < e.size() && ok;) {
      const char c = e[k];
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\\') { k++; continue; }
      if (idStart(c)) {
        size_t a = k; while (k < e.size() && idChar(e[k])) k++;
        const std::string id = e.substr(a, k - a);
        if (id == "defined") {   // its operand is a name, never expanded
          t.push_back(id);
          size_t q = k; while (q < e.size() && (e[q] == ' ' || e[q] == '\t')) q++;
          const bool paren = q < e.size() && e[q] == '(';
          if (paren) { q++; while (q < e.size() && (e[q] == ' ' || e[q] == '\t')) q++; }
          size_t b = q; while (q < e.size() && idChar(e[q])) q++;
          if (q == b) { ok = false; return; }
          t.push_back(e.substr(b, q - b));
          if (paren) { while (q < e.size() && (e[q] == ' ' || e[q] == '\t')) q++; if (q >= e.size() || e[q] != ')') { ok = false; return; } q++; }
          k = q; continue;
        }
        const Macro::Kind kd = macroKind(m, id);
        if (kd == Macro::kObj) { lex(m.at(id).body, depth + 1); continue; }
        ok = false; return;   // (undefined, function-like or the driver's: not settled here)
      }
      if (c >= '0' && c <= '9') { size_t a = k; while (k < e.size() && (idChar(e[k]) || e[k] == '.')) k++; t.push_back(e.substr(a, k - a)); continue; }
      static const char* const ops[] = {"<<", ">>", "<=", ">=", "==", "!=", "&&", "||"};
      bool two = false;
      for (const char* op : ops) if (e.compare(k, 2, op) == 0) { t.push_back(op); k += 2; two = true; break; }
      if (two) continue;
      if (std::string("()+-*/%<>&^|!~").find(c) == std::string::npos) { ok = false; return; }
      t.push_back(std::string(1, c)); k++;
    }
  }
  bool is(const char* op) { if (i < t.size() && t[i] == op) { i++; return true; } return false; }
  long long primary() {
    if (!ok || i >= t.size()) { ok = false; return 0; }
    if (is("(")) { long long v = binary(0); if (!is(")")) ok = false; return v; }
    if (is("!")) return !primary();
    if (is("~")) return ~primary();
    if (is("-")) return -primary();
    if (is("+")) return primary();
    if (is("defined")) {
      if (i >= t.size()) { ok = false; return 0; }
      const Macro::Kind kd = macroKind(m, t[i++]);
      if (kd == Macro::kUnknown) ok = false;
      return kd == Macro::kObj || kd == Macro::kFunc;
    }
    const std::string& n = t[i++];
    if (n.empty() || n[0] < '0' || n[0] > '9') { ok = false; return 0; }
    std::string d = n; while (!d.empty() && (d.back() == 'u' || d.back() == 'U')) d.pop_back();
    char* end = nullptr; long long v = strtoll(d.c_str(), &end, 0);
    if (!end || *end) ok = false;   // (a float, or a suffix the preprocessor doesn't take)
    return v;
  }
  // precedence climbing over C's binary operators
  static int prec(const std::string& op) {
    static const std::pair<const char*, int> P[] = {{"||", 1}, {"&&", 2}, {"|", 3}, {"^", 4}, {"&", 5}, {"==", 6}, {"!=", 6}, {"<", 7}, {">", 7}, {"<=", 7}, {">=", 7},
                                                   {"<<", 8}, {">>", 8}, {"+", 9}, {"-", 9}, {"*", 10}, {"/", 10}, {"%", 10}};
    for (auto& p : P) if (op == p.first) return p.second;
    return 0;
  }
  long long binary(int minPrec) {
    long long a = primary();
    while (ok && i < t.size()) {
      const std::string op = t[i]; const int p = prec(op);
      if (p == 0 || p < minPrec) break;
      i++;
      const long long b = binary(p + 1);
      if (!ok) return 0;
      if ((op == "/" || op == "%") && b == 0) { ok = false; return 0; }
      a = op == "||" ? (a || b) : op == "&&" ? (a && b) : op == "|" ? (a | b) : op == "^" ? (a ^ b) : op == "&" ? (a & b) : op == "==" ? (a == b) : op == "!=" ? (a != b)
        : op == "<" ? (a < b) : op == ">" ? (a > b) : op == "<=" ? (a <= b) : op == ">=" ? (a >= b) : op == "<<" ? (a << (b & 63)) : op == ">>" ? (a >> (b & 63))
        : op == "+" ? (a + b) : op == "-" ? (a - b) : op == "*" ? (a * b) : op == "/" ? (a / b) : (a % b);
    }
    return a;
  }
  int eval() { if (!ok) return -1; const long long v = binary(1); return ok && i == t.size() ? (v != 0) : -1; }   // 1 true, 0 false, -1 not known
};

inline std::string resolveConditionals(const std::string& s) {
  // a directive's name and the rest of its line (continuations joined), or false for any other line
  auto directive = [](const std::string& line, std::string& name, std::string& rest) {
    size_t k = 0; while (k < line.size() && (line[k] == ' ' || line[k] == '\t')) k++;
    if (k >= line.size() || line[k] != '#') return false;
    k++; while (k < line.size() && (line[k] == ' ' || line[k] == '\t')) k++;
    size_t a = k; while (k < line.size() && idChar(line[k])) k++;
    name = line.substr(a, k - a);
    rest.clear();
    for (; k < line.size(); k++) { if (line[k] == '\\' && k + 1 < line.size() && line[k + 1] == '\n') { k++; continue; } if (line[k] != '\n') rest += line[k]; }
    while (!rest.empty() && (rest.back() == ' ' || rest.back() == '\t' || rest.back() == '\r')) rest.pop_back();
    size_t b = 0; while (b < rest.size() && (rest[b] == ' ' || rest[b] == '\t')) b++;
    rest.erase(0, b);
    return true;
  };
  // one open conditional group. Settled while every branch so far was decided (the directives go, and only the live
  // branch's lines stay); kept from its first undecided branch on (written out, each branch read as if taken)
  struct Group {
    bool parentEmit, emit = false, kept = false, anyTrue = false, sawElse = false, live = false;
    Macros start; std::vector<Macros> ends;
  };
  Macros cur;
  std::vector<Group> st;
  std::string out; out.reserve(s.size());
  auto emitting = [&]() { return st.empty() || st.back().emit; };
  auto merge = [&](Group& g) {   // the macros after a kept group: what every way through it agrees on
    std::vector<Macros> outs = g.ends;
    if (!g.sawElse && !g.anyTrue) outs.push_back(g.start);   // (no branch taken)
    std::unordered_set<std::string> names;
    for (auto& o : outs) for (auto& kv : o) names.insert(kv.first);
    Macros r;
    for (const std::string& n : names) {
      const Macro* first = nullptr; bool same = true;
      for (auto& o : outs) {
        auto it = o.find(n); static const Macro undef;
        const Macro& mm = it == o.end() ? undef : it->second;
        if (!first) first = &mm; else if (mm.kind != first->kind || mm.body != first->body) same = false;
      }
      if (same && first) { if (first->kind != Macro::kUndef) r[n] = *first; } else r[n].kind = Macro::kUnknown;
    }
    cur = r;
  };
  for (size_t at = 0; at < s.size();) {
    // the logical line: up to an unescaped newline
    size_t e = at;
    while (e < s.size() && s[e] != '\n') { if (s[e] == '\\' && e + 1 < s.size() && s[e + 1] == '\n') e++; e++; }
    if (e < s.size()) e++;
    const std::string line = s.substr(at, e - at);
    at = e;
    std::string name, rest;
    if (!directive(line, name, rest)) { if (emitting()) out += line; continue; }
    if (name == "if" || name == "ifdef" || name == "ifndef") {
      Group g; g.parentEmit = emitting();
      if (g.parentEmit) {
        int v;
        if (name == "if") v = PPExpr(cur, rest).eval();
        else {
          size_t k = 0; while (k < rest.size() && idChar(rest[k])) k++;
          const Macro::Kind kd = k == 0 || k != rest.size() ? Macro::kUnknown : macroKind(cur, rest);
          v = kd == Macro::kUnknown ? -1 : (kd != Macro::kUndef) == (name == "ifdef");
        }
        if (v < 0) { g.kept = true; g.start = cur; g.emit = g.live = true; out += line; }
        else { g.emit = v == 1; g.anyTrue = v == 1; }
      }
      st.push_back(std::move(g));
      continue;
    }
    if ((name == "elif" || name == "else" || name == "endif") && !st.empty()) {
      Group& g = st.back();
      if (!g.parentEmit) { if (name == "endif") st.pop_back(); continue; }
      if (g.kept && g.live) g.ends.push_back(cur);
      if (g.kept) cur = g.start;
      if (name == "endif") {
        if (g.kept) { merge(g); out += line; }
        st.pop_back();
        continue;
      }
      g.live = false; g.emit = false;
      if (g.anyTrue) continue;   // (a branch before it is taken: the rest are dead)
      if (name == "else") {
        g.emit = g.live = true; g.anyTrue = true; g.sawElse = true;
        if (g.kept) out += line;
        continue;
      }
      const int v = PPExpr(cur, rest).eval();
      if (v == 0) continue;
      if (!g.kept && v == 1) { g.emit = true; g.anyTrue = true; continue; }
      if (!g.kept) {   // the first undecided branch after decided false ones: it opens the group as written
        g.kept = true; g.start = cur;
        size_t h = line.find('#');
        out += line.substr(0, h) + "#if " + rest + "\n";
      } else out += line;
      g.emit = g.live = true; if (v == 1) g.anyTrue = true;
      continue;
    }
    if (!emitting()) continue;
    if (name == "define") {
      size_t k = 0; while (k < rest.size() && idChar(rest[k])) k++;
      Macro mm; mm.kind = k < rest.size() && rest[k] == '(' ? Macro::kFunc : Macro::kObj; mm.body = mm.kind == Macro::kObj ? rest.substr(k) : std::string();
      if (k > 0) cur[rest.substr(0, k)] = mm;
    } else if (name == "undef") {
      size_t k = 0; while (k < rest.size() && idChar(rest[k])) k++;
      if (k > 0) cur[rest.substr(0, k)].kind = Macro::kUndef;
    }
    out += line;
  }
  if (!st.empty()) return s;   // (unbalanced: the driver will say so - left as it was)
  return out;
}

// One top-level function definition or prototype: its text [b, e) and its name
struct Decl { size_t b, e; std::string name; };

inline std::string prune(const std::string& src) {
  const std::string s = resolveConditionals(stripComments(src));
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
