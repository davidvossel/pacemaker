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
#ifndef CRM_COMMON_CLUSTER__H
#  define CRM_COMMON_CLUSTER__H

#  include <crm/common/xml.h>
#  include <crm/common/msg.h>
#  include <crm/common/util.h>
#  include <crm/ais.h>

#  if SUPPORT_HEARTBEAT
#    include <heartbeat/hb_api.h>
#    include <ocf/oc_event.h>
#  endif

extern gboolean crm_have_quorum;
extern GHashTable *crm_peer_cache;
extern GHashTable *crm_peer_id_cache;
extern unsigned long long crm_peer_seq;

extern void crm_peer_init(void);
extern void crm_peer_destroy(void);
extern char *get_corosync_uuid(uint32_t id, const char *uname);
extern const char *get_node_uuid(uint32_t id, const char *uname);
extern int get_corosync_id(int id, const char *uuid);

extern gboolean crm_cluster_connect(char **our_uname, char **our_uuid, void *dispatch,
                                    void *destroy,
#  if SUPPORT_HEARTBEAT
                                    ll_cluster_t ** hb_conn
#  else
                                    void **unused
#  endif
    );

extern gboolean init_cman_connection(gboolean(*dispatch) (unsigned long long, gboolean),
                                     void (*destroy) (gpointer));

extern gboolean init_quorum_connection(gboolean(*dispatch) (unsigned long long, gboolean),
                                       void (*destroy) (gpointer));

extern gboolean send_cluster_message(const char *node, enum crm_ais_msg_types service,
                                     xmlNode * data, gboolean ordered);

extern void destroy_crm_node(gpointer data);

extern crm_node_t *crm_get_peer(unsigned int id, const char *uname);

extern void crm_update_peer_proc(const char *source, crm_node_t *peer, uint32_t flag, const char *status);
extern crm_node_t *crm_update_peer(const char *source, unsigned int id, uint64_t born, uint64_t seen, int32_t votes,
                                   uint32_t children, const char *uuid, const char *uname,
                                   const char *addr, const char *state);

extern guint crm_active_peers(void);
extern gboolean crm_is_peer_active(const crm_node_t * node);
extern guint reap_crm_member(uint32_t id);
extern int crm_terminate_member(int nodeid, const char *uname, IPC_Channel * cluster);
extern int crm_terminate_member_no_mainloop(int nodeid, const char *uname, int *connection);
extern gboolean crm_get_cluster_name(char **cname);

#  if SUPPORT_HEARTBEAT
gboolean crm_is_heartbeat_peer_active(const crm_node_t * node);
extern gboolean ccm_have_quorum(oc_ed_t event);
extern const char *ccm_event_name(oc_ed_t event);
extern crm_node_t *crm_update_ccm_node(const oc_ev_membership_t * oc, int offset, const char *state,
                                       uint64_t seq);
#  endif

#  if SUPPORT_COROSYNC
extern int ais_fd_sync;
extern GFDSource *ais_source;
gboolean crm_is_corosync_peer_active(const crm_node_t * node);
extern gboolean send_ais_text(int class, const char *data, gboolean local,
                              const char *node, enum crm_ais_msg_types dest);
extern gboolean get_ais_nodeid(uint32_t * id, char **uname);
#  endif

extern void empty_uuid_cache(void);
extern const char *get_uuid(const char *uname);
extern const char *get_uname(const char *uuid);
extern void set_uuid(xmlNode * node, const char *attr, const char *uname);
extern void unget_uuid(const char *uname);

enum crm_status_type {
    crm_status_uname,
    crm_status_nstate,
    crm_status_processes,
};

enum crm_ais_msg_types text2msg_type(const char *text);
extern void
 crm_set_status_callback(void (*dispatch) (enum crm_status_type, crm_node_t *, const void *));

/* *INDENT-OFF* */
enum cluster_type_e 
{
    pcmk_cluster_unknown     = 0x0001,
    pcmk_cluster_invalid     = 0x0002,
    pcmk_cluster_heartbeat   = 0x0004,
    pcmk_cluster_classic_ais = 0x0010,
    pcmk_cluster_corosync    = 0x0020,
    pcmk_cluster_cman        = 0x0040,
};
/* *INDENT-ON* */

extern enum cluster_type_e get_cluster_type(void);
extern const char *name_for_cluster_type(enum cluster_type_e type);

extern gboolean is_corosync_cluster(void);
extern gboolean is_cman_cluster(void);
extern gboolean is_openais_cluster(void);
extern gboolean is_classic_ais_cluster(void);
extern gboolean is_heartbeat_cluster(void);

#endif
