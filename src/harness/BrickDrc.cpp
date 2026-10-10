#include "harness/BrickDrc.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <climits>
#include <cmath>
#include <optional>

namespace Edraw_Desktop {

namespace {
// A heatshrink's recovered wall where the library does not give it: 0.30 mm (0.012 in).
constexpr double kDefaultShrinkWallMm = 0.30;

// A conductor in square millimetres, from a gauge in AWG or in mm2.
double gaugeAreaMm2(double v, bool awg) {
    if (v <= 0.0) return 0.0;
    if (!awg) return v;
    const double dmm = 0.127 * std::pow(92.0, (36.0 - v) / 39.0);
    return M_PI * dmm * dmm / 4.0;
}
bool gaugeIsAwg(const QString& mode) { return !mode.contains(QLatin1String("mm"), Qt::CaseInsensitive); }
// Every figure in a gauge as written - "20-24", "24 to 20", "0.5" - as conductor areas.
QList<double> gaugeAreas(const QString& text, bool awg) {
    QList<double> out;
    static const QRegularExpression num(QStringLiteral("\\d+(?:\\.\\d+)?"));
    for (auto it = num.globalMatch(text); it.hasNext();)
        if (const double a = gaugeAreaMm2(it.next().captured(0).toDouble(), awg); a > 0.0) out << a;
    return out;
}
struct GaugeRange {
    double lo = 0.0, hi = 0.0;
    QString said;
};
// A range from its figures (one figure is a range of one), a little either side for rounding.
std::optional<GaugeRange> rangeOf(const QList<double>& areas, const QString& said) {
    if (areas.isEmpty()) return std::nullopt;
    const auto [lo, hi] = std::minmax_element(areas.begin(), areas.end());
    return GaugeRange{*lo * 0.97, *hi * 1.03, said};
}
} // namespace

namespace {
// What a brick is called in a finding: its name, or what it is.
QString drcTitle(const Brick& b) {
    if (!b.name.isEmpty()) return b.name;
    return b.type == BrickType::End ? partKindName(b.kind) : brickTypeName(b.type);
}
} // namespace

BrickMark brickCompatibility(const BrickDesign& d, const Brick& b, const PartsLibrary& merged,
                             const std::function<int(const QString&)>& bomItemOf, std::vector<BrickFinding>* all) {
    BrickMark m;
    auto partOf = [&merged](const QString& idOrPn) -> const LibraryPart* {
        if (idOrPn.isEmpty()) return nullptr;
        if (const LibraryPart* p = merged.byId(idOrPn)) return p;
        for (const LibraryPart& p : merged.parts)
            if (p.mfrPn == idOrPn) return &p;
        return nullptr;
    };
    // A size range of a part - "entry" read from entryMin / entryMax; one of them alone is both ends.
    auto rangeMm = [](const LibraryPart& q, const QString& key, double& lo, double& hi) {
        bool okLo = false, okHi = false;
        lo = q.value(key + QStringLiteral("Min")).toDouble(&okLo);
        hi = q.value(key + QStringLiteral("Max")).toDouble(&okHi);
        if (!okLo && !okHi) {
            bool one = false;
            lo = hi = q.value(key).toDouble(&one);
            return one && lo > 0.0;
        }
        if (!okLo) lo = hi;
        if (!okHi) hi = lo;
        if (lo > hi) std::swap(lo, hi);
        return hi > 0.0;
    };
    // THE STACK OF A RUN, from the inside out: the bundle its wires make (D), the cable over them, the braid over
    // that, the heatshrink over all - each layer's part checked to fit over what is inside it, and the size the run
    // comes to at the outside.
    struct Bundle {
        double d = 0.0;      // across the outside, mm; 0 = not known
        int level = -1;      // as a mark's compat
        QString why;
    };
    auto bundleOf = [&](const Brick& s) {
        Bundle out;
        auto say = [&out, all](int level, const char* rule, const QString& why) {
            if (all) all->push_back({QLatin1String(rule), level, why});
            if (level > out.level) {
                out.level = level;
                out.why = why;
            }
        };
        const BrickRunSpec& r = s.run;
        const QString name = s.name.isEmpty() ? drcTitle(s) : s.name;
        const int wires = s.type == BrickType::TwistedPair ? 2 * std::max(1, r.pairs) : std::max(1, r.wires);
        double d = 0.0;
        for (const QString& layer : r.stack) {
            const LibraryPart* lp = partOf(r.stackParts.value(layer));
            const QString lname = lp ? (lp->mfrPn.isEmpty() ? lp->title() : lp->mfrPn) : layer;
            if (layer == QLatin1String("wires")) {
                // THE D-BUNDLE: so many wires of their diameter, packed round.
                const double od = lp ? lp->value(QStringLiteral("od")).toDouble() : 0.0;
                if (od > 0.0) d = od * std::sqrt(wires / 0.7);
                else say(1, "data", QStringLiteral("%1: the wires' diameter is not known").arg(name));
            } else if (layer == QLatin1String("cable")) {
                // A cable is its own outside, whatever is in it.
                const double od = lp ? lp->value(QStringLiteral("od")).toDouble() : 0.0;
                if (od > 0.0) {
                    if (d > 0.0 && od < d * 0.95)
                        say(2, "stack", QStringLiteral("%1: cable %2 (%3 mm) is smaller than its wires (%4 mm)")
                                   .arg(name, lname).arg(od, 0, 'f', 1).arg(d, 0, 'f', 1));
                    d = od;
                } else if (lp) {
                    say(1, "data", QStringLiteral("%1: the diameter of cable %2 is not known").arg(name, lname));
                }
            } else if (layer == QLatin1String("braid")) {
                double lo = 0.0, hi = 0.0;
                if (!lp) {
                    if (d > 0.0) d += 1.0;
                    continue;
                }
                if (!rangeMm(*lp, QStringLiteral("diameter"), lo, hi)) {
                    say(1, "data", QStringLiteral("%1: the size of braid %2 is not known").arg(name, lname));
                } else if (d > 0.0 && (d < lo * 0.95 || d > hi * 1.05)) {
                    say(2, "stack", QStringLiteral("%1: braid %2 is for %3-%4 mm, over %5 mm").arg(name, lname).arg(lo, 0, 'f', 1)
                               .arg(hi, 0, 'f', 1).arg(d, 0, 'f', 1));
                }
                if (d > 0.0) d += 1.0;   // the braid over it, about half a millimetre a side
            } else if (layer == QLatin1String("heatshrink")) {
                double lo = 0.0, hi = 0.0;
                const double wall = lp ? std::max(0.0, lp->value(QStringLiteral("wall")).toDouble()) : 0.0;
                if (!lp) {
                    continue;
                }
                if (!rangeMm(*lp, QStringLiteral("entry"), lo, hi)) {
                    say(1, "data", QStringLiteral("%1: the size of heatshrink %2 is not known").arg(name, lname));
                } else if (d > 0.0 && d > hi * 1.02) {
                    say(2, "stack", QStringLiteral("%1: heatshrink %2 opens to %3 mm - too small for %4 mm").arg(name, lname)
                               .arg(hi, 0, 'f', 1).arg(d, 0, 'f', 1));
                } else if (d > 0.0 && d < lo * 0.98) {
                    say(2, "stack", QStringLiteral("%1: heatshrink %2 shrinks only to %3 mm - loose on %4 mm").arg(name, lname)
                               .arg(lo, 0, 'f', 1).arg(d, 0, 'f', 1));
                }
                // ITS RECOVERED WALL, from the library - or, not given there, 0.30 mm (0.012 in), and said so.
                if (wall <= 0.0)
                    say(1, "data", QStringLiteral("%1: the recovered wall of heatshrink %2 is not in the library - 0.30 mm "
                                          "(0.012 in) assumed for the size").arg(name, lname));
                if (d > 0.0) d += 2.0 * (wall > 0.0 ? wall : kDefaultShrinkWallMm);
            }
        }
        out.d = d;
        return out;
    };
    if (brickIsSpecial(b.type)) {
        // A run: on the bill when every layer given a part number is.
        bool any = false, all = true;
        for (auto it = b.run.stackParts.cbegin(); it != b.run.stackParts.cend(); ++it) {
            const LibraryPart* p = partOf(it.value());
            if (!p) continue;
            any = true;
            all = all && bomItemOf(p->id) > 0;
        }
        if (any) m.bom = all ? 1 : 0;
        // AND WHETHER ITS STACK FITS TOGETHER: each layer over what is inside it. Checked once any layer has a part.
        if (any) {
            const Bundle bu = bundleOf(b);
            m.compat = std::max(0, bu.level);
            m.why = bu.level > 0 ? bu.why
                                 : bu.d > 0.0 ? QStringLiteral("Its layers fit one over another - %1 mm across").arg(bu.d, 0, 'f', 1)
                                              : QStringLiteral("Its layers fit one over another");
        }
        return m;
    }
    if (b.type == BrickType::Joint) {
        // A JOINT: tied to a run, with its splice parts on the bill, the wires that go in said - and the bundle at the
        // joint, splices and all, going into what covers the run there.
        auto say = [&m, all](int level, const char* rule, const QString& why) {
        if (all) all->push_back({QLatin1String(rule), level, why});
            if (level > m.compat) {
                m.compat = level;
                m.why = why;
            }
        };
        const BrickJoint& j = b.joint;
        bool onBill = !j.parts.isEmpty();
        for (const QString& id : j.parts) onBill = onBill && bomItemOf(id) > 0;
        if (!j.parts.isEmpty()) m.bom = onBill ? 1 : 0;
        const Brick* run = d.brick(jointRun(d, b.id));
        if (!run) {
            say(1, "logic", QStringLiteral("Tie it to a run - the cable, fanout or branch the joint is made in"));
            return m;
        }
        if (j.parts.isEmpty())
            say(1, "logic", merged.ofKind(PartKind::Splice).empty()
                       ? QStringLiteral("No splice in the parts library - add one to its Splice group")
                       : QStringLiteral("Pick the splice part it is made with"));
        const int in = parseWireList(j.inWires, run->run.wires).size();
        if (in == 0) say(1, "logic", QStringLiteral("Say which of the run's wires go into it"));
        const LibraryPart* sp = j.parts.isEmpty() ? nullptr : partOf(j.parts.front());
        const double spOd = sp ? sp->value(QStringLiteral("od")).toDouble() : 0.0;
        if (sp && spOd <= 0.0) say(1, "data", QStringLiteral("The splice's outer diameter is not in the library - the joint's size cannot be checked"));
        // THE BUNDLE AT THE JOINT: the wires not joined, packed round, and the splices made there beside them.
        const LibraryPart* wp = partOf(run->run.stackParts.value(QStringLiteral("wires")));
        const double wireOd = wp ? wp->value(QStringLiteral("od")).toDouble() : 0.0;
        if (spOd > 0.0 && wireOd > 0.0) {
            const int rest = std::max(0, run->run.wires - in);
            const int together = j.stepped ? 1 : jointSpliceCount(d, b);
            const double dRest = wireOd * std::sqrt(rest / 0.7);
            const double dJoint = std::sqrt(dRest * dRest + together * spOd * spOd / 0.7);
            double lo = 0.0, hi = 0.0;
            QString cover;
            for (const QString& layer : run->run.stack) {
                const LibraryPart* lp = partOf(run->run.stackParts.value(layer));
                if (!lp) continue;
                if (layer == QLatin1String("heatshrink")) {
                    lo = lp->value(QStringLiteral("entryMin")).toDouble();
                    hi = lp->value(QStringLiteral("entryMax")).toDouble();
                    cover = lp->mfrPn.isEmpty() ? lp->title() : lp->mfrPn;
                }
                if (layer == QLatin1String("cable") && hi <= 0.0) {
                    hi = lp->value(QStringLiteral("od")).toDouble();
                    cover = lp->mfrPn.isEmpty() ? lp->title() : lp->mfrPn;
                }
            }
            if (hi > 0.0 && dJoint > hi * 1.02)
                say(2, "joint", QStringLiteral("At the joint the bundle is %1 mm across, splices and all - more than %2 takes (%3 mm)")
                           .arg(dJoint, 0, 'f', 1).arg(cover).arg(hi, 0, 'f', 1));
            else if (m.compat < 0) {
                m.compat = 0;
                m.why = QStringLiteral("At the joint the bundle is %1 mm across, splices and all").arg(dJoint, 0, 'f', 1);
            }
            (void)lo;
        }
        if (m.compat < 0) {
            m.compat = 0;
            m.why = QStringLiteral("Joint set");
        }
        return m;
    }
    if (b.type != BrickType::End) return m;
    const LibraryPart* p = partOf(b.libPart);
    if (p) m.bom = bomItemOf(p->id) > 0 ? 1 : 0;
    // The worst thing found is what the mark says.
    auto say = [&m, all](int level, const char* rule, const QString& why) {
        if (all) all->push_back({QLatin1String(rule), level, why});
        if (level > m.compat) {
            m.compat = level;
            m.why = why;
        }
    };
    if (b.libPart.isEmpty()) {
        say(1, "logic", QStringLiteral("No part chosen yet"));
        return m;
    }
    if (!p) {
        say(1, "logic", QStringLiteral("Its part is not in the library"));
        return m;
    }
    // WHAT COMES INTO IT: the wires of each run linked to it - how many, and how big.
    struct In {
        int wires = 0;
        double area = 0.0;
        QString from;
        bool solid = false;   // its conductors solid, not stranded
    };
    QList<In> ins;
    int used = 0;
    const int ports = d.portCount(b);
    QSet<int> portsUsed;
    for (const QString& o : d.linksOf(b.id)) {
        portsUsed.insert(d.portOn(b.id, o));
        ++used;
        const Brick* s = d.brick(o);
        if (!s || !brickIsSpecial(s->type)) continue;
        const BrickLink* l = d.link(b.id, o);
        int w = l && l->wires > 0 ? l->wires : 0;
        if (w == 0) {
            int parts = 0;
            for (const QString& e : d.sideOf(o, d.portOn(o, b.id)))
                if (const Brick* eb = d.brick(e); eb && eb->type == BrickType::End) ++parts;
            const int all = s->type == BrickType::TwistedPair ? 2 * std::max(1, s->run.pairs) : std::max(1, s->run.wires);
            w = std::max(1, all / std::max(1, parts));
        }
        In in;
        in.wires = w;
        in.from = s->name.isEmpty() ? drcTitle(*s) : s->name;
        if (const LibraryPart* wp = partOf(s->run.stackParts.value(QStringLiteral("wires")))) {
            const QList<double> a = gaugeAreas(wp->value(QStringLiteral("gauge")), gaugeIsAwg(wp->value(QStringLiteral("gaugeMode"))));
            if (!a.isEmpty()) in.area = a.front();
            in.solid = wp->value(QStringLiteral("conductor")).compare(QLatin1String("Solid"), Qt::CaseInsensitive) == 0;
        }
        // A cable made of solid conductors says so too.
        if (const LibraryPart* cp = partOf(s->run.stackParts.value(QStringLiteral("cable"))))
            in.solid = in.solid || cp->value(QStringLiteral("conductor")).compare(QLatin1String("Solid"), Qt::CaseInsensitive) == 0;
        ins << in;
    }
    if (used == 0) {
        say(1, "logic", QStringLiteral("Not linked to anything yet"));
        return m;
    }
    if (portsUsed.size() < ports) say(1, "logic", QStringLiteral("Not every side of it is linked yet"));
    // WHAT GOES INTO EACH OF ITS ENTRIES: the whole bundle of the run there - the D-bundle, braid, heatshrink over
    // it - must go into that entry of a Y-split (its main side or a leg), a heatshrink, a boot, a backshell, a gland;
    // and into a connector's cable entry where the library gives one.
    {
        const bool sized = p->kind == PartKind::YSplit || p->kind == PartKind::Heatshrink ||
                           p->kind == PartKind::HeatshrinkBoot || p->kind == PartKind::Backshell ||
                           p->kind == PartKind::Grommet || p->kind == PartKind::CableGland ||
                           p->kind == PartKind::Overmould;
        for (const QString& o : d.linksOf(b.id)) {
            const Brick* s = d.brick(o);
            if (!s || !brickIsSpecial(s->type)) continue;
            const int port = d.portOn(b.id, o);
            const QString key = p->kind == PartKind::YSplit ? (port <= 0 ? QStringLiteral("main")
                                                                         : QStringLiteral("leg%1").arg(port))
                                : p->kind == PartKind::Heatshrink ? QStringLiteral("entry")
                                                                  : QStringLiteral("cableEntry");
            const QString where = p->kind == PartKind::YSplit ? (port <= 0 ? QStringLiteral("its main side")
                                                                           : QStringLiteral("leg %1").arg(port))
                                                              : QStringLiteral("its entry");
            const QString sname = s->name.isEmpty() ? drcTitle(*s) : s->name;
            double lo = 0.0, hi = 0.0;
            if (!rangeMm(*p, key, lo, hi)) {
                if (sized) say(1, "data", QStringLiteral("The size of %1 is not in the library").arg(where));
                continue;
            }
            const Bundle bu = bundleOf(*s);
            if (bu.d <= 0.0) {
                say(1, "data", QStringLiteral("How big %1 is across is not known - give its layers part numbers").arg(sname));
                continue;
            }
            if (bu.d > hi * 1.05)
                say(2, "entry", QStringLiteral("%1 (%2 mm across) does not go into %3 (%4-%5 mm)").arg(sname).arg(bu.d, 0, 'f', 1)
                           .arg(where).arg(lo, 0, 'f', 1).arg(hi, 0, 'f', 1));
            else if (bu.d < lo * 0.95)
                say(2, "entry", QStringLiteral("%1 (%2 mm across) is loose in %3 (%4-%5 mm)").arg(sname).arg(bu.d, 0, 'f', 1)
                           .arg(where).arg(lo, 0, 'f', 1).arg(hi, 0, 'f', 1));
        }
    }
    int total = 0;
    for (const In& in : ins) total += in.wires;
    // WHAT IT TAKES: the gauge ranges its wires must fall in.
    QList<GaugeRange> takes;
    bool takesKnown = true;
    auto rangeFromPart = [&](const LibraryPart& q) {
        const bool awg = gaugeIsAwg(q.value(QStringLiteral("gaugeMode")));
        QList<double> a = gaugeAreas(q.value(QStringLiteral("gaugeMin")), awg) +
                          gaugeAreas(q.value(QStringLiteral("gaugeMax")), awg);
        if (a.isEmpty()) a = gaugeAreas(q.value(QStringLiteral("gauge")), awg);
        // A FERRULE BY ITS ENTRY SIZE - "0.5 mm2 (20 AWG)": the first figure, in the unit it is written in.
        if (a.isEmpty() && q.kind == PartKind::Ferrule) {
            const QString size = q.value(QStringLiteral("size"));
            const QList<double> all = gaugeAreas(size, !size.contains(QLatin1String("mm"), Qt::CaseInsensitive));
            if (!all.isEmpty()) a = {all.front()};
        }
        const QString unit = awg ? QStringLiteral(" AWG") : QStringLiteral(" mm²");
        const QString said = (q.mfrPn.isEmpty() ? q.title() : q.mfrPn) + QStringLiteral(" ") +
                             QStringList{q.value(QStringLiteral("gaugeMin")), q.value(QStringLiteral("gaugeMax"))}.join(QLatin1Char('-')) + unit;
        return rangeOf(a, said);
    };
    if (p->kind == PartKind::Connector) {
        const int ways = p->wayCount();
        if (ways > 0 && total > ways)
            say(2, "ways", QStringLiteral("%1 wires into a %2-way connector").arg(total).arg(ways));
        if (partIsWired(*p)) {
            takesKnown = false;   // it comes with its own wires: nothing to size
        } else if (partIncludesPins(*p)) {
            // WITH ITS PINS: the wires must suit the pins it comes with.
            for (const QString& g : partPinGauges(*p)) {
                if (g.isEmpty()) continue;
                const bool awg = gaugeIsAwg(g);
                if (auto r = rangeOf(gaugeAreas(g, awg), g)) takes << *r;
            }
            if (takes.isEmpty()) say(1, "data", QStringLiteral("The size of its pins is not in the library"));
        } else {
            // WITHOUT ITS PINS: its contacts must be on the bill, and the wires must suit them.
            QList<const LibraryPart*> onBill;
            for (const QString& id : tiedContactsOf(merged, p->id))
                if (bomItemOf(id) > 0)
                    if (const LibraryPart* c = merged.byId(id)) onBill << c;
            if (onBill.isEmpty()) {
                say(1, "contacts", QStringLiteral("Its contacts are not on the BOM yet"));
                takesKnown = false;
            }
            for (const LibraryPart* c : onBill)
                if (auto r = rangeFromPart(*c)) takes << *r;
            if (!onBill.isEmpty() && takes.isEmpty()) say(1, "data", QStringLiteral("The size of its contacts is not in the library"));
        }
    } else if (p->kind == PartKind::Ferrule || p->kind == PartKind::Crimp || p->kind == PartKind::ContactPin ||
               p->kind == PartKind::Splice || p->kind == PartKind::Terminal) {
        if (auto r = rangeFromPart(*p)) takes << *r;
        else say(1, "data", QStringLiteral("Its wire size is not in the library"));
    } else {
        takesKnown = false;   // a Y-split, a heatshrink, a boot: sized to the bundle, not to a wire
    }
    // SOLID WIRE IS NEVER CRIMPED: a crimp is made on strands, and a solid conductor in a crimp contact works loose
    // and cracks. A connector whose contacts are crimped (said so, or bought without its pins - its contacts crimp
    // terminals), a ferrule, a crimp terminal, a contact - with a solid wire into it is a fault.
    {
        const QString term = p->value(QStringLiteral("termination"));
        bool crimped = term.compare(QLatin1String("Crimp"), Qt::CaseInsensitive) == 0;
        if (p->kind == PartKind::Connector && term.isEmpty() && !partIncludesPins(*p) && !partIsWired(*p) &&
            !tiedContactsOf(merged, p->id).isEmpty())
            crimped = true;
        if (p->kind == PartKind::Ferrule || p->kind == PartKind::Crimp || p->kind == PartKind::ContactPin) crimped = true;
        if (crimped)
            for (const In& in : ins)
                if (in.solid)
                    say(2, "solid", QStringLiteral("%1 has solid wires, and these are crimp contacts - a crimp is made on "
                                          "stranded wire: use stranded wire, or solder contacts").arg(in.from));
    }
    // THE WIRES AGAINST WHAT IT TAKES.
    if (takesKnown && !takes.isEmpty()) {
        for (const In& in : ins) {
            if (in.area <= 0.0) {
                say(1, "data", QStringLiteral("The wire size of %1 is not known - give its wires a part number").arg(in.from));
                continue;
            }
            bool fits = false;
            for (const GaugeRange& r : takes) fits = fits || (in.area >= r.lo && in.area <= r.hi);
            if (!fits) {
                QStringList said;
                for (const GaugeRange& r : takes) said << r.said;
                say(2, "gauge", QStringLiteral("The wires of %1 (%2 mm²) do not suit %3").arg(in.from).arg(in.area, 0, 'f', 2)
                           .arg(said.join(QStringLiteral(", "))));
            }
        }
    }
    if (m.compat < 0) {
        m.compat = 0;
        m.why = QStringLiteral("Suits what it is linked to");
    }
    return m;
}

const std::vector<BrickDrcRule>& brickDrcRules() {
    // THE RULES BY THEIR TRADE NAMES, each with what it checks - the label in the list, the description when it is
    // hovered and in its explainer.
    auto rule = [](const char* id, const char* label, const char* group, const char* text) {
        // A warning where it is something missing or unfinished; a fail where the harness is wrong.
        static const QSet<QString> warn = {
QStringLiteral("nopart"), QStringLiteral("nodesig"), QStringLiteral("shortrun"),
                                           QStringLiteral("nolayerpn"), QStringLiteral("transpn"), QStringLiteral("fanshrinkpn"),
                                           QStringLiteral("notonbom"), QStringLiteral("compat-contacts"), QStringLiteral("compat-data")};
        const QString key = QString::fromLatin1(id);
        return BrickDrcRule{key, QString::fromUtf8(label), QString::fromLatin1(group), QString::fromUtf8(text),
                            warn.contains(key) ? 1 : 2};
    };
    static const std::vector<BrickDrcRule> rules = {
        rule("drawing-fields","Drawing details consistency","Logical","Every drawing P/N, revision and title field on the model and sheets must match the drawing details. Historical revision entries are not current-revision copies."),
        // LOGICAL DRC: connectivity, identification, definition, procurement.
        rule("drawn-open", "Part disconnected on the drawing", "Logical",
             "A part on the drawing touches none of its runs - it has been moved off its wires on the sheet. Put it back on the run's end, or place the harness again."),
        rule("unlinked", "Unconnected element", "Logical",
             "A part or run on the harness with no connection to anything else. Connect it, or delete it."),
        rule("openside", "Open port", "Logical",
             "A part with a mating face left unconnected - a splice's far side, a Y-split leg. Connect every port."),
        rule("nopart", "Undefined part", "Logical",
             "An end part with no library part assigned. Assign a library part so it has a part number."),
        rule("notinlib", "Part not found in library", "Logical",
             "The part number given is in none of the connected parts libraries. Correct it, or add the part."),
        rule("nodesig", "Missing reference designator", "Logical",
             "An end part with no reference designator (X1, P2, F3...). Give every part a designator."),
        rule("dupdesig", "Duplicate reference designator", "Logical",
             "The same reference designator on two or more parts. Every designator must be unique."),
        rule("openrun", "Unterminated run", "Logical",
             "A cable or twisted run with nothing at one end. A fan-out or bridge may end loose; a cable may not."),
        rule("nolength", "Zero-length run", "Logical", "A run with no length set. Give it its build length."),
        rule("shortrun", "Run shorter than its terminations", "Logical",
             "The fans and transition sleeves at its ends take more than the run's length. Lengthen it, or shorten them."),
        rule("wirecount", "Conductor count exceeded", "Logical",
             "The links take more conductors out of a run than it carries. Check the wires given to each part."),
        rule("nolayerpn", "Stack layer without part number", "Logical",
             "A layer of a run's construction (wires, cable, braid, sleeve) with no part number. Assign one."),
        rule("bridge", "Invalid bridge wire", "Logical",
             "A bridge wire not joined to a cable, or taking a conductor number the cable does not carry."),
        rule("branch", "Orphan branch", "Logical",
             "A branch not joined to a cable body, or with no part at its end."),
        rule("joint", "Incomplete splice", "Logical",
             "A splice not tied to a run, with no splice part, or with no conductors going into it."),
        rule("transpn", "Transition sleeve without part number", "Logical",
             "A transition heatshrink drawn with no part number - it cannot be bought or cut."),
        rule("fanshrinkpn", "Wire-end sleeve without part number", "Logical",
             "Heatshrink over the wire ends with no part number."),
        rule("notonbom", "Part not on BOM", "Logical",
             "A part used on the harness that is not on the bill of materials. Add it to the BOM."),
        // COMPATIBILITY VALIDATION: mechanical fit and electrical suitability.
        rule("compat-stack", "Stack-up interference", "Compatibility",
             "A run's layers do not fit one over another - a sleeve smaller than what it covers, or too big to recover onto it."),
        rule("compat-entry", "Bundle-to-entry mismatch", "Compatibility",
             "A run's bundle does not go into - or is loose in - the entry of the part it ends in (backshell, boot, gland, Y-split)."),
        rule("compat-entrysize", "Entry size out of range", "Compatibility",
             "A part's entry size from its library parameters does not suit what goes into it: a transition sleeve over the "
             "cable and onto the wires, a layer over the sleeve under it, a wire-end sleeve on its wire, a splice on its wires."),
        rule("compat-gauge", "Conductor gauge mismatch", "Compatibility",
             "Wires outside the gauge range of the contact, ferrule, terminal or splice they go into."),
        rule("compat-ways", "Cavity count exceeded", "Compatibility",
             "More conductors into a connector than it has cavities."),
        rule("compat-solid", "Solid conductor in crimp contact", "Compatibility",
             "A solid-core wire into a crimp contact - a crimp is made on stranded wire. Use stranded wire, or solder contacts."),
        rule("compat-contacts", "Contacts not on BOM", "Compatibility",
             "A connector bought without its contacts, and none of its contacts on the bill of materials."),
        rule("compat-joint", "Splice bundle oversize", "Compatibility",
             "At a splice the bundle, splices and all, is bigger than the sleeve or cable that covers the run there."),
        rule("compat-data", "Missing library data", "Compatibility",
             "A figure a fit check needs is not in the parts library (a diameter, an entry size, a wall). The check cannot be made."),
    };
    return rules;
}

std::vector<BrickIssue> brickDrc(const BrickDesign& d, const QString& harness, const PartsLibrary& merged,
                                 const std::function<int(const QString&)>& bomItemOf) {
    std::vector<BrickIssue> out;
    QSet<QString> seen;
    // One finding: its rule, how bad, about which brick - said once however often it is found.
    auto add = [&](const QString& rule, int level, const Brick* b, const QString& said, bool named = true) {
        const QString message = b && named ? drcTitle(*b) + QStringLiteral(": ") + said : said;
        const QString key = harness + QLatin1Char('|') + rule + QLatin1Char('|') + (b ? b->id : QString()) +
                            QLatin1Char('|') + message;
        if (seen.contains(key)) return;
        seen.insert(key);
        out.push_back({key, rule, harness, b ? b->id : QString(), message, level});
    };
    auto partOf = [&merged](const QString& idOrPn) -> const LibraryPart* {
        if (idOrPn.isEmpty()) return nullptr;
        if (const LibraryPart* p = merged.byId(idOrPn)) return p;
        for (const LibraryPart& p : merged.parts)
            if (p.mfrPn == idOrPn) return &p;
        return nullptr;
    };
    auto pnOf = [](const LibraryPart& p) { return p.mfrPn.isEmpty() ? p.title() : p.mfrPn; };
    // A part number used somewhere: in the library, and on the bill.
    auto usePart = [&](const Brick& b, const QString& id, const QString& what) {
        if (id.isEmpty()) return;
        const LibraryPart* p = partOf(id);
        if (!p) {
            add(QStringLiteral("notinlib"), 2, &b, QStringLiteral("%1 %2 is not in the parts library").arg(what, id));
            return;
        }
        if (bomItemOf && bomItemOf(p->id) <= 0)
            add(QStringLiteral("notonbom"), 1, &b, QStringLiteral("%1 %2 is not on the BOM").arg(what, pnOf(*p)));
    };

    // A size range from a part's parameters - "entry" from entryMin / entryMax, one of them alone both ends.
    auto rangeOf = [](const LibraryPart& q, const QString& key, double& lo, double& hi) {
        bool okLo = false, okHi = false;
        lo = q.value(key + QStringLiteral("Min")).toDouble(&okLo);
        hi = q.value(key + QStringLiteral("Max")).toDouble(&okHi);
        if (!okLo && !okHi) {
            bool one = false;
            lo = hi = q.value(key).toDouble(&one);
            return one && lo > 0.0;
        }
        if (!okLo) lo = hi;
        if (!okHi) hi = lo;
        if (lo > hi) std::swap(lo, hi);
        return hi > 0.0;
    };
    // A sleeve's entry: a heatshrink's "entry", a boot's cable side.
    auto sleeveRange = [&](const LibraryPart& q, double& lo, double& hi) {
        return rangeOf(q, QStringLiteral("entry"), lo, hi) || rangeOf(q, QStringLiteral("cableEntry"), lo, hi);
    };
    auto wallOf = [](const LibraryPart& q) {
        const double w = q.value(QStringLiteral("wall")).toDouble();
        return w > 0.0 ? w : 0.30;
    };
    // A RUN'S SIZES: one of its wires across, the wires packed round, and the whole of it at the outside.
    struct RunSize {
        double wire = 0.0, wires = 0.0, outside = 0.0;
    };
    auto sizeOf = [&](const Brick& s) {
        RunSize z;
        const BrickRunSpec& r = s.run;
        const int n = s.type == BrickType::TwistedPair ? 2 * std::max(1, r.pairs) : std::max(1, r.wires);
        for (const QString& layer : r.stack) {
            const LibraryPart* lp = partOf(r.stackParts.value(layer));
            if (!lp) continue;
            const double od = lp->value(QStringLiteral("od")).toDouble();
            if (layer == QLatin1String("wires") && od > 0.0) {
                z.wire = od;
                z.wires = z.outside = n > 1 ? od * std::sqrt(n / 0.7) : od;
            } else if (layer == QLatin1String("cable") && od > 0.0) {
                z.outside = od;
            } else if (layer == QLatin1String("braid") && z.outside > 0.0) {
                z.outside += 1.0;
            } else if (layer == QLatin1String("heatshrink") && z.outside > 0.0) {
                z.outside += 2.0 * wallOf(*lp);
            }
        }
        return z;
    };
    auto mm = [](double v) { return QString::number(v, 'f', 1); };
    auto checkEntrySizes = [&](const Brick& b) {
        const QString rule = QStringLiteral("compat-entrysize");
        if (brickIsSpecial(b.type)) {
            const BrickRunSpec& r = b.run;
            const RunSize z = sizeOf(b);
            // THE TRANSITION SLEEVE: open wide enough to go over the cable body, and recovering small enough to grip
            // the wires coming out of it - at each end, its own part where the two ends are their own.
            if (r.transitions != RunEnds::None && r.outermost() != QLatin1String("wires")) {
                const bool tStart = r.transitions == RunEnds::Start || r.transitions == RunEnds::Both;
                const bool tFinish = r.transitions == RunEnds::Finish || r.transitions == RunEnds::Both;
                QStringList sleeves;
                if (tStart || r.transitionSame) sleeves << r.transitionPart;
                if (tFinish && !r.transitionSame) sleeves << r.transitionPartFinish;
                for (const QString& id : sleeves) {
                    const LibraryPart* sp = partOf(id);
                    if (!sp) continue;
                    double lo = 0.0, hi = 0.0;
                    if (!sleeveRange(*sp, lo, hi)) {
                        add(QStringLiteral("compat-data"), 1, &b,
                            QStringLiteral("the entry size of transition %1 is not in the library").arg(pnOf(*sp)));
                        continue;
                    }
                    if (z.outside > 0.0 && hi < z.outside * 0.98)
                        add(rule, 2, &b,
                            QStringLiteral("transition %1 opens to %2 mm - the cable body is %3 mm").arg(pnOf(*sp), mm(hi), mm(z.outside)));
                    if (z.wires > 0.0 && lo > z.wires * 1.02)
                        add(rule, 2, &b,
                            QStringLiteral("transition %1 recovers only to %2 mm - loose on the %3 mm wire bundle")
                                .arg(pnOf(*sp), mm(lo), mm(z.wires)));
                    // EACH LAYER OVER IT: open wide enough to go over the one under it, recovered.
                    double under = z.outside > 0.0 ? z.outside + 2.0 * wallOf(*sp) : 0.0;
                    const QStringList& layers = id == r.transitionPartFinish && !r.transitionSame ? r.transitionLayersFinish
                                                                                                 : r.transitionLayers;
                    for (const QString& lid : layers) {
                        const LibraryPart* lp = partOf(lid);
                        if (!lp) continue;
                        double llo = 0.0, lhi = 0.0;
                        if (!sleeveRange(*lp, llo, lhi)) {
                            add(QStringLiteral("compat-data"), 1, &b,
                                QStringLiteral("the entry size of layer %1 is not in the library").arg(pnOf(*lp)));
                            continue;
                        }
                        if (under > 0.0 && lhi < under * 0.98)
                            add(rule, 2, &b,
                                QStringLiteral("layer %1 opens to %2 mm - the sleeve under it is %3 mm").arg(pnOf(*lp), mm(lhi), mm(under)));
                        if (under > 0.0) under += 2.0 * wallOf(*lp);
                    }
                }
            }
            // A SLEEVE OVER EACH WIRE END: over one wire, and onto it.
            if (r.fanEndShrink)
                if (const LibraryPart* fp = partOf(r.fanEndShrinkPart)) {
                    double lo = 0.0, hi = 0.0;
                    if (!sleeveRange(*fp, lo, hi))
                        add(QStringLiteral("compat-data"), 1, &b,
                            QStringLiteral("the entry size of wire-end sleeve %1 is not in the library").arg(pnOf(*fp)));
                    else if (z.wire > 0.0 && hi < z.wire * 0.98)
                        add(rule, 2, &b, QStringLiteral("wire-end sleeve %1 opens to %2 mm - the wire is %3 mm").arg(pnOf(*fp), mm(hi), mm(z.wire)));
                    else if (z.wire > 0.0 && lo > z.wire * 1.02)
                        add(rule, 2, &b,
                            QStringLiteral("wire-end sleeve %1 recovers only to %2 mm - loose on the %3 mm wire").arg(pnOf(*fp), mm(lo), mm(z.wire)));
                }
            return;
        }
        if (b.type != BrickType::End) return;
        const LibraryPart* p = partOf(b.libPart);
        if (!p) return;
        for (const QString& o : d.linksOf(b.id)) {
            const Brick* s = d.brick(o);
            if (!s || !brickIsSpecial(s->type)) continue;
            const RunSize z = sizeOf(*s);
            const QString from = drcTitle(*s);
            // A SPLICE: the wires that go into it against its insertion size.
            if (p->kind == PartKind::Splice) {
                double lo = 0.0, hi = 0.0;
                if (!rangeOf(*p, QStringLiteral("insertion"), lo, hi)) continue;
                const BrickLink* l = d.link(b.id, o);
                const int n = l && l->wires > 0 ? l->wires : std::max(1, s->run.wires);
                const double bundle = z.wire > 0.0 ? (n > 1 ? z.wire * std::sqrt(n / 0.7) : z.wire) : 0.0;
                if (bundle > 0.0 && bundle > hi * 1.02)
                    add(rule, 2, &b,
                        QStringLiteral("the %1 wire(s) of %2 (%3 mm) do not go into its insertion size (%4-%5 mm)")
                            .arg(n).arg(from, mm(bundle), mm(lo), mm(hi)), true);
                continue;
            }
            // A CONNECTOR A COVERED RUN GOES INTO: its cable entry known, to check the run against.
            if (p->kind == PartKind::Connector && s->run.outermost() != QLatin1String("wires")) {
                double lo = 0.0, hi = 0.0;
                if (!rangeOf(*p, QStringLiteral("cableEntry"), lo, hi))
                    add(QStringLiteral("compat-data"), 1, &b,
                        QStringLiteral("the cable entry of %1 is not in the library - %2 cannot be checked into it").arg(pnOf(*p), from));
            }
        }
    };

    // DESIGNATORS: every end part has one, and no two the same.
    QHash<QString, QList<const Brick*>> byName;
    for (const Brick& b : d.bricks) {
        if (b.type != BrickType::End) continue;
        const QString name = b.name.trimmed();
        if (name.isEmpty()) add(QStringLiteral("nodesig"), 1, &b, QStringLiteral("has no designator"));
        else byName[name.toUpper()] << &b;
    }
    for (auto it = byName.cbegin(); it != byName.cend(); ++it)
        if (it.value().size() > 1)
            for (const Brick* b : it.value())
                add(QStringLiteral("dupdesig"), 2, b,
                    QStringLiteral("designator %1 is used by %2 parts").arg(b->name.trimmed()).arg(it.value().size()));

    for (const Brick& b : d.bricks) {
        const QStringList links = d.linksOf(b.id);
        if (links.isEmpty()) add(QStringLiteral("unlinked"), 1, &b, QStringLiteral("is linked to nothing"));
        if (b.type == BrickType::End) {
            if (b.libPart.isEmpty()) add(QStringLiteral("nopart"), 1, &b, QStringLiteral("has no library part chosen"));
            else usePart(b, b.libPart, QStringLiteral("part"));
            QSet<int> used;
            for (const QString& o : links) used.insert(d.portOn(b.id, o));
            if (!links.isEmpty() && used.size() < d.portCount(b))
                add(QStringLiteral("openside"), 1, &b, QStringLiteral("not every side of it is linked"));
        } else if (brickIsSpecial(b.type)) {
            const BrickRunSpec& r = b.run;
            const bool bridge = brickIsBridge(b), branch = brickIsBranch(b);
            const QString cable = joinedSpecial(d, b.id);
            if (branch) {
                if (cable.isEmpty()) add(QStringLiteral("branch"), 2, &b, QStringLiteral("is not joined to a cable"));
                else {
                    bool part = false;
                    for (const QString& o : d.sideOf(b.id, 1 - d.portOn(b.id, cable)))
                        if (const Brick* ob = d.brick(o); ob && ob->type == BrickType::End) part = true;
                    if (!part) add(QStringLiteral("branch"), 1, &b, QStringLiteral("has no part at its end"));
                }
            }
            if (bridge) {
                const Brick* from = d.brick(cable);
                if (!from) {
                    add(QStringLiteral("bridge"), 2, &b, QStringLiteral("is not joined to a cable or fanout to take its wires from"));
                } else if (r.bridgeWires.trimmed() != QLatin1String(kBridgeShield)) {
                    const int has = from->type == BrickType::TwistedPair ? 2 * std::max(1, from->run.pairs)
                                                                        : std::max(1, from->run.wires);
                    for (int w : parseWireList(r.bridgeWires, 999))
                        if (w > has)
                            add(QStringLiteral("bridge"), 2, &b,
                                QStringLiteral("takes wire %1 - %2 has %3").arg(w).arg(drcTitle(*from)).arg(has));
                }
            }
            // AN END WITH NOTHING THERE: a cable or a twisted run ends on something; a fanout and a bridge may end
            // loose, ready to land; a branch is checked above.
            if (!bridge && !branch && b.type != BrickType::Fanout && !links.isEmpty()) {
                const bool atStart = !d.sideOf(b.id, kPortLeft).isEmpty();
                const bool atFinish = !d.sideOf(b.id, kPortRight).isEmpty();
                if (!atStart || !atFinish)
                    add(QStringLiteral("openrun"), 1, &b,
                        QStringLiteral("has nothing at its %1").arg(!atStart ? QStringLiteral("start") : QStringLiteral("finish")));
            }
            if (!branch && r.lengthMm <= 1e-9) add(QStringLiteral("nolength"), 2, &b, QStringLiteral("has no length"));
            // ITS FANS AND TRANSITIONS TAKE MORE THAN IT IS LONG.
            {
                double take = std::max(0.0, r.fanStartMm) + std::max(0.0, r.fanFinishMm);
                const bool tStart = r.transitions == RunEnds::Start || r.transitions == RunEnds::Both;
                const bool tFinish = r.transitions == RunEnds::Finish || r.transitions == RunEnds::Both;
                if (tStart) take += r.transitionLenStartMm;
                if (tFinish) take += r.transitionLenFinishMm;
                if (r.lengthMm > 1e-9 && take > r.lengthMm + 1e-6)
                    add(QStringLiteral("shortrun"), 1, &b,
                        QStringLiteral("is %1 mm long - its fans and transitions take %2 mm")
                            .arg(r.lengthMm, 0, 'f', 0)
                            .arg(take, 0, 'f', 0));
            }
            // MORE WIRES GIVEN THAN IT HAS.
            {
                const int has = b.type == BrickType::TwistedPair ? 2 * std::max(1, r.pairs) : std::max(1, r.wires);
                int given = 0;
                for (const QString& o : links)
                    if (const BrickLink* l = d.link(b.id, o); l && l->wires > 0) given += l->wires;
                if (given > has)
                    add(QStringLiteral("wirecount"), 2, &b, QStringLiteral("gives its parts %1 wires and has %2").arg(given).arg(has));
            }
            // ITS LAYERS' PART NUMBERS: given, in the library, on the bill.
            for (const QString& layer : r.stack) {
                const QString id = r.stackParts.value(layer);
                if (id.isEmpty()) add(QStringLiteral("nolayerpn"), 1, &b, QStringLiteral("its %1 layer has no part number").arg(layer));
                else usePart(b, id, layer);
            }
            // ITS TRANSITIONS AND THE HEATSHRINK OVER ITS WIRE ENDS: a part number for each.
            if (r.transitions != RunEnds::None && r.outermost() != QLatin1String("wires")) {
                const bool apart = !r.transitionSame;
                const bool tStart = r.transitions == RunEnds::Start || r.transitions == RunEnds::Both;
                const bool tFinish = r.transitions == RunEnds::Finish || r.transitions == RunEnds::Both;
                if (r.transitionPart.isEmpty() && (!apart || tStart))
                    add(QStringLiteral("transpn"), 1, &b,
                        apart ? QStringLiteral("its start transition heatshrink has no part number")
                              : QStringLiteral("its transition heatshrink has no part number"));
                else usePart(b, r.transitionPart, QStringLiteral("transition"));
                if (apart && tFinish) {
                    if (r.transitionPartFinish.isEmpty())
                        add(QStringLiteral("transpn"), 1, &b, QStringLiteral("its finish transition heatshrink has no part number"));
                    else usePart(b, r.transitionPartFinish, QStringLiteral("transition"));
                }
                for (const QString& id : r.transitionLayers) usePart(b, id, QStringLiteral("layer over the transition"));
                if (apart)
                    for (const QString& id : r.transitionLayersFinish) usePart(b, id, QStringLiteral("layer over the transition"));
            }
            if (r.fanEndShrink) {
                if (r.fanEndShrinkPart.isEmpty())
                    add(QStringLiteral("fanshrinkpn"), 1, &b, QStringLiteral("its heatshrink over the wire ends has no part number"));
                else usePart(b, r.fanEndShrinkPart, QStringLiteral("wire-end heatshrink"));
            }
        } else if (b.type == BrickType::Joint) {
            const Brick* run = d.brick(jointRun(d, b.id));
            if (!run) add(QStringLiteral("joint"), 2, &b, QStringLiteral("is not tied to a run"));
            if (b.joint.parts.isEmpty()) add(QStringLiteral("joint"), 1, &b, QStringLiteral("has no splice part"));
            for (const QString& id : b.joint.parts) usePart(b, id, QStringLiteral("splice"));
            if (run && parseWireList(b.joint.inWires, run->run.wires).isEmpty())
                add(QStringLiteral("joint"), 1, &b, QStringLiteral("has no wires going into it"));
        }
        // ENTRY SIZES FROM THE PARTS' OWN PARAMETERS against what goes into them.
        checkEntrySizes(b);
        // COMPATIBILITY: every finding the fit checks make, each under its rule - what the logical rules above already
        // say (not linked, no part) left to them.
        std::vector<BrickFinding> found;
        brickCompatibility(d, b, merged, bomItemOf, &found);
        for (const BrickFinding& f : found) {
            if (f.rule == QLatin1String("logic")) continue;
            add(QStringLiteral("compat-") + f.rule, f.level, &b, f.why, !f.why.startsWith(drcTitle(b)));
        }
    }
    return out;
}

} // namespace Edraw_Desktop

namespace Edraw_Desktop {

std::vector<BrickIssue> drawingConnectivity(const std::vector<Entity>& ents) {
    std::vector<BrickIssue> out;
    // THE PLACED HARNESS'S PARTS AND RUNS: each part the objects of one copy mark; a run a twisted-pair object.
    struct Part {
        Bounds box;
        QString lib, harness, brick, name;
        bool run = false;
    };
    QHash<QString, Part> parts;
    std::vector<Vec2> ends;
    for (const Entity& e : ents) {
        const QString copy = e.meta.value(QStringLiteral("partCopy")).toString();
        if (copy.isEmpty()) continue;
        // A run: a bundle, or one wire drawn as a double line.
        if (e.type == EntityType::TwistedPair ||
            (e.type == EntityType::Polyline && e.meta.value(QStringLiteral("double")).toBool())) {
            if (e.points.size() >= 2) {
                ends.push_back(e.points.front());
                ends.push_back(e.points.back());
            } else {
                // Drawn by its two ends alone.
                ends.push_back(e.a);
                ends.push_back(e.b);
            }
            parts[copy].run = true;
            continue;
        }
        Part& p = parts[copy];
        const QString role = e.meta.value(QStringLiteral("mechRole")).toString();
        if (role == QLatin1String("designator")) {
            p.name = e.meta.value(QStringLiteral("text")).toString();
        }
        // The part's own body - not its designator, its balloon, a dimension.
        if (e.type == EntityType::Text || e.type == EntityType::Dimension || role == QLatin1String("designator") ||
            role == QLatin1String("balloon") || role == QLatin1String("dim"))
            continue;
        p.box.expand(e.bounds());
        if (p.lib.isEmpty()) p.lib = e.meta.value(QStringLiteral("libPart")).toString();
        if (p.harness.isEmpty()) p.harness = e.meta.value(QStringLiteral("harness")).toString();
        if (p.brick.isEmpty()) p.brick = e.meta.value(QStringLiteral("harnessBrick")).toString();
        if (p.name.isEmpty()) p.name = e.meta.value(QStringLiteral("partName")).toString();
    }
    auto touches = [](const Bounds& b, const Vec2& v, double tol) {
        return b.valid && v.x >= b.min.x - tol && v.x <= b.max.x + tol && v.y >= b.min.y - tol && v.y <= b.max.y + tol;
    };
    auto overlap = [](const Bounds& a, const Bounds& b, double tol) {
        return a.valid && b.valid && a.min.x <= b.max.x + tol && b.min.x <= a.max.x + tol && a.min.y <= b.max.y + tol &&
               b.min.y <= a.max.y + tol;
    };
    constexpr double kTol = 1.0;
    // ON A RUN: a part a run's end touches - and, through it, a part fitted face to face on that one (a boot on a
    // connector), and so on.
    QSet<QString> onRun;
    QStringList ids;
    for (auto it = parts.cbegin(); it != parts.cend(); ++it)
        if (!it.value().run && it.value().box.valid) ids << it.key();
    if (ends.empty()) return out;   // no run placed: nothing to be disconnected from
    for (const QString& id : ids)
        for (const Vec2& v : ends)
            if (touches(parts.value(id).box, v, kTol)) {
                onRun.insert(id);
                break;
            }
    for (bool grew = true; grew;) {
        grew = false;
        for (const QString& id : ids) {
            if (onRun.contains(id)) continue;
            for (const QString& other : onRun)
                if (overlap(parts.value(id).box, parts.value(other).box, kTol)) {
                    onRun.insert(id);
                    grew = true;
                    break;
                }
        }
    }
    for (const QString& id : ids) {
        if (onRun.contains(id)) continue;
        const Part& p = parts.value(id);
        const QString what = !p.name.isEmpty() ? p.name : !p.lib.isEmpty() ? p.lib : QStringLiteral("A part");
        BrickIssue i;
        i.rule = QStringLiteral("drawn-open");
        i.harness = p.harness.isEmpty() ? QStringLiteral("(drawing)") : p.harness;
        i.brickId = p.brick;
        i.message = QStringLiteral("%1 touches none of its runs on the drawing - it has been moved off its wires").arg(what);
        i.key = i.harness + QStringLiteral("|drawn-open|") + id;
        i.level = 2;
        i.located = true;
        i.atX = (p.box.min.x + p.box.max.x) / 2.0;
        i.atY = (p.box.min.y + p.box.max.y) / 2.0;
        out.push_back(i);
    }
    return out;
}

} // namespace Edraw_Desktop
