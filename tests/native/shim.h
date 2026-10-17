#ifndef NTP_SHIM_H
#define NTP_SHIM_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#define DMOD_GLOBAL_POINTER
#define dmod_dmntp_api_declaration(v,r,n,p) r dmntp##n p
#define Dmod_Malloc malloc
#define Dmod_Free free
#define Dmod_StrDup strdup
#define Dmod_GetEnv getenv
typedef uint64_t Dmod_Timestamp_t;
typedef struct { unsigned Size; } Dmod_Config_t;
uint64_t Dmod_GetUptime(void);
typedef pthread_mutex_t* dmosi_mutex_t;
typedef sem_t* dmosi_semaphore_t;
dmosi_mutex_t dmosi_mutex_create(bool recursive);
void dmosi_mutex_destroy(dmosi_mutex_t lock);
int dmosi_mutex_lock(dmosi_mutex_t lock);
int dmosi_mutex_unlock(dmosi_mutex_t lock);
dmosi_semaphore_t dmosi_semaphore_create(uint32_t initial, uint32_t max);
void dmosi_semaphore_destroy(dmosi_semaphore_t sem);
int dmosi_semaphore_wait(dmosi_semaphore_t sem, uint32_t count, int32_t timeout);
int dmosi_semaphore_post(dmosi_semaphore_t sem, uint32_t count);
typedef struct dmntp* dmntp_t;
dmntp_t dmntp_create(void);
void dmntp_destroy(dmntp_t h);
bool dmntp_is_valid(dmntp_t h);
int dmntp_add_server(dmntp_t h, const char* host);
int dmntp_remove_server(dmntp_t h, const char* host);
int dmntp_sync(uint32_t timeout, uint64_t* sec, uint32_t* ns, uint32_t* rtt, uint32_t* jitter);
int dmntp_sync_with(dmntp_t h, uint32_t timeout, uint64_t* sec, uint32_t* ns, uint32_t* rtt, uint32_t* jitter);
typedef enum { dmip_family_none, dmip_family_v4 } dmip_family_t;
typedef struct { dmip_family_t family; union { uint8_t v4[4]; } addr; } dmip_addr_t;
typedef void* dmnetif_iface_t;
typedef void (*dmudp_datagram_handler_t)(const dmip_addr_t*, uint16_t, uint16_t, dmnetif_iface_t, const uint8_t*, size_t);
int dmudp_bind_any(dmudp_datagram_handler_t handler, uint16_t* port);
void dmudp_unbind(uint16_t port);
int dmudp_send(const dmip_addr_t* server, uint16_t source, uint16_t dest, const void* bytes, size_t len, uint32_t timeout);
int dmdns_resolve(const char* host, dmip_family_t family, dmip_addr_t** addrs, size_t* count, uint32_t timeout);
#endif
