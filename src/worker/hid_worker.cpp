#include "worker/hid_worker.h"
#include "transport/hid_lock.h"
#include <QString>
#include <QDebug>

HIDWorker::HIDWorker(QObject* parent) 
    : QThread(parent), transport_(std::make_unique<HIDTransport>()) {
    controller_ = std::make_unique<AWELCController>(*transport_);
}

HIDWorker::~HIDWorker() {
    stop();
}

void HIDWorker::connectDevice() {
    enqueue([this]() {
        if (!transport_->open()) {
            emit disconnected("Could not open AW-ELC device (187c:0551). Check udev rules.");
            return;
        }
        
        QString fw = QString::fromStdString(controller_->getFirmware());
        auto [platform, zones] = controller_->getConfig();
        
        emit connected(fw, platform, zones);
    });
}

void HIDWorker::stop() {
    should_stop_ = true;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        // Add sentinel command to unblock wait
        command_queue_.emplace([]() {});
    }
    queue_cv_.notify_one();
    wait(3000);
}

void HIDWorker::run() {
    running_ = true;
    
    while (running_ && !should_stop_) {
        std::function<void()> cmd;
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            
            // Wait for command or stop signal
            queue_cv_.wait_for(lock, std::chrono::milliseconds(500), [this]() {
                return !command_queue_.empty() || should_stop_;
            });
            
            if (command_queue_.empty()) {
                continue;
            }
            
            cmd = std::move(command_queue_.front());
            command_queue_.pop();
        }
        
        try {
            if (cmd) {
                // Serialize against the CLI via a shared flock. 2s timeout —
                // long enough for a typical CLI invocation to finish.
                HIDLock lock(2000);
                if (!lock.acquired()) {
                    qWarning() << "[hid_worker] could not acquire HID lock at"
                               << QString::fromStdString(lock.path())
                               << "— command dropped";
                    continue;
                }
                cmd();
            }
        } catch (const std::exception& e) {
            emit error(QString::fromStdString(e.what()));
        }
    }
    
    running_ = false;
    transport_->close();
}