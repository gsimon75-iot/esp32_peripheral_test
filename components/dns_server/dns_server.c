#include "dns_server.h"

#include <machine/endian.h>

static const char *TAG = "dns_server";

typedef enum {
    DNS_OPCODE_QUERY    = 0,
    DNS_OPCODE_IQUERY   = 1,
    DNS_OPCODE_STATUS   = 2,
    DNS_OPCODE_NOTIFY   = 4,
    DNS_OPCODE_UPDATE   = 5,
} dns_opcode_t;


typedef enum {
    DNS_RETCODE_NO_ERROR        = 0,
    DNS_RETCODE_FORMAT_ERROR    = 1,
    DNS_RETCODE_SERVER_FAILURE  = 2,
    DNS_RETCODE_NAME_ERROR      = 3,
    DNS_RETCODE_NOT_IMPLEMENTED = 4,
    DNS_RETCODE_REFUSED         = 5,
    DNS_RETCODE_YXDOMAIN        = 6,
    DNS_RETCODE_YXRRSET         = 7,
    DNS_RETCODE_NXRRSET         = 8,
    DNS_RETCODE_NOT_AUTH        = 9,
    DNS_RETCODE_NOT_ZONE        = 10,
} dns_retcode_t;

typedef enum {
    DNS_CLASS_IN        = 1,
    DNS_CLASS_CH        = 3,
    DNS_CLASS_HS        = 4,
    DNS_CLASS_NONE      = 254,
    DNS_CLASS_ANY       = 255,
} dns_class_t;



#if _BYTE_ORDER == _BIG_ENDIAN

#define htobe16(x)  (x)
#define be16toh(x)  (x)
#define htobe32(x)  (x)
#define be32toh(x)  (x)
#define htobe64(x)  (x)
#define be64toh(x)  (x)

#define htole16(x)  __bswap16(x)
#define le16toh(x)  __bswap16(x)
#define htole32(x)  __bswap32(x)
#define le32toh(x)  __bswap32(x)
#define htole64(x)  __bswap64(x)
#define le64toh(x)  __bswap64(x)

#else

#define htobe16(x)  __bswap16(x) 
#define be16toh(x)  __bswap16(x) 
#define htobe32(x)  __bswap32(x) 
#define be32toh(x)  __bswap32(x) 
#define htobe64(x)  __bswap64(x) 
#define be64toh(x)  __bswap64(x) 

#define htole16(x)  (x)
#define le16toh(x)  (x)
#define htole32(x)  (x)
#define le32toh(x)  (x)
#define htole64(x)  (x)
#define le64toh(x)  (x)

#endif

void
dns_write_u8(uint8_t **dst, uint8_t src) {
    **dst = src;
    (*dst)++;
}

void
dns_write_u8s(uint8_t **dst, const uint8_t *src, size_t src_length) {
    memcpy(*dst, src, src_length);
    *dst += src_length;
}


void
dns_write_u16n(uint8_t **dst, uint16_t src) {
    *(uint16_t*)(*dst) = htons(src);
    *dst += 2;
}

void
dns_write_u16le(uint8_t **dst, uint16_t src) {
    *(uint16_t*)(*dst) = htole16(src);
    *dst += 2;
}

void
dns_write_u16be(uint8_t **dst, uint16_t src) {
    *(uint16_t*)(*dst) = htobe16(src);
    *dst += 2;
}


void
dns_write_u32n(uint8_t **dst, uint32_t src) {
    *(uint32_t*)(*dst) = htonl(src);
    *dst += 4;
}

void
dns_write_u32le(uint8_t **dst, uint32_t src) {
    *(uint32_t*)(*dst) = htole32(src);
    *dst += 4;
}

void
dns_write_u32be(uint8_t **dst, uint32_t src) {
    *(uint32_t*)(*dst) = htobe32(src);
    *dst += 4;
}


void
dns_write_name(uint8_t **dst, const char *src) {
    // TODO: compress
    while (*src) {
        const char *dot_pos = strchrnul(src, '.');
        size_t label_length = dot_pos - src;
        assert(label_length < 64);
        dns_write_u8(dst, label_length);
        dns_write_u8s(dst, (const uint8_t*)src, label_length);
        if (*dot_pos == '\0') {
            break;
        }
        src = dot_pos + 1;
    }
    dns_write_u8(dst, 0);
}


typedef struct __attribute__((__packed__)) {
#if _BYTE_ORDER == _BIG_ENDIAN
    uint16_t    rcode:4;
    uint16_t    cd:1;
    uint16_t    ad:1;
    uint16_t    z:1;
    uint16_t    ra:1;
    uint16_t    rd:1;
    uint16_t    tc:1;
    uint16_t    aa:1;
    uint16_t    opcode:4;
    uint16_t    qr:1;
#else
    uint16_t    rd:1;
    uint16_t    tc:1;
    uint16_t    aa:1;
    uint16_t    opcode:4;
    uint16_t    qr:1;
    uint16_t    rcode:4;
    uint16_t    cd:1;
    uint16_t    ad:1;
    uint16_t    z:1;
    uint16_t    ra:1;
#endif
} dns_flags_t;


typedef struct __attribute__((__packed__)) {
    uint16_t    id;
    dns_flags_t flags;
    uint16_t    num_questions;
    uint16_t    num_answer_rrs;
    uint16_t    num_authority_rrs;
    uint16_t    num_additional_rrs;
} dns_header_t;


static void
dns_server_task(void *pvParameters) {
    dns_policy_t fn = (dns_policy_t)pvParameters;
    uint8_t data_buffer[512];
    dns_header_t *hdr = (dns_header_t*)data_buffer;
    char name[256];

    ESP_LOGI(TAG, "DNS server starting;");
    while (1) {
        struct sockaddr_in dest_addr;
        dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(53);

        int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to create socket; errno=%d", errno);
            break;
        }

        int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err < 0) {
            ESP_LOGE(TAG, "Socket unable to bind; errno=%d", errno);
        }

        while (1) {
            ESP_LOGI(TAG, "Waiting for data;");
            struct sockaddr_in6 source_addr; // Large enough for both IPv4 or IPv6

            socklen_t socklen = sizeof(source_addr);
            ssize_t rx_length = recvfrom(sock, data_buffer, sizeof(data_buffer), 0, (struct sockaddr *)&source_addr, &socklen);
            if (rx_length < 0) {
                ESP_LOGE(TAG, "Error receiving request; errno=%d", errno);
                break;
            }
            ESP_LOGI(TAG, "Received request; length=%d, sender_ip=0x%08x", rx_length, ntohl(((struct sockaddr_in *)&source_addr)->sin_addr.s_addr));
            ESP_LOG_BUFFER_HEXDUMP(TAG, data_buffer, rx_length, ESP_LOG_VERBOSE);

            size_t tx_length = sizeof(dns_header_t); // in case of error, only a header will be transmitted

            // save header fields that we'll overwrite for response
            uint16_t req_qr = hdr->flags.qr;
            uint16_t req_num_questions = ntohs(hdr->num_questions);

            // set up header for response
            hdr->flags.qr = 1;
            // leave .opcode unchanged
            hdr->flags.aa = 1;
            hdr->flags.tc = 0;
            hdr->flags.rd = 0;
            hdr->flags.ra = 0;
            hdr->flags.z = 0;
            hdr->flags.ad = 0;
            hdr->flags.cd = 0;
            // .rcode will be set on each case individually
            hdr->num_questions = hdr->num_answer_rrs = hdr->num_authority_rrs = hdr->num_additional_rrs = 0; // will be updated if needed

            if ((rx_length < sizeof(dns_header_t)) || (req_qr != 0) || (req_num_questions == 0)) {
                hdr->flags.rcode = DNS_RETCODE_FORMAT_ERROR;
                goto send;
            }
            uint8_t *data_end = data_buffer + rx_length;

            ESP_LOGD(TAG, "Parsed req header; id=0x%04x, opcode=%d", hdr->id, hdr->flags.opcode);

            // NOTE: Only the 0th question is served, the rest are ignored
            // https://stackoverflow.com/questions/4082081/requesting-a-and-aaaa-records-in-single-dns-query/4083071#4083071
            
            uint8_t *src = data_buffer + sizeof(dns_header_t);

            // parse the name of the 0th question (NOTE: it can't be compressed, there's nothing to backref to)
            uint8_t *dst = (uint8_t*)name;
            while (true) {
                uint8_t label_len = *(src++);
                if (label_len == 0)
                    break;
                if (   ((label_len & 0xc0) != 0x00)
                    || ((dst + label_len + 1) > (uint8_t*)(name + sizeof(name)))
                    || ((src + label_len + 1) > data_end)
                   ) {
                    hdr->flags.rcode = DNS_RETCODE_FORMAT_ERROR;
                    goto send;
                }
                memcpy(dst, src, label_len);
                src += label_len;
                dst += label_len;
                *(dst++) = '.';
            }
            *dst = '\0';

            if ((src + 4) > data_end) {
                hdr->flags.rcode = DNS_RETCODE_FORMAT_ERROR;
                goto send;
            }
            uint16_t qtype = ntohs(*(uint16_t*)src);
            src += 2;
            uint16_t qclass = ntohs(*(uint16_t*)src);
            src += 2;

            // from now on, question[0] will be returned even in error responses
            hdr->num_questions = htons(1);
            tx_length = src - data_buffer;

            if ((qclass != DNS_CLASS_IN) || (hdr->flags.opcode != DNS_OPCODE_QUERY)) {
                hdr->flags.rcode = DNS_RETCODE_NAME_ERROR;
                goto send;
            }


            ESP_LOGD(TAG, "Parsed question; name='%s', qtype=%d, qclass=%d", name, qtype, qclass);

            // write the answer rr header, because the policy fn must write the answer rdata *after* this
            *(src++) = '\xc0'; // name = backref to question[0].name
            *(src++) = sizeof(dns_header_t);
            *(uint16_t*)src = htons(qtype);
            src += 2;
            *(uint16_t*)src = htons(DNS_CLASS_IN);
            src += 2;

            uint32_t *ttl = (uint32_t*)src;
            *ttl = 0; // will be updated by policy fn
            src += 4;
            uint16_t *rdlength = (uint16_t*)src;
            *rdlength = 0; // will be updated after the policy answer
            src += 2;

            // the policy fn shall
            // - either set ttl, write rdata and return true
            // - or return false (and may write any junk, it'll be discarded)
            uint8_t *rdata = src;
            if (fn(&src, name, qtype, ttl)) {
                ESP_LOGD(TAG, "Policy answered the question;");
                *ttl = htonl(*ttl);
                *rdlength = htons(src - rdata);

                hdr->num_answer_rrs = htons(1);
                tx_length = src - data_buffer;

                hdr->flags.rcode = DNS_RETCODE_NO_ERROR;
            }
            else {
                ESP_LOGD(TAG, "Policy declined the question;");
                hdr->flags.rcode = DNS_RETCODE_NAME_ERROR;
                goto send;
            }

send:
            ESP_LOGD(TAG, "Sending response; len=%d", tx_length);
            ESP_LOG_BUFFER_HEXDUMP(TAG, data_buffer, tx_length, ESP_LOG_VERBOSE);
            err = sendto(sock, data_buffer, tx_length, 0, (struct sockaddr *)&source_addr, sizeof(source_addr));
            if (err < 0) {
                ESP_LOGE(TAG, "Error occurred during sending; errno=%d", errno);
                //break;
            }
        }

        if (sock != -1) {
            ESP_LOGE(TAG, "Shutting down socket and restarting...");
            shutdown(sock, 0);
            close(sock);
        }
    }
    vTaskDelete(NULL);
}


esp_err_t
dns_server_start(dns_policy_t fn) {
    TaskHandle_t dns_task;
    if (fn == NULL) {
        ESP_LOGE(TAG, "DNS policy fn missing;");
        return ESP_FAIL;
    }
    if (xTaskCreate(dns_server_task, TAG, 4096, (void*)fn, 5, &dns_task) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task; name='%s'", TAG);
        return ESP_FAIL;
    }
    return ESP_OK;
}

// vim: set sw=4 ts=4 indk= et si:
