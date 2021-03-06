#pragma once

#include <cinttypes>
#include "base/message.hpp"
#include "base/threadsafe_queue.hpp"

namespace csci5570 {

class AbstractModel {
 public:
  virtual void Clock(Message& msg) = 0;
  virtual void Add(Message& msg) = 0;
  virtual void Get(Message& msg) = 0;
  virtual int GetProgress(int tid) = 0;
  virtual void ResetWorker(Message& msg) = 0;
  virtual void Backup() = 0;
  virtual int Recovery() = 0;
  virtual ~AbstractModel() {}
};

}  // namespace csci5570
