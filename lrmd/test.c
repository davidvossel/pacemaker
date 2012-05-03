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

#include <crm/lrmd.h>

GMainLoop *mainloop = NULL;
lrmd_t *lrmd_conn = NULL;

#define report_event(event)	\
	if (!event->rc) {	\
		crm_info("SUCCESS:"); \
	} else {	\
		crm_info("FAILURE:");	\
	}	\

static void
test_shutdown(int nsig)
{
	lrmd_api_delete(lrmd_conn);
}

static void
unregister_res(lrmd_event_data_t *event, void *userdata)
{
	report_event(event);
}

static void
register_res(lrmd_event_data_t *event, void *userdata)
{
	report_event(event);
	lrmd_conn->cmds->set_callback(lrmd_conn, NULL, unregister_res);
	lrmd_conn->cmds->unregister_rsc(lrmd_conn, "test_rsc", 0);
}

static gboolean
start_test(gpointer user_data)
{
	int rc;

	lrmd_conn = lrmd_api_new();
	rc = lrmd_conn->cmds->connect(lrmd_conn, "lrmd_ctest", NULL);

	if (!rc) {
		crm_info("lrmd client connection established");
	} else {
		crm_info("lrmd client connection failed");
	}

	lrmd_conn->cmds->set_callback(lrmd_conn, NULL, register_res);
	lrmd_conn->cmds->register_rsc(lrmd_conn, "test_rsc", "ocf", "pacemaker", "Dummy", 0);
	return 0;
}

int main(int argc, char ** argv)
{
	crm_trigger_t *trig;

	crm_log_init("lrmd_ctest", LOG_INFO, TRUE, FALSE, argc, argv);

	trig = mainloop_add_trigger(G_PRIORITY_HIGH, start_test, NULL);
	mainloop_set_trigger(trig);
	mainloop_track_children(G_PRIORITY_HIGH);
	mainloop_add_signal(SIGTERM, test_shutdown);

	crm_info("Starting");
	mainloop = g_main_new(FALSE);
	g_main_run(mainloop);

}
