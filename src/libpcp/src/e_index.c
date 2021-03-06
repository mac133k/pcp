/*
 * Copyright (c) 2012-2017,2020-2021 Red Hat.
 * Copyright (c) 1995-2002,2004 Silicon Graphics, Inc.  All Rights Reserved.
 * Copyright (c) 2021, Ken McDonell.  All Rights Reserved.
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * Thread-safe notes:
 *
 * __pmLogReads is a diagnostic counter that is maintained with
 * non-atomic updates ... we've decided that it is acceptable for the
 * value to be subject to possible (but unlikely) missed updates
 */

#include "pmapi.h"
#include "libpcp.h"
#include "internal.h"
#include <sys/stat.h>
#include <assert.h>

/*
 * On-Disk Temporal Index Record, Version 3
 */
typedef struct {
    __int32_t	sec[2];		/* __pmTimestamp */
    __int32_t	nsec;
    __int32_t	vol;
    __int32_t	off_meta[2];
    __int32_t	off_data[2];
} __pmTI_v3;

/*
 * On-Disk Temporal Index Record, Version 2
 */
typedef struct {
    __int32_t	sec;		/* pmTimeval */
    __int32_t	usec;
    __int32_t	vol;
    __int32_t	off_meta;
    __int32_t	off_data;
} __pmTI_v2;

/*
 * This condition (off_data == 0) been seen in QA where pmlogger churns
 * quickly ... trying to understand why using this diagnostic.
 */
static void
__pmLogIndexZeroTILogDiagnostic(const __pmArchCtl *acp)
{
    struct stat	sbuf;
    int		sts;

    fprintf(stderr, "%s: Botch: log offset == 0\n", "__pmLogPutIndex");
    fprintf(stderr, "  __pmFileno=%d __pmFtell -> %lld\n",
		    __pmFileno(acp->ac_mfp), (long long)__pmFtell(acp->ac_mfp));
    if ((sts = __pmFstat(acp->ac_mfp, &sbuf)) < 0)
	fprintf(stderr, "  __pmFstat failed -> %d\n", sts);
    else
	fprintf(stderr, "  __pmFstat st_size=%lld st_ino=%lld\n",
			(long long)sbuf.st_size, (long long)sbuf.st_ino);
}

/* Emit a Log Version 3 Temporal Index entry */
static int
__pmLogPutIndex_v3(const __pmArchCtl *acp, const __pmTimestamp * const ts)
{
    __pmLogCtl		*lcp = acp->ac_log;
    size_t		bytes;
    __pmTI_v3		ti;
    __pmTimestamp	stamp;
    __pmoff64_t		off_meta;
    __pmoff64_t		off_data;

    stamp.sec = ts->sec;
    stamp.nsec = ts->nsec;
    ti.vol = acp->ac_curvol;
    off_meta = (__pmoff64_t)__pmFtell(lcp->l_mdfp);
    memcpy((void *)&ti.off_meta[0], (void *)&off_meta, 2*sizeof(__int32_t));
    off_data = (__pmoff64_t)__pmFtell(acp->ac_mfp);
    if (off_data == 0)
	__pmLogIndexZeroTILogDiagnostic(acp);
    memcpy((void *)&ti.off_data[0], (void *)&off_meta, 2*sizeof(__int32_t));

    if (pmDebugOptions.log) {
	fprintf(stderr, "%s: "
		"timestamp=%" FMT_INT64 ".09%d vol=%d meta posn=%" FMT_INT64 " log posn=%" FMT_INT64 "\n",
	    "__pmLogPutIndex",
	    stamp.sec, stamp.nsec, ti.vol, off_meta, off_data);
    }

    __htonpmTimestamp(&stamp);
    memcpy((void *)&ti.sec[0], (void *)&stamp.sec, 2*sizeof(__int32_t));
    ti.nsec = htonl(stamp.nsec);
    ti.vol = htonl(ti.vol);
    __htonll((char *)&ti.off_meta[0]);
    __htonll((char *)&ti.off_data[0]);

    bytes = __pmFwrite(&ti, 1, sizeof(ti), lcp->l_tifp);
    if (bytes != sizeof(ti)) {
	char	errmsg[PM_MAXERRMSGLEN];
	pmNotifyErr(LOG_ERR, "%s: PCP archive temporal index write failed - "
			"got %zu expecting %zu: %s\n",
			"__pmLogPutIndex", bytes, sizeof(ti),
			osstrerror_r(errmsg, sizeof(errmsg)));
	return -errno;
    }
    if (__pmFflush(lcp->l_tifp) != 0) {
	pmNotifyErr(LOG_ERR, "%s: PCP archive temporal index flush failed\n",
			"__pmLogPutIndex");
	return -errno;
    }
    return 0;
}

/* Emit a Log Version 2 Temporal Index entry */
static int
__pmLogPutIndex_v2(const __pmArchCtl *acp, const __pmTimestamp *ts)
{
    __pmLogCtl		*lcp = acp->ac_log;
    size_t		bytes;
    __pmTI_v2		ti;
    __pmoff64_t		off_meta;
    __pmoff64_t		off_data;

    ti.sec = (__uint32_t)ts->sec;
    ti.usec = (__uint32_t)ts->nsec / 1000;
    ti.vol = acp->ac_curvol;

    if (sizeof(off_t) > sizeof(__pmoff32_t)) {
	/* check for overflow of the offset ... */
	off_t	tmp;

	tmp = __pmFtell(lcp->l_mdfp);
	assert(tmp >= 0);
	off_meta = (__pmoff32_t)tmp;
	if (tmp != off_meta) {
	    pmNotifyErr(LOG_ERR, "%s: PCP archive file (%s) too big\n",
			"__pmLogPutIndex", "meta");
	    return -E2BIG;
	}
	tmp = __pmFtell(acp->ac_mfp);
	assert(tmp >= 0);
	off_data = (__pmoff32_t)tmp;
	if (tmp != off_data) {
	    pmNotifyErr(LOG_ERR, "%s: PCP archive file (%s) too big\n",
			"__pmLogPutIndex", "data");
	    return -E2BIG;
	}
    }
    else {
	off_meta = (__pmoff32_t)__pmFtell(lcp->l_mdfp);
	off_data = (__pmoff32_t)__pmFtell(acp->ac_mfp);
    }

    if (off_data == 0)
	__pmLogIndexZeroTILogDiagnostic(acp);

    if (pmDebugOptions.log) {
	fprintf(stderr, "%s: timestamp=%d.06%d vol=%d meta posn=%" FMT_INT64 " log posn=%" FMT_INT64 "\n",
	    "__pmLogPutIndex",
	    ti.sec, ti.usec, ti.vol, off_meta, off_data);
    }

    ti.sec = htonl(ti.sec);
    ti.usec = htonl(ti.usec);
    ti.vol = htonl(ti.vol);
    ti.off_meta = htonl((__int32_t)off_meta);
    ti.off_data = htonl((__int32_t)off_data);

    bytes = __pmFwrite(&ti, 1, sizeof(ti), lcp->l_tifp);
    if (bytes != sizeof(ti)) {
	char	errmsg[PM_MAXERRMSGLEN];
	pmNotifyErr(LOG_ERR, "%s: PCP archive temporal index write failed - "
			"got %zu expecting %zu: %s\n",
			"__pmLogPutIndex", bytes, sizeof(ti),
			osstrerror_r(errmsg, sizeof(errmsg)));
	return -errno;
    }
    if (__pmFflush(lcp->l_tifp) != 0) {
	pmNotifyErr(LOG_ERR, "%s: PCP archive temporal index flush failed\n",
			"__pmLogPutIndex");
	return -errno;
    }
    return 0;
}

int
__pmLogPutIndex(const __pmArchCtl *acp, const __pmTimestamp *ts)
{
    struct timespec	tmp;
    __pmTimestamp	stamp;
    __pmLogCtl		*lcp = acp->ac_log;

    if (lcp->l_tifp == NULL || lcp->l_mdfp == NULL || acp->ac_mfp == NULL) {
	/*
	 * archive not really created (failed in __pmLogCreate) ...
	 * nothing to be done
	 */
	return 0;
    }

    __pmFflush(lcp->l_mdfp);
    __pmFflush(acp->ac_mfp);

    if (ts == NULL) {
	pmtimespecNow(&tmp);
	stamp.sec = tmp.tv_sec;
	stamp.nsec = tmp.tv_nsec;
	ts = &stamp;
    }

    if (__pmLogVersion(lcp) >= PM_LOG_VERS03)
	return __pmLogPutIndex_v3(acp, ts);
    else
	return __pmLogPutIndex_v2(acp, ts);
}

int
__pmLogLoadIndex(__pmLogCtl *lcp)
{
    int		sts = 0;
    __pmFILE	*f = lcp->l_tifp;
    size_t	record_size;
    size_t	bytes;
    void	*buffer;
    __pmLogTI	*tip;

    lcp->l_numti = 0;
    lcp->l_ti = NULL;

    if (__pmLogVersion(lcp) >= PM_LOG_VERS03)
	record_size = sizeof(__pmTI_v3);
    else
	record_size = sizeof(__pmTI_v2);

    if ((buffer = (void *)malloc(record_size)) == NULL) {
	pmNoMem("__pmLogLoadIndex: buffer", record_size, PM_RECOV_ERR);
	return -oserror();
    }

    if (lcp->l_tifp != NULL) {
	__pmFseek(f, (long)lcp->l_label.total_len + 2 * sizeof(int), SEEK_SET);
	for ( ; ; ) {
	    __pmLogTI	*tmp;
	    bytes = (1 + lcp->l_numti) * sizeof(__pmLogTI);
	    tmp = (__pmLogTI *)realloc(lcp->l_ti, bytes);
	    if (tmp == NULL) {
		pmNoMem("__pmLogLoadIndex: realloc TI", bytes, PM_FATAL_ERR);
		sts = -oserror();
		goto bad;
	    }
	    lcp->l_ti = tmp;
	    bytes = __pmFread(buffer, 1, record_size, f);
	    if (bytes != record_size) {
		if (__pmFeof(f)) {
		    __pmClearerr(f);
		    sts = 0; 
		    break;
		}
	  	if (pmDebugOptions.log)
	    	    fprintf(stderr, "%s: bad TI entry len=%zu: expected %zu\n",
			    "__pmLogLoadIndex", bytes, record_size);
		if (__pmFerror(f)) {
		    __pmClearerr(f);
		    sts = -oserror();
		    goto bad;
		}
		else {
		    sts = PM_ERR_LOGREC;
		    goto bad;
		}
	    }
	    tip = &lcp->l_ti[lcp->l_numti];
	    /*
	     * swab and copy fields
	     */
	    if (__pmLogVersion(lcp) >= PM_LOG_VERS03) {
		__pmTI_v3	*tip_v3 = (__pmTI_v3 *)buffer;
		__htonll((char *)&tip_v3->sec);
		tip_v3->nsec = ntohl(tip_v3->nsec);
		tip_v3->vol = ntohl(tip_v3->vol);
		__htonll((char *)&tip_v3->off_meta[0]);
		__htonll((char *)&tip_v3->off_data[0]);
		/* sizes are not the same, so copy field-by-field */
		memcpy((void *)&tip->stamp.sec, (void *)&tip_v3->sec[0], sizeof(__int32_t));
		tip->stamp.nsec = tip_v3->nsec;
		tip->vol = tip_v3->vol;
		memcpy((void *)&tip->off_meta, (void *)&tip_v3->off_meta[0], 2*sizeof(__int32_t));
		memcpy((void *)&tip->off_data, (void *)&tip_v3->off_data[0], 2*sizeof(__int32_t));
	    }
	    else {
		__pmTI_v2	*tip_v2 = (__pmTI_v2 *)buffer;
		tip_v2->sec = ntohl(tip_v2->sec);
		tip_v2->usec = ntohl(tip_v2->usec);
		tip_v2->vol = ntohl(tip_v2->vol);
		tip_v2->off_meta = ntohl(tip_v2->off_meta);
		tip_v2->off_data = ntohl(tip_v2->off_data);
		tip->stamp.sec = tip_v2->sec;
		tip->stamp.nsec = tip_v2->usec * 1000;
		tip->vol = tip_v2->vol;
		tip->off_meta = tip_v2->off_meta;
		tip->off_data = tip_v2->off_data;
	    }

	    lcp->l_numti++;
	}
    }
    free(buffer);
    return sts;

bad:
    if (lcp->l_ti != NULL) {
	free(lcp->l_ti);
	lcp->l_ti = NULL;
    }
    lcp->l_numti = 0;
    free(buffer);
    return sts;
}
