#include <unos/tls.h>

#include <stddef.h>
#include <stdint.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/platform.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <unos/debug_log.h>
#include <unos/heap.h>
#include <unos/tcp.h>
#include <unos/tty.h>

#define TLS_MAX_CONNECTIONS 8u
#define TLS_HANDSHAKE_POLLS 220000u

typedef struct {
    int used;
    int handshake_complete;
    int tcp_sock;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_entropy_context entropy;
    mbedtls_x509_crt ca_chain;
} tls_socket_t;

static tls_socket_t tls_sockets[TLS_MAX_CONNECTIONS];
static int tls_initialized;
static const char mozilla_ca_bundle[] __attribute__((unused)) =
"-----BEGIN CERTIFICATE-----\n"
"MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw\n"
"TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh\n"
"cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4\n"
"WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu\n"
"ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY\n"
"MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc\n"
"h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+\n"
"0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U\n"
"A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW\n"
"T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH\n"
"B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC\n"
"B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv\n"
"KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn\n"
"OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn\n"
"jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw\n"
"qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI\n"
"rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV\n"
"HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq\n"
"hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL\n"
"ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ\n"
"3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK\n"
"NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5\n"
"ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur\n"
"TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC\n"
"jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc\n"
"oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq\n"
"4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA\n"
"mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d\n"
"emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=\n"
"-----END CERTIFICATE-----\n";

static void tls_debug_int(int value) {
    char buffer[16];
    size_t length = 0;
    int negative = value < 0;
    unsigned int current = negative ? (unsigned int)(-value) : (unsigned int)value;

    if (negative) {
        debug_log_puts("-");
    }
    if (current == 0) {
        debug_log_puts("0");
        return;
    }
    while (current != 0 && length < sizeof(buffer)) {
        buffer[length++] = (char)('0' + (current % 10u));
        current /= 10u;
    }
    while (length > 0) {
        char text[2] = { buffer[--length], '\0' };
        debug_log_puts(text);
    }
}

static void tls_log_error(const char *prefix, int error) {
    char error_text[96];
    mbedtls_strerror(error, error_text, sizeof(error_text));
    debug_log_puts(prefix);
    debug_log_puts(" ");
    tls_debug_int(error);
    debug_log_puts(" ");
    debug_log_puts(error_text);
    debug_log_puts("\n");
}

static void *tls_calloc(size_t count, size_t size) {
    if (size != 0 && count > ((size_t)-1) / size) {
        return NULL;
    }

    void *ptr = kmalloc(count * size);
    if (ptr == NULL) {
        return NULL;
    }

    uint8_t *bytes = ptr;
    for (size_t i = 0; i < count * size; ++i) {
        bytes[i] = 0;
    }

    return ptr;
}

static void tls_free(void *ptr) {
    kfree(ptr);
}

static int rdrand64(uint64_t *value) {
    uint8_t ok;

    for (uint32_t i = 0; i < 16u; ++i) {
        __asm__ volatile ("rdrand %0; setc %1" : "=r"(*value), "=qm"(ok) :: "cc");
        if (ok != 0) {
            return 0;
        }
    }

    return -1;
}

int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen) {
    (void)data;

    size_t written = 0;
    while (written < len) {
        uint64_t random = 0;
        if (rdrand64(&random) != 0) {
            *olen = written;
            return -1;
        }

        for (size_t i = 0; i < sizeof(random) && written < len; ++i) {
            output[written++] = (unsigned char)(random >> (i * 8u));
        }
    }

    *olen = written;
    return 0;
}

static void tls_init_once(void) {
    if (tls_initialized) {
        return;
    }

    for (size_t i = 0; i < TLS_MAX_CONNECTIONS; ++i) {
        tls_sockets[i].used = 0;
        tls_sockets[i].tcp_sock = -1;
    }

    mbedtls_platform_set_calloc_free(tls_calloc, tls_free);
    tls_initialized = 1;
}

static tls_socket_t *tls_from_handle(int sock) {
    if (sock < 0 || (size_t)sock >= TLS_MAX_CONNECTIONS || !tls_sockets[sock].used) {
        return NULL;
    }

    return &tls_sockets[sock];
}

static int tls_send_callback(void *ctx, const unsigned char *buf, size_t len) {
    tls_socket_t *socket = ctx;

    if (socket == NULL || tcp_send(socket->tcp_sock, buf, len) != 0) {
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    }

    return (int)len;
}

static int tls_recv_callback(void *ctx, unsigned char *buf, size_t len) {
    tls_socket_t *socket = ctx;
    size_t size = len;

    if (socket == NULL || tcp_recv(socket->tcp_sock, buf, &size) != 0) {
        return MBEDTLS_ERR_SSL_WANT_READ;
    }

    return (int)size;
}

int tls_connect(uint32_t ip, uint16_t port, const char *hostname) {
    tls_init_once();

    debug_log_puts("tls: connect begin ");
    debug_log_puts(hostname != NULL ? hostname : "(null)");
    debug_log_puts("\n");

    if (ip == 0 || port == 0 || hostname == NULL) {
        debug_log_puts("tls: invalid args\n");
        return -1;
    }

    tls_socket_t *socket = NULL;
    int handle = -1;
    for (size_t i = 0; i < TLS_MAX_CONNECTIONS; ++i) {
        if (!tls_sockets[i].used) {
            socket = &tls_sockets[i];
            handle = (int)i;
            break;
        }
    }

    if (socket == NULL) {
        debug_log_puts("tls: no socket slot\n");
        return -1;
    }

    socket->used = 1;
    socket->handshake_complete = 0;
    socket->tcp_sock = -1;
    mbedtls_ssl_init(&socket->ssl);
    mbedtls_ssl_config_init(&socket->conf);
    mbedtls_ctr_drbg_init(&socket->ctr_drbg);
    mbedtls_entropy_init(&socket->entropy);
    mbedtls_x509_crt_init(&socket->ca_chain);

    const unsigned char personalization[] = "vex-os tls";
    if (mbedtls_ctr_drbg_seed(
            &socket->ctr_drbg,
            mbedtls_entropy_func,
            &socket->entropy,
            personalization,
            sizeof(personalization) - 1u) != 0) {
        tty_puts("tls: ctr_drbg seed failed\n");
        debug_log_puts("tls: ctr_drbg seed failed\n");
        tls_close(handle);
        return -1;
    }

    if (mbedtls_ssl_config_defaults(
            &socket->conf,
            MBEDTLS_SSL_IS_CLIENT,
            MBEDTLS_SSL_TRANSPORT_STREAM,
            MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
        tty_puts("tls: config defaults failed\n");
        debug_log_puts("tls: config defaults failed\n");
        tls_close(handle);
        return -1;
    }

    mbedtls_ssl_conf_authmode(&socket->conf, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_rng(&socket->conf, mbedtls_ctr_drbg_random, &socket->ctr_drbg);

    if (mbedtls_ssl_setup(&socket->ssl, &socket->conf) != 0 ||
        mbedtls_ssl_set_hostname(&socket->ssl, hostname) != 0) {
        tty_puts("tls: ssl setup failed\n");
        debug_log_puts("tls: ssl setup failed\n");
        tls_close(handle);
        return -1;
    }

    socket->tcp_sock = tcp_connect(ip, port);
    if (socket->tcp_sock < 0) {
        tty_puts("tls: tcp_connect failed\n");
        debug_log_puts("tls: tcp_connect failed\n");
        tls_close(handle);
        return -1;
    }
    debug_log_puts("tls: tcp_connect ok\n");

    mbedtls_ssl_set_bio(&socket->ssl, socket, tls_send_callback, tls_recv_callback, NULL);

    for (uint32_t i = 0; i < TLS_HANDSHAKE_POLLS; ++i) {
        const int ret = mbedtls_ssl_handshake(&socket->ssl);
        if (ret == 0) {
            socket->handshake_complete = 1;
            return handle;
        }
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            tls_log_error("tls: handshake failed", ret);
            tls_close(handle);
            return -1;
        }
    }

    tty_puts("tls: handshake timeout\n");
    debug_log_puts("tls: handshake timeout\n");
    tls_close(handle);
    return -1;
}

int tls_send(int sock, const void *data, size_t size) {
    return tls_send_timeout(sock, data, size, TLS_HANDSHAKE_POLLS);
}

int tls_send_timeout(int sock, const void *data, size_t size, uint32_t timeout_polls) {
    tls_socket_t *socket = tls_from_handle(sock);
    if (socket == NULL || data == NULL) {
        return -1;
    }

    const unsigned char *bytes = data;
    size_t sent = 0;
    while (sent < size) {
        const int ret = mbedtls_ssl_write(&socket->ssl, bytes + sent, size - sent);
        if (ret > 0) {
            sent += (size_t)ret;
            continue;
        }
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            return -1;
        }
        if (timeout_polls == 0u) {
            return -1;
        }
        --timeout_polls;
    }

    return 0;
}

int tls_recv(int sock, void *buffer, size_t *size) {
    return tls_recv_timeout(sock, buffer, size, TLS_HANDSHAKE_POLLS);
}

int tls_recv_timeout(int sock, void *buffer, size_t *size, uint32_t timeout_polls) {
    tls_socket_t *socket = tls_from_handle(sock);
    if (socket == NULL || buffer == NULL || size == NULL) {
        return -1;
    }

    for (uint32_t i = 0; i < timeout_polls; ++i) {
        const int ret = mbedtls_ssl_read(&socket->ssl, buffer, *size);
        if (ret > 0) {
            *size = (size_t)ret;
            return 0;
        }
        if (ret == 0) {
            *size = 0u;
            return 0;
        }

        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            return -1;
        }
    }

    return -1;
}

int tls_can_read(int sock) {
    tls_socket_t *socket = tls_from_handle(sock);
    if (socket == NULL) {
        return 0;
    }
    if (mbedtls_ssl_get_bytes_avail(&socket->ssl) != 0u) {
        return 1;
    }
    if (socket->tcp_sock < 0) {
        return 0;
    }
    return tcp_can_read(socket->tcp_sock);
}

int tls_can_write(int sock) {
    tls_socket_t *socket = tls_from_handle(sock);
    if (socket == NULL || socket->tcp_sock < 0) {
        return 0;
    }
    return tcp_can_write(socket->tcp_sock);
}

int tls_is_closed(int sock) {
    tls_socket_t *socket = tls_from_handle(sock);
    if (socket == NULL) {
        return 1;
    }
    if (mbedtls_ssl_get_bytes_avail(&socket->ssl) != 0u) {
        return 0;
    }
    if (socket->tcp_sock < 0) {
        return 1;
    }
    return tcp_is_closed(socket->tcp_sock);
}

void tls_close(int sock) {
    tls_socket_t *socket = tls_from_handle(sock);
    if (socket == NULL) {
        return;
    }

    if (socket->tcp_sock >= 0 && socket->handshake_complete != 0) {
        (void)mbedtls_ssl_close_notify(&socket->ssl);
    }

    if (socket->tcp_sock >= 0) {
        tcp_close(socket->tcp_sock);
    }

    mbedtls_ssl_free(&socket->ssl);
    mbedtls_ssl_config_free(&socket->conf);
    mbedtls_ctr_drbg_free(&socket->ctr_drbg);
    mbedtls_entropy_free(&socket->entropy);
    mbedtls_x509_crt_free(&socket->ca_chain);

    socket->tcp_sock = -1;
    socket->handshake_complete = 0;
    socket->used = 0;
}
