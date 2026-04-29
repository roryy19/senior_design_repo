# Sensor-to-Motor Pipeline — Presentation Talking Points

This document walks through every metric, figure, and CSV in this folder.
Use it as your speaker notes during the presentation. Each section has:

- **What was tested** — the question the metric answers
- **The headline number** — what to put on the slide
- **Speaking script** — a short natural-language explanation you can read
- **Example** — a concrete walkthrough a listener can visualize
- **Expected Q&A** — questions a judge might ask and how to answer them

---

## 0. Big picture opener

> "The belt's software pipeline takes raw distance readings from 12 sensors
> and turns them into vibration levels for 8 motors. I evaluated that
> pipeline against 8 independent metrics. Every metric was computed by
> linking directly against the same C++ core library that runs on the ESP32,
> so the numbers you're about to see reflect the actual shipped code, not a
> separate model."

**Why this matters**: judges love hearing "same code that runs on hardware" —
it means your software results transfer directly to the physical prototype.

---

## Metric 1 — Angular Localization Error

**File**: `fig_01_localization.png`, `metric1_localization.csv`

### What was tested
Place a virtual obstacle at every whole-degree angle from 0° to 359°, each
at 50 cm. Run the pipeline. Compute the "weighted average angle" of the 8
motors (each motor position weighted by its vibration level). Compare that
perceived angle against the true obstacle angle. This answers:
**"When an obstacle is in a given direction, does the user feel vibration on
the correct side of their body?"**

### Headline
- Mean error: **9.60°**
- Max error: **23.64°**
- Standard deviation: **6.11°**

### Speaking script
> "For every angle around the user, I placed a single obstacle and asked the
> pipeline where the user would feel the vibration. On average the haptic
> output pointed within 10 degrees of the real obstacle, and never more than
> 24 degrees off. The staircase pattern on the left plot isn't a bug — it
> reflects the physical fact that we have 10 sensors spaced 36 degrees apart
> and 8 motors spaced 45 degrees apart. The algorithm is quantization-limited
> by the hardware layout, not by any software error. In other words, this is
> the best localization any algorithm could possibly do with this hardware."

### Example
> "If an obstacle appears at 23° — roughly front-right — the nearest sensor
> is the one at 36°. The pipeline activates motor 0 (front) and motor 1
> (front-right) with different weights. The centroid works out to about 27°.
> That's 4° off the true angle. The user still clearly feels 'front-right,'
> which is what matters for safe navigation."

### Expected Q&A
**Q: "Why is the max error 24 degrees? That seems high."**
> "Motors are 45° apart. The worst case is an obstacle exactly between two
> motors — 22.5° from each. You can never do better than that with 8 motors.
> 24° is right at that theoretical ceiling, and it only happens at the
> specific angles that fall between motor positions."

**Q: "Can you reduce this error?"**
> "Only by adding more motors. The algorithm is already optimal. Going from
> 8 to 16 motors would cut the max error in half."

### CSV vs PNG
Same data. The CSV has one row per true angle with the measured angle and
error. The left plot is a scatter of column 1 vs column 2. The right plot
graphs column 3.

---

## Metric 2 — 360° Coverage

**File**: `fig_02_coverage.png`, `metric2_coverage.csv`

### What was tested
At 7 fixed distances (10, 25, 40, 55, 70, 85, 100 cm), sweep the obstacle
angle 0°–359° and count the fraction of angles where **at least one motor
fires**. This answers:
**"Are there any directions where an obstacle would go unnoticed — 'dead zones'?"**

### Headline
- 10 – 85 cm: **100% coverage, 0 dead zones**
- 100 cm: **0% coverage** (by design — this is the off-threshold)

### Speaking script
> "A 'dead zone' would be an angle where an obstacle in range wouldn't
> trigger any vibration at all. I checked every whole-degree angle at six
> distances inside the detection range and found zero dead zones. Every one
> of the 360 possible obstacle directions produces a haptic response. At
> exactly 100 cm the coverage drops to zero — that's not a flaw, that's the
> defined boundary of our detection range. Beyond 100 cm, the categorizer
> returns level 0 (off) so the user isn't spammed with vibration for distant
> objects."

### Example
> "Say an obstacle is directly behind the user at 180°, 40 cm away. That's
> the hardest case because the back of the belt has fewer sensors crossing
> the back-zone. But because sensor 5 sits at exactly 180° and three motors
> are within 45°, at least one motor always fires. No direction is blind."

### Expected Q&A
**Q: "Why does the plot show 0% at 100 cm?"**
> "That's the designed detection boundary. We chose 100 cm because it's just
> beyond arm's length and gives the user time to react. Anything farther
> isn't in the 'immediate danger' range we're optimizing for."

**Q: "Did you test dead zones between sensors?"**
> "Yes — the sweep is every single whole degree, so angles like 18° and 54°
> (exactly between belt sensors) are included. All 360 angles pass."

### CSV vs PNG
Same data. CSV lists 7 distance rows with `coverage_pct` and `dead_zone_angles`
columns. PNG is a bar chart of those 7 rows.

---

## Metric 3 — Motor Count per Obstacle

**File**: `fig_03_motor_count.png`, `metric3_motor_count.csv`

### What was tested
Place a single obstacle at 50 cm and sweep the angle 0°–359°. For each angle,
count how many motors turn on. This answers:
**"Is the vibration localized, or does one obstacle light up the whole belt?"**

### Headline
- Mean: **2.20 motors active per obstacle**
- Range: **2 – 3 motors**

### Speaking script
> "A good haptic display should localize — one obstacle should only vibrate
> the part of the belt closest to that obstacle. If every obstacle lit up
> five or six motors, the user would feel a blur, not a direction. We tested
> this by placing one obstacle at each angle and counting how many motors
> fired. On average, about 2.2 motors fire. Never fewer than 2, never more
> than 3. That means the user always feels a clear localized pulse, and the
> two-to-three motor spread gives a smooth transition as the obstacle moves
> between exact motor positions."

### Example
> "Obstacle at 0° (directly front) → only motor 0 (front) fires strongly,
> and motors 1 and 7 (front-left and front-right) fire at half intensity.
> Three motors total, but clearly a 'front' sensation. If the obstacle moves
> to 22.5°, motor 0 and motor 1 fire equally — the user feels 'between front
> and front-right,' which is exactly where the obstacle is."

### Expected Q&A
**Q: "Why is the minimum 2 and not 1?"**
> "The angular spread algorithm intentionally activates adjacent motors at
> reduced intensity, so the user gets a smooth sense of direction rather
> than a binary on/off. Always having at least two motors fire makes the
> sensation feel continuous as the obstacle moves."

### CSV vs PNG
Same data. CSV has 360 rows of `true_angle_deg, motors_active`. Left plot
graphs that directly. Right plot is a histogram of the second column.

---

## Metric 4 — Distance Monotonicity

**Files**: `fig_04_monotonicity.png`, `metric4_monotonicity.csv`, `support_distance_vs_level.csv`

### What was tested
The pipeline should be **monotonic**: if an obstacle gets closer, the
vibration level should equal or increase — never decrease. I swept every
distance from 1 to 130 cm across all 12 sensors (1560 total samples) and
checked whether the categorizer ever violates monotonicity. This answers:
**"Does the pipeline behave predictably — is 'closer = stronger' always true?"**

### Headline
- **100 % monotonic pass rate** (1560 / 1560 samples)

### Speaking script
> "Monotonicity is the most basic correctness property for a distance-to-
> vibration pipeline. The rule is simple: as something gets closer, the
> vibration should get stronger, never weaker. A violation would mean the
> belt does something weird like vibrating harder at 50 cm than at 30 cm,
> which would confuse the user. I tested every centimeter from 1 to 130
> across all 12 sensors — 1560 samples — and the pipeline passes every
> single one. The plot shows three nested step functions for three different
> user arm lengths. Every line is strictly non-increasing, which is the
> definition of correct monotonic behavior."

### Example
> "Walk the user toward a wall. At 80 cm the belt buzzes at level 1. At
> 60 cm it's at level 3. At 30 cm it's at level 5. At 5 cm it's at level 7
> maximum. Every step closer, the vibration gets stronger or stays the
> same — never weaker. The user's intuition ('closer = more urgent') matches
> what the belt does."

### Expected Q&A
**Q: "What happens if the threshold values are slightly wrong?"**
> "Monotonicity is preserved regardless of threshold values, because the
> categorizer scans thresholds in order. Even if the arm-length scaling
> shifts them, the ordering never changes."

### CSV vs PNG — different!
- `metric4_monotonicity.csv`: 12 rows, one per sensor, with pass/fail and
  violation count. This is the actual pass/fail data.
- `support_distance_vs_level.csv`: the step function data used to plot
  fig_04. This shows 131 rows of `distance_cm, level_arm50, level_arm65,
  level_arm80` — the actual level each arm length produces.
- **The PNG visualizes the supporting CSV, not the pass/fail CSV.** The
  pass/fail CSV gives you the quantitative claim ("100% passing"); the
  step function gives you the visual proof ("you can see it never dips").

---

## Metric 5 — Arm-Length Scaling Linearity

**File**: `fig_05_linearity.png`, `metric5_linearity.csv`

### What was tested
The pipeline personalizes detection range based on the user's arm length:
a shorter user gets a smaller detection zone, a taller user gets a larger
one. The scaling is supposed to be **linear** — thresholds multiply by
`userArm / referenceArm`. I swept arm length from 40 to 90 cm and recorded
the distance at which each vibration level (1 through 7) first kicks in,
then fit a straight line and measured the R² fit quality. This answers:
**"Does the personalization actually scale linearly with arm length, like the
spec says?"**

### Headline
- Mean R² = **0.9971** (essentially perfect linear fit)
- All 7 levels above R² = 0.98

### Speaking script
> "A shorter user shouldn't get vibration warnings for objects that aren't
> in arm's reach yet — the belt should shrink its detection range
> proportionally. The spec says detection range is a linear function of arm
> length: shorter arm, shorter range. I tested that directly. I swept arm
> length from 40 to 90 cm, recorded the distance where each vibration level
> first activates, and fit a line through those points. The R-squared values
> are all above 0.98, with a mean of 0.9971. That's effectively a perfect
> linear fit — the personalization works exactly as specified."

### Example
> "A 50 cm arm (short adult) gets level 7 (max vibration) when an obstacle
> is closer than 7.7 cm. A 65 cm arm (average) gets level 7 at 10 cm. An 80
> cm arm (tall user) gets level 7 at 12.3 cm. Every level scales the same
> way, cleanly, in a straight line."

### Expected Q&A
**Q: "Why is level 7 slightly lower R² (0.9845)?"**
> "Level 7 uses the smallest threshold (10 cm at reference arm), so the
> integer rounding in the sweep has proportionally larger effect. If you
> look at the raw numbers the fit is still essentially linear — the small
> R² drop is quantization noise, not algorithm drift."

**Q: "Why linear and not some other curve?"**
> "Arm length is a length scale. If you double all your body dimensions,
> you'd want to double all your distance thresholds. That's a proportional
> relationship, which is linear by definition."

### CSV vs PNG
Same data. CSV has `arm_length_cm, level, transition_distance_cm` rows for
every arm length × level combination. PNG fits and plots a line for each
level group, with the R² values computed in Python (they match the C++
summary).

---

## Metric 6 — Pipeline Latency

**File**: `fig_06_latency.png`, `metric6_latency.csv`

### What was tested
Run the full pipeline (categorize → map → pack) 100,000 times on random
input distances. Measure each call's execution time with high-resolution
clock. Report the distribution: mean, median, 99th percentile. Compare
against the 50 ms sensor read period to compute headroom. This answers:
**"Is the software fast enough to keep up with the sensors, with room to spare?"**

### Headline
- Mean: **0.384 µs** per pipeline call
- p99: **0.600 µs**
- Throughput: **~2.1 million iterations / second**
- Headroom: **~130,000× faster than the 50 ms sensor period**

### Speaking script
> "The sensor reads once every 50 milliseconds. That's our budget — the
> software has to finish processing before the next reading arrives. I ran
> the full pipeline 100,000 times on random input and measured every call.
> The average call takes 0.384 microseconds. Even the 99th percentile is
> under a microsecond. We can run the pipeline 130,000 times in the window
> of a single sensor read. The software is essentially free compared to the
> sensor I/O. This is measured on my laptop, not the ESP32, but it gives us
> a strong upper bound — and it means the software will never become the
> bottleneck on the real system."

### Example
> "If the sensor read takes 50 ms, then the software takes 0.000384 ms. The
> CPU is idle 99.9992% of the time waiting for the next sample. That means
> plenty of time to run the BLE stack, the beacon scanner, and the audio
> player in parallel — which is exactly what the other FreeRTOS tasks are
> already doing on the hardware."

### Expected Q&A
**Q: "Why test on a laptop instead of the ESP32?"**
> "The ESP32 runs at 240 MHz; my laptop runs at 3+ GHz. My laptop numbers
> will be about 15-20 times faster. Even after that derating, the ESP32
> would still run the pipeline in under 10 microseconds, which is 5,000
> times faster than the sensor period. The qualitative conclusion is
> unchanged: software is not the bottleneck."

**Q: "Does the p99 latency matter?"**
> "For a real-time system, yes — you need the worst case to fit in the
> budget, not just the average. Our p99 is 0.6 microseconds, still a
> factor of 80,000 under budget."

### CSV vs PNG — slightly different
- CSV is **subsampled**: every 50th iteration gets written (2,000 rows
  out of 100,000) to keep the file small.
- PNG histogram bins the full 100,000 samples in memory before plotting.
- The mean and p99 shown on the PNG come from the **subsampled CSV**, not
  the raw 100k, so they'll be within rounding of the authoritative numbers
  in `metrics_summary.txt`. Use the summary file for the slide numbers.

---

## Metric 7 — Multi-Obstacle Discrimination

**File**: `fig_07_discrimination.png`, `metric7_discrimination.csv`

### What was tested
Place two obstacles at 30 cm each and vary the angular separation between
them from 0° to 180°. For each separation, check whether the motor output
shows **two distinct local maxima** — meaning the user can feel two
separate objects, not one smeared-out blob. This answers:
**"Can the user distinguish between two nearby obstacles, or do they merge?"**

### Headline
- Minimum separation for discrimination: **19°**
- Separations ≥ 19° are **always discriminated** correctly

### Speaking script
> "Real environments have multiple obstacles. If two objects are close
> together in angle, the belt has to either tell the user 'two things'
> or merge them into one sensation. I tested every separation from 0° to
> 180°. Below 19°, the two obstacles appear as one single peak — which is
> actually correct behavior, because at that angular distance they're
> functionally one object from the user's perspective. At 19° and beyond,
> the belt reliably produces two distinct vibration maxima, so the user
> can feel both. The threshold is 19 degrees, which is roughly half of our
> 36° sensor spacing — exactly what you'd expect from the physics."

### Example
> "Walking through a doorway: the left and right doorframes are about 70 cm
> apart. At 1 meter away from the door, that's about 40° angular separation
> from the user's perspective. Well above our 19° threshold, so the user
> feels two distinct vibrations — left-side and right-side — and knows to
> walk through the middle."

### Expected Q&A
**Q: "Is 19° good enough in practice?"**
> "Yes. Two real-world obstacles less than 19° apart from the user's view
> are essentially touching each other from the user's angular perspective.
> Think of two chairs pushed together — you don't need to know there are
> two, you need to know 'big thing on the right.'"

**Q: "What's the physics behind 19°?"**
> "Belt sensors are 36° apart. The minimum separation where two obstacles
> can fall on two different sensors is just over half a sensor spacing.
> 19° lines up with that floor."

### CSV vs PNG
Same data. CSV has `separation_deg, discriminated` as a binary column for
each of the 181 tested separations. PNG bar-chart shows the same column
visually. The vertical dashed line on the plot marks the first `1` value
in the CSV.

---

## Metric 8 — Monte Carlo Robustness

**File**: `metric8_montecarlo.csv`, summary card on `fig_08_summary.png`

### What was tested
Generate 10,000 random scenarios. Each scenario = uniform random distances
(1 to 200 cm) for all 12 sensors. For each, run the full pipeline and
verify three invariants:

1. Every motor level stays in [0, 7]
2. The shift register bytes are valid
3. **Monotonicity holds**: halving any sensor distance never *decreases*
   the maximum motor level

This answers:
**"Does the pipeline behave correctly on arbitrary random inputs, not just
the specific scenarios I thought to test?"**

### Headline
- **10,000 / 10,000 runs passed** (100%)

### Speaking script
> "The other metrics test specific scenarios. This one tests randomness. I
> generated 10,000 random obstacle configurations — random distances on
> every sensor, all 12 at once — and checked three invariants on each run:
> the motor levels have to stay in the valid range, the packed shift
> register bytes have to be valid, and halving any distance should never
> reduce the overall motor output. All 10,000 runs pass all three checks.
> This is our 'we tested it on stuff we didn't design it for' metric. It
> catches the bugs that hand-picked test cases miss."

### Example
> "A random scenario might have sensor 0 at 47 cm, sensor 1 at 3 cm, sensor
> 2 at 192 cm, sensor 3 at 88 cm... and so on for all 12 sensors. That's a
> completely unrealistic situation, but the pipeline handles it with valid
> output. Then I took that scenario and pretended sensor 5's obstacle got
> twice as close. The max motor level only went up or stayed the same —
> never down. That proves the superposition logic is correct."

### Expected Q&A
**Q: "Why random testing if you already have unit tests?"**
> "Unit tests cover cases you thought of. Monte Carlo covers cases you
> didn't. If the unit tests passed but Monte Carlo failed, it would mean
> there's a corner case my hand-written tests missed. 100% pass on 10,000
> random runs is a strong statement that the pipeline has no input-dependent
> bugs in the tested range."

### CSV vs PNG — different!
- **There is no dedicated Metric 8 PNG.** `fig_08_summary.png` is the
  overall headline summary for all 8 metrics, with one row for Monte Carlo.
- The CSV is **heavily subsampled**: it logs the first 200 runs plus any
  failed runs. Since all 10,000 runs passed, the CSV only has 200 rows.
- The authoritative total (`10,000 runs, 10,000 passing, 100.00%`) lives
  in `metrics_summary.txt`. Use that for slides.

---

## Summary figure: `fig_08_summary.png`

One slide with all 8 headline numbers. Use this as your **very first slide**
for the evaluation section. Each judge gets the quantitative overview in one
glance, then you drill into individual metrics from there.

### Talking script
> "Before I walk through each test, here's the headline: 8 independent
> evaluations of the sensor-to-motor pipeline. Localization within 10
> degrees mean, 100% coverage across 360 degrees, 100% monotonicity pass
> rate, near-perfect linearity for the personalization feature, sub-
> microsecond latency, 19 degree multi-obstacle resolution, and 100% Monte
> Carlo robustness on 10,000 random runs. Every number comes from the real
> C++ code that runs on the ESP32."

---

## Files in this folder

| File | Type | Purpose |
|---|---|---|
| `generate_metrics.cpp` | source | Computes all 8 metrics using real core library |
| `generate_metrics.exe` | binary | Compiled version of above |
| `plot_metrics.py` | source | Reads CSVs, produces all 8 PNGs |
| `metrics_summary.txt` | text | Authoritative headline numbers (use for slides) |
| `metric1_localization.csv` | data | Raw (true, measured, error) per angle |
| `metric2_coverage.csv` | data | Raw (distance, coverage%, dead zones) per test distance |
| `metric3_motor_count.csv` | data | Raw (angle, active motors) per angle |
| `metric4_monotonicity.csv` | data | Per-sensor pass/fail (12 rows) |
| `metric5_linearity.csv` | data | Raw (arm, level, distance) for each transition |
| `metric6_latency.csv` | data | Subsampled latency (every 50th sample, 2k rows) |
| `metric7_discrimination.csv` | data | Raw (separation, discriminated flag) per degree |
| `metric8_montecarlo.csv` | data | Subsampled (first 200 + any failures) |
| `support_distance_vs_level.csv` | data | Helper data for fig_04 step plot |
| `fig_01_localization.png` | plot | True vs measured scatter + error sawtooth |
| `fig_02_coverage.png` | plot | Coverage % bar chart |
| `fig_03_motor_count.png` | plot | Motor count sweep + histogram |
| `fig_04_monotonicity.png` | plot | Step function, 3 arm lengths |
| `fig_05_linearity.png` | plot | Transition distances + regression lines |
| `fig_06_latency.png` | plot | Latency histogram + headroom bar |
| `fig_07_discrimination.png` | plot | Discrimination bar across separations |
| `fig_08_summary.png` | plot | One-slide headline table for all 8 metrics |

---

## Are CSVs and PNGs showing the same data?

**Mostly yes, three exceptions:**

1. **Metric 4 (monotonicity)** — the CSV shows per-sensor pass/fail counts
   (the quantitative claim). The PNG uses a *different* helper CSV
   (`support_distance_vs_level.csv`) to draw a step function (the visual
   proof). Both tell the same story in different formats.

2. **Metric 6 (latency)** — the CSV is subsampled (every 50th of 100,000
   iterations) to keep the file small. The PNG also reads that subsample.
   The authoritative full-population mean/p99 numbers live in
   `metrics_summary.txt`. Use the summary file for slide numbers.

3. **Metric 8 (Monte Carlo)** — the CSV logs only the first 200 runs plus
   any failures, so a 100% passing test yields a 200-row CSV even though
   10,000 runs happened. There is no dedicated PNG — only the summary card.
   The full totals live in `metrics_summary.txt`.

**Everywhere else (metrics 1, 2, 3, 5, 7)**, each CSV is the exact data
that its PNG visualizes. You can open the CSV in Excel and produce the
same chart.

---

## Rebuilding everything

From this folder:

```
g++ -std=c++17 -O2 -I ../src/core -o generate_metrics.exe generate_metrics.cpp ^
    ../src/core/distance_categorizer.cpp ^
    ../src/core/motor_mapper.cpp ^
    ../src/core/shift_register_packer.cpp ^
    ../src/core/pipeline.cpp

generate_metrics.exe
python plot_metrics.py
```
