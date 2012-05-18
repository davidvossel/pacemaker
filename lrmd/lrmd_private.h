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

#ifndef LRMD_PVT__H
#define LRMD_PVT__H

#include <glib.h>
#include <crm/common/ipc.h>
#include <crm/lrmd.h>

extern GHashTable *rsc_list;
extern GHashTable *client_list;

typedef struct lrmd_rsc_s {
	char *rsc_id;
	char *class;
	char *provider;
	char *type;

	/* NEVER dereference this pointer,
	 * It simply exists as a switch to let us know
	 * when the currently active operation has completed */
	void *active;

	/* Operations in this list
	 * have not been executed yet. */
	GList *pending_ops;
	/* Operations in this list are recurring operations
	 * that have been handed off from the pending ops list. */
	GList *recurring_ops;

	crm_trigger_t *work;
} lrmd_rsc_t;

typedef struct lrmd_client_s {
	char *id;
	char *name;

	qb_ipcs_connection_t *channel;

	long long flags;

} lrmd_client_t;

void process_lrmd_message(lrmd_client_t *client, xmlNode *request);

void free_rsc(gpointer data);

void lrmd_shutdown(int nsig);

#endif
