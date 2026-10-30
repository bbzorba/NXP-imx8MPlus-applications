// SPDX-License-Identifier: Apache-2.0
/*
 * Home Hub client node for the i.MX 8M Plus (Linux on the Cortex-A53s).
 *
 * Every 10 s, reads the board's sensors and system metrics and POSTs them as
 * JSON to the hub's /api/node endpoint (ESP32-C6 hub, see the
 * ESP32C6-Zephyr-IoT-Projects repo, applications/web_app/src/nodes.c):
 *
 *   {"id":"<hostname>","model":"<device-tree model>",
 *    "sensors":[{"name":"CPU temperature","value":46.0,"unit":"°C"}, ...]}
 *
 * If the hub is unreachable (TCP connect fails), sending stops. The node then
 * only checks every RETRY_S seconds whether the hub is back, and resumes when
 * it is.
 *
 * Configuration through environment variables:
 *   HUB_HOST  hub address or name (default: home-hub, resolved by the router)
 *   HUB_PORT  default 80
 */

#include <dirent.h>
#include <limits.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define PERIOD_S    10
#define RETRY_S     60
#define TIMEOUT_S   5
#define MAX_SENSORS 16 /* the hub keeps at most 16 per node */

struct report {
	char buf[2048];
	size_t len;
	int count;
};

static const char *hub_host;
static const char *hub_port;

static bool read_file(const char *path, char *out, size_t len)
{
	FILE *f = fopen(path, "r");
	size_t n;

	if (f == NULL) {
		return false;
	}
	n = fread(out, 1, len - 1, f);
	fclose(f);
	out[n] = '\0';
	out[strcspn(out, "\n")] = '\0';
	return n > 0;
}

static long read_long(const char *path, long def)
{
	char s[32];

	return read_file(path, s, sizeof(s)) ? strtol(s, NULL, 10) : def;
}

static void append(struct report *r, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	if (r->len < sizeof(r->buf)) {
		r->len += vsnprintf(r->buf + r->len, sizeof(r->buf) - r->len, fmt, ap);
	}
	va_end(ap);
}

/* Names come from sysfs or this file, so dropping quotes/backslashes is
 * enough to keep the JSON valid.
 */
static void add(struct report *r, const char *name, double value, const char *unit)
{
	char clean[24];
	size_t j = 0;

	if (r->count == MAX_SENSORS) {
		return;
	}
	for (size_t i = 0; name[i] && j < sizeof(clean) - 1; i++) {
		if (name[i] != '"' && name[i] != '\\') {
			clean[j++] = name[i];
		}
	}
	clean[j] = '\0';

	append(r, "%s{\"name\":\"%s\",\"value\":%.2f,\"unit\":\"%s\"}", r->count ? "," : "",
	       clean, value, unit);
	r->count++;
}

/* --- Sensors --- */

static void add_thermal_zones(struct report *r)
{
	char path[300], type[64];
	struct dirent *e;
	DIR *d = opendir("/sys/class/thermal");

	if (d == NULL) {
		return;
	}
	while ((e = readdir(d)) != NULL) {
		long temp;

		if (strncmp(e->d_name, "thermal_zone", 12) != 0) {
			continue;
		}
		snprintf(path, sizeof(path), "/sys/class/thermal/%s/type", e->d_name);
		if (!read_file(path, type, sizeof(type))) {
			continue;
		}
		snprintf(path, sizeof(path), "/sys/class/thermal/%s/temp", e->d_name);
		temp = read_long(path, LONG_MIN);
		if (temp == LONG_MIN) {
			continue;
		}
		if (strcmp(type, "cpu-thermal") == 0) {
			strcpy(type, "CPU temperature");
		} else if (strcmp(type, "soc-thermal") == 0) {
			strcpy(type, "SoC temperature");
		}
		add(r, type, temp / 1000.0, "°C");
	}
	closedir(d);
}

/* Voltage/current monitors (e.g. the USB-C PD controller). They read 0
 * unless the port is supplying power, so only non-zero values are sent.
 */
static void add_hwmon_power(struct report *r)
{
	static const struct { const char *file; const char *unit; } inputs[] = {
		{ "in0_input", "V" }, { "curr1_input", "A" },
	};
	char path[300], name[32], label[40];
	struct dirent *e;
	DIR *d = opendir("/sys/class/hwmon");

	if (d == NULL) {
		return;
	}
	while ((e = readdir(d)) != NULL) {
		if (strncmp(e->d_name, "hwmon", 5) != 0) {
			continue;
		}
		snprintf(path, sizeof(path), "/sys/class/hwmon/%s/name", e->d_name);
		if (!read_file(path, name, sizeof(name))) {
			continue;
		}
		for (size_t i = 0; i < sizeof(inputs) / sizeof(inputs[0]); i++) {
			long v;

			snprintf(path, sizeof(path), "/sys/class/hwmon/%s/%s", e->d_name,
				 inputs[i].file);
			v = read_long(path, 0);
			if (v != 0) {
				snprintf(label, sizeof(label), "%.20s %s", name, inputs[i].unit);
				add(r, label, v / 1000.0, inputs[i].unit);
			}
		}
	}
	closedir(d);
}

static double cpu_load_pct(void)
{
	static unsigned long long last_total, last_idle;
	unsigned long long v[8] = { 0 }, total = 0, idle, d_total, d_idle;
	FILE *f = fopen("/proc/stat", "r");
	double pct;

	if (f == NULL) {
		return 0.0;
	}
	if (fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", &v[0], &v[1], &v[2],
		   &v[3], &v[4], &v[5], &v[6], &v[7]) != 8) {
		fclose(f);
		return 0.0;
	}
	fclose(f);

	for (int i = 0; i < 8; i++) {
		total += v[i];
	}
	idle = v[3] + v[4]; /* idle + iowait */
	d_total = total - last_total;
	d_idle = idle - last_idle;
	pct = d_total ? 100.0 * (double)(d_total - d_idle) / (double)d_total : 0.0;
	last_total = total;
	last_idle = idle;
	return pct;
}

static void add_system(struct report *r)
{
	char path[96], s[256];
	long freq_sum = 0;
	int freq_n = 0;
	double load1;
	long mem_total = 0, mem_avail = 0;
	struct statvfs st;
	FILE *f;

	add(r, "CPU load", cpu_load_pct(), "%");

	for (int cpu = 0; cpu < 16; cpu++) {
		long khz;

		snprintf(path, sizeof(path),
			 "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", cpu);
		khz = read_long(path, -1);
		if (khz > 0) {
			freq_sum += khz;
			freq_n++;
		}
	}
	if (freq_n) {
		add(r, "CPU frequency", freq_sum / freq_n / 1000.0, "MHz");
	}

	if (read_file("/proc/loadavg", s, sizeof(s)) && sscanf(s, "%lf", &load1) == 1) {
		add(r, "Load average (1 min)", load1, "");
	}

	f = fopen("/proc/meminfo", "r");
	if (f != NULL) {
		while (fgets(s, sizeof(s), f)) {
			sscanf(s, "MemTotal: %ld", &mem_total);
			sscanf(s, "MemAvailable: %ld", &mem_avail);
		}
		fclose(f);
		if (mem_total) {
			add(r, "Memory used", 100.0 * (1.0 - (double)mem_avail / mem_total), "%");
		}
	}

	if (statvfs("/", &st) == 0 && st.f_blocks) {
		add(r, "Disk used (/)", 100.0 * (1.0 - (double)st.f_bavail / st.f_blocks), "%");
	}

	if (read_file("/proc/uptime", s, sizeof(s))) {
		add(r, "Uptime", atof(s) / 3600.0, "h");
	}
}

static void build_report(struct report *r, const char *id, const char *model)
{
	r->len = 0;
	r->count = 0;
	append(r, "{\"id\":\"%s\",\"model\":\"%s\",\"sensors\":[", id, model);
	add_thermal_zones(r);
	add_hwmon_power(r);
	add_system(r);
	append(r, "]}");
}

/* --- Network --- */

/* TCP connection to the hub, or -1 if it cannot be reached */
static int hub_connect(void)
{
	struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM };
	struct timeval tv = { .tv_sec = TIMEOUT_S };
	struct addrinfo *res;
	int fd;

	if (getaddrinfo(hub_host, hub_port, &hints, &res) != 0) {
		return -1;
	}
	fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (fd >= 0) {
		/* SO_SNDTIMEO also bounds connect() on Linux */
		setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
		setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
		if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
			close(fd);
			fd = -1;
		}
	}
	freeaddrinfo(res);
	return fd;
}

/* HTTP status of the POST, or -1 if the hub could not be reached */
static int post_report(const struct report *r)
{
	char req[256], rsp[64] = "";
	int fd = hub_connect();
	int status = -1;
	int n;

	if (fd < 0) {
		return -1;
	}
	n = snprintf(req, sizeof(req),
		     "POST /api/node HTTP/1.1\r\nHost: %s\r\n"
		     "Content-Type: application/json\r\nContent-Length: %zu\r\n"
		     "Connection: close\r\n\r\n",
		     hub_host, r->len);
	if (send(fd, req, n, MSG_NOSIGNAL) == n &&
	    send(fd, r->buf, r->len, MSG_NOSIGNAL) == (ssize_t)r->len &&
	    recv(fd, rsp, sizeof(rsp) - 1, 0) > 0) {
		sscanf(rsp, "HTTP/%*s %d", &status);
	}
	close(fd);
	return status;
}

int main(void)
{
	char id[64] = "imx8mp", model[40] = "i.MX 8M Plus";
	struct report report;
	bool online = true;

	hub_host = getenv("HUB_HOST") ? getenv("HUB_HOST") : "home-hub";
	hub_port = getenv("HUB_PORT") ? getenv("HUB_PORT") : "80";
	gethostname(id, sizeof(id) - 1);
	read_file("/proc/device-tree/model", model, sizeof(model));
	setvbuf(stdout, NULL, _IOLBF, 0);

	printf("Home Hub node '%s' (%s) -> http://%s:%s/api/node\n", id, model, hub_host,
	       hub_port);
	cpu_load_pct(); /* start the first load window */

	for (;;) {
		struct timespec start, end;
		int status;

		if (!online) {
			int fd;

			/* Hub was down: send nothing, just check whether it is back */
			sleep(RETRY_S);
			fd = hub_connect();
			if (fd < 0) {
				continue;
			}
			close(fd);
			printf("Hub reachable again, resuming\n");
			cpu_load_pct();
			online = true;
			sleep(PERIOD_S);
		}

		clock_gettime(CLOCK_MONOTONIC, &start);
		build_report(&report, id, model);
		status = post_report(&report);
		if (status < 0) {
			printf("Hub not reachable, stopping until it is back\n");
			online = false;
			continue;
		}
		if (status != 200) {
			printf("Hub rejected report: HTTP %d\n", status);
		}

		clock_gettime(CLOCK_MONOTONIC, &end);
		if (end.tv_sec - start.tv_sec < PERIOD_S) {
			sleep(PERIOD_S - (end.tv_sec - start.tv_sec));
		}
	}
}
