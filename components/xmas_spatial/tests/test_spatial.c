#include "xmas_spatial_cluster.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c); exit(1); } } while (0)
static uint32_t rng=123456;
static uint32_t random32(void) { rng=rng*1664525u+1013904223u; return rng; }
static float uniform(void) { return (random32()>>8)/16777216.0f; }
static xs_id_t identity(size_t i)
{ xs_id_t id={{2,0,0,0,(uint8_t)(i>>8),(uint8_t)(i+1)}}; return id; }
static float distance(xs_vec3_t a,xs_vec3_t b)
{ return sqrtf((a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z)); }
static void check_shape(const xs_vec3_t *actual,const xs_vec3_t *expected,size_t n,float tolerance)
{
    float worst=0;
    for (size_t i=0;i<n;i++) for (size_t j=i+1;j<n;j++)
        worst=fmaxf(worst,fabsf(distance(actual[i],actual[j])-distance(expected[i],expected[j])));
    if (worst>tolerance) fprintf(stderr,"shape max error %.6f, tolerance %.6f\n",(double)worst,(double)tolerance);
    CHECK(worst<=tolerance);
}
static void math_case(size_t n,unsigned dimensions,bool outliers)
{
    xs_model_t *m=xs_create(n); CHECK(m);
    xs_id_t *ids=calloc(n,sizeof(*ids));
    xs_vec3_t *truth=calloc(n,sizeof(*truth)),*got=calloc(n,sizeof(*got)),*again=calloc(n,sizeof(*again));
    CHECK(ids && truth && got && again);
    for (size_t i=0;i<n;i++) {
        ids[i]=identity(i);
        truth[i]=(xs_vec3_t){uniform()*2,dimensions>1?uniform()*2:0,dimensions>2?uniform()*3:0};
    }
    CHECK(xs_reset(m,ids,n)==XS_OK);
    xs_options_t o=xs_default_options(); xs_quality_t q;
    CHECK(xs_solve(m,100,&o,got,&q)==XS_INCOMPLETE);
    for (size_t i=0;i<n;i++) for (size_t j=i+1;j<n;j++) {
        float d=distance(truth[i],truth[j]);
        for (unsigned k=0;k<XS_WINDOW;k++) {
            /* Three arbitrarily bad samples among seven cannot move the median
             * away from the four correct ones, in either measurement direction. */
            float value=outliers && k<3 ? d+5+uniform()*10 : d;
            CHECK(xs_observe(m,k%2?j:i,k%2?i:j,value,100+k)==XS_OK);
        }
    }
    CHECK(xs_solve(m,110,&o,got,&q)==XS_OK);
    check_shape(got,truth,n,0.002f);
    CHECK(q.rank==dimensions); CHECK(q.rms_m<0.001f);
    double x=0,y=0,z=0;
    for (size_t i=0;i<n;i++) { x+=got[i].x; y+=got[i].y; z+=got[i].z; }
    CHECK(fabs(x)+fabs(y)+fabs(z)<0.001);
    CHECK(xs_solve(m,120,&o,again,&q)==XS_OK);
    for (size_t i=0;i<n;i++) CHECK(distance(got[i],again[i])<0.002f);
    CHECK(xs_solve(m,110+o.max_age_ms,&o,again,&q)==XS_INCOMPLETE);
    xs_destroy(m); free(ids); free(truth); free(got); free(again);
    printf("PASS exact %zu-node %uD%s\n",n,dimensions,outliers?" + gross sample outliers":"");
}
static void math_invalid_and_expiry(void)
{
    CHECK(!xs_create(3)); CHECK(!xs_create(SIZE_MAX));
    xs_model_t *m=xs_create(4); CHECK(m);
    xs_id_t ids[4]; for (size_t i=0;i<4;i++) ids[i]=identity(i);
    CHECK(xs_reset(m,ids,4)==XS_OK);
    CHECK(xs_observe(m,0,1,NAN,0)==XS_INVALID);
    CHECK(xs_observe(m,0,1,INFINITY,0)==XS_INVALID);
    CHECK(xs_observe(m,0,1,-1,0)==XS_INVALID);
    CHECK(xs_observe(m,0,1,0,0)==XS_INVALID);
    CHECK(xs_observe(m,0,0,1,0)==XS_INVALID);
    CHECK(xs_observe(m,0,4,1,0)==XS_INVALID);
    xs_options_t o=xs_default_options(); o.max_age_ms=100;
    xs_vec3_t got[4]={{11,22,33}}; xs_quality_t q;
    for (size_t i=0;i<4;i++) for (size_t j=i+1;j<4;j++)
        for (unsigned k=0;k<3;k++) CHECK(xs_observe(m,i,j,1,k)==XS_OK);
    CHECK(xs_solve(m,50,&o,got,&q)==XS_OK);
    /* One new sample must not rejuvenate the two expired ones. */
    for (size_t i=0;i<4;i++) for (size_t j=i+1;j<4;j++) CHECK(xs_observe(m,i,j,1,200)==XS_OK);
    got[0].x=99;
    CHECK(xs_solve(m,210,&o,got,&q)==XS_INCOMPLETE); CHECK(got[0].x==99);
    ids[1]=ids[0]; CHECK(xs_reset(m,ids,4)==XS_INVALID);
    o.noise_floor_m=NAN; CHECK(xs_solve(m,0,&o,got,&q)==XS_INVALID);
    xs_vec3_t dirs[]={{1,0,0},{0,1,0},{-1,0,0},{0,0,1}},u; float d;
    CHECK(xs_direction((xs_vec3_t){1,2,3},(xs_vec3_t){1,5,7},&u,&d));
    CHECK(fabsf(d-5)<1e-6f && fabsf(u.y-0.6f)<1e-6f);
    CHECK(xs_bucket((xs_vec3_t){-3,1,0},dirs,4)==2);
    CHECK(xs_bucket((xs_vec3_t){0},dirs,4)==-1);
    CHECK(xs_bucket((xs_vec3_t){NAN,0,0},dirs,4)==-1);
    xs_destroy(m); puts("PASS invalid inputs, individual sample expiry, direction/bucket API");
}
static void noisy_math(void)
{
    enum { N=12 }; xs_model_t *m=xs_create(N); CHECK(m);
    xs_id_t ids[N]; xs_vec3_t truth[N],got[N];
    for (size_t i=0;i<N;i++) { ids[i]=identity(i); truth[i]=(xs_vec3_t){uniform()*2,uniform()*2,uniform()*3}; }
    CHECK(xs_reset(m,ids,N)==XS_OK);
    for (size_t i=0;i<N;i++) for (size_t j=i+1;j<N;j++)
        for (unsigned k=0;k<7;k++) {
            float d=distance(truth[i],truth[j])+0.06f*(uniform()-0.5f);
            if (i==0 && j==1) d+=0.6f; /* quiet but consistently biased link */
            CHECK(xs_observe(m,i,j,d,k)==XS_OK);
        }
    xs_quality_t q; xs_options_t o=xs_default_options();
    CHECK(xs_solve(m,10,&o,got,&q)==XS_OK);
    CHECK(q.rank==3 && q.rms_m>0.02f && q.max_residual_m>0.2f);
    /* The fit must expose the disagreement; it must not claim perfect accuracy. */
    check_shape(got,truth,N,0.25f);
    xs_destroy(m); puts("PASS noisy ranges and visible residual from a biased edge");
}

/* A deterministic discrete-event network runs the actual production protocol.
 * Radios supply known geometric distances, NOT mocked solver results. Packets
 * are serialized, delayed, reordered, duplicated and dropped before delivery. */
enum { SIM_N=8, QUEUE_N=20000 };
typedef struct simulation simulation_t;
typedef struct {
    simulation_t *sim; xs_cluster_t *cluster; xs_vec3_t truth;
    size_t index,target; bool enabled,ranging,range_fail;
    uint32_t token; uint64_t completion;
    unsigned range_count;
} simulated_node_t;
typedef struct {
    unsigned source,target; uint8_t bytes[96]; size_t length; uint64_t due;
} packet_t;
struct simulation {
    simulated_node_t nodes[SIM_N]; packet_t queue[QUEUE_N],last_sent; size_t queued;
    uint64_t now; bool loss,block_points,block_results,partial_points; unsigned sent,dropped,duplicates;
};
static void queue_packet(simulation_t *w,unsigned source,unsigned target,const uint8_t *data,size_t length)
{
    CHECK(length<=96);
    if ((w->block_points && data[4]==4) || (w->block_results && data[4]==3)) return;
    if (w->partial_points && data[4]==4 && data[28]==0 && data[29]==0) return;
    w->sent++;
    if (w->loss && w->sent%7==0) { w->dropped++; return; }
    CHECK(w->queued<QUEUE_N);
    packet_t *p=&w->queue[w->queued++]; p->source=source; p->target=target;
    p->length=length; memcpy(p->bytes,data,length); p->due=w->now+(random32()%150);
    if (w->loss && w->sent%11==0) {
        CHECK(w->queued<QUEUE_N); w->queue[w->queued]=*p; w->queue[w->queued++].due+=200; w->duplicates++;
    }
}
static void sim_send(void *ctx,uint32_t ip,const uint8_t *data,size_t length)
{
    simulated_node_t *n=ctx; simulation_t *w=n->sim;
    CHECK(length<=sizeof(w->last_sent.bytes));
    w->last_sent.source=(unsigned)n->index; w->last_sent.length=length;
    memcpy(w->last_sent.bytes,data,length);
    for (unsigned i=0;i<SIM_N;i++)
        if (w->nodes[i].enabled && i!=n->index && (!ip || ip==i+1)) queue_packet(w,(unsigned)n->index,i,data,length);
}
static bool sim_range(void *ctx,const uint8_t ap[6],uint8_t channel,uint32_t token)
{
    (void)channel; simulated_node_t *n=ctx;
    if (n->ranging || !ap[5] || ap[5]>SIM_N) return false;
    n->target=ap[5]-1; n->token=token; n->completion=n->sim->now+100;
    n->ranging=true; n->range_count++; return true;
}
static void sim_cancel(void *ctx) { ((simulated_node_t *)ctx)->ranging=false; }
static void sim_add(simulation_t *w,size_t i,size_t capacity,uint32_t group)
{
    simulated_node_t *n=&w->nodes[i]; n->sim=w; n->index=i; n->enabled=true;
    n->truth=(xs_vec3_t){0.3f+(i%3)*0.6f,0.2f+(i%2)*1.1f,0.25f+i*0.31f+(i%3)*0.5f};
    uint8_t ap[6]={6,0,0,0,0,(uint8_t)(i+1)};
    xs_cluster_config_t cfg=xs_cluster_defaults();
    cfg.capacity=capacity; cfg.group=group; cfg.hello_ms=250; cfg.peer_timeout_ms=2000;
    cfg.settle_ms=1000; cfg.range_timeout_ms=500; cfg.retry_ms=200; cfg.publish_ms=400;
    cfg.layout_ttl_ms=15000; cfg.solver.max_age_ms=30000;
    xs_transport_t io={sim_send,sim_range,sim_cancel,n};
    n->cluster=xs_cluster_create(&cfg,&io,identity(i),(uint32_t)(100+i),ap,1,w->now); CHECK(n->cluster);
}
static void sim_steps(simulation_t *w,unsigned milliseconds)
{
    uint64_t end=w->now+milliseconds;
    while (w->now<end) {
        w->now+=50;
        for (size_t i=0;i<SIM_N;i++) {
            simulated_node_t *n=&w->nodes[i]; if (!n->enabled) continue;
            if (n->ranging && w->now>=n->completion) {
                n->ranging=false;
                bool ok=!n->range_fail && w->nodes[n->target].enabled;
                xs_cluster_range_result(n->cluster,n->token,ok,distance(n->truth,w->nodes[n->target].truth),w->now);
            }
            xs_cluster_tick(n->cluster,w->now);
        }
        /* Swap-remove reverses some delivery order; receive may append packets. */
        unsigned delivered=0;
        for (size_t i=0;i<w->queued;) {
            if (w->queue[i].due>w->now) { i++; continue; }
            packet_t p=w->queue[i]; w->queue[i]=w->queue[--w->queued];
            if (w->nodes[p.target].enabled)
                xs_cluster_receive(w->nodes[p.target].cluster,p.source+1,p.bytes,p.length,w->now);
            CHECK(++delivered<QUEUE_N);
        }
    }
}
static xs_info_t sim_check(simulation_t *w,size_t i,size_t expected)
{
    xs_node_t nodes[SIM_N]; xs_info_t info;
    size_t n=xs_cluster_copy(w->nodes[i].cluster,nodes,SIM_N,&info,w->now);
    if (n!=expected) fprintf(stderr,"node=%zu state=%d discovered=%zu valid=%d count=%zu expected=%zu pairs=%zu/%zu\n",
        i,info.state,info.discovered,info.valid,n,expected,info.quality.measured_pairs,info.quality.required_pairs);
    CHECK(n==expected && info.valid);
    xs_vec3_t got[SIM_N],truth[SIM_N];
    for (size_t j=0;j<n;j++) { got[j]=nodes[j].position; truth[j]=w->nodes[nodes[j].id.mac[5]-1].truth; }
    check_shape(got,truth,n,0.02f);
    return info;
}
static void sim_destroy(simulation_t *w)
{
    for (size_t i=0;i<SIM_N;i++) xs_cluster_destroy(w->nodes[i].cluster);
    free(w);
}
static void network_lifecycle(void)
{
    simulation_t *w=calloc(1,sizeof(*w)); CHECK(w); w->loss=true;
    for (size_t i=0;i<5;i++) sim_add(w,i,8,1234);
    sim_steps(w,50000);
    for (size_t i=0;i<5;i++) { xs_info_t info=sim_check(w,i,5); CHECK(info.coordinator==(i==0)); }
    CHECK(w->dropped && w->duplicates);
    puts("PASS five-board discovery, coordinator, lossy/reordered/duplicate packets, shared layouts");
    sim_add(w,5,8,1234); sim_steps(w,50000);
    for (size_t i=0;i<6;i++) sim_check(w,i,6);
    puts("PASS sixth-board join resets membership and builds a new layout");
    w->nodes[0].enabled=false; sim_steps(w,50000);
    for (size_t i=1;i<6;i++) { xs_info_t info=sim_check(w,i,5); CHECK(info.coordinator==(i==1)); }
    puts("PASS coordinator departure and re-election");
    xs_cluster_rejoin(w->nodes[2].cluster,987654,1,w->now);
    sim_steps(w,50000);
    for (size_t i=1;i<6;i++) sim_check(w,i,5);
    puts("PASS reboot nonce and membership recovery");
    w->nodes[1].enabled=false; sim_steps(w,40000);
    for (size_t i=2;i<6;i++) sim_check(w,i,4);
    puts("PASS exactly four boards");
    w->nodes[2].enabled=false; sim_steps(w,5000);
    for (size_t i=3;i<6;i++) {
        xs_info_t info; CHECK(xs_cluster_copy(w->nodes[i].cluster,NULL,0,&info,w->now)==0);
        CHECK(!info.valid && info.discovered==3 && info.state==XS_WAITING);
    }
    puts("PASS fewer than four boards never publishes a layout"); sim_destroy(w);
}
static void network_errors(void)
{
    simulation_t *w=calloc(1,sizeof(*w)); CHECK(w);
    for (size_t i=0;i<5;i++) sim_add(w,i,4,7);
    sim_steps(w,10000);
    for (size_t i=0;i<5;i++) {
        xs_info_t info; CHECK(xs_cluster_copy(w->nodes[i].cluster,NULL,0,&info,w->now)==0);
        CHECK(info.state==XS_CAPACITY);
    }
    w->nodes[4].enabled=false; sim_steps(w,25000);
    for (size_t i=0;i<4;i++) sim_check(w,i,4);
    sim_destroy(w); puts("PASS capacity overflow is explicit, not silent partial membership");
    w=calloc(1,sizeof(*w)); CHECK(w);
    for (size_t i=0;i<4;i++) sim_add(w,i,8,7);
    sim_add(w,4,8,8); sim_steps(w,20000);
    for (size_t i=0;i<4;i++) sim_check(w,i,4);
    xs_info_t info; CHECK(!xs_cluster_copy(w->nodes[4].cluster,NULL,0,&info,w->now)); CHECK(info.discovered==1);
    /* Complete snapshots stop arriving: the old snapshot must expire even if
     * the coordinator and all peers remain discoverable. */
    w->block_points=true; sim_steps(w,17000);
    CHECK(!xs_cluster_copy(w->nodes[1].cluster,NULL,0,&info,w->now)); CHECK(!info.valid);
    w->block_points=false; sim_steps(w,10000); sim_check(w,1,4);
    puts("PASS group isolation and stale layout expiration/recovery");
    /* Random datagrams, valid prefixes truncated at EVERY length, and mutations
     * are processed by the real decoder under ASan/UBSan. */
    uint8_t random_packet[100];
    for (unsigned k=0;k<20000;k++) {
        size_t n=random32()%sizeof(random_packet);
        for (size_t j=0;j<n;j++) random_packet[j]=(uint8_t)random32();
        xs_cluster_receive(w->nodes[0].cluster,2,random_packet,n,w->now);
    }
    CHECK(w->last_sent.length);
    packet_t valid=w->last_sent;
    for (size_t n=0;n<valid.length;n++) xs_cluster_receive(w->nodes[0].cluster,valid.source+1,valid.bytes,n,w->now);
    for (size_t i=0;i<valid.length;i++) {
        packet_t bad=valid; bad.bytes[i]^=0xff;
        xs_cluster_receive(w->nodes[0].cluster,valid.source+1,bad.bytes,bad.length,w->now);
    }
    sim_destroy(w); puts("PASS malformed, truncated, mutated datagrams");
    w=calloc(1,sizeof(*w)); CHECK(w);
    for (size_t i=0;i<4;i++) { sim_add(w,i,8,7); w->nodes[i].range_fail=true; }
    sim_steps(w,20000);
    CHECK(!xs_cluster_copy(w->nodes[0].cluster,NULL,0,&info,w->now)); CHECK(info.state==XS_COLLECTING);
    for (size_t i=0;i<4;i++) w->nodes[i].range_fail=false;
    sim_steps(w,20000); sim_check(w,0,4);
    sim_destroy(w); puts("PASS failed ranging cannot become a zero-distance measurement; recovery");
    w=calloc(1,sizeof(*w)); CHECK(w); w->partial_points=true;
    for (size_t i=0;i<4;i++) sim_add(w,i,8,7);
    sim_steps(w,20000); sim_check(w,0,4);
    for (size_t i=1;i<4;i++) CHECK(!xs_cluster_copy(w->nodes[i].cluster,NULL,0,&info,w->now));
    w->partial_points=false; sim_steps(w,3000);
    for (size_t i=1;i<4;i++) sim_check(w,i,4);
    /* Radio channels differ despite IP reachability: report incomplete rather
     * than pretending ordinary network communication supplies a distance. */
    xs_cluster_rejoin(w->nodes[3].cluster,888,6,w->now); sim_steps(w,10000);
    CHECK(!xs_cluster_copy(w->nodes[0].cluster,NULL,0,&info,w->now));
    CHECK(info.state==XS_COLLECTING && info.discovered==4);
    xs_cluster_rejoin(w->nodes[3].cluster,889,1,w->now); sim_steps(w,20000);
    for (size_t i=0;i<4;i++) sim_check(w,i,4);
    sim_destroy(w); puts("PASS incomplete snapshots stay private; cross-channel failures and recovery");
}
int main(void)
{
    setbuf(stdout,NULL);
    math_invalid_and_expiry();
    math_case(4,3,false); math_case(5,3,true); math_case(12,3,false);
    math_case(37,3,false); math_case(5,2,false); math_case(5,1,false);
    noisy_math(); network_lifecycle(); network_errors();
    puts("All spatial tests passed."); return 0;
}
