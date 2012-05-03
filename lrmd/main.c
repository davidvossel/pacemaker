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


GMainLoop *mainloop = NULL;
GHashTable *client_list = NULL;
qb_ipcs_service_t *ipcs = NULL;

static int32_t
lrmd_ipc_accept(qb_ipcs_connection_t *c, uid_t uid, gid_t gid)
{
	struct qb_ipcs_connection_stats stats = { 0, };

	qb_ipcs_connection_stats_get(c, &stats, 1);
	crm_info("Accepting client connection: %p pid=%d for uid=%d gid=%d",
		c, stats.client_pid, uid, gid);
	return 0;
}

static void
lrmd_ipc_created(qb_ipcs_connection_t *c)
{
	cl_uuid_t client_id;
	lrmd_client_t *new_client = NULL;
	char uuid_str[UU_UNPARSE_SIZEOF];

	crm_malloc0(new_client, sizeof(lrmd_client_t));
	new_client->channel = c;

	cl_uuid_generate(&client_id);
	cl_uuid_unparse(&client_id, uuid_str);

	new_client->id = crm_strdup(uuid_str);
	crm_trace("LRMD client connection established. %p id: %s", c, new_client->id);

	g_hash_table_insert(client_list, new_client->id, new_client);
	qb_ipcs_context_set(c, new_client);
}

static int32_t
lrmd_ipc_dispatch(qb_ipcs_connection_t *c, void *data, size_t size)
{
	xmlNode *request = crm_ipcs_recv(c, data, size);
	lrmd_client_t *client = (lrmd_client_t *) qb_ipcs_context_get(c);

	CRM_CHECK(client != NULL, crm_err("Invalid client"); return FALSE);
	CRM_CHECK(client->id != NULL, crm_err("Invalid client: %p", client); return FALSE);

	if (!request) {
		return 0;
	}

	if (!client->name) {
		const char *value = crm_element_value(request, F_LRMD_CLIENTNAME);

		if (value == NULL) {
			client->name = crm_itoa(crm_ipcs_client_pid(c));
		} else {
			client->name = crm_strdup(value);
		}
	}

	crm_xml_add(request, F_LRMD_CLIENTID, client->id);
	crm_xml_add(request, F_LRMD_CLIENTNAME, client->name);

	process_lrmd_message(client, request);

	return 0;
}

static int32_t
lrmd_ipc_closed(qb_ipcs_connection_t *c)
{
	lrmd_client_t *client = (lrmd_client_t *) qb_ipcs_context_get(c);

	if (!client) {
		crm_err("No client for ipc");
		return 0;
	}

	if (client->id) {
		g_hash_table_remove(client_list, client->id);
	}

	return 0;
}

static void
lrmd_ipc_destroy(qb_ipcs_connection_t *c)
{
	lrmd_client_t *client = (lrmd_client_t *) qb_ipcs_context_get(c);

	if (!client) {
		crm_err("No client for ipc");
		return;
	}

	crm_info("LRMD client disconnecting %p - name: %s id: %s", c, client->name, client->id);
	crm_free(client->name);
	crm_free(client->id);
	crm_free(client);
}

static struct qb_ipcs_service_handlers lrmd_ipc_callbacks =
{
	.connection_accept = lrmd_ipc_accept,
	.connection_created = lrmd_ipc_created,
	.msg_process = lrmd_ipc_dispatch,
	.connection_closed = lrmd_ipc_closed,
	.connection_destroyed = lrmd_ipc_destroy
};

static void
lrmd_shutdown(int nsig)
{
	crm_info("Terminating with  %d clients", g_hash_table_size(client_list));
	mainloop_del_ipc_server(ipcs);
	exit(0);
}

#ifdef  _TEST
/* TODO remove... test code. */
static void
start_cb(svc_action_t *action)
{
	printf("oh, it started\n");
}

static gboolean
lrmd_test(gpointer user_data)
{
	svc_action_t *action = resources_action_create("1234", "ocf", "pacemaker", "Dummy", "start", 0, 10000, NULL);
	gboolean res;

	printf("action: id: %s rsc: %s action: %s \n", action->id, action->rsc, action->action);
	res = services_action_async(action, start_cb);
	printf("res is %s\n", res ? "PASS" : "FAIL");
	return true;
}
/* END OF TEST CODE */
#endif

int
main(int argc, char ** argv)
{
	int rc = 0;

	crm_log_init("lrmd", LOG_INFO, TRUE, FALSE, argc, argv);

	/* TODO remove... test code */
#ifdef _TEST
	{
	GList *list = resources_list_providers("ocf");
	GList *gIter = list;
	crm_trigger_t *trig;

	for (;gIter != NULL; gIter = gIter->next) {
		printf("Service api works... %s \n", (char *) gIter->data);
	}
	trig = mainloop_add_trigger(G_PRIORITY_HIGH, lrmd_test, NULL);
	mainloop_set_trigger(trig);
	}
	/* end test code */
#endif


    rsc_list = g_hash_table_new_full(crm_str_hash, g_str_equal, NULL, free_rsc);
	client_list = g_hash_table_new(crm_str_hash, g_str_equal);
	ipcs = mainloop_add_ipc_server(CRM_SYSTEM_LRMD, QB_IPC_SHM, &lrmd_ipc_callbacks);

	if (ipcs == NULL) {
		crm_err("Failed to start IPC server");
		return 1;
	}

	mainloop_track_children(G_PRIORITY_HIGH);
	mainloop_add_signal(SIGTERM, lrmd_shutdown);
	mainloop = g_main_new(FALSE);
	crm_info("Starting");
	g_main_run(mainloop);

	mainloop_del_ipc_server(ipcs);
	g_hash_table_destroy(client_list);
	g_hash_table_destroy(rsc_list);

	return rc;
}
