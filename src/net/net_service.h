// How a Session is serviced: the one place where the two service modes differ.
//
// NET_SESSION_THREADED=1 (Linux, Windows, macOS, Android; the default): a
// thread runs the socket, so connections survive while the game does not
// poll (stage loading). Every public method locks the session.
//
// NET_SESSION_THREADED=0 (AmigaOS; also built on the host for the tests): no
// thread, no lock, none of <thread>, <mutex>, <atomic> or
// <condition_variable>. The game's task services the socket: Session::Pump()
// runs one pass, every public method runs it first, and the places that
// waited on a condition variable (Open(JOIN)) become bounded loops that pump.
// The Exec rules that force this: libnix malloc is not safe from two tasks
// (the session allocates while servicing) and a bsdsocket.library socket
// belongs to the task that opened the library.
//
// Both modes compile the same net_session.cpp; the code that differs is in
// this header and in the few #if blocks of net_session.cpp that use it.

#ifndef OPENCMR2_NET_SERVICE_H
#define OPENCMR2_NET_SERVICE_H

#ifndef NET_SESSION_THREADED
#define NET_SESSION_THREADED 1
#endif

#if NET_SESSION_THREADED

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace net {

struct ServiceState {
    std::mutex mutex;
    std::condition_variable changed;    // join result
    std::thread thread;
    std::atomic<bool> running{false};
};

// Locks the session for the scope (the service thread takes it for each step).
class ServiceGuard {
public:
    explicit ServiceGuard(ServiceState &s) : lock(s.mutex) {}
    std::unique_lock<std::mutex> lock;
};

} // namespace net

#else

namespace net {

struct ServiceState {
    bool running = false;
};

class ServiceGuard {
public:
    explicit ServiceGuard(ServiceState &) {}
};

} // namespace net

#endif

#endif
