#include <crm/common/mainloop.h>

typedef struct stonith_device_s {
    char *id;
    char *agent;
    char *namespace;

    GListPtr targets;
    time_t targets_age;
    gboolean has_attr_map;
    guint priority;
    guint active_pid;

    GHashTable *params;
    GHashTable *aliases;
    GList *pending_ops;
    crm_trigger_t *work;

} stonith_device_t;

typedef struct stonith_client_s {
    char *id;
    char *name;

    char *channel_name;
    qb_ipcs_connection_t *channel;

    long long flags;

} stonith_client_t;

typedef struct remote_fencing_op_s {
    char *id;
    char *target;
    char *action;
    guint replies;

    guint op_timer;
    guint query_timer;
    guint base_timeout;

    char *delegate;
    time_t completed;
    long long call_options;

    enum op_state state;
    char *client_id;
    char *originator;
    GListPtr query_results;
    xmlNode *request;

    guint level;                /* ABI */
    GListPtr devices;           /* ABI */

} remote_fencing_op_t;

typedef struct stonith_topology_s {
    char *node;
    GListPtr levels[ST_LEVEL_MAX];

} stonith_topology_t;

extern long long get_stonith_flag(const char *name);

extern void stonith_command(stonith_client_t * client, xmlNode * op_request, const char *remote);

extern int stonith_device_register(xmlNode * msg);

extern int stonith_level_register(xmlNode * msg);

extern void do_local_reply(xmlNode * notify_src, const char *client_id, gboolean sync_reply,
                           gboolean from_peer);

extern xmlNode *stonith_construct_reply(xmlNode * request, char *output, xmlNode * data, int rc);

extern xmlNode *stonith_construct_async_reply(async_command_t * cmd, char *output, xmlNode * data,
                                              int rc);;

extern void do_stonith_notify(int options, const char *type, enum stonith_errors result,
                              xmlNode * data, const char *remote);

extern remote_fencing_op_t *initiate_remote_stonith_op(stonith_client_t * client, xmlNode * request,
                                                       gboolean manual_ack);

extern int process_remote_stonith_exec(xmlNode * msg);

extern int process_remote_stonith_query(xmlNode * msg);

extern void *create_remote_stonith_op(const char *client, xmlNode * request, gboolean peer);

extern int stonith_fence_history(xmlNode * msg, xmlNode ** output);

extern void free_device(gpointer data);

extern void free_topology_entry(gpointer data);

extern int stonith_level_remove(xmlNode * msg);

extern int stonith_level_register(xmlNode * msg);

extern char *stonith_our_uname;
extern gboolean stand_alone;
extern GHashTable *device_list;
extern GHashTable *topology;


