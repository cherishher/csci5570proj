#include <cmath>

#include <iostream>
#include <random>
#include <thread>

#include "gflags/gflags.h"
#include "glog/logging.h"

#include "driver/engine.hpp"
#include "lib/abstract_data_loader.hpp"
#include "lib/labeled_sample.hpp"
#include "lib/parser.hpp"

#include "driver/engine_manager.hpp"

using namespace csci5570;

using Sample = double;
using DataStore = std::vector<Sample>;

DEFINE_string(config_file, "", "The config file path");
DEFINE_string(input, "", "The hdfs input url");

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);
  FLAGS_stderrthreshold = 0;
  FLAGS_colorlogtostderr = true;

  LOG(INFO) << FLAGS_config_file;

  Node node{0, "localhost", 12353};

  const char* is_recover = argv[argc];
  bool recover_tag = false;
  if(is_recover == "1"){
    recover_tag = true;
  }

  Engine engine(node, {node}, "", recover_tag);

  // 1. Start system
  engine.StartEverything();

  // 1.1 Create table
  const auto kTableId = engine.CreateTable<double>(ModelType::SSP, StorageType::Map);  // table 0

  // 1.2 Load data
  engine.Barrier();

  // 2. Start training task
  MLTask task;
  task.SetWorkerAlloc({{0, 3}});  // 3 workers on node 0
  task.SetTables({kTableId});     // Use table 0
  task.SetLambda([kTableId](const Info& info) {
    LOG(INFO) << info.DebugString();

    KVClientTable<double> table = info.CreateKVClientTable<double>(kTableId);

    for (int i = 0; i < 1e3; ++i) {
      std::vector<Key> keys{1};

      std::vector<double> ret;
      table.Get(keys, &ret);
      LOG(INFO) << ret[0];

      std::vector<double> vals{0.5};
      table.Add(keys, vals);

      table.Clock();
    }
  });

  engine.Run(task);

  // 3. Stop
  engine.StopEverything();


  EngineManager em(node, {node});
  std::cout << "test\n";
  return 0;
}