# 739-project-4
[Repo link](https://github.com/SmileIsThinking/Raft-based-Replicated-Block-Store)
## Run the code
To compile both the client and server:
```
make
```
To run the client :    
```
./client
```
To run the primary server :   
```
./server <my_node_id>
```
The hostname and port used for all nodes are configured in `include.h`. 

## Implementation
* Client interaction

The client library resides in `block_store.h` and `block_store.cpp`. `block_store.h` can be included in applications (e.g., `client.cpp`). `block_store.cpp` contains the actual implementation for read and write. It is responsible  for sending the appropriate RPC requests to the leader. 

* Thrift RPCs

`rpc.thrift` specifies the RPCs we used for this project. We split the RPCs into two services: `blob_rpc` corresponds to the block storage service where the client library would send RPC requests to the primary node; `raft_rpc` includes all RPCs needed for Raft replication scheme. 

* Leader election 

For the leader election, we design a function ```send_request_votes()``` for candidate to send RequestVote RPCs to follower and RPC handler ```request_vote``` for follower to response. We finish the highlight part in the following figure.

* Log replication

Once a leader is elected, it appends the command to its log as a new entry, then started to send AppendEntries RPCs(implemented by function ```send_appending_requests()```) in parallel to each of the other servers to replicate the entry. The RPC handler is implemented in ```append_entries``.

## Testing & Measurement
Note we have two versions of code, one of which contains concurrent optimization for sending requests but not fully tested for concurrency. (So far we have not met inconsistency due to this optimization but not confirmed for correctness)
### Correstness
We write all test cases in `client.cpp`.
- test1: test availability by crashing one node and checking users can still read written data.
- test2: test strong consistency by crashing one node and overwriting the block.
- test3: test concurrency with multiple clients by crashing or not crashing server.

More tests are described in our report with test cases and below is the link to the demo video: 
- https://drive.google.com/drive/folders/11E-LPlVXfZXQGxDksXzikIm2axdYN8At?usp=sharing
- The above duplicate/delayed append entry packets and check data consistency after receiving these packets (Test4)
- 2 election result with predifined config (Test3-1,3-2)
### Performance Test
* We evaluated the read/write latency for aligned address and non-aligned address.
* Compare latency for the above case when one server fails and all work well.
* Test the recovery time when leader crashes.
