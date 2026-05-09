#ifndef HID_WORKER_H
#define HID_WORKER_H

#include <QThread>
#include <QObject>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <memory>
#include "controller/awelc_controller.h"
#include "transport/hid_transport.h"

class HIDWorker : public QThread {
    Q_OBJECT

public:
    explicit HIDWorker(QObject* parent = nullptr);
    ~HIDWorker() override;

    void connectDevice();
    void stop();

    // Queue a command to execute
    template<typename Func>
    void enqueue(Func&& func) {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        command_queue_.emplace(std::move(func));
        queue_cv_.notify_one();
    }

    AWELCController& controller() { return *controller_; }

signals:
    void connected(const QString& firmware, uint16_t platform, int zone_count);
    void disconnected(const QString& reason);
    void error(const QString& message);

protected:
    void run() override;

private:
    std::unique_ptr<HIDTransport> transport_;
    std::unique_ptr<AWELCController> controller_;
    std::queue<std::function<void()>> command_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    bool running_ = false;
    bool should_stop_ = false;
};

#endif // HID_WORKER_H