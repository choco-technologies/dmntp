#ifndef DMNTP_INTERNAL_H
#define DMNTP_INTERNAL_H
#include <errno.h>
#include "dmod.h"
#include "dmntp.h"
#include "dmntp_protocol.h"
#include "dmosi.h"
#define DMNTP_MAGIC 0x444D4E54U
#define DMNTP_MAX_SERVERS 8u
typedef struct dmntp_server {
    char* host;
    struct dmntp_server* next;
} dmntp_server_t;
struct dmntp {
    uint32_t magic;
    dmntp_server_t* servers;
    size_t server_count;
    dmosi_mutex_t lock;
};
typedef struct {
    double offset, delay, distance;
} dmntp_result_t;
uint64_t dmntp_ntp_to_unix(uint32_t seconds);
uint32_t dmntp_unix_to_ntp(uint64_t seconds);
uint32_t dmntp_frac_to_ns(uint32_t fraction);
uint32_t dmntp_ns_to_frac(uint32_t ns);
dmntp_timestamp_t dmntp_timestamp_add_ns(dmntp_timestamp_t ts, uint32_t ns);
void dmntp_packet_init_client(dmntp_packet_t* pkt);
int dmntp_packet_validate(const dmntp_packet_t* pkt, size_t size);
int dmntp_calculate_offset(const dmntp_timestamp_t* t1, const dmntp_timestamp_t* t2,
    const dmntp_timestamp_t* t3, const dmntp_timestamp_t* t4, double* offset, double* delay);
double dmntp_calculate_jitter(const double* offsets, size_t count);
void dmntp_packet_encode(const dmntp_packet_t* packet, uint8_t* bytes);
void dmntp_packet_decode(const uint8_t* bytes, dmntp_packet_t* packet);
int dmntp_select(const dmntp_result_t* samples, size_t count, double* offset, double* jitter);
void dmntp_clear_servers(dmntp_t handle);
int dmntp_config_init(dmntp_t handle);
dmntp_t dmntp_get_instance(void);
int dmntp_query_init(void);
void dmntp_query_deinit(void);
int dmntp_query_sync(dmntp_t handle, uint32_t timeout, uint64_t* seconds,
    uint32_t* ns, uint32_t* rtt, uint32_t* jitter);
#endif
