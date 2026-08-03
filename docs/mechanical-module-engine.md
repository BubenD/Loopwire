# The Hybrid Mechanical Module Engine

Backshells, heatshrink boots, sleeves, conduits, strain reliefs and connectors — placed on a bundle
either from the library or made up on the spot, and validated identically either way.

This is the architecture for the C# side. Where it touches what already exists in the C++
application (`src/harness/PartsLibrary.h`, `src/harness/Harness.h`), that is called out — the
library, its per-kind parameter metadata and its `.plb` files are already built, and this design
consumes them rather than starting a second parts library.

---

## 1. The decision that shapes everything else

**Separate the *specification* from the *placement*.**

A `MechanicalSpec` is a bag of parameters: what the thing is. A `MechanicalModule` is a spec placed
somewhere: where it sits on the bundle, which way it faces, what it covers. The library stores
specs. The canvas holds modules. A module *has* a spec.

That split is what makes the hybrid mode fall out for free instead of being a fork through the whole
codebase:

- **From Library** — the spec is a copy of a library item's spec, plus a link back to it.
- **Manual / Custom** — the spec is one the user typed. Same type, no link.
- **Overridden Library** — the spec is a library item's, plus a *delta*: the fields the user changed,
  with the original values kept beside them.

Everything downstream — DRC, the drawing, the bill of materials, the 2D symbol — reads the spec and
never asks where it came from. There is no `if (IsCustom)` anywhere in the rules.

The corollary matters just as much: **an override keeps both values.** A module that says
"Raychem 202A132-25-0, cable entry 9.5 mm" when the manufacturer's boot is 12.7 mm is a
manufacturing defect on a drawing. `OverriddenLibrary` therefore carries the manufacturer's figures
*and* the changed ones, so the drawing, the BOM and the DRC can all say "this P/N, modified" —
loudly.

---

## 2. Domain model

### 2.1 Provenance and identity

```csharp
public enum MechanicalSourceType
{
    /// A library item, used as the manufacturer specifies it.
    LibraryItem,
    /// Typed on the spot. No manufacturer, no part number.
    ManualCustom,
    /// A library item with one or more parameters deliberately changed.
    /// The manufacturer's own figures are retained in Origin.LibraryValues.
    OverriddenLibrary
}

public enum MechanicalKind
{
    Backshell, HeatshrinkBoot, Sleeve, Conduit, StrainRelief, Connector,
    YSplit, ShieldBraid, Grommet, CableGland, CableTie, Overmould, Other
}
```

`MechanicalKind` is deliberately the same set as `PartKind` in
[`PartsLibrary.h`](../src/harness/PartsLibrary.h) — one key per kind (`"heatshrink-boot"`,
`"backshell"`, `"y-split"`), so a `.plb` file written by the C++ side loads here without a
translation table. Keep the *keys* stable; the display names may change.

```csharp
/// Where a spec came from. Null on a pure ManualCustom spec.
public sealed record SpecOrigin
{
    public required string LibraryId { get; init; }      // which library (path or GUID)
    public required string PartId { get; init; }         // LibraryPart.id
    public string? LibraryRevision { get; init; }        // so drift can be detected later
    public required string Manufacturer { get; init; }
    public required string PartNumber { get; init; }
    public string? InternalPartNumber { get; init; }

    /// The manufacturer's own values for every parameter the user has since changed.
    /// Empty on LibraryItem; populated on OverriddenLibrary.
    public ImmutableDictionary<string, string> LibraryValues { get; init; }
        = ImmutableDictionary<string, string>.Empty;
}
```

### 2.2 The spec

Geometry that every kind has is typed. Geometry that only some kinds have lives in the parameter
dictionary, keyed exactly as the library keys it. This is the same shape as `LibraryPart.params`, and
for the same reason: a kind gains a parameter by being given one in the metadata, with no new class
to write and no schema migration.

```csharp
/// A fit window: a min/max pair. The library's Range fields (PartField.Type.Range) land here,
/// stored as "<key>Min" / "<key>Max" exactly as the .plb file writes them.
public readonly record struct FitWindow(double? Min, double? Max)
{
    public bool IsStated => Min.HasValue || Max.HasValue;
    public bool Accepts(double v, double tolerance = 0.0) =>
        (!Min.HasValue || v >= Min.Value - tolerance) &&
        (!Max.HasValue || v <= Max.Value + tolerance);
}

public class MechanicalSpec
{
    public required MechanicalKind Kind { get; init; }
    public MechanicalSourceType SourceType { get; set; } = MechanicalSourceType.ManualCustom;
    public SpecOrigin? Origin { get; set; }

    public bool IsCustom => SourceType != MechanicalSourceType.LibraryItem;
    public bool IsTraceable => Origin is not null;   // a P/N can be put on the drawing

    /// A name for a custom part; the library description for a library one.
    public string Description { get; set; } = "";

    // ---- Geometry every kind carries ----
    public double? InnerDiameter { get; set; }        // mm, as supplied
    public double? OuterDiameter { get; set; }        // mm
    public double? Length { get; set; }               // mm, along the bundle
    public double? BreakoutOverlapLength { get; set; } // mm the module runs past the breakout point
    public string? Color { get; set; }

    // ---- Everything kind-specific, by the library's own keys ----
    // "shellSize", "connEntryMin"/"connEntryMax", "cableEntryMin"/"cableEntryMax", "shrinkRatio",
    // "material", "adhesive", "angle", "legs", "leg1Min"...
    public Dictionary<string, string> Parameters { get; } = new(StringComparer.Ordinal);

    /// The 2D symbol: a vector path, or a reference to the library part's stored drawing.
    public DrawingSymbol2D? DrawingSymbol2D { get; set; }

    // ---- Typed reads over the dictionary ----
    public double? Number(string key) =>
        Parameters.TryGetValue(key, out var s) &&
        double.TryParse(s, NumberStyles.Float, CultureInfo.InvariantCulture, out var v) ? v : null;

    public bool? Flag(string key) =>
        Parameters.TryGetValue(key, out var s) ? s == "1" : null;

    public FitWindow Window(string key) => new(Number(key + "Min"), Number(key + "Max"));

    public MechanicalSpec Clone() { /* deep copy, Origin carried, Parameters copied */ }
}
```

Two notes on why the dictionary is `string`-valued and not `double`:

- The library stores it that way (`QMap<QString, QString> params`), and a value is allowed to be
  blank, or `"as required"`, or a range written by hand. Parsing at the edge and keeping the raw text
  means a part that a supplier described loosely still round-trips instead of being silently
  zeroed.
- A missing value and a zero value are different things, and the DRC has to tell them apart. `null`
  from `Number()` becomes a **data gap**; `0` is a stated figure that is probably wrong.

### 2.3 The 2D symbol

```csharp
public sealed class DrawingSymbol2D
{
    /// How the symbol is obtained. Parametric is generated from the spec; Stored comes from the
    /// library part's own mechanical drawing; Imported is a DXF/SVG the user supplied.
    public SymbolSourceKind SourceKind { get; init; }

    /// Parametric: which generator, so the symbol redraws when Length or OD changes.
    public string? GeneratorKey { get; init; }         // "backshell.straight", "boot.90"

    /// Stored/Imported: the geometry itself, as entities.
    /// This is LibraryPart.mechanical (a QJsonArray of entities) on the C++ side.
    public string? EntitiesJson { get; init; }

    /// Where the bundle enters and leaves, in symbol space. What lets a module be placed on a
    /// polyline and oriented without the user nudging it.
    public Anchor2D BundleIn { get; init; }
    public Anchor2D BundleOut { get; init; }
}
```

A custom module with no drawing is not a problem: `SourceKind = Parametric` with a generator chosen
from the kind gives it a sensible outline from its own ID/OD/length. That is what makes "Manual
Custom" usable on the first click rather than a nameless box.

### 2.4 The placed module

```csharp
public interface IMechanicalModule
{
    Guid Id { get; }
    MechanicalSpec Spec { get; }
    /// The bundle segment it sits on, and how far along it.
    BundlePlacement Placement { get; }
    /// What it physically covers: needed by fit rules and by the BOM's cut lengths.
    IReadOnlyList<Guid> CoveredWireIds { get; }
    IEnumerable<MechanicalPort> Ports { get; }   // connector side / cable side / legs
}

public abstract class MechanicalComponentBase : IMechanicalModule
{
    public Guid Id { get; } = Guid.NewGuid();
    public MechanicalSpec Spec { get; }
    public BundlePlacement Placement { get; set; }
    public List<Guid> CoveredWireIds { get; } = new();

    protected MechanicalComponentBase(MechanicalSpec spec) => Spec = spec;

    /// Convenience passthroughs so call sites and XAML bindings read naturally.
    public MechanicalSourceType SourceType => Spec.SourceType;
    public string? Manufacturer => Spec.Origin?.Manufacturer;
    public string? PartNumber => Spec.Origin?.PartNumber;
    public bool IsCustom => Spec.IsCustom;

    public abstract MechanicalKind Kind { get; }
    public abstract IEnumerable<MechanicalPort> Ports { get; }
}
```

Concrete classes stay thin — they exist to name the ports and to expose the kind's own parameters
with real names:

```csharp
public sealed class Backshell : MechanicalComponentBase
{
    public override MechanicalKind Kind => MechanicalKind.Backshell;
    public string? ShellSize => Spec.Parameters.GetValueOrDefault("shellSize");
    public FitWindow ConnectorEntry => Spec.Window("connEntry");
    public FitWindow CableEntry => Spec.Window("cableEntry");
    public bool? TerminatesShield => Spec.Flag("shielded");

    public override IEnumerable<MechanicalPort> Ports =>
    [
        new(MechanicalPortRole.ConnectorSide, ConnectorEntry),
        new(MechanicalPortRole.CableSide,     CableEntry),
    ];
}

public sealed class HeatshrinkBoot : MechanicalComponentBase
{
    public override MechanicalKind Kind => MechanicalKind.HeatshrinkBoot;
    public FitWindow ConnectorEntry => Spec.Window("connEntry");
    public FitWindow CableEntry     => Spec.Window("cableEntry");
    /// As-supplied ID over recovered ID. 2.0 means it shrinks to half.
    public double? ShrinkRatio => Spec.Number("shrinkRatio");
    public bool? AdhesiveLined => Spec.Flag("adhesive");
    // ...
}
```

`Sleeve`, `Conduit`, `StrainRelief`, `YSplit` and the mechanical face of `Connector` follow the same
pattern. `YSplit` is the one with a variable port count — its legs come from the `legs` parameter and
`leg1`/`leg2`/`leg3` windows, matching what `partFields(PartKind::YSplit)` already defines.

---

## 3. The library side

### 3.1 One interface, several stores

```csharp
public interface IMechanicalLibrary
{
    string LibraryId { get; }
    string DisplayName { get; }
    bool IsWritable { get; }                       // the company library usually is not

    Task<IReadOnlyList<MechanicalLibraryItem>> SearchAsync(MechanicalQuery query, CancellationToken ct);
    Task<MechanicalLibraryItem?> GetAsync(string partId, CancellationToken ct);
    Task<MechanicalLibraryItem> SaveAsync(MechanicalSpec spec, CancellationToken ct);  // IsWritable only
}

public sealed record MechanicalLibraryItem
{
    public required string PartId { get; init; }
    public required MechanicalKind Kind { get; init; }
    public required string Description { get; init; }
    public required string Manufacturer { get; init; }
    public required string PartNumber { get; init; }
    public string? InternalPartNumber { get; init; }
    public string? Revision { get; init; }
    public required ImmutableDictionary<string, string> Parameters { get; init; }
    public DrawingSymbol2D? Symbol { get; init; }
    public bool HasGeometry => Symbol is not null;
}
```

Implementations, in the order they should be added:

| Implementation | Backing store | Writable |
| --- | --- | --- |
| `PlbMechanicalLibrary` | the existing `.plb` files via `PartsCatalog` | the active one |
| `MyCustomMechanicalLibrary` | the user's own local file, separate from the shared one | yes |
| `ProjectDbMechanicalLibrary` | the project/company database | usually no |

`PlbMechanicalLibrary` is the one to build first, because it needs no new data at all: `.plb` already
carries `mfrName`, `mfrPn`, `internalPn`, the per-kind `params`, and a mechanical drawing per part.
The mapping is mechanical (`LibraryPart` → `MechanicalLibraryItem`, `params` → `Parameters`,
`mechanical` → `DrawingSymbol2D { SourceKind = Stored, EntitiesJson = … }`).

A `CompositeMechanicalLibrary` fans a query across all connected libraries and tags each result with
where it came from — the same "several libraries at once, one of them being worked on" behaviour
`PartsCatalog` already implements.

### 3.2 Compatibility is a query, not a UI filter

The single most useful thing this engine can do is *not show the user boots that will not fit*. That
requires the fit context to reach the library, so it is part of the query rather than a predicate the
view applies afterwards:

```csharp
public sealed record MechanicalQuery
{
    public MechanicalKind? Kind { get; init; }
    public string? Text { get; init; }                  // free text over description / P/N
    public FitContext? Fit { get; init; }               // the whole point
    public bool OnlyWithGeometry { get; init; }
    public int Limit { get; init; } = 200;
}

/// What the module has to fit, measured from the drawing. Every field is nullable because the
/// drawing may not say yet — and "not stated" must not be read as "no constraint".
public sealed record FitContext
{
    public double? BundleOuterDiameter { get; init; }   // mm at the point of placement
    public double? ConnectorShellDiameter { get; init; }
    public string? ConnectorShellSize { get; init; }     // "13", "17" ... when it is a shell series
    public double? AvailableLength { get; init; }        // to the next fixed feature
    public int? LegCount { get; init; }                  // at a breakout
    public IReadOnlyList<double>? LegDiameters { get; init; }
    public string? Environment { get; init; }            // for material rules
}
```

`FitContext` is built once, by the canvas, from the bundle at the placement point — and then it is
handed to the picker *and* to the DRC. One source of truth for "what does it have to fit", used by
the thing that offers choices and the thing that judges them. If those two ever disagree, the picker
offers parts the DRC then rejects, which is the worst failure mode this window can have.

---

## 4. The GUI: picker and inspector

### 4.1 One draft, two tabs

The tabs are not two flows. They are two ways of filling in **one** `MechanicalSpecDraft`, and the
`Accept` command is gated on the same validation for both.

```
┌─ Add / edit module ─────────────────── Kind: Heatshrink boot ▾ ──┐
│ ┌ From library ─┬─ Custom ─┐                                    │
│ │ search: [raychem 202  ]  [x] only ones that fit  [ ] w/ 3D    │
│ │ ┌──────────────────────────────────────────────────────────┐  │
│ │ │ 202A132-25-0  Boot, 90°, shell 25, 12.7–6.4 mm   ✓ fits  │  │
│ │ │ 202A121-25-0  Boot, straight, shell 25, 12.7 mm   ✓ fits │  │
│ │ │ 202A132-17-0  Boot, 90°, shell 17, 8.6–4.8 mm   ✗ bundle │  │
│ │ └──────────────────────────────────────────────────────────┘  │
│ └──────────────────────────────────────────────────────────────┘ │
│ Length 34.5 mm   ID 12.7 mm   OD 15.2 mm   Overlap 6.0 mm       │
│ ⚠ Bundle is 13.1 mm; this boot recovers to 12.7 mm at most.      │
│ [x] Save this custom component to My Library    [ Place ] [Cancel]│
└──────────────────────────────────────────────────────────────────┘
```

Note that the geometry row and the warning line sit **below both tabs**, not inside one. They are
the draft, and the draft is what gets placed.

### 4.2 ViewModels

```csharp
public sealed partial class MechanicalPickerViewModel : ObservableObject
{
    private readonly IMechanicalLibrary _libraries;      // composite
    private readonly IMechanicalDrcService _drc;
    private readonly IMechanicalLibrary _myLibrary;      // writable, for Save-to-library

    [ObservableProperty] private MechanicalKind _kind;
    [ObservableProperty] private FitContext _fit = new();
    [ObservableProperty] private PickerMode _mode = PickerMode.Library;

    // ---- The library tab ----
    [ObservableProperty] private string _searchText = "";
    [ObservableProperty] private bool _onlyCompatible = true;
    public ObservableCollection<LibraryResultViewModel> Results { get; } = new();
    [ObservableProperty] private LibraryResultViewModel? _selectedResult;

    // ---- The one draft both tabs write into ----
    [ObservableProperty] private MechanicalSpecDraftViewModel _draft;

    // ---- What the draft is worth ----
    public ObservableCollection<DrcFindingViewModel> Findings { get; } = new();
    [ObservableProperty] private bool _canPlace;
    [ObservableProperty] private bool _saveToMyLibrary;
    public bool CanSaveToMyLibrary => _draft.IsCustomised && _myLibrary.IsWritable;

    [RelayCommand] private async Task SearchAsync() { /* query with Fit when OnlyCompatible */ }

    /// Picking a library row fills the draft. It does not place anything, and it does not lock the
    /// fields: editing one afterwards is what turns LibraryItem into OverriddenLibrary.
    partial void OnSelectedResultChanged(LibraryResultViewModel? row)
    {
        if (row is null) return;
        Draft.LoadFrom(row.Item);          // sets SourceType = LibraryItem, Origin = the item
        Revalidate();
    }

    [RelayCommand(CanExecute = nameof(CanPlace))]
    private async Task PlaceAsync()
    {
        var spec = Draft.ToSpec();
        if (SaveToMyLibrary && spec.IsCustom)
        {
            var saved = await _myLibrary.SaveAsync(spec, default);
            spec.SourceType = MechanicalSourceType.LibraryItem;   // now it IS a library item
            spec.Origin = SpecOrigin.From(saved, _myLibrary.LibraryId);
        }
        Accepted?.Invoke(spec);
    }

    public void Revalidate()
    {
        Findings.Clear();
        var report = _drc.Validate(Draft.ToSpec(), Fit);
        foreach (var f in report.Findings) Findings.Add(new DrcFindingViewModel(f));
        // Data gaps do not block placing — they are recorded against the module and shown on the
        // drawing as unresolved. Hard violations do.
        CanPlace = !report.HasBlockingViolation && Draft.IsPlaceable;
    }
}
```

The draft is where the two modes actually converge:

```csharp
public sealed partial class MechanicalSpecDraftViewModel : ObservableObject
{
    private MechanicalSpec _spec;
    private SpecOrigin? _origin;
    /// The library's own values, captured on LoadFrom, so an edit can be reported as an override
    /// rather than passed off as the manufacturer's figure.
    private ImmutableDictionary<string, string> _libraryValues = ImmutableDictionary<string, string>.Empty;

    [ObservableProperty] private string _description = "";
    [ObservableProperty] private double? _length;
    [ObservableProperty] private double? _innerDiameter;
    [ObservableProperty] private double? _outerDiameter;
    [ObservableProperty] private double? _breakoutOverlapLength;
    [ObservableProperty] private string? _color;

    /// The kind's own parameters, built from the metadata — one row per PartField, so a kind that
    /// gains a parameter gains a row with no XAML change. Mirrors partFields() on the C++ side.
    public ObservableCollection<ParameterFieldViewModel> Fields { get; } = new();

    /// True once anything differs from the library item, or when there is no library item at all.
    public bool IsCustomised { get; private set; }

    /// Enough has been said to draw it: a length, and one diameter.
    public bool IsPlaceable => _length is > 0 && (_innerDiameter is > 0 || _outerDiameter is > 0);

    public void LoadFrom(MechanicalLibraryItem item) { /* fill fields, remember _libraryValues */ }

    public MechanicalSpec ToSpec()
    {
        var spec = _spec.Clone();
        // ... copy the typed fields and every ParameterFieldViewModel value in ...
        spec.SourceType = _origin is null            ? MechanicalSourceType.ManualCustom
                        : ChangedKeys().Any()        ? MechanicalSourceType.OverriddenLibrary
                                                     : MechanicalSourceType.LibraryItem;
        spec.Origin = _origin is null ? null
            : _origin with { LibraryValues = ChangedKeys().ToImmutableDictionary(k => k, k => _libraryValues[k]) };
        return spec;
    }

    private IEnumerable<string> ChangedKeys() => /* current vs _libraryValues */;
}
```

`ParameterFieldViewModel` is generated from the kind's field metadata and is the same class for both
tabs — in library mode it shows the manufacturer's value and marks itself `IsOverridden` when
touched; in custom mode it starts blank. One control, one binding, one validation path:

```csharp
public sealed partial class ParameterFieldViewModel : ObservableObject
{
    public required string Key { get; init; }              // "cableEntryMin"
    public required string Label { get; init; }            // "Cable entry (min)"
    public required ParameterFieldType Type { get; init; } // Text | Number | Range | Bool | Choice
    public IReadOnlyList<string>? Choices { get; init; }
    public string? Unit { get; init; }                     // "mm"

    [ObservableProperty] private string _value = "";
    public string? LibraryValue { get; init; }
    public bool IsOverridden => LibraryValue is not null && _value != LibraryValue;
}
```

### 4.3 The inline inspector is the same ViewModel

Selecting a placed module on the canvas opens the inspector, which is `MechanicalPickerViewModel`
bound to an existing spec instead of a blank one (`LoadFromModule(module)`), with `Place` reading
`Apply`. Two windows, one ViewModel, one validation path — and "change this boot for the next size
up" is the same interaction as choosing it in the first place.

---

## 5. Physical DRC

### 5.1 Rules see specs, never library items

```csharp
public interface IMechanicalRule
{
    string RuleId { get; }        // "MECH.BUNDLE_FIT"
    string Title { get; }
    bool AppliesTo(MechanicalSpec spec);
    IEnumerable<MechanicalFinding> Check(MechanicalSpec spec, FitContext fit);
}
```

Because the signature takes a `MechanicalSpec`, requirement 3 is satisfied structurally rather than
by discipline: a rule *cannot* branch on provenance, because it is not given any. The only place
`SourceType` is read is in reporting — to say "P/N 202A132-25-0, entry overridden to 9.5 mm" instead
of just "entry 9.5 mm", which is exactly where that distinction earns its keep.

### 5.2 The rules worth having first

```csharp
/// The bundle has to fit the entry the bundle goes into.
public sealed class BundleFitRule : IMechanicalRule
{
    public string RuleId => "MECH.BUNDLE_FIT";
    public bool AppliesTo(MechanicalSpec s) => true;

    public IEnumerable<MechanicalFinding> Check(MechanicalSpec spec, FitContext fit)
    {
        if (fit.BundleOuterDiameter is not { } bundle)
        {
            yield return MechanicalFinding.Gap(RuleId, "Bundle diameter",
                "The bundle's diameter at this point is not known, so fit cannot be checked.");
            yield break;
        }
        var window = spec.Window("cableEntry");
        if (!window.IsStated)
        {
            yield return MechanicalFinding.Gap(RuleId, "cableEntry",
                "This module's cable-side entry size is not stated.", unit: "mm");
            yield break;
        }
        if (!window.Accepts(bundle))
            yield return MechanicalFinding.Violation(RuleId,
                $"The bundle is {bundle:0.0} mm; this module takes " +
                $"{window.Min:0.0}–{window.Max:0.0} mm.");
    }
}

/// Heatshrink has to be able to shrink onto the bundle — and be able to go over it first.
public sealed class ShrinkFitRule : IMechanicalRule
{
    public string RuleId => "MECH.SHRINK_FIT";
    public bool AppliesTo(MechanicalSpec s) =>
        s.Kind is MechanicalKind.HeatshrinkBoot or MechanicalKind.YSplit;

    public IEnumerable<MechanicalFinding> Check(MechanicalSpec spec, FitContext fit)
    {
        if (fit.BundleOuterDiameter is not { } bundle) yield break;
        if (spec.InnerDiameter is not { } supplied) yield break;

        // Supplied ID must clear the bundle, or it will not go on.
        if (supplied < bundle * (1.0 + MinSlipClearance))
            yield return MechanicalFinding.Violation(RuleId,
                $"Supplied ID {supplied:0.0} mm will not pass over a {bundle:0.0} mm bundle.");

        // Recovered ID must be under the bundle, or it will not grip.
        if (spec.Number("shrinkRatio") is { } ratio and > 1.0)
        {
            var recovered = supplied / ratio;
            if (recovered > bundle)
                yield return MechanicalFinding.Violation(RuleId,
                    $"Recovers to {recovered:0.0} mm, which is larger than the {bundle:0.0} mm " +
                    "bundle: it will not grip.");
        }
        else
            yield return MechanicalFinding.Gap(RuleId, "shrinkRatio",
                "Shrink ratio not stated, so recovery onto the bundle cannot be checked.");
    }
    private const double MinSlipClearance = 0.05;
}
```

Then: `ShellSizeMatchRule` (backshell/boot shell size against the connector's), `BreakoutOverlapRule`
(the overlap covers the breakout and does not run past the next feature), `ConduitBendRadiusRule`,
`LengthAvailableRule` (`Length + overlaps ≤ FitContext.AvailableLength`), `MaterialEnvironmentRule`,
and `LegFitRule` for Y-splits (each leg's window against that leg's diameter).

### 5.3 Findings, and the gaps among them

```csharp
public enum MechanicalFindingKind { Violation, Warning, DataGap }

public sealed record MechanicalFinding
{
    public required string RuleId { get; init; }
    public required MechanicalFindingKind Kind { get; init; }
    public required string Message { get; init; }
    /// DataGap only: which parameter is missing, so the UI can offer the right box to fill.
    public string? MissingKey { get; init; }
    public string? Unit { get; init; }
    public Guid? ModuleId { get; init; }
}
```

`DataGap` is the finding type that makes this engine usable on real, half-finished drawings, and it
is the same idea as the `ValidationDataGap` in the compatibility-validator design and the `VdGap` now
implemented in [`VoltageDrop.h`](../src/analysis/VoltageDrop.h): **a parameter the rule needed and
the data does not state is reported, never assumed.** A guessed entry size produces a fit check
nobody can sign.

Each gap carries `MissingKey`, so the inspector can put an editable box for exactly that parameter
next to the message, and two actions:

- **Fill it here** — writes to the draft/module spec. If the module was `LibraryItem`, this makes it
  `OverriddenLibrary`, with the gap-filled key recorded in `Origin.LibraryValues` as absent-in-library.
- **Fix it in the library** — writes the value back to the library item (writable libraries only), so
  the next drawing that uses that P/N starts complete.

Re-validation is synchronous and immediate: the draft raises `PropertyChanged`, the picker calls
`Revalidate()`, findings refresh. No "run DRC" button inside the picker — the button belongs to the
whole-drawing check, not to the dialog that is choosing one part.

---

## 6. Data handling and persistence

### 6.1 Snapshot *and* link

A placed module serialises **the whole spec**, plus the link back to the library:

```jsonc
{
  "id": "6a1f…",
  "kind": "heatshrink-boot",
  "sourceType": "OverriddenLibrary",
  "origin": {
    "libraryId": "company.plb", "partId": "p-8842", "libraryRevision": "2026-03-11",
    "manufacturer": "TE Connectivity", "partNumber": "202A132-25-0",
    "libraryValues": { "cableEntryMin": "6.4" }        // what the maker says
  },
  "description": "Boot, 90°, shell 25",
  "length": 34.5, "innerDiameter": 12.7, "outerDiameter": 15.2, "breakoutOverlapLength": 6.0,
  "parameters": { "shellSize": "25", "connEntryMin": "12.7", "cableEntryMin": "4.8",
                  "shrinkRatio": "2.0", "adhesive": "1", "angle": "90 degree" },
  "symbol": { "sourceKind": "Stored", "generatorKey": null, "entities": [ /* … */ ] }
}
```

Not a reference alone. A drawing opened in five years must reproduce, and the library will have moved
on — parts get superseded, a supplier changes a dimension, someone tidies a description. The snapshot
is what the drawing *means*; the link is what lets it be checked against the library later.

That check is a deliberate command, never automatic:

```csharp
public sealed record LibraryDriftReport(
    Guid ModuleId, string PartNumber,
    IReadOnlyList<(string Key, string InDrawing, string? InLibrary)> Differences,
    bool PartStillExists);
```

`RelinkCommand` shows the drift and lets the user take the library's figures or keep the drawing's.
Silently refreshing geometry from a library on open would change a released drawing without anyone
approving it.

### 6.2 Saving a custom component

```csharp
public async Task<MechanicalLibraryItem> SaveAsync(MechanicalSpec spec, CancellationToken ct)
{
    // 1. A signature over kind + every stated parameter, so the same custom boot typed twice
    //    does not become two library items.
    var signature = MechanicalSignature.Of(spec);
    if (await FindBySignatureAsync(signature, ct) is { } existing) return existing;

    // 2. Mint an id nothing else has; leave internalPn blank — purchasing assigns those, and a
    //    made-up house number is worse than none.
    var item = new MechanicalLibraryItem
    {
        PartId       = NewPartId(spec.Kind),
        Kind         = spec.Kind,
        Description  = string.IsNullOrWhiteSpace(spec.Description)
                           ? MechanicalDescriber.Describe(spec)   // built from the data, as describePart() does
                           : spec.Description,
        Manufacturer = spec.Origin?.Manufacturer ?? "",           // "" — not "Custom"
        PartNumber   = spec.Origin?.PartNumber ?? "",
        Parameters   = spec.Parameters.ToImmutableDictionary(),
        Symbol       = spec.DrawingSymbol2D,
    };
    await WriteAsync(item, ct);
    return item;
}
```

`MechanicalDescriber.Describe` is the C# twin of `describePart()` in
[`PartsLibrary.cpp`](../src/harness/PartsLibrary.cpp) — a description written from what the part says
it *is*, with blank parameters left out, so the sentence is only ever as complete as the data behind
it. Sharing that behaviour matters: a custom part saved here should read like the catalogue parts
beside it.

An item saved this way is a full library item. `spec.SourceType` becomes `LibraryItem` immediately
after saving (see `PlaceAsync`) — the thing on the canvas is no longer custom, because the library it
refers to now contains it.

### 6.3 Where the C++ side fits

| C# | C++ (already built) |
| --- | --- |
| `MechanicalKind` | `PartKind` — same keys via `partKindKey()` |
| `ParameterFieldViewModel` rows | `partFields(PartKind)` — `Text/Number/Range/Bool/Choice` |
| `FitWindow` | a `Range` field's `"<key>Min"`/`"<key>Max"` pair |
| `MechanicalLibraryItem` | `LibraryPart` |
| `IMechanicalLibrary` (composite) | `PartsCatalog` — several `.plb` files, one being worked on |
| `DrawingSymbol2D` (Stored) | `LibraryPart.mechanical`, entities via `partDrawingEntities()` |
| `MechanicalDescriber.Describe` | `describePart()` |
| `MechanicalFinding.DataGap` | `VdGap` in `analysis/VoltageDrop.h`; same principle |

The honest recommendation: **do not port the parameter metadata into C#.** Have the C++ side export
`partFields()` and `allPartKinds()` as JSON over the bridge, and build `ParameterFieldViewModel` rows
from that. Two copies of "what parameters does a backshell have" will drift within a month, and the
drift shows up as a field the DRC checks and the picker cannot fill in.

---

## 7. Build order

1. `MechanicalSpec`, `FitWindow`, `SpecOrigin`, `MechanicalComponentBase` + `Backshell` and
   `HeatshrinkBoot`. No UI. Unit-test `ToSpec()`'s `SourceType` transitions: library → touched →
   `OverriddenLibrary`, with `LibraryValues` holding the maker's figure.
2. `PlbMechanicalLibrary` over the existing `.plb` files. Prove a round trip: read a boot, place it,
   save the drawing, reopen it, get the same spec.
3. `BundleFitRule` + `ShrinkFitRule` + the `DataGap` path. Test with a bundle diameter present, and
   again with it absent — the second must produce a gap, not a pass.
4. `MechanicalSpecDraftViewModel` and the picker, library tab only.
5. The custom tab (the same draft, no origin) and `Save this custom component to My Library`.
6. The inline inspector, reusing the picker's ViewModel.
7. `LibraryDriftReport` and `Relink`.

Steps 1–3 are worth having before any window exists: if the spec model and the rules are right, the
picker is a form over them, and if they are wrong no amount of UI will hide it.
