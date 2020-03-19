#ifndef DNS_SERVER_H
#define DNS_SERVER_H

#include <esp_system.h>
#include <esp_log.h>

#include <lwip/err.h>
#include <lwip/sockets.h>
#include <lwip/sys.h>
#include <lwip/netdb.h>

typedef enum {
    DNS_TYPE_A          = 1,
    DNS_TYPE_NS         = 2,
    DNS_TYPE_MD         = 3,
    DNS_TYPE_MF         = 4,
    DNS_TYPE_CNAME      = 5,
    DNS_TYPE_SOA        = 6,
    DNS_TYPE_MB         = 7,
    DNS_TYPE_MG         = 8,
    DNS_TYPE_MR         = 9,
    DNS_TYPE_NULL       = 10,
    DNS_TYPE_WKS        = 11,
    DNS_TYPE_PTR        = 12,
    DNS_TYPE_HINFO      = 13,
    DNS_TYPE_MINFO      = 14,
    DNS_TYPE_MX         = 15,
    DNS_TYPE_TXT        = 16,
    DNS_TYPE_RP         = 17,
    DNS_TYPE_AFSDB      = 18,
    DNS_TYPE_X25        = 19,
    DNS_TYPE_ISDN       = 20,
    DNS_TYPE_RT         = 21,
    DNS_TYPE_NSAP       = 22,
    DNS_TYPE_NSAP_PTR   = 23,
    DNS_TYPE_SIG        = 24,
    DNS_TYPE_KEY        = 25,
    DNS_TYPE_PX         = 26,
    DNS_TYPE_GPOS       = 27,
    DNS_TYPE_AAAA       = 28,
    DNS_TYPE_LOC        = 29,
    DNS_TYPE_NXT        = 30,
    DNS_TYPE_EID        = 31,
    DNS_TYPE_NB         = 32,
    DNS_TYPE_SRV        = 33,
    DNS_TYPE_ATMA       = 34,
    DNS_TYPE_NAPTR      = 35,
    DNS_TYPE_KX         = 36,
    DNS_TYPE_CERT       = 37,
    DNS_TYPE_A6         = 38,
    DNS_TYPE_DNAME      = 39,
    DNS_TYPE_SINK       = 40,
    DNS_TYPE_OPT        = 41,
    DNS_TYPE_APL        = 42,
    DNS_TYPE_DS         = 43,
    DNS_TYPE_SSHFP      = 44,
    DNS_TYPE_IPSECKEY   = 45,
    DNS_TYPE_RRSIG      = 46,
    DNS_TYPE_NSEC       = 47,
    DNS_TYPE_DNSKEY     = 48,
    DNS_TYPE_DHCID      = 49,
    DNS_TYPE_NSEC3      = 50,
    DNS_TYPE_NSEC3PARAM = 51,
    DNS_TYPE_TLSA       = 52,
    DNS_TYPE_HIP        = 55,
    DNS_TYPE_NINFO      = 56,
    DNS_TYPE_RKEY       = 57,
    DNS_TYPE_TALINK     = 58,
    DNS_TYPE_CHILD_DS   = 59,
    DNS_TYPE_SPF        = 99,
    DNS_TYPE_UINFO      = 100,
    DNS_TYPE_UID        = 101,
    DNS_TYPE_GID        = 102,
    DNS_TYPE_UNSPEC     = 103,
    DNS_TYPE_TKEY       = 249,
    DNS_TYPE_TSIG       = 250,
    DNS_TYPE_IXFR       = 251,
    DNS_TYPE_AXFR       = 252,
    DNS_TYPE_MAILB      = 253,
    DNS_TYPE_MAILA      = 254,
    DNS_TYPE_STAR       = 255,
    DNS_TYPE_URI        = 256,
    DNS_TYPE_CAA        = 257,
    DNS_TYPE_DNSSEC_TA  = 32768,
    DNS_TYPE_DNSSEC_LV  = 32769,
} dns_type_t;


void dns_write_u8(uint8_t **dst, uint8_t src);
void dns_write_u8s(uint8_t **dst, const uint8_t *src, size_t src_length);

void dns_write_u16n(uint8_t **dst, uint16_t src);
void dns_write_u16le(uint8_t **dst, uint16_t src);
void dns_write_u16be(uint8_t **dst, uint16_t src);

void dns_write_u32n(uint8_t **dst, uint32_t src);
void dns_write_u32le(uint8_t **dst, uint32_t src);
void dns_write_u32be(uint8_t **dst, uint32_t src);

void dns_write_name(uint8_t **dst, const char *src);

typedef bool (*dns_policy_t)(uint8_t **dst, const char *name, dns_type_t type, uint32_t *ttl);

esp_err_t dns_server_start(dns_policy_t fn);


#endif // DNS_SERVER_H
// vim: set sw=4 ts=4 indk= et si:
