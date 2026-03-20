#include "httpd.h"
#include "http_protocol.h"
#include "http_config.h"
#include "http_request.h"
#include "http_core.h"
#include "http_log.h"
#include "apr_strings.h"
#include "apr_lib.h"
#include "apr_base64.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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

/*
 * Very small JSON string extractor for simple flat JSON objects.
 * Supports:
 *   "key":"value"
 *   "key" : "value"
 */
 static char *json_get_string(apr_pool_t *p, const char *json, const char *key) {
    char *key_pos = strstr(json, key);
    if (!key_pos) return apr_pstrdup(p, "");

    char *colon = strchr(key_pos, ':');
    if (!colon) return apr_pstrdup(p, "");

    char *pos = colon + 1;
    while (*pos == ' ' || *pos == '\t') pos++;

    if (*pos != '"') return apr_pstrdup(p, "");
    pos++;

    char *start = pos;
    while (*pos) {
        if (*pos == '"' && *(pos - 1) != '\\') {
            break;
        }
        pos++;
    }

    return apr_pstrndup(p, start, pos - start);
}

/*
 * Supports:
 *   "ok":true
 *   "ok":"true"
 *   "ok" : true
 *   "ok" : "true"
 */
static int json_get_bool(const char *json, const char *key) {
    char pattern_true1[128];
    char pattern_true2[128];
    char pattern_true3[128];
    char pattern_true4[128];
    char pattern_false1[128];
    char pattern_false2[128];
    char pattern_false3[128];
    char pattern_false4[128];

    snprintf(pattern_true1, sizeof(pattern_true1), "\"%s\":true", key);
    snprintf(pattern_true2, sizeof(pattern_true2), "\"%s\":\"true\"", key);
    snprintf(pattern_true3, sizeof(pattern_true3), "\"%s\" : true", key);
    snprintf(pattern_true4, sizeof(pattern_true4), "\"%s\" : \"true\"", key);

    snprintf(pattern_false1, sizeof(pattern_false1), "\"%s\":false", key);
    snprintf(pattern_false2, sizeof(pattern_false2), "\"%s\":\"false\"", key);
    snprintf(pattern_false3, sizeof(pattern_false3), "\"%s\" : false", key);
    snprintf(pattern_false4, sizeof(pattern_false4), "\"%s\" : \"false\"", key);

    if (strstr(json, pattern_true1) || strstr(json, pattern_true2) ||
        strstr(json, pattern_true3) || strstr(json, pattern_true4)) {
        return 1;
    }

    if (strstr(json, pattern_false1) || strstr(json, pattern_false2) ||
        strstr(json, pattern_false3) || strstr(json, pattern_false4)) {
        return 0;
    }

    return 0;
}

static char *base64_decode_to_pool(apr_pool_t *p, const char *b64, int *decoded_len) {
    if (!b64 || !*b64) {
        *decoded_len = 0;
        return apr_pstrdup(p, "");
    }

    int max_len = apr_base64_decode_len(b64);
    char *out = apr_palloc(p, max_len + 1);

    *decoded_len = apr_base64_decode_binary((unsigned char *)out, b64);
    out[*decoded_len] = '\0';

    return out;
}

static int call_zpepm(request_rec *r, const char *document_root, const char *uri) {
    apr_pool_t *p = r->pool;

    char *real_path = build_real_path(p, document_root, uri);
    if (!real_path) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "mod_zpe: failed to resolve file path");
        ap_rputs("Failed to resolve file path.\n", r);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    const char *script_type = get_script_type(real_path);
    if (!script_type) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "mod_zpe: could not determine script type for path: %s", real_path);
        ap_rputs("Could not determine script type.\n", r);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "mod_zpe: real path: %s", real_path);
    ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "mod_zpe: script type: %s", script_type);

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

    ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "mod_zpe: request JSON: %s", request_json);

    uint32_t req_len = (uint32_t)strlen(request_json);
    uint32_t req_len_be = htonl(req_len);

    int sock = zpepm_connect();
    if (sock < 0) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "mod_zpe: could not connect to ZPE-PM at %s:%d", ZPEPM_HOST, ZPEPM_PORT);
        ap_rputs("Could not connect to ZPE-PM.\n", r);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    if (send_all(sock, (const char *)&req_len_be, 4) < 0 ||
        send_all(sock, request_json, req_len) < 0) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "mod_zpe: failed to send request to ZPE-PM");
        close(sock);
        ap_rputs("Failed to send request to ZPE-PM.\n", r);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    uint32_t resp_len_be;
    if (read_all(sock, (char *)&resp_len_be, 4) < 0) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "mod_zpe: failed to read response length from ZPE-PM");
        close(sock);
        ap_rputs("Failed to read response length from ZPE-PM.\n", r);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    uint32_t resp_len = ntohl(resp_len_be);
    char *resp_json = apr_palloc(p, resp_len + 1);

    if (read_all(sock, resp_json, resp_len) < 0) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "mod_zpe: failed to read response body from ZPE-PM");
        close(sock);
        ap_rputs("Failed to read response body from ZPE-PM.\n", r);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    resp_json[resp_len] = '\0';
    close(sock);

    ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "mod_zpe: raw response JSON: %s", resp_json);

    /*
     * Current ZPE-PM response shape:
     * {
     *   "output" : "<base64>",
     *   "id" : "apache-request",
     *   "ok" : "true",
     *   "error" : "",
     *   "exec_ms" : "71"
     * }
     */
     int ok = json_get_bool(resp_json, "ok");
     char *output_b64 = json_get_string(p, resp_json, "output");
     char *error = json_get_string(p, resp_json, "error");

     int decoded_len = 0;
     char *output = base64_decode_to_pool(p, output_b64, &decoded_len);

     ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "mod_zpe: parsed ok: %d", ok);
     ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "mod_zpe: parsed output field length: %ld", output_b64 ? (long)strlen(output_b64) : 0L);
     ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "mod_zpe: decoded output length: %d", decoded_len);

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

     if (decoded_len > 0) {
         ap_rwrite(output, decoded_len, r);
         ap_rflush(r);
     } else {
         ap_rputs("Decoded output is EMPTY", r);
     }

    return OK;
}

static int zpe_handler(request_rec *r) {
    if (strcmp(r->handler, "zpe")) {
        return DECLINED;
    }

    ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "mod_zpe: handling request for URI: %s", r->uri);

    if (r->method_number != M_GET) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "mod_zpe: method not allowed: %s", r->method);
        return HTTP_METHOD_NOT_ALLOWED;
    }

    r->content_type = "text/html; charset=utf-8";

    const char *document_root = ap_document_root(r);
    if (!document_root) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "mod_zpe: could not determine DocumentRoot");
        ap_rputs("Could not determine DocumentRoot.\n", r);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "mod_zpe: DocumentRoot: %s", document_root);

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
