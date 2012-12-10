/*
 * Copyright (c) 2008 Andrew Beekhof
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
#include <crm/crm.h>

#include <sys/param.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/socket.h>

#include <netinet/ip.h>

#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <glib.h>

#include <crm/common/ipc.h>
#include <crm/common/xml.h>

#ifdef HAVE_GNUTLS_GNUTLS_H
#  undef KEYFILE
#  include <gnutls/gnutls.h>
#endif

#ifdef HAVE_GNUTLS_GNUTLS_H

const int psk_tls_kx_order[] = {
    GNUTLS_KX_DHE_PSK,
    GNUTLS_KX_PSK,
};
const int anon_tls_kx_order[] = {
    GNUTLS_KX_ANON_DH,
    GNUTLS_KX_DHE_RSA,
    GNUTLS_KX_DHE_DSS,
    GNUTLS_KX_RSA,
    0
};

gnutls_anon_client_credentials anon_cred_c;
gnutls_anon_server_credentials anon_cred_s;

static int crm_send_tls(gnutls_session * session, const char *msg, size_t len);
static char *crm_recv_tls(gnutls_session * session, size_t *len);
#endif

char *crm_recv_plaintext(int sock, size_t *len);
int crm_send_plaintext(int sock, const char *msg, size_t len);

#ifdef HAVE_GNUTLS_GNUTLS_H
gnutls_session *create_anon_tls_session(int csock, int type);
gnutls_session *create_psk_tls_session(int csock, int type, void *credentials);

gnutls_session *
create_anon_tls_session(int csock, int type /* GNUTLS_SERVER, GNUTLS_CLIENT */ )
{
    int rc = 0;
    gnutls_session *session = gnutls_malloc(sizeof(gnutls_session));

    gnutls_init(session, type);
#  ifdef HAVE_GNUTLS_PRIORITY_SET_DIRECT
/*      http://www.manpagez.com/info/gnutls/gnutls-2.10.4/gnutls_81.php#Echo-Server-with-anonymous-authentication */
    gnutls_priority_set_direct(*session, "NORMAL:+ANON-DH", NULL);
/*	gnutls_priority_set_direct (*session, "NONE:+VERS-TLS-ALL:+CIPHER-ALL:+MAC-ALL:+SIGN-ALL:+COMP-ALL:+ANON-DH", NULL); */
#  else
    gnutls_set_default_priority(*session);
    gnutls_kx_set_priority(*session, anon_tls_kx_order);
#  endif
    gnutls_transport_set_ptr(*session, (gnutls_transport_ptr) GINT_TO_POINTER(csock));
    switch (type) {
        case GNUTLS_SERVER:
            gnutls_credentials_set(*session, GNUTLS_CRD_ANON, anon_cred_s);
            break;
        case GNUTLS_CLIENT:
            gnutls_credentials_set(*session, GNUTLS_CRD_ANON, anon_cred_c);
            break;
    }

    do {
        rc = gnutls_handshake(*session);
    } while (rc == GNUTLS_E_INTERRUPTED || rc == GNUTLS_E_AGAIN);

    if (rc < 0) {
        crm_err("Handshake failed: %s", gnutls_strerror(rc));
        gnutls_deinit(*session);
        gnutls_free(session);
        return NULL;
    }
    return session;
}


gnutls_session *
create_psk_tls_session(int csock, int type /* GNUTLS_SERVER, GNUTLS_CLIENT */, void *credentials)
{
    int rc = 0;
    gnutls_session *session = gnutls_malloc(sizeof(gnutls_session));

    gnutls_init(session, type);
#  ifdef HAVE_GNUTLS_PRIORITY_SET_DIRECT
    gnutls_priority_set_direct(*session, "NORMAL:+DHE-PSK:+PSK", NULL);
#  else
    gnutls_set_default_priority(*session);
    gnutls_kx_set_priority(*session, psk_tls_kx_order);
#  endif
    gnutls_transport_set_ptr(*session, (gnutls_transport_ptr) GINT_TO_POINTER(csock));
    switch (type) {
        case GNUTLS_SERVER:
            gnutls_credentials_set(*session, GNUTLS_CRD_PSK, (gnutls_psk_server_credentials_t) credentials);
            break;
        case GNUTLS_CLIENT:
            gnutls_credentials_set(*session, GNUTLS_CRD_PSK, (gnutls_psk_client_credentials_t) credentials);
            break;
    }

    do {
        rc = gnutls_handshake(*session);
    } while (rc == GNUTLS_E_INTERRUPTED || rc == GNUTLS_E_AGAIN);

    if (rc < 0) {
        crm_err("Handshake failed: %s", gnutls_strerror(rc));
        gnutls_deinit(*session);
        gnutls_free(session);
        return NULL;
    }
    return session;
}


static int
crm_send_tls(gnutls_session * session, const char *buf, size_t len)
{
    const char *unsent = buf;
    int rc = 0;
    int total_send;

    if (buf == NULL) {
        return -1;
    }

    total_send = len;
    crm_trace("Message size: %d", len);

    while (TRUE) {
        rc = gnutls_record_send(*session, unsent, len);
        crm_debug("Sent %d bytes", rc);

        if (rc == GNUTLS_E_INTERRUPTED || rc == GNUTLS_E_AGAIN) {
            crm_debug("Retry");

        } else if (rc < 0) {
            crm_err("Connection terminated");
            break;

        } else if (rc < len) {
            crm_debug("Only sent %d of %d bytes", rc, len);
            len -= rc;
            unsent += rc;
        } else {
            break;
        }
    }

    return rc < 0 ? rc : total_send;
}

static char *
crm_recv_tls(gnutls_session * session, size_t *recv_len)
{
    char *buf = NULL;
    int flag = 0;
    int rc = 0;
    int len = 0;
    int chunk_size = 1024;
    int buf_size;
    int non_blocking = FALSE;
    void *sock_ptr;
    if (session == NULL) {
        return NULL;
    }

    sock_ptr = gnutls_transport_get_ptr(*session);
    buf = calloc(1, chunk_size + 1);
    buf_size = chunk_size;

    flag = fcntl(GPOINTER_TO_INT(sock_ptr), F_GETFL);
    if (flag & O_NONBLOCK) {
        non_blocking = TRUE;
    }

    while (TRUE) {
        errno = 0;

        if ((buf_size - len) < (chunk_size/2)) {
            buf_size += chunk_size;
            crm_trace("Retry with %d more bytes. buf is now %d bytes", (int)chunk_size, buf_size);
            buf = realloc(buf, buf_size + 1);
            CRM_ASSERT(buf != NULL);
        }

        rc = gnutls_record_recv(*session, buf + len, chunk_size);
        crm_trace("Got %d more bytes. errno=%d", rc, errno);

        if (rc > 0) {
            len += rc;
        }

        if (rc == GNUTLS_E_INTERRUPTED) {
            crm_trace("Retry");

        } else if (rc == GNUTLS_E_AGAIN) {
            if (non_blocking) {
                crm_trace("non-blocking, exiting read on rc = %d errno = %d", rc, errno);
                /* We added +1 to alloc so there is always room for null terminator */
                buf[len] = '\0';
                goto success;
            } else {
                crm_trace("Retry");
            }

        } else if (rc == GNUTLS_E_UNEXPECTED_PACKET_LENGTH) {
            crm_trace("Session disconnected");
            goto bail;

        } else if (rc < 0) {
            crm_err("Error receiving message: %s (%d)", gnutls_strerror(rc), rc);
            goto bail;

        } else if (rc == 0) {
            crm_trace("EOF return NULL");
            goto bail;

        } else if (buf[len - 1] != 0) {
            crm_trace("Last char is %d '%c'", buf[len - 1], buf[len - 1]);

        } else {
            crm_trace("Got %d more bytes", (int)rc);
            goto success;
        }
    }

  success:
    if (recv_len) {
        *recv_len = len;
    }
    return buf;
  bail:
    free(buf);
    return NULL;

}
#endif

int
crm_send_plaintext(int sock, const char *buf, size_t len)
{

    int rc = 0;
    const char *unsent = buf;
    int total_send;

    if (buf == NULL) {
        return -1;
    }
    total_send = len;

    crm_trace("Message on socket %d: size=%d", sock, len);
  retry:
    rc = write(sock, unsent, len);
    if (rc < 0) {
        switch (errno) {
        case EINTR:
        case EAGAIN:
            crm_trace("Retry");
            goto retry;
        default:
            crm_perror(LOG_ERR, "Could only write %d of the remaining %d bytes", rc, (int) len);
            break;
        }

    } else if (rc < len) {
        crm_trace("Only sent %d of %d remaining bytes", rc, len);
        len -= rc;
        unsent += rc;
        goto retry;

     } else {
        crm_trace("Sent %d bytes: %.100s", rc, buf);
    }

    return rc < 0 ? rc : total_send;

}

char *
crm_recv_plaintext(int sock, size_t *recv_len)
{
    char *buf = NULL;
    ssize_t rc = 0;
    ssize_t len = 0;
    ssize_t chunk_size = 512;
    int non_blocking = FALSE;
    int flag = 0;

    buf = calloc(1, chunk_size);

    flag = fcntl(sock, F_GETFL);
    if (flag & O_NONBLOCK) {
        non_blocking = TRUE;
    }
    while (1) {
        errno = 0;
        rc = read(sock, buf + len, chunk_size);
        crm_trace("Got %d more bytes. errno=%d", (int)rc, errno);

        if (errno == EINTR) {
            crm_trace("Retry: %d", (int)rc);
            if (rc > 0) {
                len += rc;
                buf = realloc(buf, len + chunk_size);
                CRM_ASSERT(buf != NULL);
            }
        } else if (errno == EAGAIN) {
            if (non_blocking) {
                /* We added +1 to alloc so there is always room for null terminator */
                buf[len] = '\0';
                goto success;
            } else {
                crm_trace("Retry");
            }

        } else if (rc < 0) {
            crm_perror(LOG_ERR, "Error receiving message: %d", (int)rc);
            goto bail;

        } else if (rc == chunk_size) {
            len += rc;
            chunk_size *= 2;
            buf = realloc(buf, len + chunk_size);
            crm_trace("Retry with %d more bytes", (int)chunk_size);
            CRM_ASSERT(buf != NULL);

        } else if (buf[len + rc - 1] != 0) {
            crm_trace("Last char is %d '%c'", buf[len + rc - 1], buf[len + rc - 1]);
            crm_trace("Retry with %d more bytes", (int)chunk_size);
            len += rc;
            buf = realloc(buf, len + chunk_size);
            CRM_ASSERT(buf != NULL);

        } else {
            goto success;
        }
    }

  success:
    if (recv_len) {
        *recv_len = len;
    }
    return buf;

  bail:
    free(buf);
    return NULL;

}

int
crm_send_remote_msg_raw(void *session, const char *buf, size_t len, gboolean encrypted)
{
    int rc = -1;
    if (encrypted) {
#ifdef HAVE_GNUTLS_GNUTLS_H
        rc = crm_send_tls(session, buf, len);
#else
        CRM_ASSERT(encrypted == FALSE);
#endif
    } else {
        rc = crm_send_plaintext(GPOINTER_TO_INT(session), buf, len);
    }
    return rc;
}

int
crm_send_remote_msg(void *session, xmlNode * msg, gboolean encrypted)
{
    int rc = -1;
    char *xml_text = NULL;
    int len = 0;

    xml_text = dump_xml_unformatted(msg);
    if (xml_text) {
        len = strlen(xml_text);
        len++; /* null char */
    } else {
        crm_err("Invalid XML, can not send msg");
        return -1;
    }
    if (encrypted) {
#ifdef HAVE_GNUTLS_GNUTLS_H
        rc = crm_send_tls(session, xml_text, len);
#else
        CRM_ASSERT(encrypted == FALSE);
#endif
    } else {
        rc = crm_send_plaintext(GPOINTER_TO_INT(session), xml_text, len);
    }
    free(xml_text);
    return rc;
}

char *
crm_recv_remote_raw(void *data, gboolean encrypted, size_t *recv_len)
{
    char *reply = NULL;
    if (recv_len) {
        *recv_len = 0;
    }

    if (encrypted) {
#ifdef HAVE_GNUTLS_GNUTLS_H
        reply = crm_recv_tls(data, recv_len);
#else
        CRM_ASSERT(encrypted == FALSE);
#endif
    } else {
        reply = crm_recv_plaintext(GPOINTER_TO_INT(data), recv_len);
    }
    if (reply == NULL || strlen(reply) == 0) {
        crm_trace("Empty reply");
    }

    return reply;
}

xmlNode *
crm_recv_remote_msg(void *session, gboolean encrypted)
{
    char *reply = NULL;
    xmlNode *xml = NULL;

    if (encrypted) {
#ifdef HAVE_GNUTLS_GNUTLS_H
        reply = crm_recv_tls(session, NULL);
#else
        CRM_ASSERT(encrypted == FALSE);
#endif
    } else {
        reply = crm_recv_plaintext(GPOINTER_TO_INT(session), NULL);
    }
    if (reply == NULL || strlen(reply) == 0) {
        crm_trace("Empty reply");

    } else {
        xml = string2xml(reply);
        if (xml == NULL) {
            crm_err("Couldn't parse: '%.120s'", reply);
        }
    }

    free(reply);
    return xml;
}
