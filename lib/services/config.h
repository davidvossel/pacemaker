#ifndef MH_CONFIG_H
#define MH_CONFIG_H

#define HAVE_STRING_H 1
#define HAVE_SYS_IOCTL_H 1
#define HAVE_ASPRINTF 1
#define HAVE_RESOLV_H 1
#define HAVE_TIME 1
#define HAVE_G_LIST_FREE_FULL 1
#define HAVE_PK_GET_SYNC 1
#define HAVE_AUGEAS 1

#define LOCAL_STATE_DIR "/var"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#endif
