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

#include <glib.h>
#include <unistd.h>

#include <crm/crm.h>
#include <crm/services.h>
#include <crm/common/mainloop.h>
#include <crm/common/ipc.h>

#include <lrmd_private.h>

GHashTable *rsc_list = NULL;
GHashTable *client_list = NULL;

#ifdef _TEST
static void
dump_xml(const char *description, xmlNode *request)
{
	char *dump;

	dump = request ? dump_xml_formatted(request) : NULL;
	crm_info("%s = %s", description, dump);
	crm_free(dump);
}
#endif

static void
send_reply(lrmd_client_t *client, int rc, int call_id)
{
	int send_rc = 0;
	xmlNode *reply = NULL;

	reply = create_xml_node(NULL, T_LRMD_REPLY);
	crm_xml_add(reply, F_LRMD_ORIGIN, __FUNCTION__);
	crm_xml_add_int(reply, F_LRMD_RC, rc);
	crm_xml_add_int(reply, F_LRMD_CALLID, call_id);

	send_rc = crm_ipcs_send(client->channel, reply, FALSE);

	free_xml(reply);
	if (send_rc < 0)  {
		crm_warn("LRMD reply to %s failed: %d", client->name, send_rc);
	}
}

static void
send_client_notify(gpointer key, gpointer value, gpointer user_data)
{
	xmlNode *update_msg = user_data;
	lrmd_client_t *client = value;

	if (client == NULL) {
		crm_trace("Skipping NULL client");
		return;
	} else if (client->channel == NULL) {
		crm_trace("Skipping client with NULL channel");
		return;
	} else if (client->name == NULL) {
		crm_trace("Skipping unnammed client / comamnd channel");
		return;
	}

	if (crm_ipcs_send(client->channel, update_msg, TRUE) <= 0) {
		crm_warn("Notification of client %s/%s failed",
			client->name, client->id);
	}
}

static void
send_notify(lrmd_client_t *client, int rc, xmlNode *request)
{
	int call_id = 0;
	xmlNode *notify = NULL;
	xmlNode *rsc = get_xpath_object("//"F_LRMD_RSC, request, LOG_ERR);
	const char *rsc_id = crm_element_value(rsc, F_LRMD_RSC_ID);
	const char *op = crm_element_value(request, F_LRMD_OPERATION);

	crm_element_value_int(request, F_LRMD_CALLID, &call_id);

	notify = create_xml_node(NULL, T_LRMD_NOTIFY);
	crm_xml_add(notify, F_LRMD_ORIGIN, __FUNCTION__);
	crm_xml_add_int(notify, F_LRMD_RC, rc);
	crm_xml_add_int(notify, F_LRMD_CALLID, call_id);
	crm_xml_add(notify, F_LRMD_OPERATION, op);
	crm_xml_add(notify, F_LRMD_RSC_ID, rsc_id);

	g_hash_table_foreach(client_list, send_client_notify, notify);

	crm_free(notify);
}

static gboolean
lrmd_rsc_execute(lrmd_rsc_t *rsc)
{
	/* TODO implement */
	return TRUE;
}

static gboolean
lrmd_rsc_dispatch(gpointer user_data)
{
	return lrmd_rsc_execute(user_data);
}

void
free_rsc(gpointer data)
{
	lrmd_rsc_t *rsc = data;

	crm_free(rsc->rsc_id);
	crm_free(rsc->class);
	crm_free(rsc->provider);
	crm_free(rsc->type);
	mainloop_destroy_trigger(rsc->work);

	crm_free(rsc);
}

static lrmd_rsc_t *
build_rsc_from_xml(xmlNode *msg)
{
	xmlNode *dev = get_xpath_object("//"F_LRMD_RSC, msg, LOG_ERR);
	lrmd_rsc_t *rsc = NULL;

	crm_malloc0(rsc, sizeof(lrmd_rsc_t));
	rsc->rsc_id = crm_element_value_copy(dev, F_LRMD_RSC_ID);
	rsc->class = crm_element_value_copy(dev, F_LRMD_CLASS);
	rsc->provider = crm_element_value_copy(dev, F_LRMD_PROVIDER);
	rsc->type = crm_element_value_copy(dev, F_LRMD_TYPE);
	rsc->work = mainloop_add_trigger(G_PRIORITY_HIGH, lrmd_rsc_dispatch, rsc);
	return rsc;
}

static int
process_lrmd_signon(lrmd_client_t *client, xmlNode *request)
{
	xmlNode *reply = create_xml_node(NULL, "reply");
	crm_xml_add(reply, F_LRMD_OPERATION, CRM_OP_REGISTER);
	crm_xml_add(reply, F_LRMD_CLIENTID,  client->id);
	crm_ipcs_send(client->channel, reply, FALSE);

	free_xml(reply);
	return lrmd_ok;
}

static int
process_lrmd_rsc_register(lrmd_client_t *client, xmlNode *request)
{
	int rc = lrmd_ok;
	lrmd_rsc_t *rsc = build_rsc_from_xml(request);

	g_hash_table_replace(rsc_list, rsc->rsc_id, rsc);
	crm_info("Added '%s' to the rsc list (%d active resources)",
		rsc->rsc_id, g_hash_table_size(rsc_list));

	return rc;
}

static int
process_lrmd_rsc_unregister(lrmd_client_t *client, xmlNode *request)
{
	int rc = lrmd_ok;
	xmlNode *rsc = get_xpath_object("//"F_LRMD_RSC, request, LOG_ERR);
	const char *rsc_id = crm_element_value(rsc, F_LRMD_RSC_ID);

	if (!rsc_id) {
		return lrmd_err_unknown_rsc;
	}

	if (g_hash_table_remove(rsc_list, rsc_id)) {
		crm_info("Removed '%s' from the resource list (%d active resources)",
			rsc_id, g_hash_table_size(rsc_list));
	} else {
		crm_info("Resource '%s' not found (%d active resources)",
			rsc_id, g_hash_table_size(rsc_list));
	}

	return rc;
}

void
process_lrmd_message(lrmd_client_t *client, xmlNode *request)
{
	int rc = lrmd_ok;
	int call_options = 0;
	int call_id = 0;
	const char *op = crm_element_value(request, F_LRMD_OPERATION);
	int do_reply = 1;

	crm_element_value_int(request, F_LRMD_CALLOPTS, &call_options);
	crm_element_value_int(request, F_LRMD_CALLID, &call_id);

#ifdef _TEST
	crm_info("processing client request");
	dump_xml("request", request);
#endif

	if (crm_str_eq(op, CRM_OP_REGISTER, TRUE)) {
		rc = process_lrmd_signon(client, request);
		do_reply = 0;
	} else if (crm_str_eq(op, LRMD_OP_RSC_REG, TRUE)) {
		rc = process_lrmd_rsc_register(client, request);
		send_notify(client, rc, request);
	} else if (crm_str_eq(op, LRMD_OP_RSC_UNREG, TRUE)) {
		rc = process_lrmd_rsc_unregister(client, request);
		send_notify(client, rc, request);
	} else {
		rc = lrmd_err_unknown_operation;
		crm_err("Unknown %s from %s", op, client->name);
		crm_log_xml_warn(request, "UnknownOp");
	}

	if (do_reply) {
		send_reply(client, rc, call_id);
	}
}

