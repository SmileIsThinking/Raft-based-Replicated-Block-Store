enum Errno {
    SUCCESS = 0,
    NOT_LEADER = 1,
    NO_LEADER = 2,
    UNEXPECTED = 99,
}

struct request_ret {
    1: Errno rc,
    2: string value,
    3: i32 node_id,
}

# struct write_ret {
#     1: Errno rc,
#     2: i32 node_id,
# }

service blob_rpc {
    void ping(),
    request_ret read(1:i64 addr),
    request_ret write(1:i64 addr, 2:string value),

    oneway void compareLogs(),
    oneway void compareBlock(1:i64 addr),
}

enum PB_Errno {
    SUCCESS = 0,
    IS_LEADER = 1,
    NOT_LEADER = 2,
    BACKUP_EXISTS = 3,
    UNEXPECTED = 99,
}

/* ===================================== */
/* Raft RPC  */
/* ===================================== */

enum Raft_Errno {
    SUCCESS = 0,
    IS_LEADER = 1,
    NOT_LEADER = 2,
    NO_LEADER = 3,
    UNEXPECTED = 99,
}

struct client_request_reply {
    1: string value;
    2: i32 err;
}

struct request_vote_args {
    1: i32 term,
    2: i32 candidateId,
    3: i32 lastLogIndex,
    4: i32 lastLogTerm,
}

struct request_vote_reply {
    1: i32 term,
    2: bool voteGranted,
}


struct entry {
    // 0: read, 1: write
    1: i32 command,
    2: i32 term,   
    3: i64 address,
    4: string content,
}

struct append_entries_args {
    1: i32 term,
    2: i32 leaderId,
    3: i32 prevLogIndex,
    4: i32 prevLogTerm,
    5: list<entry> entries,
    6: i32 leaderCommit,
}

struct append_entries_reply {
    1: i32 term,
    2: i32 success,
    // initialized to -1
    // 0: false
    // 1: true
}

service raft_rpc {
    void ping(1: i32 other),

    request_vote_reply request_vote(1:request_vote_args requestVote),
    append_entries_reply append_entries(1:append_entries_args appendEntry),
    oneway void compareTest(1: list<entry> leaderLog, 2: i32 leaderTerm, 3: i32 leaderVote),
    oneway void blockTest(1: i64 address, 2: string value),
}
