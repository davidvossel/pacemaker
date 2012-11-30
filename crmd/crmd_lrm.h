/* 
 * Copyright (C) 2004 Andrew Beekhof <andrew@beekhof.net>
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 * 
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */


extern gboolean verify_stopped(enum crmd_fsa_state cur_state, int log_level);
extern void lrm_clear_last_failure(const char *rsc_id);

typedef struct resource_history_s {
    char *id;
    lrmd_rsc_info_t rsc;
    lrmd_event_data_t *last;
    lrmd_event_data_t *failed;
    GList *recurring_op_list;

    /* Resources must be stopped using the same
     * parameters they were started with.  This hashtable
     * holds the parameters that should be used for the next stop
     * cmd on this resource. */
    GHashTable *stop_params;
} rsc_history_t;

struct recurring_op_s {
    char *rsc_id;
    char *op_type;
    char *op_key;
    int call_id;
    int interval;
    gboolean remove;
    gboolean cancelled;
};

typedef struct lrm_state_s {
    const char *node_name;
    lrmd_t *conn;

    GHashTable *resource_history;
    GHashTable *pending_ops;
    GHashTable *deletion_ops;

    int num_lrm_register_fails;
} lrm_state_t;

struct pending_deletion_op_s {
    char *rsc;
    ha_msg_input_t *input;
};

lrm_state_t *lrm_state_create(const char *node_name);
void lrm_state_destroy(const char *node_name);
void lrm_state_reset_tables(lrm_state_t *lrm_state);
GList *lrm_state_get_list(void);

gboolean lrm_state_init_local(void);
void lrm_state_destroy_all(void);

/*!
 * \brief Find lrm_state data by either node name
 */
lrm_state_t *lrm_state_find(const char *node_name);

/*!
 * \brief Either find a existing state object with the node name
 *        or create a new one if no object is found.
 */
lrm_state_t *lrm_state_find_or_create(const char *node_name);

