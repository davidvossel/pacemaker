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

#include <crm_internal.h>

#include <glib.h>
#include <unistd.h>

#include <crm/crm.h>
#include <crm/services.h>

#include <crm/lrmd.h>

lrmd_t *lrmd_conn = NULL;

int main(int argc, char ** argv)
{
	int rc = 0;
	int fd = 0;

	crm_log_init("lrmd_ctest", LOG_INFO, TRUE, FALSE, argc, argv);
	lrmd_conn = lrmd_api_new();
	rc = lrmd_conn->cmds->connect(lrmd_conn, "lrmd_ctest", &fd);

	if (!rc) {
		printf("lrmd client connection established\n");
	} else {
		printf("lrmd client connection failed\n");
		return -1;
	}

	lrmd_api_delete(lrmd_conn);
	return 0;
}
