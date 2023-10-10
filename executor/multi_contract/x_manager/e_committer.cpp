/*
 * Copyright (c) 2019-2022 ExpoLab, UC Davis
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 */

#include "executor/multi_contract/x_manager/e_committer.h"
#include "executor/multi_contract/x_manager/executor_state.h"

#include <future>
#include <queue>

#include "common/utils/utils.h"
#include "eEVM/exception.h"

#include "glog/logging.h"
#include "eEVM/processor.h"

//#define Debug

namespace resdb {
namespace contract {
namespace x_manager {

namespace {

class TimeTrack {
public:
  TimeTrack(std::string name = ""){
    name_ = name;
    start_time_ = GetCurrentTime();
  }

  ~TimeTrack(){
    uint64_t end_time = GetCurrentTime();
    //LOG(ERROR) << name_ <<" run:" << (end_time - start_time_)<<"ms"; 
  }
  
  double GetRunTime(){
    uint64_t end_time = GetCurrentTime();
    return (end_time - start_time_) / 1000000.0;
  }
private:
  std::string name_;
  uint64_t start_time_;
};

}

ECommitter:: ECommitter(
    DataStorage * storage,
    GlobalState * global_state, 
    int window_size,
    int worker_num):gs_(global_state),
    worker_num_(worker_num),
    window_size_(window_size) {

  //LOG(ERROR)<<"init window:"<<window_size<<" worker:"<<worker_num_;
  window_size_ = 1500;
  controller_ = std::make_unique<EController>(storage, window_size);
  //controller_ = std::make_unique<StreamingEController>(storage, window_size*2);
  executor_ = std::make_unique<ContractExecutor>();

  resp_list_.resize(window_size_);
  is_done_.resize(window_size_);
  for(int i = 0; i < window_size_;++i){
    is_done_[i] =false;
    resp_list_[i] = nullptr;
  }

  //context_list_.resize(window_size_);
  //tmp_resp_list_.resize(window_size_);
  first_id_ = 0;
  last_id_ = 1;
  is_stop_ = false;
  id_ = 1;
  for (int i = 0; i < worker_num_; ++i) {
    workers_.push_back(std::thread([&]() {
      while (!is_stop_) {
        auto request_ptr = request_queue_.Pop();
        if (request_ptr == nullptr) {
          continue;
        }
        ExecutionContext * request = *request_ptr;
      
        TimeTrack track;
        ExecutorState executor_state(controller_.get(), request->GetContractExecuteInfo()->commit_id);
        executor_state.Set(gs_->GetAccount(
            request->GetContractExecuteInfo()->contract_address), 
           request->GetContractExecuteInfo()->commit_id,
           request->RedoTime());

        std::unique_ptr<ExecuteResp> resp = std::make_unique<ExecuteResp>();
        auto ret = ExecContract(request->GetContractExecuteInfo()->caller_address, 
            request->GetContractExecuteInfo()->contract_address,
            request->GetContractExecuteInfo()->func_addr,
            request->GetContractExecuteInfo()->func_params, &executor_state);
        resp->state = ret.status();
        resp->contract_address = request->GetContractExecuteInfo()->contract_address;
        resp->commit_id = request->GetContractExecuteInfo()->commit_id;
        resp->user_id = request->GetContractExecuteInfo()->user_id;
        if(ret.ok()){
          resp->ret = 0;
          resp->result = *ret;
          if(request->IsRedo()){
            resp->retry_time=request->RedoTime();
          }
          resp->runtime = track.GetRunTime()*1000;
        }
        else {
          //LOG(ERROR)<<"commit :"<<resp->commit_id<<" fail";
          resp->ret = -1;
        }
        resp_queue_.Push(std::move(resp));
      }
    }));
  }
}


ECommitter::~ECommitter(){
  //LOG(ERROR)<<"desp";
  is_stop_ = true;
  for (int i = 0; i < worker_num_; ++i) {
    workers_[i].join();
  }
}

void ECommitter::AddTask(int64_t commit_id, std::unique_ptr<ExecutionContext> context){
    context_list_[commit_id] = std::move(context);
}

ExecutionContext* ECommitter::GetTaskContext(int64_t commit_id){
  return context_list_[commit_id].get();
}

void ECommitter::AsyncExecContract(std::vector<ContractExecuteInfo>& requests) {
  
  return ;
}

std::vector<std::unique_ptr<ExecuteResp>>  ECommitter::ExecContract(
    std::vector<ContractExecuteInfo>& requests) {

/*
  std::promise<bool> done;
  std::future<bool> done_future = done.get_future();
  std::vector<std::unique_ptr<ExecuteResp>> resp_list;
  int num = 0;
  call_back_ = [&](std::unique_ptr<ExecuteResp> resp) {
    resp_list.push_back(std::move(resp));
      
      std::lock_guard<std::mutex> lk(mutex_);
      cv_.notify_all();
      num--;
  };

  std::vector<std::unique_ptr<ExecuteResp>> tmp_resp_list;
  for(int i = 0; i <= requests.size(); ++i){
    tmp_resp_list.push_back(nullptr);
  }

  auto ResponseProcess = [&]() {
    auto resp = resp_queue_.Pop();
    if(resp == nullptr){
      return; 
    }

    int64_t resp_id= resp->commit_id;
    int ret = resp->ret;
    #ifdef Debug
    LOG(ERROR)<<"resp:"<<resp_id<<" list size:"<<tmp_resp_list.size()<<" ret:"<<ret;
    #endif
    tmp_resp_list[resp_id] = std::move(resp);

    if(ret==0){
    #ifdef Debug
      LOG(ERROR)<<"commit:"<<resp_id;
    #endif
        bool commit_ret = controller_->Commit(resp_id);
        if(!commit_ret){
          controller_->Clear(resp_id);
          auto context_ptr = GetTaskContext(resp_id);
          context_ptr->SetRedo();
          #ifdef Debug
          LOG(ERROR)<<"redo:"<<resp_id;
          #endif
          request_queue_.Push(std::make_unique<ExecutionContext*>(context_ptr));
        }
        const auto& redo_list = controller_->GetRedo();
        for(int64_t id : redo_list) {
          controller_->Clear(id);
          auto context_ptr = GetTaskContext(id);
          context_ptr->SetRedo();
          #ifdef Debug
          LOG(ERROR)<<"redo:"<<id;
          #endif
          request_queue_.Push(std::make_unique<ExecutionContext*>(context_ptr));
        }

        const auto& done_list = controller_->GetDone();
        for(int id : done_list){
          //tmp_resp_list[id]->rws = std::move(controller_->GetChangeList(id));
          //LOG(ERROR)<<"done :"<<id<<" wrs size:"<<tmp_resp_list[id]->rws.size();
          //resp_list.push_back(std::move(tmp_resp_list[id]));
          //process_num--;
          call_back_(std::move(tmp_resp_list[id]));
          continue; 
        }
        //resp_list.push_back(std::move(tmp_resp_list[resp_id]));
        //process_num--;
        //continue; 
    }
    else {
          controller_->Clear(resp_id);
      auto context_ptr = GetTaskContext(resp_id);
      context_ptr->SetRedo();
    #ifdef Debug
      LOG(ERROR)<<"redo :"<<resp_id;
      #endif
      request_queue_.Push(std::make_unique<ExecutionContext*>(context_ptr));
    }
  };

  std::thread th = std::thread([&](){
    while(resp_list.size() != requests.size()){
    ResponseProcess();
  }});

  auto WaitNext = [&](){
    int c = 64;
  while(!is_stop_) {
    int timeout_ms = 10000;
    std::unique_lock<std::mutex> lk(mutex_);
    cv_.wait_for(lk, std::chrono::microseconds(timeout_ms), [&] {
        return num<c;
        //return id_ - first_id_<window_size_;
    });
    if(num<c){
      return true;
    }
    num++;
  }
  };

  for(auto& request: requests) {
    if(!WaitNext()){
    break;
    }
    int cur_idx = id_;
    assert(id_>=last_id_);

    request.commit_id = id_++;
    auto context = std::make_unique<ExecutionContext>(request);
    auto context_ptr = context.get();

    //LOG(ERROR)<<"execute:"<<request.commit_id<<" id:"<<id_<<" first id:"<<first_id_<<" last id:"<<last_id_<<" window:"<<window_size_<<" cur idx:"<<cur_idx;
   assert(cur_idx<window_size_);
    assert(resp_list_[cur_idx] == nullptr);

    AddTask(cur_idx, std::move(context));
    controller_->Clear(request.commit_id);
    request_queue_.Push(std::make_unique<ExecutionContext*>(context_ptr));
  }

th.join();
  return resp_list;
  */
  controller_->Clear();
  int id = 0;
  std::vector<std::unique_ptr<ExecuteResp>> tmp_resp_list;
  for(auto& request: requests) {
    request.commit_id = id++;
    auto context = std::make_unique<ExecutionContext>(request);
    auto context_ptr = context.get();
    context_ptr->start_time = GetCurrentTime();

    AddTask(request.commit_id, std::move(context));
    request_queue_.Push(std::make_unique<ExecutionContext*>(context_ptr));
    tmp_resp_list.push_back(nullptr);
  }

  std::vector<std::unique_ptr<ExecuteResp>> resp_list;

  int process_num = id;
  //LOG(ERROR)<<"wait num:"<<process_num;
  while(process_num>0) {
    auto resp = resp_queue_.Pop();
    if(resp == nullptr){
      continue;
    }

    int64_t resp_id= resp->commit_id;
    int ret = resp->ret;
    #ifdef Debug
    LOG(ERROR)<<"resp:"<<resp_id<<" list size:"<<tmp_resp_list.size()<<" ret:"<<ret;
    #endif
    tmp_resp_list[resp_id] = std::move(resp);

    if(ret==0){
    #ifdef Debug
      LOG(ERROR)<<"commit:"<<resp_id;
    #endif
        bool commit_ret = controller_->Commit(resp_id);
        if(!commit_ret){
          controller_->Clear(resp_id);
          auto context_ptr = GetTaskContext(resp_id);
          context_ptr->SetRedo();
          #ifdef Debug
          LOG(ERROR)<<"redo:"<<resp_id;
          #endif
          request_queue_.Push(std::make_unique<ExecutionContext*>(context_ptr));
        }
        const auto& redo_list = controller_->GetRedo();
        for(int64_t id : redo_list) {
          controller_->Clear(id);
          auto context_ptr = GetTaskContext(id);
          context_ptr->SetRedo();
          #ifdef Debug
          LOG(ERROR)<<"redo:"<<id;
          #endif
          request_queue_.Push(std::make_unique<ExecutionContext*>(context_ptr));
        }

        const auto& done_list = controller_->GetDone();
        for(int id : done_list){
          tmp_resp_list[id]->rws = *controller_->GetChangeList(id);
          //LOG(ERROR)<<"done :"<<id<<" wrs size:"<<tmp_resp_list[id]->rws.size();
          resp_list.push_back(std::move(tmp_resp_list[id]));
          process_num--;
          continue; 
        }
        //resp_list.push_back(std::move(tmp_resp_list[resp_id]));
        //process_num--;
        //continue; 
    }
    else {
          controller_->Clear(resp_id);
      auto context_ptr = GetTaskContext(resp_id);
      context_ptr->SetRedo();
    #ifdef Debug
      LOG(ERROR)<<"redo :"<<resp_id;
      #endif
      request_queue_.Push(std::make_unique<ExecutionContext*>(context_ptr));
    }
  }
  return resp_list;
//  LOG(ERROR)<<"last id:"<<last_id_;
}

absl::StatusOr<std::string> ECommitter::ExecContract(
    const Address& caller_address, const Address& contract_address,
    const std::string& func_addr,
    const Params& func_param, EVMState * state) {
    //LOG(ERROR)<<"start:"<<caller_address;
  return executor_->ExecContract(caller_address, contract_address, func_addr, func_param, state);  
}

}
}  // namespace contract
}  // namespace resdb
