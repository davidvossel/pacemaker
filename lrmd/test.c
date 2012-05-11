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

/* *INDENT-OFF* */
static struct crm_option long_options[] = {
	{"help",             0, 0, '?'},
	{"verbose",          0, 0, 'V', "\t\tPrint out logs and events to screen"},
	{"quiet",            0, 0, 'Q', "\t\tSuppress all output to screen"},
	/* just incase we have to add data to events,
	 * we don't want break a billion regression tests. Instead
	 * we'll create different versions */
	{"listen",           1, 0, 'l', "\tListen for a specific event string"},
	{"event-ver",        1, 0, 'e', "\tVersion of event to listen to"},
	{"api-call",         1, 0, 'c', "\tDirectly relates to lrmd api functions"},
	{"no-wait",          0, 0, 'w', "\tMake api call and do not wait for result."},
	{"-spacer-",         1, 0, '-', "\nParameters for api-call option"},
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

	{"-spacer-",         1, 0, '-'},
	{0, 0, 0, 0}
};
/* *INDENT-ON* */

static int exec_call_id = 0;

static struct {
	int verbose;
	int quiet;
	int print;
	int interval;
	int timeout;
	int start_delay;
	int cancel_call_id;
	int event_version;
	int no_wait;
	const char *api_call;
	const char *rsc_id;
	const char *provider;
	const char *class;
	const char *type;
	const char *action;
	const char *listen;
	lrmd_key_value_t *params;
} options;

GMainLoop *mainloop = NULL;
lrmd_t *lrmd_conn = NULL;

static char event_buf_v0[1024];

#define print_result(result) \
	if (!options.quiet) { \
		result; \
	} \

#define report_event(event)	\
	snprintf(event_buf_v0, sizeof(event_buf_v0), "NEW_EVENT event_type:%d rsc_id:%s action:%s rc:%d exec_rc:%s op_status:%s", \
		event->type,	\
		event->rsc_id,	\
		event->action ? event->action : "none",	\
		event->rc,	\
		services_ocf_exitcode_str(event->exec_rc),	\
		services_lrm_status_str(event->lrmd_op_status));	\
	crm_info("%s", event_buf_v0);;

static void
test_shutdown(int nsig)
{
	lrmd_api_delete(lrmd_conn);
}

static void
read_events(lrmd_event_data_t *event, void *userdata)
{
	report_event(event);
	if (options.listen) {
		if (safe_str_eq(options.listen, event_buf_v0)) {
			print_result(printf("LISTEN EVENT SUCCESSFUL\n"));
			exit(0);
		}
	}

	if (exec_call_id && (event->call_id == exec_call_id)) {
		if ((event->rc == lrmd_ok) && (event->lrmd_op_status == 0)) {
			print_result(printf("API-CALL SUCCESSFUL for 'exec'\n"));
		} else {
			print_result(printf("API-CALL FAILURE for 'exec', rc:%d lrmd_op_status:%s\n",
				event->rc,
				services_lrm_status_str(event->lrmd_op_status)));
			exit(-1);
		}

		if (!options.listen) {
			exit(0);
		}
	}
}

static gboolean
timeout_err(gpointer data)
{
	print_result(printf("LISTEN EVENT FAILURE - timeout occurred, never found.\n"));
	exit(-1);

	return FALSE;
}

static void
try_connect(void)
{
	int tries = 10;
	int i = 0;
	int rc = 0;

	lrmd_conn = lrmd_api_new();

	for (i = 0; i < tries; i++) {
		rc = lrmd_conn->cmds->connect(lrmd_conn, "lrmd_ctest", NULL);

		if (!rc) {
			crm_info("lrmd client connection established");
			return;
		} else {
			crm_info("lrmd client connection failed");
		}
		sleep(1);
	}

	print_result(printf("API CONNECTION FAILURE\n"));
	exit(-1);
}

static gboolean
start_test(gpointer user_data)
{
	int rc = 0;

	try_connect();

	lrmd_conn->cmds->set_callback(lrmd_conn, NULL, read_events);

	if (options.timeout) {
		g_timeout_add(options.timeout, timeout_err, NULL);
	}

	if (!options.api_call) {
		return 0;
	}

	if (safe_str_eq(options.api_call, "exec")) {
		rc = lrmd_conn->cmds->exec(lrmd_conn,
			options.rsc_id,
			options.action,
			options.interval,
			options.timeout,
			options.start_delay,
			0,
			options.params);

		if (rc > 0) {
			exec_call_id = rc;
			print_result(printf("API-CALL 'exec' action pending, waiting on response\n"));
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
			options.action,
			options.interval);
	} else if (safe_str_eq(options.api_call, "metadata")) {
		char *output = NULL;
		rc = lrmd_conn->cmds->get_metadata(lrmd_conn,
			options.class,
			options.provider,
			options.type, &output, 0);
		if (rc == lrmd_ok) {
			print_result(printf("%s", output));
			crm_free(output);
		}
	} else if (safe_str_eq(options.api_call, "list_agents")) {
		lrmd_list_t *list = NULL;
		lrmd_list_t *iter = NULL;
		rc = lrmd_conn->cmds->list_agents(lrmd_conn, &list);

		if (rc > 0) {
			print_result(printf("%d agents found\n", rc));
			for (iter = list; iter != NULL; iter = iter->next) {
				print_result(printf("%s\n", iter->val));
			}
			lrmd_list_freeall(list);
			rc = 0;
		} else {
			print_result(printf("API_CALL FAILURE - no agents found\n"));
			rc = -1;
		}
	} else if (safe_str_eq(options.api_call, "list_providers")) {
		lrmd_list_t *list = NULL;
		lrmd_list_t *iter = NULL;
		rc = lrmd_conn->cmds->list_providers(lrmd_conn, options.type, &list);

		if (rc > 0) {
			print_result(printf("%d providers found\n", rc));
			for (iter = list; iter != NULL; iter = iter->next) {
				print_result(printf("%s\n", iter->val));
			}
			lrmd_list_freeall(list);
			rc = 0;
		} else {
			print_result(printf("API_CALL FAILURE - no providers found\n"));
			rc = -1;
		}
	} else if (options.action) {
		print_result(printf("API-CALL FAILURE unknown action '%s'\n", options.action));
		exit(-1);
	}

	if (rc < 0) {
		print_result(printf("API-CALL FAILURE for '%s' rc:%d\n", options.api_call, rc));
		exit(-1);
	}

	if (options.api_call && rc == lrmd_ok) {
		print_result(printf("API-CALL SUCCESSFUL for '%s'\n", options.api_call));
		if (!options.listen) {
			exit(0);
		}
	}

	if (options.no_wait) {
		/* just make the call and exit regardless of anything else. */
		exit(0);
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
		case 'V':
			options.verbose = 1;
			break;
		case 'Q':
			options.quiet = 1;
			options.verbose = 0;
			break;
		case 'e':
			options.event_version = atoi(optarg);
			break;
		case 'l':
			options.listen = optarg;
			break;
		case 'w':
			options.no_wait = 1;
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


	crm_log_init("lrmd_ctest", LOG_INFO, TRUE, options.verbose ? TRUE : FALSE, argc, argv);

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
