#ifndef HID_LOCK_H
#define HID_LOCK_H

#include <string>

// RAII lock around /dev/hidraw* access, to serialize GUI + CLI. Backed by
// flock() on a shared lock file ($XDG_RUNTIME_DIR/alienware-rgb.lock, or
// /tmp/alienware-rgb.lock as fallback).
//
//   HIDLock lock(2000);            // wait up to 2000ms
//   if (!lock.acquired()) return;  // another process is busy
//   // ... HID writes ...
//   // lock released when object goes out of scope
//
// Used by both the GUI HIDWorker (around each command) and the CLI (around
// the whole command run).
class HIDLock {
public:
    explicit HIDLock(int timeout_ms = 2000);
    ~HIDLock();

    bool acquired() const { return acquired_; }
    const std::string& path() const { return path_; }

    HIDLock(const HIDLock&) = delete;
    HIDLock& operator=(const HIDLock&) = delete;

private:
    std::string path_;
    int fd_ = -1;
    bool acquired_ = false;
};

#endif // HID_LOCK_H
