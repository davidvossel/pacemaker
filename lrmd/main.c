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
qb_ipcs_service_t *ipcs = NULL;

static int32_t
lrmd_ipc_accept(qb_ipcs_connection_t *c, uid_t uid, gid_t gid)
{
	crm_trace("Connecting %p for uid=%d gid=%d", c, uid, gid);
	return 0;
}

static void
lrmd_ipc_created(qb_ipcs_connection_t *c)
{
	crm_info("client connection established");
}

static int32_t
lrmd_ipc_dispatch(qb_ipcs_connection_t *c, void *data, size_t size)
{
	xmlNode *msg = crm_ipcs_recv(c, data, size);
	xmlNode *ack = create_xml_node(NULL, "ack");

	crm_ipcs_send(c, ack, FALSE);
	free_xml(ack);

	if (msg != NULL) {
		xmlNode *data = get_message_xml(msg, F_CRM_DATA);

		process_lrmd_message(msg, data, c);
		free_xml(msg);
	}
	return 0;
}

static int32_t
lrmd_ipc_closed(qb_ipcs_connection_t *c)
{
	return 0;
}

static void
lrmd_ipc_destroy(qb_ipcs_connection_t *c)
{
	crm_trace("Disconnecting %p", c);
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
	crm_info("Terminating");
    mainloop_del_ipc_server(ipcs);
	exit(0);
}

/* TODO remove... test code. */
static void
start_cb(svc_action_t *action)
{
	printf("oh, it started\n");
}

static gboolean
lrmd_test(gpointer user_data)
{
	svc_action_t *action = resources_action_create("1234", "ocf", "pacemaker", "Dummy", "Start", 0, 10000, NULL);
	gboolean res = services_action_async(action, start_cb);

	printf("res is %s\n", res ? "PASS" : "FAIL");
	return true;
}
/* END OF TEST CODE */

int
main(int argc, char ** argv)
{
	int rc = 0;

	crm_log_init("lrmd", LOG_INFO, TRUE, FALSE, argc, argv);

	/* TODO remove... test code */
	{
	GList *list = resources_list_providers("ocf");
	GList *gIter = list;
	crm_trigger_t *trig;

	crm_info("LRMD it works");
	for (;gIter != NULL; gIter = gIter->next) {
		printf("Service api works... %s \n", (char *) gIter->data);
	}
	trig = mainloop_add_trigger(G_PRIORITY_HIGH, lrmd_test, NULL);
	mainloop_set_trigger(trig);
	}
	/* end test code */


	ipcs = mainloop_add_ipc_server(CRM_SYSTEM_LRMD, QB_IPC_SHM, &lrmd_ipc_callbacks);

	if (ipcs == NULL) {
		crm_err("Failed to start IPC server");
		return 1;
	}

	mainloop_track_children(G_PRIORITY_HIGH);
	mainloop_add_signal(SIGTERM, lrmd_shutdown);
	mainloop = g_main_new(FALSE);
	printf("starting\n");
	g_main_run(mainloop);

	return rc;
}
