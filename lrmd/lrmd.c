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

typedef struct lrmd_cmd_s {
	int timeout;
	int interval;
	int start_delay;

	int call_id;
	int rc;
	int exec_rc;
	int lrmd_op_status;

	/* Timer ids, must be removed on cmd destruction. */
	int delay_id;
	int stonith_recurring_id;

	char *origin;
	char *rsc_id;
	char *action;

	GHashTable *params;
} lrmd_cmd_t;

static void cmd_finalize(lrmd_cmd_t *cmd, lrmd_rsc_t *rsc);
static gboolean lrmd_rsc_dispatch(gpointer user_data);

static lrmd_rsc_t *
build_rsc_from_xml(xmlNode *msg)
{
	xmlNode *rsc_xml = get_xpath_object("//"F_LRMD_RSC, msg, LOG_ERR);
	lrmd_rsc_t *rsc = NULL;

	crm_malloc0(rsc, sizeof(lrmd_rsc_t));
	rsc->rsc_id = crm_element_value_copy(rsc_xml, F_LRMD_RSC_ID);
	rsc->class = crm_element_value_copy(rsc_xml, F_LRMD_CLASS);
	rsc->provider = crm_element_value_copy(rsc_xml, F_LRMD_PROVIDER);
	rsc->type = crm_element_value_copy(rsc_xml, F_LRMD_TYPE);
	rsc->work = mainloop_add_trigger(G_PRIORITY_HIGH, lrmd_rsc_dispatch, rsc);
	return rsc;
}

static lrmd_cmd_t *
create_lrmd_cmd(xmlNode *msg)
{
	xmlNode *rsc_xml = get_xpath_object("//"F_LRMD_RSC, msg, LOG_ERR);
	lrmd_cmd_t *cmd = NULL;

	crm_malloc0(cmd, sizeof(lrmd_cmd_t));


	crm_element_value_int(msg, F_LRMD_CALLID, &cmd->call_id);

	crm_element_value_int(rsc_xml, F_LRMD_RSC_INTERVAL, &cmd->interval);
	crm_element_value_int(rsc_xml, F_LRMD_RSC_TIMEOUT, &cmd->timeout);
	crm_element_value_int(rsc_xml, F_LRMD_RSC_START_DELAY, &cmd->start_delay);

	cmd->origin = crm_element_value_copy(rsc_xml, F_LRMD_ORIGIN);
	cmd->action = crm_element_value_copy(rsc_xml, F_LRMD_RSC_ACTION);
	cmd->rsc_id = crm_element_value_copy(rsc_xml, F_LRMD_RSC_ID);

	cmd->params = xml2list(rsc_xml);

	return cmd;
}

static void
free_lrmd_cmd(lrmd_cmd_t *cmd)
{
	if (cmd->stonith_recurring_id) {
		g_source_remove(cmd->stonith_recurring_id);
	}
	if (cmd->delay_id) {
		g_source_remove(cmd->delay_id);
	}
	if (cmd->params) {
		g_hash_table_destroy(cmd->params);
	}
	crm_free(cmd->origin);
	crm_free(cmd->action);
	crm_free(cmd->rsc_id);
	crm_free(cmd);
}

static gboolean
stonith_recurring_op_helper(gpointer data)
{
	lrmd_cmd_t *cmd = data;
	lrmd_rsc_t *rsc = NULL;

	cmd->stonith_recurring_id = 0;

	rsc = cmd->rsc_id ? g_hash_table_lookup(rsc_list, cmd->rsc_id) : NULL;

	if (!rsc) {
		/* This will never happen, but for the sake of completion
		 * this is what should happen if it did. */
		cmd->rc = lrmd_ok;
		cmd->lrmd_op_status = PCMK_LRM_OP_CANCELLED;
		cmd_finalize(cmd, NULL);
	} else {
		/* take it out of recurring_ops list, and put it in the pending ops
		 * to be executed */
		rsc->recurring_ops = g_list_remove(rsc->recurring_ops, cmd);
		rsc->pending_ops = g_list_append(rsc->pending_ops, cmd);
		mainloop_set_trigger(rsc->work);
	}

	return FALSE;
}

static gboolean
start_delay_helper(gpointer data)
{
	lrmd_cmd_t *cmd = data;
	lrmd_rsc_t *rsc = NULL;

	cmd->delay_id = 0;
	rsc = cmd->rsc_id ? g_hash_table_lookup(rsc_list, cmd->rsc_id) : NULL;

	if (rsc) {
		mainloop_set_trigger(rsc->work);
	}

	return FALSE;
}

static void
schedule_lrmd_cmd(lrmd_rsc_t *rsc, lrmd_cmd_t *cmd)
{
	CRM_CHECK(cmd != NULL, return);
	CRM_CHECK(rsc != NULL, return);

	crm_trace("Scheduling %s on %s", cmd->action, rsc->rsc_id);
	rsc->pending_ops = g_list_append(rsc->pending_ops, cmd);
	mainloop_set_trigger(rsc->work);

	if (cmd->start_delay) {
		cmd->delay_id = g_timeout_add(cmd->start_delay, start_delay_helper, cmd);
	}
}

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
		crm_err("Asked to send event to  NULL client");
		return;
	} else if (client->channel == NULL) {
		crm_trace("Asked to send event to disconnected client");
		return;
	} else if (client->name == NULL) {
		crm_trace("Asked to send event to client with no name");
		return;
	}

	if (crm_ipcs_send(client->channel, update_msg, TRUE) <= 0) {
		crm_warn("Notification of client %s/%s failed",
			client->name, client->id);
	}
}

static void
send_cmd_complete_notify(lrmd_cmd_t *cmd)
{
	xmlNode *notify = NULL;
	notify = create_xml_node(NULL, T_LRMD_NOTIFY);

	crm_xml_add(notify, F_LRMD_ORIGIN, __FUNCTION__);
	crm_xml_add_int(notify, F_LRMD_RC, cmd->rc);
	crm_xml_add_int(notify, F_LRMD_EXEC_RC, cmd->exec_rc);
	crm_xml_add_int(notify, F_LRMD_OP_STATUS, cmd->lrmd_op_status);
	crm_xml_add_int(notify, F_LRMD_CALLID, cmd->call_id);
	crm_xml_add(notify, F_LRMD_OPERATION, LRMD_OP_RSC_EXEC);
	crm_xml_add(notify, F_LRMD_RSC_ID, cmd->rsc_id);
	crm_xml_add(notify, F_LRMD_RSC_ACTION, cmd->action);

	g_hash_table_foreach(client_list, send_client_notify, notify);

	free_xml(notify);
}

static void
send_generic_notify(int rc, xmlNode *request)
{
	int call_id = 0;
	xmlNode *notify = NULL;
	xmlNode *rsc_xml = get_xpath_object("//"F_LRMD_RSC, request, LOG_ERR);
	const char *rsc_id = crm_element_value(rsc_xml, F_LRMD_RSC_ID);
	const char *op = crm_element_value(request, F_LRMD_OPERATION);

	crm_element_value_int(request, F_LRMD_CALLID, &call_id);

	notify = create_xml_node(NULL, T_LRMD_NOTIFY);
	crm_xml_add(notify, F_LRMD_ORIGIN, __FUNCTION__);
	crm_xml_add_int(notify, F_LRMD_RC, rc);
	crm_xml_add_int(notify, F_LRMD_CALLID, call_id);
	crm_xml_add(notify, F_LRMD_OPERATION, op);
	crm_xml_add(notify, F_LRMD_RSC_ID, rsc_id);

	g_hash_table_foreach(client_list, send_client_notify, notify);

	free_xml(notify);
}

static void
cmd_finalize(lrmd_cmd_t *cmd, lrmd_rsc_t *rsc)
{
	crm_trace("Resource operation rsc:%s action:%s completed", cmd->rsc_id, cmd->action);
	send_cmd_complete_notify(cmd);

	if (rsc && (rsc->active == cmd)) {
		rsc->active = NULL;
		mainloop_set_trigger(rsc->work);
	}

	if (cmd->interval && (cmd->lrmd_op_status == PCMK_LRM_OP_CANCELLED)) {
		if (rsc) {
			rsc->recurring_ops = g_list_remove(rsc->recurring_ops, cmd);
		}
		free_lrmd_cmd(cmd);
	} else if (cmd->interval == 0) {
		free_lrmd_cmd(cmd);
	}
}

static void
action_complete(svc_action_t *action)
{
	lrmd_rsc_t *rsc;
	lrmd_cmd_t *cmd = action->cb_data;

	if (!cmd) {
		crm_err("LRMD action (%s) completed does not match any known operations.", action->id);
		return;
	}

	cmd->exec_rc = action->rc;
	cmd->lrmd_op_status = action->status;
	rsc = cmd->rsc_id ? g_hash_table_lookup(rsc_list, cmd->rsc_id) : NULL;

	cmd_finalize(cmd, rsc);
}

static int
lrmd_rsc_execute_stonith(lrmd_rsc_t *rsc, lrmd_cmd_t *cmd)
{
	int rc = 0;
	stonith_t *stonith_api = get_stonith_connection();

	if (rc) {
		cmd->exec_rc = rc;
		cmd->rc = lrmd_err_stonith_connection;
		cmd->lrmd_op_status = PCMK_LRM_OP_ERROR;
		cmd_finalize(cmd, rsc);
		return lrmd_err_stonith_connection;
	}

	if (safe_str_eq(cmd->action, "start")) {
		char *key = NULL;
		char *value = NULL;
		stonith_key_value_t *device_params = NULL;
		GHashTableIter iter;

		g_hash_table_iter_init(&iter, cmd->params);
		while (g_hash_table_iter_next(&iter, (gpointer *) & key, (gpointer *) & value)) {
			device_params = stonith_key_value_add(device_params, key, value);
		}

		rc = stonith_api->cmds->register_device(stonith_api,
				st_opt_sync_call,
				cmd->rsc_id,
				rsc->provider,
				rsc->type,
				device_params);

		stonith_key_value_freeall(device_params, 1, 1);
		if (rc == 0) {
			rc = stonith_api->cmds->call(stonith_api,
				st_opt_sync_call,
				cmd->rsc_id,
				"monitor",
				NULL,
				cmd->timeout);
		}
	} else if (safe_str_eq(cmd->action, "stop")) {
		rc = stonith_api->cmds->remove_device(stonith_api, st_opt_sync_call, cmd->rsc_id);
	} else if (safe_str_eq(cmd->action, "monitor")) {
		rc = stonith_api->cmds->call(stonith_api,
			st_opt_sync_call,
			cmd->rsc_id,
			cmd->action,
			NULL,
			cmd->timeout);
	}

	if (rc) {
		/* translate the errors we can */
		cmd->exec_rc = rc;
		switch (rc) {
		case st_err_not_supported:
			cmd->lrmd_op_status = PCMK_LRM_OP_NOTSUPPORTED;
			break;
		case st_err_timeout:
			cmd->lrmd_op_status = PCMK_LRM_OP_TIMEOUT;
			break;
		default:
			cmd->lrmd_op_status = PCMK_LRM_OP_ERROR;
		}
	} else {
		cmd->rc = cmd->exec_rc = lrmd_ok;
		cmd->lrmd_op_status = PCMK_LRM_OP_DONE;
	}

	if (cmd->interval > 0) {
		rsc->recurring_ops = g_list_append(rsc->recurring_ops, cmd);
		cmd->stonith_recurring_id = g_timeout_add(cmd->interval, stonith_recurring_op_helper, cmd);
	}
	cmd_finalize(cmd, rsc);

	return rc;
}

static int
lrmd_rsc_execute_service_lib(lrmd_rsc_t *rsc, lrmd_cmd_t *cmd)
{
	const char *action_name = cmd->action;
	svc_action_t *action = NULL;

	if (safe_str_eq(action_name, "monitor") &&
		(safe_str_eq(rsc->class, "lsb") ||
		 safe_str_eq(rsc->class, "service") ||
		 safe_str_eq(rsc->class, "systemd"))) {
		action_name = "status";
	}

	crm_trace("Creating action, resource:%s action:%s class:%s provider:%s agent:%s",
		rsc->rsc_id,
		cmd->action,
		rsc->class,
		rsc->provider,
		rsc->type);

	action = resources_action_create(rsc->rsc_id,
		rsc->class,
		rsc->provider,
		rsc->type,
		action_name,
		cmd->interval,
		cmd->timeout,
		cmd->params);

	cmd->params = NULL; /* We no longer own the params */
	if (!action) {
		crm_err("Failed to create action, action:%s on resource %s", cmd->action, rsc->rsc_id);
		cmd->rc = lrmd_err_exec_failed;
		goto exec_done;
	}

	action->cb_data = cmd;
	if (!services_action_async(action, action_complete)) {
		services_action_free(action);
		action = NULL;
		cmd->rc = lrmd_err_exec_failed;
		cmd->lrmd_op_status = PCMK_LRM_OP_ERROR;
		goto exec_done;
	}

	if (cmd->interval) {
		rsc->recurring_ops = g_list_append(rsc->recurring_ops, cmd);
	}

	/* The cmd will be finalized by the action_complete callback after
	 * the service library is done with it */
	rsc->active = cmd; /* only one op at a time for a rsc */
	cmd = NULL;

exec_done:
	if (cmd) {
		cmd_finalize(cmd, rsc);
	}
	return TRUE;
}

static gboolean
lrmd_rsc_execute(lrmd_rsc_t *rsc)
{
	lrmd_cmd_t *cmd = NULL;
	CRM_CHECK(rsc != NULL, return FALSE);

	if (rsc->active) {
		crm_trace("%s is still active", rsc->rsc_id);
		return TRUE;
	}

	if (rsc->pending_ops) {
		GList *first = rsc->pending_ops;
		cmd = first->data;
		if (cmd->delay_id) {
			crm_trace("Command %s %s was asked to run too early, waiting for start_delay timeout of %dms",
				cmd->rsc_id, cmd->action, cmd->start_delay);
			return TRUE;
		}
		rsc->pending_ops = g_list_remove_link(rsc->pending_ops, first);
		g_list_free_1(first);
	}

	if (!cmd) {
		crm_trace("Nothing further to do for %s", rsc->rsc_id);
		return TRUE;
	}

	if (safe_str_eq(rsc->class, "stonith")) {
		lrmd_rsc_execute_stonith(rsc, cmd);
	} else {
		lrmd_rsc_execute_service_lib(rsc, cmd);
	}

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
	GListPtr gIter = NULL;
	lrmd_rsc_t *rsc = data;
	int is_stonith = safe_str_eq(rsc->class, "stonith");

	for (gIter = rsc->pending_ops; gIter != NULL; gIter = gIter->next) {
		lrmd_cmd_t *cmd = gIter->data;
		cmd->rc = lrmd_ok;
		/* command was never executed */
		cmd->lrmd_op_status = PCMK_LRM_OP_CANCELLED;
		cmd_finalize(cmd, NULL);
	}
	/* frees list, but not list elements. */
	g_list_free(rsc->pending_ops);

	for (gIter = rsc->recurring_ops; gIter != NULL; gIter = gIter->next) {
		lrmd_cmd_t *cmd = gIter->data;
		if (is_stonith) {
			cmd->rc = lrmd_ok;
			cmd->lrmd_op_status = PCMK_LRM_OP_CANCELLED;
			cmd_finalize(cmd, NULL);
		} else {
			/* This command is already handed off to service library,
			 * let service library cancel it and tell us via the callback
			 * when it is cancelled. The rsc can be safely destroyed
			 * even if we are waiting for the cancel result */
			services_action_cancel(rsc->rsc_id, cmd->action, cmd->interval);
		}
	}
	/* frees list, but not list elements. */
	g_list_free(rsc->recurring_ops);

	crm_free(rsc->rsc_id);
	crm_free(rsc->class);
	crm_free(rsc->provider);
	crm_free(rsc->type);
	mainloop_destroy_trigger(rsc->work);

	crm_free(rsc);
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
	xmlNode *rsc_xml = get_xpath_object("//"F_LRMD_RSC, request, LOG_ERR);
	const char *rsc_id = crm_element_value(rsc_xml, F_LRMD_RSC_ID);

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

static int
process_lrmd_rsc_exec(lrmd_client_t *client, xmlNode *request)
{
	lrmd_rsc_t *rsc = NULL;
	lrmd_cmd_t *cmd = NULL;
	xmlNode *rsc_xml = get_xpath_object("//"F_LRMD_RSC, request, LOG_ERR);
	const char *rsc_id = crm_element_value(rsc_xml, F_LRMD_RSC_ID);

	if (!rsc_id) {
		return lrmd_err_missing;
	}
	if (!(rsc = g_hash_table_lookup(rsc_list, rsc_id))) {
		return lrmd_err_unknown_rsc;
	}

	cmd = create_lrmd_cmd(request);
	schedule_lrmd_cmd(rsc, cmd);

	return cmd->call_id;
}

static int
cancel_op(const char *rsc_id, const char *action, int interval)
{
	GListPtr gIter = NULL;
	lrmd_rsc_t *rsc = g_hash_table_lookup(rsc_list, rsc_id);

	/* How to cancel an action.
	 * 1. Check pending ops list, if it hasn't been handed off
	 *    to the service library or stonith recurring list remove
	 *    it there and that will stop it.
	 * 2. If it isn't in the pending ops list, then its either a
	 *    recurring op in the stonith recurring list, or the service
	 *    library's recurring list.  Stop it there
	 * 3. If not found in any lists, then this operation has either
	 *    been executed already and is not a recurring operation, or
	 *    never existed.
	 */
	if (!rsc) {
		return lrmd_err_unknown_rsc;
	}

	for (gIter = rsc->pending_ops; gIter != NULL; gIter = gIter->next) {
		lrmd_cmd_t *cmd = gIter->data;

		if (safe_str_eq(cmd->action, action) && cmd->interval == interval) {
			cmd->rc = lrmd_ok;
			cmd->lrmd_op_status = PCMK_LRM_OP_CANCELLED;
			cmd_finalize(cmd, rsc);
			return lrmd_ok;
		}
	}

	if (safe_str_eq(rsc->class, "stonith")) {
		/* The service library does not handle stonith operations.
		 * We have to handle recurring stonith opereations ourselves. */
		for (gIter = rsc->recurring_ops; gIter != NULL; gIter = gIter->next) {
			lrmd_cmd_t *cmd = gIter->data;

			if (safe_str_eq(cmd->action, action) && cmd->interval == interval) {
				cmd->rc = lrmd_ok;
				cmd->lrmd_op_status = PCMK_LRM_OP_CANCELLED;
				cmd_finalize(cmd, rsc);
				return lrmd_ok;
			}
		}
	} else if (services_action_cancel(rsc_id, action, interval) == TRUE) {
		/* The service library will tell the action_complete callback function
		 * this action was cancelled, which will destroy the cmd and remove
		 * it from the recurring_op list. Do not do that in this function
		 * if the service library says it cancelled it. */
		return lrmd_ok;
	}

	return lrmd_err_unknown_operation;
}

static int
process_lrmd_rsc_cancel(lrmd_client_t *client, xmlNode *request)
{
	xmlNode *rsc_xml = get_xpath_object("//"F_LRMD_RSC, request, LOG_ERR);
	const char *rsc_id = crm_element_value(rsc_xml, F_LRMD_RSC_ID);
	const char *action = crm_element_value(rsc_xml, F_LRMD_RSC_ACTION);
	int interval = 0;

	crm_element_value_int(rsc_xml, F_LRMD_RSC_INTERVAL, &interval);

	if (!rsc_id || !action) {
		return lrmd_err_missing;
	}

	return cancel_op(rsc_id, action, interval);
}

void
process_lrmd_message(lrmd_client_t *client, xmlNode *request)
{
	int rc = lrmd_ok;
	int call_options = 0;
	int call_id = 0;
	const char *op = crm_element_value(request, F_LRMD_OPERATION);
	int do_reply = 0;
	int do_notify = 0;
	int exit = 0;

	crm_element_value_int(request, F_LRMD_CALLOPTS, &call_options);
	crm_element_value_int(request, F_LRMD_CALLID, &call_id);

	if (crm_str_eq(op, CRM_OP_REGISTER, TRUE)) {
		rc = process_lrmd_signon(client, request);
	} else if (crm_str_eq(op, LRMD_OP_RSC_REG, TRUE)) {
		rc = process_lrmd_rsc_register(client, request);
		do_notify = 1;
		do_reply = 1;
	} else if (crm_str_eq(op, LRMD_OP_RSC_UNREG, TRUE)) {
		rc = process_lrmd_rsc_unregister(client, request);
		do_notify = 1;
		do_reply = 1;
	} else if (crm_str_eq(op, LRMD_OP_RSC_EXEC, TRUE)) {
		rc = process_lrmd_rsc_exec(client, request);
		do_reply = 1;
	} else if (crm_str_eq(op, LRMD_OP_RSC_CANCEL, TRUE)) {
		rc = process_lrmd_rsc_cancel(client, request);
		do_reply = 1;
	} else if (crm_str_eq(op, CRM_OP_QUIT, TRUE)) {
		do_reply = 1;
		exit = 1;
	} else {
		rc = lrmd_err_unknown_operation;
		do_reply = 1;
		crm_err("Unknown %s from %s", op, client->name);
		crm_log_xml_warn(request, "UnknownOp");
	}

	if (do_reply) {
		send_reply(client, rc, call_id);
	}

	if (do_notify) {
		send_generic_notify(rc, request);
	}

	if (exit) {
		lrmd_shutdown(0);
	}
}

