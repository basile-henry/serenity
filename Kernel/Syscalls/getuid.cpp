/*
 * Copyright (c) 2018-2020, Andreas Kling <kling@serenityos.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <Kernel/Process.h>

namespace Kernel {

KResultOr<uid_t> Process::sys$getuid()
{
    REQUIRE_PROMISE(stdio);
    return uid();
}

KResultOr<gid_t> Process::sys$getgid()
{
    REQUIRE_PROMISE(stdio);
    return gid();
}

KResultOr<uid_t> Process::sys$geteuid()
{
    REQUIRE_PROMISE(stdio);
    return euid();
}

KResultOr<gid_t> Process::sys$getegid()
{
    REQUIRE_PROMISE(stdio);
    return egid();
}

KResultOr<int> Process::sys$getresuid(Userspace<uid_t*> ruid, Userspace<uid_t*> euid, Userspace<uid_t*> suid)
{
    REQUIRE_PROMISE(stdio);
    if (!copy_to_user(ruid, &protected_data().uid) || !copy_to_user(euid, &protected_data().euid) || !copy_to_user(suid, &protected_data().suid))
        return EFAULT;
    return 0;
}

KResultOr<int> Process::sys$getresgid(Userspace<gid_t*> rgid, Userspace<gid_t*> egid, Userspace<gid_t*> sgid)
{
    REQUIRE_PROMISE(stdio);
    if (!copy_to_user(rgid, &protected_data().gid) || !copy_to_user(egid, &protected_data().egid) || !copy_to_user(sgid, &protected_data().sgid))
        return EFAULT;
    return 0;
}

KResultOr<int> Process::sys$getgroups(ssize_t count, Userspace<gid_t*> user_gids)
{
    REQUIRE_PROMISE(stdio);
    if (count < 0)
        return EINVAL;
    if (!count)
        return extra_gids().size();
    if (count != (int)extra_gids().size())
        return EINVAL;

    if (!copy_to_user(user_gids, extra_gids().data(), sizeof(gid_t) * count))
        return EFAULT;

    return 0;
}

}
