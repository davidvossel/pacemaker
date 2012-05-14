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

typedef struct lrmd_s lrmd_t;
typedef struct lrmd_key_value_s lrmd_key_value_t;
struct lrmd_key_value_t;

#define F_LRMD_OPERATION        "lrmd_op"
#define F_LRMD_CLIENTNAME       "lrmd_clientname"
#define F_LRMD_CLIENTID         "lrmd_clientid"
#define F_LRMD_CALLBACK_TOKEN	"lrmd_async_id"
#define F_LRMD_CALLID           "lrmd_callid"
#define F_LRMD_CANCEL_CALLID    "lrmd_cancel_callid"
#define F_LRMD_CALLOPTS         "lrmd_callopt"
#define F_LRMD_CALLDATA         "lrmd_calldata"
#define F_LRMD_RC               "lrmd_rc"
#define F_LRMD_EXEC_RC          "lrmd_exec_rc"
#define F_LRMD_OP_STATUS        "lrmd_exec_op_status"
#define F_LRMD_TIMEOUT          "lrmd_timeout"
#define F_LRMD_CLASS            "lrmd_class"
#define F_LRMD_PROVIDER         "lrmd_provider"
#define F_LRMD_TYPE             "lrmd_type"
#define F_LRMD_ORIGIN           "lrmd_origin"
#define F_LRMD_RSC_ID           "lrmd_rsc_id"
#define F_LRMD_RSC_ACTION       "lrmd_rsc_action"
#define F_LRMD_RSC_START_DELAY  "lrmd_rsc_start_delay"
#define F_LRMD_RSC_INTERVAL     "lrmd_rsc_interval"
#define F_LRMD_RSC_TIMEOUT      "lrmd_rsc_timeout"
#define F_LRMD_RSC_METADATA     "lrmd_rsc_metadata_res"
#define F_LRMD_RSC              "lrmd_rsc"

#define LRMD_OP_RSC_REG           "lrmd_rsc_register"
#define LRMD_OP_RSC_EXEC          "lrmd_rsc_exec"
#define LRMD_OP_RSC_CANCEL        "lrmd_rsc_cancel"
#define LRMD_OP_RSC_UNREG         "lrmd_rsc_unregister"
#define LRMD_OP_RSC_METADATA  "lrmd_rsc_metadata"

#define T_LRMD           "lrmd"
#define T_LRMD_REPLY     "lrmd_reply"
#define T_LRMD_NOTIFY    "lrmd_notify"

extern lrmd_t *lrmd_api_new(void);
extern void lrmd_api_delete(lrmd_t * lrmd);
extern lrmd_key_value_t *lrmd_key_value_add(lrmd_key_value_t *kvp,
	const char *key,
	const char *value);

/* Reserved for future use */
enum lrmd_call_options {
	lrmd_opt_none		= 0x00000000,
};

enum lrmd_errors {
	lrmd_ok                      =  0,
	lrmd_pending                 = -1,
	lrmd_err_generic             = -2,
	lrmd_err_internal            = -3,
	lrmd_err_not_supported       = -4,
	lrmd_err_connection          = -5,
	lrmd_err_missing             = -6,
	lrmd_err_exists              = -7,
	lrmd_err_timeout             = -8,
	lrmd_err_ipc                 = -9,
	lrmd_err_peer                = -10,
	lrmd_err_unknown_operation   = -11,
	lrmd_err_unknown_rsc         = -12,
	lrmd_err_none_available      = -13,
	lrmd_err_authentication      = -14,
	lrmd_err_signal              = -15,
	lrmd_err_exec_failed         = -16,
	lrmd_err_no_metadata         = -17,
	lrmd_err_stonith_connection  = -18,
};

enum lrmd_callback_event {
	lrmd_event_register,
	lrmd_event_unregister,
	lrmd_event_exec_complete,
};

typedef struct lrmd_event_data_s {
	/* type of event */
	enum lrmd_callback_event type;
	/* the resource this event occurred on. */
	const char *rsc_id;
	/* the action performed */
	const char *action;
	/* api return code */
	int rc;
	/* executed ra return code */
	int exec_rc;
	/* client api call id associated with this event */
	int call_id;
	/* the lrmd status returned for exec_complete events */
	int lrmd_op_status;
} lrmd_event_data_t;

typedef void (*lrmd_event_callback)(lrmd_event_data_t *event, void *userdata);

typedef struct lrmd_list_s {
	const char *val;
	struct lrmd_list_s *next;
} lrmd_list_t;

void lrmd_list_freeall(lrmd_list_t *head);

typedef struct lrmd_api_operations_s
{
	/*!
	 * \brief Connect from the lrmd.
	 *
	 * \note This must be done before executing any other API functions.
	 *
	 * \retval 0, success
	 * \retval negative error code on failure
	 */
	int (*connect) (lrmd_t *lrmd, const char *client_name, int *lrmd_fd);

	/*!
	 * \brief Disconnect from the lrmd.
	 *
	 * \retval 0, success
	 * \retval negative error code on failure
	 */
	int (*disconnect)(lrmd_t *lrmd);

	/*!
	 * \brief Register a resource with the lrmd.
	 *
	 * \retval 0, success
	 * \retval negative error code on failure
	 */
	int (*register_rsc) (lrmd_t *lrmd,
		const char *rsc_id,
		const char *class,
		const char *provider,
		const char *agent,
		enum lrmd_call_options options);

	/*!
	 * \brief Unregister a resource from the lrmd.
	 *
	 * \note All pending and recurring operations will be cancelled
	 *       automatically.
	 *
	 * \retval 0, success
	 * \retval negative error code on failure
	 *
	 */
	int (*unregister_rsc) (lrmd_t *lrmd,
		const char *rsc_id,
		enum lrmd_call_options options);

	/*!
	 * \brief Sets the callback to receive lrmd events on.
	 */
	void (*set_callback) (lrmd_t *lrmd,
		void *userdata,
		lrmd_event_callback callback);

	/*!
	 * \brief Get the metadata documentation for a resource.
	 *
	 * \note Value is returned in output.  Output must be freed when set
	 *
	 * \retval lrmd_ok success
	 * \retval negative error code on failure
	 */
	int (*get_metadata) (lrmd_t *lrmd,
		const char *class,
		const char *provider,
		const char *agent,
		char **output,
		enum lrmd_call_options options);

	/*!
	 * \brief Issue a command on a resource
	 *
	 * \note Operations on individual resources are guaranteed to occur
	 *       in the order the client api calls them in.
	 *
	 * \note Operations between different resources are not guaranteed
	 *       to occur in any specific order in relation to one another
	 *       regardless of what order the client api is called in.
	 * \retval call_id to track async event result on success
	 * \retval negative error code on failure
	 */
	int (*exec)(lrmd_t *lrmd,
		const char *rsc_id,
		const char *action,
		int interval, /* ms */
		int timeout, /* ms */
		int start_delay, /* ms TODO IMPLEMENT START DELAY. it is ignored right now.*/
		enum lrmd_call_options options,
		lrmd_key_value_t *params); /* ownership of params is given up to api here */

	/*!
	 * \brief Cancel a recurring command.
	 *
	 * \note The cancel is completed async from this call.
	 *       We can be guaranteed the cancel has completed once
	 *       the callback receives an exec_complete event with
	 *       the lrmd_op_status signifying that the operation is
	 *       cancelled.
	 * \note For each resource, cancel operations and exec operations
	 *       are processed in the order they are received.
	 *       It is safe to assume that for a single resource, a cancel
	 *       will occur in the lrmd before an exec if the client's cancel
	 *       api call occurs before the exec api call.
	 *
	 *       It is not however safe to assume any operation on one resource will
	 *       occur before an operation on another resource regardless of
	 *       the order the client api is called in.
	 *
	 * \retval 0, cancel command sent.
	 * \retval negative error code on failure
	 */
	int (*cancel)(lrmd_t *lrmd,
		const char *rsc_id,
		const char *action,
		int interval);

	/*!
	 * \brief Retrieve a list of installed resource agents from all providers
	 *
	 * \note list must be freed using lrmd_list_freeall()
	 *
	 * \retval num items in list on success
	 * \retval negative error code on failure
	 */
	int (*list_agents)(lrmd_t *lrmd, lrmd_list_t **agents);

	/*!
	 * \brief Retrieve a list of resource agent providers
	 *
	 * \note When the agent is provided, only the agent's provider will be returned
	 * \note When no agent is supplied, all providers will be returned.
	 * \note List must be freed using lrmd_list_freeall()
	 *
	 * \retval num items in list on success
	 * \retval negative error code on failure
	 */
	int (*list_providers)(lrmd_t *lrmd,
		const char *agent,
		lrmd_list_t **providers);

	/*!
	 * \brief  Shutdown the lrmd process remotely
	 */
	int (*shutdown) (lrmd_t *lrmd);
} lrmd_api_operations_t;

struct lrmd_s {
	lrmd_api_operations_t *cmds;
	void *private;
};

#endif
