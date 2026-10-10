# Harness Design Platform: market scope and investor payback

Research and model date: 10 October 2026. Figures below distinguish owner inputs, public publisher estimates and illustrative modeling assumptions.

## Customer and product scope

Target companies include hardware startups, defense/aerospace suppliers, high-tech device companies, hardware OEMs, complex-system builders, project engineering firms and harness manufacturers. Eligibility depends on designing harnesses, electrical interfaces or cabinet wiring. This does not include every software-only startup.

Manufacturing/Test has been removed from the proposed ecosystem on slide 6. Electrical CAB wiring is interpreted as control-cabinet/control-panel wiring and marked Proposed. Planned functions include equipment/terminal definitions, wire numbers, from/to connections, ducts, route lengths and project reuse. Its price and delivery commitments are not defined. [Zuken E3.panel](https://www.zuken.com/en/product/e3series/cabinet-layout-and-routing/) supports the cabinet-design workflow category.

Current reusable parts, drawing templates, blocks and saved harness wizard definitions support local reuse. A managed corporate harness catalog, controlled variants and coordinated project revisions remain planned capabilities.

## Rechecked market

| Category | Publisher estimate | Source |
|---|---|---|
| Global harness design software | USD 245.71M in 2025 | [360iResearch, November 2025, via Research and Markets](https://www.researchandmarkets.com/reports/6090408/electrical-wiring-harness-design-software) |
| Global harness design software | USD 1,039M in 2025 | [LP Information, June 5, 2026, via MarketResearch.com](https://www.marketresearch.com/LP-Information-Inc-v4134/Global-Electrical-Wiring-Harness-Design-45255319/) |
| Broad ECAD ecosystem | USD 9.81B in 2025, USD 19.37B forecast for 2031 | [TechSci Research, January 2026](https://www.techsciresearch.com/report/electrical-computer-aided-design-ecad-market/25205.html) |

The harness reports already cover multiple end-user industries, including machinery and aerospace. The first explicitly includes aerospace/defense and railways. They do not describe sales exclusively to harness manufacturers. The two estimates differ substantially and their public summaries do not reconcile the methodologies. The displayed USD 0.25B–1.04B span represents these published estimates, not a confidence interval or consensus.

Broad ECAD includes PCB tools and software services, support, training and maintenance. It provides adjacent ecosystem context, not the app's current serviceable market. It overlaps the harness category and must not be added to it. Total hardware-industry revenue is not software TAM. No independently verified global count of all eligible startup, defense, device and complex-system companies was found.

Industry context: [Siemens aerospace/defense electrical systems](https://resources.sw.siemens.com/cs-CZ/e-book-aerospace-defense-cloud-based-electrical-systems-development/) and [Siemens wire harness design](https://www.siemens.com/en-us/products/designcenter/cad-software/electronic-electrical-cad/wire-harness-design/).

Manufacturer directories represent one target segment. [RevenueBase wire harness manufacturers](https://revenuebase.ai/companies/browse/wire-harness-manufacturers) lists 1,363 companies in its September 19, 2026 snapshot. Its [broader cable/harness category](https://revenuebase.ai/companies/browse/cable-and-harness-manufacturers) lists 6,132 in the September 16 snapshot. [WHMA](https://www.whma.org/about/history-whma) states approximately 250 members. These populations overlap and are not additive or confirmed software buyers.

## One seat per subscribing company

Prices come from the project owner. No corporate discount is assumed.

| Tier | USD per seat/month | Annual revenue from one seat |
|---|---:|---:|
| Free | 0 | 0 |
| Drawing | 70 | 840 |
| Engineering | 100 | 1,200 |
| Complete | 150 | 1,800 |

Annual subscription revenue = companies × seats/company × monthly seat price × 12.

At the Engineering tier:

| Paying companies | One seat/company ARR | Five seats/company ARR |
|---:|---:|---:|
| 100 | USD 120,000 | USD 600,000 |
| 500 | USD 600,000 | USD 3,000,000 |
| 1,000 | USD 1,200,000 | USD 6,000,000 |
| 3,000 | USD 3,600,000 | USD 18,000,000 |

Revenue is not profit or investor cash. At 3,000 total paid seats, revenue is USD 3.6M/year, requiring 3,000 one-seat companies or 600 five-seat companies.

## Owner's base funding plan

Five salaries × NIS 40,000/month × 24 months = NIS 4.8M.

Office rent over 24 months = NIS 1.2M.

Base requirement = NIS 6M. Annual salary/rent run rate = NIS 3M.

Employer costs, equipment, marketing, software/cloud services, legal/accounting, insurance and contingency are not fully budgeted by these figures. This is not a fully loaded operating forecast.

## Investor cash-distribution payback assumptions

| Input | Illustrative assumption |
|---|---|
| Initial investment | NIS 6M |
| Ownership | 20% |
| Subscription used in model | Engineering, USD 100/seat/month |
| Planning exchange rate | NIS 3.5/USD, not a current spot-rate assertion |
| Cash contribution after variable costs | 85% of subscription revenue |
| Annual salary/rent cost from Year 3 | NIS 3M, held constant |
| Reserve | 20% of positive operating cash for taxes/reinvestment |
| Investor distributions | Annual pro-rata cash payments, starting at end of Year 3 |
| Customer adoption | Stable seats from Year 3; no acquisition ramp modeled |

The reserve is not a statutory tax rate or a complete tax calculation. The model does not include inflation, additional hires, dilution, preferences, further financing or an agreed dividend policy. Distributions require actual available cash and appropriate approval. Seat scenarios are not sales forecasts.

Annual NIS revenue = paid seats × 100 × 12 × 3.5.

Operating cash = annual revenue × 85% − NIS 3M.

Annual investor cash = max(operating cash, 0) × 80% × 20%.

Payback measures nominal, undiscounted cash recovery; it is not a share redemption. With annual payments at the end of Years 3 onward:

Payback year = 2 + ceil(NIS 6M / annual investor cash).

| Stable paid seats | Annual NIS revenue | Annual investor cash | Full NIS 6M recovered |
|---:|---:|---:|---|
| 1,500 | 6,300,000 | 376,800 | End of Year 18 |
| 3,000 | 12,600,000 | 1,233,600 | End of Year 7 |
| 5,000 | 21,000,000 | 2,376,000 | End of Year 5 |

At 3,000 seats, four payments total NIS 4,934,400 by end of Year 6. Five payments total NIS 6,168,000 by end of Year 7.

Base operating cash break-even is ceil(3,000,000 / (100 × 12 × 3.5 × 85%)) = 841 paid Engineering seats. If every customer buys one seat, 841 paying companies are required for this modeled base break-even. This does not itself recover the investment or include every possible operating expense. Non-positive operating cash produces no investor payment in the model.

## Investor payback through a sale

Separate exit-only cases use the same NIS 6M investment and 20% ownership, without prior distributions:

| Hypothetical sale | Exit equity value | Investor gross proceeds | Gross ROI |
|---|---:|---:|---:|
| Year 3 | NIS 30M | NIS 6M | 0% |
| Year 5 | NIS 60M | NIS 12M | 100% |
| Year 7 | NIS 90M | NIS 18M | 200% |

Gross proceeds = equity sale value × ownership. Gross ROI = (proceeds − investment) / investment.

At 20% ownership, a qualifying NIS 30M equity sale is the gross investment-recovery threshold. Dates and values are hypothetical, not valuation forecasts or exit commitments. Dilution, preferences, debt adjustments, transaction costs and investor taxes are excluded. Do not combine this exit-only table with the distribution scenario without an explicit combined cash-flow model.

## Explanation of the removed customer ROI slide

Old slide 21 was an illustrative customer-capacity example: five engineers saving two hours/week for 48 weeks, valued at USD 60/hour, gives USD 28,800 in annual time value. Dividing the net value after USD 9,000 in assumed software/onboarding costs by that cost produced 220% customer ROI. The onboarding amount and time savings were assumptions, not owner-supplied commercial results. This did not calculate investor payback, did not necessarily generate cash savings, and has been removed from the investor deck.

## Product evidence and source images

Product screenshots are actual application captures with demonstration data. The app is in active development. Prototype screenshots do not establish production readiness, standards certification, customer traction or measured financial performance.

The two light harness backgrounds are generated conceptual illustrations, not customer products or app output. Their saved assets and original generation prompts are recorded in the prior Sources_and_ROI_Assumptions.md. The new presentation reuses those backgrounds.

Competitor descriptions use official sources: [Siemens Capital](https://www.siemens.com/en-gb/products/capital/offerings/wiring-harness-designer-essentials/), [Zuken E3.formboard](https://www.zuken.com/en/resource/datasheet-e3-series-formboard/), [EPLAN Harness proD](https://www.eplan.com/gb-en/products/eplan-harness-prod/), [SOLIDWORKS Electrical 3D](https://www.solidworks.com/product/solidworks-electrical-3d), [Cadonix](https://www.cadonix.com/home/), and [RapidHarness](https://rapidharness.com/features) with its [FAQ](https://rapidharness.com/support/faq) and [prices](https://rapidharness.com/pricing).
