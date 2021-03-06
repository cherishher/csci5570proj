#include "server/consistency/ssp_model.hpp"
#include "glog/logging.h"
#include "gtest/gtest.h"

namespace csci5570 {

SSPModel::SSPModel(uint32_t model_id, std::unique_ptr<AbstractStorage>&& storage_ptr, int staleness,
                   ThreadsafeQueue<Message>* reply_queue)
    : model_id_(model_id), storage_(std::move(storage_ptr)), staleness_(staleness), reply_queue_(reply_queue) {
  // TODO
}

void SSPModel::Clock(Message& msg) {
  // TODO
  if (!progress_tracker_.CheckThreadValid(msg.meta.sender))
    return;
  int cur_mini_clock = progress_tracker_.AdvanceAndGetChangedMinClock(msg.meta.sender);
  if (cur_mini_clock != -1 &&
      GetPendingSize(cur_mini_clock) > 0) {  // min_clock changed, process pending messages if needed
    auto pendingMsgs = buffer_.Pop(cur_mini_clock);
    for (auto pending : pendingMsgs) {
      if (pending.meta.flag == Flag::kAdd)
        Add(pending);
      if (pending.meta.flag == Flag::kGet)
        Get(pending);
    }
    if(cur_mini_clock % 10 == 0){
      this->Backup();
    }
  }
}

void SSPModel::Add(Message& msg) {
  // TODO
  if (!progress_tracker_.CheckThreadValid(msg.meta.sender))
    return;
  if (GetProgress(msg.meta.sender) - progress_tracker_.GetMinClock() <= staleness_) {
    Message reply = storage_->Add(msg);
    reply_queue_->Push(reply);
  } else {
    buffer_.Push(GetProgress(msg.meta.sender) - staleness_, msg);
  }
}

void SSPModel::Get(Message& msg) {
  // TODO
  if (!progress_tracker_.CheckThreadValid(msg.meta.sender))
    return;
  if (GetProgress(msg.meta.sender) - progress_tracker_.GetMinClock() <= staleness_) {
    Message reply = storage_->Get(msg);
    // add round info
    reply.meta.round = GetProgress(msg.meta.sender);
    reply_queue_->Push(reply);
  } else {
    buffer_.Push(GetProgress(msg.meta.sender) - staleness_, msg);
  }
}

int SSPModel::GetProgress(int tid) {
  // TODO
  return progress_tracker_.GetProgress(tid);
}

int SSPModel::GetPendingSize(int progress) {
  // TODO
  return buffer_.Size(progress);
}

void SSPModel::ResetWorker(Message& msg) {
  // TODO
  third_party::SArray<uint32_t> tids(msg.data[0]);
  progress_tracker_.Init(std::vector<uint32_t>(tids.begin(), tids.end()));
  Message relpy;
  relpy.meta.flag = Flag::kResetWorkerInModel;
  relpy.meta.model_id = msg.meta.model_id;
  relpy.meta.recver = msg.meta.sender;
  relpy.meta.sender = msg.meta.recver;
  reply_queue_->Push(relpy);
}

void SSPModel::Backup() {
  storage_->Backup(model_id_);
  progress_tracker_.Backup(model_id_);
}

int SSPModel::Recovery() {
  storage_->Recovery(model_id_);
  int min_clock = progress_tracker_.Recovery(model_id_);
  return min_clock;
}

}  // namespace csci5570
