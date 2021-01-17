/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 * Copyright (C) 2001, 2002, 2004, 2007, 2008, 2010 by the Massachusetts Institute of Technology.
 * All rights reserved.
 *
 *
 * Export of this software from the United States of America may require
 * a specific license from the United States Government.  It is the
 * responsibility of any person or organization contemplating export to
 * obtain such a license before exporting.
 *
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of M.I.T. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  Furthermore if you modify this software you must label
 * your software as modified software and not distribute it in such a
 * fashion that it might be confused with the original M.I.T. software.
 * M.I.T. makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 */

#include "crypto_int.h"

#if defined(__has_feature)
#if __has_feature(memory_sanitizer)
#include <sanitizer/msan_interface.h>
#endif
#endif

krb5_error_code KRB5_CALLCONV
krb5_c_random_seed(krb5_context context, krb5_data *data)
{
    return krb5_c_random_add_entropy(context, KRB5_C_RANDSOURCE_OLDAPI, data);
}

/* Routines to get entropy from the OS. */
#if defined(_WIN32)

krb5_boolean
k5_get_os_entropy(unsigned char *buf, size_t len, int strong)
{
    krb5_boolean result;
    HCRYPTPROV provider;

    /* CryptGenRandom is always considered strong. */

    if (!CryptAcquireContext(&provider, NULL, NULL, PROV_RSA_FULL,
                             CRYPT_VERIFYCONTEXT))
        return FALSE;
    result = CryptGenRandom(provider, len, buf);
    (void)CryptReleaseContext(provider, 0);
    return result;
}

#else /* not Windows */
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef __linux__
#include <sys/syscall.h>
#endif /* __linux__ */

/* Open device, ensure that it is not a regular file, and read entropy.  Return
 * true on success, false on failure. */
static krb5_boolean
read_entropy_from_device(const char *device, unsigned char *buf, size_t len)
{
    struct stat sb;
    int fd;
    unsigned char *bp;
    size_t left;
    ssize_t count;
    krb5_boolean result = FALSE;

    fd = open(device, O_RDONLY);
    if (fd == -1)
        return FALSE;
    set_cloexec_fd(fd);
    if (fstat(fd, &sb) == -1 || S_ISREG(sb.st_mode))
        goto cleanup;

    for (bp = buf, left = len; left > 0;) {
        count = read(fd, bp, left);
        if (count <= 0)
            goto cleanup;
        left -= count;
        bp += count;
    }
    result = TRUE;

cleanup:
    close(fd);
    return result;
}

krb5_boolean
k5_get_os_entropy(unsigned char *buf, size_t len, int strong)
{
    const char *device;
#if defined(__linux__) && defined(SYS_getrandom)
    int r;

    while (len > 0) {
        /*
         * Pull from the /dev/urandom pool, but require it to have been seeded.
         * This ensures strong randomness while only blocking during first
         * system boot.
         *
         * glibc does not currently provide a binding for getrandom:
         * https://sourceware.org/bugzilla/show_bug.cgi?id=17252
         */
        errno = 0;
        r = syscall(SYS_getrandom, buf, len, 0);
        if (r <= 0) {
            if (errno == EINTR)
                continue;

            /* ENOSYS or other unrecoverable failure */
            break;
        }

#if defined(__has_feature)
#if __has_feature(memory_sanitizer)
        __msan_unpoison(buf, r);
#endif
#endif

        len -= r;
        buf += r;
    }
    if (len == 0)
        return TRUE;
#endif /* defined(__linux__) && defined(SYS_getrandom) */

    device = strong ? "/dev/random" : "/dev/urandom";
    return read_entropy_from_device(device, buf, len);
}

#endif /* not Windows */
