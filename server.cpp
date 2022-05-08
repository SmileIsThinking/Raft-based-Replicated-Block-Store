#include "server.h"
#include "server_store.h"
#include <iostream>
#include <unistd.h>
#include <string>
#include <random>






void pb_rpcHandler::heartbeat() {
  // primary receive heartbeat - unexpected behavior, ignore the result
  if (is_primary.load())
    return;
  // note that the other server (primary) is still alive
  std::cout << "Heartbeat received" << std::endl;
  last_heartbeat = getMillisec();
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
  my_blob_port = cliPort[myID];
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

  new_backup_ret ret;
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


// TODO: seq num
void blob_rpcHandler::read(request_ret& _return, const int64_t addr) {
  // not a leader
  // TODO: what if currently there is no leader

  // TODO: hangout
  if (leaderID.load() == -1) {
    std::cerr << "No leader" << std::endl;
    _return.rc = Errno::NO_LEADER;
    return;
  }

  if (role.load() != 0) {
    std::cerr << "read: Not a leader" << std::endl;
    _return.rc = Errno::NOT_LEADER;
    _return.node_id = leaderID.load();
    return;
  }

  entry raftEntry;
  raftEntry.command = 0;
  raftEntry.address = addr;
  raftEntry.term = currentTerm.load();

  new_request(_return, raftEntry);
  return;

  // _return.rc = Errno::UNEXPECTED;
  // return;
}

void new_request(request_ret& _return, entry e) {
  std::vector<entry> tmpLog;
  tmpLog.emplace_back(e);
  ServerStore::append_log(tmpLog);

  pthread_rwlock_wrlock(&raftloglock);
  raftLog.insert(raftLog.end(), tmpLog.begin(), tmpLog.end());
  int reqIndex = raftLog.size() - 1;

  nextIndex[myID] = reqIndex + 1;
  matchIndex[myID] = reqIndex;  
  pthread_rwlock_unlock(&raftloglock);

  std::string value;
  while(1) {
    if(commitIndex >= reqIndex) {
      if(lastApplied == reqIndex - 1){
        if(e.command == 0) {
          ServerStore::read(e.address, _return.value);
        }else if(e.command == 1) {
          ServerStore::write(e.address, e.content);
        }
        _return.rc = Errno::SUCCESS;
        lastApplied++;
        return;        
      }
    }
  }
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

void blob_rpcHandler::write(request_ret& _return, const int64_t addr, const std::string& value) {
  if (leaderID.load() == -1) {
    std::cerr << "No leader" << std::endl;
    _return.rc = Errno::NO_LEADER;
    return;
  }  
  // Write to not leader
  if (role.load() != 0) {
    std::cerr << "write: Not a leader" << std::endl;
    _return.rc = Errno::NOT_LEADER;
    _return.node_id = leaderID.load();
    return;
  }

  entry raftEntry;
  raftEntry.command = 1;
  raftEntry.address = addr;
  raftEntry.content = value;
  raftEntry.term = currentTerm.load();

  new_request(_return, raftEntry);
  return;  

// retry:

//   // creating a copy to followers, block write requests
//   while (pending_candidate.load());
//   // exist write requests, block whole file read for creating new backups
//   num_write_requests.fetch_add(1, std::memory_order_acq_rel);
//   // in case of race condition
//   if (pending_candidate.load()) {
//     num_write_requests.fetch_sub(1, std::memory_order_acq_rel);
//     goto retry;
//   }

//   int64_t seq;
//   int result = ServerStore::write(addr, value);

//   // done with writing
//   num_write_requests.fetch_sub(1, std::memory_order_acq_rel);

//   if (result != 0){
//     _return.rc = Errno::UNEXPECTED;
//     return ;
//   }
    
//   // TODO : write request
//   for(int i = 0; i < NODE_NUM; i++) {
//     if(i == myID) {
//       continue;
//     }
//     std::cout << "send write value to replicas " << i << std::endl;
//     // PB_Errno::type reply = rpcServer[i]->update(addr, value, seq);
//     // if (reply == PB_Errno::SUCCESS){
//     //   _return.rc = Errno::SUCCESS;
//     //   return ;
//     // }
//     // else{
//     //   _return.rc = Errno::UNEXPECTED;
//     //   return ;
//     // }
//     return;
//       // should not happen
      
//   }
  
}

void appendTimeout() {
  
  std::cout << "role: " << role.load() << std::endl;
  while(1) {
    // std::cout << "append timeouting" << std::endl;
    if(role.load() != 2) {
      break;
    }
    int64_t curr = getMillisec();
    // std::cout << "curr time: " << curr << std::endl;
    if(curr - last_election > REAL_TIMEOUT) {
      std::cout << "election timeout" << std::endl;
      break;
    }
  }

  toCandidate();
}

void toFollower(int term) {
  std:: cout << "TO FOLLOWER !!!" << std::endl;
  // time(&last_election);
  // std::cout << "last append: " << last_election << std::endl;
  pthread_rwlock_wrlock(&rolelock);

  role.store(2);
  int vote = -1;
  ServerStore::write_state(term, vote);
  currentTerm.store(term);
  votedFor.store(vote); 

  std::thread(appendTimeout).detach();

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

  std::thread(send_request_votes).detach();
}

void leaderHeartbeat() {
  int i = 1;

  while(1) {
    if(role.load() != 0) {
      break;
    }   
    std::cout << "Send heartbeat " <<  i << " !" << std::endl;
    i++;
    std::thread(send_appending_requests).detach();
    std::this_thread::sleep_for(std::chrono::milliseconds(HB_FREQ));
  }
  return;
}

void toLeader() {
  std:: cout << "TO LEADER !!!" << std::endl;
  pthread_rwlock_wrlock(&rolelock);

  role.store(0);
  leaderID.store(myID);
  pthread_rwlock_unlock(&rolelock);

  pthread_rwlock_rdlock(&raftloglock);
  int index = (int)raftLog.size() - 1;
  pthread_rwlock_unlock(&raftloglock);
  for(int i = 0; i < NODE_NUM; i++) {
    if(i == myID) {
      nextIndex[i] = index + 1;
      matchIndex[i] = index;
    }else {
      nextIndex[i] = index + 1;
      matchIndex[i] = -1;
    }

  }

  std::thread(leaderHeartbeat).detach();
}

void raft_rpcHandler::request_vote(request_vote_reply& ret, const request_vote_args& requestVote) {
  std::cout << "Receive Reuqest Vote RPC" << std::endl;

  if(requestVote.term > currentTerm.load()) {
    std::cout << "Get larger term!" << std::endl;
    toFollower(requestVote.term);
  }

  if(requestVote.term < currentTerm.load()) {
    ret.voteGranted = false;
    ret.term = currentTerm.load();
    return;
  }


  
  int vote = votedFor.load();
  std::cout << vote << std::endl;
  if(vote == -1 || vote == requestVote.candidateId) {
    // lastLogTerm == -1: no log
    if(requestVote.lastLogTerm == -1 || requestVote.lastLogTerm > currentTerm.load()) { 
      ret.voteGranted = true;
      votedFor.store(requestVote.candidateId);  
      return;      
    }else if(requestVote.lastLogTerm == currentTerm.load() && requestVote.lastLogIndex >= (int)raftLog.size()) {
      ret.voteGranted = true;
      last_election = getMillisec();
      REAL_TIMEOUT = dist(gen) + ELECTION_TIMEOUT;
      votedFor.store(requestVote.candidateId);   
      return;  
    } 
  }
  std::cout << "Myvote " << votedFor.load() << std::endl;
  ret.voteGranted = false;
  ret.term = currentTerm.load();
  // std::cout << "Mvote " << vote << std::endl;
  return;    
 
}

/*
pure virtual method called
terminate called without an active exception

If you encounter this error, be aware of the deleting objects issue
https://tombarta.wordpress.com/2008/07/10/gcc-pure-virtual-method-called/
*/
void send_vote(int ID, request_vote_reply* ret, const request_vote_args& requestVote) {
  try {

    rpcServer[ID]->request_vote(ret[ID], requestVote);
  }catch(apache::thrift::transport::TTransportException) {
    std::cout << "Node: " << ID << "is DEAD!" << std::endl;
    return;
  }
  
  return;
}


void send_request_votes() {
  if(role.load() != 1) {
    std::cerr << "Not a Candidate !!" << std::endl;
    return;
  }

  std::cout << "Send Request Votes to others!" << std::endl;
  // time(&last_election);
  // requestVote init
  request_vote_args requestVote;
  requestVote.term = currentTerm.load();
  requestVote.candidateId = myID;
  pthread_rwlock_rdlock(&raftloglock);
  int index = raftLog.size() - 1;
  requestVote.lastLogIndex = index;
  if(index < 0) {
    requestVote.lastLogTerm = -1;
  }else {
    requestVote.lastLogTerm = raftLog[index].term;
  }
  
  pthread_rwlock_unlock(&raftloglock);

  request_vote_reply ret[NODE_NUM];
  // request_vote_reply 

  std::thread* requestThread = nullptr;

  for(int i = 0; i < NODE_NUM; i++) {
    if(i == myID) {
      ret[i].voteGranted = true;
      continue;
    }else {
      ret[i].voteGranted = false;
    }
    
    std::cout << "send vote request to " << i << std::endl;
    // rpcServer[i]->request_vote(ret[i], requestVote);

    // detach: variable liveness should be guaranteed!
    requestThread = new std::thread(send_vote, i, ret, requestVote);
    // TODO: multi-thread
    requestThread->join();
  }

  last_election = getMillisec();
  REAL_TIMEOUT = dist(gen) + ELECTION_TIMEOUT;
  // std::cout << "REAL TIMEOUT: " << REAL_TIMEOUT << std::endl;
  // random election timeout in [T, 2T] (T >> RTT)
  // srand (time(NULL));


  while(1) {
    // std::cout << "My role: " << role.load() << std::endl;
    if(role.load() == 2) {
      std::cout << "From Candidate To Follower, maybe received AppendEntry" << std::endl;
      return;
    }
    int count = 0;
    int64_t curr = getMillisec();
    // std::cout << "Right now the time: " << curr << std::endl;
    // std::cout << "Last election: " << last_election << std::endl;
    // std::cout << "TIMEOUT: " << REAL_TIMEOUT << std::endl;
    if(curr - last_election > REAL_TIMEOUT) {
      break;
    }
    for(int i = 0; i < NODE_NUM; i++) {
      if(ret[i].voteGranted == true) {
        count++;
      }
    }
    if(count >= MAJORITY) {
      std::cout << "MAJORITY" << std::endl;
      if(role.load() != 0) {
        toLeader();
      }
  
      // new thread???
      // std::thread(send_appending_requests).detach();
      return;
    }
  }

  if(role.load() == 2) {
    std::cout << "From Candidate To Follower, maybe received AppendEntry" << std::endl;
    return;
  }

  toCandidate();
  
  // send_request_votes();
  
  return;
}


// ret true if sth wrong
bool check_prev_entries(int prev_term, int prev_index){  
    if (prev_index == -1 && raftLog.empty()){
        return false;
    } else if(prev_index >= 0 && prev_index<=(int)raftLog.size()-1){
        if(prev_term == raftLog[prev_index].term){
            return false;
        }
    }
    return true;
}

// ALERT: idx == -1 if the log is emtpy. But, it's ok in this implementation.
void append_logs(const std::vector<entry>& logs, int idx){
  if(logs.empty()) {
    return;
  }
  // not idx is the appending entries' prev log index
  
  // conflict 
  if ((int)raftLog.size() - 1 >= idx + 1){ // note if follower has idx size-1, ae has idx: idx + 1
      ServerStore::remove_log(idx + 1);
      raftLog.erase(raftLog.begin() + idx + 1, raftLog.end());
  }

  ServerStore::append_log(logs);

  pthread_rwlock_wrlock(&raftloglock);
  raftLog.insert(raftLog.end(), logs.begin(), logs.end());
  pthread_rwlock_unlock(&raftloglock);
}



void raft_rpcHandler::append_entries(append_entries_reply& ret, const append_entries_args& appendEntries) {

  std::cout << "Receive Append Entries RPC" << std::endl;
  if(role.load() == 0) {
    std::cout << "I am a leader now, reject append entries" << std::endl;
    return;
  }
  last_election = getMillisec();
  REAL_TIMEOUT = dist(gen) + ELECTION_TIMEOUT;

  // when term >= currentTerm: toFollower
  if(appendEntries.term > currentTerm.load()) {
    std::cout << "Get larger term!" << std::endl;
    toFollower(appendEntries.term);
    return;
  }
  // if(entryNum > 0){
  //     printf("append_entries: term: %d | leaderid: %d\n",appendEntries.term, votedFor.load());
  // }
  if(appendEntries.term < currentTerm.load() || check_prev_entries(appendEntries.prevLogTerm, appendEntries.prevLogIndex)){
      ret.term = currentTerm.load();
      ret.success = 0;
      return;
  } 


  // not a leader and not a follower
  if(role.load() == 1) {
    toFollower(appendEntries.term);
  }
 
  leaderID.store(appendEntries.leaderId);

  append_logs(appendEntries.entries, appendEntries.prevLogIndex);
  if(commitIndex < appendEntries.leaderCommit){
    commitIndex = std::min(appendEntries.leaderCommit, (int)raftLog.size()-1);
  }
  
  ret.success = 1;
  ret.term = currentTerm.load();
  return;
}

void send_appending(int ID, append_entries_reply* ret, const append_entries_args* appendEntry) {
  try {
    rpcServer[ID]->append_entries(ret[ID], appendEntry[ID]);
  }catch(apache::thrift::transport::TTransportException) {
    std::cout << "Node: " << ID << "is DEAD!" << std::endl;
    ret[ID].success = -2;
    return;
  }
  
  return;
}

void send_appending_requests(){  
    if(role.load() != 0) {
        std::cerr << "Not a Leader !!" << std::endl;
        return;
    }

    std::cout << "Send Appending entries to others!" << std::endl;

    std::thread* appendThread = nullptr;
    append_entries_args preEntry;
    preEntry.term = currentTerm.load();
    preEntry.leaderId = myID;
    preEntry.leaderCommit = commitIndex;    
    preEntry.entries = std::vector<entry>();

    // learn a lesson
    // https://stackoverflow.com/
    // questions/201101/how-to-initialize-all-members-of-an-array-to-the-same-value
    append_entries_args appendEntry[NODE_NUM] = {preEntry, preEntry, preEntry};
    append_entries_reply preRet;
    preRet.success = -1;
    append_entries_reply ret[NODE_NUM] = {preRet, preRet, preRet};
    pthread_rwlock_rdlock(&raftloglock);
    int lastIndex = raftLog.size() - 1;
    pthread_rwlock_unlock(&raftloglock);

    for (int i = 0; i < NODE_NUM; i++) {
      if(i == myID) {
          continue;
      }
      
      if(lastIndex >= nextIndex[i]) {
        // potential error
        appendEntry[i].entries = std::vector<entry>(raftLog.begin() + nextIndex[i], raftLog.end());
      }
      appendEntry[i].prevLogIndex = nextIndex[i] - 1;

      // ALERT: stack overflow
      if (appendEntry[i].prevLogIndex < 0) {
        // no log yet! be cautious of stack overflow (log[-1])
        appendEntry[i].prevLogTerm = -1;
      } else {
        appendEntry[i].prevLogTerm = raftLog[appendEntry[i].prevLogIndex].term;
      }
      appendThread = new std::thread(send_appending, i, ret, appendEntry);
      // TODO: Multi-thread
      appendThread->join();
      // appendThread->detach();
    }

      // TODO: think about it, concurrentlly add one with load/fetch_add
    while(1) {
      if(role.load() != 0) {
        std::cerr << "Have received AppendEntries, convert to a follower !!" << std::endl;
        return;
      }
      int ack_num = 0;
      for(int i = 0; i < NODE_NUM; i++) {
        if(i == myID) {
          ack_num++;
          continue;
        }
        // if(ack_num == NODE_NUM - 1) {
        //   break;
        // }
        if(ret[i].success == 1) {
          if(currentTerm.load() < ret[i].term) {
            toFollower(ret[i].term);
            return;
          }
          ack_num++;

          nextIndex[i] = lastIndex + 1;
          matchIndex[i] = nextIndex[i] - 1;      
        }else if(ret[i].success == 0) {
          if(currentTerm.load() < ret[i].term) {
            toFollower(ret[i].term);
            return;
          }
          ack_num++;

          nextIndex[i] = nextIndex[i] - 1;
        }else if(ret[i].success == -2) {
          ack_num++;
        }
      }
      if(ack_num == NODE_NUM) {
        break;
      }
    }
    // std::cout << "Ready to Commit!" << std::endl;
    int N = commitIndex;
    while(1) {
      N++;
      if(N > lastIndex) {
        break;
      }
      int count = 0;

      for(int i = 0; i < NODE_NUM; i++) {
        if(matchIndex[i] >= N) {
          count++;
        }
      }

      if(count >= MAJORITY && raftLog[N].term == currentTerm.load()) {
        commitIndex = N;
      }else {
        break;
      }
    }
    return;
}

void raft_rpcHandler::ping(int other) {
  try {
    std::shared_ptr<TTransport> socket(new TSocket(nodeAddr[other], raftPort[other]));
    std::shared_ptr<TTransport> transport(new TBufferedTransport(socket));
    std::shared_ptr<TProtocol> protocol(new TBinaryProtocol(transport));
    syncInfo[other] = std::make_shared<::apache::thrift::async::TConcurrentClientSyncInfo>();
    rpcServer[other] = std::make_shared<raft_rpcConcurrentClient>(protocol, syncInfo[other]);
    transport->open();
    std::cout << "ping success" << std::endl;
  } catch (apache::thrift::transport::TTransportException) {
    std::cout << "ping fail" << std::endl;
  }
  return;
}

void start_raft_server(int id) {
  if(id < 0 || id > 2) {
    std::cout << "Unknown ID" << std::endl;
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

void raft_rpc_init() {
  for(int i = 0; i < NODE_NUM; i++) {
    if(i == myID) {
      continue;
    }
    try {
      std::shared_ptr<TTransport> socket(new TSocket(nodeAddr[i], raftPort[i]));
      std::shared_ptr<TTransport> transport(new TBufferedTransport(socket));
      std::shared_ptr<TProtocol> protocol(new TBinaryProtocol(transport));
      syncInfo[i] = std::make_shared<::apache::thrift::async::TConcurrentClientSyncInfo>();
      rpcServer[i] = std::make_shared<raft_rpcConcurrentClient>(protocol, syncInfo[i]);
      transport->open();
      rpcServer[i]->ping(myID);
    } catch(apache::thrift::transport::TTransportException) {
      continue;
    }
  }
  return;
}

void server_init(long init_timeout) {

  pthread_rwlock_init(&rolelock, NULL);
  pthread_rwlock_init(&raftloglock, NULL);

  // start storage
  ServerStore::init(myID);
  leaderID.store(-1);

  int term = 0, vote = -1;
  int ret = ServerStore::read_state(&term, &vote);
  if(ret == -1) {
    ServerStore::write_state(term, vote);
    currentTerm.store(term);
    votedFor.store(vote);
  }
  raftLog = ServerStore::read_full_log();
  
  
  raft_rpc_init();
  for(int i = 0; i < (int)raftLog.size(); i++) {
    std::cout << "Index:  " << i << std::endl;
    entry_format_print(raftLog[i]);
  }


  REAL_TIMEOUT = init_timeout > 0 ? init_timeout : (dist(gen) + ELECTION_TIMEOUT);
  toFollower(term);
}



int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) {
    std::cout << "Usage: ./server <my_node_id> <initial timeout>" << std::endl;
    return 1;
  }
  std::cout << "Use <initial timeout> to set which one becomes leader at start" << std::endl;
  myID = std::atoi(argv[1]);
  long init_time_out = 0;
  if(argc > 2){
      init_time_out = atol(argv[2]);
  }


  std::thread blob(start_blob_server);
  std::thread raft(start_raft_server, myID);
  server_init(init_time_out);


  blob.join();
  raft.join();
  std::cout << "why terminate" << std::endl;
  // sleep(10);
  // std::string t;
  // std::cout << "Input terminate if you want to terminate" << std::endl;
  // std::cin.ignore();

  // raft_rpc_init();
  // log store test

  // std::vector<entry> logEntries;
  // entry logEntry;
  // logEntry.command = 1;
  // logEntry.term = 2;
  // logEntry.address = 333;
  // stringGenerator(logEntry.content, BLOCK_SIZE);

  // logEntries.emplace_back(logEntry);

  // entry_format_print(logEntry);
  // // std::cout << "Log num: " << ServerStore::read_log_num() << std::endl;
  // ServerStore::append_log(logEntries);

  // logEntry = ServerStore::read_log(ServerStore::read_log_num()-1);
  // std::cout << "Index: " << ServerStore::read_log_num()-1 << std::endl;
  // entry_format_print(logEntry);



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
  //     int64_t curr = getMillisec();
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

void entry_format_print(entry logEntry) {
  std::cout << "========================" << std::endl;
  std::cout << "Command: " << logEntry.command << std::endl;
  std::cout << "Term: " << logEntry.term << std::endl;
  std::cout << "Address: " << logEntry.address << std::endl;
  std::cout << "Content: " << logEntry.content << std::endl;
  std::cout << "========================" << std::endl;
}