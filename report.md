# Project 4 : CONCURRENCY CONTROL

This experiment will implement the lock manager in `bustub`, which is responsible for tracking tuple-level locks used in the database so that the database supports concurrent query plan execution.

## TASK #1 - LOCK MANAGER + DEADLOCK PREVENTION

In this experiment, a two-phase locking strategy will be used to implement specific tuple-level locks. The specific locking and unlocking strategy should be determined by the isolation level of the transaction. When a transaction needs to read or write a tuple, it needs to try to obtain the read lock or write lock corresponding to the tuple according to the isolation level, and release it at the appropriate moment.

### Transaction and isolation levels

```C++
155 class Transaction {
...
257  private:
258   /** The current transaction state. */
259   TransactionState state_;
260   /** The isolation level of the transaction. */
261   IsolationLevel isolation_level_;
262   /** The thread ID, used in single-threaded transactions. */
263   std::thread::id thread_id_;
264   /** The ID of this transaction. */
265   txn_id_t txn_id_;
266 
267   /** The undo set of table tuples. */
268   std::shared_ptr<std::deque<TableWriteRecord>> table_write_set_;
269   /** The undo set of indexes. */
270   std::shared_ptr<std::deque<IndexWriteRecord>> index_write_set_;
271   /** The LSN of the last record written by the transaction. */
272   lsn_t prev_lsn_;
273 
274   /** Concurrent index: the pages that were latched during index operation. */
275   std::shared_ptr<std::deque<Page *>> page_set_;
276   /** Concurrent index: the page IDs that were deleted during index operation.*/
277   std::shared_ptr<std::unordered_set<page_id_t>> deleted_page_set_;
278 
279   /** LockManager: the set of shared-locked tuples held by this transaction. */
280   std::shared_ptr<std::unordered_set<RID>> shared_lock_set_;
281   /** LockManager: the set of exclusive-locked tuples held by this transaction. */
282   std::shared_ptr<std::unordered_set<RID>> exclusive_lock_set_;
283 };
284 
```

Transactions in `bustub` are managed by `Transaction` and `TransactionManager`. `Transaction` maintains all information about the transaction, including transaction ID, transaction isolation level, transaction status (lock expansion, lock contraction, `COMMIT` and `ABORT`), transaction tuple modification record and index modification record, transaction The page modification record and the lock currently owned by the transaction.

The actual behavior of transactions in `TransactionManager`, such as `BEGIN`, `COMMIT`, `ABORT`, and the specific transaction corresponding to the ID can be obtained through it.

![figure1](https://github.com/yunfanlong/Concurrency-Control/blob/main/figure1.png)

Different isolation levels of transactions will lead to different possible concurrency exceptions, and are implemented through different ways of acquiring and releasing locks in the two-phase lock strategy.

![figure2](https://github.com/yunfanlong/Concurrency-Control/blob/main/figure1.png)

### Deadlock prevention strategy

In this experiment, deadlock prevention will be implemented through the `Wound-Wait` strategy. The specific method is: when a high-priority transaction waits for the lock of a low-priority transaction, kill the low-priority transaction; when the priority When a lower-priority transaction waits for a lock from a higher-priority transaction, the lower-priority transaction blocks. In `bustub`, the priority of a transaction is determined by its transaction ID, with a smaller transaction ID representing a higher priority.

### Lock request table

![figure3](https://github.com/yunfanlong/Concurrency-Control/blob/main/figure3.png)

In the lock manager, use `lock table` to manage locks. `lock table` is a hash table with tuple ID as key and lock request queue as value. Among them, the lock request stores the transaction ID of the tuple lock requested, the tuple lock type requested, and whether the request is allowed; saving the lock in a queue ensures the order of lock requests:

```C++
 38   class LockRequest {
 39    public:
 40     LockRequest(txn_id_t txn_id, LockMode lock_mode) : txn_id_(txn_id), lock_mode_(lock_mode), granted_(false) {}
 41 
 42     txn_id_t txn_id_;
 43     LockMode lock_mode_;
 44     bool granted_;
 45   };
 46 
 47   class LockRequestQueue {
 48    public:
 49     std::list<LockRequest> request_queue_;
 50     std::mutex latch_;
 51     // for notifying blocked transactions on this rid
 52     std::condition_variable cv_;
 53     // txn_id of an upgrading transaction (if any)
 54     txn_id_t upgrading_ = INVALID_TXN_ID;
 55   };
```

### LockShared

In `LockShared`, transaction `txn` requests a read lock with tuple ID `rid`：

```C++
 20 auto LockManager::LockShared(Transaction *txn, const RID &rid) -> bool {
 21   if (txn->GetState() == TransactionState::ABORTED) {
 22     return false;
 23   }
 24   if (txn->GetState() == TransactionState::SHRINKING) {
 25     txn->SetState(TransactionState::ABORTED);
 26     throw TransactionAbortException(txn->GetTransactionId(), AbortReason::LOCK_ON_SHRINKING);
 27   }
 28   if (txn->GetIsolationLevel() == IsolationLevel::READ_UNCOMMITTED) {
 29     txn->SetState(TransactionState::ABORTED);
 30     throw TransactionAbortException(txn->GetTransactionId(), AbortReason::LOCKSHARED_ON_READ_UNCO    MMITTED);
 31   }
```

**Lines 20-31**: Perform pre-judgment. When the transaction status is `ABORT`, return false directly; if the current transaction status is lock shrink, calling the acquire lock function will cause the transaction `ABORT` and throw An exception occurs; when a transaction's isolation level is `READ_UNCOMMITTED`, it should not acquire a read lock, and attempting to acquire a read lock will result in `ABORT` and throw an exception.

```C++
 32   txn->SetState(TransactionState::GROWING);
 33   std::unique_lock<std::mutex> lk(latch_);
 34   auto &lock_request_queue = lock_table_[rid];
 35   auto &request_queue = lock_request_queue.request_queue_;
 36   auto &cv = lock_request_queue.cv_;
 37   auto txn_id = txn->GetTransactionId();
 38   request_queue.emplace_back(txn_id, LockMode::SHARED);
 39   txn->GetSharedLockSet()->emplace(rid);
 40   txn_table_[txn_id] = txn;
 41   //Wound Wait : Kill all low priority transaction
 42   bool can_grant = true;
 43   bool is_kill = false;
 44   for (auto &request : request_queue) {
 45     if (request.lock_mode_ == LockMode::EXCLUSIVE) {
 46       if (request.txn_id_ > txn_id) {
 47         txn_table_[request.txn_id_]->SetState(TransactionState::ABORTED);
 48         is_kill = true;
 49       } else {
 50         can_grant = false;
 51       }
 52     }
 53     if (request.txn_id_ == txn_id) {
 54       request.granted_ = can_grant;
 55       break;
 56     }
 57   }
 58   if (is_kill) {
 59     cv.notify_all();
 60   }
```

**Lines 32-60**: If the prejudgment passes, the current transaction status is set to `GROWING`, and the data structure of the mutex protection lock manager is obtained. Then, obtain the lock request queue corresponding to the tuple ID and its related members, and add the lock request of the current transaction to the queue. Here `txn_table_` is a tuple that stores <transaction ID, transaction>. It should be noted that when **the request is added to the queue**, `GetSharedLockSet()` should be called to add the tuple ID to the transaction's holding lock set, so that the request can be removed from the queue when the lock is killed. delete.

In order to avoid deadlock, the transaction needs to check whether there is a lock request in the current queue that blocks it. If it exists, it will determine whether the priority of the current transaction is higher than the requested transaction. If so, kill the transaction; if not, `can_grant` will be used. Set to false to indicate that the transaction will be blocked. If the transaction kills any other transaction, other transactions waiting for the lock are awakened through the condition variable `cv` of the lock request queue, so that the killed transaction can exit the request queue.

```C++
 61   //Wait the lock
 62   while (!can_grant) {
 63     for (auto &request : request_queue) {
 64       if (request.lock_mode_ == LockMode::EXCLUSIVE &&
 65           txn_table_[request.txn_id_]->GetState() != TransactionState::ABORTED) {
 66         break;
 67       }
 68       if (request.txn_id_ == txn_id) {
 69         can_grant = true;
 70         request.granted_ = true;
 71       }
 72     }
 73     if (!can_grant) {
 74       cv.wait(lk);
 75     }
 76     if (txn->GetState() == TransactionState::ABORTED) {
 77       throw TransactionAbortException(txn->GetTransactionId(), AbortReason::DEADLOCK);
 78     }
 79   }
 80   return true;
 81 }
```

**Lines 61-81**: If there are other transactions blocking the transaction and the transaction cannot kill it, it will enter a loop and wait for the lock. In the loop, the transaction calls `wait` of the condition variable `cv` to block itself and release the lock atomically. Every time it wakes up, it checks:

1. Whether the transaction is killed, if so, an exception will be thrown;

2. Check whether the lock request before the transaction in the queue has a livewrite lock (the status is not `ABORT`). If so, continue to block. If not, set the `granted_` of the request to true and return.

### LockExclusive

`LockExclusive` causes transaction `txn` to attempt to acquire the tuple write lock with tuple ID `rid`.

```c++
 83 auto LockManager::LockExclusive(Transaction *txn, const RID &rid) -> bool {
 84   if (txn->GetState() == TransactionState::ABORTED) {
 85     return false;
 86   }
 87   if (txn->GetState() == TransactionState::SHRINKING) {
 88     txn->SetState(TransactionState::ABORTED);
 89     throw TransactionAbortException(txn->GetTransactionId(), AbortReason::LOCK_ON_SHRINKING);
 90   }
```

**Lines 83-90**: Perform pre-checking. If the current transaction status is `ABORT`, return false. If the current lock is in the contraction phase, set its status to `ABORT` and throw an exception.

```C++
 91   txn->SetState(TransactionState::GROWING);
 92   std::unique_lock<std::mutex> lk(latch_);
 93   auto &lock_request_queue = lock_table_[rid];
 94   auto &request_queue = lock_request_queue.request_queue_;
 95   auto &cv = lock_request_queue.cv_;
 96   auto txn_id = txn->GetTransactionId();
 97   request_queue.emplace_back(txn_id, LockMode::EXCLUSIVE);
 98   txn->GetExclusiveLockSet()->emplace(rid);
 99   txn_table_[txn_id] = txn;
100   //Wound Wait
101   bool can_grant = true;
102   bool is_kill = false;
103   for (auto &request : request_queue) {
104     if (request.txn_id_ == txn_id) {
105       request.granted_ = can_grant;
106       break;
107     }
108     if (request.txn_id_ > txn_id) {
109       txn_table_[request.txn_id_]->SetState(TransactionState::ABORTED);
110       is_kill = true;
111     } else {
112       can_grant = false;
113     }
114   }
115   if (is_kill) {
116     cv.notify_all();
117   }
```

**Lines 91-117**: Update the transaction status, obtain the lock request queue, insert the request into the queue, and add the lock to the transaction's own lock collection. Query whether there is a request that blocks the transaction lock request. When acquiring a write lock, any lock request in the queue will cause it to be blocked. When the transaction priority of the lock request is low, it will be killed. If there are requests that cannot be killed, the transaction will be blocked. When any transaction is killed, all transactions in the lock waiting queue will be awakened.

```C++
119   while (!can_grant) {
120     auto it = request_queue.begin();
121     while (txn_table_[it->txn_id_]->GetState() == TransactionState::ABORTED) {
122       ++it;
123     }
124     if (it->txn_id_ == txn_id) {
125       can_grant = true;
126       it->granted_ = true;
127     }
128     if (!can_grant) {
129       cv.wait(lk);
130     }
131     if (txn->GetState() == TransactionState::ABORTED) {
132       throw TransactionAbortException(txn->GetTransactionId(), AbortReason::DEADLOCK);
133     }
134   }
135   return true;
136 }
```

**Line 91-117**: Waiting for the lock to be available. Whenever the transaction is awakened, check whether it has been killed. If it is killed, an exception will be thrown; if it has not been killed, check whether there are any pending transactions in front of the queue. The lock request is killed, if not, the lock is obtained and the lock request `granted_` is set to true.

### LockUpgrade

`LockUpgrade` is used to upgrade the read lock with the tuple ID `rid` owned by the current transaction `txn` to a write lock.

```C++
138 auto LockManager::LockUpgrade(Transaction *txn, const RID &rid) -> bool {
139   if (txn->GetState() == TransactionState::ABORTED) {
140     return false;
141   }
142   std::unique_lock<std::mutex> lk(latch_);
143   auto &lock_request_queue = lock_table_[rid];
144   if (lock_request_queue.upgrading_ != INVALID_TXN_ID) {
145     txn->SetState(TransactionState::ABORTED);
146     throw TransactionAbortException(txn->GetTransactionId(), AbortReason::UPGRADE_CONFLICT);
147   }
148   auto &request_queue = lock_request_queue.request_queue_;
149   auto &cv = lock_request_queue.cv_;
150   auto txn_id = txn->GetTransactionId();
151   lock_request_queue.upgrading_ = txn_id;
```

**Lines 138-151**: Determine whether the current transaction has been killed, and whether the tuple's lock request sequence already contains other transactions waiting to upgrade the lock. If so, kill the transaction and throw an exception. If the test passes, the `upgrading_` of the current lock request queue is set to the current transaction ID to prompt that there is a transaction waiting to upgrade the lock in the queue.

```C++
153   while (!can_grant) {
154     auto it = request_queue.begin();
155     auto target = it;
156     can_grant = true;
157     bool is_kill = false;
158     while (it != request_queue.end() && it->granted_) {
159       if (it->txn_id_ == txn_id) {
160         target = it;
161       } else if (it->txn_id_ > txn_id) {
162         txn_table_[it->txn_id_]->SetState(TransactionState::ABORTED);
163         is_kill = true;
164       } else {
165         can_grant = false;
166       }
167       ++it;
168     }
169     if (is_kill) {
170       cv.notify_all();
171     } 
172     if (!can_grant) {
173       cv.wait(lk);
174     } else {
175       target->lock_mode_ = LockMode::EXCLUSIVE;
176       lock_request_queue.upgrading_ = INVALID_TXN_ID;
177     } 
178     if (txn->GetState() == TransactionState::ABORTED) {
179       throw TransactionAbortException(txn->GetTransactionId(), AbortReason::DEADLOCK);
180     } 
181   } 
182   
183   txn->GetSharedLockSet()->erase(rid);
184   txn->GetExclusiveLockSet()->emplace(rid);
185   return true;

```

**Lines 152-184**: There is no mention of the behavior of updating locks in `Wound Wait`. Here, every time it wakes up and tries to upgrade the lock, it is regarded as a write lock acquisition, that is, every time it tries to upgrade the lock, will kill the blocking transaction at the front of the queue.

The specific method is that every time a transaction is awakened, first check whether it has been killed, and then traverse the lock request queue in front of it. If its priority is lower, it will be killed. If its priority is higher, it will be killed. can_grant` is set to false, indicating that it will be blocked later. If you kill any transaction, other transactions will be awakened. If `can_grant` is false, the transaction will be blocked. If `can_grant` is true, the `lock_mode_` of the lock request will be updated and `upgrading_` will be initialized.

When the upgrade is successful, the transaction's owning lock set is updated.

### Unlock

The `Unlock` function causes transaction `txn` to release the lock on the tuple with tuple ID `rid`.

```c++
188 auto LockManager::Unlock(Transaction *txn, const RID &rid) -> bool {
189   if (txn->GetState() == TransactionState::GROWING && txn->GetIsolationLevel() == IsolationLevel::REPEATABLE_READ) {
190     txn->SetState(TransactionState::SHRINKING);
191   }
192 
193   std::unique_lock<std::mutex> lk(latch_);
194   auto &lock_request_queue = lock_table_[rid];
195   auto &request_queue = lock_request_queue.request_queue_;
196   auto &cv = lock_request_queue.cv_;
197   auto txn_id = txn->GetTransactionId();
198   auto it = request_queue.begin();
199   while (it->txn_id_ != txn_id) {
200     ++it;
201   }
202 
203   request_queue.erase(it);
204   cv.notify_all();
205   txn->GetSharedLockSet()->erase(rid);
206   txn->GetExclusiveLockSet()->erase(rid);
207   return true;
208 }
```

It should be noted that when the transaction isolation level is `READ_COMMIT`, the read lock obtained by the transaction will be released immediately after use, so this type of transaction does not comply with the 2PL rules. For the sake of program compatibility, here it is considered that the `READ_COMMIT` transaction is in` It always remains in the `GROWING` state before COMMIT` or `ABORT`. For other transactions, it will change to the `SHRINKING` state when `Unlock` is called. When releasing the lock, traverse the lock request column and delete the lock request of the corresponding transaction, then wake up other transactions, and delete the lock in the transaction's lock set.

## TASK #3 - CONCURRENT QUERY EXECUTION

In this section, it is necessary to provide tuple lock protection for the `NEXT()` method of the sequential scan, insert, delete, and update plans of the query plan executor, so that the above plans can support concurrent execution.

```C++
 33 bool SeqScanExecutor::Next(Tuple *tuple, RID *rid) {
 34   const Schema *out_schema = this->GetOutputSchema();
 35   auto exec_ctx = GetExecutorContext();
 36   Transaction *txn = exec_ctx->GetTransaction();
 37   TransactionManager *txn_mgr = exec_ctx->GetTransactionManager();
 38   LockManager *lock_mgr = exec_ctx->GetLockManager();
 39   Schema table_schema = table_info_->schema_;
 40   while (iter_ != end_) {
 41     Tuple table_tuple = *iter_;
 42     *rid = table_tuple.GetRid();
 43     if (txn->GetSharedLockSet()->count(*rid) == 0U && txn->GetExclusiveLockSet()->count(*rid) == 0U) {
 44       if (txn->GetIsolationLevel() != IsolationLevel::READ_UNCOMMITTED && !lock_mgr->LockShared(txn, *rid)) {
 45         txn_mgr->Abort(txn);
 46       }
 47     }
 48     std::vector<Value> values;
 49     for (const auto &col : GetOutputSchema()->GetColumns()) {
 50       values.emplace_back(col.GetExpr()->Evaluate(&table_tuple, &table_schema));
 51     }
 52     *tuple = Tuple(values, out_schema);
 53     auto *predicate = plan_->GetPredicate();
 54     if (predicate == nullptr || predicate->Evaluate(tuple, out_schema).GetAs<bool>()) {
 55       ++iter_;
 56       if (txn->GetIsolationLevel() == IsolationLevel::READ_COMMITTED) {
 57         lock_mgr->Unlock(txn, *rid);
 58       }
 59       return true;
 60     }
 61     if (txn->GetSharedLockSet()->count(*rid) != 0U && txn->GetIsolationLevel() == IsolationLevel::READ_COMMITTED) {
 62       lock_mgr->Unlock(txn, *rid);
 63     }
 64     ++iter_;
 65   }
 66   return false;
 67 }
```

When `SeqScanExecutor` obtains a tuple from the table, it needs to add a read lock to the tuple under the following conditions, and call `Abort` to kill the tuple when the lock fails:

1. The transaction does not own the read lock or write lock on the tuple (because the same tuple may be accessed multiple times in a transaction);
2. The isolation level of the transaction is not `READ_UNCOMMITTED`.

After using the tuple, you need to unlock the tuple under the following conditions:

1. The transaction owns the read lock on the tuple (the lock owned by the transaction may be a write lock);
2. When the transaction isolation level is `READ_COMMITTED`, the read lock needs to be released immediately after use. At other levels, the lock will be released in `COMMIT` or `ABORT`.

```C++
 37 bool InsertExecutor::Next([[maybe_unused]] Tuple *tuple, RID *rid) {
 38   auto exec_ctx = GetExecutorContext();
 39   Transaction *txn = exec_ctx_->GetTransaction();
 40   TransactionManager *txn_mgr = exec_ctx->GetTransactionManager();
 41   LockManager *lock_mgr = exec_ctx->GetLockManager();
 42 
 43   Tuple tmp_tuple;
 44   RID tmp_rid;
 45   if (is_raw_) {
 46     for (uint32_t idx = 0; idx < size_; idx++) {
 47       const std::vector<Value> &raw_value = plan_->RawValuesAt(idx);
 48       tmp_tuple = Tuple(raw_value, &table_info_->schema_);
 49       if (table_info_->table_->InsertTuple(tmp_tuple, &tmp_rid, txn)) {
 50         if (!lock_mgr->LockExclusive(txn, tmp_rid)) {
 51           txn_mgr->Abort(txn);
 52         }
 53         for (auto indexinfo : indexes_) {
 54           indexinfo->index_->InsertEntry(
 55               tmp_tuple.KeyFromTuple(table_info_->schema_, indexinfo->key_schema_, indexinfo->index_->GetKeyAttrs()),
 56               tmp_rid, txn);
 57           IndexWriteRecord iwr(*rid, table_info_->oid_, WType::INSERT, *tuple, *tuple, indexinfo->index_oid_,
 58                                exec_ctx->GetCatalog());
 59           txn->AppendIndexWriteRecord(iwr);
 60         }
 61       }
 62     }
 63     return false;
 64   }
 65   while (child_executor_->Next(&tmp_tuple, &tmp_rid)) {
 66     if (table_info_->table_->InsertTuple(tmp_tuple, &tmp_rid, txn)) {
 67       if (!lock_mgr->LockExclusive(txn, *rid)) {
 68         txn_mgr->Abort(txn);
 69       }
 70       for (auto indexinfo : indexes_) {
 71         indexinfo->index_->InsertEntry(tmp_tuple.KeyFromTuple(*child_executor_->GetOutputSchema(),
 72                                                               indexinfo->key_schema_, indexinfo->index_->GetKeyAttrs()),
 73                                        tmp_rid, txn);
 74         txn->GetIndexWriteSet()->emplace_back(tmp_rid, table_info_->oid_, WType::INSERT, tmp_tuple, tmp_tuple,
 75                                               indexinfo->index_oid_, exec_ctx->GetCatalog());
 76       }
 77     }
 78   }
 79   return false;
 80 }
```

For `InsertExecutor`, it obtains the write lock of the insertion table after inserting the tuple into the table. It should be noted that when its tuple source is other plan nodes, its source tuple is not the same tuple as the tuple inserted into the table.

```C++
 30 bool DeleteExecutor::Next([[maybe_unused]] Tuple *tuple, RID *rid) {
 31   auto exec_ctx = GetExecutorContext();
 32   Transaction *txn = exec_ctx_->GetTransaction();
 33   TransactionManager *txn_mgr = exec_ctx->GetTransactionManager();
 34   LockManager *lock_mgr = exec_ctx->GetLockManager();
 35 
 36   while (child_executor_->Next(tuple, rid)) {
 37     if (txn->GetIsolationLevel() != IsolationLevel::REPEATABLE_READ) {
 38       if (!lock_mgr->LockExclusive(txn, *rid)) {
 39         txn_mgr->Abort(txn);
 40       }
 41     } else {
 42       if (!lock_mgr->LockUpgrade(txn, *rid)) {
 43         txn_mgr->Abort(txn);
 44       }
 45     }
 46     if (table_info_->table_->MarkDelete(*rid, txn)) {
 47       for (auto indexinfo : indexes_) {
 48         indexinfo->index_->DeleteEntry(tuple->KeyFromTuple(*child_executor_->GetOutputSchema(), i    ndexinfo->key_schema_,
 49                                                            indexinfo->index_->GetKeyAttrs()),
 50                                        *rid, txn);
 51         IndexWriteRecord iwr(*rid, table_info_->oid_, WType::DELETE, *tuple, *tuple, indexinfo->i    ndex_oid_,
 52                              exec_ctx->GetCatalog());
 53         txn->AppendIndexWriteRecord(iwr);
 54       }
 55     }
 56   }
 57   return false;
 58 }
```

For `DeleteExecutor` and `UpdateExecutor`, after they obtain the sub-plan node tuple, they should add a read lock to the tuple. It should be noted that when the transaction isolation level is `REPEATABLE_READ`, its child plan node owns the read lock of the tuple, so `LockUpgrade` should be called at this time to upgrade the lock instead of obtaining a new write lock.

## Test Reslut

![figure4](https://github.com/yunfanlong/Concurrency-Control/blob/main/figure4.png)