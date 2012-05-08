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

/* *INDENT-OFF* */
static struct crm_option long_options[] = {
	{"help",             0, 0, '?'},
	{"listen",           0, 0, 'l'},
	{"api-call",         1, 0, 'c'},
	{"action",           1, 0, 'a'},
	{"rsc-id",           1, 0, 'r'},
	{"cancel-call-id",   1, 0, 'x'},
	{"provider",         1, 0, 'P'},
	{"class",            1, 0, 'C'},
	{"type",             1, 0, 'T'},
	{"interval",         1, 0, 'i'},
	{"timeout",          1, 0, 't'},
	{"start-delay",      1, 0, 's'},
	{"param-key",        1, 0, 'k'},
	{"param-val",        1, 0, 'v'},
	{0, 0, 0, 0}
};
/* *INDENT-ON* */

static int exec_call_id = 0;

static struct {
	int print;
	int listen;
	int interval;
	int timeout;
	int start_delay;
	int cancel_call_id;
	const char *api_call;
	const char *rsc_id;
	const char *provider;
	const char *class;
	const char *type;
	const char *action;
	lrmd_key_value_t *params;

} options;

#define report_event(event)	\
	crm_info("NEW_EVENT"); \
	crm_info("event_type: %d", event->type); \
	crm_info("rsc_id: %s", event->rsc_id); \
	crm_info("exec_id: %s", event->exec_id); \
	crm_info("rc: %d", event->rc); \
	crm_info("exec_rc: %d", event->exec_rc); \
	crm_info("call_id: %d", event->call_id); \
	crm_info("op_status: %d", event->lrmd_op_status); \
	crm_info("END_EVENT");	\

static void
test_shutdown(int nsig)
{
	lrmd_api_delete(lrmd_conn);
}

static void
read_events(lrmd_event_data_t *event, void *userdata)
{
	if (options.listen) {
		report_event(event);
	}

	if (exec_call_id && (event->call_id == exec_call_id)) {
		if ((event->rc == lrmd_ok) && (event->lrmd_op_status == 0)) {
			crm_info("ACTION SUCCESSFUL");
		} else {
			crm_info("ACTION FAILURE rc:%d lrmd_op_status:%d",
				event->rc,
				event->lrmd_op_status);
			exit(-1);
		}

		if (!options.listen) {
			exit(1);
		}
	}
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
	lrmd_conn->cmds->set_callback(lrmd_conn, NULL, read_events);

	if (!options.api_call) {
		return 0;
	}

	if (safe_str_eq(options.api_call, "exec")) {
		char buf[512];
		snprintf(buf, 512, "%s_%s_%d",
			options.rsc_id, options.api_call, options.interval);
		rc = lrmd_conn->cmds->exec(lrmd_conn,
			buf,
			options.rsc_id,
			options.action,
			options.interval,
			options.timeout,
			options.start_delay,
			0,
			options.params);

		if (rc > 0) {
			exec_call_id = rc;
			crm_info("ACTION PENDING call_id:%d", rc);
		}

	} else if (safe_str_eq(options.api_call, "register_rsc")) {
		rc = lrmd_conn->cmds->register_rsc(lrmd_conn,
			options.rsc_id,
			options.class,
			options.provider,
			options.type,
			0);
	} else if (safe_str_eq(options.api_call, "unregister_rsc")) {
		rc = lrmd_conn->cmds->unregister_rsc(lrmd_conn,
			options.rsc_id,
			0);
	} else if (safe_str_eq(options.api_call, "cancel")) {
		rc = lrmd_conn->cmds->cancel(lrmd_conn,
			options.rsc_id,
			options.cancel_call_id);
	} else if (options.action) {
		crm_err("API FAILURE unknown action '%s'", options.action);
		exit(-1);
	}

	if (rc < 0) {
		crm_err("API FAILURE rc:%d", rc);
		exit(-1);
	}

	if (options.action && rc == lrmd_ok) {
		crm_info("ACTION SUCCESSFUL");
		if (!options.listen) {
			exit(1);
		}
	}

	return 0;
}

int main(int argc, char ** argv)
{
	int option_index = 0;
	int argerr = 0;
	int flag;
	char *key = NULL;
	char *val = NULL;
	crm_trigger_t *trig;

	crm_log_init("lrmd_ctest", LOG_INFO, TRUE, TRUE, argc, argv);
	crm_set_options(NULL, "mode [options]", long_options,
		"Inject commands into the lrmd and watch for events\n");

	while (1) {
		flag = crm_get_option(argc, argv, &option_index);
		if (flag == -1)
			break;

		switch(flag) {
		case '?':
			crm_help(flag, LSB_EXIT_OK);
			break;
		case 'l':
			options.listen = 1;
			break;
		case 'c':
			options.api_call = optarg;
			break;
		case 'a':
			options.action = optarg;
			break;
		case 'r':
			options.rsc_id = optarg;
			break;
		case 'x':
			options.cancel_call_id = atoi(optarg);
			break;
		case 'P':
			options.provider = optarg;
			break;
		case 'C':
			options.class = optarg;
			break;
		case 'T':
			options.type = optarg;
			break;
		case 'i':
			options.interval = atoi(optarg);
			break;
		case 't':
			options.timeout = atoi(optarg);
			break;
		case 's':
			options.start_delay = atoi(optarg);
			break;
		case 'k':
			key = optarg;
			if (key && val) {
				options.params = lrmd_key_value_add(options.params, key, val);
				key = val = NULL;
			}
			break;
		case 'v':
			val = optarg;
			if (key && val) {
				options.params = lrmd_key_value_add(options.params, key, val);
				key = val = NULL;
			}
			break;
		default:
			++argerr;
		break;
		}
	}

	if (argerr) {
		crm_help('?', LSB_EXIT_GENERIC);
	}
	if (optind > argc) {
		++argerr;
	}

	/* if we can't perform an api_call or listen for events, 
	 * there is nothing to do */
	if (!options.api_call && !options.listen) {
		crm_err("Nothing to be done.  Please specify 'api-call' and/or 'listen'");
		return 0;
	}

	trig = mainloop_add_trigger(G_PRIORITY_HIGH, start_test, NULL);
	mainloop_set_trigger(trig);
	mainloop_track_children(G_PRIORITY_HIGH);
	mainloop_add_signal(SIGTERM, test_shutdown);

	crm_info("Starting");
	mainloop = g_main_new(FALSE);
	g_main_run(mainloop);

	return 0;
}
