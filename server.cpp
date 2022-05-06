#include "server.h"
#include "server_store.h"
#include <iostream>
#include <unistd.h>
#include <string>
#include <random>

#include "util.h"


void blob_rpcHandler::read(read_ret& _return, const int64_t addr) {
  // not a leader
  // TODO: what if currently there is no leader
  while(leaderID.load() != -1);
  if (role.load() != 0) {
    std::cerr << "read: Not a leader" << std::endl;
    _return.rc = Errno::NOT_LEADER;
    _return.node_id = leaderID.load();
    return;
  }

  if (ServerStore::read(addr, _return.value) == 0) {
    _return.rc = Errno::SUCCESS;
    return;
  }

  _return.rc = Errno::UNEXPECTED;
  return;
}

PB_Errno::type raft_rpcHandler::update(const int64_t addr, const std::string& value, const int64_t seq) {
  static std::atomic<int64_t> curr_seq(0);

  if (role.load() == 0) {
    std::cerr << "update: is a leader" << std::endl;
    return PB_Errno::IS_LEADER;
  }
  // Wait for previous updates to complete
  while (curr_seq.load() != seq);
  // int64_t tmp;
  int result = ServerStore::write(addr, value);
  // let subsequent requests run
  curr_seq.fetch_add(1, std::memory_order_acq_rel);
  // TODO: handle write failures
  if (result == 0)
    return PB_Errno::SUCCESS;
  else
    // Backup fail to make copy, crash to avoid inconsistency
    exit(1);
}
void blob_rpcHandler::write(write_ret& _return, const int64_t addr, const std::string& value) {
  // Write to not leader
  if (role.load() != 0) {
    std::cerr << "write: Not a leader" << std::endl;
    _return.rc = Errno::NOT_LEADER;
    _return.node_id = leaderID.load();
    return ;
  }

retry:

  // creating a copy to followers, block write requests
  while (pending_candidate.load());
  // exist write requests, block whole file read for creating new backups
  num_write_requests.fetch_add(1, std::memory_order_acq_rel);
  // in case of race condition
  if (pending_candidate.load()) {
    num_write_requests.fetch_sub(1, std::memory_order_acq_rel);
    goto retry;
  }

  int64_t seq;
  entry e;
  e.term = currentTerm.load();
  e.command = 1;
  e.content = value;
  raftLog.push_back(e);
  int result = ServerStore::write(addr, value);

  // done with writing
  num_write_requests.fetch_sub(1, std::memory_order_acq_rel);

  if (result != 0){
    _return.rc = Errno::UNEXPECTED;
    return ;
  }
    
  // TODO : write request
  for(int i = 0; i < NODE_NUM; i++) {
    if(i == myID) {
      continue;
    }
    std::cout << "send write value to replicas " << i << std::endl;
    PB_Errno::type reply = rpcServer[i]->update(addr, value, seq);
    if (reply == PB_Errno::SUCCESS){
      _return.rc = Errno::SUCCESS;
      return ;
    }
    else{
      _return.rc = Errno::UNEXPECTED;
      return ;
    }
      // should not happen
      
  }
  
}

void pb_rpcHandler::heartbeat() {
  // primary receive heartbeat - unexpected behavior, ignore the result
  if (is_primary.load())
    return;
  // note that the other server (primary) is still alive
  std::cout << "Heartbeat received" << std::endl;
  time(&last_heartbeat);
}

void send_heartbeat() {
  try {
    while (true) {
      sleep(HB_FREQ);
      if (!has_backup.load())
        break;
      std::cout << "sending heartbeat" << std::endl;
      other->heartbeat();
    }
  } catch (TTransportException) {
    std::cerr << "Backup Failure" << std::endl;
    has_backup.store(false);
    pending_backup.store(false);
  }
}

void new_backup_helper() {
  sleep(5);
  if (pending_backup.load()) {
    has_backup.store(false);
    pending_backup.store(false);
  }
}

void pb_rpcHandler::new_backup(new_backup_ret& _return, const std::string& hostname, const int32_t port) {
  if (!is_primary.load()) {
    std::cerr << "new_backup: Not a Primary" << std::endl;
    _return.rc = PB_Errno::NOT_LEADER;
    return;
  }

  if (has_backup.load()) {
    std::cerr << "new_backup: Backup Already Exists" << std::endl;
    _return.rc = PB_Errno::BACKUP_EXISTS;
    return;
  }

  std::shared_ptr<TTransport> socket(new TSocket(hostname, port));
  std::shared_ptr<TTransport> transport(new TBufferedTransport(socket));
  std::shared_ptr<TProtocol> protocol(new TBinaryProtocol(transport));
  otherSyncInfo = std::make_shared<::apache::thrift::async::TConcurrentClientSyncInfo>();
  other = std::make_shared<pb_rpcConcurrentClient>(protocol, otherSyncInfo);
  transport->open();
  other->ping();

  // block future write operations
  pending_backup.store(true);
  // block wait for current write operations to complete
  while (num_write_requests.load() != 0);
  // full file read
  ServerStore::full_read(_return.content);

  std::thread(send_heartbeat).detach();
  std::thread(new_backup_helper).detach();
  _return.rc = PB_Errno::SUCCESS;
  return;
}

void pb_rpcHandler::new_backup_succeed() {
  // Allow new write requests after backup is ready
  if (pending_backup.load()) {
    has_backup.store(true);
    pending_backup.store(false);
  }
}

void start_pb_server() {
  std::cout << "Starting PB Server at " << my_pb_port << std::endl;
  ::std::shared_ptr<pb_rpcHandler> handler(new pb_rpcHandler());
  ::std::shared_ptr<TProcessor> processor(new pb_rpcProcessor(handler));
  ::std::shared_ptr<TServerTransport> serverTransport(new TServerSocket(my_pb_port));
  ::std::shared_ptr<TTransportFactory> transportFactory(new TBufferedTransportFactory());
  ::std::shared_ptr<TProtocolFactory> protocolFactory(new TBinaryProtocolFactory());
  std::shared_ptr<ThreadFactory> threadFactory = std::shared_ptr<ThreadFactory>(new ThreadFactory());
  std::shared_ptr<ThreadManager> threadManager = ThreadManager::newSimpleThreadManager(PB_SERVER_WORKER);

  TThreadPoolServer pb_server(processor, serverTransport, transportFactory, protocolFactory, threadManager);

  threadManager->threadFactory(threadFactory);
  threadManager->start();
  pb_server.serve();
}

void start_blob_server() {
  std::cout << "Starting Blob Server at " << my_blob_port << std::endl;
  ::std::shared_ptr<blob_rpcHandler> handler(new blob_rpcHandler());
  ::std::shared_ptr<TProcessor> processor(new blob_rpcProcessor(handler));
  ::std::shared_ptr<TServerTransport> serverTransport(new TServerSocket(my_blob_port));
  ::std::shared_ptr<TTransportFactory> transportFactory(new TBufferedTransportFactory());
  ::std::shared_ptr<TProtocolFactory> protocolFactory(new TBinaryProtocolFactory());
  std::shared_ptr<ThreadFactory> threadFactory = std::shared_ptr<ThreadFactory>(new ThreadFactory());
  std::shared_ptr<ThreadManager> threadManager = ThreadManager::newSimpleThreadManager(BLOB_SERVER_WORKER);

  TThreadPoolServer blob_server(processor, serverTransport, transportFactory, protocolFactory, threadManager);

  threadManager->threadFactory(threadFactory);
  threadManager->start();
  blob_server.serve();
}

void connect_to_primary(const std::string& hostname, const int port) {
  std::shared_ptr<TTransport> socket(new TSocket(hostname, port));
  std::shared_ptr<TTransport> transport(new TBufferedTransport(socket));
  std::shared_ptr<TProtocol> protocol(new TBinaryProtocol(transport));
  other = std::make_shared<pb_rpcClient>(protocol);
  transport->open();

  other->ping();

  struct new_backup_ret ret;
  other->new_backup(ret, my_addr, my_pb_port);
  if (ret.rc != PB_Errno::SUCCESS)
    exit(0);
  if (ServerStore::full_write(ret.content) != 0) {
    std::cout << "fail to write to backup" << std::endl;
    exit(1);
  }
  other->new_backup_succeed();
}


/* ===================================== */
/* Raft Implementation  */
/* ===================================== */
void raft_rpc_init() {
  for(int i = 0; i < NODE_NUM; i++) {
    std::shared_ptr<TTransport> socket(new TSocket(nodeAddr[i], raftPort[i]));
    std::shared_ptr<TTransport> transport(new TBufferedTransport(socket));
    std::shared_ptr<TProtocol> protocol(new TBinaryProtocol(transport));
    syncInfo[i] = std::make_shared<::apache::thrift::async::TConcurrentClientSyncInfo>();
    rpcServer[i] = std::make_shared<raft_rpcConcurrentClient>(protocol, syncInfo[i]);
    transport->open();
    rpcServer[i]->ping();
  }
  return;
}

void append_timeout() {
  while(1) {
    if(currentTerm.load() != 2) {
      break;
    }
    time_t curr = time(NULL);
    if(curr - last_append > APPEND_TIMEOUT) {
      break;
    }
  }

  toCandidate();
}

void toFollower(int term) {
  std:: cout << "TO FOLLOWER !!!" << std::endl;
  time(&last_append);
  pthread_rwlock_wrlock(&rolelock);

  role.store(2);
  int vote = -1;

  ServerStore::write_state(term, vote);
  currentTerm.store(term);
  votedFor.store(vote); 
  
  std::thread(append_timeout).detach();

  pthread_rwlock_unlock(&rolelock);
}

void toCandidate() {
  std:: cout << "TO CANDIDATE !!!" << std::endl;
  pthread_rwlock_wrlock(&rolelock);

  role.store(1);
  int term = currentTerm.load() + 1;
  int vote = myID;

  ServerStore::write_state(term, vote);
  currentTerm.store(term);
  votedFor.store(vote);

  pthread_rwlock_unlock(&rolelock);
}

void toLeader() {
  std:: cout << "TO LEADER !!!" << std::endl;
  pthread_rwlock_wrlock(&rolelock);

  role.store(0);
  leaderID.store(myID);
  pthread_rwlock_unlock(&rolelock);

  pthread_rwlock_rdlock(&raftloglock);
  int index = raftLog.size() - 1;

  pthread_rwlock_unlock(&raftloglock);
  for(int i = 0; i < NODE_NUM; i++) {
    nextIndex[i] = index + 1;
    matchIndex[i] = 0;
  }

}

void raft_rpcHandler::request_vote(request_vote_reply& ret, const request_vote_args& requestVote) {
  std::cout << "get vote request" << std::endl;

  if(requestVote.term < currentTerm.load()) {
    ret.voteGranted = false;
    ret.term = currentTerm.load();
    return;
  }

  if(requestVote.term > currentTerm.load()) {
    toFollower(requestVote.term);
  }
  
  int vote = votedFor.load();
  if(vote == -1 || vote == requestVote.candidateId) {
    if(requestVote.lastLogTerm > currentTerm.load()) {
      ret.voteGranted = true;
      votedFor.store(requestVote.candidateId);  
      return;
    }else if(requestVote.lastLogTerm == currentTerm.load() && requestVote.lastLogIndex >= (int)raftLog.size()) {
      ret.voteGranted = true;
      votedFor.store(requestVote.candidateId);   
      return;  
    } 
  }

  ret.voteGranted = false;
  ret.term = currentTerm.load();
  return;    
 
}

void send_vote(int ID, request_vote_reply* ret, const request_vote_args& requestVote) {
  rpcServer[ID]->request_vote(ret[ID], requestVote);
  return;
}


void send_request_votes() {
  if(role.load() != 1) {
    std::cerr << "Not a Candidate !!" << std::endl;
    return;
  }

  // requestVote init
  request_vote_args requestVote;
  requestVote.term = currentTerm.load();
  requestVote.candidateId = myID;
  pthread_rwlock_rdlock(&raftloglock);
  int index = raftLog.size() - 1;
  requestVote.lastLogIndex = index;
  requestVote.lastLogTerm = raftLog[index].term;
  pthread_rwlock_unlock(&raftloglock);

  request_vote_reply ret[NODE_NUM];

  std::thread* requestThread = nullptr;

  for(int i = 0; i < NODE_NUM; i++) {
    if(i == myID) {
      ret[i].voteGranted = true;
      continue;
    }
    ret[i].voteGranted = false;
    std::cout << "send vote request to " << i << std::endl;
    // rpcServer[i]->request_vote(ret[i], requestVote);

    requestThread = new std::thread(send_vote, i, ret, requestVote);
    requestThread->detach();
  }

  time(&last_election);

  // random election timeout in [T, 2T] (T >> RTT)
  srand (time(NULL));
  int real_timeout = rand() % ELECTION_TIMEOUT + ELECTION_TIMEOUT;

  while(1) {
    if(role.load() != 1) {
      std::cerr << "Have received AppendEntries, convert to a follower !!" << std::endl;
      return;
    }
    int count = 0;
    time_t curr = time(NULL);
    if(curr - last_election > real_timeout) {
      break;
    }
    for(int i = 0; i < NODE_NUM; i++) {
      if(ret[i].voteGranted == true) {
        count++;
      }
    }
    if(count >= MAJORITY) {
      toLeader();

      // new thread???
      std::thread(send_appending_requests).detach();
      return;
    }
  }

  if(role.load() != 1) {
    std::cerr << "Have received AppendEntries, convert to a follower !!" << std::endl;
    return;
  }

  toCandidate();
  std::thread(send_request_votes).detach();
  // send_request_votes();
  
  return;
}




bool check_prev_entries(int prev_term, int prev_index){  // ret true if sth wrong
    if (prev_index == -1 && raftLog.empty()){
        return false;
    } else if(prev_index >= 0 && prev_index<=(int)raftLog.size()-1){
        if(prev_term == raftLog[prev_index].term){
            return false;
        }
    }
    return true;
}

void append_logs(const std::vector<entry>& logs, int idx){
  if(logs.empty() == true) {
    return;
  }
  // not idx is the appending entries' prev log index
  
  // conflict 
  if ((int)raftLog.size() - 1 >= idx + 1){ // note if follower has idx size-1, ae has idx idx + 1
      ServerStore::remove_log(idx + 1);
      raftLog.erase(raftLog.begin() + idx + 1, raftLog.end());
  }

  ServerStore::append_log(logs);
  raftLog.insert(raftLog.end(), logs.begin(), logs.end());
}

void applyToStateMachine() {
  while (commitIndex > lastApplied){
    int newApplied = lastApplied + 1;
    if (raftLog[lastApplied].command == 1){
      ServerStore::write(raftLog[newApplied].address, raftLog[newApplied].content);
    }
    lastApplied++;
  }
  return;
}

void raft_rpcHandler::append_entries(append_entries_reply& ret, const append_entries_args& appendEntries) {
  std::cout << "append entries starts" << std::endl;

  // if(entryNum > 0){
  //     printf("append_entries: term: %d | leaderid: %d\n",appendEntries.term, votedFor.load());
  // }
  if(appendEntries.term < currentTerm.load() || check_prev_entries(appendEntries.prevLogTerm, appendEntries.prevLogIndex)){
      time(&last_append);
      ret.term = currentTerm.load();
      ret.success = false;
      return;
      
  } 

  // when term >= currentTerm: toFollower
  toFollower(appendEntries.term);
  leaderID.store(appendEntries.leaderId);

  append_logs(appendEntries.entries, appendEntries.prevLogIndex);
  if(commitIndex < appendEntries.leaderCommit){
    commitIndex = std::min(appendEntries.leaderCommit, (int)raftLog.size()-1);
  }
  applyToStateMachine();
  
  ret.success = true;
  ret.term = currentTerm.load();
  return;
}

void send_appending(int ID, append_entries_reply& ret, const append_entries_args& appendEntry) {
  rpcServer[ID]->append_entries(ret, appendEntry);
  return;
}

void send_appending_requests(){  // this is the sender
    // todo: add multi threaded implementation
    if(role.load() != 1) {
        std::cerr << "Not a Candidate !!" << std::endl;
        return;
    }
    int ack_success = 0;  // need to be concurrent one with load/fetch_add

    std::thread* appendThread = nullptr;

    append_entries_args curr_args;
    append_entries_reply curr_ret;

    for (int i = 0; i < NODE_NUM; i++) {
        if(i == myID) {
            continue;
        }
        // for each server, need to lock the raftlog
        // TODO: check the index correctness
        int curr_entry = (int)raftLog.size() - 1;  // note the index starts from zero nextIndex[] ???
        
        curr_args.term = currentTerm.load();

        
        while(curr_entry >= 0){
            
            curr_args.prevLogIndex = curr_entry - 1;
            if (curr_entry > 0){
                curr_args.prevLogTerm = raftLog[curr_args.prevLogIndex].term;
            } else{
                curr_args.prevLogTerm = 0;
            }
            curr_args.leaderId = myID;
            curr_args.leaderCommit = commitIndex;
            curr_args.entries = {raftLog.begin() + curr_entry, raftLog.end()};
            //
            rpcServer[i]->append_entries(curr_ret, curr_args);
            // appendThread = new std::thread(send_appending, i, curr_ret, curr_args);
            // appendThread->detach();
            if (curr_ret.success){
                ack_success++;
                break;
            }
            if(curr_ret.term > currentTerm.load()){
                // todo: ??? what happened ??? I am not leader???
            }
            //
            curr_entry--;
        }
    }
}

void start_raft_server(int id) {
  if(id < 0 || id > 2) {
    std::cout << "unknown id" << std::endl;
    return;
  }
  std::cout << "Starting Raft Server at " << raftPort[id] << std::endl;
  ::std::shared_ptr<raft_rpcHandler> handler(new raft_rpcHandler());
  ::std::shared_ptr<TProcessor> processor(new raft_rpcProcessor(handler));
  ::std::shared_ptr<TServerTransport> serverTransport(new TServerSocket(raftPort[id]));
  ::std::shared_ptr<TTransportFactory> transportFactory(new TBufferedTransportFactory());
  ::std::shared_ptr<TProtocolFactory> protocolFactory(new TBinaryProtocolFactory());
  std::shared_ptr<ThreadFactory> threadFactory = std::shared_ptr<ThreadFactory>(new ThreadFactory());
  std::shared_ptr<ThreadManager> threadManager = ThreadManager::newSimpleThreadManager(RAFT_SERVER_WORKER);

  TThreadPoolServer raft_server(processor, serverTransport, transportFactory, protocolFactory, threadManager);

  threadManager->threadFactory(threadFactory);
  threadManager->start();
  raft_server.serve();
}


void server_init() {
  pthread_rwlock_init(&rolelock, NULL);

  pthread_rwlock_init(&raftloglock, NULL);

  // start storage
  ServerStore::init(myID);

  leaderID.store(-1);

  int term = 0, vote = 0;
  ServerStore::read_state(&term, &vote);

  toFollower(term);

}

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cout << "Usage: ./server <my_node_id> " << std::endl;
    return 1;
  }
  myID = std::atoi(argv[1]);
  server_init();

  // raft_rpc_init();
  // log store test

  entry logEntry;
  logEntry.command = 1;
  logEntry.term = 2;
  logEntry.address = 333;
  stringGenerator(logEntry.content, BLOCK_SIZE);

  std::cout << logEntry.content << std::endl;
  // int ret = ServerStore::append_log(logEntry);



  // num_write_requests.store(0);



  // // start pb server in background
  // std::thread pb(start_pb_server);

  // // If backup, attempt to connect to primary. We assume node 0 is primary
  // if (!is_primary.load()) {
  //   int primary_id = std::stoi(argv[2]);
  //   connect_to_primary(addr(primary_id), pb_port(primary_id));
  // }

  // // start blob server
  // std::thread blob(start_blob_server);

  // // check for primary failure
  // if (!is_primary.load()) {
  //   while (true) {
  //     sleep(HB_FREQ);
  //     time_t curr = time(NULL);
  //     if (curr - last_heartbeat > HB_FREQ * 2) {
  //       std::cout << "Primary Failure" << std::endl;
  //       is_primary.store(true);
  //       break;
  //     }
  //   }
  // }

  // blob.join();
  // pb.join();
  return 0;
}

