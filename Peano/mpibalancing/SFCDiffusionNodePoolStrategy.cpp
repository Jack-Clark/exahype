#include "mpibalancing/SFCDiffusionNodePoolStrategy.h"
#include "tarch/parallel/Node.h"
#include "tarch/compiler/CompilerSpecificSettings.h"

#include <sstream>
#include <limits>
#include <map>


tarch::logging::Log mpibalancing::SFCDiffusionNodePoolStrategy::_log( "mpibalancing::SFCDiffusionNodePoolStrategy" );


mpibalancing::SFCDiffusionNodePoolStrategy::SFCDiffusionNodePoolStrategy(int mpiRanksPerNode, int primaryMPIRanksPerNode, double waitTimeOutSec):
  NodePoolStrategy(),
  _tag(-1),
  _nodes(),
  _waitTimeOut(waitTimeOutSec),
  _mpiRanksPerNode(mpiRanksPerNode),
  _primaryMPIRanksPerNode(primaryMPIRanksPerNode),
  _numberOfPrimaryRanksPerNodeThatAreCurrentlyDeployed(_primaryMPIRanksPerNode),
  _nodePoolState(NodePoolState::DeployingIdlePrimaryRanks) {

  assertion( mpiRanksPerNode>0 );
  assertion( primaryMPIRanksPerNode<=mpiRanksPerNode );
  assertion( mpiRanksPerNode&primaryMPIRanksPerNode==0 );
}


mpibalancing::SFCDiffusionNodePoolStrategy::~SFCDiffusionNodePoolStrategy() {
}


void mpibalancing::SFCDiffusionNodePoolStrategy::fillWorkerRequestQueue(RequestQueue& queue) {
  if (queue.empty() && _nodePoolState==NodePoolState::DeployingAlsoSecondaryRanks) {
    logInfo(
      "fillWorkerRequestQueue(RequestQueue)",
      "running out of ranks. Answered all pending MPI questions before but new requests keep on dropping in. Stop to deliver MPI ranks"
    );
    _nodePoolState = NodePoolState::NoNodesLeft;
  }

  #ifdef Parallel
  assertion( _tag >= 0 );

  std::clock_t waitTimeoutTimeStamp;

  bool continueToWait = true;
  while ( continueToWait ) {
    while (tarch::parallel::messages::WorkerRequestMessage::isMessageInQueue(_tag, true)) {
      tarch::parallel::messages::WorkerRequestMessage message;
      message.receive(MPI_ANY_SOURCE,_tag, true, SendAndReceiveLoadBalancingMessagesBlocking);
      queue.push_back( message );
      waitTimeoutTimeStamp = clock() + static_cast<std::clock_t>(std::floor(_waitTimeOut * CLOCKS_PER_SEC));
    }

    continueToWait =
        hasIdleNode(-1) &&
        (static_cast<int>(queue.size())+1 < getNumberOfRegisteredNodes()-getNumberOfIdleNodes()) &&
        (clock() < waitTimeoutTimeStamp);
  }
  #endif

  int totalNumberOfRequestedWorkers = 0;
  for (auto m: queue) {
    totalNumberOfRequestedWorkers += m.getNumberOfRequestedWorkers();
  }
  if (
    totalNumberOfRequestedWorkers>getNumberOfIdlePrimaryRanks()
    &&
    _nodePoolState!=NodePoolState::NoNodesLeft
  ) {
    _nodePoolState = NodePoolState::DeployingAlsoSecondaryRanks;
    logInfo(
      "fillWorkerRequestQueue(RequestQueue)",
      "have " << totalNumberOfRequestedWorkers <<
      " worker requests but only " << getNumberOfIdlePrimaryRanks() <<
      " primary node(s), i.e. code is running out of idle nodes. Start to deploy secondary nodes"
    );
    queue = sortRequestQueue( queue );
  }
  else {
    _numberOfPrimaryRanksPerNodeThatAreCurrentlyDeployed = std::max(1,totalNumberOfRequestedWorkers / getNumberOfPhysicalNodes());
  }
}


mpibalancing::SFCDiffusionNodePoolStrategy::RequestQueue mpibalancing::SFCDiffusionNodePoolStrategy::sortRequestQueue( const RequestQueue&  queue ) {
  assertion( _nodePoolState == NodePoolState::DeployingAlsoSecondaryRanks );
  RequestQueue result;

  #ifdef Parallel
  // Compute priorities of the nodes which equals the total number of
  // requests issued through all ranks of a node.
  std::map<std::string, int>  nodeToPriorityMap;
  for (auto p: queue) {
    assertion( p.getSenderRank()>=0 );
    assertion( p.getSenderRank()<static_cast<int>(_nodes.size()) );
    const std::string nodeName = _nodes[p.getSenderRank()].getNodeName();

    if (nodeToPriorityMap.count(nodeName)) {
      nodeToPriorityMap.insert( std::pair<std::string,int>(nodeName,0) );
    }

    nodeToPriorityMap[nodeName] += p.getNumberOfRequestedWorkers();
    assertion( nodeToPriorityMap[nodeName]>0 );
  }
  logDebug(
    "sortRequestQueue(Queue)",
    "node priority map has " << nodeToPriorityMap.size() << " entries. First entry=" <<
    nodeToPriorityMap.begin()->first << " with priority " << nodeToPriorityMap.begin()->second
  );


  // Sort the queue's requests according to their priorities.
  while ( !nodeToPriorityMap.empty() ) {
    // Find the one node with the maximum weight
    std::string maxNode     = "undef";
    int         maxPriority = -1;
    for (auto p: nodeToPriorityMap) {
      if (p.second>maxPriority) {
        maxPriority = p.second;
        maxNode     = p.first;
      }
    }
    logInfo( "sortRequestQueue(Queue)", "answer requests by node " << maxNode );
    assertionEquals(nodeToPriorityMap.count(maxNode),1);
    nodeToPriorityMap.erase(maxNode);

    // First sort in all those nodes that are inside
    for(auto p: queue) {
      if (
        _nodes[p.getSenderRank()].getNodeName().compare(maxNode)==0
        &&
        !isFirstOrLastPrimaryMPIRankOnANode(p.getSenderRank())
      ) {
        logDebug( "sortRequestQueue(Queue)", "prioritise rank " << p.getSenderRank() );
        result.push_back(p);
      }
    }

    // Now insert all those guys that are outside along the SFC
    for(auto p: queue) {
      if (
        _nodes[p.getSenderRank()].getNodeName().compare(maxNode)==0
        &&
        isFirstOrLastPrimaryMPIRankOnANode(p.getSenderRank())
      ) {
        result.push_back(p);
      }
    }
  }

  #endif

  assertionEquals( result.size(), queue.size() );
  return result;
}


tarch::parallel::messages::WorkerRequestMessage mpibalancing::SFCDiffusionNodePoolStrategy::extractElementFromRequestQueue(RequestQueue& queue) {
  assertion( !queue.empty() );

  RequestQueue::iterator pResultInQueue = queue.begin();
  tarch::parallel::messages::WorkerRequestMessage result = *pResultInQueue;
  queue.erase(pResultInQueue);

  return result;
}


bool mpibalancing::SFCDiffusionNodePoolStrategy::isFirstOrLastPrimaryMPIRankOnANode(int rank) const {
  return (rank % _mpiRanksPerNode == 0)
      || (rank % _mpiRanksPerNode == _primaryMPIRanksPerNode-1);
}


bool mpibalancing::SFCDiffusionNodePoolStrategy::isPrimaryMPIRank(int rank) const {
  const int NumberOfRanksPerPrimaryRank = _mpiRanksPerNode / _primaryMPIRanksPerNode;

  return rank % NumberOfRanksPerPrimaryRank==0;

//  return rank % _mpiRanksPerNode < _primaryMPIRanksPerNode;
}


void mpibalancing::SFCDiffusionNodePoolStrategy::addNode(const tarch::parallel::messages::RegisterAtNodePoolMessage& node) {
  #ifdef Parallel
  logTraceInWith1Argument( "addNode(...)", node.getSenderRank() );

  while (static_cast<int>(_nodes.size())<=node.getSenderRank()) {
    _nodes.push_back( NodePoolListEntry() );
  }

  const std::string name = tarch::parallel::StringTools::convert(node.getNodeName());
  assertion(_primaryMPIRanksPerNode!=0);
  _nodes[node.getSenderRank()] = NodePoolListEntry(
    name,
    isPrimaryMPIRank(node.getSenderRank())
  );

  logTraceOutWith1Argument( "addNode(...)", _nodes[node.getSenderRank()].toString() );
  #endif
}


void mpibalancing::SFCDiffusionNodePoolStrategy::removeNode( int rank ) {
  assertion( isRegisteredNode(rank) );
  _nodes[rank] = NodePoolListEntry(); // overwrite with invalidated entry
}


int mpibalancing::SFCDiffusionNodePoolStrategy::getNumberOfIdlePrimaryRanks() const {
  int result = 0;
  for (auto node: _nodes) {
    if (node.isIdlePrimaryRank()) {
      result++;
    }
  }
  return result;
}


bool mpibalancing::SFCDiffusionNodePoolStrategy::hasIdleNode(int forMaster) const {
  switch (_nodePoolState) {
    case NodePoolState::DeployingIdlePrimaryRanks:
      {
        for (auto node: _nodes) {
          if (node.isIdlePrimaryRank()) {
            return true;
          }
        }
      }
      break;
    case NodePoolState::DeployingAlsoSecondaryRanks:
    {
      for (auto node: _nodes) {
        if (node.isIdlePrimaryRank() || node.isIdleSecondaryRank()) {
          return true;
        }
      }
    }
    break;
    case NodePoolState::NoNodesLeft:
      break;
  }
  return false;
}


int mpibalancing::SFCDiffusionNodePoolStrategy::getNumberOfIdleNodes() const {
  int result = 0;
  for (auto node: _nodes) {
    if (node.isIdlePrimaryRank() || node.isIdleSecondaryRank()) {
      result++;
    }
  }
  return result;
}


void mpibalancing::SFCDiffusionNodePoolStrategy::setNodeIdle( int rank ) {
  assertion( isRegisteredNode(rank) );
  _nodes[rank].deActivate();
  if (isPrimaryMPIRank(rank)) {
    _nodePoolState = NodePoolState::DeployingIdlePrimaryRanks;
  }
  else if (_nodePoolState == NodePoolState::NoNodesLeft) {
    _nodePoolState = NodePoolState::DeployingAlsoSecondaryRanks;
  }
}


bool mpibalancing::SFCDiffusionNodePoolStrategy::isRegisteredNode(int rank) const {
  return static_cast<int>(_nodes.size())>rank
      && _nodes[rank].isRegistered();
}


bool mpibalancing::SFCDiffusionNodePoolStrategy::isIdleNode(int rank) const {
  return (static_cast<int>(_nodes.size())>rank)
      && (_nodes[rank].isIdlePrimaryRank() || _nodes[rank].isIdleSecondaryRank());
}


int mpibalancing::SFCDiffusionNodePoolStrategy::getNumberOfRegisteredNodes() const {
  int result = 0;
  for (auto node: _nodes) {
    if (node.isRegistered()) {
      result++;
    }
  }
  return result;
}


std::string mpibalancing::SFCDiffusionNodePoolStrategy::toString() const {
  std::ostringstream result;
  for (
    NodeContainer::const_iterator p = _nodes.begin();
    p != _nodes.end();
    p++
  ) {
    p->toString(result);
  }
  return result.str();
}


void mpibalancing::SFCDiffusionNodePoolStrategy::reserveParticularNode(int rank) {
  assertion( isRegisteredNode(rank) );
  assertion( _nodes[rank].isIdlePrimaryRank() || _nodes[rank].isIdleSecondaryRank() );

  _nodes[rank].activate();
}


int mpibalancing::SFCDiffusionNodePoolStrategy::getNumberOfPhysicalNodes() const {
  return std::max(static_cast<int>(_nodes.size()) / _mpiRanksPerNode,1);
}


int mpibalancing::SFCDiffusionNodePoolStrategy::reserveNode(int forMaster) {
  switch (_nodePoolState) {
    case NodePoolState::DeployingIdlePrimaryRanks:
      {
        for (int j=0; j<getNumberOfPhysicalNodes(); j++) {
          for (int i=0; i<_numberOfPrimaryRanksPerNodeThatAreCurrentlyDeployed;i++) {
            const int rank = (j*_mpiRanksPerNode+i+forMaster+1) % tarch::parallel::Node::getInstance().getNumberOfNodes();
            if (_nodes[rank].isIdlePrimaryRank()) {
              _nodes[rank].activate();
              return rank;
            }
          }
        }

        logInfo(
          "reserveNode(int)",
          "can't serve request from rank " << forMaster << " with constraint " <<
          _numberOfPrimaryRanksPerNodeThatAreCurrentlyDeployed << " of primary ranks per node. Fallback to all primary ranks"
        );

        // Fallback
        const int firstRankToStudy = (forMaster/_mpiRanksPerNode)*_mpiRanksPerNode;
        for (int i=0; i<static_cast<int>(_nodes.size());i++) {
          const int rank = (firstRankToStudy + i) % tarch::parallel::Node::getInstance().getNumberOfNodes();
          if (_nodes[rank].isIdlePrimaryRank()) {
            _nodes[rank].activate();
            return rank;
          }
        }
      }
      break;
    case NodePoolState::DeployingAlsoSecondaryRanks:
      {
        assertion( hasIdleNode(forMaster) );
        assertion( !_nodes[forMaster].isIdlePrimaryRank() );
        assertion( !_nodes[forMaster].isIdleSecondaryRank() );

        for (int i=1; i>0; i++) { // not an endless loop
          if (
            forMaster+i < tarch::parallel::Node::getInstance().getNumberOfNodes()
            &&
            (_nodes[forMaster+i].isIdlePrimaryRank() || _nodes[forMaster+i].isIdleSecondaryRank())
          ) {
            _nodes[forMaster+i].activate();
            return forMaster+i;
          }
          if (
            forMaster-i >= 0
            &&
            ( _nodes[forMaster-i].isIdlePrimaryRank() || _nodes[forMaster-i].isIdleSecondaryRank())
          ) {
            _nodes[forMaster-i].activate();
            return forMaster-i;
          }
        }
      }
      break;
    case NodePoolState::NoNodesLeft:
      break;
  }
  assertion(false);
  return -1;
}


void mpibalancing::SFCDiffusionNodePoolStrategy::setNodePoolTag(int tag) {
  _tag = tag;
}


mpibalancing::SFCDiffusionNodePoolStrategy::NodePoolListEntry::NodePoolListEntry():
  _state(State::Undef),
  _name("undef") {
}


mpibalancing::SFCDiffusionNodePoolStrategy::NodePoolListEntry::NodePoolListEntry(
  const std::string& name,
  bool isPrimaryNode
  ):
  _state( isPrimaryNode ? State::WorkingPrimaryRank : State::WorkingSecondaryRank ),
  _name(name) {
}


mpibalancing::SFCDiffusionNodePoolStrategy::NodePoolListEntry::~NodePoolListEntry() {
}


std::string mpibalancing::SFCDiffusionNodePoolStrategy::NodePoolListEntry::getNodeName() const {
  return _name;
}


std::string mpibalancing::SFCDiffusionNodePoolStrategy::NodePoolListEntry::toString() const {
  std::ostringstream out;
  toString(out);
  return out.str();
}


void mpibalancing::SFCDiffusionNodePoolStrategy::NodePoolListEntry::toString(std::ostream& out) const {
  out << "(state:";
  switch (_state) {
    case State::Undef:                 out << "undef";                   break;
    case State::WorkingPrimaryRank:    out << "working-primary-rank";    break;
    case State::WorkingSecondaryRank:  out << "working-secondary-rank";  break;
    case State::IdlePrimaryRank:       out << "idle-primary-rank";       break;
    case State::IdleSecondaryRank:     out << "idle-secondary-rank";     break;
  }
  out << ",name:" << _name << ")";
}


void mpibalancing::SFCDiffusionNodePoolStrategy::NodePoolListEntry::activate() {
  assertion( _state==State::IdlePrimaryRank || _state==State::IdleSecondaryRank );
  _state = _state==State::IdlePrimaryRank ? State::WorkingPrimaryRank : State::WorkingSecondaryRank;
}


void mpibalancing::SFCDiffusionNodePoolStrategy::NodePoolListEntry::deActivate() {
  assertion( _state==State::WorkingPrimaryRank || _state==State::WorkingSecondaryRank );
  _state = _state==State::WorkingPrimaryRank ? State::IdlePrimaryRank : State::IdleSecondaryRank;
}


bool mpibalancing::SFCDiffusionNodePoolStrategy::NodePoolListEntry::isRegistered() const {
  return _state!=State::Undef;
}


bool mpibalancing::SFCDiffusionNodePoolStrategy::NodePoolListEntry::isIdlePrimaryRank() const {
  return _state==State::IdlePrimaryRank;
}


bool mpibalancing::SFCDiffusionNodePoolStrategy::NodePoolListEntry::isIdleSecondaryRank() const {
  return _state==State::IdleSecondaryRank;
}
