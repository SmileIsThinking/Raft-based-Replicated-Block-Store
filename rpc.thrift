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
    3: list<i64> seq,
}

service pb_rpc {
    void ping(),
    new_backup_ret new_backup(1:string hostname, 2:i32 port),
    oneway void new_backup_succeed();
    PB_Errno update(1:i64 addr, 2:string value, 3:list<i64> seq),
    oneway void heartbeat(),
}
