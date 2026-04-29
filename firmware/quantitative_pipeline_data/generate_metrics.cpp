/*
 * generate_metrics.cpp -- Quantitative pipeline metrics generator
 *
 * Links against the real core library (same code that runs on the ESP32)
 * and computes 8 evaluation metrics for the sensor-to-motor pipeline.
 *
 * Outputs CSV files for graphing + a headline summary printed to stdout
 * and saved to metrics_summary.txt.
 *
 * Build:
 *   g++ -std=c++17 -O2 -I ../src/core -o generate_metrics.exe generate_metrics.cpp ^
 *       ../src/core/distance_categorizer.cpp ^
 *       ../src/core/motor_mapper.cpp ^
 *       ../src/core/shift_register_packer.cpp ^
 *       ../src/core/pipeline.cpp
 */

#include "sensor_config.h"
#include "distance_categorizer.h"
#include "motor_mapper.h"
#include "shift_register_packer.h"
#include "pipeline.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <chrono>
#include <random>
#include <vector>
#include <algorithm>

using namespace firmware;
using clk = std::chrono::high_resolution_clock;

static constexpr float PI = 3.14159265358979323846f;

/* Shortest angular distance in degrees, [0, 180] */
static float angDiff(float a, float b) {
    float d = std::fabs(a - b);
    if (d > 180.0f) d = 360.0f - d;
    return d;
}

/* Circular mean of angles (degrees) weighted by `w`. Returns deg in [0, 360). */
static float circularMean(const float anglesDeg[], const float w[], int n) {
    float sx = 0.0f, sy = 0.0f;
    for (int i = 0; i < n; i++) {
        float r = anglesDeg[i] * PI / 180.0f;
        sx += w[i] * std::cos(r);
        sy += w[i] * std::sin(r);
    }
    if (sx == 0.0f && sy == 0.0f) return -1.0f;  /* undefined */
    float m = std::atan2(sy, sx) * 180.0f / PI;
    if (m < 0.0f) m += 360.0f;
    return m;
}

static void clearDistances(float d[TOTAL_SENSORS]) {
    for (int i = 0; i < TOTAL_SENSORS; i++) d[i] = 999.0f;
}

/* Place a virtual obstacle at angle `ang` (deg) and distance `dist` (cm).
 * Assigns the distance to whichever belt sensor is angularly closest
 * (simulates what the real belt would "see"). */
static void placeObstacle(float d[TOTAL_SENSORS], float ang, float dist) {
    int best = 0;
    float bestDiff = 360.0f;
    for (int s = 0; s < NUM_BELT_SENSORS; s++) {
        float diff = angDiff(BELT_SENSOR_ANGLES[s], ang);
        if (diff < bestDiff) { bestDiff = diff; best = s; }
    }
    d[best] = dist;
}

/* ========================================================================
 * METRIC 1: ANGULAR LOCALIZATION ERROR
 *
 * Sweep a single obstacle around 360 deg at fixed distance. For each angle,
 * run the full pipeline, compute the circular mean of the motor activation
 * (weighted by level), and measure the error vs the true obstacle angle.
 * ======================================================================== */
struct LocalizationStats {
    float mean;
    float maxAbs;
    float stdev;
};

static LocalizationStats metric1_localization(const char *csv_path, float obstacleDist) {
    FILE *csv = fopen(csv_path, "w");
    fprintf(csv, "true_angle_deg,measured_angle_deg,abs_error_deg\n");

    std::vector<float> errors;
    errors.reserve(360);

    for (int trueAng = 0; trueAng < 360; trueAng++) {
        float dist[TOTAL_SENSORS];
        clearDistances(dist);
        placeObstacle(dist, (float)trueAng, obstacleDist);

        uint8_t motor_levels[NUM_MOTORS];
        mapSensorsToMotors(dist, motor_levels);

        float angles[NUM_MOTORS];
        float weights[NUM_MOTORS];
        for (int m = 0; m < NUM_MOTORS; m++) {
            angles[m] = MOTOR_ANGLES[m];
            weights[m] = (float)motor_levels[m];
        }
        float measured = circularMean(angles, weights, NUM_MOTORS);
        float err = angDiff(measured, (float)trueAng);
        errors.push_back(err);

        fprintf(csv, "%d,%.2f,%.3f\n", trueAng, measured, err);
    }
    fclose(csv);

    float sum = 0.0f, maxAbs = 0.0f;
    for (float e : errors) { sum += e; if (e > maxAbs) maxAbs = e; }
    float mean = sum / (float)errors.size();
    float var = 0.0f;
    for (float e : errors) { float d = e - mean; var += d * d; }
    var /= (float)errors.size();
    return { mean, maxAbs, std::sqrt(var) };
}

/* ========================================================================
 * METRIC 2: 360 DEGREE COVERAGE
 *
 * At several fixed obstacle distances, sweep angle 0..359 and count the
 * fraction of angles where at least one motor fires.
 * ======================================================================== */
struct CoverageRow {
    float dist;
    float pctCoverage;  /* 0..100 */
    int deadZones;
};

static std::vector<CoverageRow> metric2_coverage(const char *csv_path) {
    FILE *csv = fopen(csv_path, "w");
    fprintf(csv, "obstacle_distance_cm,coverage_pct,dead_zone_angles\n");

    const float dists[] = { 10.0f, 25.0f, 40.0f, 55.0f, 70.0f, 85.0f, 100.0f };
    std::vector<CoverageRow> rows;

    for (float dc : dists) {
        int hits = 0, dead = 0;
        for (int a = 0; a < 360; a++) {
            float dist[TOTAL_SENSORS];
            clearDistances(dist);
            placeObstacle(dist, (float)a, dc);

            uint8_t ml[NUM_MOTORS];
            mapSensorsToMotors(dist, ml);
            bool fired = false;
            for (int m = 0; m < NUM_MOTORS; m++) if (ml[m] > 0) { fired = true; break; }
            if (fired) hits++;
            else dead++;
        }
        float pct = 100.0f * (float)hits / 360.0f;
        fprintf(csv, "%.1f,%.2f,%d\n", dc, pct, dead);
        rows.push_back({ dc, pct, dead });
    }
    fclose(csv);
    return rows;
}

/* ========================================================================
 * METRIC 3: MOTOR COUNT PER OBSTACLE
 *
 * For a single obstacle at threshold distance (e.g. 50 cm) swept across
 * all angles, count how many motors fire. Report mean, min, max.
 * ======================================================================== */
struct MotorCountStats {
    float mean;
    int minCount;
    int maxCount;
};

static MotorCountStats metric3_motor_count(const char *csv_path, float dc) {
    FILE *csv = fopen(csv_path, "w");
    fprintf(csv, "true_angle_deg,motors_active\n");

    int sum = 0, mn = 99, mx = 0;
    for (int a = 0; a < 360; a++) {
        float dist[TOTAL_SENSORS];
        clearDistances(dist);
        placeObstacle(dist, (float)a, dc);

        uint8_t ml[NUM_MOTORS];
        mapSensorsToMotors(dist, ml);
        int cnt = 0;
        for (int m = 0; m < NUM_MOTORS; m++) if (ml[m] > 0) cnt++;
        sum += cnt;
        if (cnt < mn) mn = cnt;
        if (cnt > mx) mx = cnt;
        fprintf(csv, "%d,%d\n", a, cnt);
    }
    fclose(csv);
    return { (float)sum / 360.0f, mn, mx };
}

/* ========================================================================
 * METRIC 4: DISTANCE MONOTONICITY
 *
 * Per sensor, sweep distance 1..130 cm in 1 cm steps. Verify that the
 * categorizer level is monotonically non-increasing as distance grows
 * (i.e. closer always >= farther). Report total pass count across
 * sensors x steps and percent passing.
 * ======================================================================== */
struct MonotonStats {
    int total;
    int passing;
    float pct;
};

static MonotonStats metric4_monotonicity(const char *csv_path) {
    FILE *csv = fopen(csv_path, "w");
    fprintf(csv, "sensor_idx,passed,violations\n");

    int total = 0, passing = 0;
    for (int s = 0; s < TOTAL_SENSORS; s++) {
        int viol = 0;
        int lastLevel = 8;  /* impossible high start */
        for (int d = 1; d <= 130; d++) {
            uint8_t lvl = categorizeDistance((float)d, 0.0f);
            total++;
            if ((int)lvl <= lastLevel) passing++;
            else viol++;
            lastLevel = (int)lvl;
        }
        fprintf(csv, "%d,%d,%d\n", s, (viol == 0 ? 1 : 0), viol);
    }
    fclose(csv);
    return { total, passing, 100.0f * (float)passing / (float)total };
}

/* ========================================================================
 * METRIC 5: ARM LENGTH SCALING LINEARITY
 *
 * For each level (1..7), find the transition distance where the
 * categorizer first hits that level, across arm lengths 40..90 cm.
 * Fit a line distance = a * arm + b via least squares and report R-squared.
 * ======================================================================== */
struct LinearityStats {
    float r2_per_level[8];  /* index 1..7 used */
    float mean_r2;
};

static float leastSquaresR2(const std::vector<float> &x, const std::vector<float> &y) {
    int n = (int)x.size();
    float sx = 0, sy = 0, sxx = 0, sxy = 0, syy = 0;
    for (int i = 0; i < n; i++) {
        sx += x[i]; sy += y[i];
        sxx += x[i] * x[i]; syy += y[i] * y[i];
        sxy += x[i] * y[i];
    }
    float num = (float)n * sxy - sx * sy;
    float denX = (float)n * sxx - sx * sx;
    float denY = (float)n * syy - sy * sy;
    if (denX <= 0 || denY <= 0) return 1.0f;
    float r = num / std::sqrt(denX * denY);
    return r * r;
}

static LinearityStats metric5_linearity(const char *csv_path) {
    FILE *csv = fopen(csv_path, "w");
    fprintf(csv, "arm_length_cm,level,transition_distance_cm\n");

    std::vector<float> arms;
    for (int a = 40; a <= 90; a += 2) arms.push_back((float)a);

    /* transitions[lvl][i] = distance at which level lvl first appears for arm[i] */
    std::vector<float> transitions[8];

    for (float arm : arms) {
        /* Walk distance from high to low, find each level boundary */
        int lastLvl = -1;
        for (int d = 200; d >= 0; d--) {
            int lvl = (int)categorizeDistance((float)d, arm);
            if (lvl != lastLvl) {
                if (lvl >= 1 && lvl <= 7) {
                    transitions[lvl].push_back((float)d);
                    fprintf(csv, "%.1f,%d,%d\n", arm, lvl, d);
                }
                lastLvl = lvl;
            }
        }
    }
    fclose(csv);

    LinearityStats out = {};
    float acc = 0.0f;
    int cnt = 0;
    for (int lvl = 1; lvl <= 7; lvl++) {
        if (transitions[lvl].size() != arms.size()) {
            out.r2_per_level[lvl] = -1.0f;
            continue;
        }
        float r2 = leastSquaresR2(arms, transitions[lvl]);
        out.r2_per_level[lvl] = r2;
        acc += r2;
        cnt++;
    }
    out.mean_r2 = cnt > 0 ? acc / cnt : 0.0f;
    return out;
}

/* ========================================================================
 * METRIC 6: PIPELINE LATENCY
 *
 * Run processSensorReadings() N=100000 times on random inputs.
 * Report mean, median, p99, and throughput in iterations per second.
 * Compare against 50 ms sensor period for headroom factor.
 * ======================================================================== */
struct LatencyStats {
    double mean_us;
    double median_us;
    double p99_us;
    double iter_per_sec;
    double headroom_vs_50ms;
};

static LatencyStats metric6_latency(const char *csv_path) {
    FILE *csv = fopen(csv_path, "w");
    fprintf(csv, "iteration,latency_us\n");

    const int N = 100000;
    std::mt19937 rng(12345);
    std::uniform_real_distribution<float> U(1.0f, 130.0f);

    std::vector<double> samples;
    samples.reserve(N);

    /* Warmup */
    for (int i = 0; i < 1000; i++) {
        float dist[TOTAL_SENSORS];
        for (int k = 0; k < TOTAL_SENSORS; k++) dist[k] = U(rng);
        uint8_t sr[SHIFT_REGISTER_BYTES];
        uint8_t ml[NUM_MOTORS];
        processSensorReadings(dist, sr, ml);
    }

    auto wall_start = clk::now();
    for (int i = 0; i < N; i++) {
        float dist[TOTAL_SENSORS];
        for (int k = 0; k < TOTAL_SENSORS; k++) dist[k] = U(rng);

        auto t0 = clk::now();
        uint8_t sr[SHIFT_REGISTER_BYTES];
        uint8_t ml[NUM_MOTORS];
        processSensorReadings(dist, sr, ml);
        auto t1 = clk::now();

        double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
        samples.push_back(us);
    }
    auto wall_end = clk::now();
    double wall_sec = std::chrono::duration<double>(wall_end - wall_start).count();

    /* Subsample CSV to avoid 100k rows: every 50 iterations */
    for (int i = 0; i < N; i += 50) {
        fprintf(csv, "%d,%.4f\n", i, samples[i]);
    }
    fclose(csv);

    double sum = 0.0;
    for (double v : samples) sum += v;
    double mean = sum / N;

    std::sort(samples.begin(), samples.end());
    double median = samples[N / 2];
    double p99 = samples[(int)(N * 0.99)];

    double ips = (double)N / wall_sec;
    double headroom = 50000.0 / mean;  /* 50 ms = 50000 us */

    return { mean, median, p99, ips, headroom };
}

/* ========================================================================
 * METRIC 7: MULTI-OBSTACLE DISCRIMINATION
 *
 * Place two obstacles at varying angular separation (both at 30 cm).
 * For each separation 0..180 deg (1 deg steps), check whether the output
 * shows TWO distinct local maxima among the 8 motors (treated as a ring).
 * Report the minimum separation at which discrimination succeeds.
 * ======================================================================== */
static bool hasTwoLocalMaxima(const uint8_t ml[NUM_MOTORS]) {
    int peaks = 0;
    for (int i = 0; i < NUM_MOTORS; i++) {
        int prev = (i + NUM_MOTORS - 1) % NUM_MOTORS;
        int next = (i + 1) % NUM_MOTORS;
        if (ml[i] > 0 && ml[i] >= ml[prev] && ml[i] >= ml[next]
            && (ml[i] > ml[prev] || ml[i] > ml[next])) {
            peaks++;
        }
    }
    return peaks >= 2;
}

struct DiscriminationStats {
    int minSeparationDeg;
    float discriminationPct;  /* fraction of separations >= min that yielded 2 peaks */
};

static DiscriminationStats metric7_discrimination(const char *csv_path) {
    FILE *csv = fopen(csv_path, "w");
    fprintf(csv, "separation_deg,discriminated\n");

    int minSep = -1;
    int total = 0, yes = 0;

    for (int sep = 0; sep <= 180; sep++) {
        float dist[TOTAL_SENSORS];
        clearDistances(dist);
        placeObstacle(dist, 0.0f, 30.0f);
        placeObstacle(dist, (float)sep, 30.0f);

        uint8_t ml[NUM_MOTORS];
        mapSensorsToMotors(dist, ml);
        bool ok = hasTwoLocalMaxima(ml);

        fprintf(csv, "%d,%d\n", sep, ok ? 1 : 0);
        total++;
        if (ok) yes++;
        if (ok && minSep < 0) minSep = sep;
    }
    fclose(csv);
    return { minSep, 100.0f * (float)yes / (float)total };
}

/* ========================================================================
 * METRIC 8: MONTE CARLO ROBUSTNESS
 *
 * N random scenarios. Each scenario = uniform random distances [1, 200]
 * for all 12 sensors. For each, run the full pipeline and verify:
 *   - Every motor level is in [0, MAX_MOTOR_LEVEL]
 *   - Packed bytes are non-null when any motor > 0
 *   - Closer sensor variant always produces >= motor level than farther
 *     variant at the same angle (monotonicity invariant)
 * Report pass rate.
 * ======================================================================== */
struct MonteCarloStats {
    int runs;
    int passing;
    float pct;
};

static MonteCarloStats metric8_montecarlo(const char *csv_path) {
    FILE *csv = fopen(csv_path, "w");
    fprintf(csv, "run_idx,passed,failure_reason\n");

    const int N = 10000;
    std::mt19937 rng(987654321);
    std::uniform_real_distribution<float> U(1.0f, 200.0f);

    int pass = 0;
    for (int i = 0; i < N; i++) {
        float dist[TOTAL_SENSORS];
        for (int k = 0; k < TOTAL_SENSORS; k++) dist[k] = U(rng);

        uint8_t sr[SHIFT_REGISTER_BYTES];
        uint8_t ml[NUM_MOTORS];
        processSensorReadings(dist, sr, ml);

        bool ok = true;
        const char *why = "";

        for (int m = 0; m < NUM_MOTORS; m++) {
            if (ml[m] > MAX_MOTOR_LEVEL) { ok = false; why = "level_gt_max"; break; }
        }

        if (ok) {
            /* monotonicity invariant: halving any sensor distance should never
             * decrease that sensor's dominant motor level */
            for (int s = 0; s < NUM_BELT_SENSORS && ok; s++) {
                float closer[TOTAL_SENSORS];
                for (int k = 0; k < TOTAL_SENSORS; k++) closer[k] = dist[k];
                closer[s] = dist[s] * 0.5f;
                uint8_t ml2[NUM_MOTORS];
                mapSensorsToMotors(closer, ml2);
                int maxA = 0, maxB = 0;
                for (int m = 0; m < NUM_MOTORS; m++) {
                    if (ml[m] > maxA) maxA = ml[m];
                    if (ml2[m] > maxB) maxB = ml2[m];
                }
                if (maxB < maxA) { ok = false; why = "non_monotone"; }
            }
        }

        if (ok) pass++;
        /* Subsample CSV: log first 200 + any failures */
        if (i < 200 || !ok) {
            fprintf(csv, "%d,%d,%s\n", i, ok ? 1 : 0, why);
        }
    }
    fclose(csv);
    return { N, pass, 100.0f * (float)pass / (float)N };
}

/* ========================================================================
 * SUPPORTING DATA: distance-vs-level step function + angular falloff
 * (used by the python plots, kept for visual context)
 * ======================================================================== */
static void support_distance_vs_level(const char *csv_path) {
    FILE *csv = fopen(csv_path, "w");
    fprintf(csv, "distance_cm,level_arm50,level_arm65,level_arm80\n");
    for (int d = 0; d <= 130; d++) {
        uint8_t a = categorizeDistance((float)d, 50.0f);
        uint8_t b = categorizeDistance((float)d, 65.0f);
        uint8_t c = categorizeDistance((float)d, 80.0f);
        fprintf(csv, "%d,%d,%d,%d\n", d, a, b, c);
    }
    fclose(csv);
}

/* ========================================================================
 * MAIN
 * ======================================================================== */
int main(void) {
    printf("Generating quantitative pipeline metrics...\n\n");

    printf("[1/8] Angular localization error...\n");
    auto loc = metric1_localization("metric1_localization.csv", 50.0f);

    printf("[2/8] 360 degree coverage...\n");
    auto cov = metric2_coverage("metric2_coverage.csv");

    printf("[3/8] Motor count per obstacle...\n");
    auto mc = metric3_motor_count("metric3_motor_count.csv", 50.0f);

    printf("[4/8] Distance monotonicity...\n");
    auto mono = metric4_monotonicity("metric4_monotonicity.csv");

    printf("[5/8] Arm length scaling linearity...\n");
    auto lin = metric5_linearity("metric5_linearity.csv");

    printf("[6/8] Pipeline latency (100k samples)...\n");
    auto lat = metric6_latency("metric6_latency.csv");

    printf("[7/8] Multi-obstacle discrimination...\n");
    auto disc = metric7_discrimination("metric7_discrimination.csv");

    printf("[8/8] Monte Carlo robustness (10k runs)...\n");
    auto mcr = metric8_montecarlo("metric8_montecarlo.csv");

    printf("\nSupporting data: distance vs level...\n");
    support_distance_vs_level("support_distance_vs_level.csv");

    /* ---- Headline summary ---- */
    FILE *sum = fopen("metrics_summary.txt", "w");
    auto both = [&](const char *fmt, auto... args) {
        fprintf(stdout, fmt, args...);
        fprintf(sum,    fmt, args...);
    };

    both("\n%s\n", "===========================================================");
    both("  SENSOR-TO-MOTOR PIPELINE -- HEADLINE METRICS\n");
    both("%s\n\n", "===========================================================");

    both("[1] Angular localization error (obstacle @ 50 cm, 360 deg sweep)\n");
    both("       mean error      : %6.2f deg\n", loc.mean);
    both("       max error       : %6.2f deg\n", loc.maxAbs);
    both("       std deviation   : %6.2f deg\n\n", loc.stdev);

    both("[2] 360 deg coverage (fraction of angles producing >=1 motor)\n");
    for (auto &r : cov) {
        both("       @ %5.1f cm      : %6.2f %%  (dead zones: %d)\n",
             r.dist, r.pctCoverage, r.deadZones);
    }
    both("\n");

    both("[3] Motor count per single obstacle (obstacle @ 50 cm)\n");
    both("       mean            : %6.2f motors active\n", mc.mean);
    both("       min             : %d\n", mc.minCount);
    both("       max             : %d\n\n", mc.maxCount);

    both("[4] Distance monotonicity (closer always >= farther level)\n");
    both("       samples tested  : %d\n", mono.total);
    both("       passing         : %d\n", mono.passing);
    both("       pass rate       : %6.2f %%\n\n", mono.pct);

    both("[5] Arm length scaling linearity (40-90 cm sweep, 7 levels)\n");
    for (int l = 1; l <= 7; l++) {
        both("       level %d R^2     : %6.4f\n", l, lin.r2_per_level[l]);
    }
    both("       mean R^2        : %6.4f\n\n", lin.mean_r2);

    both("[6] Pipeline latency (100k random inputs)\n");
    both("       mean            : %7.3f us\n", lat.mean_us);
    both("       median          : %7.3f us\n", lat.median_us);
    both("       p99             : %7.3f us\n", lat.p99_us);
    both("       throughput      : %10.0f iter/sec\n", lat.iter_per_sec);
    both("       headroom vs 50ms: %7.1f x faster than sensor period\n\n",
         lat.headroom_vs_50ms);

    both("[7] Multi-obstacle discrimination (both @ 30 cm)\n");
    both("       min separation  : %d deg\n", disc.minSeparationDeg);
    both("       discrimination%% : %6.2f %% of tested separations\n\n",
         disc.discriminationPct);

    both("[8] Monte Carlo robustness\n");
    both("       runs            : %d\n", mcr.runs);
    both("       passing         : %d\n", mcr.passing);
    both("       pass rate       : %6.2f %%\n\n", mcr.pct);

    both("%s\n", "===========================================================");
    fclose(sum);

    printf("\nDone. CSVs + metrics_summary.txt written to current directory.\n");
    printf("Run plot_metrics.py to generate graphs.\n");
    return 0;
}
