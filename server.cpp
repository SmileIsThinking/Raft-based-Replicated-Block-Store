#include "server.h"
#include "server_store.h"
#include <iostream>
#include <unistd.h>
#include <string>
#include <random>

#include "util.h"

void blob_rpcHandler::read(read_ret& _return, const int64_t addr) {
  // Read from a backup
  if (!is_primary.load()) {
    std::cerr << "read: Not a Primary" << std::endl;
    _return.rc = Errno::BACKUP;
    return;
  }

  // TODO: check for return values  
  if (ServerStore::read(addr, _return.value) == 0) {
    _return.rc = Errno::SUCCESS;
    return;
  }

  _return.rc = Errno::UNEXPECTED;
  return;
}

Errno::type blob_rpcHandler::write(const int64_t addr, const std::string& value) {
  // Write to a backup
  if (!is_primary.load()) {
    std::cerr << "write: Not a Primary" << std::endl;
    return Errno::BACKUP;
  }

retry:
  // creating a copy to backup, block write requests
  while (pending_backup.load());
  // exist write requests, block whole file read for creating new backups
  num_write_requests.fetch_add(1, std::memory_order_acq_rel);
  // in case of race condition
  if (pending_backup.load()) {
    num_write_requests.fetch_sub(1, std::memory_order_acq_rel);
    goto retry;
  }

  int64_t seq;
  int result = ServerStore::write(addr, value, seq);

  // done with writing
  num_write_requests.fetch_sub(1, std::memory_order_acq_rel);

  if (result != 0)
    return Errno::UNEXPECTED;
  if (!has_backup.load())
    return Errno::SUCCESS;
  // TODO: check for return values  
  try {
    PB_Errno::type reply = other->update(addr, value, seq);
    if (reply == PB_Errno::SUCCESS)
      return Errno::SUCCESS;
    else
      // should not happen
      return Errno::UNEXPECTED;
  } catch (TTransportException) {
    std::cerr << "Backup Failure" << std::endl;
    has_backup.store(false);
    return Errno::SUCCESS;
  }
}

PB_Errno::type pb_rpcHandler::update(const int64_t addr, const std::string& value, const int64_t seq) {
  static std::atomic<int64_t> curr_seq(0);

  if (is_primary.load()) {
    std::cerr << "update: Not a Primary" << std::endl;
    return PB_Errno::NOT_BACKUP;
  }
  // Wait for previous updates to complete
  while (curr_seq.load() != seq);
  int64_t tmp;
  int result = ServerStore::write(addr, value, tmp);
  // let subsequent requests run
  curr_seq.fetch_add(1, std::memory_order_acq_rel);
  // TODO: handle write failures
  if (result == 0)
    return PB_Errno::SUCCESS;
  else
    // Backup fail to make copy, crash to avoid inconsistency
    exit(1);
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
    _return.rc = PB_Errno::NOT_PRIMARY;
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



// TODO: follower timeout
void toFollower(int term) {
  pthread_rwlock_wrlock(&rolelock);

  role.store(2);
  int vote = -1;

  ServerStore::write_state(term, vote);
  currentTerm.store(term);
  votedFor.store(vote); 

  pthread_rwlock_unlock(&rolelock);
}

void toCandidate() {
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
    }else if(requestVote.lastLogTerm == currentTerm.load() && requestVote.lastLogIndex >= raftLog.size()) {
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

  // TODO: multi-thread requests
  std::thread* requestThread = nullptr;

  for(int i = 0; i < NODE_NUM; i++) {
    if(i == myID) {
      ret[i].voteGranted = true;
      continue;
    }
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

bool check_prev_entries(int prev_term, int prev_index){
    if (prev_index == 0 && raftLog.empty()){
        return true;
    } else if(prev_index > 0 && prev_index<=raftLog.size()){
        if(prev_term == raftLog[prev_index-1].term){  //todo: -1 correct? based on implementation if init idx = 0, first log = 1, correct
            return true;
        }
    }
    return false;
}

void append_logs(const std::vector<entry>& logs, int idx){
    if (raftLog.size() > idx){ //todo: check index
        raftLog.erase(raftLog.begin() + idx, raftLog.end());
    }
    raftLog.insert(raftLog.end(), logs.begin(), logs.end());
}

void raft_rpcHandler::append_entries(append_entries_reply& ret, const append_entries_args& appendEntries) {
  std::cout << "append entries starts" << std::endl;

  if(appendEntries.term > currentTerm.load()) {
    toFollower(appendEntries.term);
  }

  if(entryNum > 0){
      printf("append_entries: term: %d | leaderid: %d\n",appendEntries.term, votedFor.load());
  }
  //todo: update timer for local node
  if(appendEntries.term < currentTerm.load() || check_prev_entries(appendEntries.prevLogTerm, appendEntries.prevLogIndex)){
      ret.success = false;
  } else{
      // success
      //todo:set current as follower, if curr node is proposing to be the leader
      // currentTerm.store(appendEntries.term);
      // votedFor = appendEntries.leaderId;
      toFollower(appendEntries.term);

      //todo: append/replace entries beginning the index and CHECK the TYPE DEFINE
      append_logs(appendEntries.entries, appendEntries.prevLogIndex);
      entryNum = appendEntries.leaderCommit; //todo: correct?
      ret.success = true;
  }
  ret.term = currentTerm;
  // todo: write to disk implementation
  // ServerStore::append_log();
}

void send_appending_requests(){
    if(role.load() != 1) {
        std::cerr << "Not a Candidate !!" << std::endl;
        return;
    }
    for (int i = 0; i < NODE_NUM; i++) {
        if(i == myID) {
            continue;
        }
        // for each server, need to lock the raftlog
        // todo: check the index correctness
        int curr_entry = raftLog.size();
        append_entries_args curr_args;
        curr_args.term = currentTerm.load();
        while(curr_entry-- > 0){
            append_entries_reply curr_ret;
            curr_args.prevLogIndex = curr_entry - 1;
            curr_args.prevLogTerm = raftLog[curr_args.prevLogIndex].term;
            curr_args.leaderId = myID;
            curr_args.entries = {raftLog.begin() + curr_args.prevLogIndex - 1, raftLog.end()};
            rpcServer[i]->append_entries(curr_ret, curr_args);
            if (curr_ret.success){
                break;
            }
            if(curr_ret.term > currentTerm.load()){
                // todo: ??? what happened ???
            }
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


void server_init(int ID) {
  pthread_rwlock_init(&rolelock, NULL);

  pthread_rwlock_init(&raftloglock, NULL);

  /* raft init */
  myID = ID;


  // TODO: read from persistent store
  int term;
  toFollower(term);
}

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cout << "Usage: ./server <my_node_id> " << std::endl;
    return 1;
  }

  server_init(std::atoi(argv[1]));

  // raft_rpc_init();



  is_primary.store(argc == 2);
  printf("%s\n", is_primary ? "Primary" : "Backup");
  int my_id = std::stoi(argv[1]);
  my_addr = addr(my_id);
  my_blob_port = blob_port(my_id);
  my_pb_port = pb_port(my_id);

  has_backup.store(false);
  pending_backup.store(false);
  num_write_requests.store(0);

  // start storage
  ServerStore::init(my_id);

  // start pb server in background
  std::thread pb(start_pb_server);

  // If backup, attempt to connect to primary. We assume node 0 is primary
  if (!is_primary.load()) {
    int primary_id = std::stoi(argv[2]);
    connect_to_primary(addr(primary_id), pb_port(primary_id));
  }

  // start blob server
  std::thread blob(start_blob_server);

  // check for primary failure
  if (!is_primary.load()) {
    while (true) {
      sleep(HB_FREQ);
      time_t curr = time(NULL);
      if (curr - last_heartbeat > HB_FREQ * 2) {
        std::cout << "Primary Failure" << std::endl;
        is_primary.store(true);
        break;
      }
    }
  }

  blob.join();
  pb.join();
  return 0;
}
