#pragma once

#include "base/actor_model.hpp"
#include "base/message.hpp"
#include "base/threadsafe_queue.hpp"
#include "worker/abstract_callback_runner.hpp"

#include <condition_variable>
#include <memory>
#include <thread>
#include <unordered_map>

namespace csci5570 {

class AbstractWorkerThread : public Actor {
 public:
  AbstractWorkerThread(uint32_t worker_id) : Actor(worker_id) {}

 protected:
  virtual void OnReceive(Message& msg) = 0;  // callback on receival of a message

  // there may be other functions
  //   Wait() and Nofify() for telling when parameters are ready

  // there may be other states such as
  //   a local copy of parameters
  //   some locking mechanism for notifying when parameters are ready
  //   a counter of msgs from servers, etc.
};

class WorkerHelperThread : public AbstractWorkerThread {
 public:
  WorkerHelperThread(uint32_t worker_id, AbstractCallbackRunner* callback_runner)
      : AbstractWorkerThread(worker_id), callback_runner_(callback_runner) {}

 protected:
  void OnReceive(Message& msg) {
      callback_runner_->AddResponse(msg.meta.recver,msg.meta.model_id,msg);
   }

  void Main() {
    //printf("Main() in the worker helper thread and worker helper queue size is %d\n",work_queue_.Size());
    auto* work_queue = this->GetWorkQueue();
    while (true) {
      Message m;
      work_queue->WaitAndPop(&m);
      if(m.meta.flag == Flag::kExit){
        return;
      }
      switch (m.meta.flag) {
        case Flag::kGet:
          //LOG(INFO) << "worker get message";
          this->OnReceive(m);
          break;
        case Flag::kAdd:
          //LOG(INFO) << "worker add message";
          this->OnReceive(m);
          break;
      }
    }
  }

 private:
  AbstractCallbackRunner* callback_runner_;
};

}  // namespace csci5570
