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

#include <AK/StringView.h>
#include <Kernel/Process.h>

namespace Kernel {

KResultOr<int> Process::sys$pledge(Userspace<const Syscall::SC_pledge_params*> user_params)
{
    Syscall::SC_pledge_params params;
    if (!copy_from_user(&params, user_params))
        return EFAULT;

    if (params.promises.length > 1024 || params.execpromises.length > 1024)
        return E2BIG;

    String promises;
    if (params.promises.characters) {
        promises = copy_string_from_user(params.promises);
        if (promises.is_null())
            return EFAULT;
    }

    String execpromises;
    if (params.execpromises.characters) {
        execpromises = copy_string_from_user(params.execpromises);
        if (execpromises.is_null())
            return EFAULT;
    }

    auto parse_pledge = [&](auto& pledge_spec, u32& mask) {
        auto parts = pledge_spec.split_view(' ');
        for (auto& part : parts) {
#define __ENUMERATE_PLEDGE_PROMISE(x)   \
    if (part == #x) {                   \
        mask |= (1u << (u32)Pledge::x); \
        continue;                       \
    }
            ENUMERATE_PLEDGE_PROMISES
#undef __ENUMERATE_PLEDGE_PROMISE
            return false;
        }
        return true;
    };

    MutableProtectedData mutable_protected_data { *this };

    if (!promises.is_null()) {
        u32 new_promises = 0;
        if (!parse_pledge(promises, new_promises))
            return EINVAL;
        if (protected_data().promises && (!new_promises || new_promises & ~protected_data().promises))
            return EPERM;

        mutable_protected_data->has_promises = true;
        mutable_protected_data->promises = new_promises;
    }

    if (!execpromises.is_null()) {
        u32 new_execpromises = 0;
        if (!parse_pledge(execpromises, new_execpromises))
            return EINVAL;
        if (protected_data().execpromises && (!new_execpromises || new_execpromises & ~protected_data().execpromises))
            return EPERM;
        mutable_protected_data->has_execpromises = true;
        mutable_protected_data->execpromises = new_execpromises;
    }

    return 0;
}

}
