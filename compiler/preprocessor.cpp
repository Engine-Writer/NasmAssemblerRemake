#include <compiler/preprocessor.hpp>
#include <compiler/lexer.hpp>
#include <utilities/stringutils.hpp>
#include <basichumanrights.hpp>

#include <algorithm>
#include <charconv>
#include <format>
#include <string>
#include <unordered_map>
#include <vector>

static int evalPrec(Tok k) {
    switch (k) {
        case Tok::Or:      return 1;
        case Tok::Xor:     return 2;
        case Tok::And:     return 3;
        case Tok::Shl:
        case Tok::Shr:     return 4;
        case Tok::Plus:
        case Tok::Minus:   return 5;
        case Tok::Star:
        case Tok::Slash:
        case Tok::Percent: return 6;
        default:           return -1;
    }
}

static Errorable<int64_t> evalExpr(const TkLine &t, size_t &i, int min);

static Errorable<int64_t> evalAtom(const TkLine &t, size_t &i) {
    if (i >= t.size()) return { .main = 0, .error = 1, .errTxt = "expected a value" };
    const Tk &k = t[i++];

    if (k.kind == Tok::Minus) {
        auto a = evalAtom(t, i);
        if (a.error) return a;
        return { .main = -a.main };
    }

    if (k.kind == Tok::Plus) return evalAtom(t, i);

    if (k.kind == Tok::LParen) {
        auto v = evalExpr(t, i, 1);
        if (v.error) return v;
        if (i >= t.size() || t[i].kind != Tok::RParen)
            return { .main = 0, .error = 1, .errTxt = "missing `)`" };
        ++i;
        return { .main = v.main };
    }

    if (k.kind == Tok::String && k.text.size() == 3)
        return { .main = static_cast<int64_t>(static_cast<unsigned char>(k.text[1])) };

    if (k.kind == Tok::Number) {
        std::string_view s = k.text;
        int base = 10;
        if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
            base = 16;
            s.remove_prefix(2);
        } else if (s.size() > 2 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) {
            base = 2;
            s.remove_prefix(2);
        } else if (s.size() > 1 && (s.back() == 'h' || s.back() == 'H')) {
            base = 16;
            s.remove_suffix(1);
        }

        uint64_t v = 0;
        auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v, base);
        if (ec != std::errc{} || p != s.data() + s.size())
            return { .main = 0, .error = 1, .errTxt = std::format("bad number `{}`", k.text) };
        return { .main = static_cast<int64_t>(v) };
    }

    return {
        .main = 0,
        .error = 1,
        .errTxt = std::format("expected a value, got `{}`", k.text)
    };
}

static Errorable<int64_t> evalExpr(const TkLine &t, size_t &i, int min) {
    auto la = evalAtom(t, i);
    if (la.error) return la;
    int64_t l = la.main;

    while (i < t.size()) {
        Tok k = t[i].kind;
        int p = evalPrec(k);
        if (p < min) break;

        size_t n = 1;
        if (k == Tok::Shl || k == Tok::Shr) {
            if (i + 1 >= t.size() || t[i + 1].kind != k)
                return { .main = 0, .error = 1, .errTxt = "expected `<<` or `>>`" };
            n = 2;
        }
        i += n;

        auto ra = evalExpr(t, i, p + 1);
        if (ra.error) return ra;
        int64_t r = ra.main;

        uint64_t a = static_cast<uint64_t>(l);
        uint64_t b = static_cast<uint64_t>(r);

        switch (k) {
            case Tok::Plus:  l = static_cast<int64_t>(a + b); break;
            case Tok::Minus: l = static_cast<int64_t>(a - b); break;
            case Tok::Star:  l = static_cast<int64_t>(a * b); break;
            case Tok::And:   l = static_cast<int64_t>(a & b); break;
            case Tok::Or:    l = static_cast<int64_t>(a | b); break;
            case Tok::Xor:   l = static_cast<int64_t>(a ^ b); break;
            case Tok::Shl:   l = r < 0 || r >= 64 ? 0 : static_cast<int64_t>(a << r); break;
            case Tok::Shr:   l = r < 0 || r >= 64 ? 0 : static_cast<int64_t>(a >> r); break;
            case Tok::Slash:
            case Tok::Percent:
                if (!r) return { .main = 0, .error = 1, .errTxt = "division by zero" };
                if (l == INT64_MIN && r == -1) l = k == Tok::Slash ? INT64_MIN : 0;
                else l = k == Tok::Slash ? l / r : l % r;
                break;
            default: break;
        }
}

    return { .main = l };
}

static Errorable<std::vector<TkLine>> readLines(std::string_view src) {
    std::vector<TkLine> lines(1);

    for (const Token &t : tokenizeAll(src)) {
        if (t.kind == Tok::Error) return {
            .main{}, .error = 1,
            .errTxt = std::format("line {}: unexpected `{}`", t.line, t.text),
        };
        if (t.kind == Tok::End) break;
        if (t.kind == Tok::Newline) {
            lines.emplace_back();
            continue;
        }
        lines.back().push_back({t.kind, std::string(t.text), t.line, t.spaced});
    }

    return { .main = std::move(lines) };
}

static Errorable<size_t> findClose(const TkLine &toks, size_t open) {
    int depth = 0;
    for (size_t i = open; i < toks.size(); ++i) {
        if (toks[i].kind == Tok::LParen) ++depth;
        else if (toks[i].kind == Tok::RParen && --depth == 0) return { .main = i };
    }

    return {
        .main = 0, .error = 1,
        .errTxt = std::format("line {}: unclosed `(`", toks[open].line)
    };
}

static Args splitArgs(const TkLine &toks, size_t from, size_t to) {
    Args out;
    if (from >= to) return out;

    TkLine cur;
    int depth = 0;
    for (size_t i = from; i < to; ++i) {
        Tok kind = toks[i].kind;
        if (kind == Tok::LParen || kind == Tok::LBracket) ++depth;
        if (kind == Tok::RParen || kind == Tok::RBracket) --depth;

        if (kind == Tok::Comma && depth == 0) {
            out.push_back(std::move(cur));
            cur.clear();
        } else cur.push_back(toks[i]);
    }
    out.push_back(std::move(cur));
    return out;
}

Errorable<std::vector<TkLine>> Preprocessor::collect(std::vector<TkLine> lines) {
    std::vector<TkLine> rest;
    std::string name;
    Macro cur;
    bool inside = false;
    int startLine = 0;

    for (auto &ln : lines) {
        if (ln.empty()) continue;

        std::string dir;
        if (ln[0].kind == Tok::Directive) dir = capitalizeStr(ln[0].text);

        if (inside) {
            if (dir == "%ENDMACRO") {
                m_macros[name] = std::move(cur);
                cur = {};
                inside = false;
            } else cur.body.push_back(std::move(ln));

        } else if (dir == "%MACRO") {
            if (
                ln.size() != 3 ||
                ln[1].kind != Tok::Ident ||
                ln[2].kind != Tok::Number
            ) return {
                .main{}, .error = 1,
                .errTxt = std::format("line {}: usage: %macro name argcount", ln[0].line)
            };

            name = ln[1].text;
            auto [p, ec] = std::from_chars(
                ln[2].text.data(),
                ln[2].text.data() + ln[2].text.size(),
                cur.nargs
            );
            if (ec != std::errc{} || p != ln[2].text.data() + ln[2].text.size()) return {
                .main{}, .error = 1,
                .errTxt = std::format("line {}: bad argcount", ln[0].line)
            };

            startLine = ln[0].line;
            inside = true;

        } else rest.push_back(std::move(ln));
    }

    if (inside) return {
        .main{}, .error = 1,
        .errTxt = std::format("line {}: unterminated %macro `{}`", startLine, name)
    };

    return { .main = std::move(rest) };
}

Errorable<TkLine> Preprocessor::expandDefines(const TkLine &in, std::vector<std::string> hide) {
    TkLine out;

    for (size_t i = 0; i < in.size(); ++i) {
        const Tk &t = in[i];
        auto it = m_defines.find(t.text);

        if (
            t.kind != Tok::Ident || it == m_defines.end() ||
            std::find(hide.begin(), hide.end(), t.text) != hide.end()
        ) {
            out.push_back(t);
            continue;
        }

        const Define &d = it->second;
        auto nested = hide;
        nested.push_back(t.text);

        if (!d.fn) {
            auto ex = expandDefines(d.body, nested);
            if (ex.error) return {
                .main{}, .error = ex.error, .errTxt = std::move(ex.errTxt)
            };
            for (auto &k : ex.main) out.push_back(std::move(k));
            continue;
        }

        if (i + 1 >= in.size() || in[i + 1].kind != Tok::LParen) {
            out.push_back(t);
            continue;
        }

        auto closeR = findClose(in, i + 1);
        if (closeR.error) return {
            .main{}, .error = closeR.error, .errTxt = std::move(closeR.errTxt)
        };
        size_t close = closeR.main;

        Args args = splitArgs(in, i + 2, close);
        if (args.size() != d.params.size()) return {
                .main{}, .error = 1,
                .errTxt = std::format(
                    "line {}: `{}` expects {} args, got {}",
                    t.line, t.text, d.params.size(), args.size()
                )
            };

        TkLine bound;
        for (const Tk &b : d.body) {
            auto p = std::find(d.params.begin(), d.params.end(), b.text);
            if (b.kind == Tok::Ident && p != d.params.end()) {
                const TkLine &arg = args[static_cast<size_t>(p - d.params.begin())];
                bound.insert(bound.end(), arg.begin(), arg.end());
            } else bound.push_back(b);
        }

        auto ex = expandDefines(bound, nested);
        if (ex.error) return {
            .main{}, .error = ex.error, .errTxt = std::move(ex.errTxt)
        };
        for (auto &k : ex.main) out.push_back(std::move(k));
        i = close;
    }

    return { .main = std::move(out) };
}

Errorable<bool> Preprocessor::directive(const TkLine &ln) {
    if (ln[0].kind != Tok::Directive) return { .main = false };

    std::string dir = capitalizeStr(ln[0].text);
    size_t line = ln[0].line;

    if (dir == "%DEFINE") {
        if (ln.size() < 2 || ln[1].kind != Tok::Ident) {
            return {
                .main = false, .error = 1,
                .errTxt = std::format("line {}: usage: %define NAME [body]", line)
            };
        }

        Define def;
        size_t bodyFrom = 2;

        if (ln.size() > 2 && ln[2].kind == Tok::LParen && !ln[2].spaced) {
            def.fn = true;
            size_t i = 3;
            while (i < ln.size() && ln[i].kind != Tok::RParen) {
                if (ln[i].kind != Tok::Ident) return {
                        .main = false, .error = 1,
                        .errTxt = std::format("line {}: bad %define parameter list", line)
                    };

                def.params.push_back(ln[i++].text);
                if (i < ln.size() && ln[i].kind == Tok::Comma) ++i;
            }

            if (i >= ln.size()) return {
                .main = false, .error = 1,
                .errTxt = std::format("line {}: missing `)` in %define", line)
            };

            bodyFrom = i + 1;
        }

        def.body.assign(ln.begin() + bodyFrom, ln.end());
        m_defines[ln[1].text] = std::move(def);
        return { .main = true };
    }

    if (dir == "%UNDEF") {
        if (ln.size() != 2 || ln[1].kind != Tok::Ident) { return {
            .main = false, .error = 1,
            .errTxt = std::format("line {}: usage: %undef NAME", line)
        };
        }
        m_defines.erase(ln[1].text);
        return { .main = true };
    }

    if (dir == "%ASSIGN") {
        if (ln.size() < 3 || ln[1].kind != Tok::Ident) return {
            .main = false, .error = 1,
            .errTxt = std::format("line {}: usage: %assign NAME expression", line)
        };

        auto ex = expandDefines(TkLine(ln.begin() + 2, ln.end()), {});
        if (ex.error) return { .main = false, .error = ex.error, .errTxt = std::move(ex.errTxt) };

        size_t i = 0;
        auto ev = evalExpr(ex.main, i, 1);
        if (ev.error) return {
            .main = false, .error = ev.error, .errTxt = std::move(ev.errTxt)
        };
        if (i != ex.main.size()) return {
            .main = false, .error = 1,
            .errTxt = std::format("unexpected `{}`", ex.main[i].text)
        };

        Define def;
        def.body.push_back({Tok::Number, std::to_string(ev.main), line, false});
        m_defines[ln[1].text] = std::move(def);
        return { .main = true };
    }

    return { .main = false };
}

Errorable<TkLine> Preprocessor::substitute(const TkLine &body, const Args &args, int id, size_t line) {
    TkLine out;

    for (const Tk &t : body) {
        if (t.kind == Tok::MacroArg) {
            if (t.text.size() < 2) return {
                .main{}, .error = 1,
                .errTxt = std::format("line {}: bad macro parameter", line)
            };

            size_t idx = 0;
            auto [p, ec] = std::from_chars(
                t.text.data() + 1,
                t.text.data() + t.text.size(),
                idx
            );
            if (ec != std::errc{} || p != t.text.data() + t.text.size()) return {
                .main{}, .error = 1,
                .errTxt = std::format("line {}: bad macro parameter `{}`", line, t.text)
            };

            if (idx == 0) {
                out.push_back({Tok::Number, std::to_string(args.size()), line, false});
            } else if (idx > args.size()) return {
                .main{}, .error = 1,
                .errTxt = std::format("line {}: macro parameter %{} out of range", line, idx)
            }; else out.insert(out.end(), args[idx - 1].begin(), args[idx - 1].end());

        } else if (t.kind == Tok::MacroLabel) {
            out.push_back({
                Tok::Ident,
                "__" + t.text.substr(2) + "_" + std::to_string(id),
                line,
                false
            });

        } else out.push_back(t);
    }

    return { .main = std::move(out) };
}

Errorable<bool> Preprocessor::process(TkLine ln, int srcLine, int depth) {
    if (ln.empty()) return { .main = true };

    if (depth > kMaxDepth) return {
        .main{}, .error = 1,
        .errTxt = std::format("line {}: macro nesting too deep", srcLine)
    };

    auto d = directive(ln);
    if (d.error) return { .main{}, .error = d.error, .errTxt = std::move(d.errTxt) };
    if (d.main) return { .main = true };

    auto ex = expandDefines(ln, {});
    if (ex.error) return { .main{}, .error = ex.error, .errTxt = std::move(ex.errTxt) };
    ln = std::move(ex.main);

    size_t i = 0;
    if (ln.size() >= 2 && ln[0].kind == Tok::Ident && ln[1].kind == Tok::Colon) i = 2;

    auto it = m_macros.end();
    if (i < ln.size() && ln[i].kind == Tok::Ident) it = m_macros.find(ln[i].text);

    if (it != m_macros.end()) {
        const Macro &m = it->second;
        Args args = splitArgs(ln, i + 1, ln.size());
        if (args.size() != m.nargs) {
            return {
                .main{}, .error = 1,
                .errTxt = std::format(
                    "line {}: macro `{}` expects {} args, got {}",
                    srcLine, ln[i].text, m.nargs, args.size()
                )
            };
        }

        if (i == 2) {
            auto p = process(TkLine(ln.begin(), ln.begin() + 2), srcLine, depth + 1);
            if (p.error) return p;
        }

        int id = ++m_uid;
        for (const TkLine &b : m.body) {
            auto s = substitute(b, args, id, srcLine);
            if (s.error) return { .main{}, .error = s.error, .errTxt = std::move(s.errTxt) };

            auto p = process(std::move(s.main), srcLine, depth + 1);
            if (p.error) return p;
        }

        return { .main = true };
    }

    for (size_t k = 0; k < ln.size(); ++k) {
        if (k) m_out.text += ' ';
        m_out.text += ln[k].text;
    }
    m_out.text += '\n';
    m_out.lineMap.push_back(srcLine);

    return { .main = true };
}

Errorable<Expanded> Preprocessor::run(std::string_view src) {
    auto rl = readLines(src);
    if (rl.error) return { .main{}, .error = rl.error, .errTxt = std::move(rl.errTxt) };

    auto cl = collect(std::move(rl.main));
    if (cl.error) return { .main{}, .error = cl.error, .errTxt = std::move(cl.errTxt) };

    for (auto &ln : cl.main) {
        int srcLine = ln[0].line;
        auto p = process(std::move(ln), srcLine, 0);
        if (p.error) return { .main{}, .error = p.error, .errTxt = std::move(p.errTxt) };
    }

    return { .main = std::move(m_out) };
}
