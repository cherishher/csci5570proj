#pragma once

#include <ctime>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <vector>

#include "engine.hpp"

namespace csci5570 {

class EngineManager : public Engine{
 public:
	EngineManager(const Node& node, const std::vector<Node>& nodes, 
									bool isPrimary = true): Engine(node, nodes) {
		this->isPrimary_ = isPrimary;
	}

	void StartEM(){
		id_mapper_.reset(new SimpleIdMapper(node_, nodes_));
    	id_mapper_->Init(1); // num_server_threads_per_node = 1
    	mailbox_.reset(new Mailbox(node_, nodes_, id_mapper_.get()));
    	mailbox_->Start();
    	sender_.reset(new Sender(mailbox_.get()));
    	sender_->Start();

    	// see if it needs to be changed !!!
		std::vector<uint32_t> sids = id_mapper_->GetServerThreadsForId(node_.id);
		HeartBeatServerThread_.reset(new ServerThread(sids.size() - 1));

		StartAllEngines();
    	StartHeartBeat();
	}

	void StopEM(){
		StopAllEngines();
		sender_->Stop();
		mailbox_->Stop();
		StopHeartBeat();
	}

	void StartAllEngines(){
		for (int i = 0; i < nodes_.size(); i++){
			if (!(node_ == nodes_[i])) {
				uint32_t to_node_thread_id = id_mapper_->GetHeartBeatThreadForId(nodes_[i].id);
	  			std::unique_ptr<Engine> ptr(new Engine(nodes_[i], nodes_)); // append hdfs_address
	  			ptr->StartEverything();
      			engine_group_.push_back(std::move(ptr));
  			}
		}
	}

	void StopAllEngines(){
		for (int i = 0; i < nodes_.size(); i++){
			if (!(node_ == nodes_[i])){
      			engine_group_[i].get()->StopEverything();
  			}
		}
	}

	void HeartBeat(){
		
		// call get thread_id with node_id as parameter for communication
		uint32_t from_node_thread_id = id_mapper_->GetHeartBeatThreadForId(node_.id);
		time_t timestamp = time(NULL);

		for(int i = 0; i < nodes_.size(); i++) {

			uint32_t to_node_thread_id = id_mapper_->GetHeartBeatThreadForId(nodes_[i].id);

			Message msg; // the heartbeat message
		    msg.meta.flag = Flag::kHeartbeat;
		    msg.meta.sender = from_node_thread_id;
		    msg.meta.recver = to_node_thread_id;
		    msg.meta.timestamp = timestamp;

			sender_.get()->GetMessageQueue()->Push(msg);
			heartbeat_count_[to_node_thread_id].push_back(timestamp);
		}
		sender_->Send();

		// add this time stamp to all the nodes's already sent heartbeat_count_

	}

	bool HeartBeatDetection(int threshold = 4){
		
		// get heartbeat server thread's messages
      	HeartBeatServerThread_.get()->GetWorkQueue();

		// check message queue, put it into maps
		ThreadsafeQueue<Message>* queue;
		while(queue->Size() > 0){
			Message msg;
			queue->WaitAndPop(&msg);

			// check maps
			if(heartbeat_count_.find(msg.meta.sender) == heartbeat_count_.end()){
				std::deque<time_t> deq;
				heartbeat_count_.insert(std::make_pair(msg.meta.sender, std::deque<time_t>()));
			} else {
				for (int i = 0; i < heartbeat_count_[msg.meta.sender].size(); i++){
					if(heartbeat_count_[msg.meta.sender][i] < msg.meta.timestamp){
						heartbeat_count_[msg.meta.sender].pop_front();
					}
				}
			}
		}

		for(auto it = heartbeat_count_.begin(); it != heartbeat_count_.end(); ++it){
			if(it->second.size() >= threshold){
				// Get recovery data address
				std::string hdfs_address = "hdfs:///csci5570@group6/recovery/" + it->first; // using thread_id as its recovery address
				// restart this engine
				RestartEngine(hdfs_address, it->first);
			}
		}
	}

	bool RestartEngine(std::string hdfs_address, uint32_t engine_thread_id){
		StopHeartBeat();
		
		// system call
		std::string cmd = "python ";
		cmd += "../scripts/launch.py ";//  need to write a new script
		cmd += hdfs_addr_ + " 1";// 1 -> recovery, 0 -> not recovery
		system(cmd.c_str());

		sleep(30);
		StartHeartBeat();
	}

	// timer in seconds
	bool StartHeartBeat(int interval = 10){
		if (expired_ == false){
  			// timer is currently running, please expire it first...
            return false;
        }
        expired_ = false;

        heartbeat_count_.clear();
        for(int i = 0; i < nodes_.size(); i++){
        	heartbeat_count_.insert(std::make_pair(id_mapper_->GetHeartBeatThreadForId(nodes_[i].id), std::deque<time_t>()));
        }

        std::thread([this, interval](){
            while (!try_to_expire_){
	            std::this_thread::sleep_for(std::chrono::seconds(interval));
	            HeartBeat();
	            HeartBeatDetection();
            }
            // stop timer task
            {
                std::lock_guard<std::mutex> locker(mutex_);
                expired_ = true;
                expired_cond_.notify_one();
            }
        }).detach();
        return true;
	}

	bool StopHeartBeat(){
		if (expired_){
            return false;
        }
        if (try_to_expire_){
             // timer is trying to expire, please wait...
            return false;
        }

        try_to_expire_ = true;

        {
            std::unique_lock<std::mutex> locker(mutex_);
            expired_cond_.wait(locker, [this]{return expired_ == true; });
            if (expired_ == true){
                try_to_expire_ = false;
                return true;
            }
        }
	}

 private:
 	// Node node_;
 	// std::vector<Node> nodes_; // the EM will occupy an entire Node! the nodes includes all the nodes.
	// std::unique_ptr<Mailbox> mailbox_; // inherit from engine
	// std::unique_ptr<Sender> sender_; // inherit from engine
	
	bool isPrimary_ = true; // denote the primary engine manager if yes, or the secondary engine manager if false
	std::map<uint32_t, std::deque<time_t>> heartbeat_count_; // thread_id, each engine's heartbeat message

	std::unique_ptr<ServerThread> HeartBeatServerThread_;

	std::vector<std::unique_ptr<Engine>> engine_group_;

	// about the timer taks
	std::atomic<bool> expired_;
	std::atomic<bool> try_to_expire_;
	std::mutex mutex_;
	std::condition_variable expired_cond_;
};

} 

// Message Type