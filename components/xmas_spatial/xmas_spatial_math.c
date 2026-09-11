#include "xmas_spatial_math.h"
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    float value[XS_WINDOW];
    uint64_t time[XS_WINDOW];
    unsigned used, next;
} edge_t;
struct xs_model {
    size_t capacity, count;
    xs_id_t *ids;
    edge_t *edges;
    float *d, *w, *b, *v; /* dense matrices, allocated once */
    xs_vec3_t *x, *trial, *gradient, *best, *previous;
    size_t anchors[4];
    bool frame, previous_valid;
};

static xs_vec3_t sub(xs_vec3_t a, xs_vec3_t b)
{ return (xs_vec3_t){a.x-b.x, a.y-b.y, a.z-b.z}; }
static xs_vec3_t scale(xs_vec3_t a, float s)
{ return (xs_vec3_t){a.x*s, a.y*s, a.z*s}; }
static float dot(xs_vec3_t a, xs_vec3_t b)
{ return a.x*b.x+a.y*b.y+a.z*b.z; }
static xs_vec3_t cross(xs_vec3_t a, xs_vec3_t b)
{ return (xs_vec3_t){a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
static float length(xs_vec3_t a) { return sqrtf(dot(a,a)); }
static bool finite_vec(xs_vec3_t a)
{ return isfinite(a.x) && isfinite(a.y) && isfinite(a.z); }
static void center(xs_vec3_t *x, size_t n)
{
    double a=0,b=0,c=0;
    for (size_t i=0;i<n;i++) { a+=x[i].x; b+=x[i].y; c+=x[i].z; }
    xs_vec3_t mean={(float)(a/n),(float)(b/n),(float)(c/n)};
    for (size_t i=0;i<n;i++) x[i]=sub(x[i],mean);
}
static size_t edge_index(const xs_model_t *m, size_t a, size_t b)
{
    if (a>b) { size_t t=a; a=b; b=t; }
    return a*(2*m->capacity-a-1)/2+b-a-1;
}
static float median(float *a, unsigned n)
{
    for (unsigned i=1;i<n;i++) {
        float v=a[i]; unsigned j=i;
        while (j && a[j-1]>v) { a[j]=a[j-1]; j--; }
        a[j]=v;
    }
    return n%2 ? a[n/2] : (a[n/2-1]+a[n/2])*0.5f;
}

xs_options_t xs_default_options(void)
{ return (xs_options_t){3, 1800000, 0.15f, 0.30f, 400}; }
xs_model_t *xs_create(size_t capacity)
{
    /* Bound multiplication before allocating; there is no fixed node limit. */
    if (capacity<4 || capacity>SIZE_MAX/capacity ||
        capacity*capacity>SIZE_MAX/sizeof(edge_t)) return NULL;
    xs_model_t *m=calloc(1,sizeof(*m));
    if (!m) return NULL;
    m->capacity=capacity;
    m->ids=calloc(capacity,sizeof(*m->ids));
    m->edges=calloc(capacity*(capacity-1)/2,sizeof(*m->edges));
    m->d=calloc(capacity*capacity,sizeof(float));
    m->w=calloc(capacity*capacity,sizeof(float));
    m->b=calloc(capacity*capacity,sizeof(float));
    m->v=calloc(capacity*capacity,sizeof(float));
    m->x=calloc(capacity,sizeof(xs_vec3_t));
    m->trial=calloc(capacity,sizeof(xs_vec3_t));
    m->gradient=calloc(capacity,sizeof(xs_vec3_t));
    m->best=calloc(capacity,sizeof(xs_vec3_t));
    m->previous=calloc(capacity,sizeof(xs_vec3_t));
    if (!m->ids || !m->edges || !m->d || !m->w || !m->b || !m->v ||
        !m->x || !m->trial || !m->gradient || !m->best || !m->previous) {
        xs_destroy(m); return NULL;
    }
    return m;
}
void xs_destroy(xs_model_t *m)
{
    if (!m) return;
    free(m->ids); free(m->edges); free(m->d); free(m->w); free(m->b); free(m->v);
    free(m->x); free(m->trial); free(m->gradient); free(m->best); free(m->previous);
    free(m);
}
xs_status_t xs_reset(xs_model_t *m, const xs_id_t *ids, size_t n)
{
    if (!m || !ids || n<4 || n>m->capacity) return XS_INVALID;
    for (size_t i=1;i<n;i++) if (memcmp(ids[i-1].mac,ids[i].mac,6)>=0) return XS_INVALID;
    memcpy(m->ids,ids,n*sizeof(*ids)); m->count=n;
    memset(m->edges,0,m->capacity*(m->capacity-1)/2*sizeof(*m->edges));
    m->frame=false; m->previous_valid=false;
    return XS_OK;
}
xs_status_t xs_observe(xs_model_t *m,size_t a,size_t b,float d,uint64_t now)
{
    /* 10 km is far beyond ornament ranging, and avoids squared-distance
     * overflow. Zero/negative/wrapped/NaN reports are failures, not neighbours. */
    if (!m || a>=m->count || b>=m->count || a==b || !isfinite(d) || d<=0 || d>10000)
        return XS_INVALID;
    edge_t *e=&m->edges[edge_index(m,a,b)];
    e->value[e->next]=d; e->time[e->next]=now;
    e->next=(e->next+1)%XS_WINDOW;
    if (e->used<XS_WINDOW) e->used++;
    return XS_OK;
}

/* Symmetric Jacobi eigendecomposition, written here rather than using a matrix
 * package. For each p,q we rotate their plane to annihilate A[p,q]:
 * tau=(Aqq-App)/(2Apq), t=sign(tau)/(abs(tau)+sqrt(1+tau^2)),
 * c=1/sqrt(1+t^2), s=t*c. A <- R^T A R; V <- V R.
 * V's columns are eigenvectors; diagonal(A) holds eigenvalues. Cyclic sweeps
 * cost O(n^3), storage O(n^2). This is intended for tens of boards, not a
 * claim that an ESP32 has unbounded RAM or radio airtime. */
static bool jacobi(float *a,float *v,size_t n)
{
    memset(v,0,n*n*sizeof(float));
    for (size_t i=0;i<n;i++) v[i*n+i]=1;
    for (unsigned sweep=0;sweep<60;sweep++) {
        double diag=0,off=0;
        for (size_t p=0;p<n;p++) {
            diag+=fabs(a[p*n+p]);
            for (size_t q=p+1;q<n;q++) off+=fabs(a[p*n+q]);
        }
        if (off<=1e-7*(diag+1e-12)) return true;
        for (size_t p=0;p<n;p++) for (size_t q=p+1;q<n;q++) {
            double apq=a[p*n+q];
            if (fabs(apq)<1e-10*(diag+1e-12)/n) continue;
            double tau=(a[q*n+q]-a[p*n+p])/(2*apq);
            double t=copysign(1.0,tau)/(fabs(tau)+hypot(1.0,tau));
            float c=(float)(1/sqrt(1+t*t)),s=(float)(t*c);
            a[p*n+p]-=(float)(t*apq); a[q*n+q]+=(float)(t*apq);
            a[p*n+q]=a[q*n+p]=0;
            for (size_t k=0;k<n;k++) {
                if (k!=p && k!=q) {
                    float kp=a[k*n+p],kq=a[k*n+q];
                    a[k*n+p]=a[p*n+k]=c*kp-s*kq;
                    a[k*n+q]=a[q*n+k]=s*kp+c*kq;
                }
                float vp=v[k*n+p],vq=v[k*n+q];
                v[k*n+p]=c*vp-s*vq; v[k*n+q]=s*vp+c*vq;
            }
        }
    }
    return false;
}

/* Classical metric multidimensional scaling:
 * D_ij = median of recent pairwise FTM distances.
 * J=I-11^T/n; B=-1/2 J (D elementwise squared) J.
 * For exact Euclidean distances, B=X X^T for centered coordinates X.
 * Decompose B=V Lambda V^T and take the three largest positive eigenvalues:
 * X_ik=V_ik sqrt(lambda_k). Negative eigenvalues arise from inconsistent noisy
 * measurements; truncation is an initialization, NOT proof the data are good.
 * Row means and grand mean implement J without allocating/multiplying J. */
static bool mds(xs_model_t *m)
{
    size_t n=m->count; double grand=0;
    for (size_t i=0;i<n;i++) {
        double row=0;
        for (size_t j=0;j<n;j++) row+=(double)m->d[i*n+j]*m->d[i*n+j];
        m->gradient[i].x=(float)(row/n); grand+=row;
    }
    grand/=n*n;
    for (size_t i=0;i<n;i++) for (size_t j=0;j<n;j++) {
        double d=m->d[i*n+j];
        m->b[i*n+j]=(float)(-0.5*(d*d-m->gradient[i].x-m->gradient[j].x+grand));
    }
    if (!jacobi(m->b,m->v,n)) return false;
    size_t top[3]={SIZE_MAX,SIZE_MAX,SIZE_MAX};
    for (unsigned k=0;k<3;k++) {
        float largest=0;
        for (size_t j=0;j<n;j++) {
            bool used=false;
            for (unsigned h=0;h<k;h++) if (top[h]==j) used=true;
            if (!used && m->b[j*n+j]>largest) { largest=m->b[j*n+j]; top[k]=j; }
        }
    }
    for (size_t i=0;i<n;i++) {
        float a[3]={0};
        for (unsigned k=0;k<3;k++) if (top[k]!=SIZE_MAX)
            a[k]=m->v[i*n+top[k]]*sqrtf(m->b[top[k]*n+top[k]]);
        m->x[i]=(xs_vec3_t){a[0],a[1],a[2]};
    }
    return true;
}

/* Robust weighted stress, refined with gradient descent and Armijo line search:
 * E(X)=sum_(i<j) w_ij rho(||Xi-Xj||-Dij).
 * rho(e)=e^2/2 if |e|<=h, else h(|e|-h/2) (Huber loss).
 * grad_i += w_ij clamp(e,-h,h) (Xi-Xj)/||Xi-Xj||; grad_j -= same.
 * Backtracking guarantees accepted steps decrease E; centering removes the
 * translation null mode. Huber reduces an inconsistent edge's influence, but
 * cannot identify every biased edge, especially with only four/five boards.
 * Four points have 3*4-6=6 shape parameters and six edges: no redundancy.
 * Five have nine parameters and ten edges. More samples reduce random noise;
 * they cannot remove fixed multipath/calibration errors. */
static double energy(xs_model_t *m,const xs_vec3_t *x,float h,bool gradient)
{
    size_t n=m->count; double e=0;
    if (gradient) memset(m->gradient,0,n*sizeof(xs_vec3_t));
    for (size_t i=0;i<n;i++) for (size_t j=i+1;j<n;j++) {
        xs_vec3_t v=sub(x[i],x[j]); float r=length(v);
        float error=r-m->d[i*n+j],a=fabsf(error),w=m->w[i*n+j];
        e+=w*(a<=h ? 0.5*error*error : h*(a-0.5*h));
        if (gradient && r>1e-9f) {
            v=scale(v,w*fmaxf(-h,fminf(h,error))/r);
            m->gradient[i]=sub(m->gradient[i],scale(v,-1));
            m->gradient[j]=sub(m->gradient[j],v);
        }
    }
    return e;
}
static double refine(xs_model_t *m,const xs_options_t *o,bool *converged)
{
    size_t n=m->count; float max_degree=0;
    for (size_t i=0;i<n;i++) {
        float degree=0;
        for (size_t j=0;j<n;j++) degree+=m->w[i*n+j];
        max_degree=fmaxf(max_degree,degree);
    }
    double current=energy(m,m->x,o->huber_m,true);
    *converged=false;
    for (unsigned it=0;it<o->iterations;it++) {
        double norm=0;
        for (size_t i=0;i<n;i++) norm+=dot(m->gradient[i],m->gradient[i]);
        if (norm<1e-12) { *converged=true; break; }
        float step=1.0f/fmaxf(max_degree,1e-9f); bool accepted=false;
        double next=current;
        for (unsigned bt=0;bt<24;bt++,step*=0.5f) {
            for (size_t i=0;i<n;i++) m->trial[i]=sub(m->x[i],scale(m->gradient[i],step));
            center(m->trial,n);
            next=energy(m,m->trial,o->huber_m,false);
            if (next<=current-1e-4*step*norm) { accepted=true; break; }
        }
        if (!accepted) break;
        memcpy(m->x,m->trial,n*sizeof(xs_vec3_t));
        if (current-next<1e-9*(1+current)) { *converged=true; current=next; break; }
        current=energy(m,m->x,o->huber_m,true);
    }
    return current;
}

/* Fix the six continuous gauge freedoms without claiming compass/gravity axes.
 * Choose a long baseline A->B, then C far from its line, D far from ABC's plane.
 * Persist these indices until membership resets; choosing new PCA axes on each
 * scan would swap axes near equal eigenvalues. x is AB, y is C's perpendicular
 * component, z=x cross y; reflect z so D is on its positive side. Center at the
 * group centroid. Near-degenerate anchors cannot provide a stable 3D frame;
 * rank/eigen_ratio expose that rather than assigning physical meaning to z. */
static void canonicalize(xs_model_t *m)
{
    size_t n=m->count; xs_vec3_t *x=m->best;
    if (!m->frame) {
        float far=-1;
        for (size_t i=0;i<n;i++) for (size_t j=i+1;j<n;j++) {
            float d=dot(sub(x[j],x[i]),sub(x[j],x[i]));
            if (d>far) { far=d; m->anchors[0]=i; m->anchors[1]=j; }
        }
    }
    size_t a=m->anchors[0],b=m->anchors[1];
    xs_vec3_t u=sub(x[b],x[a]); float len=length(u);
    u=len>1e-9f ? scale(u,1/len) : (xs_vec3_t){1,0,0};
    if (!m->frame) {
        float far=-1;
        for (size_t i=0;i<n;i++) {
            xs_vec3_t p=sub(x[i],x[a]); p=sub(p,scale(u,dot(u,p)));
            float d=dot(p,p);
            if (d>far) { far=d; m->anchors[2]=i; }
        }
    }
    xs_vec3_t v=sub(x[m->anchors[2]],x[a]); v=sub(v,scale(u,dot(u,v)));
    len=length(v);
    if (len<1e-7f) {
        xs_vec3_t axis=fabsf(u.x)<0.8f ? (xs_vec3_t){1,0,0} : (xs_vec3_t){0,1,0};
        v=sub(axis,scale(u,dot(u,axis))); len=length(v);
    }
    v=scale(v,1/len); xs_vec3_t z=cross(u,v);
    if (!m->frame) {
        float far=-1;
        for (size_t i=0;i<n;i++) {
            float d=fabsf(dot(z,sub(x[i],x[a])));
            if (d>far) { far=d; m->anchors[3]=i; }
        }
        m->frame=true;
    }
    if (dot(z,sub(x[m->anchors[3]],x[a]))<0) z=scale(z,-1);
    center(x,n);
    for (size_t i=0;i<n;i++) x[i]=(xs_vec3_t){dot(x[i],u),dot(x[i],v),dot(x[i],z)};
}

xs_status_t xs_solve(xs_model_t *m,uint64_t now,const xs_options_t *o,
                     xs_vec3_t *out,xs_quality_t *q)
{
    if (q) memset(q,0,sizeof(*q));
    if (!m || !o || !out || !q || m->count<4 || !o->min_samples ||
        o->min_samples>XS_WINDOW || !o->max_age_ms || !o->iterations ||
        !isfinite(o->noise_floor_m) || o->noise_floor_m<0.001f ||
        !isfinite(o->huber_m) || o->huber_m<=0) return XS_INVALID;
    size_t n=m->count; q->count=n; q->required_pairs=n*(n-1)/2;
    memset(m->d,0,n*n*sizeof(float)); memset(m->w,0,n*n*sizeof(float));
    for (size_t i=0;i<n;i++) for (size_t j=i+1;j<n;j++) {
        edge_t *e=&m->edges[edge_index(m,i,j)]; float values[XS_WINDOW],dev[XS_WINDOW];
        unsigned k=0;
        for (unsigned t=0;t<e->used;t++)
            if (now>=e->time[t] && now-e->time[t]<=o->max_age_ms) values[k++]=e->value[t];
        if (k<o->min_samples) continue;
        float d=median(values,k);
        for (unsigned t=0;t<k;t++) dev[t]=fabsf(values[t]-d);
        /* MAD*1.4826 estimates Gaussian sigma, remains robust to <50% gross
         * sample outliers. Do NOT divide by sqrt(k): repeated FTM observations
         * share antenna/multipath bias. A quiet but biased link is still possible. */
        float sigma=1.4826f*median(dev,k);
        float w=1/(sigma*sigma+o->noise_floor_m*o->noise_floor_m);
        m->d[i*n+j]=m->d[j*n+i]=d; m->w[i*n+j]=m->w[j*n+i]=w;
        q->measured_pairs++;
    }
    if (q->measured_pairs!=q->required_pairs) return XS_INCOMPLETE;
    if (!mds(m)) return XS_NUMERIC;
    bool convergence=false;
    double best=refine(m,o,&convergence); q->converged=convergence;
    memcpy(m->best,m->x,n*sizeof(xs_vec3_t));
    /* A second start from the preceding layout avoids throwing away temporal
     * information. Small deterministic 3D perturbations give a rank-deficient
     * initialization a chance to escape a flat stationary configuration. */
    for (unsigned start=0;start<2;start++) {
        memcpy(m->x,(start==0 && m->previous_valid)?m->previous:m->best,n*sizeof(xs_vec3_t));
        if (start==1) {
            float amplitude=0.01f*m->d[1]; uint32_t r=0x91ab32u;
            for (size_t i=0;i<n;i++) {
                float p[3];
                for (unsigned k=0;k<3;k++) { r=r*1664525u+1013904223u; p[k]=amplitude*((r>>8)/16777216.0f-0.5f); }
                m->x[i].x+=p[0]; m->x[i].y+=p[1]; m->x[i].z+=p[2];
            }
        }
        double e=refine(m,o,&convergence);
        if (e<best) { best=e; q->converged=convergence; memcpy(m->best,m->x,n*sizeof(xs_vec3_t)); }
    }
    if (!isfinite(best)) return XS_NUMERIC;
    canonicalize(m);
    float cov[9]={0},vectors[9]; double sum=0;
    for (size_t i=0;i<n;i++) {
        if (!finite_vec(m->best[i])) return XS_NUMERIC;
        float p[3]={m->best[i].x,m->best[i].y,m->best[i].z};
        for (unsigned a=0;a<3;a++) for (unsigned b=0;b<3;b++) cov[a*3+b]+=p[a]*p[b]/n;
        for (size_t j=i+1;j<n;j++) {
            float e=fabsf(length(sub(m->best[i],m->best[j]))-m->d[i*n+j]);
            sum+=(double)e*e; q->max_residual_m=fmaxf(q->max_residual_m,e);
        }
    }
    if (!jacobi(cov,vectors,3)) return XS_NUMERIC;
    float largest=fmaxf(cov[0],fmaxf(cov[4],cov[8]));
    float smallest=fmaxf(0,fminf(cov[0],fminf(cov[4],cov[8])));
    for (unsigned k=0;k<3;k++) if (cov[k*3+k]>fmaxf(1e-10f,largest*1e-6f)) q->rank++;
    q->eigen_ratio=largest>0 ? smallest/largest : 0;
    q->rms_m=(float)sqrt(sum/q->required_pairs);
    memcpy(out,m->best,n*sizeof(xs_vec3_t));
    memcpy(m->previous,m->best,n*sizeof(xs_vec3_t)); m->previous_valid=true;
    return XS_OK;
}
bool xs_direction(xs_vec3_t from,xs_vec3_t to,xs_vec3_t *unit,float *distance)
{
    if (!unit || !finite_vec(from) || !finite_vec(to)) return false;
    xs_vec3_t v=sub(to,from); float d=length(v);
    if (!isfinite(d) || d<1e-7f) return false;
    *unit=scale(v,1/d); if (distance) *distance=d; return true;
}
int xs_bucket(xs_vec3_t p,const xs_vec3_t *directions,size_t count)
{
    xs_vec3_t u;
    if (!directions || count>INT_MAX || !xs_direction((xs_vec3_t){0},p,&u,NULL)) return -1;
    int best=-1; float score=-FLT_MAX;
    for (size_t i=0;i<count;i++) {
        xs_vec3_t v;
        if (!xs_direction((xs_vec3_t){0},directions[i],&v,NULL)) continue;
        float s=dot(u,v); if (s>score) { score=s; best=(int)i; }
    }
    return best;
}
