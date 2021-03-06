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

using Sample = lib::LabeledSample<std::vector<std::pair<int, int>>, int>;
using DataStore = std::vector<Sample>;
using Parse = std::function<Sample(boost::string_ref, int)>;

DEFINE_string(config_file, "", "The config file path");
DEFINE_string(input, "", "The hdfs input url");

int main(int argc, char** argv) {
  for (int i = 0; i < argc; i++)
    gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);
  FLAGS_stderrthreshold = 0;
  FLAGS_colorlogtostderr = true;

  LOG(INFO) << FLAGS_config_file;

  Node node{0, "localhost", 12353};

  bool recovery = false;
  if (std::strcmp(argv[argc - 2], "1") == 0) {
    recovery = true;
  }

  Engine engine(node, {node});
  engine.StartEverything();

  uint32_t kTableId = 0;
  int min_clock = 0;

  if (recovery) {
    //   engine = Engine(node, {node}, true);
    min_clock = engine.RecoveryEngine();
    //   engine.Barrier();
  } else {
    // 1.1 Create table
    kTableId = engine.CreateTable<double>(ModelType::SSP, StorageType::Map);  // table 0
    engine.Barrier();
  }

  // 1.2 Load data

  // double lambda = 0.1;
  // double alpha = 0.01;
  // int n_features = 123;
  // std::string url = "hdfs:///datasets/classification/a9";
  // DataStore data;
  // Parse parse(lib::Parser<Sample, DataStore>::parse_libsvm);
  // lib::AbstractDataLoader<Sample, DataStore>::load<Parse>(url, n_features, parse, &data);

  // 2. Start training task
  MLTask task;
  task.SetWorkerAlloc({{0, 3}});  // 3 workers on node 0
  task.SetTables({kTableId});     // Use table 0
  task.SetLambda([kTableId, min_clock](const Info& info) {
    LOG(INFO) << info.DebugString();

    KVClientTable<double> table = info.CreateKVClientTable<double>(kTableId);

    std::vector<Key> parameter_keys;  // parameters index
    std::vector<double> initial;      // parameter values

    // initial parameters
    for (int m = 0; m < 10; m++) {
      parameter_keys.push_back(m);
      initial.push_back(0);
    }
    if (min_clock == 0) {
      table.Add(parameter_keys, initial);
    }

    // calculate and update parameters
    for (int i = min_clock; i < 20; i++) {
      std::vector<double> ret;  // parameter values
      table.Get(parameter_keys, &ret);
      for (int j = 0; j < ret.size(); j++) {
        std::cout << "parameter is :" << ret[j] << std::endl;
        ret[j]++;
      }

      table.Add(parameter_keys, ret);

      table.Clock();
    }
  });

  engine.Run(task);

  // 3. Stop
  engine.StopEverything();

  return 0;
}
