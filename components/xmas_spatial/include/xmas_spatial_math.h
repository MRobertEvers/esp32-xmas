#ifndef XMAS_SPATIAL_MATH_H
#define XMAS_SPATIAL_MATH_H

/* Portable C11; only the C runtime and libm are needed. All distances are metres,
 * time is caller-supplied monotonic milliseconds. No global state or threads. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XS_WINDOW 7
typedef struct xs_model xs_model_t;
typedef struct { uint8_t mac[6]; } xs_id_t;
typedef struct { float x, y, z; } xs_vec3_t;
typedef enum {
    XS_OK = 0, XS_INVALID, XS_NO_MEMORY, XS_INCOMPLETE, XS_NUMERIC
} xs_status_t;
typedef struct {
    unsigned min_samples;       /* 1..XS_WINDOW; recommended 3 */
    uint64_t max_age_ms;         /* each individual sample expires */
    float noise_floor_m;        /* systematic-error floor, NOT standard error */
    float huber_m;              /* residual where squared loss becomes linear */
    unsigned iterations;        /* maximum gradient steps per start */
} xs_options_t;
typedef struct {
    size_t count, measured_pairs, required_pairs;
    unsigned rank;              /* 0..3; 1/2 means line/plane, not a full 3D map */
    bool converged;             /* numerical convergence, NOT physical accuracy */
    float rms_m, max_residual_m;
    float eigen_ratio;          /* smallest/largest coordinate covariance eigenvalue */
} xs_quality_t;

xs_options_t xs_default_options(void);
xs_model_t *xs_create(size_t capacity);
void xs_destroy(xs_model_t *model);
/* ids must be unique and sorted lexicographically. A reset clears samples and
 * the coordinate frame. Runtime capacity may be any value >=4 that fits RAM. */
xs_status_t xs_reset(xs_model_t *model, const xs_id_t *ids, size_t count);
xs_status_t xs_observe(xs_model_t *model, size_t a, size_t b,
                       float distance_m, uint64_t now_ms);
/* Requires a complete graph of fresh pair estimates. Sparse graphs are NOT
 * filled with shortest-path lengths: those are not Euclidean measurements.
 * out is written only on success, quality is initialized even on failure.
 * Frame: centroid at zero, persistent ID-selected axes; may reset on membership
 * change. Reflection is arbitrary. This is position, not board orientation. */
xs_status_t xs_solve(xs_model_t *model, uint64_t now_ms,
                     const xs_options_t *options, xs_vec3_t *out,
                     xs_quality_t *quality);
/* Unit vector from 'from' to 'to'; false for coincident points/invalid inputs. */
bool xs_direction(xs_vec3_t from, xs_vec3_t to, xs_vec3_t *unit, float *distance);
/* Nearest supplied unit direction by maximum dot product. Choose your own
 * 10 (or other number of) buckets; -1 means undefined at the group centroid.
 * Bucket vectors need not already be normalized. No implied gravity/north. */
int xs_bucket(xs_vec3_t point, const xs_vec3_t *directions, size_t count);

#ifdef __cplusplus
}
#endif
#endif
