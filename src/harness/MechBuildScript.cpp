#include "harness/MechBuildScript.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace Edraw_Desktop {

namespace {

// PartKind survives a save as its own name rather than as a number: the enum has things inserted into
// the middle of it (it grew "beyond the list asked for" once already), and a file written before an
// insertion would come back meaning a different kind of part.
QString kindKey(PartKind kind) { return partKindName(kind); }

PartKind kindFromKey(const QString& key) {
    for (int i = 0; i <= static_cast<int>(PartKind::Other); ++i) {
        const auto k = static_cast<PartKind>(i);
        if (partKindName(k).compare(key, Qt::CaseInsensitive) == 0) return k;
    }
    return PartKind::Other;
}

double num(const QJsonObject& o, const char* key, double dflt = 0.0) {
    return o.contains(QLatin1String(key)) ? o.value(QLatin1String(key)).toDouble(dflt) : dflt;
}

} // namespace

// ==================================================================================================
// Roles
// ==================================================================================================

MechStepRole mechStepRole(PartKind kind) {
    switch (kind) {
    case PartKind::Wire:
    case PartKind::Cable:
        return MechStepRole::Run;

    case PartKind::Connector:
    case PartKind::Crimp:            // a ring or fork terminal IS the end of the run
    case PartKind::Ferrule:
        return MechStepRole::End;

    case PartKind::YSplit:
    case PartKind::Splice:           // conductors part company here, which is a node
        return MechStepRole::Node;

    // Things that sit AT a point: a body stacked back from the node, or a mark on it.
    case PartKind::Backshell:
    case PartKind::HeatshrinkBoot:
    case PartKind::Grommet:
    case PartKind::CableGland:
    case PartKind::Marker:
        return MechStepRole::AtNode;

    // Things pulled ALONG the run that arrives.
    case PartKind::Heatshrink:
    case PartKind::ShieldBraid:
    case PartKind::Sleeve:
    case PartKind::Conduit:
    case PartKind::CableTie:
    case PartKind::Tape:
        return MechStepRole::AlongRun;

    default:
        // An unknown kind is treated as a mark at a point. It is the harmless choice: it lands
        // somewhere real, is drawn as whatever its default model is, and does not invent a run.
        return MechStepRole::AtNode;
    }
}

// ==================================================================================================
// Script accessors and JSON
// ==================================================================================================

const MechStep* MechBuildScript::step(const QString& id) const {
    for (const MechStep& s : steps)
        if (s.id == id) return &s;
    return nullptr;
}

std::vector<const MechStep*> MechBuildScript::childSteps(const QString& stepId) const {
    std::vector<const MechStep*> out;
    for (const MechStep& s : steps)
        if (s.parentStep == stepId) out.push_back(&s);
    return out;
}

const MechStep* MechBuildScript::firstStep() const {
    for (const MechStep& s : steps)
        if (s.parentStep.isEmpty()) return &s;
    return nullptr;
}

QString MechBuildScript::nextStepId() const {
    int n = 1;
    for (;; ++n) {
        const QString id = QStringLiteral("S%1").arg(n);
        if (!step(id)) return id;
    }
}

QJsonObject MechConductorGroup::toJson() const {
    QJsonObject o;
    o["count"] = count;
    if (!gauge.isEmpty()) o["gauge"] = gauge;
    if (!libPart.isEmpty()) o["libPart"] = libPart;
    if (!name.isEmpty()) o["name"] = name;
    if (!designators.isEmpty()) o["designators"] = QJsonArray::fromStringList(designators);
    return o;
}

MechConductorGroup MechConductorGroup::fromJson(const QJsonObject& obj) {
    MechConductorGroup g;
    g.count = obj.value("count").toInt(0);
    g.gauge = obj.value("gauge").toString();
    g.libPart = obj.value("libPart").toString();
    g.name = obj.value("name").toString();
    for (const QJsonValue& v : obj.value("designators").toArray())
        g.designators << v.toString();
    return g;
}

QJsonObject MechStep::toJson() const {
    QJsonObject o;
    o["id"] = id;
    o["kind"] = kindKey(kind);
    if (!parentStep.isEmpty()) o["parent"] = parentStep;
    if (!libPart.isEmpty()) o["libPart"] = libPart;
    if (!name.isEmpty()) o["name"] = name;
    if (turnDeg != 0.0) o["turnDeg"] = turnDeg;
    if (lengthMm != 0.0) o["lengthMm"] = lengthMm;
    if (wireCount != 0) o["wireCount"] = wireCount;
    if (!gauge.isEmpty()) o["gauge"] = gauge;
    if (odMm != 0.0) o["odMm"] = odMm;
    if (idMm != 0.0) o["idMm"] = idMm;
    if (pinCount != 0) o["pinCount"] = pinCount;
    if (!contactPart.isEmpty()) o["contactPart"] = contactPart;
    if (contactQty != 0) o["contactQty"] = contactQty;
    if (dimension) o["dimension"] = true;
    if (!dimText.isEmpty()) o["dimText"] = dimText;
    if (continuesConductors) o["continues"] = true;
    if (!balloon) o["balloon"] = false;      // on by default, so only the exception is written
    if (lengthBreak) o["lengthBreak"] = true;
    if (showName) o["showName"] = true;
    if (!conductors.empty()) {
        QJsonArray gs;
        for (const MechConductorGroup& g : conductors) gs.append(g.toJson());
        o["conductors"] = gs;
    }
    if (isSubpart) o["subpart"] = true;
    if (!subParts.empty()) {
        QJsonArray subs;
        for (const MechSubPart& sp : subParts) {
            QJsonObject s;
            if (!sp.libPart.isEmpty()) s["libPart"] = sp.libPart;
            s["kind"] = kindKey(sp.kind);
            if (!sp.name.isEmpty()) s["name"] = sp.name;
            s["qty"] = sp.qty;
            if (!sp.unit.isEmpty()) s["unit"] = sp.unit;
            s["bomOnly"] = sp.bomOnly;
            subs.append(s);
        }
        o["subParts"] = subs;
    }
    return o;
}

MechStep MechStep::fromJson(const QJsonObject& obj) {
    MechStep s;
    s.id = obj.value("id").toString();
    s.kind = kindFromKey(obj.value("kind").toString());
    s.parentStep = obj.value("parent").toString();
    s.libPart = obj.value("libPart").toString();
    s.name = obj.value("name").toString();
    s.turnDeg = num(obj, "turnDeg");
    s.lengthMm = num(obj, "lengthMm");
    s.wireCount = obj.value("wireCount").toInt(0);
    s.gauge = obj.value("gauge").toString();
    s.odMm = num(obj, "odMm");
    s.idMm = num(obj, "idMm");
    s.pinCount = obj.value("pinCount").toInt(0);
    s.contactPart = obj.value("contactPart").toString();
    s.contactQty = obj.value("contactQty").toInt(0);
    s.dimension = obj.value("dimension").toBool(false);
    s.dimText = obj.value("dimText").toString();
    s.continuesConductors = obj.value("continues").toBool(false);
    s.balloon = obj.value("balloon").toBool(true);
    s.lengthBreak = obj.value("lengthBreak").toBool(false);
    s.showName = obj.value("showName").toBool(false);
    for (const QJsonValue& v : obj.value("conductors").toArray())
        s.conductors.push_back(MechConductorGroup::fromJson(v.toObject()));
    s.isSubpart = obj.value("subpart").toBool(false);
    for (const QJsonValue& v : obj.value("subParts").toArray()) {
        const QJsonObject so = v.toObject();
        MechSubPart sp;
        sp.libPart = so.value("libPart").toString();
        sp.kind = kindFromKey(so.value("kind").toString());
        sp.name = so.value("name").toString();
        sp.qty = num(so, "qty", 1.0);
        sp.unit = so.value("unit").toString();
        sp.bomOnly = so.value("bomOnly").toBool(true);
        s.subParts.push_back(sp);
    }
    return s;
}

QJsonObject MechBuildScript::toJson() const {
    QJsonObject o;
    if (!name.isEmpty()) o["name"] = name;
    o["leftToRight"] = leftToRight;
    QJsonArray arr;
    for (const MechStep& s : steps) arr.append(s.toJson());
    o["steps"] = arr;
    return o;
}

MechBuildScript MechBuildScript::fromJson(const QJsonObject& obj) {
    MechBuildScript sc;
    sc.name = obj.value("name").toString();
    sc.leftToRight = obj.value("leftToRight").toBool(true);
    for (const QJsonValue& v : obj.value("steps").toArray())
        sc.steps.push_back(MechStep::fromJson(v.toObject()));
    return sc;
}

// ==================================================================================================
// Compiling a script into the tree the renderer, the bill and the fit checks already understand
// ==================================================================================================

namespace {

// The conductors a run step carries, as wires added to the definition.
//
// A run states its make-up rather than referring to conductors declared elsewhere: the wizard asks
// "how many wires, what gauge" at the step, because that is the question a person building a harness
// answers. A cable states its jacket and its cores the same way.
QStringList wiresForRun(MechHarnessDef& def, const MechStep& step) {
    QStringList ids;
    const QString stem = step.name.isEmpty() ? step.id : step.name;

    // SEVERAL GROUPS WHERE THERE ARE SEVERAL, one where there is one. A hook-up run carrying two of
    // 0.5 and one of 1.5 is one bundle of three conductors, not two runs - so the groups are flattened
    // into the wires of a single segment.
    std::vector<MechConductorGroup> groups = step.conductors;
    if (groups.empty() && step.wireCount > 0) {
        MechConductorGroup only;
        only.count = step.wireCount;
        only.gauge = step.gauge;
        groups.push_back(only);
    }

    int n = 0;
    for (size_t gi = 0; gi < groups.size(); ++gi) {
        const MechConductorGroup& g = groups[gi];
        for (int i = 0; i < std::max(0, g.count); ++i) {
            ++n;
            MechWire w;
            w.id = QStringLiteral("%1W%2").arg(step.id).arg(n);
            // A DESIGNATOR TYPED FOR THIS WIRE WINS. On a harness that starts open there is no
            // connector to be named from, so what the user typed is the only identification the
            // conductor has and nothing derived may overwrite it. Where none was typed the old
            // naming stands: the group's name, or the step's, and the wire's place in it.
            const QString own = g.designators.value(i).trimmed();
            w.label = !own.isEmpty()
                          ? own
                          : (g.name.isEmpty() ? QStringLiteral("%1-%2").arg(stem).arg(n)
                                              : QStringLiteral("%1-%2").arg(g.name).arg(i + 1));
            w.gauge = g.gauge.isEmpty() ? step.gauge : g.gauge;
            w.libPart = g.libPart;
            // A CABLE's cores all carry its name, which is what lets a run be recognised as a cable
            // rather than as loose wires. Loose wires carry none, because they are not cores of
            // anything.
            if (step.kind == PartKind::Cable) w.bundle = stem;
            def.wires.push_back(w);
            ids << w.id;
        }
    }
    return ids;
}

MechFitting fittingFor(const MechStep& step) {
    MechFitting f;
    f.id = step.id;
    f.libPart = step.libPart;
    f.kind = step.kind;
    f.name = step.name;
    f.lengthMm = step.lengthMm;
    f.idMm = step.idMm;
    f.odMm = step.odMm;
    f.dimension = step.dimension;
    f.dimText = step.dimText;
    f.balloon = step.balloon;
    return f;
}

} // namespace

MechCompiled compileMechScript(const MechBuildScript& script) {
    MechCompiled out;
    MechHarnessDef& def = out.def;
    def.name = script.name;

    // WHICH WAY THE HARNESS RUNS. The root is laid in this direction and every other node is placed
    // at its parent's direction plus its own turn, so this one figure flips the whole tree and leaves
    // every relative turn alone. See MechDrawOptions::baseDirectionDeg.
    def.draw.baseDirectionDeg = script.leftToRight ? 0.0 : 180.0;
    // DIMENSIONS ARE PER STEP HERE, AND ONLY PER STEP.
    //
    // MechDrawOptions::dimensions means "a length dimension on every segment" and is on by default,
    // which is right for the builder - it has a tick for it. The wizard has no such tick: it asks
    // "Dimension this step" on each step, one at a time. But the drawing code reads the two as
    // `opt.dimensions || step.dimension`, so the default swamped every one of those boxes - each step
    // came out dimensioned however carefully the boxes had been left clear, and there was nothing in
    // the window to turn it off with.
    //
    // So the script turns off the AUTOMATIC half only: a step that asks is still dimensioned, and the
    // bare-conductor length is still stated, but nothing is dimensioned merely for existing.
    def.draw.dimensions = false;

    // WHICH STATIONS ARE ONLY PLACES. A step with anything hung off it as a part draws none of its own
    // body - see MechNode::hasSubparts - so the nodes those steps land on are marked once the tree is
    // built. Collected here, before any node exists, because it is a fact about the SCRIPT.
    QSet<QString> mothers;
    for (const MechStep& st : script.steps)
        if (st.isSubpart && !st.parentStep.isEmpty()) mothers.insert(st.parentStep);

    const MechStep* first = script.firstStep();
    if (!first) return out;   // an empty script is not an error, it is an empty drawing

    // The root node. Every step lands on a node; the first one needs a node to land on, and what kind
    // of node it becomes is decided by the step itself below.
    MechNode root;
    root.id = QStringLiteral("N1");
    root.name = QStringLiteral("N1");
    root.kind = MechNodeKind::Root;
    def.nodes.push_back(root);

    // Walk the script as a tree, parents before children, so a step always has its anchor placed. A
    // step whose parent is missing is dropped and reported rather than attached to the root, which
    // would put it somewhere the user never asked for.
    struct Pending { const MechStep* step; QString anchorNode; };
    std::vector<Pending> queue;
    queue.push_back({first, root.id});

    while (!queue.empty()) {
        const Pending job = queue.front();
        queue.erase(queue.begin());
        const MechStep& step = *job.step;

        // Where this step's own children will attach. A run moves it on; everything else leaves it.
        QString outputNode = job.anchorNode;

        MechNode* anchor = nullptr;
        for (MechNode& n : def.nodes)
            if (n.id == job.anchorNode) anchor = &n;
        if (!anchor) {
            out.errors << QStringLiteral("Step %1 has no node to attach to.").arg(step.id);
            continue;
        }

        switch (mechStepRole(step.kind)) {
        case MechStepRole::Run: {
            // A length of harness, and a node at the far end of it. The new node is a free end until
            // something later says otherwise - which is exactly what an unfinished branch is.
            MechNode far;
            far.id = def.nextNodeId();
            far.name = far.id;
            far.kind = MechNodeKind::FreeEnd;
            far.angleDeg = step.turnDeg;

            MechSegment seg;
            seg.id = def.nextSegmentId();
            seg.fromNode = anchor->id;
            // AND WHICH HOUSING AT THAT STATION IT LEAVES, when the run hangs off one of them rather
            // than off the station. The parent step is the housing: wires described under connector 1.1
            // leave 1.1, not the place 1.1 happens to stand at. Empty for everything else, which is
            // every run at a station with a single connector on it.
            for (const MechExtraEnd& x : anchor->extraEnds)
                if (x.id == step.parentStep) { seg.fromEnd = x.id; break; }
            seg.toNode = far.id;
            seg.lengthMm = step.lengthMm;
            seg.dimension = step.dimension;
            seg.dimText = step.dimText;
            seg.balloon = step.balloon;
            seg.lengthBreak = step.lengthBreak;

            // THE CONDUCTORS: continued, or started here.
            //
            // Continuing means reusing the run before it - the SAME MechWire ids - which is what stops
            // the drawing inventing a splice. mechGaugeChanges() decides there is a splice by comparing
            // what arrives at a node with what leaves it; two lengths of one bundle that had been given
            // separate wire sets looked exactly like a gauge transition, and the drawing dutifully drew
            // heatshrink over it and added a NOTES entry nobody asked for.
            //
            // A run with nothing to continue from starts its own conductors whatever the flag says: at
            // the first run there is no previous bundle, and honouring the flag there would produce a
            // run with no conductors at all.
            const MechSegment* previous =
                anchor->parentSegment.isEmpty() ? nullptr : def.segment(anchor->parentSegment);
            if (step.continuesConductors && previous && !previous->wireIds.isEmpty()) {
                seg.wireIds = previous->wireIds;
                // A continued run is the same cable it was, jacket and all - otherwise a cable would
                // become loose wires halfway along itself.
                if (previous->isCable() && step.odMm <= 0.0) {
                    seg.jacketOdMm = previous->jacketOdMm;
                    seg.cableName = previous->cableName;
                    seg.cablePart = previous->cablePart;
                }
            } else {
                seg.wireIds = wiresForRun(def, step);
            }
            if (step.kind == PartKind::Cable) {
                seg.jacketOdMm = step.odMm;
                seg.cablePart = step.libPart;
                seg.cableName = step.name.isEmpty() ? step.id : step.name;
            } else if (!step.name.isEmpty()) {
                // A RUN OF LOOSE WIRES HAS A DESIGNATOR TOO. cableName is where a run keeps what it is
                // called; on a cable that is the cable's name and on loose wires it is the run's, and
                // the drawing needs somewhere to read it from either way. Only set from a name somebody
                // actually gave, so an unnamed hook-up run is not lettered with its step id.
                seg.cableName = step.name;
            }
            seg.showName = step.showName;
            far.parentSegment = seg.id;

            def.segments.push_back(seg);
            def.nodes.push_back(far);

            out.stepToSegment.insert(step.id, seg.id);
            out.stepToNode.insert(step.id, far.id);
            out.objectToStep.insert(seg.id, step.id);
            out.objectToStep.insert(far.id, step.id);
            outputNode = far.id;
            break;
        }

        case MechStepRole::End:
            // The node this lands on IS the connector. Nothing is created: a connector at the start of
            // a trunk and one at the end of a branch are the same thing, which is what the domain
            // already says with MechNodeKind::Connector.
            // A SECOND HOUSING ON A NODE THAT IS ALREADY A CONNECTOR, when the step is a part OF what
            // is there rather than the next station along.
            //
            // Two End steps landing on one node used to fight over it: the second simply overwrote the
            // first, so a pair of connectors feeding one cable silently became one connector. Marked as
            // a subpart, the second is what it physically is - another housing at the same place, drawn
            // beside the first with its wires gathering into the same run. Everything the renderer needs
            // is already there; see MechExtraEnd.
            if (step.isSubpart && anchor->kind == MechNodeKind::Connector) {
                MechExtraEnd x;
                x.id = step.id;   // so a run leaving THIS housing can say so - see MechSegment::fromEnd
                x.libPart = step.libPart;
                x.name = step.name;
                x.pinCount = step.pinCount;
                x.balloon = step.balloon;
                anchor->extraEnds.push_back(x);
                out.stepToNode.insert(step.id, anchor->id);
                break;
            }
            anchor->kind = MechNodeKind::Connector;
            if (!step.libPart.isEmpty()) anchor->libPart = step.libPart;
            if (!step.name.isEmpty()) anchor->name = step.name;
            if (step.pinCount > 0) anchor->pinCount = step.pinCount;
            anchor->balloon = step.balloon;
            anchor->dimension = step.dimension;
            anchor->dimText = step.dimText;
            out.stepToNode.insert(step.id, anchor->id);
            out.objectToStep.insert(anchor->id, step.id);
            break;

        case MechStepRole::Node:
            // It goes several ways from here. The branches are the steps naming this one as parent.
            anchor->kind = MechNodeKind::Breakout;
            if (!step.name.isEmpty()) anchor->name = step.name;
            // AND THE PART THAT SITS ON IT, when there is one. A junction is not always a bare point:
            // a splice block, a breakout boot, a three-way adaptor is a bought part with a drawing and
            // its own faces the conductors leave by. The part was simply dropped here - chosen on the
            // step, absent from the node - so it reached neither the drawing nor the bill.
            if (!step.libPart.isEmpty()) anchor->libPart = step.libPart;
            anchor->balloon = step.balloon;
            out.stepToNode.insert(step.id, anchor->id);
            out.objectToStep.insert(anchor->id, step.id);
            break;

        case MechStepRole::AtNode:
            anchor->fittings.push_back(fittingFor(step));
            out.stepToNode.insert(step.id, anchor->id);
            break;

        case MechStepRole::AlongRun: {
            // Over the run that ARRIVES at this node. At the root nothing arrives yet, so there is
            // nothing to put it on - and saying so is better than sliding it onto the run that leaves,
            // which is a different piece of harness.
            MechSegment* seg = nullptr;
            for (MechSegment& s : def.segments)
                if (s.id == anchor->parentSegment) seg = &s;
            if (!seg) {
                out.errors << QStringLiteral(
                                  "%1 goes over a run, and there is no run before it yet.")
                                  .arg(step.name.isEmpty() ? step.id : step.name);
                break;
            }
            seg->coverings.push_back(fittingFor(step));
            out.stepToSegment.insert(step.id, seg->id);
            break;
        }
        }

        for (const MechStep* child : script.childSteps(step.id))
            queue.push_back({child, outputNode});
    }

    // THE STATIONS THAT ARE ONLY PLACES, now that every step has landed on a node. A step with parts
    // hung off it does not draw a body of its own - see MechNode::hasSubparts - and which node that is
    // can only be said once the tree has been walked.
    for (const QString& motherStep : mothers) {
        const QString nodeId = out.stepToNode.value(motherStep);
        if (nodeId.isEmpty()) continue;
        // NOT "only where the node is the mother's own". That guard read well and was wrong: a later
        // step landing on the same node takes ownership of it, so the station un-silenced itself the
        // moment the next one was being described - which is the one state somebody is always looking
        // at. The node the mother landed on is where its parts are; that is the whole of the question.
        for (MechNode& nd : out.def.nodes)
            if (nd.id == nodeId) nd.hasSubparts = true;
    }

    return out;
}

// ==================================================================================================
// Whether the neighbours go together
// ==================================================================================================

namespace {

// A gauge as mm2, from either "0.5" or "22 AWG". Both are written on real drawings and both arrive
// here. 0 means it said nothing usable.
double gaugeAsMm2(const QString& gauge) { return mechGaugeMm2(gauge); }

// A part's Range parameter, as its two halves. The library stores a Range under "<key>Min" and
// "<key>Max"; either may be absent, which is a part that only states one end of its window.
void rangeOf(const LibraryPart& part, const QString& key, double& lo, double& hi) {
    lo = part.params.value(key + QStringLiteral("Min")).toDouble();
    hi = part.params.value(key + QStringLiteral("Max")).toDouble();
}

// What to call a library part in a message. LibraryPart has no single "name": a released part is
// known by its manufacturer number, a house part by its description, and something half-entered by
// neither - so the id is the last resort rather than the first.
QString partLabel(const LibraryPart& p, const QString& fallbackId) {
    if (!p.description.trimmed().isEmpty()) return p.description.trimmed();
    if (!p.mfrPn.trimmed().isEmpty()) return p.mfrPn.trimmed();
    return p.id.isEmpty() ? fallbackId : p.id;
}

struct Named {
    QString id;
    QString shown;
};

Named nameOf(const MechStep& s) {
    return {s.id, s.name.isEmpty() ? partKindName(s.kind) : s.name};
}

} // namespace

std::vector<MechStepIssue> mechScriptIssues(const MechBuildScript& script,
                                            const MechCompiled& compiled,
                                            const PartsCatalog* catalog) {
    std::vector<MechStepIssue> out;

    const auto add = [&](MechIssueKind kind, const MechStep& s, const MechStep* other,
                         const QString& msg) {
        MechStepIssue i;
        i.kind = kind;
        i.stepId = s.id;
        i.stepName = nameOf(s).shown;
        if (other) i.neighbourStepId = other->id;
        i.message = msg;
        out.push_back(i);
    };

    for (const MechStep& step : script.steps) {
        const MechStep* parent = step.parentStep.isEmpty() ? nullptr
                                                           : script.step(step.parentStep);

        // ---- A CONTACT AGAINST THE CONDUCTOR IT IS CRIMPED ONTO ----------------------------------
        //
        // The check nobody can do by eye on a released drawing, and the one most worth having: a 20
        // AWG contact on a 14 AWG wire is a crimp that pulls out. Both figures are stated - the
        // contact's range by its library entry, the conductor's gauge by the run - so the comparison
        // needs no assumption at all.
        if (!step.contactPart.isEmpty()) {
            // The conductor is the one on the run this connector sits on: the parent step when that
            // parent is a run.
            const MechStep* run = (parent && mechStepRole(parent->kind) == MechStepRole::Run)
                                      ? parent
                                      : nullptr;
            if (!run) {
                add(MechIssueKind::Gap, step, nullptr,
                    QStringLiteral("A contact is chosen but there is no run before this step to say "
                                   "what gauge it is being crimped onto."));
            } else if (gaugeAsMm2(run->gauge) <= 0.0) {
                add(MechIssueKind::Gap, step, run,
                    QStringLiteral("The contact cannot be checked: the run before it has no gauge."));
            } else if (!catalog) {
                add(MechIssueKind::Gap, step, run,
                    QStringLiteral("The contact cannot be checked: no parts library is open."));
            } else if (const LibraryPart* c = catalog->byId(step.contactPart)) {
                double lo = 0.0, hi = 0.0;
                rangeOf(*c, QStringLiteral("gauge"), lo, hi);
                const bool inAwg =
                    c->params.value(QStringLiteral("gaugeMode")).compare(QLatin1String("AWG"),
                                                                        Qt::CaseInsensitive) == 0;
                if (lo <= 0.0 && hi <= 0.0) {
                    add(MechIssueKind::Gap, step, run,
                        QStringLiteral("%1 states no gauge range, so the crimp cannot be checked.")
                            .arg(partLabel(*c, step.contactPart)));
                } else {
                    // BOTH ENDS INTO mm2 BEFORE COMPARING, and this is the part that has to be got
                    // right: AWG runs BACKWARDS. "AWG 20 to 24" is 0.52 mm2 down to 0.20 mm2, so the
                    // number written as the minimum is the LARGER cross-section. Comparing the raw
                    // figures would pass every wire that should fail and fail every one that should
                    // pass.
                    double loMm2 = inAwg ? mechAwgMm2(static_cast<int>(std::lround(lo))) : lo;
                    double hiMm2 = inAwg ? mechAwgMm2(static_cast<int>(std::lround(hi))) : hi;
                    if (loMm2 > hiMm2) std::swap(loMm2, hiMm2);

                    const double wireMm2 = gaugeAsMm2(run->gauge);
                    if ((loMm2 > 0.0 && wireMm2 < loMm2 * 0.999) ||
                        (hiMm2 > 0.0 && wireMm2 > hiMm2 * 1.001)) {
                        add(MechIssueKind::Violation, step, run,
                            QStringLiteral("%1 takes %2 to %3 mm² and the run is %4 mm² (%5).")
                                .arg(partLabel(*c, step.contactPart))
                                .arg(loMm2, 0, 'f', 2)
                                .arg(hiMm2, 0, 'f', 2)
                                .arg(wireMm2, 0, 'f', 2)
                                .arg(run->gauge));
                    }
                }
            } else {
                add(MechIssueKind::Gap, step, run,
                    QStringLiteral("The contact %1 is not in any open library.")
                        .arg(step.contactPart));
            }
        }

        // ---- A FITTING AGAINST WHAT IT HAS TO PASS -----------------------------------------------
        //
        // Delegated to mechFittingFits, which already reads the library's own window fields - a
        // backshell's cable entry, a boot's, a sleeve's diameter - and already reports "unstated" as
        // a gap rather than a pass. Reimplementing that here would be a second opinion on the same
        // question, and the two would drift.
        const MechStepRole role = mechStepRole(step.kind);
        if ((role == MechStepRole::AtNode || role == MechStepRole::AlongRun) &&
            !step.libPart.isEmpty()) {
            if (!catalog) {
                add(MechIssueKind::Gap, step, parent,
                    QStringLiteral("%1 cannot be checked: no parts library is open.")
                        .arg(nameOf(step).shown));
            } else if (const LibraryPart* p = catalog->byId(step.libPart)) {
                // What it goes over: the run it is on, taken from the compiled tree so the figure is
                // the one the drawing will use - jacket where there is a jacket, bundle where the
                // jacket is stripped.
                double overMm = 0.0;
                const QString segId = compiled.stepToSegment.value(step.id);
                const MechSegment* seg = segId.isEmpty() ? nullptr : compiled.def.segment(segId);
                if (!seg && parent) {
                    const QString pSeg = compiled.stepToSegment.value(parent->id);
                    if (!pSeg.isEmpty()) seg = compiled.def.segment(pSeg);
                }
                if (seg) overMm = segmentBodyDiameterMm(compiled.def, *seg);

                if (overMm <= 0.0) {
                    add(MechIssueKind::Gap, step, parent,
                        QStringLiteral("%1 cannot be checked: what it goes over has no diameter yet.")
                            .arg(nameOf(step).shown));
                } else {
                    QString why;
                    bool known = false;
                    const bool fits = mechFittingFits(*p, overMm, why, &known);
                    if (!known) {
                        add(MechIssueKind::Gap, step, parent,
                            QStringLiteral("%1 states no size window, so the fit cannot be checked.")
                                .arg(partLabel(*p, step.libPart)));
                    } else if (!fits) {
                        add(MechIssueKind::Violation, step, parent,
                            QStringLiteral("%1 does not fit: %2.")
                                .arg(partLabel(*p, step.libPart), why));
                    }
                }
            } else {
                add(MechIssueKind::Gap, step, parent,
                    QStringLiteral("%1 is not in any open library.").arg(step.libPart));
            }
        }

        // ---- A RUN WITH NOTHING SAID ABOUT IT ----------------------------------------------------
        if (role == MechStepRole::Run) {
            if (step.lengthMm <= 0.0)
                add(MechIssueKind::Gap, step, parent,
                    QStringLiteral("%1 has no length.").arg(nameOf(step).shown));
            if (step.kind == PartKind::Cable && step.odMm <= 0.0)
                add(MechIssueKind::Gap, step, parent,
                    QStringLiteral("%1 is a cable with no jacket diameter, so nothing over it can be "
                                   "sized.")
                        .arg(nameOf(step).shown));
            if (step.wireCount > 0 && gaugeAsMm2(step.gauge) <= 0.0)
                add(MechIssueKind::Gap, step, parent,
                    QStringLiteral("%1 has %2 conductor(s) and no gauge.")
                        .arg(nameOf(step).shown)
                        .arg(step.wireCount));
        }
    }

    // Anything the compiler itself could not do is news of the same kind.
    for (const QString& e : compiled.errors) {
        MechStepIssue i;
        i.kind = MechIssueKind::Violation;
        i.message = e;
        out.push_back(i);
    }

    return out;
}

} // namespace Edraw_Desktop
