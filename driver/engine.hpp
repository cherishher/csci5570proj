#pragma once

#include <vector>

#include "base/abstract_partition_manager.hpp"
#include "base/node.hpp"
#include "comm/mailbox.hpp"
#include "comm/sender.hpp"
#include "driver/ml_task.hpp"
#include "driver/simple_id_mapper.hpp"
#include "driver/worker_spec.hpp"
#include "server/server_thread.hpp"
#include "worker/abstract_callback_runner.hpp"
#include "worker/worker_thread.hpp"

#include "base/range_partition_manager.hpp"
#include "server/consistency/asp_model.hpp"
#include "server/consistency/bsp_model.hpp"
#include "server/consistency/ssp_model.hpp"
#include "server/map_storage.hpp"

namespace csci5570 {

enum class ModelType { SSP, BSP, ASP };
enum class StorageType { Map };  // May have Vector

class Engine {
 public:
  /**
   * Engine constructor
   *
   * @param node     the current node
   * @param nodes    all nodes in the cluster
   */
  Engine(const Node& node, const std::vector<Node>& nodes) : node_(node), nodes_(nodes){};
  // Engine(const Node& node, const std::vector<Node>& nodes, const std::string hdfs_addr) : node_(node), nodes_(nodes),
  // hdfs_addr_(hdfs_addr){};

  /**
   * The flow of starting the engine:
   * 1. Create an id_mapper and a mailbox
   * 2. Start Sender
   * 3. Create ServerThreads and WorkerThreads
   * 4. Register the threads to mailbox through ThreadsafeQueue
   * 5. Start the communication threads: bind and connect to all other nodes
   *
   * @param num_server_threads_per_node the number of server threads to start on each node
   */
  void StartEverything(int num_server_threads_per_node = 1);
  void CreateIdMapper(int num_server_threads_per_node = 1);
  void CreateMailbox();
  void StartServerThreads();
  void StartWorkerThreads();
  void StartMailbox();
  void StartSender();

  uint32_t RecoveryEngine() {
    int model_count = RecoveryModelCount();
    int min_clock;
    for (int i = 0; i < model_count; i++) {
      min_clock = RecoveryTable<double>(i);
    }
    return min_clock;
  }

  /**
   * The flow of stopping the engine:
   * 1. Stop the Sender
   * 2. Stop the mailbox: by Barrier() and then exit
   * 3. The mailbox will stop the corresponding registered threads
   * 4. Stop the ServerThreads and WorkerThreads
   */
  void StopEverything();
  void StopServerThreads();
  void StopWorkerThreads();
  void StopSender();
  void StopMailbox();

  /**
   * Synchronization barrier for processes
   */
  void Barrier();
  /**
   * Create the whole picture of the worker group, and register the workers in the id mapper
   *
   * @param worker_alloc    the worker allocation information
   */
  WorkerSpec AllocateWorkers(const std::vector<WorkerAlloc>& worker_alloc);

  /**
   * Create the partitions of a model on the local servers
   * 1. Assign a table id (incremental and consecutive)
   * 2. Register the partition manager to the model
   * 3. For each local server thread maintained by the engine
   *    a. Create a storage according to <storage_type>
   *    b. Create a model according to <model_type>
   *    c. Register the model to the server thread
   *
   * @param partition_manager   the model partition manager
   * @param model_type          the consistency of model - bsp, ssp, asp
   * @param storage_type        the storage type - map, vector...
   * @param model_staleness     the staleness for ssp model
   * @return                    the created table(model) id
   */
  template <typename Val>
  uint32_t CreateTable(std::unique_ptr<AbstractPartitionManager> partition_manager, ModelType model_type,
                       StorageType storage_type, int model_staleness = 0) {
    // each model corresponds to a table
    uint32_t table_id = model_count_++;
    std::string model_type_string;
    std::string storage_type_string;

    RegisterPartitionManager(table_id, std::move(partition_manager));
    std::unique_ptr<AbstractStorage> storage;

    switch (storage_type) {
    case StorageType::Map:
      storage.reset(new MapStorage<Val>());
      storage_type_string = "Map";
      break;
    default:
      storage.reset(new MapStorage<Val>());
    }

    std::unique_ptr<AbstractModel> model;
    for (int i = 0; i < server_thread_group_.size(); i++) {
      switch (model_type) {
      case ModelType::ASP:
        model.reset(new ASPModel(table_id, std::move(storage), sender_->GetMessageQueue()));
        model->Backup();
        model_type_string = "ASP";
        break;
      case ModelType::BSP:
        model.reset(new BSPModel(table_id, std::move(storage), sender_->GetMessageQueue()));
        model->Backup();
        model_type_string = "BSP";
        break;
      case ModelType::SSP:
        model.reset(new SSPModel(table_id, std::move(storage), model_staleness, sender_->GetMessageQueue()));
        model->Backup();
        model_type_string = "SSP";
        break;
      default:
        break;
      }
      server_thread_group_[i].get()->RegisterModel(table_id, std::move(model));
      BackupTable(table_id, model_type_string, storage_type_string, model_staleness);
    }
    BackupModelConunt();
    return table_id;
  }

  void BackupTable(uint32_t table_id, std::string model_type, std::string storage_type, int model_staleness = 0) {
    std::ofstream outfile;
    std::string path = "/data/table" + std::to_string(table_id) + ".txt";
    outfile.open(path);
    auto range = partition_manager_map_[table_id]->GetRanges();
    outfile << model_type << "\n";
    outfile << storage_type << "\n";
    outfile << model_staleness << "\n";
    for (int i = 0; i < range.size(); i++) {
      outfile << range[i].begin() << " " << range[i].end() << "\n";
    }
    outfile.close();
  }

  template <typename Val>
  int RecoveryTable(uint32_t table_id) {
    std::ifstream ifs;
    std::string path = "/data/table" + std::to_string(table_id) + ".txt";
    ifs.open(path, std::ifstream::in);
    std::string s;
    ifs >> s;
    std::string model_type_string = s;
    ifs >> s;
    std::string storage_type_string = s;
    ifs >> s;
    int model_staleness = std::stoi(s);
    const std::vector<uint32_t> sids = GetServerThreadIds();
    // build ranges
    int sid_size = sids.size();
    std::vector<third_party::Range> ranges;
    int begin = 0;
    int end = 0;
    while (ifs >> s) {
      begin = std::stoi(s);
      ifs >> s;
      end = std::stoi(s);
      third_party::Range range(begin, end);
      ranges.push_back(range);
    }
    ifs.close();
    std::unique_ptr<AbstractPartitionManager> partition_manager(new RangePartitionManager(sids, ranges));
    RegisterPartitionManager(table_id, std::move(partition_manager));

    std::unique_ptr<AbstractModel> model;
    std::unique_ptr<AbstractStorage> storage;
    int min_clock;
    storage.reset(new MapStorage<Val>());
    for (int i = 0; i < server_thread_group_.size(); i++) {
      if (model_type_string == "ASP") {
        model.reset(new ASPModel(table_id, std::move(storage), sender_->GetMessageQueue()));
        min_clock = model->Recovery();
      } else if (model_type_string == "BSP") {
        model.reset(new BSPModel(table_id, std::move(storage), sender_->GetMessageQueue()));
        min_clock = model->Recovery();
      } else {
        model.reset(new SSPModel(table_id, std::move(storage), model_staleness, sender_->GetMessageQueue()));
        min_clock = model->Recovery();
        printf("model recovery finish\n");
      }
      server_thread_group_[i].get()->RegisterModel(table_id, std::move(model));
      printf("Register model finish\n");
    }
    return min_clock;
  }

  void BackupModelConunt() {
    std::ofstream outfile;
    std::string path = "/data/engine.txt";
    outfile.open(path);
    outfile << model_count_;
  }

  uint32_t RecoveryModelCount() {
    std::ifstream ifs;
    std::string path = "/data/engine.txt";
    ifs.open(path, std::ifstream::in);
    std::string s;
    ifs >> s;
    uint32_t model_count = std::stoi(s);
    return model_count;
  }

  /**
   * Create the partitions of a model on the local servers using a default partitioning scheme
   * 1. Create a default partition manager
   * 2. Create a table with the partition manager
   *
   * @param model_type          the consistency of model - bsp, ssp, asp
   * @param storage_type        the storage type - map, vector...
   * @param model_staleness     the staleness for ssp model
   * @return                    the created table(model) id
   */
  template <typename Val>
  uint32_t CreateTable(ModelType model_type, StorageType storage_type, int model_staleness = 0) {
    // get server thread ids
    const std::vector<uint32_t> sids = GetServerThreadIds();

    // build ranges
    int count = sids.size();
    std::vector<third_party::Range> ranges(count);
    //  TODO... implement ranges
    ranges = {{0, 20}, {20, 40}, {40, 60}, {60, 80}, {80, 110}};
    // build partition manager
    std::unique_ptr<AbstractPartitionManager> partition_manager(new RangePartitionManager(sids, ranges));
    uint32_t table_id = CreateTable<Val>(std::move(partition_manager), model_type, storage_type, model_staleness);
    return table_id;
  }

  /**
   * Reset workers in the specified model so that each model knows the workers with the right of access
   */
  void InitTable(uint32_t table_id, const std::vector<uint32_t>& worker_ids);

  /**
   * Run the task
   *
   * After starting the system, the engine run a task by starting the prescribed threads to run UDF
   *
   * @param task    the task to run
   */
  void Run(const MLTask& task);

  /**
   * Returns the server thread ids
   */
  std::vector<uint32_t> GetServerThreadIds() { return id_mapper_->GetAllServerThreads(); }

 protected:
  /**
   * Register partition manager for a model to the engine
   *
   * @param table_id            the model id
   * @param partition_manager   the partition manager for the specific model
   */
  void RegisterPartitionManager(uint32_t table_id, std::unique_ptr<AbstractPartitionManager> partition_manager);

  std::map<uint32_t, std::unique_ptr<AbstractPartitionManager>> partition_manager_map_;
  // nodes
  Node node_;
  std::vector<Node> nodes_;
  // mailbox
  std::unique_ptr<SimpleIdMapper> id_mapper_;
  std::unique_ptr<Mailbox> mailbox_;
  std::unique_ptr<Sender> sender_;
  // worker elements
  std::unique_ptr<AbstractCallbackRunner> callback_runner_;
  std::unique_ptr<AbstractWorkerThread> worker_thread_;
  // server elements
  std::vector<std::unique_ptr<ServerThread>> server_thread_group_;
  size_t model_count_ = 0;

  std::string hdfs_addr_;
};  // namespace csci5570

}  // namespace csci5570
