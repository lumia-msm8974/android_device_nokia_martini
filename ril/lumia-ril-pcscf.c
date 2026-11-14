/* SPDX-License-Identifier: Apache-2.0 */
/*
 * libril-lumia-pcscf: wrapper around the MSM8974 Qualcomm RIL that fills the
 * P-CSCF list of the data calls.
 *
 * The qcril blob of the MSM8974 Lumias (libril-qc-qmi-1.so) never reads the
 * P-CSCF addresses that the network returns in the PCO: the library has no
 * P-CSCF code at all, and it never calls the dsi_iface_ioctl() of
 * libdsi_netctrl that could fetch them. Android 11 only takes the P-CSCF from
 * the data call response, and some IMS networks (Iliad Italy) have no DNS on
 * the IMS PDN, so a userspace IMS stack (phh IMS) cannot find its proxy.
 *
 * rild loads this library instead of qcril (vendor.rild.libpath). It loads
 * qcril, wraps the request entry point and the completion callbacks, and for
 * every active data call whose P-CSCF field is empty it asks the modem: a QMI
 * WDS client bound to the same rmnet port, with the IP family of the call,
 * reads the runtime settings of the call (P-CSCF server lists, plus the IP
 * address, which must match the one of the call). Everything else goes
 * through untouched.
 *
 * When an "ims" call comes up without a P-CSCF the modem profile probably
 * does not ask for it in the PCO: vendor.radio.lumia_ims_profile=retry starts
 * lumia-ims-profile, which fixes the profile for the next attempt.
 *
 * persist.vendor.lumia.ril_pcscf=0 turns the lookup off (pure pass-through).
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <android/log.h>
#include <sys/system_properties.h>
#define RIL_SHLIB	/* struct RIL_Env, as seen by a vendor RIL */
#include <telephony/ril.h>

#define TAG "LumiaRIL"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)

/* Same path as before, so that TARGET_LD_SHIM_LIBS still matches it. */
#define REAL_RIL_PATH		"/system/vendor/lib/libril-qc-qmi-1.so"
#define QMI_CCI_PATH		"/vendor/lib/libqmi_cci.so"
#define QMI_SERVICES_PATH	"/vendor/lib/libqmiservices.so"

#define PROP_ENABLE		"persist.vendor.lumia.ril_pcscf"
#define PROP_PROFILE		"vendor.radio.lumia_ims_profile"

#define QMI_WDS_GET_RUNTIME_SETTINGS	0x002d
#define QMI_WDS_SET_CLIENT_IP_FAMILY	0x004d

#define TLV_RESULT		0x02
#define TLV_REQUESTED		0x10	/* request: uint32 mask */
#define TLV_IP_FAMILY_PREF	0x01	/* request: uint8 4 or 6 */
#define TLV_IPV4_ADDRESS	0x1e	/* uint32, first octet in the MSB */
#define TLV_PCSCF_USING_PCO	0x22	/* uint8 */
#define TLV_PCSCF_IPV4_LIST	0x23	/* uint8 count, count * uint32 */
#define TLV_IPV6_ADDRESS	0x25	/* 16 bytes, prefix length */
#define TLV_PCSCF_IPV6_LIST	0x2e	/* uint8 count, count * 16 bytes */

#define SETTINGS_IP_ADDRESS	(1u << 8)
#define SETTINGS_PCSCF_PCO	(1u << 10)
#define SETTINGS_PCSCF_LIST	(1u << 11)

#define MAX_INSTANCES		8	/* rmnet0..rmnet7 */
#define MAX_PENDING		32
#define MAX_CACHE		8
#define MAX_TRIES		3
#define PCSCF_LEN		384
#define QMI_TIMEOUT_MS		2000

typedef void (*request_fn)(int, void *, size_t, RIL_Token, int);
typedef void (*complete_fn)(RIL_Token, RIL_Errno, void *, size_t);
typedef void (*unsol_fn)(int, const void *, size_t, int);
typedef const RIL_RadioFunctions *(*init_fn)(const struct RIL_Env *, int,
					     char **);

typedef int (*qmi_init_fn)(void *, unsigned int, void *, void *, void *,
			   unsigned int, void **);
typedef int (*qmi_raw_fn)(void *, unsigned int, void *, unsigned int, void *,
			  unsigned int, unsigned int *, unsigned int);
typedef int (*qmi_release_fn)(void *);

static void *real_handle;
static const struct RIL_Env *rild_env;
static struct RIL_Env wrapped_env;
static const RIL_RadioFunctions *real_funcs;
static RIL_RadioFunctions wrapped_funcs;

/* Requests whose answer is a data call list, by token. */
struct pending {
	RIL_Token token;
	int request;
	int ims;
};

/* P-CSCF of one data call, or the attempts that found none. */
struct cache {
	int used;
	int seen;
	int cid;
	char ifname[16];
	char addresses[128];
	char pcscf[PCSCF_LEN];
	int tries;
	int ims;
};

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static struct pending pending[MAX_PENDING];
static unsigned int pending_next;
static struct cache cache[MAX_CACHE];

static qmi_init_fn qmi_init;
static qmi_raw_fn qmi_send_raw;
static qmi_release_fn qmi_release;
static void *wds_object;
static int qmi_state;			/* 0 not loaded, 1 ready, -1 failed */
static void *clients[MAX_INSTANCES][2];	/* [port][0 = IPv4, 1 = IPv6] */
static union {
	long long align;
	uint8_t bytes[1024];
} client_params[MAX_INSTANCES][2];
static uint8_t response[2048];
static unsigned int response_len;

static int enabled(void)
{
	char value[PROP_VALUE_MAX] = { 0 };

	__system_property_get(PROP_ENABLE, value);
	return strcmp(value, "0") != 0;
}

static int empty(const char *s)
{
	return !s || !*s;
}

/* ---- QMI ---------------------------------------------------------------- */

static unsigned int le16(const uint8_t *p)
{
	return p[0] | ((unsigned int)p[1] << 8);
}

static uint32_t le32(const uint8_t *p)
{
	return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
	       ((uint32_t)p[3] << 24);
}

static const uint8_t *find_tlv(unsigned int tag, unsigned int *len)
{
	unsigned int i = 0;

	while (i + 3 <= response_len) {
		unsigned int n = le16(response + i + 1);

		if (n > response_len - i - 3)
			return NULL;
		if (response[i] == tag) {
			*len = n;
			return response + i + 3;
		}
		i += n + 3;
	}
	return NULL;
}

/* 0 when the transport and the QMI result both succeed; 1 on a QMI error. */
static int qmi_request(void *client, unsigned int msg, const void *data,
		       unsigned int len)
{
	const uint8_t *result;
	unsigned int n;
	int ret;

	response_len = 0;
	ret = qmi_send_raw(client, msg, (void *)data, len, response,
			   sizeof(response), &response_len, QMI_TIMEOUT_MS);
	if (ret) {
		LOGW("QMI 0x%04x: transport error %d", msg, ret);
		return -1;
	}
	result = find_tlv(TLV_RESULT, &n);
	if (!result || n < 4) {
		LOGW("QMI 0x%04x: no result TLV", msg);
		return -1;
	}
	if (le16(result)) {
		LOGI("QMI 0x%04x: error 0x%04x", msg, le16(result + 2));
		return 1;
	}
	return 0;
}

static int qmi_load(void)
{
	void *cci, *services;

	if (qmi_state)
		return qmi_state > 0;
	qmi_state = -1;
	cci = dlopen(QMI_CCI_PATH, RTLD_NOW | RTLD_GLOBAL);
	services = dlopen(QMI_SERVICES_PATH, RTLD_NOW | RTLD_GLOBAL);
	if (!cci || !services) {
		LOGW("cannot load the QMI libraries: %s", dlerror());
		return 0;
	}
	qmi_init = (qmi_init_fn)dlsym(cci, "qmi_client_init_instance");
	qmi_send_raw = (qmi_raw_fn)dlsym(cci, "qmi_client_send_raw_msg_sync");
	qmi_release = (qmi_release_fn)dlsym(cci, "qmi_client_release");
	wds_object = dlsym(services, "wds_qmi_idl_service_object_v01");
	if (!qmi_init || !qmi_send_raw || !qmi_release || !wds_object) {
		LOGW("QMI symbols missing");
		return 0;
	}
	qmi_state = 1;
	return 1;
}

/* WDS client on QMI port "instance" (rmnetN), bound to one IP family. */
static void *wds_client(unsigned int instance, int ipv6)
{
	const uint8_t family[] = { TLV_IP_FAMILY_PREF, 1, 0, ipv6 ? 6 : 4 };
	void **slot = &clients[instance][ipv6];
	int ret;

	if (*slot)
		return *slot;
	memset(&client_params[instance][ipv6], 0,
	       sizeof(client_params[instance][ipv6]));
	ret = qmi_init(wds_object, instance, NULL, NULL,
		       &client_params[instance][ipv6], QMI_TIMEOUT_MS, slot);
	if (ret) {
		LOGW("WDS client on port %u: error %d", instance, ret);
		*slot = NULL;
		return NULL;
	}
	if (qmi_request(*slot, QMI_WDS_SET_CLIENT_IP_FAMILY, family,
			sizeof(family))) {
		qmi_release(*slot);
		*slot = NULL;
		return NULL;
	}
	return *slot;
}

static void drop_client(unsigned int instance, int ipv6)
{
	if (clients[instance][ipv6])
		qmi_release(clients[instance][ipv6]);
	clients[instance][ipv6] = NULL;
}

static void append(char *out, const char *addr)
{
	size_t n = strlen(out), a = strlen(addr);
	const char *p = out;

	/* skip duplicates */
	while ((p = strstr(p, addr))) {
		if ((p == out || p[-1] == ' ') && (p[a] == ' ' || !p[a]))
			return;
		p += a;
	}
	if (n + a + 2 > PCSCF_LEN)
		return;
	if (n)
		out[n++] = ' ';
	memcpy(out + n, addr, a + 1);
}

/* Does the first address of the call (without "/prefix") equal addr? */
static int call_has_address(const char *addresses, const char *addr)
{
	const char *p = addresses;
	size_t a = strlen(addr);

	while (p && *p) {
		while (*p == ' ')
			p++;
		if (!strncasecmp(p, addr, a) &&
		    (p[a] == '/' || p[a] == ' ' || !p[a]))
			return 1;
		p = strchr(p, ' ');
	}
	return 0;
}

/*
 * Ask the modem for the P-CSCF of the call on rmnet<instance>, IP family
 * ipv6 or IPv4. Appends to out. Returns 1 if the WDS client looks at the same
 * call (same IP address), 0 otherwise.
 */
static int query_family(unsigned int instance, int ipv6,
			const char *addresses, char *out)
{
	const uint32_t mask = SETTINGS_IP_ADDRESS | SETTINGS_PCSCF_PCO |
			      SETTINGS_PCSCF_LIST;
	const uint8_t req[] = { TLV_REQUESTED, 4, 0, mask & 0xff,
				(mask >> 8) & 0xff, (mask >> 16) & 0xff,
				mask >> 24 };
	char addr[INET6_ADDRSTRLEN];
	const uint8_t *v;
	unsigned int n, i;
	void *client;
	int ret;

	client = wds_client(instance, ipv6);
	if (!client)
		return 0;
	ret = qmi_request(client, QMI_WDS_GET_RUNTIME_SETTINGS, req,
			  sizeof(req));
	if (ret < 0)
		drop_client(instance, ipv6);
	if (ret)
		return 0;

	/* The client must see the call of this ifname, not another one. */
	addr[0] = 0;
	if (ipv6 && (v = find_tlv(TLV_IPV6_ADDRESS, &n)) && n >= 16)
		inet_ntop(AF_INET6, v, addr, sizeof(addr));
	else if (!ipv6 && (v = find_tlv(TLV_IPV4_ADDRESS, &n)) && n >= 4) {
		uint32_t a = le32(v);

		snprintf(addr, sizeof(addr), "%u.%u.%u.%u", a >> 24,
			 (a >> 16) & 0xff, (a >> 8) & 0xff, a & 0xff);
	}
	if (!addr[0] || !call_has_address(addresses, addr)) {
		LOGW("port %u IPv%d: runtime settings of another call, ignored",
		     instance, ipv6 ? 6 : 4);
		return 0;
	}

	if ((v = find_tlv(TLV_PCSCF_USING_PCO, &n)) && n >= 1)
		LOGI("port %u IPv%d: P-CSCF requested in the PCO: %u",
		     instance, ipv6 ? 6 : 4, v[0]);
	if ((v = find_tlv(TLV_PCSCF_IPV4_LIST, &n)) && n >= 1 &&
	    n == 1 + 4u * v[0]) {
		for (i = 0; i < v[0]; i++) {
			uint32_t a = le32(v + 1 + 4 * i);

			snprintf(addr, sizeof(addr), "%u.%u.%u.%u", a >> 24,
				 (a >> 16) & 0xff, (a >> 8) & 0xff, a & 0xff);
			if (a)
				append(out, addr);
		}
	}
	if ((v = find_tlv(TLV_PCSCF_IPV6_LIST, &n)) && n >= 1 &&
	    n == 1 + 16u * v[0]) {
		static const uint8_t zero[16];

		for (i = 0; i < v[0]; i++) {
			if (!memcmp(v + 1 + 16 * i, zero, 16))
				continue;
			inet_ntop(AF_INET6, v + 1 + 16 * i, addr, sizeof(addr));
			append(out, addr);
		}
	} else if (v) {
		LOGW("port %u: unexpected P-CSCF IPv6 list (%u bytes)",
		     instance, n);
	}
	return 1;
}

static int port_of(const char *ifname, unsigned int *instance)
{
	char *end;
	unsigned long n;

	if (strncmp(ifname, "rmnet", 5) || ifname[5] < '0' || ifname[5] > '9')
		return 0;
	n = strtoul(ifname + 5, &end, 10);
	if (*end || n >= MAX_INSTANCES)
		return 0;
	*instance = n;
	return 1;
}

static void lookup(const RIL_Data_Call_Response_v11 *call, char *out)
{
	const char *type = call->type ? call->type : "";
	unsigned int instance;
	int v4, v6;

	out[0] = 0;
	if (!port_of(call->ifname, &instance) || !qmi_load())
		return;
	v6 = strcasestr(type, "IPV6") != NULL;
	v4 = !v6 || !strcasecmp(type, "IPV4V6");
	if (v6)
		query_family(instance, 1, call->addresses, out);
	if (v4)
		query_family(instance, 0, call->addresses, out);
}

/* ---- data call lists ---------------------------------------------------- */

static struct cache *cache_find(const RIL_Data_Call_Response_v11 *call)
{
	struct cache *free_slot = NULL, *c;
	int i;

	for (i = 0; i < MAX_CACHE; i++) {
		c = &cache[i];
		if (!c->used) {
			if (!free_slot)
				free_slot = c;
			continue;
		}
		if (c->cid == call->cid && !strcmp(c->ifname, call->ifname) &&
		    !strncmp(c->addresses, call->addresses ? call->addresses : "",
			     sizeof(c->addresses) - 1))
			return c;
	}
	if (!free_slot)
		free_slot = &cache[call->cid % MAX_CACHE];
	memset(free_slot, 0, sizeof(*free_slot));
	free_slot->used = 1;
	free_slot->cid = call->cid;
	snprintf(free_slot->ifname, sizeof(free_slot->ifname), "%s",
		 call->ifname);
	snprintf(free_slot->addresses, sizeof(free_slot->addresses), "%s",
		 call->addresses ? call->addresses : "");
	return free_slot;
}

/* Called with lock held. Returns a malloc'ed P-CSCF list or NULL. */
static char *pcscf_for(const RIL_Data_Call_Response_v11 *call, int ims)
{
	struct cache *c;

	if (call->status || !call->active || empty(call->ifname) ||
	    empty(call->addresses))
		return NULL;
	c = cache_find(call);
	c->seen = 1;
	c->ims |= ims;
	if (!c->pcscf[0] && c->tries < MAX_TRIES) {
		c->tries++;
		lookup(call, c->pcscf);
		LOGI("cid %d on %s: %s P-CSCF from the modem", call->cid,
		     call->ifname, c->pcscf[0] ? "got" : "no");
		if (!c->pcscf[0] && c->ims && c->tries == 1)
			__system_property_set(PROP_PROFILE, "retry");
	}
	return c->pcscf[0] ? strdup(c->pcscf) : NULL;
}

static size_t record_size(size_t len, int *version)
{
	if (len % sizeof(RIL_Data_Call_Response_v11) == 0) {
		*version = 11;
		return sizeof(RIL_Data_Call_Response_v11);
	}
	if (len % sizeof(RIL_Data_Call_Response_v9) == 0) {
		*version = 9;
		return sizeof(RIL_Data_Call_Response_v9);
	}
	if (len % sizeof(RIL_Data_Call_Response_v6) == 0) {
		*version = 6;
		return sizeof(RIL_Data_Call_Response_v6);
	}
	return 0;
}

static void to_v11(const void *list, int version, size_t i,
		   RIL_Data_Call_Response_v11 *d)
{
	memset(d, 0, sizeof(*d));
	if (version == 11) {
		*d = ((const RIL_Data_Call_Response_v11 *)list)[i];
	} else if (version == 9) {
		const RIL_Data_Call_Response_v9 *s =
			&((const RIL_Data_Call_Response_v9 *)list)[i];

		d->status = s->status;
		d->suggestedRetryTime = s->suggestedRetryTime;
		d->cid = s->cid;
		d->active = s->active;
		d->type = s->type;
		d->ifname = s->ifname;
		d->addresses = s->addresses;
		d->dnses = s->dnses;
		d->gateways = s->gateways;
		d->pcscf = s->pcscf;
	} else {
		const RIL_Data_Call_Response_v6 *s =
			&((const RIL_Data_Call_Response_v6 *)list)[i];

		d->status = s->status;
		d->suggestedRetryTime = s->suggestedRetryTime;
		d->cid = s->cid;
		d->active = s->active;
		d->type = s->type;
		d->ifname = s->ifname;
		d->addresses = s->addresses;
		d->dnses = s->dnses;
		d->gateways = s->gateways;
	}
}

/*
 * Fill the empty P-CSCF fields of a data call list. Returns a malloc'ed v11
 * copy (and its length), or NULL when nothing changed. full_list marks a
 * complete list: calls that are not in it are forgotten.
 */
static RIL_Data_Call_Response_v11 *fix_list(const void *list, size_t len,
					     int ims, int full_list,
					     size_t *out_len, char ***strings)
{
	RIL_Data_Call_Response_v11 *out = NULL;
	size_t size, count, i;
	int version, changed = 0;

	*strings = NULL;
	if (!list || !len || !(size = record_size(len, &version)))
		return NULL;
	count = len / size;
	out = calloc(count, sizeof(*out));
	*strings = calloc(count, sizeof(char *));
	if (!out || !*strings) {
		free(out);
		free(*strings);
		*strings = NULL;
		return NULL;
	}

	pthread_mutex_lock(&lock);
	if (full_list)
		for (i = 0; i < MAX_CACHE; i++)
			cache[i].seen = 0;
	for (i = 0; i < count; i++) {
		to_v11(list, version, i, &out[i]);
		if (!empty(out[i].pcscf))
			continue;
		(*strings)[i] = pcscf_for(&out[i], ims);
		if ((*strings)[i]) {
			out[i].pcscf = (*strings)[i];
			changed = 1;
		}
	}
	if (full_list)
		for (i = 0; i < MAX_CACHE; i++)
			if (cache[i].used && !cache[i].seen)
				cache[i].used = 0;
	pthread_mutex_unlock(&lock);

	if (!changed) {
		free(out);
		free(*strings);
		*strings = NULL;
		return NULL;
	}
	*out_len = count * sizeof(*out);
	return out;
}

static void free_fixed(RIL_Data_Call_Response_v11 *list, size_t len,
		       char **strings)
{
	size_t i;

	if (strings)
		for (i = 0; i < len / sizeof(*list); i++)
			free(strings[i]);
	free(strings);
	free(list);
}

/* ---- callbacks ---------------------------------------------------------- */

static void on_request(int request, void *data, size_t datalen, RIL_Token t,
		       int socket_id)
{
	if (request == RIL_REQUEST_SETUP_DATA_CALL ||
	    request == RIL_REQUEST_DATA_CALL_LIST) {
		const char **args = data;
		struct pending *p;

		pthread_mutex_lock(&lock);
		p = &pending[pending_next++ % MAX_PENDING];
		p->token = t;
		p->request = request;
		p->ims = request == RIL_REQUEST_SETUP_DATA_CALL && args &&
			 datalen >= 3 * sizeof(char *) && args[2] &&
			 !strcasecmp(args[2], "ims");
		pthread_mutex_unlock(&lock);
	}
	((request_fn)real_funcs->onRequest)(request, data, datalen, t,
					    socket_id);
}

static void on_request_complete(RIL_Token t, RIL_Errno e, void *response_data,
				size_t len)
{
	RIL_Data_Call_Response_v11 *fixed = NULL;
	int request = 0, ims = 0, i;
	size_t fixed_len = 0;
	char **strings = NULL;

	pthread_mutex_lock(&lock);
	for (i = 0; i < MAX_PENDING; i++) {
		if (pending[i].request && pending[i].token == t) {
			request = pending[i].request;
			ims = pending[i].ims;
			pending[i].request = 0;
			pending[i].token = NULL;
			break;
		}
	}
	pthread_mutex_unlock(&lock);

	if (request && e == RIL_E_SUCCESS && enabled())
		fixed = fix_list(response_data, len, ims,
				 request == RIL_REQUEST_DATA_CALL_LIST,
				 &fixed_len, &strings);
	if (fixed) {
		((complete_fn)rild_env->OnRequestComplete)(t, e, fixed,
							   fixed_len);
		free_fixed(fixed, fixed_len, strings);
		return;
	}
	((complete_fn)rild_env->OnRequestComplete)(t, e, response_data, len);
}

/* Takes the socket id too: forwarded as is whatever the build of libril. */
static void on_unsolicited(int unsol, const void *data, size_t len,
			   int socket_id)
{
	RIL_Data_Call_Response_v11 *fixed = NULL;
	size_t fixed_len = 0;
	char **strings = NULL;

	if (unsol == RIL_UNSOL_DATA_CALL_LIST_CHANGED && enabled())
		fixed = fix_list(data, len, 0, 1, &fixed_len, &strings);
	if (fixed) {
		((unsol_fn)rild_env->OnUnsolicitedResponse)(unsol, fixed,
							    fixed_len,
							    socket_id);
		free_fixed(fixed, fixed_len, strings);
		return;
	}
	((unsol_fn)rild_env->OnUnsolicitedResponse)(unsol, data, len,
						    socket_id);
}

static void *load_real(void)
{
	if (!real_handle) {
		real_handle = dlopen(REAL_RIL_PATH, RTLD_NOW);
		if (!real_handle)
			LOGW("cannot load %s: %s", REAL_RIL_PATH, dlerror());
	}
	return real_handle;
}

const RIL_RadioFunctions *RIL_Init(const struct RIL_Env *env, int argc,
				   char **argv)
{
	init_fn real_init;

	if (!load_real())
		return NULL;
	real_init = (init_fn)dlsym(real_handle, "RIL_Init");
	if (!real_init) {
		LOGW("RIL_Init missing in %s", REAL_RIL_PATH);
		return NULL;
	}
	rild_env = env;
	wrapped_env = *env;
	wrapped_env.OnRequestComplete = on_request_complete;
	wrapped_env.OnUnsolicitedResponse =
		(__typeof__(wrapped_env.OnUnsolicitedResponse))on_unsolicited;

	real_funcs = real_init(&wrapped_env, argc, argv);
	if (!real_funcs)
		return NULL;
	wrapped_funcs = *real_funcs;
	wrapped_funcs.onRequest = (RIL_RequestFunc)on_request;
	LOGI("qcril wrapped (RIL version %d), P-CSCF lookup %s",
	     real_funcs->version, enabled() ? "on" : "off");
	return &wrapped_funcs;
}

/* SIM Access Profile: untouched, straight to qcril. */
const RIL_RadioFunctions *RIL_SAP_Init(const struct RIL_Env *env, int argc,
				       char **argv)
{
	init_fn real_sap;

	if (!load_real())
		return NULL;
	real_sap = (init_fn)dlsym(real_handle, "RIL_SAP_Init");
	return real_sap ? real_sap(env, argc, argv) : NULL;
}
