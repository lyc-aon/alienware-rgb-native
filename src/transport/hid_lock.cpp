#include "transport/hid_lock.h"

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <thread>

namespace {
std::string defaultLockPath() {
    if (const char* runtime = std::getenv("XDG_RUNTIME_DIR"); runtime && *runtime) {
        return std::string(runtime) + "/alienware-rgb.lock";
    }
    return "/tmp/alienware-rgb.lock";
}
}

HIDLock::HIDLock(int timeout_ms) : path_(defaultLockPath()) {
    fd_ = ::open(path_.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (fd_ < 0) {
        // Fall back to /tmp if XDG_RUNTIME_DIR unwritable.
        path_ = "/tmp/alienware-rgb.lock";
        fd_ = ::open(path_.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    }
    if (fd_ < 0) return;

    // Try non-blocking first; if busy, poll with small sleeps up to timeout_ms.
    const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::milliseconds(timeout_ms);
    while (true) {
        if (::flock(fd_, LOCK_EX | LOCK_NB) == 0) {
            acquired_ = true;
            return;
        }
        if (errno != EWOULDBLOCK && errno != EAGAIN) {
            break;  // unexpected error
        }
        if (std::chrono::steady_clock::now() >= deadline) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
}

HIDLock::~HIDLock() {
    if (fd_ >= 0) {
        if (acquired_) ::flock(fd_, LOCK_UN);
        ::close(fd_);
    }
}
