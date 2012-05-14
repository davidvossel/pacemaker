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

    if(condition & G_IO_NVAL) {
        crm_trace("Marking failed adaptor %p unused", adaptor);
        adaptor->is_used = QB_FALSE;
    }

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

    if (qb_ipcs_run(server)) {
        crm_err("IPC server %s failed to run. errno %d", name, errno);
        return NULL;
    }

    return server;
}

void mainloop_del_ipc_server(qb_ipcs_service_t *server) 
{
    qb_ipcs_destroy(server);
}

typedef struct mainloop_io_s
{
        char *name;
        void *userdata;

        guint source;
        crm_ipc_t *ipc;
        GIOChannel *channel;

        int (*dispatch_fn_ipc)(const char *buffer, ssize_t length, gpointer userdata);
        int (*dispatch_fn_io) (gpointer userdata);
        void (*destroy_fn) (gpointer userdata);

} mainloop_io_t;

static gboolean
mainloop_gio_callback(GIOChannel *gio, GIOCondition condition, gpointer data)
{
    gboolean keep = TRUE;
    mainloop_io_t *client = data;

    if(condition & G_IO_IN) {
        if(client->ipc) {
            long rc = crm_ipc_read(client->ipc);
            crm_trace("New message from %s[%p] = %d", client->name, client, rc);

            if(rc <= 0) {
                crm_perror(LOG_TRACE, "Message acquisition failed: %ld", rc);
        
            } else if(client->dispatch_fn_ipc) {
                const char *buffer = crm_ipc_buffer(client->ipc);
                if(client->dispatch_fn_ipc(buffer, rc, client->userdata) < 0) {
                    crm_trace("Connection to %s no longer required", client->name);
                    keep = FALSE;
                }
            }

        } else {
            crm_trace("New message from %s[%p]", client->name, client);
            if(client->dispatch_fn_io) {
                if(client->dispatch_fn_io(client->userdata) < 0) {
                    crm_trace("Connection to %s no longer required", client->name);
                    keep = FALSE;
                }
            }
        }
    }

    if(client->ipc && crm_ipc_connected(client->ipc) == FALSE) {
        crm_err("Connection to %s[%p] closed", client->name, client);
        keep = FALSE;
    } else if(condition & G_IO_HUP) {
        crm_trace("Recieved G_IO_HUP for %s [%p] connection", client->name, client);
        keep = FALSE;
    } else if(condition & G_IO_NVAL) {
        crm_err("Recieved G_IO_NVAL for %s [%p] connection", client->name, client);
        keep = FALSE;
    } else if(condition & G_IO_ERR) {
        crm_err("Recieved G_IO_ERR for %s [%p] connection", client->name, client);
        keep = FALSE;
    }
    
    return keep;
}

static void
mainloop_gio_destroy(gpointer c)
{
    mainloop_io_t *client = c;

    crm_trace("Destroying %s[%p]", client->name, c);
    if(client->destroy_fn) {
        client->destroy_fn(client->userdata);
    }
    
    if(client->ipc) {
        crm_ipc_close(client->ipc);
        crm_ipc_destroy(client->ipc);
    }
    free(client->name);
    free(client);
}

mainloop_io_t *
mainloop_add_ipc_client(
    const char *name, size_t max_size, void *userdata, struct ipc_client_callbacks *callbacks) 
{
    mainloop_io_t *client = NULL;
    crm_ipc_t *conn = crm_ipc_new(name, max_size);

    if(conn && crm_ipc_connect(conn)) {
        int32_t fd = crm_ipc_get_fd(conn);
        client = mainloop_add_fd(name, fd, userdata, NULL);
        client->ipc = conn;
        client->destroy_fn = callbacks->destroy;
        client->dispatch_fn_ipc = callbacks->dispatch;
    }

    if(conn && client == NULL) {
        crm_trace("Connection to %s failed", name);
        crm_ipc_close(conn);
        crm_ipc_destroy(conn);
    }
    
    return client;
}

void
mainloop_del_ipc_client(mainloop_io_t *client)
{
    mainloop_del_fd(client);
}

crm_ipc_t *
mainloop_get_ipc_client(mainloop_io_t *client)
{
    if(client) {
        return client->ipc;
    }
    return NULL;
}

mainloop_io_t *
mainloop_add_fd(
    const char *name, int fd, void *userdata, struct mainloop_fd_callbacks *callbacks) 
{
    mainloop_io_t *client = NULL;
    if(fd > 0) {
        crm_malloc0(client, sizeof(mainloop_io_t));          
        client->name = crm_strdup(name);
        client->userdata = userdata;

        if(callbacks) {
            client->destroy_fn = callbacks->destroy;
            client->dispatch_fn_io = callbacks->dispatch;
        }

        client->channel = g_io_channel_unix_new(fd);
        client->source = g_io_add_watch_full(
            client->channel, G_PRIORITY_DEFAULT, (G_IO_IN|G_IO_HUP|G_IO_NVAL|G_IO_ERR),
            mainloop_gio_callback, client, mainloop_gio_destroy);
        crm_trace("Added connection %d for %s[%p].%d", client->source, client->name, client, fd);
    }

    return client;
}

void
mainloop_del_fd(mainloop_io_t *client)
{
    if(client != NULL) {
        crm_trace("Removing client %s[%p]", client->name, client);
        g_io_channel_unref(client->channel);
        /* Results in mainloop_ipcc_destroy() being called once the source is removed from mainloop? */
    }
}

static gboolean
child_timeout_callback(gpointer p)
{
    mainloop_child_t *child = p;

    child->timerid = 0;
    if (child->timeout) {
        crm_crit("%s process (PID %d) will not die!", child->desc, (int)child->pid);
        return FALSE;
    }

    child->timeout = TRUE;
    crm_warn("%s process (PID %d) timed out", child->desc, (int)child->pid);

    if (kill(child->pid, SIGKILL) < 0) {
        if (errno == ESRCH) {
            /* Nothing left to do */
            return FALSE;
        }
        crm_perror(LOG_ERR, "kill(%d, KILL) failed", child->pid);
    }

    child->timerid = g_timeout_add(5000, child_timeout_callback, child);
    return FALSE;
}

static void
mainloop_child_destroy(mainloop_child_t *child)
{
    if (child->timerid != 0) {
        crm_trace("Removing timer %d", child->timerid);
        g_source_remove(child->timerid);
        child->timerid = 0;
    }

    free(child->desc);
    g_free(child);
}

static void
child_death_dispatch(GPid pid, gint status, gpointer user_data)
{
    int signo = 0;
    int exitcode = 0;
    mainloop_child_t *child = user_data;

    crm_trace("Managed process %d exited: %p", pid, child);

    if (WIFEXITED(status)) {
        exitcode = WEXITSTATUS(status);
        crm_trace("Managed process %d (%s) exited with rc=%d", pid,
                 child->desc, exitcode);

    } else if (WIFSIGNALED(status)) {
        signo = WTERMSIG(status);
        crm_trace("Managed process %d (%s) exited with signal=%d", pid,
                 child->desc, signo);
    }
#ifdef WCOREDUMP
    if (WCOREDUMP(status)) {
        crm_err("Managed process %d (%s) dumped core", pid, child->desc);
    }
#endif
    child->callback(child, status, signo, exitcode);
    crm_trace("Removed process entry for %d", pid);

    mainloop_child_destroy(child);
    return;
}

/* Create/Log a new tracked process
 * To track a process group, use -pid
 */
void
mainloop_add_child(pid_t pid, int timeout, const char *desc, void * privatedata,
    void (*callback)(mainloop_child_t *p, int status, int signo, int exitcode))
{
    mainloop_child_t *child = g_new(mainloop_child_t, 1);

    child->pid = pid;
    child->timerid = 0;
    child->timeout = FALSE;
    child->desc = strdup(desc);
    child->privatedata = privatedata;
    child->callback = callback;

    if (timeout) {
        child->timerid = g_timeout_add(
            timeout, child_timeout_callback, child);
    }

    child->watchid = g_child_watch_add(pid, child_death_dispatch, child);
}
