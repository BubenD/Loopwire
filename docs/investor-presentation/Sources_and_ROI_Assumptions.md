# Harness Design Platform: light edition, sources and ROI assumptions

Research date: 10 October 2026. Product name is a working title.

## Market figures

| Measure | Published figure | Source and scope |
|---|---|---|
| Electrical wiring harness design software | $224.66 million in 2024, $245.71 million in 2025, $485.49 million forecast for 2032. Publisher states 10.11% CAGR. | [360iResearch report, November 2025, via Research and Markets](https://www.researchandmarkets.com/reports/6090408/electrical-wiring-harness-design-software). A publisher estimate for this specific software category, not all electrical CAD. |
| Wire harness manufacturers | 1,363 companies | [RevenueBase directory](https://revenuebase.ai/companies/browse/wire-harness-manufacturers), refreshed September 19, 2026. A directory matched to company descriptions, not a census of design businesses or buyers. |
| Cable and harness manufacturers | 6,132 companies across 31 countries | [RevenueBase broader directory](https://revenuebase.ai/companies/browse/cable-and-harness-manufacturers), refreshed September 16, 2026. Includes adjacent categories. Its reported matching method achieved 86% on-target results in a blind review. |
| WHMA membership | Approximately 250 members | [WHMA history](https://www.whma.org/about/history-whma). Membership covers a different population and does not establish the worldwide number of manufacturers. |
| Physical wire harness sales | $100.22 billion in 2025 | [The Business Research Company report via Research and Markets](https://www.researchandmarkets.com/reports/5866799/wire-harness-global-market-opportunities). Industrial context, separate from software revenue. |
| Physical wire harness sales, alternative estimate | $55.1 billion in 2025 | [Custom Market Insights](https://www.custommarketinsights.com/report/wire-harness-market/). Different reported scope and estimate. The deck does not combine the hardware estimates into a consensus. |

The company directory counts overlap. They are not additive and do not measure confirmed addressable customers. No reliable worldwide census of harness design-only companies was located. A measured SAM or SOM requires account qualification, seat counts and purchasing research.

## Competitor sources

- [Siemens Capital Harness Designer Essentials](https://www.siemens.com/en-gb/products/capital/offerings/wiring-harness-designer-essentials/)
- [Zuken E3.formboard](https://www.zuken.com/en/resource/datasheet-e3-series-formboard/) and [E3.series electrical design / DRC](https://www.zuken.com/en/solution/electrical-technologies/electrical-planning-documentation/)
- [EPLAN Harness proD](https://www.eplan.com/gb-en/products/eplan-harness-prod/)
- [SOLIDWORKS Electrical 3D](https://www.solidworks.com/product/solidworks-electrical-3d)
- [Cadonix Arcadia](https://www.cadonix.com/home/)
- [RapidHarness features](https://rapidharness.com/features), [FAQ](https://rapidharness.com/support/faq), and [pricing](https://rapidharness.com/pricing)

The comparison uses vendor documentation. It does not establish absent features or measured superiority. Local file control is a concrete deployment distinction relative to the cited RapidHarness cloud requirements and Arcadia cloud offering. Other vendors also offer on-premises tools. No comparative productivity or reliability benchmark has been completed.

RapidHarness prices at research date: individual Standard $219/month or $2,199/year, individual Professional $299/month or $2,999/year. Five-user Standard Team $6,599/year and Professional Team $8,999/year. These products have different feature scopes, so the deck avoids a feature-equivalent savings claim.

## Project owner's commercial plan

| Tier | USD per month | Annualized USD, without a discount |
|---|---:|---:|
| Free | 0 | 0 |
| Harness drawing | 70 | 840 |
| Schematic and mechanical harness wizards | 100 | 1,200 |
| 3D and system design | 150 | 1,800 |

Single-seat and corporate subscriptions are planned. The presentation assumes progressively broader capability bundles. Free-tier limits, detailed entitlements, corporate terms, annual discounts and licensing implementation remain to be defined.

Revenue scenarios are illustrative, not forecasts or existing revenue. They assume five paid seats per corporate customer and an average $100/month per seat. This gives $6,000 annual recurring revenue per customer: 50 customers = $300,000, 250 = $1,500,000, and 500 = $3,000,000. Scenarios exclude discounts, churn, sales/support costs, tax and foreign exchange.

## Staffing and funding plan

The project owner proposed five salaried positions: backend software engineer, promotion/sales, CEO (the project owner), full stack engineer and system software engineer. Responsibilities in the deck are proposals.

- Salaries: 5 × NIS 40,000/month × 24 months = **NIS 4,800,000**.
- Office rent: NIS 1,200,000 over 24 months = **NIS 50,000/month**.
- Stated base costs: **NIS 6,000,000 over 24 months**, or **NIS 250,000/month**.

It is not established whether salaries include employer costs. Equipment, marketing spend, cloud/software services, insurance, legal/accounting and contingency have no additional allowance in the supplied figures. The presentation treats NIS 6 million as a base funding requirement, not a fully loaded operating forecast. No agreed valuation, transaction terms or currency conversion is asserted. Investor ROI uses the separately labeled ownership scenario below.

## Product examples and maturity

Screenshots come from the current native application's widgets and renderer. A schematic demonstration uses two six-contact connectors and named signals. It passed the current schematic rules during capture. The mechanical example uses a local drawing fixture with a branched harness. The system example uses a controller module, sensor and actuator. Demonstration files intentionally contain incomplete or unsaved validation evidence to illustrate the issue workflow. The PDF search example uses a demonstration datasheet.

The app is in active development. Screenshots and implemented APIs do not establish production readiness, standards certification, electrical simulation accuracy or commercial customer traction. Manufacturing guidance, hardware test interfaces, controlled approvals, revision comparison and PLM/ERP integrations are proposed ecosystem work. Market-entry targets and investment milestones are proposals.

All substantive web sources also appear in the relevant PowerPoint speaker notes. The app's source, README, CLAUDE.md and project-planning document support the product capability descriptions.


## Primary customer focus

The project owner clarified that the software serves small and midsize companies that build systems containing harnesses, especially engineering project firms managing many projects. Harness manufacturers are an additional segment. Manufacturer directories describe only one part of this broader potential buyer population.

Current reuse tools include parts libraries, drawing templates, blocks and saved harness wizard designs. A managed corporate harness catalog, controlled variants, automatic project-wide change propagation and approvals remain proposed ecosystem work. The presentation describes common engineering practice rather than existing simultaneous collaboration.

Industry context: [Zuken](https://www.zuken.com/en/solution/electrical-technologies/manufacturing-and-service-documentation/) and [Siemens](https://www.siemens.com/en-us/products/designcenter/cad-software/electronic-electrical-cad/wire-harness-design/).

## Illustrative customer ROI

This is an economic capacity model, not measured customer results or guaranteed cash savings.

Assumptions: 5 engineering seats, USD 100 per seat/month, 48 working weeks/year and USD 60 per engineering hour. Annual subscriptions total USD 6,000. A one-time onboarding allowance of USD 3,000 gives a first-year cost of USD 9,000. The onboarding allowance is not announced product pricing. The hourly value is an assumption, not a wage benchmark or the startup's payroll.

Annual time value = engineers × weeks × hours saved per engineer per week × hourly value.

Net benefit = annual time value − first-year cost.

First-year ROI = net benefit / first-year cost.

| Hours saved / engineer / week | Annual time value | Net benefit | First-year ROI |
|---:|---:|---:|---:|
| 0.5 | USD 7,200 | −USD 1,800 | −20% |
| 1.0 | USD 14,400 | USD 5,400 | 60% |
| 2.0 | USD 28,800 | USD 19,800 | 220% |

Break-even is 9,000 / (5 × 48 × 60) = 0.625 hours, or 37.5 minutes per engineer/week. The slide rounds this to about 38 minutes. Reclaimed capacity may support more projects or better review quality. It does not automatically reduce payroll or produce the same amount in cash. Actual hours saved and adoption costs need pilot evidence.

## Illustrative investor ROI

The project owner selected illustrative 20% ownership for the proposed NIS 6 million investment. Ownership and investment terms are not agreed.

Gross investor proceeds = exit equity value × ownership.

Gross multiple = investor proceeds / initial investment.

Gross ROI = (investor proceeds − initial investment) / initial investment.

| Illustrative exit equity value | Investor proceeds at 20% | Gross multiple | Gross ROI |
|---:|---:|---:|---:|
| NIS 30 million | NIS 6 million | 1.0x | 0% |
| NIS 60 million | NIS 12 million | 2.0x | 100% |
| NIS 90 million | NIS 18 million | 3.0x | 200% |

Exit values are scenarios, not forecasts, revenue-based valuations or commitments. The model excludes dilution, further financing, liquidation preferences, taxes and transaction fees. It supplies no exit date or annualized return.

## Harness image provenance

Supporting images are AI-generated conceptual harness illustrations, produced with the built-in image generation tool on 10 October 2026. They are not photos of customer systems or actual app output. Product screenshots remain captures of the actual application.

Final saved assets:

- C:/Work/AI/Edraw_desktop/docs/investor-presentation/backgrounds/harness-hero-light.png
- C:/Work/AI/Edraw_desktop/docs/investor-presentation/backgrounds/harness-footer-light.png

Hero prompt:

> Use case: productivity-visual. Create one photorealistic editorial studio photograph-style illustration for the background of a professional investor presentation about industrial wire harness design software. Landscape 16:9, at least 1920 x 1080. Bright high-key lighting, pure warm white/light ivory background. An elegant real-world industrial multi-branch cable harness with black woven protective sleeving, heatshrink branch junctions, precisely formed smooth bends, realistic silver circular multi-contact connectors and a few neat red/blue/yellow wire fanouts. Place the whole harness predominantly in the RIGHTMOST 40 percent and along the LOWER RIGHT corner. The LEFT 60 percent must remain mostly empty clean white negative space for dark editable slide text. Cables should not cross the text area. Subtle natural contact shadows, meticulous engineering detail, restrained neutral tones, crisp premium product photography, no dark scene. No typography, no labels, no logos, no people, no watermark. This is conceptual supporting photography, not a product screenshot.

Secondary background prompt:

> Use case: productivity-visual. A photorealistic bright studio photograph-style illustration to serve as a clean investor presentation background. Landscape 16:9 at least 1920x1080. White to very pale gray matte background with high-key diffuse illumination. A meticulously arranged industrial electrical wire harness with a black braided trunk, a compact branch heatshrink, a small silver metal connector, plastic keyed plugs and neat colored signal-wire fanouts. Compose the objects ONLY in a narrow strip across the BOTTOM 20 percent and concentrated at the far RIGHT edge. The upper 80 percent and left-center remain clean white negative space. Most details should read as elegant product photography at the lower right, subtle shadows. Realistic materials and credible industrial harness assembly, no logos, no text, no decorative technology graphics, no dark scene, no people. Conceptual supporting image, not a real app screenshot or customer photograph.
