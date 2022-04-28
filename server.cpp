#include "server.h"
#include "server_store.h"

#include "util.h"

thread_local std::string local_backup_hostname;
thread_local int local_backup_port;
thread_local std::shared_ptr<pb_rpcIf> other;

void blob_rpcHandler::read(read_ret& _return, const int64_t addr) {
  // Read from a backup
  if (!is_primary.load()) {
    std::cerr << "read: Not a Primary" << std::endl;
    _return.rc = Errno::BACKUP;
    return;
  }

  // TODO: check for return values
  if (ServerStore::read(addr, _return.value) >= 0) {
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

  std::vector<int64_t> seq;
  int result = ServerStore::write(addr, value, seq);

  // done with writing
  num_write_requests.fetch_sub(1, std::memory_order_acq_rel);

  if (result < 0)
    return Errno::UNEXPECTED;
  if (!has_backup.load())
    return Errno::SUCCESS;
  // TODO: check for return values
  try {
    if (local_backup_hostname != backup_hostname || local_backup_port != backup_port) {
      local_backup_hostname = backup_hostname;
      local_backup_port = backup_port;
      std::shared_ptr<TTransport> socket(new TSocket(local_backup_hostname, local_backup_port));
      std::shared_ptr<TTransport> transport(new TBufferedTransport(socket));
      std::shared_ptr<TProtocol> protocol(new TBinaryProtocol(transport));
      other = std::make_shared<pb_rpcClient>(protocol);
      transport->open();
      other->ping();
    }
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

PB_Errno::type pb_rpcHandler::update(const int64_t addr, const std::string& value, const std::vector<int64_t>& seq) {
  if (is_primary.load()) {
    std::cerr << "update: Not a Primary" << std::endl;
    return PB_Errno::NOT_BACKUP;
  }
  // Wait for previous updates to complete
  ServerStore::wait_prev(addr, seq);
  std::vector<int64_t> tmp;
  int result = ServerStore::write(addr, value, tmp);
  // TODO: handle write failures
  if (result >= 0)
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

void send_heartbeat(std::shared_ptr<pb_rpcIf> other) {
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

  backup_hostname = hostname;
  backup_port = port;
  std::shared_ptr<TTransport> socket(new TSocket(hostname, port));
  std::shared_ptr<TTransport> transport(new TBufferedTransport(socket));
  std::shared_ptr<TProtocol> protocol(new TBinaryProtocol(transport));
  auto other = std::make_shared<pb_rpcClient>(protocol);
  transport->open();
  other->ping();

  // block future write operations
  pending_backup.store(true);
  // block wait for current write operations to complete
  while (num_write_requests.load() != 0);
  // full file read
  ServerStore::full_read(_return.content, _return.seq);

  std::thread(send_heartbeat, other).detach();
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
  auto other = std::make_shared<pb_rpcClient>(protocol);
  transport->open();

  other->ping();

  struct new_backup_ret ret;
  other->new_backup(ret, my_addr, my_pb_port);
  if (ret.rc != PB_Errno::SUCCESS)
    exit(0);
  if (ServerStore::full_write(ret.content, ret.seq) < 0) {
    std::cout << "fail to write to backup" << std::endl;
    exit(1);
  }
  other->new_backup_succeed();
}

int main(int argc, char** argv) {
  // TODO: change the logic for which is primary
  if (argc != 2 && argc != 3) {
    std::cout << "Usage: ./server <my_node_id> [primary_node_id]" << std::endl;
    return 1;
  }

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
    // wait for PB server to start
    sleep(1);
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
