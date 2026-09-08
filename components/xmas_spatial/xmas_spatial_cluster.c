#include "xmas_spatial_cluster.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Wire protocol v1: explicitly encoded big-endian integers, never a memcpy of
 * a C struct (padding/alignment/host endian differ). All datagrams are <=96 B.
 * Header: magic[4]='XSP1', type:u8, reserved:u8=0, length:u16,
 * group:u32, sender station MAC[6], sender boot nonce:u32, sequence:u32.
 * HELLO: responder AP MAC[6], channel:u8.
 * JOB: target station MAC[6], target boot:u32, target AP MAC[6], channel:u8.
 * RESULT: target station MAC[6], target boot:u32, distance_mm:u32, success:u8.
 * POINT: count:u16,index:u16,node MAC[6],node boot:u32,xyz:3*i32 mm,
 *        rms:u32 mm,max_error:u32 mm,rank:u8,converged:u8,
 *        eigen_ratio:u32 ppm,age:u32 ms.
 * HELLO/POINT are periodically rebroadcast. JOB retries reuse its sequence;
 * the initiator caches its result so a lost RESULT doesn't start another FTM.
 * POINT fragments carry a snapshot sequence and membership identity. Receivers
 * commit only when every unique index is present. Old/reordered/duplicate
 * packets cannot mix layouts. Group IDs are NOT a security mechanism: use a
 * trusted LAN. We intentionally add no crypto/protocol/math dependencies. */
enum { HEADER=26, HELLO=1, JOB=2, RESULT=3, POINT=4, PACKET_MAX=96 };
static const size_t packet_sizes[]={0,HEADER+7,HEADER+17,HEADER+15,HEADER+44};
typedef struct {
    xs_id_t id;
    uint8_t ap[6],channel;
    uint32_t boot,ip;
    uint64_t seen;
} peer_t;
typedef struct {
    uint8_t type;
    xs_id_t sender,target;
    uint8_t ap[6],channel,rank;
    uint32_t boot,seq,target_boot,mm,age;
    uint16_t count,index;
    bool success,converged;
    xs_vec3_t position;
    float rms,max_error,ratio;
} message_t;
struct xs_cluster {
    xs_cluster_config_t cfg;
    xs_transport_t io;
    xs_id_t self;
    peer_t *peers;
    size_t count;
    xs_model_t *model;
    xs_id_t *ids;
    xs_vec3_t *positions;
    xs_node_t *layout,*staging;
    uint8_t *received;
    xs_quality_t quality,staging_quality;
    uint32_t sequence,layout_seq,staging_seq,range_token;
    uint64_t changed,last_hello,last_publish,layout_time,staging_time,overflow_until;
    bool hello_sent,publish_sent,valid,staging_active,model_ready;
    size_t stage_count;
    xs_state_t state;
    size_t pair_a,pair_b;
    bool reverse,job_active,local_active,cached;
    message_t job,local_job,cache;
    uint64_t job_deadline,job_sent,local_deadline;
    xs_id_t initiator;
    uint32_t local_leader_boot;
};

static uint16_t get16(const uint8_t *p) { return (uint16_t)((p[0]<<8)|p[1]); }
static uint32_t get32(const uint8_t *p)
{ return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
static void put16(uint8_t *p,uint16_t n) { p[0]=(uint8_t)(n>>8); p[1]=(uint8_t)n; }
static void put32(uint8_t *p,uint32_t n)
{ p[0]=(uint8_t)(n>>24); p[1]=(uint8_t)(n>>16); p[2]=(uint8_t)(n>>8); p[3]=(uint8_t)n; }
static bool same(xs_id_t a,xs_id_t b) { return !memcmp(a.mac,b.mac,6); }
static bool newer(uint32_t a,uint32_t b) { return a!=b && (uint32_t)(a-b)<0x80000000u; }
static bool valid_mac(xs_id_t id)
{
    static const uint8_t zero[6]={0};
    return !(id.mac[0]&1) && memcmp(id.mac,zero,6)!=0;
}
static size_t find_peer(const xs_cluster_t *c,xs_id_t id)
{
    for (size_t i=0;i<c->count;i++) if (same(c->peers[i].id,id)) return i;
    return SIZE_MAX;
}
static uint32_t next_sequence(xs_cluster_t *c)
{ if (!++c->sequence) ++c->sequence; return c->sequence; }
static bool leader(const xs_cluster_t *c) { return same(c->peers[0].id,c->self); }
static uint32_t mm(float d) { return (uint32_t)lroundf(d*1000); }
static float signed_mm(uint32_t u)
{
    /* Decode signed two's complement without implementation-defined u32->i32. */
    int64_t v=u<=INT32_MAX ? (int64_t)u : (int64_t)u-4294967296LL;
    return (float)v/1000;
}
static bool decode(message_t *m,const uint8_t *p,size_t len,uint32_t group)
{
    if (!p || len<HEADER || memcmp(p,"XSP1",4) || p[4]<HELLO || p[4]>POINT ||
        p[5] || get16(p+6)!=len || packet_sizes[p[4]]!=len || get32(p+8)!=group) return false;
    memset(m,0,sizeof(*m)); m->type=p[4]; memcpy(m->sender.mac,p+12,6);
    m->boot=get32(p+18); m->seq=get32(p+22);
    if (!m->boot || !valid_mac(m->sender)) return false;
    p+=HEADER;
    if (m->type==HELLO) {
        memcpy(m->ap,p,6); m->channel=p[6];
        xs_id_t ap; memcpy(ap.mac,p,6);
        return valid_mac(ap) && m->channel>=1 && m->channel<=14;
    }
    if (!m->seq) return false;
    if (m->type==JOB || m->type==RESULT) {
        memcpy(m->target.mac,p,6); m->target_boot=get32(p+6);
        if (!valid_mac(m->target) || !m->target_boot) return false;
        if (m->type==JOB) {
            memcpy(m->ap,p+10,6); m->channel=p[16];
            xs_id_t ap; memcpy(ap.mac,m->ap,6);
            return valid_mac(ap) && m->channel>=1 && m->channel<=14;
        }
        m->mm=get32(p+10); m->success=p[14]!=0;
        return p[14]<=1 && (!m->success || (m->mm>0 && m->mm<=10000000));
    }
    m->count=get16(p); m->index=get16(p+2); memcpy(m->target.mac,p+4,6);
    m->target_boot=get32(p+10);
    m->position=(xs_vec3_t){signed_mm(get32(p+14)),signed_mm(get32(p+18)),signed_mm(get32(p+22))};
    m->rms=get32(p+26)/1000.0f; m->max_error=get32(p+30)/1000.0f;
    m->rank=p[34]; m->converged=p[35]!=0; m->ratio=get32(p+36)/1000000.0f; m->age=get32(p+40);
    return m->count>=4 && m->index<m->count && valid_mac(m->target) && m->target_boot &&
        m->rank<=3 && p[35]<=1 && m->ratio<=1 && m->rms<=20000 && m->max_error<=20000 &&
        fabsf(m->position.x)<=20000 && fabsf(m->position.y)<=20000 && fabsf(m->position.z)<=20000;
}
static void send_message(xs_cluster_t *c,uint32_t ip,const message_t *m)
{
    uint8_t buffer[PACKET_MAX]={0}; uint8_t *p=buffer;
    memcpy(p,"XSP1",4); p[4]=m->type; put16(p+6,(uint16_t)packet_sizes[m->type]);
    put32(p+8,c->cfg.group); memcpy(p+12,c->self.mac,6);
    put32(p+18,c->peers[find_peer(c,c->self)].boot); put32(p+22,m->seq); p+=HEADER;
    if (m->type==HELLO) { memcpy(p,m->ap,6); p[6]=m->channel; }
    else if (m->type==JOB || m->type==RESULT) {
        memcpy(p,m->target.mac,6); put32(p+6,m->target_boot);
        if (m->type==JOB) { memcpy(p+10,m->ap,6); p[16]=m->channel; }
        else { put32(p+10,m->mm); p[14]=m->success; }
    } else {
        put16(p,m->count); put16(p+2,m->index); memcpy(p+4,m->target.mac,6); put32(p+10,m->target_boot);
        put32(p+14,(uint32_t)(int32_t)lroundf(m->position.x*1000));
        put32(p+18,(uint32_t)(int32_t)lroundf(m->position.y*1000));
        put32(p+22,(uint32_t)(int32_t)lroundf(m->position.z*1000));
        put32(p+26,mm(m->rms)); put32(p+30,mm(m->max_error));
        p[34]=m->rank; p[35]=m->converged; put32(p+36,(uint32_t)lroundf(m->ratio*1000000)); put32(p+40,m->age);
    }
    c->io.send(c->io.context,ip,buffer,packet_sizes[m->type]);
}
static void invalidate(xs_cluster_t *c,uint64_t now)
{
    if (c->local_active) c->io.cancel(c->io.context);
    c->local_active=c->job_active=c->cached=false;
    c->valid=c->staging_active=c->model_ready=false;
    c->layout_seq=c->staging_seq=0; c->changed=now;
    c->pair_a=0; c->pair_b=1; c->reverse=false;
    memset(&c->quality,0,sizeof(c->quality));
    c->state=c->count<4 ? XS_WAITING : XS_SETTLING;
}
xs_cluster_config_t xs_cluster_defaults(void)
{
    return (xs_cluster_config_t){16,0x584d4153u,2000,20000,5000,4000,750,5000,1800000,xs_default_options()};
}
xs_cluster_t *xs_cluster_create(const xs_cluster_config_t *cfg,const xs_transport_t *io,
    xs_id_t self,uint32_t boot,const uint8_t ap[6],uint8_t channel,uint64_t now)
{
    if (!cfg || !io || !io->send || !io->range || !io->cancel || !ap ||
        !valid_mac(self) || !boot || channel<1 || channel>14 || cfg->capacity<4 || cfg->capacity>UINT16_MAX ||
        cfg->hello_ms<100 || cfg->peer_timeout_ms<3ULL*cfg->hello_ms || !cfg->settle_ms ||
        cfg->range_timeout_ms<100 || !cfg->retry_ms || !cfg->publish_ms || !cfg->layout_ttl_ms ||
        !cfg->solver.min_samples || cfg->solver.min_samples>XS_WINDOW || !cfg->solver.max_age_ms ||
        !cfg->solver.iterations || !isfinite(cfg->solver.noise_floor_m) || cfg->solver.noise_floor_m<0.001f ||
        !isfinite(cfg->solver.huber_m) || cfg->solver.huber_m<=0) return NULL;
    xs_id_t ap_id; memcpy(ap_id.mac,ap,6); if (!valid_mac(ap_id)) return NULL;
    xs_cluster_t *c=calloc(1,sizeof(*c)); if (!c) return NULL;
    c->cfg=*cfg; c->io=*io; c->self=self; size_t n=cfg->capacity;
    c->peers=calloc(n,sizeof(peer_t)); c->ids=calloc(n,sizeof(xs_id_t));
    c->positions=calloc(n,sizeof(xs_vec3_t)); c->layout=calloc(n,sizeof(xs_node_t));
    c->staging=calloc(n,sizeof(xs_node_t)); c->received=calloc(n,1); c->model=xs_create(n);
    if (!c->peers || !c->ids || !c->positions || !c->layout || !c->staging || !c->received || !c->model) {
        xs_cluster_destroy(c); return NULL;
    }
    c->count=1; c->peers[0].id=self; c->peers[0].boot=boot;
    memcpy(c->peers[0].ap,ap,6); c->peers[0].channel=channel;
    invalidate(c,now); return c;
}
void xs_cluster_destroy(xs_cluster_t *c)
{
    if (!c) return;
    if (c->local_active) c->io.cancel(c->io.context);
    xs_destroy(c->model); free(c->peers); free(c->ids); free(c->positions);
    free(c->layout); free(c->staging); free(c->received); free(c);
}
void xs_cluster_rejoin(xs_cluster_t *c,uint32_t boot,uint8_t channel,uint64_t now)
{
    if (!c || !boot || channel<1 || channel>14) return;
    peer_t self=c->peers[find_peer(c,c->self)]; self.boot=boot; self.channel=channel;
    c->count=1; c->peers[0]=self; c->hello_sent=false; c->overflow_until=0;
    invalidate(c,now);
}
static void publish(xs_cluster_t *c,uint64_t now)
{
    if (!c->valid || now-c->layout_time>c->cfg.layout_ttl_ms) return;
    message_t m={.type=POINT,.seq=c->layout_seq,.count=(uint16_t)c->count,
        .rms=c->quality.rms_m,.max_error=c->quality.max_residual_m,.rank=(uint8_t)c->quality.rank,
        .converged=c->quality.converged,.ratio=c->quality.eigen_ratio,.age=(uint32_t)(now-c->layout_time)};
    for (size_t i=0;i<c->count;i++) {
        m.index=(uint16_t)i; m.target=c->layout[i].id; m.target_boot=c->peers[i].boot;
        m.position=c->layout[i].position; send_message(c,0,&m);
    }
    c->last_publish=now; c->publish_sent=true;
}
static void advance(xs_cluster_t *c,uint64_t now)
{
    c->job_active=false;
    if (++c->pair_b<c->count) return;
    c->pair_a++; c->pair_b=c->pair_a+1;
    if (c->pair_b<c->count) return;
    c->pair_a=0; c->pair_b=1; c->reverse=!c->reverse;
    xs_status_t status=xs_solve(c->model,now,&c->cfg.solver,c->positions,&c->quality);
    c->valid=status==XS_OK;
    if (c->valid) {
        for (size_t i=0;i<c->count;i++) c->layout[i]=(xs_node_t){c->peers[i].id,c->positions[i]};
        c->layout_time=now; c->layout_seq=next_sequence(c); c->state=XS_LAYOUT;
        publish(c,now);
    } else c->state=status==XS_INCOMPLETE ? XS_COLLECTING : XS_BAD_GEOMETRY;
}
static void accept_result(xs_cluster_t *c,const message_t *m,uint64_t now)
{
    if (!c->job_active || !leader(c) || m->seq!=c->job.seq || !same(m->sender,c->initiator) ||
        !same(m->target,c->job.target) || m->target_boot!=c->job.target_boot) return;
    if (m->success) xs_observe(c->model,c->pair_a,c->pair_b,m->mm/1000.0f,now);
    advance(c,now);
}
void xs_cluster_range_result(xs_cluster_t *c,uint32_t token,bool success,float d,uint64_t now)
{
    if (!c || !c->local_active || token!=c->range_token) return;
    c->local_active=false;
    message_t r=c->local_job; r.type=RESULT; r.sender=c->self;
    r.success=success && isfinite(d) && d>=0.001f && d<=10000;
    r.mm=r.success ? mm(d) : 0; c->cache=r; c->cached=true;
    if (leader(c)) accept_result(c,&r,now);
    else send_message(c,c->peers[0].ip,&r);
}
static void start_local(xs_cluster_t *c,const message_t *m,uint64_t now)
{
    if (c->cached && c->local_leader_boot==m->boot) {
        if (c->cache.seq==m->seq) { send_message(c,c->peers[0].ip,&c->cache); return; }
        if (!newer(m->seq,c->cache.seq)) return;
    }
    if (c->local_active) return;
    c->local_job=*m; c->local_active=true; c->local_leader_boot=m->boot;
    c->local_deadline=now+c->cfg.range_timeout_ms;
    if (!++c->range_token) ++c->range_token;
    size_t self=find_peer(c,c->self);
    /* S3 has one radio. APSTA uses the upstream station's channel. A mesh LAN
     * can span channels, but that does not make every off-channel FTM pairing
     * usable while keeping the association. Fail visibly; don't infer RSSI ranges. */
    if (m->channel!=c->peers[self].channel || !c->io.range(c->io.context,m->ap,m->channel,c->range_token))
        xs_cluster_range_result(c,c->range_token,false,0,now);
}
static void handle_hello(xs_cluster_t *c,uint32_t ip,const message_t *m,uint64_t now)
{
    if (same(m->sender,c->self)) return;
    size_t i=find_peer(c,m->sender);
    bool changed=false;
    if (i==SIZE_MAX) {
        if (c->count==c->cfg.capacity) {
            if (now>=c->overflow_until) invalidate(c,now);
            c->overflow_until=now+c->cfg.peer_timeout_ms; c->valid=false; c->state=XS_CAPACITY;
            return;
        }
        i=0; while (i<c->count && memcmp(c->peers[i].id.mac,m->sender.mac,6)<0) i++;
        memmove(c->peers+i+1,c->peers+i,(c->count-i)*sizeof(peer_t)); c->count++;
        memset(c->peers+i,0,sizeof(peer_t)); changed=true;
    }
    peer_t *p=&c->peers[i];
    changed=changed || p->boot!=m->boot || p->ip!=ip || p->channel!=m->channel || memcmp(p->ap,m->ap,6);
    p->id=m->sender; p->boot=m->boot; p->ip=ip; p->channel=m->channel; p->seen=now;
    memcpy(p->ap,m->ap,6);
    if (changed) invalidate(c,now);
}
static void handle_point(xs_cluster_t *c,const message_t *m,uint64_t now)
{
    if (leader(c) || !same(m->sender,c->peers[0].id) || m->count!=c->count ||
        m->count>c->cfg.capacity || m->age>c->cfg.layout_ttl_ms || m->age>now ||
        !same(c->peers[m->index].id,m->target) || c->peers[m->index].boot!=m->target_boot ||
        (c->layout_seq && !newer(m->seq,c->layout_seq))) return;
    if (!c->staging_active || m->seq!=c->staging_seq) {
        if (c->staging_active && !newer(m->seq,c->staging_seq)) return;
        c->staging_active=true; c->staging_seq=m->seq; c->stage_count=0;
        memset(c->received,0,c->count); c->staging_time=now-m->age;
        c->staging_quality=(xs_quality_t){.count=c->count,.measured_pairs=c->count*(c->count-1)/2,
            .required_pairs=c->count*(c->count-1)/2,.rank=m->rank,.converged=m->converged,
            .rms_m=m->rms,.max_residual_m=m->max_error,.eigen_ratio=m->ratio};
    }
    /* Metadata must describe the same immutable snapshot. Age alone changes
     * on retransmit. Keep the oldest inferred timestamp to avoid rejuvenation. */
    if (m->rank!=c->staging_quality.rank || m->rms!=c->staging_quality.rms_m ||
        m->max_error!=c->staging_quality.max_residual_m || m->ratio!=c->staging_quality.eigen_ratio ||
        m->converged!=c->staging_quality.converged) return;
    if (now-m->age<c->staging_time) c->staging_time=now-m->age;
    if (!c->received[m->index]) {
        c->received[m->index]=1; c->stage_count++;
        c->staging[m->index]=(xs_node_t){m->target,m->position};
    }
    if (c->stage_count==c->count) {
        memcpy(c->layout,c->staging,c->count*sizeof(xs_node_t));
        c->quality=c->staging_quality; c->layout_time=c->staging_time; c->layout_seq=m->seq;
        c->valid=true; c->state=XS_LAYOUT; c->staging_active=false;
    }
}
void xs_cluster_receive(xs_cluster_t *c,uint32_t ip,const uint8_t *data,size_t length,uint64_t now)
{
    message_t m;
    if (!c || !ip || !decode(&m,data,length,c->cfg.group)) return;
    if (m.type==HELLO) { handle_hello(c,ip,&m,now); return; }
    size_t i=find_peer(c,m.sender);
    if (i==SIZE_MAX || c->peers[i].boot!=m.boot || c->peers[i].ip!=ip || now<c->overflow_until) return;
    if (m.type==RESULT) accept_result(c,&m,now);
    else if (m.type==POINT) handle_point(c,&m,now);
    else if (m.type==JOB && i==0 && !leader(c) && now-c->changed>=c->cfg.settle_ms && c->count>=4) {
        size_t target=find_peer(c,m.target);
        if (target!=SIZE_MAX && !same(m.target,c->self) && c->peers[target].boot==m.target_boot &&
            !memcmp(c->peers[target].ap,m.ap,6) && c->peers[target].channel==m.channel) start_local(c,&m,now);
    }
}
void xs_cluster_tick(xs_cluster_t *c,uint64_t now)
{
    if (!c) return;
    for (size_t i=0;i<c->count;) {
        if (!same(c->peers[i].id,c->self) && now-c->peers[i].seen>c->cfg.peer_timeout_ms) {
            memmove(c->peers+i,c->peers+i+1,(c->count-i-1)*sizeof(peer_t)); c->count--; invalidate(c,now);
        } else i++;
    }
    if (!c->hello_sent || now-c->last_hello>=c->cfg.hello_ms) {
        size_t self=find_peer(c,c->self);
        message_t m={.type=HELLO,.channel=c->peers[self].channel}; memcpy(m.ap,c->peers[self].ap,6);
        send_message(c,0,&m); c->hello_sent=true; c->last_hello=now;
    }
    if (c->valid && now-c->layout_time>c->cfg.layout_ttl_ms) { c->valid=false; c->state=XS_COLLECTING; }
    if (now<c->overflow_until) { c->state=XS_CAPACITY; return; }
    if (c->state==XS_CAPACITY) invalidate(c,now);
    if (c->count<4 || now-c->changed<c->cfg.settle_ms) return;
    if (!c->model_ready) {
        for (size_t i=0;i<c->count;i++) c->ids[i]=c->peers[i].id;
        if (xs_reset(c->model,c->ids,c->count)!=XS_OK) return;
        c->model_ready=true; c->state=XS_COLLECTING;
    }
    if (c->local_active && now>=c->local_deadline) {
        c->io.cancel(c->io.context);
        xs_cluster_range_result(c,c->range_token,false,0,now);
    }
    if (!leader(c)) return;
    if (c->valid && (!c->publish_sent || now-c->last_publish>=c->cfg.publish_ms)) publish(c,now);
    if (c->job_active && now>=c->job_deadline) advance(c,now);
    if (!c->job_active) {
        size_t a=c->reverse?c->pair_b:c->pair_a,b=c->reverse?c->pair_a:c->pair_b;
        c->initiator=c->peers[a].id;
        c->job=(message_t){.type=JOB,.sender=c->self,.boot=c->peers[0].boot,.seq=next_sequence(c),
            .target=c->peers[b].id,.target_boot=c->peers[b].boot,.channel=c->peers[b].channel};
        memcpy(c->job.ap,c->peers[b].ap,6); c->job_active=true;
        c->job_deadline=now+c->cfg.range_timeout_ms+c->cfg.retry_ms*2ULL;
        c->job_sent=now;
        if (a==0) start_local(c,&c->job,now);
        else send_message(c,c->peers[a].ip,&c->job);
    } else if (!same(c->initiator,c->self) && now-c->job_sent>=c->cfg.retry_ms) {
        size_t i=find_peer(c,c->initiator);
        if (i!=SIZE_MAX) send_message(c,c->peers[i].ip,&c->job);
        c->job_sent=now;
    }
}
size_t xs_cluster_copy(xs_cluster_t *c,xs_node_t *nodes,size_t capacity,xs_info_t *info,uint64_t now)
{
    if (!c || !info) return 0;
    bool valid=c->valid && now-c->layout_time<=c->cfg.layout_ttl_ms && now>=c->overflow_until;
    *info=(xs_info_t){.state=c->state,.discovered=c->count,.count=valid?c->count:0,
        .coordinator=leader(c),.valid=valid,.version=c->layout_seq,
        .age_ms=c->valid?now-c->layout_time:0,.quality=c->quality};
    if (!valid && info->state==XS_LAYOUT) info->state=XS_COLLECTING;
    if (valid && nodes && capacity>=c->count) memcpy(nodes,c->layout,c->count*sizeof(xs_node_t));
    return valid?c->count:0;
}
