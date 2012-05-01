/* 
 * Copyright (C) 2004 Andrew Beekhof <andrew@beekhof.net>
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
 */

#include <crm_internal.h>

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#include <stdlib.h>
#include <signal.h>
#include <errno.h>

#include <sys/wait.h>

#include <crm/crm.h>
#include <crm/common/xml.h>
#include <crm/common/mainloop.h>
#include <crm/common/ipc.h>

static gboolean
crm_trigger_prepare(GSource * source, gint * timeout)
{
    crm_trigger_t *trig = (crm_trigger_t *) source;

    /* cluster-glue's FD and IPC related sources make use of
     * g_source_add_poll() but do not set a timeout in their prepare
     * functions
     *
     * This means mainloop's poll() will block until an event for one
     * of these sources occurs - any /other/ type of source, such as
     * this one or g_idle_*, that doesn't use g_source_add_poll() is
     * S-O-L and wont be processed until there is something fd-based
     * happens.
     *
     * Luckily the timeout we can set here affects all sources and
     * puts an upper limit on how long poll() can take.
     *
     * So unconditionally set a small-ish timeout, not too small that
     * we're in constant motion, which will act as an upper bound on
     * how long the signal handling might be delayed for.
     */
    *timeout = 500;             /* Timeout in ms */

    return trig->trigger;
}

static GHashTable *mainloop_process_table = NULL;

static gboolean
crm_trigger_check(GSource * source)
{
    crm_trigger_t *trig = (crm_trigger_t *) source;

    return trig->trigger;
}

static gboolean
crm_trigger_dispatch(GSource * source, GSourceFunc callback, gpointer userdata)
{
    crm_trigger_t *trig = (crm_trigger_t *) source;

    trig->trigger = FALSE;

    if (callback) {
        return callback(trig->user_data);
    }
    return TRUE;
}

static GSourceFuncs crm_trigger_funcs = {
    crm_trigger_prepare,
    crm_trigger_check,
    crm_trigger_dispatch,
    NULL
};

static crm_trigger_t *
mainloop_setup_trigger(GSource * source, int priority, gboolean(*dispatch) (gpointer user_data),
                       gpointer userdata)
{
    crm_trigger_t *trigger = NULL;

    trigger = (crm_trigger_t *) source;

    trigger->id = 0;
    trigger->trigger = FALSE;
    trigger->user_data = userdata;

    if (dispatch) {
        g_source_set_callback(source, dispatch, trigger, NULL);
    }

    g_source_set_priority(source, priority);
    g_source_set_can_recurse(source, FALSE);

    trigger->id = g_source_attach(source, NULL);
    return trigger;
}

crm_trigger_t *
mainloop_add_trigger(int priority, gboolean(*dispatch) (gpointer user_data), gpointer userdata)
{
    GSource *source = NULL;

    CRM_ASSERT(sizeof(crm_trigger_t) > sizeof(GSource));
    source = g_source_new(&crm_trigger_funcs, sizeof(crm_trigger_t));
    CRM_ASSERT(source != NULL);

    return mainloop_setup_trigger(source, priority, dispatch, userdata);
}

void
mainloop_set_trigger(crm_trigger_t * source)
{
    source->trigger = TRUE;
}

gboolean
mainloop_destroy_trigger(crm_trigger_t * source)
{
    source->trigger = FALSE;
    if (source->id > 0) {
        g_source_remove(source->id);
    }
    return TRUE;
}

typedef struct signal_s {
    crm_trigger_t trigger;      /* must be first */
    void (*handler) (int sig);
    int signal;

} crm_signal_t;

static crm_signal_t *crm_signals[NSIG];

static gboolean
crm_signal_dispatch(GSource * source, GSourceFunc callback, gpointer userdata)
{
    crm_signal_t *sig = (crm_signal_t *) source;

    crm_info("Invoking handler for signal %d: %s", sig->signal, strsignal(sig->signal));

    sig->trigger.trigger = FALSE;
    if (sig->handler) {
        sig->handler(sig->signal);
    }
    return TRUE;
}

static void
mainloop_signal_handler(int sig)
{
    if (sig > 0 && sig < NSIG && crm_signals[sig] != NULL) {
        mainloop_set_trigger((crm_trigger_t *) crm_signals[sig]);
    }
}

static GSourceFuncs crm_signal_funcs = {
    crm_trigger_prepare,
    crm_trigger_check,
    crm_signal_dispatch,
    NULL
};

gboolean
crm_signal(int sig, void (*dispatch) (int sig))
{
    sigset_t mask;
    struct sigaction sa;
    struct sigaction old;

    if (sigemptyset(&mask) < 0) {
        crm_perror(LOG_ERR, "Call to sigemptyset failed");
        return FALSE;
    }

    memset(&sa, 0, sizeof(struct sigaction));
    sa.sa_handler = dispatch;
    sa.sa_flags = SA_RESTART;
    sa.sa_mask = mask;

    if (sigaction(sig, &sa, &old) < 0) {
        crm_perror(LOG_ERR, "Could not install signal handler for signal %d", sig);
        return FALSE;
    }

    return TRUE;
}

gboolean
mainloop_add_signal(int sig, void (*dispatch) (int sig))
{
    GSource *source = NULL;
    int priority = G_PRIORITY_HIGH - 1;

    if (sig == SIGTERM) {
        /* TERM is higher priority than other signals,
         *   signals are higher priority than other ipc.
         * Yes, minus: smaller is "higher"
         */
        priority--;
    }

    if (sig >= NSIG || sig < 0) {
        crm_err("Signal %d is out of range", sig);
        return FALSE;

    } else if (crm_signals[sig] != NULL) {
        crm_err("Signal handler for %d is already installed", sig);
        return FALSE;
    }

    CRM_ASSERT(sizeof(crm_signal_t) > sizeof(GSource));
    source = g_source_new(&crm_signal_funcs, sizeof(crm_signal_t));

    crm_signals[sig] = (crm_signal_t *) mainloop_setup_trigger(source, priority, NULL, NULL);
    CRM_ASSERT(crm_signals[sig] != NULL);

    crm_signals[sig]->handler = dispatch;
    crm_signals[sig]->signal = sig;

    if (crm_signal(sig, mainloop_signal_handler) == FALSE) {
        crm_signal_t *tmp = crm_signals[sig];

        crm_signals[sig] = NULL;

        mainloop_destroy_trigger((crm_trigger_t *) tmp);
        return FALSE;
    }
#if 0
    /* If we want signals to interrupt mainloop's poll(), instead of waiting for
     * the timeout, then we should call siginterrupt() below
     *
     * For now, just enforce a low timeout
     */
    if (siginterrupt(sig, 1) < 0) {
        crm_perror(LOG_INFO, "Could not enable system call interruptions for signal %d", sig);
    }
#endif

    return TRUE;
}

gboolean
mainloop_destroy_signal(int sig)
{
    crm_signal_t *tmp = NULL;

    if (sig >= NSIG || sig < 0) {
        crm_err("Signal %d is out of range", sig);
        return FALSE;

    } else if (crm_signal(sig, NULL) == FALSE) {
        crm_perror(LOG_ERR, "Could not uninstall signal handler for signal %d", sig);
        return FALSE;

    } else if (crm_signals[sig] == NULL) {
        return TRUE;
    }

    tmp = crm_signals[sig];
    crm_signals[sig] = NULL;
    mainloop_destroy_trigger((crm_trigger_t *) tmp);
    return TRUE;
}

static qb_array_t *gio_map = NULL;

/*
 * libqb...
 */
struct gio_to_qb_poll {
        int32_t is_used;
        GIOChannel *channel;
        int32_t events;
        void * data;
        qb_ipcs_dispatch_fn_t fn;
        enum qb_loop_priority p;
};

static gboolean
gio_read_socket (GIOChannel *gio, GIOCondition condition, gpointer data)
{
    struct gio_to_qb_poll *adaptor = (struct gio_to_qb_poll *)data;
    gint fd = g_io_channel_unix_get_fd(gio);

    crm_trace("%p.%d %d vs. %d (G_IO_IN)", data, fd, condition, (condition & G_IO_IN));
    crm_trace("%p.%d %d vs. %d (G_IO_HUP)", data, fd, condition, (condition & G_IO_HUP));

    return (adaptor->fn(fd, condition, adaptor->data) == 0);
}

static void
gio_destroy(gpointer data) 
{
    struct gio_to_qb_poll *adaptor = (struct gio_to_qb_poll *)data;
    crm_trace("Marking adaptor %p unused", adaptor);
    adaptor->is_used = QB_FALSE;
}


static int32_t
gio_poll_dispatch_add(enum qb_loop_priority p, int32_t fd, int32_t evts,
                  void *data, qb_ipcs_dispatch_fn_t fn)
{
    struct gio_to_qb_poll *adaptor;
    GIOChannel *channel;
    int32_t res = 0;

    res = qb_array_index(gio_map, fd, (void**)&adaptor);
    if (res < 0) {
        crm_err("Array lookup failed for fd=%d: %d", fd, res);
        return res;
    }

    crm_trace("Adding fd=%d to mainloop as adapater %p", fd, adaptor);
    if (adaptor->is_used) {
        crm_err("Adapter for descriptor %d is still in-use", fd);
        return -EEXIST;
    }

    channel = g_io_channel_unix_new(fd);
    if (!channel) {
        crm_err("No memory left to add fd=%d", fd);
        return -ENOMEM;
    }

    /* Because unlike the poll() API, glib doesn't tell us about HUPs by default */
    evts |= (G_IO_HUP|G_IO_NVAL|G_IO_ERR);

    adaptor->channel = channel;
    adaptor->fn = fn;
    adaptor->events = evts;
    adaptor->data = data;
    adaptor->p = p;
    adaptor->is_used = QB_TRUE;

    res = g_io_add_watch_full(channel, G_PRIORITY_DEFAULT, evts, gio_read_socket, adaptor, gio_destroy);
    crm_trace("Added to mainloop with gsource id=%d", res);
    if(res > 0) {
        return 0;
    }
    
    return -EINVAL;
}

static int32_t
gio_poll_dispatch_mod(enum qb_loop_priority p, int32_t fd, int32_t evts,
                  void *data, qb_ipcs_dispatch_fn_t fn)
{
    return 0;
}

static int32_t
gio_poll_dispatch_del(int32_t fd)
{
    struct gio_to_qb_poll *adaptor;
    crm_trace("Looking for fd=%d", fd);
    if (qb_array_index(gio_map, fd, (void**)&adaptor) == 0) {
        crm_trace("Marking adaptor %p unused", adaptor);
        g_io_channel_unref(adaptor->channel);
        adaptor->is_used = QB_FALSE;
    }
    return 0;
}

struct qb_ipcs_poll_handlers gio_poll_funcs = {
    .job_add = NULL,
    .dispatch_add = gio_poll_dispatch_add,
    .dispatch_mod = gio_poll_dispatch_mod,
    .dispatch_del = gio_poll_dispatch_del,
};

static enum qb_ipc_type
pick_ipc_type(enum qb_ipc_type requested)
{
    const char *env = getenv("PCMK_ipc_type");

    if(env && strcmp("shared-mem", env) == 0) {
        return QB_IPC_SHM;
    } else if(env && strcmp("socket", env) == 0) {
        return QB_IPC_SOCKET;
    } else if(env && strcmp("posix", env) == 0) {
        return QB_IPC_POSIX_MQ;
    } else if(env && strcmp("sysv", env) == 0) {
        return QB_IPC_SYSV_MQ;
    } else if(requested == QB_IPC_NATIVE) {
        /* We prefer sockets actually */
        return QB_IPC_SOCKET;
    }
    return requested;
}

qb_ipcs_service_t *mainloop_add_ipc_server(
    const char *name, enum qb_ipc_type type, struct qb_ipcs_service_handlers *callbacks) 
{
    qb_ipcs_service_t* server = NULL;

    if(gio_map == NULL) {
        gio_map = qb_array_create_2(64, sizeof(struct gio_to_qb_poll), 1);
    }

    server = qb_ipcs_create(name, 0, pick_ipc_type(type), callbacks);
    qb_ipcs_poll_handlers_set(server, &gio_poll_funcs);
    qb_ipcs_run(server);

    return server;
}

void mainloop_del_ipc_server(qb_ipcs_service_t *server) 
{
    qb_ipcs_destroy(server);
}

typedef struct mainloop_ipc_s
{
        GFDSource *source;
        crm_ipc_t *ipc;
        void *userdata;
        char *name;

        struct ipc_client_callbacks *callbacks;

} mainloop_ipc_t;

static gboolean
mainloop_ipcc_callback(int fd, gpointer c)
{
    long rc = 0;
    gboolean keep = TRUE;
    mainloop_ipc_t *client = c;

    do {
        rc = crm_ipc_read(client->ipc);
        crm_trace("New message for %s[%p] = %d", client->name, c, rc);

        if(rc <= 0) {
            crm_perror(LOG_TRACE, "Message acquisition failed: %ld", rc);
        
        } else if(client->callbacks && client->callbacks->dispatch) {
            const char *buffer = crm_ipc_buffer(client->ipc);
            if(client->callbacks->dispatch(buffer, rc, client->userdata) < 0) {
                crm_trace("Connection to %s no longer required", client->name);
                keep = FALSE;
            } else {
                crm_trace("delivered: %.60s", buffer);
            }

        } else {
            crm_trace("No callbacks? %p", client->callbacks);
        }

    } while(keep && rc > 0);
    
    if(crm_ipc_connected(client->ipc) == FALSE) {
        crm_err("Connection to %s[%p] closed", client->name, c);
        keep = FALSE;
    }
    
    return keep;
}

static void
mainloop_ipcc_destroy(gpointer c)
{
    mainloop_ipc_t *client = c;

    crm_trace("Destroying %s[%p]", client->name, c);
    if(client->callbacks && client->callbacks->destroy) {
        client->callbacks->destroy(client->userdata);
    }
    
    crm_ipc_close(client->ipc);
    crm_ipc_destroy(client->ipc);
    free(client->name);
    free(client);
}


mainloop_ipc_t *
mainloop_add_ipc_client(
    const char *name, size_t max_size, void *userdata, struct ipc_client_callbacks *callbacks) 
{
    mainloop_ipc_t *client = NULL;
    crm_ipc_t *conn = crm_ipc_new(name, max_size);

    if(conn && crm_ipc_connect(conn)) {
        int32_t fd = crm_ipc_get_fd(conn);

        if(fd > 0) {
            crm_malloc0(client, sizeof(mainloop_ipc_t));            
            client->ipc = conn;
            client->name = crm_strdup(name);
            client->userdata = userdata;
            client->callbacks = callbacks;
            client->source = G_main_add_fd(G_PRIORITY_HIGH, fd, FALSE,
                                           mainloop_ipcc_callback, client, mainloop_ipcc_destroy);
            crm_trace("Added connection %s[%p].%d", client->name, client, fd);
        }
    }

    if(conn && client == NULL) {
        crm_trace("Connection to %s failed", name);
        crm_ipc_close(conn);
        crm_ipc_destroy(conn);
    }
    
    return client;
}

void
mainloop_del_ipc_client(mainloop_ipc_t *client)
{
    crm_trace("Removing client %s[%p]", client->name, client);
    if(client != NULL) {
        G_main_del_fd(client->source);
        /* Results in mainloop_ipcc_destroy() being called once the source is removed from mainloop */
    }
}

crm_ipc_t *
mainloop_get_ipc_client(mainloop_ipc_t *client)
{
    if(client) {
        return client->ipc;
    }
    return NULL;
}

static gboolean
mainloop_fd_prepare(GSource *source, gint *timeout)
{
    return FALSE;
}

static gboolean
mainloop_fd_check(GSource* source)
{
    mainloop_fd_t *trig = (mainloop_fd_t *) source;
    if (trig->gpoll.revents) {
        return TRUE;
    }
    return FALSE;
}

static gboolean
mainloop_fd_dispatch(GSource *source, GSourceFunc callback, gpointer userdata)
{
    mainloop_fd_t *trig = (mainloop_fd_t *) source;
    crm_trace("%p", source);
    /*
     * Is output now unblocked?
     *
     * If so, turn off OUTPUT_EVENTS to avoid going into
     * a tight poll(2) loop.
     */
    if (trig->gpoll.revents & G_IO_OUT) {
        trig->gpoll.events &= ~G_IO_OUT;
    }

    if (trig->dispatch != NULL
        && trig->dispatch(trig->gpoll.fd, trig->user_data) == FALSE) {
        g_source_remove_poll(source, &trig->gpoll);
        g_source_unref(source); /* Really? */
        return FALSE;
    }
    return TRUE;
}

static void
mainloop_fd_destroy(GSource *source)
{
    mainloop_fd_t *trig = (mainloop_fd_t *) source;
    crm_trace("%p", source);

    if (trig->dnotify) {
        trig->dnotify(trig->user_data);
    }
}

static GSourceFuncs mainloop_fd_funcs = {
    mainloop_fd_prepare,
    mainloop_fd_check,
    mainloop_fd_dispatch,
    mainloop_fd_destroy,
};

mainloop_fd_t *
mainloop_add_fd(int priority, int fd,
                gboolean (*dispatch)(int fd, gpointer userdata),
                GDestroyNotify notify, gpointer userdata)
{
    GSource *source = NULL;
    mainloop_fd_t *fd_source = NULL;
    CRM_ASSERT(sizeof(mainloop_fd_t) > sizeof(GSource));
    source = g_source_new(&mainloop_fd_funcs, sizeof(mainloop_fd_t));
    CRM_ASSERT(source != NULL);

    fd_source = (mainloop_fd_t *) source;
    fd_source->id = 0;
    fd_source->gpoll.fd = fd;
    fd_source->gpoll.events = G_IO_ERR|G_IO_NVAL|G_IO_IN|G_IO_PRI|G_IO_HUP;
    fd_source->gpoll.revents = 0;

    /*
     * Normally we'd use g_source_set_callback() to specify the dispatch
     * function, but we want to supply the fd too, so we store it in
     * fd_source->dispatch instead
     */
    fd_source->dnotify = notify;
    fd_source->dispatch = dispatch;
    fd_source->user_data = userdata;

    g_source_set_priority(source, priority);
    g_source_set_can_recurse(source, FALSE);
    g_source_add_poll(source, &fd_source->gpoll);

    fd_source->id = g_source_attach(source, NULL);
    return fd_source;
}

gboolean
mainloop_destroy_fd(mainloop_fd_t *source)
{
    g_source_remove_poll((GSource *) source, &source->gpoll);
    g_source_remove(source->id);
    source->id = 0;
    g_source_unref((GSource *) source);

    return TRUE;
}

static gboolean
child_timeout_callback(gpointer p)
{
    pid_t pid = (pid_t) GPOINTER_TO_INT(p);
    mainloop_child_t *pinfo = (mainloop_child_t *) g_hash_table_lookup(
            mainloop_process_table, p);

    if (pinfo == NULL) {
        return FALSE;
    }

    pinfo->timerid = 0;
    if (pinfo->timeout) {
        crm_crit("%s process (PID %d) will not die!", pinfo->desc, (int)pid);
        return FALSE;
    }

    pinfo->timeout = TRUE;
    crm_warn("%s process (PID %d) timed out", pinfo->desc, (int)pid);

    if (kill(pinfo->pid, SIGKILL) < 0) {
        if (errno == ESRCH) {
            /* Nothing left to do */
            return FALSE;
        }
        crm_perror(LOG_ERR, "kill(%d, KILL) failed", pid);
    }

    pinfo->timerid = g_timeout_add(5000, child_timeout_callback, p);
    return FALSE;
}

static void
mainloop_child_destroy(gpointer data)
{
    mainloop_child_t *p = data;

    free(p->desc);

    g_free(p);
}

/* Create/Log a new tracked process
 * To track a process group, use -pid
 */
void
mainloop_add_child(pid_t pid, int timeout, const char *desc, void * privatedata,
                   void (*callback)(mainloop_child_t *p, int status, int signo,
                                    int exitcode))
{
    mainloop_child_t *p = g_new(mainloop_child_t, 1);

    if (mainloop_process_table == NULL) {
        mainloop_process_table = g_hash_table_new_full(
            g_direct_hash, g_direct_equal, NULL, mainloop_child_destroy);
    }

    p->pid = pid;
    p->timerid = 0;
    p->timeout = FALSE;
    p->desc = strdup(desc);
    p->privatedata = privatedata;
    p->callback = callback;

    if (timeout) {
        p->timerid = g_timeout_add(
            timeout, child_timeout_callback, GINT_TO_POINTER(pid));
    }

    g_hash_table_insert(mainloop_process_table, GINT_TO_POINTER(abs(pid)), p);
}

static void
child_death_dispatch(int sig)
{
    int status = 0;
    while (TRUE) {
        pid_t pid = wait3(&status, WNOHANG, NULL);
        if (pid > 0) {
            int signo = 0, exitcode = 0;

            mainloop_child_t *p = g_hash_table_lookup(mainloop_process_table,
                                                      GINT_TO_POINTER(pid));
            crm_trace("Managed process %d exited: %p", pid, p);
            if (p == NULL) {
                continue;
            }

            if (WIFEXITED(status)) {
                exitcode = WEXITSTATUS(status);
                crm_trace("Managed process %d (%s) exited with rc=%d", pid,
                         p->desc, exitcode);

            } else if (WIFSIGNALED(status)) {
                signo = WTERMSIG(status);
                crm_trace("Managed process %d (%s) exited with signal=%d", pid,
                         p->desc, signo);
            }
#ifdef WCOREDUMP
            if (WCOREDUMP(status)) {
                crm_err("Managed process %d (%s) dumped core", pid, p->desc);
            }
#endif
            if (p->timerid != 0) {
                crm_trace("Removing timer %d", p->timerid);
                g_source_remove(p->timerid);
                p->timerid = 0;
            }
            p->callback(p, status, signo, exitcode);
            g_hash_table_remove(mainloop_process_table, GINT_TO_POINTER(pid));
            crm_trace("Removed process entry for %d", pid);
            return;

        } else {
            if (errno == EAGAIN) {
                continue;
            } else if (errno != ECHILD) {
                crm_perror(LOG_ERR, "wait3() failed");
            }
            break;
        }
    }
}

void
mainloop_track_children(int priority)
{
    if (mainloop_process_table == NULL) {
        mainloop_process_table = g_hash_table_new_full(
            g_direct_hash, g_direct_equal, NULL, NULL/*TODO: Add destructor */);
    }

    mainloop_add_signal(SIGCHLD, child_death_dispatch);
}
