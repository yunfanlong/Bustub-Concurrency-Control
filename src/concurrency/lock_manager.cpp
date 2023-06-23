//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// lock_manager.cpp
//
// Identification: src/concurrency/lock_manager.cpp
//
// Copyright (c) 2015-2019, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "concurrency/lock_manager.h"

#include <utility>
#include <vector>

namespace bustub {

auto LockManager::LockShared(Transaction *txn, const RID &rid) -> bool {

}

auto LockManager::LockExclusive(Transaction *txn, const RID &rid) -> bool {

}

auto LockManager::LockUpgrade(Transaction *txn, const RID &rid) -> bool {

}

auto LockManager::Unlock(Transaction *txn, const RID &rid) -> bool {

}

}  // namespace bustub
