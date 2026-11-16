/*
STEAM_SESSION.C

steam_session.h: the CM session's steps, request by request. Field numbers
are Steam's (steammessages_auth, _clientserver_login, _clientserver_appinfo,
_clientserver_2, _contentsystem), written by hand.
*/

#include "steam_session.h"

#include "steam_crypto.h"
#include "steam_net.h"
#include "steam_pb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CALL_TIMEOUT_MS 20000
#define END_TIMEOUT_MS 8000
/* an individual account in the public universe, desktop instance: the
steamid a logon is sent with before Steam gives the real one */
#define STEAMID_ANONYMOUS_INDIVIDUAL 0x0110000100000000ull
#define PLATFORM_STEAM_CLIENT 1

/* (64-bit: a 32-bit long overflows after 24.8 days of uptime - the PC's, or a Vita's since its boot) */
static long long now_ms(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static void say(struct steam_session *session, const char *format, ...)
	__attribute__((format(printf, 2, 3)));

#include <stdarg.h>

static void say(struct steam_session *session, const char *format, ...)
{
	char line[256];
	va_list arguments;

	if (!session->log)
		return;
	va_start(arguments, format);
	vsnprintf(line, sizeof(line), format, arguments);
	va_end(arguments);
	session->log(session->log_context, line);
}

int steam_session_init(struct steam_session *session, steam_log_proc log, void *log_context, const char *marker_path)
{
	memset(session, 0, sizeof(*session));
	session->log = log;
	session->log_context = log_context;
	session->next_job = 1;
	session->secrets = calloc(1, sizeof(*session->secrets));
	if (marker_path)
		snprintf(session->marker_path, sizeof(session->marker_path), "%s", marker_path);
	return session->secrets != NULL;
}

/* ---------- sending and waiting */

static int send_message(struct steam_session *session, uint32_t emsg, uint64_t job, const char *job_name,
	const void *body, size_t body_size)
{
	struct steam_cm_header header;
	uint8_t *message;
	size_t capacity = body_size + 512;
	long length;
	int ok;

	if (!session->connected)
		return 0;
	memset(&header, 0, sizeof(header));
	header.steamid = session->logged_on ? session->steamid : (emsg == STEAM_EMSG_CLIENT_LOGON ? STEAMID_ANONYMOUS_INDIVIDUAL : 0);
	header.session_id = session->logged_on ? session->session_id : 0;
	header.job_source = job ? job : STEAM_JOB_NONE;
	header.job_target = STEAM_JOB_NONE;
	if (job_name)
		snprintf(header.job_name, sizeof(header.job_name), "%s", job_name);
	message = malloc(capacity);
	if (!message)
		return 0;
	length = steam_cm_build(message, capacity, emsg, &header, body, body_size);
	ok = length > 0 && steam_ws_send(&session->ws, message, (size_t)length);
	/* (it may hold a token) */
	steam_wipe(message, capacity);
	free(message);
	return ok;
}

struct waiting
{
	struct steam_session *session;
	uint32_t emsg;
	uint64_t job;
	int found;
};

static int keep_reply(struct steam_session *session, const struct steam_cm_message *message)
{
	if (session->reply_capacity < message->body_size + 1)
	{
		uint8_t *grown = malloc(message->body_size + 1);

		if (!grown)
			return 0;
		if (session->reply)
		{
			steam_wipe(session->reply, session->reply_capacity);
			free(session->reply);
		}
		session->reply = grown;
		session->reply_capacity = message->body_size + 1;
	}
	memcpy(session->reply, message->body, message->body_size);
	session->reply_size = message->body_size;
	session->reply_header = message->header;
	return 1;
}

static int handle_message(void *context, const struct steam_cm_message *message)
{
	struct waiting *waiting = context;
	struct steam_session *session = waiting->session;

	if (message->emsg == STEAM_EMSG_CLIENT_LOGGED_OFF && session->logged_on)
	{
		session->logged_on = 0;
		session->kicked = 1;
		say(session, "steam: the server signed this session off");
	}
	if (waiting->found)
		return 0;
	if (waiting->job ? message->header.job_target == waiting->job && message->emsg == waiting->emsg :
		message->emsg == waiting->emsg)
	{
		if (!keep_reply(session, message))
			return -1;
		waiting->found = 1;
	}
	return 0;
}

static void heartbeat(struct steam_session *session)
{
	long long now = now_ms();

	if (session->logged_on && session->heartbeat_seconds > 0 &&
		now - session->last_heartbeat_ms >= (long long)session->heartbeat_seconds * 1000)
	{
		send_message(session, STEAM_EMSG_CLIENT_HEARTBEAT, 0, NULL, NULL, 0);
		session->last_heartbeat_ms = now;
	}
}

/* waits for emsg (with job as its target, if job) for up to timeout_ms:
1 with the reply kept, 0 on a timeout or a broken session */
static int wait_for(struct steam_session *session, uint32_t emsg, uint64_t job, int timeout_ms)
{
	struct waiting waiting = { session, emsg, job, 0 };
	long long deadline = now_ms() + timeout_ms;

	for (;;)
	{
		const uint8_t *data;
		size_t size;
		struct steam_cm_message message;
		long long remaining = deadline - now_ms();
		int result;

		/* (a wait of 0 still sends a heartbeat that is due and reads what is
		there: steam_session_pump between chunks) */
		heartbeat(session);
		if (remaining < 0)
			remaining = 0;
		result = steam_ws_receive(&session->ws, &data, &size, remaining > 1000 ? 1000 : (int)remaining);
		if (result < 0)
		{
			session->connected = 0;
			return 0;
		}
		if (result == 0)
		{
			if (now_ms() >= deadline)
				return 0;
			continue;
		}
		if (!steam_cm_parse(data, size, &message))
		{
			say(session, "steam: a malformed message was dropped");
			steam_ws_wipe_message(&session->ws);
			continue;
		}
		if (message.emsg == STEAM_EMSG_MULTI)
		{
			if (steam_cm_multi(message.body, message.body_size, handle_message, &waiting) < 0)
				say(session, "steam: a malformed multi message was dropped");
		}
		else if (handle_message(&waiting, &message) < 0)
		{
			steam_ws_wipe_message(&session->ws);
			return 0;
		}
		/* (what was needed is copied out; the message may have held a token) */
		steam_ws_wipe_message(&session->ws);
		if (waiting.found)
			return 1;
		if (session->kicked && emsg != STEAM_EMSG_CLIENT_LOGGED_OFF)
			return 0;
	}
}

/* a service method call and its response's body (session->reply): 1, or 0
with a reason */
static int service_call(struct steam_session *session, const char *method, int authenticated, const void *body,
	size_t body_size, char *error, int error_size)
{
	uint64_t job = session->next_job++;

	if (!send_message(session, authenticated ? STEAM_EMSG_SERVICE_METHOD_CALL : STEAM_EMSG_SERVICE_METHOD_CALL_NONAUTHED,
		job, method, body, body_size))
	{
		snprintf(error, (size_t)error_size, "%s: cannot send", method);
		return 0;
	}
	if (!wait_for(session, STEAM_EMSG_SERVICE_METHOD_RESPONSE, job, CALL_TIMEOUT_MS))
	{
		snprintf(error, (size_t)error_size, "%s: no answer", method);
		return 0;
	}
	if (session->reply_header.eresult != STEAM_ERESULT_OK)
	{
		snprintf(error, (size_t)error_size, "%s: refused (result %d)", method, (int)session->reply_header.eresult);
		return 0;
	}
	return 1;
}

int steam_session_call(struct steam_session *session, const char *method, const void *body, size_t body_size,
	const uint8_t **reply, size_t *reply_size, char *error, int error_size)
{
	*reply = NULL;
	*reply_size = 0;
	if (!session->logged_on)
	{
		snprintf(error, (size_t)error_size, "%s: not signed in", method);
		return 0;
	}
	if (!service_call(session, method, 1, body, body_size, error, error_size))
		return 0;
	*reply = session->reply;
	*reply_size = session->reply_size;
	return 1;
}

/* a client message sent as a job and its response: 1, or 0 */
static int job_call(struct steam_session *session, uint32_t emsg, uint32_t response, const void *body, size_t body_size,
	char *error, int error_size)
{
	uint64_t job = session->next_job++;

	if (!send_message(session, emsg, job, NULL, body, body_size))
	{
		snprintf(error, (size_t)error_size, "cannot send message %u", (unsigned int)emsg);
		return 0;
	}
	if (!wait_for(session, response, job, CALL_TIMEOUT_MS))
	{
		snprintf(error, (size_t)error_size, "no answer to message %u", (unsigned int)emsg);
		return 0;
	}
	return 1;
}

/* ---------- the steps */

int steam_session_connect(struct steam_session *session, const char *url, char *error, int error_size)
{
	struct steam_stream stream;
	char host[STEAM_HOST_MAXIMUM + 8], path[512];
	uint8_t body[16];
	struct steam_pb_writer writer;

	if (!session->secrets)
	{
		snprintf(error, (size_t)error_size, "out of memory");
		return 0;
	}
	if (url != session->url)
		snprintf(session->url, sizeof(session->url), "%s", url);
	if (!steam_net_open(&stream, url, host, sizeof(host), path, sizeof(path), error, error_size))
		return 0;
	if (!steam_ws_open(&session->ws, &stream, host, path, CALL_TIMEOUT_MS, error, error_size))
		return 0;
	session->connected = 1;
	steam_pb_writer_init(&writer, body, sizeof(body));
	steam_pb_put_varint(&writer, 1, STEAM_PROTOCOL_VERSION);
	if (writer.error || !send_message(session, STEAM_EMSG_CLIENT_HELLO, 0, NULL, body, writer.size))
	{
		snprintf(error, (size_t)error_size, "cannot send ClientHello");
		return 0;
	}
	say(session, "steam: connected to %s", host);
	return 1;
}

static int copy_string(char *output, size_t capacity, const struct steam_pb_field *field)
{
	if (field->wire != STEAM_PB_BYTES || field->size >= capacity || memchr(field->data, 0, field->size))
		return 0;
	memcpy(output, field->data, field->size);
	output[field->size] = 0;
	return 1;
}

/* a challenge URL from Steam: printable ASCII, https:// */
static int challenge_url_ok(const char *url)
{
	const char *cursor;

	if (strncmp(url, "https://", 8))
		return 0;
	for (cursor = url; *cursor; cursor++)
		if ((unsigned char)*cursor <= ' ' || (unsigned char)*cursor >= 0x7f)
			return 0;
	return 1;
}

int steam_session_qr_begin(struct steam_session *session, char *url, size_t url_capacity, float *interval, char *error,
	int error_size)
{
	uint8_t body[256];
	struct steam_pb_writer writer;
	struct steam_pb_reader reader;
	struct steam_pb_field field;
	size_t details;
	int result, have_url = 0, have_client = 0;

	url[0] = 0;
	*interval = 5.0f;
	steam_pb_writer_init(&writer, body, sizeof(body));
	steam_pb_put_string(&writer, 1, STEAM_DEVICE_NAME);
	steam_pb_put_varint(&writer, 2, PLATFORM_STEAM_CLIENT);
	details = steam_pb_begin(&writer, 3);
	steam_pb_put_string(&writer, 1, STEAM_DEVICE_NAME);
	steam_pb_put_varint(&writer, 2, PLATFORM_STEAM_CLIENT);
	steam_pb_end(&writer, details);
	steam_pb_put_string(&writer, 4, "Client");
	if (writer.error || !service_call(session, "Authentication.BeginAuthSessionViaQR#1", 0, body, writer.size, error,
		error_size))
	{
		if (!error[0])
			snprintf(error, (size_t)error_size, "cannot begin a QR sign-in");
		return 0;
	}
	steam_pb_reader_init(&reader, session->reply, session->reply_size);
	while ((result = steam_pb_next(&reader, &field)) > 0)
	{
		switch (field.number)
		{
		case 1:
			session->secrets->qr_client_id = field.value;
			have_client = 1;
			break;
		case 2:
			have_url = copy_string(url, url_capacity, &field) && challenge_url_ok(url);
			break;
		case 3:
			if (field.wire != STEAM_PB_BYTES || field.size > sizeof(session->secrets->qr_request_id))
				result = -1;
			else
			{
				memcpy(session->secrets->qr_request_id, field.data, field.size);
				session->secrets->qr_request_id_size = field.size;
			}
			break;
		case 4:
			if (field.wire == STEAM_PB_FIXED32)
			{
				union
				{
					uint32_t bits;
					float value;
				} convert;

				convert.bits = (uint32_t)field.value;
				/* (between 1 and 30 seconds, whatever is sent) */
				if (convert.value >= 1.0f && convert.value <= 30.0f)
					*interval = convert.value;
			}
			break;
		}
		if (result < 0)
			break;
	}
	steam_wipe(session->reply, session->reply_capacity);
	if (result < 0 || !have_url || !have_client || !session->secrets->qr_request_id_size)
	{
		url[0] = 0;
		snprintf(error, (size_t)error_size, "malformed QR sign-in answer");
		return 0;
	}
	say(session, "steam: QR sign-in begun (poll every %.1f s)", (double)*interval);
	return 1;
}

static void forget_qr(struct steam_session *session)
{
	session->secrets->qr_client_id = 0;
	steam_wipe(session->secrets->qr_request_id, sizeof(session->secrets->qr_request_id));
	session->secrets->qr_request_id_size = 0;
}

int steam_session_qr_poll(struct steam_session *session, char *url, size_t url_capacity, char *error, int error_size)
{
	uint8_t body[128];
	struct steam_pb_writer writer;
	struct steam_pb_reader reader;
	struct steam_pb_field field;
	struct steam_secrets *secrets = session->secrets;
	int result, new_url = 0;

	if (!secrets->qr_request_id_size)
	{
		snprintf(error, (size_t)error_size, "no QR sign-in under way");
		return STEAM_QR_FAILED;
	}
	steam_pb_writer_init(&writer, body, sizeof(body));
	steam_pb_put_varint(&writer, 1, secrets->qr_client_id);
	steam_pb_put_bytes(&writer, 2, secrets->qr_request_id, secrets->qr_request_id_size);
	result = !writer.error && service_call(session, "Authentication.PollAuthSessionStatus#1", 0, body, writer.size,
		error, error_size);
	steam_wipe(body, sizeof(body));
	if (!result)
	{
		forget_qr(session);
		return STEAM_QR_FAILED;
	}
	steam_pb_reader_init(&reader, session->reply, session->reply_size);
	while ((result = steam_pb_next(&reader, &field)) > 0)
	{
		switch (field.number)
		{
		case 1:
			if (field.wire == STEAM_PB_VARINT)
				secrets->qr_client_id = field.value;
			break;
		case 2:
			if (copy_string(url, url_capacity, &field) && challenge_url_ok(url))
				new_url = 1;
			else
				url[0] = 0;
			break;
		case 3:
			if (!copy_string(secrets->refresh_token, sizeof(secrets->refresh_token), &field))
				result = -1;
			break;
		case 4:
			if (!copy_string(secrets->access_token, sizeof(secrets->access_token), &field))
				result = -1;
			break;
		case 6:
			if (!copy_string(secrets->account_name, sizeof(secrets->account_name), &field))
				result = -1;
			break;
		}
		if (result < 0)
			break;
	}
	steam_wipe(session->reply, session->reply_capacity);
	if (result < 0)
	{
		steam_wipe(secrets->refresh_token, sizeof(secrets->refresh_token));
		steam_wipe(secrets->access_token, sizeof(secrets->access_token));
		forget_qr(session);
		snprintf(error, (size_t)error_size, "malformed QR sign-in status");
		return STEAM_QR_FAILED;
	}
	if (secrets->refresh_token[0] && secrets->account_name[0])
	{
		forget_qr(session);
		say(session, "steam: QR sign-in approved (token: %u bytes)", (unsigned int)strlen(secrets->refresh_token));
		if (session->marker_path[0])
		{
			/* (the no-secret marker: the date and the device's name alone) */
			FILE *marker = fopen(session->marker_path, "wb");

			if (marker)
			{
				fprintf(marker, "Steam sign-in started %ld by %s; not signed out if this file is still here.\n",
					(long)time(NULL), STEAM_DEVICE_NAME);
				fclose(marker);
			}
		}
		return STEAM_QR_APPROVED;
	}
	return new_url ? STEAM_QR_NEW_URL : STEAM_QR_WAITING;
}

int steam_session_logon(struct steam_session *session, char *error, int error_size)
{
	struct steam_secrets *secrets = session->secrets;
	uint8_t *body;
	size_t capacity = STEAM_TOKEN_MAXIMUM + 512;
	struct steam_pb_writer writer;
	struct steam_pb_reader reader;
	struct steam_pb_field field;
	int result, eresult = 2, sent;

	if (!secrets->refresh_token[0])
	{
		snprintf(error, (size_t)error_size, "not signed in");
		return 0;
	}
	body = malloc(capacity);
	if (!body)
	{
		snprintf(error, (size_t)error_size, "out of memory");
		return 0;
	}
	steam_pb_writer_init(&writer, body, capacity);
	steam_pb_put_varint(&writer, 1, STEAM_PROTOCOL_VERSION);
	steam_pb_put_varint(&writer, 3, session->cell_id);
	steam_pb_put_string(&writer, 6, "english");
	steam_pb_put_varint(&writer, 8, 0);
	steam_pb_put_string(&writer, 50, secrets->account_name);
	steam_pb_put_string(&writer, 96, STEAM_DEVICE_NAME);
	steam_pb_put_string(&writer, 108, secrets->refresh_token);
	sent = !writer.error && send_message(session, STEAM_EMSG_CLIENT_LOGON, 0, NULL, body, writer.size);
	steam_wipe(body, capacity);
	free(body);
	if (!sent)
	{
		snprintf(error, (size_t)error_size, "cannot send the logon");
		return 0;
	}
	if (!wait_for(session, STEAM_EMSG_CLIENT_LOGON_RESPONSE, 0, CALL_TIMEOUT_MS))
	{
		snprintf(error, (size_t)error_size, "no answer to the logon");
		return 0;
	}
	steam_pb_reader_init(&reader, session->reply, session->reply_size);
	while ((result = steam_pb_next(&reader, &field)) > 0)
	{
		if (field.number == 1 && field.wire == STEAM_PB_VARINT)
			eresult = steam_pb_int32(&field);
		else if (field.number == 3 && field.wire == STEAM_PB_VARINT)
			session->heartbeat_seconds = steam_pb_int32(&field);
		else if (field.number == 7 && field.wire == STEAM_PB_VARINT)
			session->cell_id = (uint32_t)field.value;
	}
	if (result < 0 || eresult != STEAM_ERESULT_OK)
	{
		snprintf(error, (size_t)error_size, "logon refused (result %d)", eresult);
		return 0;
	}
	if (session->heartbeat_seconds < 5 || session->heartbeat_seconds > 300)
		session->heartbeat_seconds = 9;
	session->steamid = session->reply_header.steamid;
	session->session_id = session->reply_header.session_id;
	session->logged_on = 1;
	session->last_heartbeat_ms = now_ms();
	say(session, "steam: signed in (cell %u)", (unsigned int)session->cell_id);
	return 1;
}

int steam_session_pump(struct steam_session *session, int timeout_ms)
{
	/* (waits for nothing in particular: the heartbeats go, a log-off is seen) */
	wait_for(session, 0xffffffffu, 0, timeout_ms);
	return session->connected;
}

int steam_session_app_info(struct steam_session *session, uint32_t app, struct steam_kv *kv, char *error, int error_size)
{
	uint8_t body[64];
	struct steam_pb_writer writer;
	struct steam_pb_reader reader, inner;
	struct steam_pb_field field, item;
	size_t app_info;
	int result, pending = 1, rounds = 0, got = 0;

	memset(kv, 0, sizeof(*kv));
	/* the app's access token */
	steam_pb_writer_init(&writer, body, sizeof(body));
	steam_pb_put_varint(&writer, 2, app);
	if (writer.error || !job_call(session, STEAM_EMSG_CLIENT_PICS_ACCESS_TOKEN_REQUEST,
		STEAM_EMSG_CLIENT_PICS_ACCESS_TOKEN_RESPONSE, body, writer.size, error, error_size))
	{
		return 0;
	}
	steam_pb_reader_init(&reader, session->reply, session->reply_size);
	while ((result = steam_pb_next(&reader, &field)) > 0)
	{
		uint32_t token_app = 0;
		uint64_t token = 0;

		if (field.number != 3 || field.wire != STEAM_PB_BYTES)
			continue;
		steam_pb_reader_init(&inner, field.data, field.size);
		while (steam_pb_next(&inner, &item) > 0)
		{
			if (item.number == 1)
				token_app = (uint32_t)item.value;
			else if (item.number == 2)
				token = item.value;
		}
		if (token_app == app)
			session->secrets->app_access_token = token;
	}
	steam_wipe(session->reply, session->reply_capacity);
	/* the product info */
	steam_pb_writer_init(&writer, body, sizeof(body));
	app_info = steam_pb_begin(&writer, 2);
	steam_pb_put_varint(&writer, 1, app);
	if (session->secrets->app_access_token)
		steam_pb_put_varint(&writer, 2, session->secrets->app_access_token);
	steam_pb_end(&writer, app_info);
	steam_pb_put_varint(&writer, 7, 1);
	{
		uint64_t job = session->next_job++;

		if (writer.error || !send_message(session, STEAM_EMSG_CLIENT_PICS_PRODUCT_INFO_REQUEST, job, NULL, body,
			writer.size))
		{
			steam_wipe(body, sizeof(body));
			snprintf(error, (size_t)error_size, "cannot ask for the app's info");
			return 0;
		}
		steam_wipe(body, sizeof(body));
		/* (responses until one says nothing is pending) */
		while (pending && rounds++ < 16)
		{
			if (!wait_for(session, STEAM_EMSG_CLIENT_PICS_PRODUCT_INFO_RESPONSE, job, CALL_TIMEOUT_MS))
			{
				snprintf(error, (size_t)error_size, "no app info");
				return 0;
			}
			pending = 0;
			steam_pb_reader_init(&reader, session->reply, session->reply_size);
			while ((result = steam_pb_next(&reader, &field)) > 0)
			{
				if (field.number == 6 && field.wire == STEAM_PB_VARINT)
					pending = field.value != 0;
				if (field.number != 1 || field.wire != STEAM_PB_BYTES || got)
					continue;
				{
					const uint8_t *buffer = NULL;
					size_t buffer_size = 0;
					uint32_t info_app = 0;

					steam_pb_reader_init(&inner, field.data, field.size);
					while (steam_pb_next(&inner, &item) > 0)
					{
						if (item.number == 1)
							info_app = (uint32_t)item.value;
						else if (item.number == 5 && item.wire == STEAM_PB_BYTES)
							buffer = item.data, buffer_size = item.size;
					}
					if (info_app != app || !buffer)
						continue;
					if (!steam_kv_parse((const char *)buffer, buffer_size, kv, error, error_size))
						return 0;
					got = 1;
				}
			}
			if (result < 0)
			{
				steam_kv_free(kv);
				snprintf(error, (size_t)error_size, "malformed app info");
				return 0;
			}
		}
	}
	if (!got)
	{
		/* (Steam may instead name an HTTP host for a large appinfo: phase 2) */
		snprintf(error, (size_t)error_size, "no app info for %u (or it is offered over HTTP)", (unsigned int)app);
		return 0;
	}
	return 1;
}

const uint8_t *steam_session_depot_key(struct steam_session *session, uint32_t app, uint32_t depot, char *error,
	int error_size)
{
	struct steam_secrets *secrets = session->secrets;
	uint8_t body[32];
	struct steam_pb_writer writer;
	struct steam_pb_reader reader;
	struct steam_pb_field field;
	int index, result, eresult = 2;
	uint32_t key_depot = 0;
	const uint8_t *key = NULL;

	for (index = 0; index < secrets->depot_key_count; index++)
		if (secrets->depot_keys[index].depot == depot)
			return secrets->depot_keys[index].key;
	if (secrets->depot_key_count >= STEAM_DEPOT_KEYS_MAXIMUM)
	{
		snprintf(error, (size_t)error_size, "too many depot keys");
		return NULL;
	}
	steam_pb_writer_init(&writer, body, sizeof(body));
	steam_pb_put_varint(&writer, 1, depot);
	steam_pb_put_varint(&writer, 2, app);
	if (writer.error || !job_call(session, STEAM_EMSG_CLIENT_GET_DEPOT_KEY, STEAM_EMSG_CLIENT_GET_DEPOT_KEY_RESPONSE, body,
		writer.size, error, error_size))
	{
		return NULL;
	}
	steam_pb_reader_init(&reader, session->reply, session->reply_size);
	while ((result = steam_pb_next(&reader, &field)) > 0)
	{
		if (field.number == 1 && field.wire == STEAM_PB_VARINT)
			eresult = steam_pb_int32(&field);
		else if (field.number == 2 && field.wire == STEAM_PB_VARINT)
			key_depot = (uint32_t)field.value;
		else if (field.number == 3 && field.wire == STEAM_PB_BYTES && field.size == 32)
			key = field.data;
	}
	if (result < 0 || eresult != STEAM_ERESULT_OK || key_depot != depot || !key)
	{
		steam_wipe(session->reply, session->reply_capacity);
		snprintf(error, (size_t)error_size, "no key for depot %u (result %d): does this account own it?",
			(unsigned int)depot, eresult);
		return NULL;
	}
	index = secrets->depot_key_count++;
	secrets->depot_keys[index].depot = depot;
	memcpy(secrets->depot_keys[index].key, key, 32);
	steam_wipe(session->reply, session->reply_capacity);
	say(session, "steam: depot %u key received", (unsigned int)depot);
	return secrets->depot_keys[index].key;
}

int steam_session_manifest_code(struct steam_session *session, uint32_t app, uint32_t depot, uint64_t gid,
	const char *branch, uint64_t *code, char *error, int error_size)
{
	uint8_t body[96];
	struct steam_pb_writer writer;
	struct steam_pb_reader reader;
	struct steam_pb_field field;
	int result;

	*code = 0;
	steam_pb_writer_init(&writer, body, sizeof(body));
	steam_pb_put_varint(&writer, 1, app);
	steam_pb_put_varint(&writer, 2, depot);
	steam_pb_put_varint(&writer, 3, gid);
	steam_pb_put_string(&writer, 4, branch);
	if (writer.error || !service_call(session, "ContentServerDirectory.GetManifestRequestCode#1", 1, body, writer.size,
		error, error_size))
	{
		return 0;
	}
	steam_pb_reader_init(&reader, session->reply, session->reply_size);
	while ((result = steam_pb_next(&reader, &field)) > 0)
		if (field.number == 1 && field.wire == STEAM_PB_VARINT)
			*code = field.value;
	steam_wipe(session->reply, session->reply_capacity);
	if (result < 0 || !*code)
	{
		*code = 0;
		snprintf(error, (size_t)error_size, "no manifest request code for depot %u", (unsigned int)depot);
		return 0;
	}
	return 1;
}

int steam_host_ok(const char *host, size_t length)
{
	size_t index;

	if (!length || length > STEAM_HOST_MAXIMUM || host[0] == '.' || host[0] == '-' || host[length - 1] == '.')
		return 0;
	for (index = 0; index < length; index++)
	{
		char c = host[index];

		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.'))
			return 0;
		/* (no empty label) */
		if (c == '.' && index + 1 < length && host[index + 1] == '.')
			return 0;
	}
	return 1;
}

/* packed varints (an app list): 1 if app is among them, 0 if not, -1 if
malformed */
static int packed_has(const uint8_t *data, size_t size, uint32_t app)
{
	size_t cursor = 0;
	int found = 0;

	while (cursor < size)
	{
		uint64_t value = 0;
		unsigned int shift = 0, count = 0;
		uint8_t byte;

		do
		{
			if (cursor >= size || count++ >= 10)
				return -1;
			byte = data[cursor++];
			value |= (uint64_t)(byte & 0x7f) << shift;
			shift += 7;
		} while (byte & 0x80);
		if (value == app)
			found = 1;
	}
	return found;
}

int steam_parse_cdn_servers(const uint8_t *body, size_t size, uint32_t app, struct steam_cdn_server *servers,
	int maximum)
{
	struct steam_pb_reader reader, inner;
	struct steam_pb_field field, item;
	int count = 0, result;

	steam_pb_reader_init(&reader, body, size);
	while ((result = steam_pb_next(&reader, &field)) > 0)
	{
		char type[32] = "", host[STEAM_HOST_MAXIMUM + 1] = "", vhost[STEAM_HOST_MAXIMUM + 1] = "", https[32] = "";
		int proxy = 0, china = 0, allowed = 1, listed = 0, serves = 0, ok;
		const char *name;

		if (field.number != 1 || field.wire != STEAM_PB_BYTES)
			continue;
		steam_pb_reader_init(&inner, field.data, field.size);
		while ((ok = steam_pb_next(&inner, &item)) > 0)
		{
			switch (item.number)
			{
			case 1: allowed &= copy_string(type, sizeof(type), &item); break;
			case 7: china = item.value != 0; break;
			case 8: allowed &= copy_string(host, sizeof(host), &item); break;
			case 9: allowed &= copy_string(vhost, sizeof(vhost), &item); break;
			case 10: proxy = item.value != 0; break;
			case 12: allowed &= copy_string(https, sizeof(https), &item); break;
			case 13:
				/* (the apps it serves, if it lists them: ours must be there) */
				listed = 1;
				if (item.wire == STEAM_PB_VARINT)
					serves |= (uint32_t)item.value == app && item.value == app;
				else if (item.wire == STEAM_PB_BYTES)
				{
					int has = packed_has(item.data, item.size, app);

					if (has < 0)
						return -1;
					serves |= has;
				}
				break;
			}
		}
		if (ok < 0)
			return -1;
		name = vhost[0] ? vhost : host;
		/* (HTTPS only: plain HTTP is refused, docs/steam_download_security.md 3.3) */
		if (!allowed || proxy || china || (listed && !serves) || (strcmp(type, "SteamCache") && strcmp(type, "CDN")) ||
			!steam_host_ok(name, strlen(name)) || (strcmp(https, "mandatory") && strcmp(https, "optional")) ||
			count >= maximum)
		{
			continue;
		}
		snprintf(servers[count].host, sizeof(servers[count].host), "%s", name);
		servers[count].https = 1;
		count++;
	}
	return result < 0 ? -1 : count;
}

int steam_session_cdn_servers(struct steam_session *session, struct steam_cdn_server *servers, int maximum, char *error,
	int error_size)
{
	uint8_t body[32];
	struct steam_pb_writer writer;
	int count;

	steam_pb_writer_init(&writer, body, sizeof(body));
	steam_pb_put_varint(&writer, 1, session->cell_id);
	steam_pb_put_varint(&writer, 2, 20);
	if (writer.error || !service_call(session, "ContentServerDirectory.GetServersForSteamPipe#1", 1, body, writer.size,
		error, error_size))
	{
		return -1;
	}
	count = steam_parse_cdn_servers(session->reply, session->reply_size, 976730u, servers, maximum);
	if (count <= 0)
	{
		snprintf(error, (size_t)error_size, "no usable CDN server");
		return -1;
	}
	say(session, "steam: %d CDN servers", count);
	return count;
}

int steam_parse_cm_list(const char *json, size_t size, char urls[][STEAM_HOST_MAXIMUM + 32], int maximum)
{
	static const char key[] = "\"endpoint\"";
	size_t cursor = 0;
	int count = 0;

	if (size > (1u << 20))
		return 0;
	while (cursor + sizeof(key) < size && count < maximum)
	{
		const char *found = NULL;
		size_t index, start, end;
		char host[STEAM_HOST_MAXIMUM + 1];
		unsigned long port = 0;

		for (index = cursor; index + sizeof(key) - 1 <= size; index++)
			if (!memcmp(json + index, key, sizeof(key) - 1))
			{
				found = json + index;
				break;
			}
		if (!found)
			break;
		cursor = index + sizeof(key) - 1;
		while (cursor < size && (json[cursor] == ' ' || json[cursor] == ':'))
			cursor++;
		if (cursor >= size || json[cursor] != '"')
			continue;
		start = ++cursor;
		while (cursor < size && json[cursor] != '"' && json[cursor] != ':' && cursor - start <= STEAM_HOST_MAXIMUM)
			cursor++;
		end = cursor;
		if (cursor >= size || json[cursor] != ':' || !steam_host_ok(json + start, end - start))
			continue;
		memcpy(host, json + start, end - start);
		host[end - start] = 0;
		for (cursor++; cursor < size && json[cursor] >= '0' && json[cursor] <= '9' && port <= 65535; cursor++)
			port = port * 10 + (unsigned long)(json[cursor] - '0');
		if (cursor >= size || json[cursor] != '"' || !port || port > 65535)
			continue;
		snprintf(urls[count++], STEAM_HOST_MAXIMUM + 32, "wss://%s:%lu/cmsocket/", host, port);
	}
	return count;
}

void steam_session_end(struct steam_session *session)
{
	struct steam_secrets *secrets = session->secrets;

	if (session->ended)
		return;
	session->ended = 1;
	/* (the connection lost with the token live: a new one, only to revoke it) */
	if (secrets && !session->connected && secrets->refresh_token[0] && session->url[0])
	{
		char error[160];

		if (session->ws.stream.close || session->ws.input)
			steam_ws_close(&session->ws);
		session->logged_on = 0;
		if (steam_session_connect(session, session->url, error, sizeof(error)))
			say(session, "steam: connected again to revoke the sign-in");
		else
			say(session, "steam: could not connect again to revoke the sign-in");
	}
	if (secrets && session->connected && secrets->refresh_token[0])
	{
		uint8_t *body;
		size_t capacity = STEAM_TOKEN_MAXIMUM + 64;
		struct steam_pb_writer writer;
		uint64_t job = session->next_job++;
		int sent;

		body = malloc(capacity);
		if (body)
		{
			steam_pb_writer_init(&writer, body, capacity);
			steam_pb_put_string(&writer, 1, secrets->refresh_token);
			steam_pb_put_varint(&writer, 2, 1 /* k_EAuthTokenRevokePermanent */);
			sent = !writer.error && send_message(session, session->logged_on ? STEAM_EMSG_SERVICE_METHOD_CALL :
				STEAM_EMSG_SERVICE_METHOD_CALL_NONAUTHED, job, "Authentication.RevokeToken#1", body, writer.size);
			steam_wipe(body, capacity);
			free(body);
			if (sent && wait_for(session, STEAM_EMSG_SERVICE_METHOD_RESPONSE, job, END_TIMEOUT_MS) &&
				session->reply_header.eresult == STEAM_ERESULT_OK)
			{
				session->revoked = 1;
			}
		}
		say(session, session->revoked ? "steam: the sign-in was revoked" :
			"steam: the revoke was not confirmed: remove '" STEAM_DEVICE_NAME "' in the Steam app if it is listed");
	}
	if (session->connected && session->logged_on)
	{
		if (send_message(session, STEAM_EMSG_CLIENT_LOGOFF, 0, NULL, NULL, 0))
		{
			session->logged_off = 1;
			/* (the server's ClientLoggedOff, briefly) */
			wait_for(session, STEAM_EMSG_CLIENT_LOGGED_OFF, 0, 2000);
		}
		session->logged_on = 0;
	}
	if (session->connected || session->ws.input || session->ws.stream.close)
		steam_ws_close(&session->ws);
	session->connected = 0;
	if (session->revoked && session->marker_path[0])
		remove(session->marker_path);
	if (secrets)
	{
		steam_wipe(secrets, sizeof(*secrets));
		free(secrets);
		session->secrets = NULL;
	}
	if (session->reply)
	{
		steam_wipe(session->reply, session->reply_capacity);
		free(session->reply);
		session->reply = NULL;
	}
	session->reply_capacity = session->reply_size = 0;
	say(session, "steam: session ended");
}
