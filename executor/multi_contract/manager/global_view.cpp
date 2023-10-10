#include "executor/multi_contract/manager/global_view.h"

#include "eEVM/util.h"

#include <glog/logging.h>

namespace resdb {
namespace contract {

GlobalView::GlobalView(DataStorage* storage) :storage_(storage){ }

void GlobalView::store(const uint256_t& key, const uint256_t& value) {
  storage_->Store(key, value);
}

uint256_t GlobalView::load(const uint256_t& key) {
  return storage_->Load(key).first;
}

bool GlobalView::remove(const uint256_t& key) {
  return storage_->Remove(key);
}

}
} // namespace eevm
