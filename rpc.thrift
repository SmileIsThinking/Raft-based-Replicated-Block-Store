enum Errno {
    SUCCESS = 0,
    BACKUP = 1,
    UNEXPECTED = 99,
}

struct read_ret {
    1: Errno rc,
    2: string value,
}

service blob_rpc {
    void ping(),
    read_ret read(1:i64 addr),
    Errno write(1:i64 addr, 2:string value),
}

enum PB_Errno {
    SUCCESS = 0,
    NOT_BACKUP = 1,
    NOT_PRIMARY = 2,
    BACKUP_EXISTS = 3,
    UNEXPECTED = 99,
}

struct new_backup_ret {
    1: PB_Errno rc,
    2: string content,
}

service pb_rpc {
    void ping(),
    new_backup_ret new_backup(1:string hostname, 2:i32 port),
    oneway void new_backup_succeed();
    PB_Errno update(1:i64 addr, 2:string value, 3:i64 seq),
    oneway void heartbeat(),
}

/* ===================================== */
/* Raft RPC  */
/* ===================================== */

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
    2: bool success,
}

service raft_rpc {
    void ping(),
    request_vote_reply request_vote(1:request_vote_args requestVote),
    append_entries_reply append_entries(1:append_entries_args appendEntry),
}
