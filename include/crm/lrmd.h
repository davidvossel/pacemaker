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

#define F_LRMD_OPERATION		"lrmd_op"
#define F_LRMD_CLIENTNAME		"lrmd_clientname"
#define F_LRMD_CLIENTID		"lrmd_clientid"

#define T_LRMD		"lrmd"

//#define _TEST // TODO remove all references to this define.
enum lrmd_errors {
    lrmd_ok				=  0,
    lrmd_pending			= -1,
    lrmd_err_generic			= -2,
    lrmd_err_internal			= -3,
    lrmd_err_not_supported		= -4,
    lrmd_err_connection			= -5,
    lrmd_err_missing			= -6,
    lrmd_err_exists			= -7,
    lrmd_err_timeout			= -8,
    lrmd_err_ipc				= -9,
/*
    lrmd_err_peer				= -10,
    lrmd_err_unknown_operation		= -11,
    lrmd_err_unknown_device		= -12,
    lrmd_err_unknown_port			= -13,
    lrmd_err_none_available		= -14,
    lrmd_err_authentication		= -15,
    lrmd_err_signal			= -16,
    lrmd_err_agent_fork			= -17,
    lrmd_err_agent_args			= -18,
    lrmd_err_agent			= -19,
    lrmd_err_invalid_target		= -20,
    lrmd_err_invalid_level		= -21,
*/
};

extern lrmd_t *lrmd_api_new(void);
extern void lrmd_api_delete(lrmd_t * lrmd);

typedef struct lrmd_api_operations_s
{
	int (*connect) (lrmd_t *lrmd, const char *name, int *lrmd_fd);
	int (*disconnect)(lrmd_t *lrmd);
} lrmd_api_operations_t;

struct lrmd_s {
	lrmd_api_operations_t *cmds;
	void *private;
};

#endif
