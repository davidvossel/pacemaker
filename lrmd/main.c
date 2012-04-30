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

#include <glib.h>
#include <unistd.h>

#include <crm/crm.h>
#include <crm/services.h>


GMainLoop *mainloop = NULL;

static void
lrmd_shutdown(int nsig)
{
	crm_info("Terminating");
	exit(0);
}

/* TODO remove... test code. */
static void start_cb(svc_action_t *action)
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

int main(int argc, char ** argv)
{
	int rc = 0;

	crm_log_init("lrmd-ng", LOG_INFO, TRUE, FALSE, argc, argv);

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

	mainloop_track_children(G_PRIORITY_HIGH);
	mainloop_add_signal(SIGTERM, lrmd_shutdown);
	mainloop = g_main_new(FALSE);
	printf("starting\n");
	g_main_run(mainloop);

	return rc;
}
