/*
 * Copyright (C) 1994-2017 Altair Engineering, Inc.
 * For more information, contact Altair at www.altair.com.
 *  
 * This file is part of the PBS Professional ("PBS Pro") software.
 * 
 * Open Source License Information:
 *  
 * PBS Pro is free software. You can redistribute it and/or modify it under the
 * terms of the GNU Affero General Public License as published by the Free 
 * Software Foundation, either version 3 of the License, or (at your option) any 
 * later version.
 *  
 * PBS Pro is distributed in the hope that it will be useful, but WITHOUT ANY 
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE.  See the GNU Affero General Public License for more details.
 *  
 * You should have received a copy of the GNU Affero General Public License along 
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *  
 * Commercial License Information: 
 * 
 * The PBS Pro software is licensed under the terms of the GNU Affero General 
 * Public License agreement ("AGPL"), except where a separate commercial license 
 * agreement for PBS Pro version 14 or later has been executed in writing with Altair.
 *  
 * Altair’s dual-license business model allows companies, individuals, and 
 * organizations to create proprietary derivative works of PBS Pro and distribute 
 * them - whether embedded or bundled with other software - under a commercial 
 * license agreement.
 * 
 * Use of Altair’s trademarks, including but not limited to "PBS™", 
 * "PBS Professional®", and "PBS Pro™" and Altair’s logos is subject to Altair's 
 * trademark licensing policies.
 *
 */
/**
 * @file	int_rdrpy.c
 * @brief
 * Read the reply to a batch request.
 * A reply structure is allocated and cleared.
 * The reply is read and decoded into the structure.
 * The reply structure is returned.
 *
 * The caller MUST free the reply structure by calling
 * PBS_FreeReply().
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "libpbs.h"
#include "dis.h"


/**
 * @brief read a batch reply from the given socket
 *
 * @param[in] sock - The socket fd to read from
 * @param[out] rc  - Return DIS error code
 *
 * @return Batch reply structure
 * @retval  !NULL - Success
 * @retval   NULL - Failure
 *
 */
struct batch_reply *
PBSD_rdrpy_sock(int sock, int *rc)
{
	struct batch_reply *reply;
	time_t old_timeout;

	*rc = DIS_SUCCESS;
	/* clear any prior error message */
	if ((reply = (struct batch_reply *)malloc(sizeof(struct batch_reply))) == 0) {
		pbs_errno = PBSE_SYSTEM;
		return ((struct batch_reply *)0);
	}
	(void)memset(reply, 0, sizeof(struct batch_reply));

	DIS_tcp_setup(sock);
	old_timeout = pbs_tcp_timeout;
	if (pbs_tcp_timeout < PBS_DIS_TCP_TIMEOUT_LONG)
		pbs_tcp_timeout = PBS_DIS_TCP_TIMEOUT_LONG;

	if ((*rc = decode_DIS_replyCmd(sock, reply)) != 0) {
		(void)free(reply);
		pbs_errno = PBSE_PROTOCOL;
		return (struct batch_reply *)NULL;
	}
	DIS_tcp_reset(sock, 0);		/* reset DIS read buffer */
	pbs_tcp_timeout = old_timeout;

	pbs_errno = reply->brp_code;
	return reply;
}

/**
 * @brief read a batch reply from the given connecction index
 *
 * @param[in] c - The connection index to read from
 *
 * @return DIS error code
 * @retval   DIS_SUCCESS  - Success
 * @retval  !DIS_SUCCESS  - Failure
 */
struct batch_reply *
PBSD_rdrpy(int c)
{
	int rc;
	struct batch_reply *reply;
	int sock;

	/* clear any prior error message */

	if (connection[c].ch_errtxt != (char *)NULL) {
		free(connection[c].ch_errtxt);
		connection[c].ch_errtxt = (char *)NULL;
	}

	sock = connection[c].ch_socket;
	reply = PBSD_rdrpy_sock(sock, &rc);
	if (reply == NULL) {
		connection[c].ch_errno = PBSE_PROTOCOL;
		connection[c].ch_errtxt = strdup(dis_emsg[rc]);
		if (connection[c].ch_errtxt == NULL)
			pbs_errno = PBSE_SYSTEM;
		return (struct batch_reply *) NULL;
	}

	connection[c].ch_errno = reply->brp_code;
	pbs_errno = reply->brp_code;

	if (reply->brp_choice == BATCH_REPLY_CHOICE_Text) {
		if (reply->brp_un.brp_txt.brp_str != NULL) {

			/*No memory leak, see beginning of function*/
			connection[c].ch_errtxt = strdup(reply->brp_un.brp_txt.brp_str);
			if (connection[c].ch_errtxt == NULL) {
				pbs_errno = PBSE_SYSTEM;
				return (struct batch_reply *)NULL;
			}
		}
	}
	return reply;
}

/*
 * PBS_FreeReply - Free a batch_reply structure allocated in PBS_rdrpy()
 *
 *	Any additional allocated substructures pointed to from the
 *	reply structure are freed, then the base struture itself is gone.
 */

void
PBSD_FreeReply(reply)
struct batch_reply *reply;
{
	struct brp_select   *psel;
	struct brp_select   *pselx;
	struct brp_cmdstat  *pstc;
	struct brp_cmdstat  *pstcx;
	struct attrl        *pattrl;
	struct attrl	    *pattrx;

	if (reply == 0)
		return;
	if (reply->brp_choice == BATCH_REPLY_CHOICE_Text) {
		if (reply->brp_un.brp_txt.brp_str) {
			(void)free(reply->brp_un.brp_txt.brp_str);
			reply->brp_un.brp_txt.brp_str = (char *)0;
			reply->brp_un.brp_txt.brp_txtlen = 0;
		}

	} else if (reply->brp_choice == BATCH_REPLY_CHOICE_Select) {
		psel = reply->brp_un.brp_select;
		while (psel) {
			pselx = psel->brp_next;
			(void)free(psel);
			psel = pselx;
		}

	} else if (reply->brp_choice == BATCH_REPLY_CHOICE_Status) {
		pstc = reply->brp_un.brp_statc;
		while (pstc) {
			pstcx = pstc->brp_stlink;
			pattrl = pstc->brp_attrl;
			while (pattrl) {
				pattrx = pattrl->next;
				if (pattrl->name)
					(void)free(pattrl->name);
				if (pattrl->resource)
					(void)free(pattrl->resource);
				if (pattrl->value)
					(void)free(pattrl->value);
				(void)free(pattrl);
				pattrl = pattrx;
			}
			(void)free(pstc);
			pstc = pstcx;
		}
	} else if (reply->brp_choice == BATCH_REPLY_CHOICE_RescQuery) {
		(void)free(reply->brp_un.brp_rescq.brq_avail);
		(void)free(reply->brp_un.brp_rescq.brq_alloc);
		(void)free(reply->brp_un.brp_rescq.brq_resvd);
		(void)free(reply->brp_un.brp_rescq.brq_down);
	}

	(void)free(reply);
}
