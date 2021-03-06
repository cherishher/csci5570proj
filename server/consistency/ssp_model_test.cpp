#include "glog/logging.h"
#include "gtest/gtest.h"

#include "base/threadsafe_queue.hpp"
#include "server/consistency/ssp_model.hpp"
#include "server/map_storage.hpp"

namespace csci5570 {
namespace {

class TestSSPModel : public testing::Test {
 public:
  TestSSPModel() {}
  ~TestSSPModel() {}

 protected:
  void SetUp() {}
  void TearDown() {}
};

TEST_F(TestSSPModel, CheckConstructor) {
  ThreadsafeQueue<Message> reply_queue;
  int staleness = 1;
  int model_id = 0;
  std::unique_ptr<AbstractStorage> storage(new MapStorage<int>());
  std::unique_ptr<AbstractModel> model(new SSPModel(model_id, std::move(storage), staleness, &reply_queue));
}

TEST_F(TestSSPModel, CheckGetAndAdd) {
  ThreadsafeQueue<Message> reply_queue;
  int staleness = 1;
  int model_id = 0;
  std::unique_ptr<AbstractStorage> storage(new MapStorage<int>());
  std::unique_ptr<AbstractModel> model(new SSPModel(model_id, std::move(storage), staleness, &reply_queue));
  Message reset_msg;
  third_party::SArray<uint32_t> tids({2, 3});
  reset_msg.AddData(tids);
  model->ResetWorker(reset_msg);
  Message reset_reply_msg;
  reply_queue.WaitAndPop(&reset_reply_msg);
  EXPECT_EQ(reset_reply_msg.meta.flag, Flag::kResetWorkerInModel);

  // Message3
  Message m3;
  m3.meta.flag = Flag::kAdd;
  m3.meta.model_id = 0;
  m3.meta.sender = 2;
  m3.meta.recver = 0;
  third_party::SArray<int> m3_keys({0});
  third_party::SArray<int> m3_vals({1});
  m3.AddData(m3_keys);
  m3.AddData(m3_vals);

  // Message4
  Message m4;
  m4.meta.flag = Flag::kAdd;
  m4.meta.model_id = 0;
  m4.meta.sender = 3;
  m4.meta.recver = 0;
  third_party::SArray<int> m4_keys({1});
  third_party::SArray<int> m4_vals({2});
  m4.AddData(m4_keys);
  m4.AddData(m4_vals);

  // Message5
  Message m5;
  m5.meta.flag = Flag::kGet;
  m5.meta.model_id = 0;
  m5.meta.sender = 2;
  m5.meta.recver = 0;
  third_party::SArray<int> m5_keys({0});
  m5.AddData(m5_keys);

  // Message6
  Message m6;
  m6.meta.flag = Flag::kGet;
  m6.meta.model_id = 0;
  m6.meta.sender = 3;
  m6.meta.recver = 0;
  third_party::SArray<int> m6_keys({1});
  m6.AddData(m6_keys);

  model.get()->Add(m3);
  model.get()->Add(m4);
  model.get()->Get(m5);
  model.get()->Get(m6);

  // Check
  Message check_msg;
  auto rep_keys = third_party::SArray<int>();
  auto rep_vals = third_party::SArray<int>();

  ASSERT_EQ(reply_queue.Size(), 4);// 2 -> 4
  reply_queue.WaitAndPop(&check_msg);// add
  reply_queue.WaitAndPop(&check_msg);// add

  reply_queue.WaitAndPop(&check_msg);
  ASSERT_EQ(check_msg.data.size(), 2);
  rep_keys = third_party::SArray<int>(check_msg.data[0]);
  rep_vals = third_party::SArray<int>(check_msg.data[1]);
  ASSERT_EQ(rep_keys.size(), 1);
  ASSERT_EQ(rep_vals.size(), 1);
  EXPECT_EQ(rep_keys[0], 0);
  EXPECT_EQ(rep_vals[0], 1);
  EXPECT_EQ(check_msg.meta.sender, 0);
  EXPECT_EQ(check_msg.meta.recver, 2);

  reply_queue.WaitAndPop(&check_msg);
  ASSERT_EQ(check_msg.data.size(), 2);
  rep_keys = third_party::SArray<int>(check_msg.data[0]);
  rep_vals = third_party::SArray<int>(check_msg.data[1]);
  ASSERT_EQ(rep_keys.size(), 1);
  ASSERT_EQ(rep_vals.size(), 1);
  EXPECT_EQ(rep_keys[0], 1);
  EXPECT_EQ(rep_vals[0], 2);
  EXPECT_EQ(check_msg.meta.sender, 0);
  EXPECT_EQ(check_msg.meta.recver, 3);
}

TEST_F(TestSSPModel, CheckClock) {
  ThreadsafeQueue<Message> reply_queue;
  int staleness = 1;
  int model_id = 0;
  std::unique_ptr<AbstractStorage> storage(new MapStorage<int>());
  std::unique_ptr<AbstractModel> model(new SSPModel(model_id, std::move(storage), staleness, &reply_queue));
  Message reset_msg;
  third_party::SArray<uint32_t> tids({2, 3});
  reset_msg.AddData(tids);
  model->ResetWorker(reset_msg);
  Message reset_reply_msg;
  reply_queue.WaitAndPop(&reset_reply_msg);
  EXPECT_EQ(reset_reply_msg.meta.flag, Flag::kResetWorkerInModel);

  // Message1
  Message m1;
  m1.meta.flag = Flag::kClock;
  m1.meta.model_id = 0;
  m1.meta.sender = 2;
  m1.meta.recver = 0;
  model->Clock(m1);

  // Message2
  Message m2;
  m2.meta.flag = Flag::kClock;
  m2.meta.model_id = 0;
  m2.meta.sender = 3;
  m2.meta.recver = 0;
  model->Clock(m2);

  // Message3
  Message m3;
  m3.meta.flag = Flag::kClock;
  m3.meta.model_id = 0;
  m3.meta.sender = 2;
  m3.meta.recver = 0;
  model->Clock(m3);

  EXPECT_EQ(model->GetProgress(2), 2);
  EXPECT_EQ(model->GetProgress(3), 1);
}

TEST_F(TestSSPModel, CheckStaleness) {
  ThreadsafeQueue<Message> reply_queue;
  int staleness = 2;
  int model_id = 0;
  std::unique_ptr<AbstractStorage> storage(new MapStorage<int>());
  std::unique_ptr<AbstractModel> model(new SSPModel(model_id, std::move(storage), staleness, &reply_queue));
  Message reset_msg;
  third_party::SArray<uint32_t> tids({2, 3});
  reset_msg.AddData(tids);
  model->ResetWorker(reset_msg);
  Message reset_reply_msg;
  reply_queue.WaitAndPop(&reset_reply_msg);
  EXPECT_EQ(reset_reply_msg.meta.flag, Flag::kResetWorkerInModel);

  // Message1
  Message m1;
  m1.meta.flag = Flag::kGet;
  m1.meta.model_id = 0;
  m1.meta.sender = 2;
  m1.meta.recver = 0;
  third_party::SArray<int> m1_keys({0});
  m1.AddData(m1_keys);
  model->Get(m1);
  reply_queue.WaitAndPop(&m1);

  // Message2
  Message m2;
  m2.meta.flag = Flag::kClock;
  m2.meta.model_id = 0;
  m2.meta.sender = 2;
  m2.meta.recver = 0;
  model->Clock(m2);

  // Message3
  Message m3;
  m3.meta.flag = Flag::kAdd;
  m3.meta.model_id = 0;
  m3.meta.sender = 2;
  m3.meta.recver = 0;
  third_party::SArray<int> m3_keys({0});
  third_party::SArray<int> m3_vals({1});
  m3.AddData(m3_keys);
  m3.AddData(m3_vals);
  model->Add(m3);

  // Message4
  Message m4;
  m4.meta.flag = Flag::kClock;
  m4.meta.model_id = 0;
  m4.meta.sender = 2;
  m4.meta.recver = 0;
  model->Clock(m4);

  // Message5
  Message m5;
  m5.meta.flag = Flag::kClock;
  m5.meta.model_id = 0;
  m5.meta.sender = 2;
  m5.meta.recver = 0;
  model->Clock(m5);

  // Check
  Message m;
  m.meta.flag = Flag::kGet;
  m.meta.model_id = 0;
  m.meta.sender = 2;
  m.meta.recver = 0;
  third_party::SArray<int> m_keys1({0});
  m.AddData(m_keys1);
  model->Get(m);
  EXPECT_EQ(dynamic_cast<SSPModel*>(model.get())->GetPendingSize(1), 1);

  // Message6
  Message m6;
  m6.meta.flag = Flag::kClock;
  m6.meta.model_id = 0;
  m6.meta.sender = 3;
  m6.meta.recver = 0;
  model->Clock(m6);

  // Check
  Message m7;
  m7.meta.flag = Flag::kGet;
  m7.meta.model_id = 0;
  m7.meta.sender = 2;
  m7.meta.recver = 0;
  third_party::SArray<int> m7_keys({0});
  m7.AddData(m7_keys);
  model->Get(m7);
  EXPECT_EQ(dynamic_cast<SSPModel*>(model.get())->GetPendingSize(1), 0);
}

}  // namespace
}  // namespace csci5570
