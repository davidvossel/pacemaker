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

#ifndef LRMD__H
#define LRMD__H

#include <crm/services.h>

typedef struct lrmd_s lrmd_t;

#define F_LRMD_OPERATION		"lrmd_op"
#define F_LRMD_CLIENTNAME		"lrmd_clientname"
#define F_LRMD_CLIENTID		"lrmd_clientid"
#define F_LRMD_CALLBACK_TOKEN	"lrmd_async_id"
#define F_LRMD_CALLID		"lrmd_callid"
#define F_LRMD_CALLOPTS		"lrmd_callopt"
#define F_LRMD_CALLDATA		"lrmd_calldata"
#define F_LRMD_RC			"lrmd_rc"
#define F_LRMD_TIMEOUT		"lrmd_timeout"
#define F_LRMD_CLASS		"lrmd_class"
#define F_LRMD_PROVIDER		"lrmd_provider"
#define F_LRMD_TYPE		"lrmd_type"
#define F_LRMD_ORIGIN		"lrmd_origin"
#define F_LRMD_RSC_ID	"lrmd_rsc_id"
#define F_LRMD_RSC_METADATA	"lrmd_rsc_metadata_res"

#define LRMD_OP_RSC_REG	"lrmd_rsc_register"
#define LRMD_OP_RSC_UNREG	"lrmd_rsc_unregister"
#define LRMD_OP_RSC_CALL	"lrmd_rsc_call"
#define LRMD_OP_RSC_GET_METADATA	"lrmd_rsc_metadata"

#define F_LRMD_RSC	"lrmd_rsc"

#define T_LRMD		"lrmd"
#define T_LRMD_REPLY	"lrmd_reply"
#define T_LRMD_NOTIFY	"lrmd_notify"

extern lrmd_t *lrmd_api_new(void);
extern void lrmd_api_delete(lrmd_t * lrmd);

enum lrmd_call_options {
	lrmd_opt_none		= 0x00000000,
	// lrmd_opt_sync_call	= 0x00000001, 
};

enum lrmd_errors {
	lrmd_ok						=  0,
	lrmd_pending				= -1,
	lrmd_err_generic			= -2,
	lrmd_err_internal			= -3,
	lrmd_err_not_supported		= -4,
	lrmd_err_connection			= -5,
	lrmd_err_missing			= -6,
	lrmd_err_exists				= -7,
	lrmd_err_timeout			= -8,
	lrmd_err_ipc				= -9,
	lrmd_err_peer				= -10,
	lrmd_err_unknown_operation	= -11,
	lrmd_err_unknown_rsc		= -12,
	lrmd_err_none_available		= -13,
	lrmd_err_authentication		= -14,
	lrmd_err_signal				= -15,
};

enum lrmd_callback_event {
	lrmd_event_register,
	lrmd_event_unregister,
	lrmd_event_call_complete,
};

typedef struct lrmd_event_data_s {
	enum lrmd_callback_event type;
	const char *rsc_id;
	int rc;
	int call_id;
} lrmd_event_data_t;

typedef void (*lrmd_event_callback)(lrmd_event_data_t *event, void *userdata);

typedef struct lrmd_api_operations_s
{
	int (*connect) (lrmd_t *lrmd, const char *client_name, int *lrmd_fd);

	int (*disconnect)(lrmd_t *lrmd);

	int (*register_rsc) (lrmd_t *lrmd,
		const char *rsc_id,
		const char *class,
		const char *provider,
		const char *type,
		enum lrmd_call_options options);

	int (*unregister_rsc) (lrmd_t *lrmd,
		const char *rsc_id,
		enum lrmd_call_options options);

	int (*set_callback) (lrmd_t *lrmd,
		void *userdata,
		lrmd_event_callback callback);

	/* TODO IMPLEMENT */
	int (*get_metadata) (lrmd_t *lrmd,
		const char *class,
		const char *provider,
		const char *type,
		char **output,
		enum lrmd_call_options options);

	/*!
	 * \brief Issue a command on a resource
	 * \retval call id of the command, -1 on failure
	 */
	/* TODO IMPLEMENT */
	int (*call)(lrmd_t *lrmd,
		const char *rsc_id,
		const char *action,
		int interval, /* ms */
		int timeout, /* ms */
		int start_delay, /* ms */
		enum lrmd_call_options,
		GHashTable *params); /* TODO make this not glib */

	/*!
	 * \brief Cancel a recurring command.
	 *
	 * \retval 0, cancel command sent.
	 * \retval -1, cancel command failed.
	 */
	/* TODO IMPLEMENT add callback for cancel. */
	int (*cancel)(lrmd_t *lrmd, const char *call_id);

} lrmd_api_operations_t;

struct lrmd_s {
	lrmd_api_operations_t *cmds;
	void *private;
};

#endif
