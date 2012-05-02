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

static gboolean
process_lrmd_register(lrmd_client_t *client, xmlNode *request)
{
	xmlNode *reply = create_xml_node(NULL, "reply");
	crm_xml_add(reply, F_LRMD_OPERATION, CRM_OP_REGISTER);
	crm_xml_add(reply, F_LRMD_CLIENTID,  client->id);
	crm_ipcs_send(client->channel, reply, FALSE);

	free_xml(reply);
	return TRUE;
}

gboolean process_lrmd_message(lrmd_client_t *client, xmlNode *request)
{
	const char *op = crm_element_value(request, F_LRMD_OPERATION);

#ifdef _TEST
	crm_info("processing client request");
	dump_xml("request", request);
#endif
	if (crm_str_eq(op, CRM_OP_REGISTER, TRUE)) {
		return process_lrmd_register(client, request);
	} else {
		crm_err("Unknown %s from %s", op, client->name);
		crm_log_xml_warn(request, "UnknownOp");
	}

	return TRUE;
}

