/* Copyright (c) 2011 maidsafe.net limited
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.
    * Neither the name of the maidsafe.net limited nor the names of its
    contributors may be used to endorse or promote products derived from this
    software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#ifndef MAIDSAFE_DHT_TESTS_LOCAL_NETWORK_H_
#define MAIDSAFE_DHT_TESTS_LOCAL_NETWORK_H_

#include <iostream>  // NOLINT (Fraser)
#include <string>
#include <utility>
#include <vector>

#include "maidsafe/common/test.h"

#include "maidsafe/dht/node_container.h"
#include "maidsafe/dht/node_id.h"

namespace maidsafe {
namespace dht {
namespace test {

template <typename NodeType>
class LocalNetwork {
 public:
  LocalNetwork(size_t num_full_nodes,
                   size_t num_client_nodes,
                   uint8_t threads_per_node,
                   uint16_t k,
                   uint16_t alpha,
                   uint16_t beta,
                   const bptime::time_duration &mean_refresh_interval);
  void SetUp();
  void TearDown();

  maidsafe::test::TestPath test_root_;
  size_t num_full_nodes_, num_client_nodes_;
  uint8_t threads_per_node_;
  uint16_t k_, alpha_, beta_;
  bptime::time_duration mean_refresh_interval_;
  std::vector<std::shared_ptr<maidsafe::dht::NodeContainer<NodeType>>>  // NOLINT (Fraser)
      node_containers_;
  std::vector<NodeId> node_ids_;
  boost::mutex mutex_;
  boost::condition_variable cond_var_;
};

template <typename NodeType>
LocalNetwork<NodeType>::LocalNetwork(
    size_t num_full_nodes,
    size_t num_client_nodes,
    uint8_t threads_per_node,
    uint16_t k,
    uint16_t alpha,
    uint16_t beta,
    const bptime::time_duration &mean_refresh_interval)
        : test_root_(maidsafe::test::CreateTestPath(
                     "MaidSafe_Test_Nodes_Environment")),
          num_full_nodes_(num_full_nodes),
          num_client_nodes_(num_client_nodes),
          threads_per_node_(threads_per_node),
          k_(k),
          alpha_(alpha),
          beta_(beta),
          mean_refresh_interval_(mean_refresh_interval),
          node_containers_(),
          node_ids_(),
          mutex_(),
          cond_var_() {}

template <typename NodeType>
void LocalNetwork<NodeType>::SetUp() {
  std::vector<Contact> bootstrap_contacts;
  for (size_t i = 0; i != num_client_nodes_ + num_full_nodes_; ++i) {
    std::shared_ptr<maidsafe::dht::NodeContainer<NodeType>>
        node_container(new maidsafe::dht::NodeContainer<NodeType>());
    node_container->Init(threads_per_node_, KeyPairPtr(),
                         MessageHandlerPtr(), false, k_, alpha_, beta_,
                         mean_refresh_interval_);
    node_container->MakeAllCallbackFunctors(&mutex_, &cond_var_);

    int result(kPendingResult);
    std::pair<Port, Port> port_range(8000, 65535);
    result = node_container->Start(bootstrap_contacts, port_range);
    ASSERT_EQ(kSuccess, result);
    ASSERT_TRUE(node_container->node()->joined());
    if (i < num_full_nodes_)
      bootstrap_contacts.push_back(node_container->node()->contact());
    node_containers_.push_back(node_container);
    node_ids_.push_back(node_container->node()->contact().node_id());
  }

  // Lookup every other node to properly populate each node's routing table
  auto begin_itr(node_containers_.begin());
  for (; begin_itr != node_containers_.end() - 1; ++begin_itr) {
    for (auto itr(begin_itr + 1); itr != node_containers_.end(); ++itr) {
      boost::mutex::scoped_lock lock(mutex_);
      (*begin_itr)->FindNodes((*itr)->node()->contact().node_id(), 0);
      int result(kPendingResult);
      std::vector<Contact> closest_nodes;
      if (!cond_var_.timed_wait(lock,
                                transport::kDefaultInitialTimeout * 2,
                                (*begin_itr)->wait_for_find_nodes_functor())) {
        std::cerr << DebugId((*begin_itr)->node()->contact())
                  << " failed to wait while finding node "
                  << DebugId((*itr)->node()->contact()) << std::endl;
      }
      (*begin_itr)->GetAndResetFindNodesResult(&result, &closest_nodes);
      ASSERT_FALSE(closest_nodes.empty());
      if (result != kSuccess ||
          (*itr)->node()->contact() != closest_nodes[0]) {
        std::cerr << DebugId((*begin_itr)->node()->contact())
                  << " failed while finding node "
                  << DebugId((*itr)->node()->contact()) << "   Result: "
                  << result << "    Closest contact found: "
                  << DebugId(closest_nodes[0]) << std::endl;
      }
    }
  }
#ifndef NDEBUG
  size_t expected_routing_table_size(node_containers_.size() - 1);
  std::for_each(
      node_containers_.begin(),
      node_containers_.end(),
      [expected_routing_table_size]
          (std::shared_ptr<maidsafe::dht::NodeContainer<NodeType>> container) {
    std::vector<Contact> contacts;
    container->node()->GetAllContacts(&contacts);
    if (contacts.size() != expected_routing_table_size)
      container->node()->PrintRoutingTable();
  });
#endif
}

template <typename NodeType>
void LocalNetwork<NodeType>::TearDown() {
#ifndef NDEBUG
  if (testing::UnitTest::GetInstance()->failed_test_count() != 0) {
    std::for_each(
        node_containers_.begin(),
        node_containers_.end(),
        [](std::shared_ptr<maidsafe::dht::NodeContainer<NodeType>> container) {
      container->node()->PrintRoutingTable();
    });
  }
#endif
  for (auto it(node_containers_.begin()); it != node_containers_.end(); ++it)
    (*it)->Stop(nullptr);
  test_root_.reset();
}

}   //  namespace test
}   //  namespace dht
}   //   namespace maidsafe

#endif  // MAIDSAFE_DHT_TESTS_LOCAL_NETWORK_H_
