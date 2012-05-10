/*
 * Copyright (c) 2012 David Vossel <dvossel@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <crm_internal.h>

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

#include <sys/types.h>
#include <sys/wait.h>

#include <glib.h>
#include <dirent.h>

#include <crm/crm.h>
#include <crm/lrmd.h>
#include <crm/services.h>
#include <crm/common/mainloop.h>
#include <crm/msg_xml.h>

CRM_TRACE_INIT_DATA(lrmd);

typedef struct lrmd_key_value_s {
	char *key;
	char *value;
	struct lrmd_key_value_s *next;
} lrmd_key_value_t;

typedef struct lrmd_private_s {
	int call_id;

	char *token;
	crm_ipc_t *ipc;
	mainloop_io_t *source;

	lrmd_event_callback callback;
	void *callback_userdata;

} lrmd_private_t;

static lrmd_list_t *
lrmd_list_add(lrmd_list_t *head, const char *value)
{
	lrmd_list_t *p, *end;

	crm_malloc0(p, sizeof(lrmd_list_t));
	p->val = crm_strdup(value);

	end = head;
	while (end && end->next) {
		end = end->next;
	}

	if (end) {
		end->next = p;
	} else {
		head = p;
	}

	return head;
}

void
lrmd_list_freeall(lrmd_list_t *head)
{
	lrmd_list_t *p;
	while (head) {
		char *val = (char *) head->val;
		p = head->next;
		crm_free(val);
		crm_free(head);
		head = p;
	}
}

lrmd_key_value_t *
lrmd_key_value_add(lrmd_key_value_t *head, const char *key, const char *value)
{
	lrmd_key_value_t *p, *end;

	crm_malloc0(p, sizeof(lrmd_key_value_t));
	p->key = crm_strdup(key);
	p->value = crm_strdup(value);

	end = head;
	while (end && end->next) {
		end = end->next;
	}

	if (end) {
		end->next = p;
	} else {
		head = p;
	}

	return head;
}

static void
lrmd_key_value_freeall(lrmd_key_value_t * head)
{
	lrmd_key_value_t *p;
	while (head) {
		p = head->next;
		crm_free(head->key);
		crm_free(head->value);
		crm_free(head);
		head = p;
	}
}

static int
lrmd_dispatch_internal(const char *buffer, ssize_t length, gpointer userdata)
{
	const char *type;
	lrmd_t *lrmd = userdata;
	lrmd_private_t *native = lrmd->private;
	lrmd_event_data_t event = { 0, };
	xmlNode *msg;

	if (!native->callback) {
		/* no callback set */
		return 1;
	}

	msg = string2xml(buffer);
	type = crm_element_value(msg, F_LRMD_OPERATION);
	crm_element_value_int(msg, F_LRMD_RC, &event.rc);
	crm_element_value_int(msg, F_LRMD_CALLID, &event.call_id);
	event.rsc_id = crm_element_value(msg, F_LRMD_RSC_ID);

	if (crm_str_eq(type, LRMD_OP_RSC_REG, TRUE)) {
		event.type = lrmd_event_register;
	} else if (crm_str_eq(type, LRMD_OP_RSC_UNREG, TRUE)) {
		event.type = lrmd_event_unregister;
	} else if (crm_str_eq(type, LRMD_OP_RSC_EXEC, TRUE)) {
		crm_element_value_int(msg, F_LRMD_EXEC_RC, &event.exec_rc);
		crm_element_value_int(msg, F_LRMD_OP_STATUS, &event.lrmd_op_status);
		event.action = crm_element_value(msg, F_LRMD_RSC_ACTION);
		event.type = lrmd_event_exec_complete;
	}

	native->callback(&event, native->callback_userdata);
	free_xml(msg);
	return 1;
}

static xmlNode *
lrmd_create_op(int call_id,
	const char *token,
	const char *op, xmlNode *data,
	enum lrmd_call_options options)
{
	xmlNode *op_msg = create_xml_node(NULL, "lrmd_command");

	CRM_CHECK(op_msg != NULL, return NULL);
	CRM_CHECK(token != NULL, return NULL);

	crm_xml_add(op_msg, F_XML_TAGNAME, "lrmd_command");

	crm_xml_add(op_msg, F_TYPE, T_LRMD);
	crm_xml_add(op_msg, F_LRMD_CALLBACK_TOKEN, token);
	crm_xml_add(op_msg, F_LRMD_OPERATION, op);
	crm_xml_add_int(op_msg, F_LRMD_CALLID, call_id);
	crm_trace("Sending call options: %.8lx, %d", (long)options, options);
	crm_xml_add_int(op_msg, F_LRMD_CALLOPTS, options);

	if (data != NULL) {
		add_message_xml(op_msg, F_LRMD_CALLDATA, data);
	}

	return op_msg;
}

static void
lrmd_connection_destroy(gpointer user_data)
{
	crm_info("connection destroyed");
}

static int
lrmd_send_command(lrmd_t * lrmd,
	const char *op,
	xmlNode *data,
	xmlNode **output_data,
	int timeout, /* ms. defaults to 1000 if set to 0 */
	enum lrmd_call_options options)
{
	int rc = lrmd_ok;
	int reply_id = -1;
	lrmd_private_t *native = lrmd->private;
	xmlNode *op_msg = NULL;
	xmlNode *op_reply = NULL;

	if (!native->ipc) {
		return lrmd_err_connection;
	}

	if (op == NULL) {
		crm_err("No operation specified");
		return lrmd_err_missing;
	}

	native->call_id++;
	if (native->call_id < 1) {
		native->call_id = 1;
	}

	CRM_CHECK(native->token != NULL,;);

	op_msg = lrmd_create_op(native->call_id,
		native->token,
		op,
		data,
		options);

	if (op_msg == NULL) {
		return lrmd_err_missing;
	}

	crm_xml_add_int(op_msg, F_LRMD_TIMEOUT, timeout);

	timeout = !timeout ? 1000 : timeout;
	rc = crm_ipc_send(native->ipc, op_msg, &op_reply, timeout);
	free_xml(op_msg);

	if (rc < 0) {
		crm_perror(LOG_ERR, "Couldn't perform %s operation (timeout=%d): %d", op, timeout, rc);
		rc = lrmd_err_ipc;
		goto done;
	}

	rc = lrmd_ok;
	crm_element_value_int(op_reply, F_LRMD_CALLID, &reply_id);
	if (reply_id == native->call_id) {
		crm_trace("reply received");
		if (crm_element_value_int(op_reply, F_LRMD_RC, &rc) != 0) {
			rc = lrmd_err_peer;
			goto done;
		}

		if (output_data) {
			*output_data = op_reply;
			op_reply = NULL; /* Prevent subsequent free */
		}

	} else if (reply_id <= 0) {
		crm_err("Recieved bad reply: No id set");
		crm_log_xml_err(op_reply, "Bad reply");
		rc = lrmd_err_peer;
	} else {
		crm_err("Recieved bad reply: %d (wanted %d)", reply_id, native->call_id);
		crm_log_xml_err(op_reply, "Old reply");
		rc = lrmd_err_peer;
	}

	crm_log_xml_trace(op_reply, "Reply");

done:
	if (crm_ipc_connected(native->ipc) == FALSE) {
		crm_err("LRMD disconnected");
	}

	free_xml(op_reply);
	return rc;
}

static int
lrmd_api_connect(lrmd_t * lrmd, const char *name, int *lrmd_fd)
{
	int rc = lrmd_ok;
	lrmd_private_t *native = lrmd->private;
	static struct ipc_client_callbacks lrmd_callbacks = {
		.dispatch = lrmd_dispatch_internal,
		.destroy = lrmd_connection_destroy
	};

	crm_info("Connecting to lrmd");

	if (lrmd_fd) {
		/* No mainloop */
		native->ipc = crm_ipc_new("lrmd", 0);
		if (native->ipc && crm_ipc_connect(native->ipc)) {
			*lrmd_fd = crm_ipc_get_fd(native->ipc);
		} else if (native->ipc) {
			rc = lrmd_err_connection;
		}

	} else {
		/* With mainloop */
		native->source = mainloop_add_ipc_client("lrmd", 0, lrmd, &lrmd_callbacks);
		native->ipc = mainloop_get_ipc_client(native->source);
	}

	if (native->ipc == NULL) {
		crm_debug("Could not connect to the LRMD API");
		rc = lrmd_err_connection;
	}

	if (!rc) {
		xmlNode *reply = NULL;
		xmlNode *hello = create_xml_node(NULL, "lrmd_command");

		crm_xml_add(hello, F_TYPE, T_LRMD);
		crm_xml_add(hello, F_LRMD_OPERATION, CRM_OP_REGISTER);
		crm_xml_add(hello, F_LRMD_CLIENTNAME, name);

		rc = crm_ipc_send(native->ipc, hello, &reply, -1);

		if (rc < 0) {
			crm_perror(LOG_DEBUG, "Couldn't complete registration with the lrmd API: %d", rc);
			rc = lrmd_err_ipc;
		} else if(reply == NULL) {
			crm_err("Did not receive registration reply");
			rc = lrmd_err_internal;
		} else {
			const char *msg_type = crm_element_value(reply, F_LRMD_OPERATION);
			const char *tmp_ticket = crm_element_value(reply, F_LRMD_CLIENTID);

			if (safe_str_neq(msg_type, CRM_OP_REGISTER)) {
				crm_err("Invalid registration message: %s", msg_type);
				crm_log_xml_err(reply, "Bad reply");
				rc = lrmd_err_internal;
			} else if (tmp_ticket == NULL) {
				crm_err("No registration token provided");
				crm_log_xml_err(reply, "Bad reply");
				rc = lrmd_err_internal;
			} else {
				crm_trace("Obtained registration token: %s", tmp_ticket);
				native->token = crm_strdup(tmp_ticket);
				rc = lrmd_ok;
			}
		}

		free_xml(reply);
		free_xml(hello);
	}

	return rc;
}

static int
lrmd_api_disconnect(lrmd_t * lrmd)
{
	lrmd_private_t *native = lrmd->private;

	crm_info("Disconnecting from lrmd service");

	if (native->source) {
		mainloop_del_ipc_client(native->source);
		native->source = NULL;
		native->ipc = NULL;
	} else if (native->ipc != NULL) {
		crm_ipc_close(native->ipc);
		crm_ipc_destroy(native->ipc);
		native->ipc = NULL;
	}

	crm_free(native->token);
	return 0;
}

static int
lrmd_api_register_rsc(lrmd_t *lrmd,
		const char *rsc_id,
		const char *class,
		const char *provider,
		const char *type,
		enum lrmd_call_options options)
{
	int rc = lrmd_ok;
	xmlNode *data = create_xml_node(NULL, F_LRMD_RSC);

	crm_xml_add(data, F_LRMD_ORIGIN, __FUNCTION__);
	crm_xml_add(data, F_LRMD_RSC_ID, rsc_id);
	crm_xml_add(data, F_LRMD_CLASS, class);
	crm_xml_add(data, F_LRMD_PROVIDER, provider);
	crm_xml_add(data, F_LRMD_TYPE, type);
	rc = lrmd_send_command(lrmd, LRMD_OP_RSC_REG, data, NULL, 0, options);
	free_xml(data);

	return rc;
}

static int
lrmd_api_unregister_rsc(lrmd_t *lrmd,
	const char *rsc_id,
	enum lrmd_call_options options)
{
	int rc = lrmd_ok;
	xmlNode *data = create_xml_node(NULL, F_LRMD_RSC);

	crm_xml_add(data, F_LRMD_ORIGIN, __FUNCTION__);
	crm_xml_add(data, F_LRMD_RSC_ID, rsc_id);
	rc = lrmd_send_command(lrmd, LRMD_OP_RSC_UNREG, data, NULL, 0, options);
	free_xml(data);

	return rc;
}

static void
lrmd_api_set_callback(lrmd_t *lrmd, void *userdata, lrmd_event_callback callback)
{
	lrmd_private_t *native = lrmd->private;

	native->callback = callback;
	native->callback_userdata = userdata;
}

static int
lrmd_api_get_metadata(lrmd_t *lrmd,
	const char *class,
	const char *provider,
	const char *type,
	char **output,
	enum lrmd_call_options options)
{
	svc_action_t *action = NULL;

	if (!class || !provider || !type) {
		return lrmd_err_missing;
	}

	action = resources_action_create("get_meta",
		class,
		provider,
		type,
		"meta-data",
		0,
		1000,
		NULL);

	if (!(services_action_sync(action))) {
		crm_err("Failed to retrieve meta-data for %s:%s:%s", class, provider, type);
		services_action_free(action);
		return lrmd_err_no_metadata;
	}

	if (!action->stdout_data) {
		crm_err("Failed to retrieve meta-data for %s:%s:%s", class, provider, type);
		services_action_free(action);
		return lrmd_err_no_metadata;
	}

	*output = crm_strdup(action->stdout_data);
	services_action_free(action);

	return lrmd_ok;
}

static int
lrmd_api_exec(lrmd_t *lrmd,
	const char *rsc_id,
	const char *action,
	int interval, /* ms */
	int timeout, /* ms */
	int start_delay, /* ms */
	enum lrmd_call_options options,
	lrmd_key_value_t *params)
{
	int rc = lrmd_ok;
	xmlNode *data = create_xml_node(NULL, F_LRMD_RSC);
	xmlNode *args = create_xml_node(data, XML_TAG_ATTRS);

	crm_xml_add(data, F_LRMD_ORIGIN, __FUNCTION__);
	crm_xml_add(data, F_LRMD_RSC_ID, rsc_id);
	crm_xml_add(data, F_LRMD_RSC_ACTION, action);
	crm_xml_add_int(data, F_LRMD_RSC_INTERVAL, interval);
	crm_xml_add_int(data, F_LRMD_RSC_TIMEOUT, timeout);
	crm_xml_add_int(data, F_LRMD_RSC_START_DELAY, start_delay);

	for (; params; params = params->next) {
		hash2field((gpointer) params->key, (gpointer) params->value, args);
	}

	rc = lrmd_send_command(lrmd, LRMD_OP_RSC_EXEC, data, NULL, timeout, options);
	free_xml(data);

	lrmd_key_value_freeall(params);
	return rc;
}

static int
lrmd_api_cancel(lrmd_t *lrmd,
		const char *rsc_id,
		const char *action,
		int interval)
{
	int rc = lrmd_ok;
	xmlNode *data = create_xml_node(NULL, F_LRMD_RSC);

	crm_xml_add(data, F_LRMD_ORIGIN, __FUNCTION__);
	crm_xml_add(data, F_LRMD_RSC_ACTION, action);
	crm_xml_add(data, F_LRMD_RSC_ID, rsc_id);
	crm_xml_add_int(data, F_LRMD_RSC_INTERVAL, interval);
	rc = lrmd_send_command(lrmd, LRMD_OP_RSC_CANCEL, data, NULL, 0, 0);
	free_xml(data);
	return rc;
}

static int
lrmd_api_list_agents(lrmd_t *lrmd, lrmd_list_t **resources)
{
	int rc = lrmd_ok;
	char *provider = NULL;
	GList *ocf_providers = NULL;
	GList *lsb_providers = NULL;
	GList *agents = NULL;
	GListPtr gIter = NULL;
	GListPtr gIter2 = NULL;

	ocf_providers = resources_list_providers("ocf");
	lsb_providers = resources_list_providers("lsb");

	for (gIter = ocf_providers; gIter != NULL; gIter = gIter->next) {
		provider = gIter->data;

		agents = resources_list_agents("ocf", provider);
		for (gIter2 = agents; gIter2 != NULL; gIter2 = gIter2->next) {
			*resources = lrmd_list_add(*resources, (const char *) gIter2->data);
			crm_free(gIter2->data);
			rc++;
		}
		g_list_free(agents);
		crm_free(provider);
	}

	for (gIter = lsb_providers; gIter != NULL; gIter = gIter->next) {
		provider = gIter->data;

		agents = resources_list_agents("lsb", provider);
		for (gIter2 = agents; gIter2 != NULL; gIter2 = gIter2->next) {
			*resources = lrmd_list_add(*resources, (const char *) gIter2->data);
			crm_free(gIter2->data);
			rc++;
		}
		g_list_free(agents);
		crm_free(provider);
	}

	g_list_free(ocf_providers);
	g_list_free(lsb_providers);

	return rc;
}

static int
lrmd_api_list_providers(lrmd_t *lrmd, lrmd_list_t **providers)
{
	int rc = lrmd_ok;
	GList *ocf_providers = NULL;
	GList *lsb_providers = NULL;
	GListPtr gIter = NULL;

	ocf_providers = resources_list_providers("ocf");
	lsb_providers = resources_list_providers("lsb");

	for (gIter = ocf_providers; gIter != NULL; gIter = gIter->next) {
		*providers = lrmd_list_add(*providers, (const char *) gIter->data);
		crm_free(gIter->data);
		rc++;
	}

	for (gIter = lsb_providers; gIter != NULL; gIter = gIter->next) {
		*providers = lrmd_list_add(*providers, (const char *) gIter->data);
		crm_free(gIter->data);
		rc++;
	}

	g_list_free(ocf_providers);
	g_list_free(lsb_providers);

	return rc;
}

lrmd_t *
lrmd_api_new(void)
{
	lrmd_t *new_lrmd = NULL;
	lrmd_private_t *pvt = NULL;

	crm_malloc0(new_lrmd, sizeof(lrmd_t));
	crm_malloc0(pvt, sizeof(lrmd_private_t));
	crm_malloc0(new_lrmd->cmds, sizeof(lrmd_api_operations_t));

	new_lrmd->private = pvt;

	new_lrmd->cmds->connect = lrmd_api_connect;
	new_lrmd->cmds->disconnect = lrmd_api_disconnect;
	new_lrmd->cmds->register_rsc = lrmd_api_register_rsc;
	new_lrmd->cmds->unregister_rsc = lrmd_api_unregister_rsc;
	new_lrmd->cmds->set_callback = lrmd_api_set_callback;
	new_lrmd->cmds->get_metadata = lrmd_api_get_metadata;
	new_lrmd->cmds->exec = lrmd_api_exec;
	new_lrmd->cmds->cancel = lrmd_api_cancel;
	new_lrmd->cmds->list_agents = lrmd_api_list_agents;
	new_lrmd->cmds->list_providers = lrmd_api_list_providers;

	return new_lrmd;
}

void
lrmd_api_delete(lrmd_t * lrmd)
{
	lrmd->cmds->disconnect(lrmd); /* no-op if already disconnected */
	crm_free(lrmd->cmds);
	crm_free(lrmd->private);
	crm_free(lrmd);
}
