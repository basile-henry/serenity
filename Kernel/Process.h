/*
 * Copyright (c) 2018-2021, Andreas Kling <kling@serenityos.org>
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

#pragma once

#include <AK/Checked.h>
#include <AK/HashMap.h>
#include <AK/InlineLinkedList.h>
#include <AK/NonnullOwnPtrVector.h>
#include <AK/NonnullRefPtrVector.h>
#include <AK/String.h>
#include <AK/Userspace.h>
#include <AK/WeakPtr.h>
#include <AK/Weakable.h>
#include <Kernel/API/Syscall.h>
#include <Kernel/FileSystem/InodeMetadata.h>
#include <Kernel/Forward.h>
#include <Kernel/FutexQueue.h>
#include <Kernel/Lock.h>
#include <Kernel/ProcessGroup.h>
#include <Kernel/StdLib.h>
#include <Kernel/Thread.h>
#include <Kernel/ThreadTracer.h>
#include <Kernel/UnixTypes.h>
#include <Kernel/UnveilNode.h>
#include <Kernel/VM/AllocationStrategy.h>
#include <Kernel/VM/RangeAllocator.h>
#include <Kernel/VM/Space.h>
#include <LibC/signal_numbers.h>
#include <LibELF/exec_elf.h>

namespace Kernel {

Time kgettimeofday();

#define ENUMERATE_PLEDGE_PROMISES         \
    __ENUMERATE_PLEDGE_PROMISE(stdio)     \
    __ENUMERATE_PLEDGE_PROMISE(rpath)     \
    __ENUMERATE_PLEDGE_PROMISE(wpath)     \
    __ENUMERATE_PLEDGE_PROMISE(cpath)     \
    __ENUMERATE_PLEDGE_PROMISE(dpath)     \
    __ENUMERATE_PLEDGE_PROMISE(inet)      \
    __ENUMERATE_PLEDGE_PROMISE(id)        \
    __ENUMERATE_PLEDGE_PROMISE(proc)      \
    __ENUMERATE_PLEDGE_PROMISE(ptrace)    \
    __ENUMERATE_PLEDGE_PROMISE(exec)      \
    __ENUMERATE_PLEDGE_PROMISE(unix)      \
    __ENUMERATE_PLEDGE_PROMISE(recvfd)    \
    __ENUMERATE_PLEDGE_PROMISE(sendfd)    \
    __ENUMERATE_PLEDGE_PROMISE(fattr)     \
    __ENUMERATE_PLEDGE_PROMISE(tty)       \
    __ENUMERATE_PLEDGE_PROMISE(chown)     \
    __ENUMERATE_PLEDGE_PROMISE(chroot)    \
    __ENUMERATE_PLEDGE_PROMISE(thread)    \
    __ENUMERATE_PLEDGE_PROMISE(video)     \
    __ENUMERATE_PLEDGE_PROMISE(accept)    \
    __ENUMERATE_PLEDGE_PROMISE(settime)   \
    __ENUMERATE_PLEDGE_PROMISE(sigaction) \
    __ENUMERATE_PLEDGE_PROMISE(setkeymap) \
    __ENUMERATE_PLEDGE_PROMISE(prot_exec) \
    __ENUMERATE_PLEDGE_PROMISE(map_fixed) \
    __ENUMERATE_PLEDGE_PROMISE(getkeymap)

enum class Pledge : u32 {
#define __ENUMERATE_PLEDGE_PROMISE(x) x,
    ENUMERATE_PLEDGE_PROMISES
#undef __ENUMERATE_PLEDGE_PROMISE
};

enum class VeilState {
    None,
    Dropped,
    Locked,
};

typedef HashMap<FlatPtr, RefPtr<FutexQueue>> FutexQueues;

struct LoadResult;

class Process
    : public RefCounted<Process>
    , public InlineLinkedListNode<Process>
    , public Weakable<Process> {

    AK_MAKE_NONCOPYABLE(Process);
    AK_MAKE_NONMOVABLE(Process);

    friend class InlineLinkedListNode<Process>;
    friend class Thread;
    friend class CoreDump;

    struct ProtectedData {
        ProcessID pid { 0 };
        ProcessID ppid { 0 };
        SessionID sid { 0 };
        uid_t euid { 0 };
        gid_t egid { 0 };
        uid_t uid { 0 };
        gid_t gid { 0 };
        uid_t suid { 0 };
        gid_t sgid { 0 };
        Vector<gid_t> extra_gids;
        bool dumpable { false };
        bool has_promises { false };
        u32 promises { 0 };
        bool has_execpromises { false };
        u32 execpromises { 0 };
    };

    // Helper class to temporarily unprotect a process's protected data so you can write to it.
    class MutableProtectedData {
    public:
        explicit MutableProtectedData(Process& process)
            : m_process(process)
        {
            m_process.unprotect_data();
        }

        ~MutableProtectedData() { m_process.protect_data(); }
        ProtectedData* operator->() { return &const_cast<ProtectedData&>(m_process.protected_data()); }

    private:
        Process& m_process;
    };

public:
    inline static Process* current()
    {
        auto current_thread = Processor::current_thread();
        return current_thread ? &current_thread->process() : nullptr;
    }

    template<typename EntryFunction>
    static RefPtr<Process> create_kernel_process(RefPtr<Thread>& first_thread, String&& name, EntryFunction entry, u32 affinity = THREAD_AFFINITY_DEFAULT)
    {
        auto* entry_func = new EntryFunction(move(entry));
        return create_kernel_process(
            first_thread, move(name), [](void* data) {
                EntryFunction* func = reinterpret_cast<EntryFunction*>(data);
                (*func)();
                delete func;
            },
            entry_func, affinity);
    }

    static RefPtr<Process> create_kernel_process(RefPtr<Thread>& first_thread, String&& name, void (*entry)(void*), void* entry_data = nullptr, u32 affinity = THREAD_AFFINITY_DEFAULT);
    static RefPtr<Process> create_user_process(RefPtr<Thread>& first_thread, const String& path, uid_t, gid_t, ProcessID ppid, int& error, Vector<String>&& arguments = Vector<String>(), Vector<String>&& environment = Vector<String>(), TTY* = nullptr);
    ~Process();

    static Vector<ProcessID> all_pids();
    static NonnullRefPtrVector<Process> all_processes();

    template<typename EntryFunction>
    RefPtr<Thread> create_kernel_thread(EntryFunction entry, u32 priority, const String& name, u32 affinity = THREAD_AFFINITY_DEFAULT, bool joinable = true)
    {
        auto* entry_func = new EntryFunction(move(entry));
        return create_kernel_thread([](void* data) {
            EntryFunction* func = reinterpret_cast<EntryFunction*>(data);
            (*func)();
            delete func;
        },
            priority, name, affinity, joinable);
    }
    RefPtr<Thread> create_kernel_thread(void (*entry)(void*), void* entry_data, u32 priority, const String& name, u32 affinity = THREAD_AFFINITY_DEFAULT, bool joinable = true);

    bool is_profiling() const { return m_profiling; }
    void set_profiling(bool profiling) { m_profiling = profiling; }
    bool should_core_dump() const { return m_should_dump_core; }
    void set_dump_core(bool dump_core) { m_should_dump_core = dump_core; }

    bool is_dead() const { return m_dead; }

    bool is_stopped() const { return m_is_stopped; }
    bool set_stopped(bool stopped) { return m_is_stopped.exchange(stopped); }

    bool is_kernel_process() const { return m_is_kernel_process; }
    bool is_user_process() const { return !m_is_kernel_process; }

    static RefPtr<Process> from_pid(ProcessID);
    static SessionID get_sid_from_pgid(ProcessGroupID pgid);

    const String& name() const { return m_name; }
    ProcessID pid() const { return protected_data().pid; }
    SessionID sid() const { return protected_data().sid; }
    bool is_session_leader() const { return protected_data().sid.value() == protected_data().pid.value(); }
    ProcessGroupID pgid() const { return m_pg ? m_pg->pgid() : 0; }
    bool is_group_leader() const { return pgid().value() == protected_data().pid.value(); }
    const Vector<gid_t>& extra_gids() const { return protected_data().extra_gids; }
    uid_t euid() const { return protected_data().euid; }
    gid_t egid() const { return protected_data().egid; }
    uid_t uid() const { return protected_data().uid; }
    gid_t gid() const { return protected_data().gid; }
    uid_t suid() const { return protected_data().suid; }
    gid_t sgid() const { return protected_data().sgid; }
    ProcessID ppid() const { return protected_data().ppid; }

    bool is_dumpable() const { return protected_data().dumpable; }
    void set_dumpable(bool);

    mode_t umask() const { return m_umask; }

    bool in_group(gid_t) const;

    RefPtr<FileDescription> file_description(int fd) const;
    int fd_flags(int fd) const;

    template<typename Callback>
    static void for_each(Callback);
    template<typename Callback>
    static void for_each_in_pgrp(ProcessGroupID, Callback);
    template<typename Callback>
    void for_each_child(Callback);
    template<typename Callback>
    IterationDecision for_each_thread(Callback) const;

    void die();
    void finalize();

    ThreadTracer* tracer() { return m_tracer.ptr(); }
    bool is_traced() const { return !!m_tracer; }
    void start_tracing_from(ProcessID tracer);
    void stop_tracing();
    void tracer_trap(Thread&, const RegisterState&);

    KResultOr<int> sys$emuctl();
    KResultOr<int> sys$yield();
    KResultOr<int> sys$sync();
    KResultOr<int> sys$beep();
    KResultOr<int> sys$get_process_name(Userspace<char*> buffer, size_t buffer_size);
    KResultOr<int> sys$set_process_name(Userspace<const char*> user_name, size_t user_name_length);
    KResultOr<int> sys$watch_file(Userspace<const char*> path, size_t path_length);
    KResultOr<int> sys$dbgputch(u8);
    KResultOr<int> sys$dbgputstr(Userspace<const u8*>, int length);
    KResultOr<int> sys$dump_backtrace();
    KResultOr<pid_t> sys$gettid();
    KResultOr<int> sys$donate(pid_t tid);
    KResultOr<int> sys$ftruncate(int fd, off_t);
    KResultOr<pid_t> sys$setsid();
    KResultOr<pid_t> sys$getsid(pid_t);
    KResultOr<int> sys$setpgid(pid_t pid, pid_t pgid);
    KResultOr<pid_t> sys$getpgrp();
    KResultOr<pid_t> sys$getpgid(pid_t);
    KResultOr<uid_t> sys$getuid();
    KResultOr<gid_t> sys$getgid();
    KResultOr<uid_t> sys$geteuid();
    KResultOr<gid_t> sys$getegid();
    KResultOr<pid_t> sys$getpid();
    KResultOr<pid_t> sys$getppid();
    KResultOr<int> sys$getresuid(Userspace<uid_t*>, Userspace<uid_t*>, Userspace<uid_t*>);
    KResultOr<int> sys$getresgid(Userspace<gid_t*>, Userspace<gid_t*>, Userspace<gid_t*>);
    KResultOr<mode_t> sys$umask(mode_t);
    KResultOr<int> sys$open(Userspace<const Syscall::SC_open_params*>);
    KResultOr<int> sys$close(int fd);
    KResultOr<ssize_t> sys$read(int fd, Userspace<u8*>, ssize_t);
    KResultOr<ssize_t> sys$readv(int fd, Userspace<const struct iovec*> iov, int iov_count);
    KResultOr<ssize_t> sys$write(int fd, Userspace<const u8*>, ssize_t);
    KResultOr<ssize_t> sys$writev(int fd, Userspace<const struct iovec*> iov, int iov_count);
    KResultOr<int> sys$fstat(int fd, Userspace<stat*>);
    KResultOr<int> sys$stat(Userspace<const Syscall::SC_stat_params*>);
    KResultOr<int> sys$lseek(int fd, off_t, int whence);
    KResultOr<int> sys$kill(pid_t pid_or_pgid, int sig);
    [[noreturn]] void sys$exit(int status);
    KResultOr<int> sys$sigreturn(RegisterState& registers);
    KResultOr<pid_t> sys$waitid(Userspace<const Syscall::SC_waitid_params*>);
    KResultOr<FlatPtr> sys$mmap(Userspace<const Syscall::SC_mmap_params*>);
    KResultOr<FlatPtr> sys$mremap(Userspace<const Syscall::SC_mremap_params*>);
    KResultOr<int> sys$munmap(Userspace<void*>, size_t);
    KResultOr<int> sys$set_mmap_name(Userspace<const Syscall::SC_set_mmap_name_params*>);
    KResultOr<int> sys$mprotect(Userspace<void*>, size_t, int prot);
    KResultOr<int> sys$madvise(Userspace<void*>, size_t, int advice);
    KResultOr<int> sys$msyscall(Userspace<void*>);
    KResultOr<int> sys$purge(int mode);
    KResultOr<int> sys$select(Userspace<const Syscall::SC_select_params*>);
    KResultOr<int> sys$poll(Userspace<const Syscall::SC_poll_params*>);
    KResultOr<ssize_t> sys$get_dir_entries(int fd, Userspace<void*>, ssize_t);
    KResultOr<int> sys$getcwd(Userspace<char*>, size_t);
    KResultOr<int> sys$chdir(Userspace<const char*>, size_t);
    KResultOr<int> sys$fchdir(int fd);
    KResultOr<int> sys$adjtime(Userspace<const timeval*>, Userspace<timeval*>);
    KResultOr<int> sys$gettimeofday(Userspace<timeval*>);
    KResultOr<int> sys$clock_gettime(clockid_t, Userspace<timespec*>);
    KResultOr<int> sys$clock_settime(clockid_t, Userspace<const timespec*>);
    KResultOr<int> sys$clock_nanosleep(Userspace<const Syscall::SC_clock_nanosleep_params*>);
    KResultOr<int> sys$gethostname(Userspace<char*>, ssize_t);
    KResultOr<int> sys$sethostname(Userspace<const char*>, ssize_t);
    KResultOr<int> sys$uname(Userspace<utsname*>);
    KResultOr<int> sys$readlink(Userspace<const Syscall::SC_readlink_params*>);
    KResultOr<int> sys$ttyname(int fd, Userspace<char*>, size_t);
    KResultOr<int> sys$ptsname(int fd, Userspace<char*>, size_t);
    KResultOr<pid_t> sys$fork(RegisterState&);
    KResultOr<int> sys$execve(Userspace<const Syscall::SC_execve_params*>);
    KResultOr<int> sys$dup2(int old_fd, int new_fd);
    KResultOr<int> sys$sigaction(int signum, Userspace<const sigaction*> act, Userspace<sigaction*> old_act);
    KResultOr<int> sys$sigprocmask(int how, Userspace<const sigset_t*> set, Userspace<sigset_t*> old_set);
    KResultOr<int> sys$sigpending(Userspace<sigset_t*>);
    KResultOr<int> sys$getgroups(ssize_t, Userspace<gid_t*>);
    KResultOr<int> sys$setgroups(ssize_t, Userspace<const gid_t*>);
    KResultOr<int> sys$pipe(int pipefd[2], int flags);
    KResultOr<int> sys$killpg(pid_t pgrp, int sig);
    KResultOr<int> sys$seteuid(uid_t);
    KResultOr<int> sys$setegid(gid_t);
    KResultOr<int> sys$setuid(uid_t);
    KResultOr<int> sys$setgid(gid_t);
    KResultOr<int> sys$setresuid(uid_t, uid_t, uid_t);
    KResultOr<int> sys$setresgid(gid_t, gid_t, gid_t);
    KResultOr<unsigned> sys$alarm(unsigned seconds);
    KResultOr<int> sys$access(Userspace<const char*> pathname, size_t path_length, int mode);
    KResultOr<int> sys$fcntl(int fd, int cmd, u32 extra_arg);
    KResultOr<int> sys$ioctl(int fd, unsigned request, FlatPtr arg);
    KResultOr<int> sys$mkdir(Userspace<const char*> pathname, size_t path_length, mode_t mode);
    KResultOr<clock_t> sys$times(Userspace<tms*>);
    KResultOr<int> sys$utime(Userspace<const char*> pathname, size_t path_length, Userspace<const struct utimbuf*>);
    KResultOr<int> sys$link(Userspace<const Syscall::SC_link_params*>);
    KResultOr<int> sys$unlink(Userspace<const char*> pathname, size_t path_length);
    KResultOr<int> sys$symlink(Userspace<const Syscall::SC_symlink_params*>);
    KResultOr<int> sys$rmdir(Userspace<const char*> pathname, size_t path_length);
    KResultOr<int> sys$mount(Userspace<const Syscall::SC_mount_params*>);
    KResultOr<int> sys$umount(Userspace<const char*> mountpoint, size_t mountpoint_length);
    KResultOr<int> sys$chmod(Userspace<const char*> pathname, size_t path_length, mode_t);
    KResultOr<int> sys$fchmod(int fd, mode_t);
    KResultOr<int> sys$chown(Userspace<const Syscall::SC_chown_params*>);
    KResultOr<int> sys$fchown(int fd, uid_t, gid_t);
    KResultOr<int> sys$socket(int domain, int type, int protocol);
    KResultOr<int> sys$bind(int sockfd, Userspace<const sockaddr*> addr, socklen_t);
    KResultOr<int> sys$listen(int sockfd, int backlog);
    KResultOr<int> sys$accept(int sockfd, Userspace<sockaddr*>, Userspace<socklen_t*>);
    KResultOr<int> sys$connect(int sockfd, Userspace<const sockaddr*>, socklen_t);
    KResultOr<int> sys$shutdown(int sockfd, int how);
    KResultOr<ssize_t> sys$sendmsg(int sockfd, Userspace<const struct msghdr*>, int flags);
    KResultOr<ssize_t> sys$recvmsg(int sockfd, Userspace<struct msghdr*>, int flags);
    KResultOr<int> sys$getsockopt(Userspace<const Syscall::SC_getsockopt_params*>);
    KResultOr<int> sys$setsockopt(Userspace<const Syscall::SC_setsockopt_params*>);
    KResultOr<int> sys$getsockname(Userspace<const Syscall::SC_getsockname_params*>);
    KResultOr<int> sys$getpeername(Userspace<const Syscall::SC_getpeername_params*>);
    KResultOr<int> sys$sched_setparam(pid_t pid, Userspace<const struct sched_param*>);
    KResultOr<int> sys$sched_getparam(pid_t pid, Userspace<struct sched_param*>);
    KResultOr<int> sys$create_thread(void* (*)(void*), Userspace<const Syscall::SC_create_thread_params*>);
    [[noreturn]] void sys$exit_thread(Userspace<void*>);
    KResultOr<int> sys$join_thread(pid_t tid, Userspace<void**> exit_value);
    KResultOr<int> sys$detach_thread(pid_t tid);
    KResultOr<int> sys$set_thread_name(pid_t tid, Userspace<const char*> buffer, size_t buffer_size);
    KResultOr<int> sys$get_thread_name(pid_t tid, Userspace<char*> buffer, size_t buffer_size);
    KResultOr<int> sys$rename(Userspace<const Syscall::SC_rename_params*>);
    KResultOr<int> sys$mknod(Userspace<const Syscall::SC_mknod_params*>);
    KResultOr<int> sys$halt();
    KResultOr<int> sys$reboot();
    KResultOr<int> sys$realpath(Userspace<const Syscall::SC_realpath_params*>);
    KResultOr<ssize_t> sys$getrandom(Userspace<void*>, size_t, unsigned int);
    KResultOr<int> sys$getkeymap(Userspace<const Syscall::SC_getkeymap_params*>);
    KResultOr<int> sys$setkeymap(Userspace<const Syscall::SC_setkeymap_params*>);
    KResultOr<int> sys$module_load(Userspace<const char*> path, size_t path_length);
    KResultOr<int> sys$module_unload(Userspace<const char*> name, size_t name_length);
    KResultOr<int> sys$profiling_enable(pid_t);
    KResultOr<int> sys$profiling_disable(pid_t);
    KResultOr<int> sys$futex(Userspace<const Syscall::SC_futex_params*>);
    KResultOr<int> sys$chroot(Userspace<const char*> path, size_t path_length, int mount_flags);
    KResultOr<int> sys$pledge(Userspace<const Syscall::SC_pledge_params*>);
    KResultOr<int> sys$unveil(Userspace<const Syscall::SC_unveil_params*>);
    KResultOr<int> sys$perf_event(int type, FlatPtr arg1, FlatPtr arg2);
    KResultOr<int> sys$get_stack_bounds(Userspace<FlatPtr*> stack_base, Userspace<size_t*> stack_size);
    KResultOr<int> sys$ptrace(Userspace<const Syscall::SC_ptrace_params*>);
    KResultOr<int> sys$sendfd(int sockfd, int fd);
    KResultOr<int> sys$recvfd(int sockfd, int options);
    KResultOr<long> sys$sysconf(int name);
    KResultOr<int> sys$disown(ProcessID);
    KResultOr<FlatPtr> sys$allocate_tls(size_t);
    KResultOr<int> sys$prctl(int option, FlatPtr arg1, FlatPtr arg2);
    KResultOr<int> sys$set_coredump_metadata(Userspace<const Syscall::SC_set_coredump_metadata_params*>);
    [[noreturn]] void sys$abort();
    KResultOr<int> sys$anon_create(size_t, int options);

    template<bool sockname, typename Params>
    int get_sock_or_peer_name(const Params&);

    static void initialize();

    [[noreturn]] void crash(int signal, u32 eip, bool out_of_memory = false);
    [[nodiscard]] siginfo_t wait_info();

    const TTY* tty() const { return m_tty; }
    void set_tty(TTY*);

    u32 m_ticks_in_user { 0 };
    u32 m_ticks_in_kernel { 0 };

    u32 m_ticks_in_user_for_dead_children { 0 };
    u32 m_ticks_in_kernel_for_dead_children { 0 };

    Custody& current_directory();
    Custody* executable() { return m_executable.ptr(); }
    const Custody* executable() const { return m_executable.ptr(); }

    const Vector<String>& arguments() const { return m_arguments; };
    const Vector<String>& environment() const { return m_environment; };

    int number_of_open_file_descriptors() const;
    int max_open_file_descriptors() const
    {
        return m_max_open_file_descriptors;
    }

    KResult exec(String path, Vector<String> arguments, Vector<String> environment, int recusion_depth = 0);

    KResultOr<LoadResult> load(NonnullRefPtr<FileDescription> main_program_description, RefPtr<FileDescription> interpreter_description, const Elf32_Ehdr& main_program_header);

    bool is_superuser() const { return euid() == 0; }

    void terminate_due_to_signal(u8 signal);
    KResult send_signal(u8 signal, Process* sender);

    u8 termination_signal() const { return m_termination_signal; }

    u16 thread_count() const
    {
        return m_thread_count.load(AK::MemoryOrder::memory_order_relaxed);
    }

    Lock& big_lock() { return m_big_lock; }
    Lock& ptrace_lock() { return m_ptrace_lock; }

    Custody& root_directory();
    Custody& root_directory_relative_to_global_root();
    void set_root_directory(const Custody&);

    bool has_promises() const { return protected_data().has_promises; }
    bool has_promised(Pledge pledge) const { return protected_data().promises & (1u << (u32)pledge); }

    VeilState veil_state() const
    {
        return m_veil_state;
    }
    const UnveilNode& unveiled_paths() const
    {
        return m_unveiled_paths;
    }

    bool wait_for_tracer_at_next_execve() const
    {
        return m_wait_for_tracer_at_next_execve;
    }
    void set_wait_for_tracer_at_next_execve(bool val)
    {
        m_wait_for_tracer_at_next_execve = val;
    }

    KResultOr<u32> peek_user_data(Userspace<const u32*> address);
    KResult poke_user_data(Userspace<u32*> address, u32 data);

    void disowned_by_waiter(Process& process);
    void unblock_waiters(Thread::WaitBlocker::UnblockFlags, u8 signal = 0);
    Thread::WaitBlockCondition& wait_block_condition() { return m_wait_block_condition; }

    HashMap<String, String>& coredump_metadata() { return m_coredump_metadata; }
    const HashMap<String, String>& coredump_metadata() const { return m_coredump_metadata; }

    const NonnullRefPtrVector<Thread>& threads_for_coredump(Badge<CoreDump>) const { return m_threads_for_coredump; }

    PerformanceEventBuffer* perf_events() { return m_perf_event_buffer; }

    Space& space() { return *m_space; }
    const Space& space() const { return *m_space; }

    VirtualAddress signal_trampoline() const { return m_signal_trampoline; }

private:
    friend class MemoryManager;
    friend class Scheduler;
    friend class Region;

    bool add_thread(Thread&);
    bool remove_thread(Thread&);

    Process(RefPtr<Thread>& first_thread, const String& name, uid_t, gid_t, ProcessID ppid, bool is_kernel_process, RefPtr<Custody> cwd = nullptr, RefPtr<Custody> executable = nullptr, TTY* = nullptr, Process* fork_parent = nullptr);
    static ProcessID allocate_pid();

    void kill_threads_except_self();
    void kill_all_threads();
    bool dump_core();
    bool dump_perfcore();
    bool create_perf_events_buffer_if_needed();

    KResult do_exec(NonnullRefPtr<FileDescription> main_program_description, Vector<String> arguments, Vector<String> environment, RefPtr<FileDescription> interpreter_description, Thread*& new_main_thread, u32& prev_flags, const Elf32_Ehdr& main_program_header);
    KResultOr<ssize_t> do_write(FileDescription&, const UserOrKernelBuffer&, size_t);

    KResultOr<RefPtr<FileDescription>> find_elf_interpreter_for_executable(const String& path, const Elf32_Ehdr& elf_header, int nread, size_t file_size);

    int alloc_fd(int first_candidate_fd = 0);

    KResult do_kill(Process&, int signal);
    KResult do_killpg(ProcessGroupID pgrp, int signal);
    KResult do_killall(int signal);
    KResult do_killself(int signal);

    KResultOr<siginfo_t> do_waitid(idtype_t idtype, int id, int options);

    KResultOr<String> get_syscall_path_argument(const char* user_path, size_t path_length) const;
    KResultOr<String> get_syscall_path_argument(Userspace<const char*> user_path, size_t path_length) const
    {
        return get_syscall_path_argument(user_path.unsafe_userspace_ptr(), path_length);
    }
    KResultOr<String> get_syscall_path_argument(const Syscall::StringArgument&) const;

    bool has_tracee_thread(ProcessID tracer_pid);

    void clear_futex_queues_on_exec();

    Process* m_prev { nullptr };
    Process* m_next { nullptr };

    String m_name;

    OwnPtr<Space> m_space;
    VirtualAddress m_signal_trampoline;

    RefPtr<ProcessGroup> m_pg;

    const ProtectedData& protected_data() const;
    void protect_data();
    void unprotect_data();
    OwnPtr<KBuffer> m_protected_data;

    OwnPtr<ThreadTracer> m_tracer;

    static const int m_max_open_file_descriptors { FD_SETSIZE };

    class FileDescriptionAndFlags {
    public:
        operator bool() const { return !!m_description; }

        FileDescription* description() { return m_description; }
        const FileDescription* description() const { return m_description; }

        u32 flags() const { return m_flags; }
        void set_flags(u32 flags) { m_flags = flags; }

        void clear();
        void set(NonnullRefPtr<FileDescription>&&, u32 flags = 0);

    private:
        RefPtr<FileDescription> m_description;
        u32 m_flags { 0 };
    };
    Vector<FileDescriptionAndFlags> m_fds;

    u8 m_termination_status { 0 };
    u8 m_termination_signal { 0 };
    Atomic<u32> m_thread_count { 0 };
    mutable IntrusiveList<Thread, &Thread::m_process_thread_list_node> m_thread_list;
    mutable RecursiveSpinLock m_thread_list_lock;

    const bool m_is_kernel_process;
    bool m_dead { false };
    bool m_profiling { false };
    Atomic<bool, AK::MemoryOrder::memory_order_relaxed> m_is_stopped { false };
    bool m_should_dump_core { false };

    RefPtr<Custody> m_executable;
    RefPtr<Custody> m_cwd;
    RefPtr<Custody> m_root_directory;
    RefPtr<Custody> m_root_directory_relative_to_global_root;

    Vector<String> m_arguments;
    Vector<String> m_environment;

    RefPtr<TTY> m_tty;

    mode_t m_umask { 022 };

    WeakPtr<Region> m_master_tls_region;
    size_t m_master_tls_size { 0 };
    size_t m_master_tls_alignment { 0 };

    Lock m_big_lock { "Process" };
    Lock m_ptrace_lock { "ptrace" };

    RefPtr<Timer> m_alarm_timer;

    VeilState m_veil_state { VeilState::None };
    UnveilNode m_unveiled_paths { "/", { .full_path = "/", .unveil_inherited_from_root = true } };

    OwnPtr<PerformanceEventBuffer> m_perf_event_buffer;

    FutexQueues m_futex_queues;
    SpinLock<u8> m_futex_lock;

    // This member is used in the implementation of ptrace's PT_TRACEME flag.
    // If it is set to true, the process will stop at the next execve syscall
    // and wait for a tracer to attach.
    bool m_wait_for_tracer_at_next_execve { false };

    Thread::WaitBlockCondition m_wait_block_condition;

    HashMap<String, String> m_coredump_metadata;

    NonnullRefPtrVector<Thread> m_threads_for_coredump;
};

extern InlineLinkedList<Process>* g_processes;
extern RecursiveSpinLock g_processes_lock;

template<typename Callback>
inline void Process::for_each(Callback callback)
{
    VERIFY_INTERRUPTS_DISABLED();
    ScopedSpinLock lock(g_processes_lock);
    for (auto* process = g_processes->head(); process;) {
        auto* next_process = process->next();
        if (callback(*process) == IterationDecision::Break)
            break;
        process = next_process;
    }
}

template<typename Callback>
inline void Process::for_each_child(Callback callback)
{
    VERIFY_INTERRUPTS_DISABLED();
    ProcessID my_pid = pid();
    ScopedSpinLock lock(g_processes_lock);
    for (auto* process = g_processes->head(); process;) {
        auto* next_process = process->next();
        if (process->ppid() == my_pid || process->has_tracee_thread(pid())) {
            if (callback(*process) == IterationDecision::Break)
                break;
        }
        process = next_process;
    }
}

template<typename Callback>
inline IterationDecision Process::for_each_thread(Callback callback) const
{
    ScopedSpinLock thread_list_lock(m_thread_list_lock);
    for (auto& thread : m_thread_list) {
        IterationDecision decision = callback(thread);
        if (decision != IterationDecision::Continue)
            return decision;
    }
    return IterationDecision::Continue;
}

template<typename Callback>
inline void Process::for_each_in_pgrp(ProcessGroupID pgid, Callback callback)
{
    VERIFY_INTERRUPTS_DISABLED();
    ScopedSpinLock lock(g_processes_lock);
    for (auto* process = g_processes->head(); process;) {
        auto* next_process = process->next();
        if (!process->is_dead() && process->pgid() == pgid) {
            if (callback(*process) == IterationDecision::Break)
                break;
        }
        process = next_process;
    }
}

inline bool InodeMetadata::may_read(const Process& process) const
{
    return may_read(process.euid(), process.egid(), process.extra_gids());
}

inline bool InodeMetadata::may_write(const Process& process) const
{
    return may_write(process.euid(), process.egid(), process.extra_gids());
}

inline bool InodeMetadata::may_execute(const Process& process) const
{
    return may_execute(process.euid(), process.egid(), process.extra_gids());
}

inline ProcessID Thread::pid() const
{
    return m_process->pid();
}

inline const LogStream& operator<<(const LogStream& stream, const Process& process)
{
    return stream << process.name() << '(' << process.pid().value() << ')';
}

#define REQUIRE_NO_PROMISES                        \
    do {                                           \
        if (Process::current()->has_promises()) {  \
            dbgln("Has made a promise");           \
            Process::current()->crash(SIGABRT, 0); \
            VERIFY_NOT_REACHED();                  \
        }                                          \
    } while (0)

#define REQUIRE_PROMISE(promise)                                     \
    do {                                                             \
        if (Process::current()->has_promises()                       \
            && !Process::current()->has_promised(Pledge::promise)) { \
            dbgln("Has not pledged {}", #promise);                   \
            Process::current()->coredump_metadata().set(             \
                "pledge_violation", #promise);                       \
            Process::current()->crash(SIGABRT, 0);                   \
            VERIFY_NOT_REACHED();                                    \
        }                                                            \
    } while (0)

}

inline static String copy_string_from_user(const Kernel::Syscall::StringArgument& string)
{
    return copy_string_from_user(string.characters, string.length);
}

template<>
struct AK::Formatter<Kernel::Process> : AK::Formatter<String> {
    void format(FormatBuilder& builder, const Kernel::Process& value)
    {
        return AK::Formatter<String>::format(builder, String::formatted("{}({})", value.name(), value.pid().value()));
    }
};
