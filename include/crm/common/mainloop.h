/* 
 * Copyright (C) 2009 Andrew Beekhof <andrew@beekhof.net>
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
#ifndef CRM_COMMON_MAINLOOP__H
#  define CRM_COMMON_MAINLOOP__H

#  include <glib.h>

typedef struct trigger_s {
    GSource source;
    gboolean trigger;
    void *user_data;
    guint id;

} crm_trigger_t;

extern crm_trigger_t *mainloop_add_trigger(int priority, gboolean(*dispatch) (gpointer user_data),
                                           gpointer userdata);

extern void mainloop_set_trigger(crm_trigger_t * source);

extern gboolean mainloop_destroy_trigger(crm_trigger_t * source);

extern gboolean crm_signal(int sig, void (*dispatch) (int sig));

extern gboolean mainloop_add_signal(int sig, void (*dispatch) (int sig));

extern gboolean mainloop_destroy_signal(int sig);

#include <crm/common/ipc.h>

struct ipc_client_callbacks 
{
        int (*dispatch)(const char *buffer, ssize_t length, gpointer userdata);
        void (*destroy) (gpointer);
};

qb_ipcs_service_t *mainloop_add_ipc_server(
    const char *name, enum qb_ipc_type type, struct qb_ipcs_service_handlers *callbacks);

void mainloop_del_ipc_server(qb_ipcs_service_t *server);

typedef struct mainloop_ipc_s mainloop_ipc_t;

mainloop_ipc_t *mainloop_add_ipc_client(
    const char *name, size_t max_size, void *userdata, struct ipc_client_callbacks *callbacks);

void mainloop_del_ipc_client(mainloop_ipc_t *client);

crm_ipc_t *mainloop_get_ipc_client(mainloop_ipc_t *client);

typedef struct mainloop_fd_s {
    GSource source;
    GPollFD	gpoll;
    guint id;
    void *user_data;
    GDestroyNotify dnotify;
    gboolean (*dispatch)(int fd, gpointer user_data);
} mainloop_fd_t;

mainloop_fd_t *
mainloop_add_fd(int priority, int fd,
                gboolean (*dispatch)(int fd, gpointer userdata),
                GDestroyNotify notify, gpointer userdata);

gboolean
mainloop_destroy_fd(mainloop_fd_t* source);

typedef struct mainloop_child_s mainloop_child_t;
struct mainloop_child_s {
    pid_t     pid;
    char     *desc;
    unsigned  timerid;
    gboolean  timeout;
    void     *privatedata;

    /* Called when a process dies */
    void (*callback)(mainloop_child_t* p, int status, int signo, int exitcode);
};

void
mainloop_track_children(int priority);

/*
 * Create a new tracked process
 * To track a process group, use -pid
 */
void
mainloop_add_child(pid_t pid, int timeout, const char *desc, void *privatedata,
                   void (*callback)(mainloop_child_t* p, int status, int signo,
                                    int exitcode));
#endif
