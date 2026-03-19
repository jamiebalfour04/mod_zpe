#include "httpd.h"
#include "http_protocol.h"
#include "http_config.h"
#include "http_request.h"
#include "http_core.h"
#include "apr_strings.h"
#include "apr_lib.h"
#include "apr_base64.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>

#define ZPEPM_HOST "127.0.0.1"
#define ZPEPM_PORT 8940

static int send_all(int sock, const char *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(sock, buf + sent, len - sent, 0);
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

static int read_all(int sock, char *buf, size_t len) {
    size_t got = 0;
    while (got < len) {
        ssize_t n = recv(sock, buf + got, len - got, 0);
        if (n <= 0) return -1;
        got += (size_t)n;
    }
    return 0;
}

static int zpepm_connect(void) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ZPEPM_PORT);

    if (inet_pton(AF_INET, ZPEPM_HOST, &addr.sin_addr) <= 0) {
        close(sock);
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }

    return sock;
}

static char *json_escape(apr_pool_t *p, const char *s) {
    if (s == NULL) return apr_pstrdup(p, "");

    size_t len = strlen(s);
    char *out = apr_palloc(p, len * 2 + 1);
    size_t j = 0;

    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        switch (c) {
            case '\\': out[j++] = '\\'; out[j++] = '\\'; break;
            case '"':  out[j++] = '\\'; out[j++] = '"';  break;
            case '\n': out[j++] = '\\'; out[j++] = 'n';  break;
            case '\r': out[j++] = '\\'; out[j++] = 'r';  break;
            case '\t': out[j++] = '\\'; out[j++] = 't';  break;
            default:   out[j++] = c; break;
        }
    }

    out[j] = '\0';
    return out;
}

static const char *get_script_type(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot || !*(dot + 1)) return NULL;
    return dot + 1;
}

static char *build_real_path(apr_pool_t *p, const char *document_root, const char *uri) {
    if (!document_root || !uri) return NULL;

    if (uri[0] == '/') {
        return apr_pstrcat(p, document_root, uri, NULL);
    } else {
        return apr_pstrcat(p, document_root, "/", uri, NULL);
    }
}

static char *json_get_string(apr_pool_t *p, const char *json, const char *key) {
    char *pattern = apr_psprintf(p, "\"%s\":\"", key);
    char *start = strstr(json, pattern);
    if (!start) return apr_pstrdup(p, "");

    start += strlen(pattern);
    char *end = start;

    while (*end) {
        if (*end == '"' && *(end - 1) != '\\') {
            break;
        }
        end++;
    }

    return apr_pstrndup(p, start, end - start);
}

static int json_get_bool(const char *json, const char *key) {
    char pattern_true[128];
    char pattern_false[128];

    snprintf(pattern_true, sizeof(pattern_true), "\"%s\":true", key);
    snprintf(pattern_false, sizeof(pattern_false), "\"%s\":false", key);

    if (strstr(json, pattern_true)) return 1;
    if (strstr(json, pattern_false)) return 0;
    return 0;
}

static char *base64_decode_to_pool(apr_pool_t *p, const char *b64) {
    if (!b64 || !*b64) return apr_pstrdup(p, "");

    int out_len = apr_base64_decode_len(b64);
    char *out = apr_palloc(p, out_len + 1);
    int actual = apr_base64_decode(out, b64);
    out[actual] = '\0';
    return out;
}

static int call_zpepm(request_rec *r, const char *document_root, const char *uri) {
    apr_pool_t *p = r->pool;

    char *real_path = build_real_path(p, document_root, uri);
    if (!real_path) {
        ap_rputs("Failed to resolve file path.\n", r);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    const char *script_type = get_script_type(real_path);
    if (!script_type) {
        ap_rputs("Could not determine script type.\n", r);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    char *path_escaped = json_escape(p, real_path);
    char *type_escaped = json_escape(p, script_type);

    char *request_json = apr_psprintf(
        p,
        "{"
        "\"id\":\"apache-request\","
        "\"mode\":\"file\","
        "\"path\":\"%s\","
        "\"script_type\":\"%s\","
        "\"timeout_ms\":5000,"
        "\"execution_profile\":\"web\","
        "\"stream\":false"
        "}",
        path_escaped,
        type_escaped
    );

    uint32_t req_len = (uint32_t)strlen(request_json);
    uint32_t req_len_be = htonl(req_len);

    int sock = zpepm_connect();
    if (sock < 0) {
        ap_rputs("Could not connect to ZPE-PM.\n", r);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    if (send_all(sock, (const char *)&req_len_be, 4) < 0 ||
        send_all(sock, request_json, req_len) < 0) {
        close(sock);
        ap_rputs("Failed to send request to ZPE-PM.\n", r);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    uint32_t resp_len_be;
    if (read_all(sock, (char *)&resp_len_be, 4) < 0) {
        close(sock);
        ap_rputs("Failed to read response length from ZPE-PM.\n", r);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    uint32_t resp_len = ntohl(resp_len_be);
    char *resp_json = apr_palloc(p, resp_len + 1);

    if (read_all(sock, resp_json, resp_len) < 0) {
        close(sock);
        ap_rputs("Failed to read response body from ZPE-PM.\n", r);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    resp_json[resp_len] = '\0';
    close(sock);

    int ok = json_get_bool(resp_json, "ok");
    char *output_b64 = json_get_string(p, resp_json, "output_b64");
    char *error_b64 = json_get_string(p, resp_json, "error_b64");

    char *output = base64_decode_to_pool(p, output_b64);
    char *error = base64_decode_to_pool(p, error_b64);

    if (!ok) {
        r->status = HTTP_INTERNAL_SERVER_ERROR;
        ap_rputs("<h1>ZPE-PM Execution Error</h1>\n", r);
        if (error && *error) {
            ap_rputs("<pre>", r);
            ap_rputs(error, r);
            ap_rputs("</pre>", r);
        }
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    if (output && *output) {
        ap_rputs(output, r);
    }

    return OK;
}

static int zpe_handler(request_rec *r) {
    if (strcmp(r->handler, "zpe")) {
        return DECLINED;
    }

    if (r->method_number != M_GET) {
        return HTTP_METHOD_NOT_ALLOWED;
    }

    r->content_type = "text/html; charset=utf-8";

    const char *document_root = ap_document_root(r);
    if (!document_root) {
        ap_rputs("Could not determine DocumentRoot.\n", r);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    return call_zpepm(r, document_root, r->uri);
}

static void register_hooks(apr_pool_t *pool) {
    ap_hook_handler(zpe_handler, NULL, NULL, APR_HOOK_MIDDLE);
}

module AP_MODULE_DECLARE_DATA zpe_module = {
    STANDARD20_MODULE_STUFF,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    register_hooks
};
