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
#include <crm/common/mainloop.h>

CRM_TRACE_INIT_DATA(lrmd);

typedef struct lrmd_private_s {
	char *token;
	crm_ipc_t *ipc;
	mainloop_ipc_t *source;
} lrmd_private_t;

static int
lrmd_dispatch_internal(const char *buffer, ssize_t length, gpointer userdata)
{
	crm_info("Got something from lrmd!");
	return 1;
}

static void
lrmd_connection_destroy(gpointer user_data)
{
	crm_info("connection destroyed");
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

	return 0;
}

#ifdef _TEST // TODO all the code in the _TEST defines will go away.
/*
static void dump_xml(const char *description, xmlNode *msg)
{
	char *dump;

	dump = msg ? dump_xml_formatted(msg) : NULL;
	crm_info("%s = %s", description, dump);
	crm_free(dump);
}
*/
#endif

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

	return new_lrmd;
}

void lrmd_api_delete(lrmd_t * lrmd)
{
	lrmd_private_t *private = lrmd->private;

	lrmd->cmds->disconnect(lrmd); /* no-op if already disconnected */
	crm_free(private->token);
	crm_free(lrmd->private);
	crm_free(lrmd);
}
