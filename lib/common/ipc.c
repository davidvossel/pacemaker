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

#include <sys/param.h>

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <errno.h>
#include <fcntl.h>

#include <crm/crm.h>
#include <crm/msg_xml.h>
#include <crm/common/ipc.h>
#include <crm/common/cluster.h>

xmlNode *
create_request_adv(const char *task, xmlNode * msg_data,
                   const char *host_to, const char *sys_to,
                   const char *sys_from, const char *uuid_from, const char *origin)
{
    char *true_from = NULL;
    xmlNode *request = NULL;
    char *reference = generateReference(task, sys_from);

    if (uuid_from != NULL) {
        true_from = generate_hash_key(sys_from, uuid_from);
    } else if (sys_from != NULL) {
        true_from = crm_strdup(sys_from);
    } else {
        crm_err("No sys from specified");
    }

    /* host_from will get set for us if necessary by CRMd when routed */
    request = create_xml_node(NULL, __FUNCTION__);
    crm_xml_add(request, F_CRM_ORIGIN, origin);
    crm_xml_add(request, F_TYPE, T_CRM);
    crm_xml_add(request, F_CRM_VERSION, CRM_FEATURE_SET);
    crm_xml_add(request, F_CRM_MSG_TYPE, XML_ATTR_REQUEST);
    crm_xml_add(request, XML_ATTR_REFERENCE, reference);
    crm_xml_add(request, F_CRM_TASK, task);
    crm_xml_add(request, F_CRM_SYS_TO, sys_to);
    crm_xml_add(request, F_CRM_SYS_FROM, true_from);

    /* HOSTTO will be ignored if it is to the DC anyway. */
    if (host_to != NULL && strlen(host_to) > 0) {
        crm_xml_add(request, F_CRM_HOST_TO, host_to);
    }

    if (msg_data != NULL) {
        add_message_xml(request, F_CRM_DATA, msg_data);
    }
    crm_free(reference);
    crm_free(true_from);

    return request;
}

ha_msg_input_t *
new_ha_msg_input(xmlNode * orig)
{
    ha_msg_input_t *input_copy = NULL;

    crm_malloc0(input_copy, sizeof(ha_msg_input_t));
    input_copy->msg = orig;
    input_copy->xml = get_message_xml(input_copy->msg, F_CRM_DATA);
    return input_copy;
}

void
delete_ha_msg_input(ha_msg_input_t * orig)
{
    if (orig == NULL) {
        return;
    }
    free_xml(orig->msg);
    crm_free(orig);
}

/*
 * This method adds a copy of xml_response_data
 */
xmlNode *
create_reply_adv(xmlNode * original_request, xmlNode * xml_response_data, const char *origin)
{
    xmlNode *reply = NULL;

    const char *host_from = crm_element_value(original_request, F_CRM_HOST_FROM);
    const char *sys_from = crm_element_value(original_request, F_CRM_SYS_FROM);
    const char *sys_to = crm_element_value(original_request, F_CRM_SYS_TO);
    const char *type = crm_element_value(original_request, F_CRM_MSG_TYPE);
    const char *operation = crm_element_value(original_request, F_CRM_TASK);
    const char *crm_msg_reference = crm_element_value(original_request, XML_ATTR_REFERENCE);

    if (type == NULL) {
        crm_err("Cannot create new_message," " no message type in original message");
        CRM_ASSERT(type != NULL);
        return NULL;
#if 0
    } else if (strcasecmp(XML_ATTR_REQUEST, type) != 0) {
        crm_err("Cannot create new_message," " original message was not a request");
        return NULL;
#endif
    }
    reply = create_xml_node(NULL, __FUNCTION__);
    crm_xml_add(reply, F_CRM_ORIGIN, origin);
    crm_xml_add(reply, F_TYPE, T_CRM);
    crm_xml_add(reply, F_CRM_VERSION, CRM_FEATURE_SET);
    crm_xml_add(reply, F_CRM_MSG_TYPE, XML_ATTR_RESPONSE);
    crm_xml_add(reply, XML_ATTR_REFERENCE, crm_msg_reference);
    crm_xml_add(reply, F_CRM_TASK, operation);

    /* since this is a reply, we reverse the from and to */
    crm_xml_add(reply, F_CRM_SYS_TO, sys_from);
    crm_xml_add(reply, F_CRM_SYS_FROM, sys_to);

    /* HOSTTO will be ignored if it is to the DC anyway. */
    if (host_from != NULL && strlen(host_from) > 0) {
        crm_xml_add(reply, F_CRM_HOST_TO, host_from);
    }

    if (xml_response_data != NULL) {
        add_message_xml(reply, F_CRM_DATA, xml_response_data);
    }

    return reply;
}


/* Libqb based IPC */

/* Server... */

int
crm_ipcs_client_pid(qb_ipcs_connection_t *c)
{
    struct qb_ipcs_connection_stats stats;
    stats.client_pid = 0;
    qb_ipcs_connection_stats_get(c, &stats, 0);
    return stats.client_pid;
}

xmlNode *
crm_ipcs_recv(qb_ipcs_connection_t *c, void *data, size_t size)
{
    char *text = ((char*)data) + sizeof(struct qb_ipc_request_header);
    crm_trace("Received %.120s", text);
    return string2xml(text);
}

ssize_t
crm_ipcs_send(qb_ipcs_connection_t *c, xmlNode *message, enum ipcs_send_flags flags)
{
    int rc;
    int lpc = 0;
    struct iovec iov[2];
    static uint32_t id = 0;
    const char *type = "Response";
    struct qb_ipc_response_header header;
    char *buffer = dump_xml_unformatted(message);

    iov[0].iov_len = sizeof(struct qb_ipc_response_header);
    iov[0].iov_base = &header;
    iov[1].iov_len = 1 + strlen(buffer);
    iov[1].iov_base = buffer;

    header.id = id++; /* We don't really use it, but doesn't hurt to set one */
    header.error = 0; /* unused */
    header.size = iov[0].iov_len + iov[1].iov_len;

    do {
        if(flags & ipcs_send_event) {
            rc = qb_ipcs_event_sendv(c, iov, 2);
            type = "Event";
            
        } else {
            rc = qb_ipcs_response_sendv(c, iov, 2);
        }

        if(rc != -EAGAIN) {
            break;
        } else if(lpc > 3 && (flags & ipcs_send_error)) {
            break;
        }

        crm_debug("Attempting resend %d of %s %d (%d bytes) to %p[%d]: %.120s",
                  ++lpc, type, header.id, header.size, c, crm_ipcs_client_pid(c), buffer);
        sleep(1);

        /* Only retry for important stuff, and even then only a limited amount for ipcs_send_error
         * Unless ipcs_send_info or ipcs_send_error is specified, we block by default
         */
    } while((flags & ipcs_send_info) == 0);

    if(rc <= 0) {
        do_crm_log((flags & ipcs_send_error)?LOG_ERR:LOG_INFO,
                   "%s %d failed, to=%p[%d], rc=%d: %.120s",
                   type, header.id, c, crm_ipcs_client_pid(c), rc, buffer);
    } else {
        crm_trace("%s %d sent, %d bytes to %p: %.120s", type, header.id, rc, c, buffer);
    }
    crm_free(buffer);
    return rc;
}

/* Client... */

#define MIN_MSG_SIZE    12336 /* sizeof(struct qb_ipc_connection_response) */
#define MAX_MSG_SIZE    20*1024

typedef struct crm_ipc_s
{
        struct pollfd pfd;
        
        int buf_size;
        int msg_size;
        char *buffer;
        char *name;

        qb_ipcc_connection_t *ipc;
        
} crm_ipc_t;

static int
pick_ipc_buffer(int max)
{
    const char *env = getenv("PCMK_ipc_buffer");

    if(env) {
        max = crm_parse_int(env, "0");
    }

    if(max <= 0) {
        max = MAX_MSG_SIZE;
    }

    if(max < MIN_MSG_SIZE) {
        max = MIN_MSG_SIZE;
    }

    crm_trace("Using max message size of %d", max);
    return max;
}


crm_ipc_t *
crm_ipc_new(const char *name, size_t max_size) 
{
    crm_ipc_t *client = NULL;
    crm_malloc0(client, sizeof(crm_ipc_t));

    client->name = crm_strdup(name);
    client->buf_size = pick_ipc_buffer(max_size);
    client->buffer = malloc(client->buf_size);

    client->pfd.fd = -1;
    client->pfd.events = POLLIN;
    client->pfd.revents = 0;
    
    return client;
}

bool
crm_ipc_connect(crm_ipc_t *client) 
{
    client->ipc = qb_ipcc_connect(client->name, client->buf_size);

    if (client->ipc == NULL) {
        crm_perror(LOG_INFO, "Could not establish %s connection", client->name);
        return FALSE;
    }

    client->pfd.fd = crm_ipc_get_fd(client);
    if(client->pfd.fd < 0) {
        crm_perror(LOG_INFO, "Could not obtain file descriptor for %s connection", client->name);
        return FALSE;
    }

    qb_ipcc_context_set(client->ipc, client);

    return TRUE;
}

void
crm_ipc_close(crm_ipc_t *client) 
{
    crm_trace("Disconnecting %s IPC connection %p", client->name, client);
    if(client->ipc) {
        qb_ipcc_disconnect(client->ipc);
    }
}

void
crm_ipc_destroy(crm_ipc_t *client) 
{
    crm_trace("Destroying %s IPC connection %p", client->name, client);
    free(client->buffer);
    free(client->name);
    free(client);    
}

int
crm_ipc_get_fd(crm_ipc_t *client)
{
    int fd = 0;
    
    CRM_ASSERT(client != NULL);
    if(qb_ipcc_fd_get(client->ipc, &fd) < 0) {
        crm_perror(LOG_ERR, "Could not obtain file IPC descriptor for %s", client->name);
    }
    return fd;
}

bool
crm_ipc_connected(crm_ipc_t *client) 
{
    bool rc = FALSE;

    if(client == NULL) {
        crm_trace("No client");
        return FALSE;

    } else if(client->pfd.fd < 0) {
        crm_trace("Bad descriptor");
        return FALSE;        
    }

    rc = qb_ipcc_is_connected(client->ipc);
    if(rc == FALSE) {
        client->pfd.fd = -1;
    }
    return rc;
}

int
crm_ipc_ready(crm_ipc_t *client) 
{
    CRM_ASSERT(client != NULL);

    if(crm_ipc_connected(client) == FALSE) {
        return -ENOTCONN;
    }

    client->pfd.revents = 0;
    return poll(&(client->pfd), 1, 0);
}

long
crm_ipc_read(crm_ipc_t *client) 
{
    CRM_ASSERT(client != NULL);
    CRM_ASSERT(client->buffer != NULL);
    
    crm_trace("Message recieved on %s IPC connection", client->name);

    client->buffer[0] = 0;
    client->msg_size = qb_ipcc_event_recv(client->ipc, client->buffer, client->buf_size-1, -1);
    if(client->msg_size >= 0) {
        struct qb_ipc_response_header *header = (struct qb_ipc_response_header *)client->buffer;
        crm_trace("Recieved response %d, size=%d, rc=%d", header->id, header->size, client->msg_size);
        client->buffer[client->msg_size] = 0;
    }

    if(crm_ipc_connected(client) == FALSE || client->msg_size == -ENOTCONN) {
        crm_err("Connection to %s failed", client->name);
    }
    
    return client->msg_size;
}

const char *
crm_ipc_buffer(crm_ipc_t *client) 
{
    CRM_ASSERT(client != NULL);    
    return client->buffer + sizeof(struct qb_ipc_response_header);
}

const char *crm_ipc_name(crm_ipc_t *client)
{
    CRM_ASSERT(client != NULL);
    return client->name;
}

int
crm_ipc_send(crm_ipc_t *client, xmlNode *message, xmlNode **reply, int32_t ms_timeout)
{
    size_t rc = 0;
    struct iovec iov[2];
    static uint32_t id = 0;
    struct qb_ipc_request_header header;
    char *buffer = dump_xml_unformatted(message);

    iov[0].iov_len = sizeof(struct qb_ipc_request_header);
    iov[0].iov_base = &header;
    iov[1].iov_len = 1 + strlen(buffer);
    iov[1].iov_base = buffer;

    header.id = id++; /* We don't really use it, but doesn't hurt to set one */
    header.size = iov[0].iov_len + iov[1].iov_len;    
    
    crm_trace("Waiting for reply to %ld bytes: %.120s...", iov[1].iov_base, buffer);
    rc = qb_ipcc_sendv_recv(client->ipc, iov, 2, client->buffer, client->buf_size, ms_timeout);
    crm_trace("rc=%d, errno=%d", rc, errno);

    if(rc > 0 && reply) {
        *reply = string2xml(crm_ipc_buffer(client));
    }

    if(crm_ipc_connected(client) == FALSE) {
        crm_err("Connection to %s closed: %d", client->name, rc);

    } else if(rc <= 0) {
        crm_perror(LOG_ERR, "Request to %s failed: %ld", client->name, rc);
    }

    crm_free(buffer);
    return rc;
}

/* Utils */

xmlNode *
create_hello_message(const char *uuid,
                     const char *client_name, const char *major_version, const char *minor_version)
{
    xmlNode *hello_node = NULL;
    xmlNode *hello = NULL;

    if (uuid == NULL || strlen(uuid) == 0
        || client_name == NULL || strlen(client_name) == 0
        || major_version == NULL || strlen(major_version) == 0
        || minor_version == NULL || strlen(minor_version) == 0) {
        crm_err("Missing fields, Hello message will not be valid.");
        return NULL;
    }

    hello_node = create_xml_node(NULL, XML_TAG_OPTIONS);
    crm_xml_add(hello_node, "major_version", major_version);
    crm_xml_add(hello_node, "minor_version", minor_version);
    crm_xml_add(hello_node, "client_name", client_name);
    crm_xml_add(hello_node, "client_uuid", uuid);

    crm_trace("creating hello message");
    hello = create_request(CRM_OP_HELLO, hello_node, NULL, NULL, client_name, uuid);
    free_xml(hello_node);

    return hello;
}

gboolean
process_hello_message(xmlNode * hello,
                      char **uuid, char **client_name, char **major_version, char **minor_version)
{
    const char *local_uuid;
    const char *local_client_name;
    const char *local_major_version;
    const char *local_minor_version;

    *uuid = NULL;
    *client_name = NULL;
    *major_version = NULL;
    *minor_version = NULL;

    if (hello == NULL) {
        return FALSE;
    }

    local_uuid = crm_element_value(hello, "client_uuid");
    local_client_name = crm_element_value(hello, "client_name");
    local_major_version = crm_element_value(hello, "major_version");
    local_minor_version = crm_element_value(hello, "minor_version");

    if (local_uuid == NULL || strlen(local_uuid) == 0) {
        crm_err("Hello message was not valid (field %s not found)", "uuid");
        return FALSE;

    } else if (local_client_name == NULL || strlen(local_client_name) == 0) {
        crm_err("Hello message was not valid (field %s not found)", "client name");
        return FALSE;

    } else if (local_major_version == NULL || strlen(local_major_version) == 0) {
        crm_err("Hello message was not valid (field %s not found)", "major version");
        return FALSE;

    } else if (local_minor_version == NULL || strlen(local_minor_version) == 0) {
        crm_err("Hello message was not valid (field %s not found)", "minor version");
        return FALSE;
    }

    *uuid = crm_strdup(local_uuid);
    *client_name = crm_strdup(local_client_name);
    *major_version = crm_strdup(local_major_version);
    *minor_version = crm_strdup(local_minor_version);

    crm_trace("Hello message ok");
    return TRUE;
}
