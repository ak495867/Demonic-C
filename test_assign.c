#define _POSIX_C_SOURCE 200112L
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>
#define NSCLOSE closesocket
#else
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#define NSCLOSE close
#endif

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#ifndef _WIN32
#include <regex.h>
#endif

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef DMC_SQLITE
typedef struct sqlite3 sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;
extern int sqlite3_open(const char *, sqlite3 **);
extern int sqlite3_close(sqlite3 *);
extern int sqlite3_prepare_v2(sqlite3 *, const char *, int, sqlite3_stmt **, const char **);
extern int sqlite3_step(sqlite3_stmt *);
extern int sqlite3_finalize(sqlite3_stmt *);
extern int sqlite3_column_count(sqlite3_stmt *);
extern const unsigned char *sqlite3_column_text(sqlite3_stmt *, int);
#define SQLITE_OK 0
#define SQLITE_ROW 100
#endif

static unsigned char **_dmc_mem = NULL;
static size_t *_dmc_mem_sz = NULL;
static int _dmc_mem_cap = 0;
static FILE **_dmc_files = NULL;
static int _dmc_files_cap = 0;
static int _dmc_next = 1;
static int __dmc_argc = 0;
static char **__dmc_argv = NULL;
static int _dmc_vec_next = 1;
static int _dmc_queue_next = 1;
static int _dmc_map_next = 1;
static int _dmc_arena_next = 1;
static int _dmc_db_next = 1;
static int _dmc_regex_next = 1;
const char *arg_text(long long i) { if (!__dmc_argv || i < 0 || i >= __dmc_argc) return ""; return __dmc_argv[i]; }

static int _dmc_resize_mem(int need) {
  if (need <= _dmc_mem_cap) return 0;
  int new_cap = _dmc_mem_cap ? _dmc_mem_cap * 2 : 128;
  if (new_cap < need) new_cap = need;
  unsigned char **nm = (unsigned char**)realloc(_dmc_mem, (size_t)new_cap * sizeof(unsigned char*));
  size_t *nsz = (size_t*)realloc(_dmc_mem_sz, (size_t)new_cap * sizeof(size_t));
  if (!nm || !nsz) return -1;
  for (int i = _dmc_mem_cap; i < new_cap; ++i) { nm[i] = NULL; nsz[i] = 0; }
  _dmc_mem = nm; _dmc_mem_sz = nsz; _dmc_mem_cap = new_cap; return 0;
}
static int _dmc_resize_files(int need) {
  if (need <= _dmc_files_cap) return 0;
  int new_cap = _dmc_files_cap ? _dmc_files_cap * 2 : 128;
  if (new_cap < need) new_cap = need;
  FILE **nf = (FILE**)realloc(_dmc_files, (size_t)new_cap * sizeof(FILE*));
  if (!nf) return -1;
  for (int i = _dmc_files_cap; i < new_cap; ++i) { nf[i] = NULL; }
  _dmc_files = nf; _dmc_files_cap = new_cap; return 0;
}
static int _dmc_ensure(int h) {
  if (h < 0) return -1;
  if (h >= _dmc_mem_cap) { if (_dmc_resize_mem(h + 1) != 0) return -1; }
  if (h >= _dmc_files_cap) { if (_dmc_resize_files(h + 1) != 0) return -1; }
  return 0;
}

int mem_alloc(long long n) {
  if (n <= 0 || n > 1073741824LL) return -1;
  int h = _dmc_next++;
  if (_dmc_ensure(h) != 0) return -1;
  _dmc_mem[h] = (unsigned char*)calloc((size_t)n, 1); _dmc_mem_sz[h] = (size_t)n; return h;
}
int mem_free(long long h) {
  if (h < 0 || h >= _dmc_mem_cap || !_dmc_mem[h]) return -1;
  free(_dmc_mem[h]); _dmc_mem[h] = NULL; _dmc_mem_sz[h] = 0; return 0;
}
int mem_size(long long h) { if (h < 0 || h >= _dmc_mem_cap) return -1; return (int)_dmc_mem_sz[h]; }
int mem_write(long long h, long long i, long long v) {
  if (h < 0 || h >= _dmc_mem_cap || !_dmc_mem[h] || i < 0 || (size_t)i >= _dmc_mem_sz[h]) return -1;
  _dmc_mem[h][i] = (unsigned char)v; return 0;
}
int mem_read(long long h, long long i) {
  if (h < 0 || h >= _dmc_mem_cap || !_dmc_mem[h] || i < 0 || (size_t)i >= _dmc_mem_sz[h]) return -1;
  return _dmc_mem[h][i];
}
int mem_map(void *addr, long long sz) { int h = _dmc_next++; if (_dmc_ensure(h) != 0) return -1; _dmc_mem[h] = (unsigned char*)addr; _dmc_mem_sz[h] = (size_t)sz; return h; }
int mem_unmap(long long h) { if (h < 0 || h >= _dmc_mem_cap) return -1; _dmc_mem[h] = NULL; _dmc_mem_sz[h] = 0; return 0; }

int file_open(const char *p, const char *m) {
  if (!p || !m) return -1;
  int h = _dmc_next++; if (_dmc_ensure(h) != 0) return -1;
  _dmc_files[h] = fopen(p, m); return _dmc_files[h] ? h : -1;
}
const char *file_read(long long h) {
  static char b[65536];
  if (h < 0 || h >= _dmc_files_cap || !_dmc_files[h]) return "";
  size_t n = fread(b, 1, sizeof(b)-1, _dmc_files[h]);
  if (ferror(_dmc_files[h])) return "";
  b[n] = 0; return b;
}
int file_write(long long h, const char *s) {
  if (h < 0 || h >= _dmc_files_cap || !_dmc_files[h] || !s) return -1;
  int n = fprintf(_dmc_files[h], "%s", s); fflush(_dmc_files[h]); return n;
}
int file_close(long long h) {
  if (h < 0 || h >= _dmc_files_cap || !_dmc_files[h]) return -1;
  int r = fclose(_dmc_files[h]); _dmc_files[h] = NULL; return r == 0 ? 0 : -1;
}

#ifdef _WIN32
int tcp_connect(const char *host, long long port) { (void)host; (void)port; return -1; }
int tcp_send(long long s, const char *text) { (void)s; (void)text; return -1; }
const char *tcp_recv(long long s, long long max) { (void)s; (void)max; return ""; }
int tcp_close(long long s) { (void)s; return -1; }
#else
int tcp_connect(const char *host, long long port) { char svc[16]; snprintf(svc, sizeof(svc), "%lld", port); struct addrinfo hints = {0}, *res = NULL; hints.ai_socktype = SOCK_STREAM; if (getaddrinfo(host, svc, &hints, &res) != 0) return -1; int s = (int)socket(res->ai_family, res->ai_socktype, res->ai_protocol); if (s >= 0 && connect(s, res->ai_addr, res->ai_addrlen) < 0) { close(s); s = -1; } freeaddrinfo(res); return s; }
int tcp_send(long long s, const char *text) { return (int)send((int)s, text, (int)strlen(text), 0); }
const char *tcp_recv(long long s, long long max) { static char b[65536]; if (max > 65535) max = 65535; int n = (int)recv((int)s, b, (int)max, 0); if (n < 0) n = 0; b[n] = 0; return b; }
int tcp_close(long long s) { close((int)s); return 0; }
#endif

typedef struct { long long *data; long long len; long long cap; } _dmc_vec;
typedef struct { long long cap; long long len; long long head; long long tail; long long *data; } _dmc_queue;
typedef struct _dmc_map_node { long long key; long long value; struct _dmc_map_node *next; } _dmc_map_node;
typedef struct { long long cap; long long used; _dmc_map_node **buckets; } _dmc_map;
static _dmc_vec **_dmc_vecs = NULL;
static _dmc_queue **_dmc_queues = NULL;
static _dmc_map **_dmc_maps = NULL;
static int _dmc_vec_cap = 0;
static int _dmc_queue_cap = 0;
static int _dmc_map_cap = 0;

static int _dmc_resize_vec(int need) {
  if (need <= _dmc_vec_cap) return 0;
  int new_cap = _dmc_vec_cap ? _dmc_vec_cap * 2 : 64;
  if (new_cap < need) new_cap = need;
  _dmc_vec **nv = (_dmc_vec**)realloc(_dmc_vecs, (size_t)new_cap * sizeof(_dmc_vec*));
  if (!nv) return -1; for (int i = _dmc_vec_cap; i < new_cap; ++i) nv[i] = NULL;
  _dmc_vecs = nv; _dmc_vec_cap = new_cap; return 0;
}
static int _dmc_resize_queue(int need) {
  if (need <= _dmc_queue_cap) return 0;
  int new_cap = _dmc_queue_cap ? _dmc_queue_cap * 2 : 64;
  if (new_cap < need) new_cap = need;
  _dmc_queue **nq = (_dmc_queue**)realloc(_dmc_queues, (size_t)new_cap * sizeof(_dmc_queue*));
  if (!nq) return -1; for (int i = _dmc_queue_cap; i < new_cap; ++i) nq[i] = NULL;
  _dmc_queues = nq; _dmc_queue_cap = new_cap; return 0;
}
static int _dmc_resize_map(int need) {
  if (need <= _dmc_map_cap) return 0;
  int new_cap = _dmc_map_cap ? _dmc_map_cap * 2 : 64;
  if (new_cap < need) new_cap = need;
  _dmc_map **nm = (_dmc_map**)realloc(_dmc_maps, (size_t)new_cap * sizeof(_dmc_map*));
  if (!nm) return -1; for (int i = _dmc_map_cap; i < new_cap; ++i) nm[i] = NULL;
  _dmc_maps = nm; _dmc_map_cap = new_cap; return 0;
}
int vec_new(void) { _dmc_vec *v = (_dmc_vec*)calloc(1, sizeof(_dmc_vec)); if (!v) return -1; int h = _dmc_vec_next++; if (_dmc_resize_vec(h) != 0) return -1; _dmc_vecs[h] = v; return h; }
int vec_len(long long h) { if (h < 0 || h >= _dmc_vec_cap || !_dmc_vecs[h]) return -1; return (int)_dmc_vecs[h]->len; }
int vec_push(long long h, long long x) { _dmc_vec *v = _dmc_vecs[h]; if (!v) return -1; if (v->len == v->cap) { long long nc = v->cap ? v->cap * 2 : 4; v->data = (long long*)realloc(v->data, (size_t)nc * sizeof(long long)); v->cap = nc; } v->data[v->len++] = x; return 0; }
int vec_get(long long h, long long i) { _dmc_vec *v = _dmc_vecs[h]; if (!v || i < 0 || i >= v->len) return -1; return (int)v->data[i]; }
int vec_set(long long h, long long i, long long x) { _dmc_vec *v = _dmc_vecs[h]; if (!v || i < 0 || i >= v->len) return -1; v->data[i] = x; return 0; }
int vec_free(long long h) { _dmc_vec *v = _dmc_vecs[h]; if (!v) return -1; free(v->data); free(v); _dmc_vecs[h] = NULL; return 0; }

int queue_new(void) { _dmc_queue *q = (_dmc_queue*)calloc(1, sizeof(_dmc_queue)); if (!q) return -1; int h = _dmc_queue_next++; if (_dmc_resize_queue(h) != 0) return -1; _dmc_queues[h] = q; return h; }
int queue_len(long long h) { if (h < 0 || h >= _dmc_queue_cap || !_dmc_queues[h]) return -1; return (int)_dmc_queues[h]->len; }
int queue_push(long long h, long long x) { _dmc_queue *q = _dmc_queues[h]; if (!q) return -1; if (q->len == q->cap) { long long nc = q->cap ? q->cap * 2 : 4; q->data = (long long*)realloc(q->data, (size_t)nc * sizeof(long long)); q->cap = nc; } q->data[q->tail] = x; q->tail = (q->tail + 1) % q->cap; ++q->len; return 0; }
int queue_pop(long long h) { _dmc_queue *q = _dmc_queues[h]; if (!q || q->len == 0) return -1; long long x = q->data[q->head]; q->head = (q->head + 1) % q->cap; --q->len; return (int)x; }
int queue_peek(long long h) { _dmc_queue *q = _dmc_queues[h]; if (!q || q->len == 0) return -1; return (int)q->data[q->head]; }
int queue_free(long long h) { _dmc_queue *q = _dmc_queues[h]; if (!q) return -1; free(q->data); free(q); _dmc_queues[h] = NULL; return 0; }

int map_new(void) { _dmc_map *m = (_dmc_map*)calloc(1, sizeof(_dmc_map)); if (!m) return -1; int h = _dmc_map_next++; if (_dmc_resize_map(h) != 0) return -1; _dmc_maps[h] = m; return h; }
int map_set(long long h, long long key, long long value) {
  _dmc_map *m = _dmc_maps[h]; if (!m) return -1;
  if (!m->buckets) { m->cap = 8; m->buckets = (_dmc_map_node**)calloc((size_t)m->cap, sizeof(_dmc_map_node*)); }
  unsigned long long k = (unsigned long long)key; long long idx = (long long)(k % (unsigned long long)m->cap);
  _dmc_map_node *n = m->buckets[idx]; while (n) { if (n->key == key) { n->value = value; return 0; } n = n->next; }
  n = (_dmc_map_node*)malloc(sizeof(_dmc_map_node)); n->key = key; n->value = value; n->next = m->buckets[idx]; m->buckets[idx] = n; ++m->used; return 0;
}
int map_get(long long h, long long key) { _dmc_map *m = _dmc_maps[h]; if (!m || !m->buckets) return -1; long long idx = (long long)((unsigned long long)key % (unsigned long long)m->cap); _dmc_map_node *n = m->buckets[idx]; while (n) { if (n->key == key) return (int)n->value; n = n->next; } return -1; }
int map_has(long long h, long long key) { _dmc_map *m = _dmc_maps[h]; if (!m || !m->buckets) return 0; long long idx = (long long)((unsigned long long)key % (unsigned long long)m->cap); _dmc_map_node *n = m->buckets[idx]; while (n) { if (n->key == key) return 1; n = n->next; } return 0; }
int map_len(long long h) { _dmc_map *m = _dmc_maps[h]; if (!m) return -1; return (int)m->used; }
int map_free(long long h) { _dmc_map *m = _dmc_maps[h]; if (!m) return -1; if (m->buckets) { for (long long i = 0; i < m->cap; ++i) { _dmc_map_node *n = m->buckets[i]; while (n) { _dmc_map_node *d = n; n = n->next; free(d); } } free(m->buckets); } free(m); _dmc_maps[h] = NULL; return 0; }

int math_abs(long long x) { return x < 0 ? -x : x; }
long long math_min(long long a, long long b) { return a < b ? a : b; }
long long math_max(long long a, long long b) { return a > b ? a : b; }
long long math_clamp(long long x, long long lo, long long hi) { return x < lo ? lo : x > hi ? hi : x; }
double math_sin(double x) { return sin(x); }
double math_cos(double x) { return cos(x); }
double math_tan(double x) { return tan(x); }
double math_sqrt(double x) { return sqrt(x); }
double math_floor(double x) { return floor(x); }
double math_ceil(double x) { return ceil(x); }
double math_abs_f(double x) { return fabs(x); }
double math_pi(void) { return 3.14159265358979323846; }
double math_e(void) { return 2.71828182845904523536; }
long long text_len(const char *s) { return (long long)strlen(s); }
const char *text_concat(const char *a, const char *b) { static char buf[131072]; snprintf(buf, sizeof(buf), "%s%s", a, b); return buf; }
const char *text_sub(const char *s, long long start, long long len) { static char buf[65536]; long long slen = (long long)strlen(s); if (start < 0) start = 0; if (start >= slen) { buf[0] = 0; return buf; } if (start + len > slen) len = slen - start; memcpy(buf, s + start, (size_t)len); buf[len] = 0; return buf; }
long long text_char_at(const char *s, long long i) { if (i < 0 || i >= (long long)strlen(s)) return -1; return (unsigned char)s[i]; }
long long text_cmp(const char *a, const char *b) { return strcmp(a, b); }
const char *text_from_int(long long x) { static char buf[32]; snprintf(buf, sizeof(buf), "%lld", x); return buf; }
const char *text_from_float(double x) { static char buf[64]; snprintf(buf, sizeof(buf), "%g", x); return buf; }
long long text_to_int(const char *s) { return atoll(s); }

static char _dmc_json_value[65536];
const char *json_parse(const char *s) { return s ? s : ""; }
const char *json_stringify(const char *s) { return s ? s : ""; }
const char *json_get(const char *json, const char *key) { _dmc_json_value[0] = 0; if (!json || !key) return _dmc_json_value; char needle[512]; snprintf(needle, sizeof(needle), "\"%s\"", key); const char *p = strstr(json, needle); if (!p) return _dmc_json_value; p = strchr(p + strlen(needle), ':'); if (!p) return _dmc_json_value; ++p; while (*p == ' ' || *p == '\\t') ++p; if (*p == '\"') { ++p; size_t n = 0; while (p[n] && p[n] != '\"' && n + 1 < sizeof(_dmc_json_value)) { _dmc_json_value[n] = p[n]; ++n; } _dmc_json_value[n] = 0; return _dmc_json_value; } size_t n = 0; while (p[n] && p[n] != ',' && p[n] != '}' && n + 1 < sizeof(_dmc_json_value)) { _dmc_json_value[n] = p[n]; ++n; } while (n && (_dmc_json_value[n - 1] == ' ' || _dmc_json_value[n - 1] == '\\n')) --n; _dmc_json_value[n] = 0; return _dmc_json_value; }

static char _dmc_http_buffer[1048576];
static int _dmc_http_url_ok(const char *url) { if (!url || !*url || strstr(url, "\"") || strstr(url, "'") || strstr(url, "&&") || strstr(url, ";") || strstr(url, "|") || strstr(url, "`")) return 0; return 1; }
const char *http_get(const char *url) { _dmc_http_buffer[0] = 0; if (!_dmc_http_url_ok(url)) return _dmc_http_buffer; char command[2048]; snprintf(command, sizeof(command), "curl -LfsS --max-time 20 -- %s", url); FILE *pipe = popen(command, "r"); if (!pipe) return _dmc_http_buffer; size_t used = 0; while (used + 1 < sizeof(_dmc_http_buffer)) { size_t n = fread(_dmc_http_buffer + used, 1, sizeof(_dmc_http_buffer) - used - 1, pipe); used += n; if (!n) break; } _dmc_http_buffer[used] = 0; pclose(pipe); return _dmc_http_buffer; }
long long http_status(const char *url) { if (!_dmc_http_url_ok(url)) return -1; char command[2048]; snprintf(command, sizeof(command), "curl -Lso /dev/null -w %%{http_code} --max-time 20 -- %s", url); FILE *pipe = popen(command, "r"); if (!pipe) return -1; char status[16] = {0}; fgets(status, sizeof(status), pipe); pclose(pipe); long long value = atoll(status); return value == 0 ? -1 : value; }
long long datetime_now(void) { return (long long)time(NULL); }
const char *datetime_format(long long value, const char *format) { static char buf[128]; time_t t = (time_t)value; struct tm *tmv = localtime(&t); if (!tmv) { buf[0] = 0; return buf; } strftime(buf, sizeof(buf), format ? format : "%Y-%m-%d %H:%M:%S", tmv); return buf; }
long long datetime_unix(void) { return (long long)time(NULL); }
#ifndef _WIN32
int regex_new(const char *pattern) { regex_t *r = (regex_t*)malloc(sizeof(regex_t)); if (!r || regcomp(r, pattern ? pattern : "", REG_EXTENDED) != 0) { free(r); return -1; } int h = _dmc_next++; _dmc_mem[h] = (unsigned char*)r; _dmc_mem_sz[h] = 0; return h; }
int regex_match(long long h, const char *text) { regex_t *r = (regex_t*)_dmc_mem[h]; return r && text && regexec(r, text, 0, NULL, 0) == 0; }
int regex_free(long long h) { regex_t *r = (regex_t*)_dmc_mem[h]; if (!r) return -1; regfree(r); free(r); _dmc_mem[h] = NULL; return 0; }
#else
int regex_new(const char *pattern) { (void)pattern; return -1; }
int regex_match(long long h, const char *text) { (void)h; (void)text; return 0; }
int regex_free(long long h) { (void)h; return -1; }
#endif

#ifdef DMC_SQLITE
static sqlite3 *_dmc_db[1024];
static char _dmc_db_buffer[1048576];
int db_connect(const char *url) { if (!url || strncmp(url, "sqlite://", 9) != 0) return -1; sqlite3 *db = NULL; if (sqlite3_open(url + 9, &db) != SQLITE_OK) { sqlite3_close(db); return -1; } int h = _dmc_next++; _dmc_db[h] = db; return h; }
const char *db_query(long long connection, const char *sql) { _dmc_db_buffer[0] = 0; if (connection <= 0 || connection >= 1024 || !_dmc_db[connection] || !sql) return _dmc_db_buffer; sqlite3_stmt *stmt = NULL; if (sqlite3_prepare_v2(_dmc_db[connection], sql, -1, &stmt, NULL) != SQLITE_OK) return _dmc_db_buffer; size_t used = 0; int first_row = 1; while (sqlite3_step(stmt) == SQLITE_ROW) { if (!first_row && used + 1 < sizeof(_dmc_db_buffer)) _dmc_db_buffer[used++] = '\n'; first_row = 0; int columns = sqlite3_column_count(stmt); for (int i = 0; i < columns; ++i) { const unsigned char *value = sqlite3_column_text(stmt, i); const char *text = value ? (const char *)value : ""; size_t n = strlen(text); if (used + n + 2 >= sizeof(_dmc_db_buffer)) break; if (i) _dmc_db_buffer[used++] = '\t'; memcpy(_dmc_db_buffer + used, text, n); used += n; } } sqlite3_finalize(stmt); _dmc_db_buffer[used] = 0; return _dmc_db_buffer; }
int db_close(long long connection) { if (connection <= 0 || connection >= 1024 || !_dmc_db[connection]) return -1; sqlite3_close(_dmc_db[connection]); _dmc_db[connection] = NULL; return 0; }
#else
int db_connect(const char *url) { (void)url; return -1; }
const char *db_query(long long connection, const char *sql) { (void)connection; (void)sql; return ""; }
int db_close(long long connection) { (void)connection; return -1; }
#endif
double text_to_float(const char *s) { return atof(s); }
const char *string_copy(const char *s) { if (!s) return ""; size_t n = strlen(s); char *b = (char*)malloc(n + 1); if (n) memcpy(b, s, n); b[n] = 0; return b; }
const char *string_slice(const char *s, long long start, long long len) { if (!s) return ""; long long slen = (long long)strlen(s); if (start < 0) start = 0; if (start >= slen) return string_copy(""); if (start + len > slen) len = slen - start; char *b = (char*)malloc((size_t)len + 1); if (len) memcpy(b, s + start, (size_t)len); b[len] = 0; return b; }

long long proc_arg_count(long long argc) { return argc; }
const char *proc_exit(long long code) { exit((int)code); return ""; }

static unsigned char *_dmc_arenas[1024];
static size_t _dmc_arena_sz[1024];
static long long _dmc_arena_top[1024];
long long arena_new(long long cap) { if (cap < 0) return -1; int h = _dmc_next++; _dmc_arenas[h] = (unsigned char*)malloc((size_t)(cap ? cap : 1)); _dmc_arena_sz[h] = (size_t)cap; _dmc_arena_top[h] = 0; return h; }
long long arena_alloc(long long h, long long n) { if (!_dmc_arenas[h] || n < 0 || _dmc_arena_top[h] + n > (long long)_dmc_arena_sz[h]) return -1; long long off = _dmc_arena_top[h]; _dmc_arena_top[h] += n; return off; }
long long arena_write(long long h, long long off, long long i, long long v) { if (!_dmc_arenas[h] || off < 0 || i < 0 || (size_t)(off + i) >= _dmc_arena_sz[h]) return -1; _dmc_arenas[h][(size_t)(off + i)] = (unsigned char)v; return 0; }
long long arena_read(long long h, long long off, long long i) { if (!_dmc_arenas[h] || off < 0 || i < 0 || (size_t)(off + i) >= _dmc_arena_sz[h]) return -1; return _dmc_arenas[h][(size_t)(off + i)]; }
long long arena_reset(long long h) { if (!_dmc_arenas[h]) return -1; _dmc_arena_top[h] = 0; return 0; }
long long arena_top(long long h) { return _dmc_arenas[h] ? _dmc_arena_top[h] : -1; }
long long arena_free(long long h) { if (!_dmc_arenas[h]) return -1; free(_dmc_arenas[h]); _dmc_arenas[h] = NULL; _dmc_arena_top[h] = 0; _dmc_arena_sz[h] = 0; return 0; }

static int _dmc_io_allowed = 0;
long long port_init(void) {
#ifdef __linux__
  if (iopl(3) == 0) { _dmc_io_allowed = 1; return 1; }
  _dmc_io_allowed = 0; return 0;
#else
  _dmc_io_allowed = 0; return 0;
#endif
}
long long port_valid(void) { return _dmc_io_allowed; }
long long port_out8(long long port, long long v) { if (!_dmc_io_allowed) return -1; __asm__ volatile("outb %0, %1" :: "a"((unsigned char)v), "Nd"((unsigned short)port)); return 0; }
long long port_in8(long long port) { if (!_dmc_io_allowed) return -1; unsigned char r; __asm__ volatile("inb %1, %0" : "=a"(r) : "Nd"((unsigned short)port)); return r; }
long long port_out16(long long port, long long v) { if (!_dmc_io_allowed) return -1; __asm__ volatile("outw %0, %1" :: "a"((unsigned short)v), "Nd"((unsigned short)port)); return 0; }
long long port_in16(long long port) { if (!_dmc_io_allowed) return -1; unsigned short r; __asm__ volatile("inw %1, %0" : "=a"(r) : "Nd"((unsigned short)port)); return r; }
long long port_out32(long long port, long long v) { if (!_dmc_io_allowed) return -1; __asm__ volatile("outl %0, %1" :: "a"((unsigned int)v), "Nd"((unsigned short)port)); return 0; }
long long port_in32(long long port) { if (!_dmc_io_allowed) return -1; unsigned int r; __asm__ volatile("inl %1, %0" : "=a"(r) : "Nd"((unsigned short)port)); return r; }

static long long _dmc_isr[256];
long long isr_set(long long vec, long long handler) { if (vec < 0 || vec >= 256) return -1; _dmc_isr[vec] = handler; return 0; }
long long isr_get(long long vec) { if (vec < 0 || vec >= 256) return -1; return _dmc_isr[vec]; }
long long isr_call(long long vec, long long arg) { (void)arg; if (vec < 0 || vec >= 256) return -1; return _dmc_isr[vec]; }

#ifdef __linux__
#include <sys/syscall.h>
long long syscall_h(long long n, long long a1, long long a2) { return syscall((long)n, a1, a2); }
long long interrupt(long long n) { (void)n; return -1; }
#else
long long syscall_h(long long n, long long a1, long long a2) { (void)n; (void)a1; (void)a2; return -1; }
long long interrupt(long long n) { (void)n; return -1; }
#endif


int __dmc_main_body(void) {
    int arr[4] = {0, 0, 0, 0};
    ((arr)[0] = 10);
    ((arr)[1] = 20);
    ((arr)[2] = 30);
    int nums[3] = {0, 0, 0};
    ((nums)[0] = ((arr)[0] + (arr)[1]));
    return (nums)[0];
}
int main(int argc, char **argv) {
    __dmc_argc = argc;
    __dmc_argv = argv;
    return __dmc_main_body();
}



