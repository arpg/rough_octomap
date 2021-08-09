/*
 * OctoMap - An Efficient Probabilistic 3D Mapping Framework Based on Octrees
 * http://octomap.github.com/
 *
 * Copyright (c) 2009-2013, K.M. Wurm and A. Hornung, University of Freiburg
 * All rights reserved.
 * License: New BSD
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the University of Freiburg nor the names of its
 *       contributors may be used to endorse or promote products derived from
 *       this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <rough_octomap/RoughOcTree.h>

namespace octomap {

  // node implementation  --------------------------------------
  std::ostream& RoughOcTreeNode::writeData(std::ostream &s) const {
    s.write((const char*) &value, sizeof(value)); // occupancy
    s.write((const char*) &rough, sizeof(rough)); // rough
    s.write((const char*) &stair_logodds, sizeof(stair_logodds)); // stair log odds

    return s;
  }

  std::istream& RoughOcTreeNode::readData(std::istream &s) {
    s.read((char*) &value, sizeof(value)); // occupancy
    s.read((char*) &rough, sizeof(rough)); // rough
    s.read((char*) &stair_logodds, sizeof(stair_logodds)); // stair log odds

    return s;
  }

  float RoughOcTreeNode::getAverageChildRough() const {
    int m = 0;
    int c = 0;

    if (children != NULL){
      for (int i=0; i<8; i++) {
        RoughOcTreeNode* child = static_cast<RoughOcTreeNode*>(children[i]);

        if (child != NULL && child->isRoughSet()) {
          m += child->getRough();
          ++c;
        }
      }
    }

    if (c > 0) {
      m /= c;
      return m;
    }
    else { // no child had a color other than white
      return NAN;
    }
  }

  void RoughOcTreeNode::updateRoughChildren() {
    rough = getAverageChildRough();
  }

  float RoughOcTreeNode::getMeanChildStairLogOdds() const{
    double mean = 0;
    uint8_t c = 0;
    if (children !=NULL){
      for (unsigned int i=0; i<8; i++) {
        if (children[i] != NULL) {
          mean += static_cast<RoughOcTreeNode*>(children[i])->getStairProbability(); // TODO check if works generally
          ++c;
        }
      }
    }

    if (c > 0)
      mean /= (double) c;

    return (float)log(mean/(1-mean));
  }

  float RoughOcTreeNode::getMaxChildStairLogOdds() const{
    float max = -std::numeric_limits<float>::max();

    if (children !=NULL){
      for (unsigned int i=0; i<8; i++) {
        if (children[i] != NULL) {
          float l = static_cast<RoughOcTreeNode*>(children[i])->getStairLogOdds(); // TODO check if works generally
          if (l > max)
            max = l;
        }
      }
    }
    return max;
  }

  void RoughOcTreeNode::addStairValue(const float& logOdds) {
    stair_logodds += logOdds;
  }

  // tree implementation  --------------------------------------
  RoughOcTree::RoughOcTree(double in_resolution)
  : OccupancyOcTreeBase<RoughOcTreeNode>(in_resolution) {
    roughOcTreeMemberInit.ensureLinking();
    binary_encoding_mode = RoughBinaryEncodingMode::BINNING;
    rough_binary_thres = 0.99;
    num_binary_bins = 16; // must be power of 2
  }

  float RoughOcTree::getNodeRough(const OcTreeKey& key) {
    RoughOcTreeNode* n = search (key);
    if (n != 0) {
      return n->getRough();
    }
    return NAN;
  }

  bool RoughOcTree::pruneNode(RoughOcTreeNode* node) {
    if (!isNodeCollapsible(node))
      return false;

    // set value to children's values (all assumed equal)
    node->copyData(*(getNodeChild(node, 0)));

    if (node->isRoughSet()) // TODO check
      node->setRough(node->getAverageChildRough());

    // delete children
    for (unsigned int i=0;i<8;i++) {
      deleteNodeChild(node, i);
    }
    delete[] node->children;
    node->children = NULL;

    return true;
  }

  bool RoughOcTree::isNodeCollapsible(const RoughOcTreeNode* node) const{
    // all children must exist, must not have children of
    // their own and have the same occupancy probability
    if (!nodeChildExists(node, 0))
      return false;

    const RoughOcTreeNode* firstChild = getNodeChild(node, 0);
    if (nodeHasChildren(firstChild))
      return false;

    for (unsigned int i = 1; i<8; i++) {
      // TODO need to add checks for roughness and agent!
      // compare nodes only using their occupancy, ignoring color for pruning
      if (!nodeChildExists(node, i) || nodeHasChildren(getNodeChild(node, i)) || !(getNodeChild(node, i)->getValue() == firstChild->getValue()))
        return false;
    }

    return true;
  }

  // Possible future use for a more efficient occupancy update
  RoughOcTreeNode* RoughOcTree::updateNodeRough(RoughOcTreeNode* node, const OcTreeKey& key, bool occupied, char agent) {
    float logOdds = this->prob_miss_log;
    if (occupied)
      logOdds = this->prob_hit_log;

    if (node && ((logOdds >= 0 && node->getLogOdds() >= this->clamping_thres_max)
             ||  (logOdds <= 0 && node->getLogOdds() <= this->clamping_thres_min))) {
      return node;
    }

    bool createdRoot = false;
    if (this->root == NULL) {
      this->root = new RoughOcTreeNode();
      this->tree_size++;
      createdRoot = true;
    }

    return updateNodeRecurs(this->root, createdRoot, key, 0, logOdds, 0);
  }

  RoughOcTreeNode* RoughOcTree::setNodeAgent(const OcTreeKey& key,
                                             char agent) {
    RoughOcTreeNode* n = search (key);
    if (n != 0) {
      n->setAgent(agent);
    }
    return n;
  }

  RoughOcTreeNode* RoughOcTree::setNodeRough(const OcTreeKey& key,
                                             float rough) {
    RoughOcTreeNode* n = search (key);
    if (n != 0) {
      n->setRough(rough);
    }
    return n;
  }

  RoughOcTreeNode* RoughOcTree::averageNodeRough(const OcTreeKey& key,
                                                 float rough) {
    RoughOcTreeNode* n = search(key);
    if (n != 0 /*&& !isnan(rough)*/) {
      if (n->isRoughSet()) {
        float prev_rough = n->getRough();
        n->setRough((prev_rough + rough)/2);
      }
      else {
        n->setRough(rough);
      }
    }
    return n;
  }

  RoughOcTreeNode* RoughOcTree::integrateNodeRough(const OcTreeKey& key,
                                                   float rough) {
    RoughOcTreeNode* n = search (key);
    if (n != 0) {
      if (n->isRoughSet()) {
        float prev_rough = n->getRough();
        double node_prob = n->getOccupancy();
        float new_rough = (prev_rough * node_prob
                           +  rough * (0.99-node_prob));
        n->setRough(new_rough);
      }
      else {
        n->setRough(rough);
      }
    }
    return n;
  }

  RoughOcTreeNode* RoughOcTree::integrateNodeStairs(const OcTreeKey& key, bool is_stairs) {
    float log_odds_update = logodds(0.49);
    if (is_stairs)
      log_odds_update = logodds(0.99);

    RoughOcTreeNode* leaf = this->search(key);
    // no change: node already at threshold
    if (leaf) {
      if ( !((log_odds_update >= 0 && leaf->getStairLogOdds() >= this->clamping_thres_max)
        || (log_odds_update <= 0 && leaf->getStairLogOdds() <= this->clamping_thres_min)) ) {
        updateNodeStairLogOdds(leaf, log_odds_update);
      }
    }

    return leaf;
  }

  float RoughOcTree::getNodeStairLogOdds(const OcTreeKey& key) {
    RoughOcTreeNode* n = search (key);
    if (n != 0) {
      return n->getStairLogOdds();
    }
    return 0.0;
  }

  RoughOcTreeNode* RoughOcTree::setNodeStairLogOdds(const OcTreeKey& key, float value) {
    RoughOcTreeNode* n = search (key);
    if (n != 0) {
      n->setStairLogOdds(value);
      return n;
    }
    return NULL;
  }

  RoughOcTreeNode* RoughOcTree::updateNodeStairs(const OcTreeKey& key, bool is_stairs) {
    float logOdds = this->prob_miss_log;
    if (is_stairs)
      logOdds = 0.24;

    return updateNodeStairs(key, logOdds);
  }

  RoughOcTreeNode* RoughOcTree::updateNodeStairs(const OcTreeKey& key, float log_odds_update) {
    // early abort (no change will happen).
    // may cause an overhead in some configuration, but more often helps
    RoughOcTreeNode* leaf = this->search(key);
    // no change: node already at threshold
    if (leaf
        && ((log_odds_update >= 0 && leaf->getStairLogOdds() >= this->clamping_thres_max)
        || ( log_odds_update <= 0 && leaf->getStairLogOdds() <= this->clamping_thres_min)))
    {
      return leaf;
    }

    bool createdRoot = false;
    if (this->root == NULL){
      this->root = new RoughOcTreeNode();
      this->tree_size++;
      createdRoot = true;
    }

    return updateNodeStairsRecurs(this->root, createdRoot, key, 0, log_odds_update);
  }

  RoughOcTreeNode* RoughOcTree::updateNodeStairsRecurs(RoughOcTreeNode* node, bool node_just_created, const OcTreeKey& key,
                                                    unsigned int depth, const float& log_odds_update) {
    bool created_node = false;

    assert(node);

    // follow down to last level
    if (depth < this->tree_depth) {
      unsigned int pos = computeChildIdx(key, this->tree_depth -1 - depth);
      if (!this->nodeChildExists(node, pos)) {
        // child does not exist, but maybe it's a pruned node?
        if (!this->nodeHasChildren(node) ) {
          // current node does not have children AND it is not a new node
          // -> expand pruned node
          this->expandNode(node);
        }
        else {
          // not a pruned node, create requested child
          this->createNodeChild(node, pos);
          created_node = true;
        }
      }

      // if (lazy_eval)
      //   return updateNodeRecurs(this->getNodeChild(node, pos), created_node, key, depth+1, log_odds_update, lazy_eval);
      // else
      {
        RoughOcTreeNode* retval = updateNodeStairsRecurs(this->getNodeChild(node, pos), created_node, key, depth+1, log_odds_update);
        // prune node if possible, otherwise set own probability
        // note: combining both did not lead to a speedup!
        if (this->pruneNode(node)){
          // return pointer to current parent (pruned), the just updated node no longer exists
          retval = node;
        } else{
          node->updateStairChildren();
        }

        return retval;
      }
    }

    // at last level, update node, end of recursion
    else {
      if (use_change_detection) {
        bool occBefore = this->isNodeStairs(node);
        updateNodeStairLogOdds(node, log_odds_update);

        if (node_just_created){  // new node
          changed_keys.insert(std::pair<OcTreeKey,bool>(key, true));
        } else if (occBefore != this->isNodeStairs(node)) {  // occupancy changed, track it
          KeyBoolMap::iterator it = changed_keys.find(key);
          if (it == changed_keys.end())
            changed_keys.insert(std::pair<OcTreeKey,bool>(key, false));
          else if (it->second == false)
            changed_keys.erase(it);
        }
      } else {
        updateNodeStairLogOdds(node, log_odds_update);
      }
      return node;
    }
  }

  void RoughOcTree::updateNodeStairLogOdds(RoughOcTreeNode* occupancyNode, const float& update) const {
    occupancyNode->addStairValue(update);
    if (occupancyNode->getStairLogOdds() < this->clamping_thres_min) {
      occupancyNode->setStairLogOdds(this->clamping_thres_min);
      return;
    }
    if (occupancyNode->getStairLogOdds() > this->clamping_thres_max) {
      occupancyNode->setStairLogOdds(this->clamping_thres_max);
    }
  }

  void RoughOcTree::updateInnerOccupancy() {
    this->updateInnerOccupancyRecurs(this->root, 0);
  }

  void RoughOcTree::updateInnerOccupancyRecurs(RoughOcTreeNode* node, unsigned int depth) {
    // only recurse and update for inner nodes:
    if (nodeHasChildren(node)){
      // return early for last level:
      if (depth < this->tree_depth){
        for (unsigned int i=0; i<8; i++) {
          if (nodeChildExists(node, i)) {
            updateInnerOccupancyRecurs(getNodeChild(node, i), depth+1);
          }
        }
      }
      node->updateOccupancyChildren();
      node->updateRoughChildren();
      node->updateStairChildren();
    }
  }

  // binary io
  std::istream& RoughOcTree::readBinaryData(std::istream &s){
    // tree needs to be newly created or cleared externally
    if (this->root) {
      OCTOMAP_ERROR_STR("Trying to read into an existing tree.");
      return s;
    }

    // printf("New tree in readbinarydata\n");

    this->root = new RoughOcTreeNode();
    this->readBinaryNode(s, this->root);
    this->size_changed = true;
    this->tree_size = calcNumNodes();  // compute number of nodes
    return s;
  }

  std::ostream& RoughOcTree::writeBinaryData(std::ostream &s) const{
    OCTOMAP_DEBUG("Writing %zu nodes to output stream...", this->size());
    if (this->root)
      this->writeBinaryNode(s, this->root);
    return s;
  }


  std::istream& RoughOcTree::readBinaryNode(std::istream &s, RoughOcTreeNode* node) {
    switch (binary_encoding_mode) {
      case THRESHOLDING:
        // printf("Reading binary node via thresholding.\n");
        return readBinaryNodeViaThresholding(s, node);
        break;
      case BINNING:
        // printf("Reading binary node via binning.\n");
        return readBinaryNodeViaBinning(s, node);
        break;
      default:
        OCTOMAP_ERROR("Invalid binary encoding mode.");
        return s;
    }
  }

  std::ostream& RoughOcTree::writeBinaryNode(std::ostream &s, const RoughOcTreeNode* node) const {
    switch (binary_encoding_mode) {
      case THRESHOLDING:
        // printf("Writing binary node via thresholding.\n");
        return writeBinaryNodeViaThresholding(s, node);
        break;
      case BINNING:
        // printf("Writing binary node via binning.\n");
        return writeBinaryNodeViaBinning(s, node);
        break;
      default:
        OCTOMAP_ERROR("Invalid binary encoding mode.");
        return s;
    }
  }

  std::istream& RoughOcTree::readBinaryNodeViaThresholding(std::istream &s, RoughOcTreeNode* node){

    assert(node);

    char childset1_char, childset2_char, childset3_char;
    s.read((char*)&childset1_char, sizeof(char));
    s.read((char*)&childset2_char, sizeof(char));
    s.read((char*)&childset3_char, sizeof(char));

    std::bitset<8> children[3];
    children[0] = (unsigned long long) childset1_char;
    children[1] = (unsigned long long) childset2_char;
    children[2] = (unsigned long long) childset3_char;
    auto children_access = [children](uint child, uint value){return children[(child*3+value)/8][(child*3+value)%8];}; // maps child and value indices to aligned char array, returns bitset reference

    //     std::cout << "read:  "
    //        << child1to4.to_string<char,std::char_traits<char>,std::allocator<char> >() << " "
    //        << child5to8.to_string<char,std::char_traits<char>,std::allocator<char> >() << std::endl;


    // inner nodes default to occupied
    node->setLogOdds(this->clamping_thres_max);

    for (unsigned int i=0; i<8; i++) {
      if ((children_access(i,0) == 1) && (children_access(i,1) == 0)) {
        // child is free leaf
        this->createNodeChild(node, i);
        this->getNodeChild(node, i)->setLogOdds(this->clamping_thres_min);
      }
      else if ((children_access(i,0) == 0) && (children_access(i,1) == 1)) {
        // child is occupied leaf
        this->createNodeChild(node, i);
        this->getNodeChild(node, i)->setLogOdds(this->clamping_thres_max);
        if (children_access(i,2) == 1) { // if binarized child is rough, set rough value to binary thres
          this->getNodeChild(node, i)->setRough(this->rough_binary_thres);
        }
        else { // else, set rough value to zero
          this->getNodeChild(node, i)->setRough(0.0f);
        }
      }
      else if ((children_access(i,0) == 1) && (children_access(i,1) == 1)) {
        // child has children
        this->createNodeChild(node, i);
        this->getNodeChild(node, i)->setLogOdds(-200.); // child is unkown, we leave it uninitialized
      }
    }

    // read children's children and set the label
    for (unsigned int i=0; i<8; i++) {
      if (this->nodeChildExists(node, i)) {
        RoughOcTreeNode* child = this->getNodeChild(node, i);
        if (fabs(child->getLogOdds() + 200.)<1e-3) { // has children?
          readBinaryNode(s, child);
          child->setLogOdds(child->getMaxChildLogOdds());
        }
      } // end if child exists
    } // end for children

    return s;
  }

  std::ostream& RoughOcTree::writeBinaryNodeViaThresholding(std::ostream &s, const RoughOcTreeNode* node) const{

    assert(node);

    // 3 bits for each children, 8 children per node -> 24 bits
    // std::bitset<8> childset1; // 1A 1B 1R 2A 2B 2R 3A 3B
    // std::bitset<8> childset2;  // 3R 4A 4B 4R 5A 5B 5R 6A
    // std::bitset<8> childset3;   // 6B 6R 7A 7B 7R 8A 8B 8R
    std::bitset<8> children[3];
    auto children_access = [&children] (uint child, uint value) {return children[(child*3+value)/8][(child*3+value)%8];}; // maps child and value indices to aligned char array, returns bitset reference

    // 10* : child is free node
    // 01* : child is occupied node
    // 00* : child is unkown node
    // 11* : child has children
    // **1 : child is rough
    // **0 : child is traversable or traversability unknown (should be treated similarly)

    // speedup: only set bits to 1, rest is init with 0 anyway,
    //          can be one logic expression per bit

    for (unsigned int i=0; i<8; i++) {
      if (this->nodeChildExists(node, i)) {
        const RoughOcTreeNode* child = this->getNodeChild(node, i);
        if      (this->nodeHasChildren(child))  { children_access(i,0) = 1; children_access(i,1) = 1; }
        else if (this->isNodeOccupied(child)) {
          children_access(i,0) = 0; children_access(i,1) = 1;
          if (child->getRough()>this->rough_binary_thres) { // fails if rough is nan or less than or equal to rough binary threshold
            children_access(i,2) = 1; // set rough bit
          }
        }
        else { children_access(i,0) = 1; children_access(i,1) = 0; }
      }
      else {
        children_access(i,0) = 0; children_access(i,1) = 0; // shouldn't be necessary since default value is 0?
      }
    }

    //     std::cout << "wrote: "
    //        << child1to4.to_string<char,std::char_traits<char>,std::allocator<char> >() << " "
    //        << child5to8.to_string<char,std::char_traits<char>,std::allocator<char> >() << std::endl;

    char childset1_char = (char) children[0].to_ulong();
    char childset2_char = (char) children[1].to_ulong();
    char childset3_char = (char) children[2].to_ulong();

    s.write((char*)&childset1_char, sizeof(char));
    s.write((char*)&childset2_char, sizeof(char));
    s.write((char*)&childset3_char, sizeof(char));

    // write children's children
    for (unsigned int i=0; i<8; i++) {
      if (this->nodeChildExists(node, i)) {
        const RoughOcTreeNode* child = this->getNodeChild(node, i);
        if (this->nodeHasChildren(child)) {
          writeBinaryNode(s, child);
        }
      }
    }

    return s;
  }

  std::istream& RoughOcTree::readBinaryNodeViaBinning(std::istream &s, RoughOcTreeNode* node){

    assert(node);

    uint num_rough_bits = log2(num_binary_bins);
    uint num_bits_per_node = 2+num_rough_bits+1; // 2 for occ, rough bits, 1 for stairs

    // 2+num_rough_bits for each children, 8 children per node -> (2+num_rough_bits)*8 bits total
    boost::dynamic_bitset<> children(num_bits_per_node*8);
    auto children_access = [&children,num_bits_per_node] (uint child, uint value) {return children[child*num_bits_per_node+value];}; // maps child and value indices to aligned char array, returns bitset reference

    //     std::cout << "read:  "
    //        << child1to4.to_string<char,std::char_traits<char>,std::allocator<char> >() << " "
    //        << child5to8.to_string<char,std::char_traits<char>,std::allocator<char> >() << std::endl;

    std::bitset<8> children_byte;
    for (int i=0; i<num_bits_per_node; i++) {
      char children_char;
      s.read((char*)&children_char, sizeof(char));
      std::bitset<8> children_byte(children_char);
      for (uint j=0; j<8; j++) {
        children[i*8+j] = children_byte[j];
      }
    }

    // inner nodes default to occupied
    node->setLogOdds(this->clamping_thres_max);

    for (unsigned int i=0; i<8; i++) {
      if ((children_access(i,0) == 1) && (children_access(i,1) == 0)) {
        // child is free leaf
        // printf("child is free\n");
        this->createNodeChild(node, i);
        this->getNodeChild(node, i)->setLogOdds(this->clamping_thres_min);
      }
      else if ((children_access(i,0) == 0) && (children_access(i,1) == 1)) {
        // child is occupied leaf
        // printf("child is occupied\n");
        this->createNodeChild(node, i);
        this->getNodeChild(node, i)->setLogOdds(this->clamping_thres_max);
        // if (children_access(i,2) == 1) { // if binarized child is rough, set rough value to binary thres
        //   this->getNodeChild(node, i)->setRough(this->rough_binary_thres);
        boost::dynamic_bitset<> rough_bits(num_rough_bits);
        for (uint j=0; j<num_rough_bits; j++) {
          rough_bits[j] = children_access(i,2+j);
        }
        int binidx = rough_bits.to_ulong();
        double min=0.0, max=1.0; // max>1.0 to prevent overflow for rough=1.0
        double binsize = (max-min)/(num_binary_bins-1);
        float rough = binidx*binsize;
        float stair = children_access(i,2+num_rough_bits);
        // if (binidx==15) {
        //   std::cout << "new bits ";
        //   std::cout << rough_bits;
        //   printf(" = %d %f", binidx, rough);
        //   std::cout << "********************************";
        //   std::cout << std::endl;
        // }
        this->getNodeChild(node, i)->setRough(rough);
        this->getNodeChild(node, i)->setStairLogOdds(stair);
      }
      else if ((children_access(i,0) == 1) && (children_access(i,1) == 1)) {
        // child has children
        // printf("child is parent\n");
        this->createNodeChild(node, i);
        this->getNodeChild(node, i)->setLogOdds(-200.); // child is unkown, we leave it uninitialized
      }
        // printf("child is unknown\n");
    }

    // read children's children and set the label
    for (unsigned int i=0; i<8; i++) {
      if (this->nodeChildExists(node, i)) {
        RoughOcTreeNode* child = this->getNodeChild(node, i);
        if (fabs(child->getLogOdds() + 200.)<1e-3) { // has children?
          readBinaryNode(s, child);
          child->setLogOdds(child->getMaxChildLogOdds());
          child->setStairLogOdds(child->getMaxChildStairLogOdds());
        }
      } // end if child exists
    } // end for children

    return s;
  }

  std::ostream& RoughOcTree::writeBinaryNodeViaBinning(std::ostream &s, const RoughOcTreeNode* node) const{

    assert(node);

    uint num_rough_bits = log2(num_binary_bins);
    uint num_bits_per_node = 2+num_rough_bits+1;

    // 2+num_rough_bits for each children, 8 children per node -> (2+num_rough_bits)*8 bits total
    boost::dynamic_bitset<> children(num_bits_per_node*8);
    auto children_access = [&children,num_bits_per_node] (uint child, uint value) {return children[child*num_bits_per_node+value];}; // maps child and value indices to aligned char array, returns bitset reference

    // 10*** : child is free node
    // 01*** : child is occupied node
    // 00*** : child is unkown node
    // 11*** : child has children
    // **000 : child is max traversable
    // **111 : child is max rough

    // speedup: only set bits to 1, rest is init with 0 anyway,
    //          can be one logic expression per bit

    for (unsigned int i=0; i<8; i++) {
      if (this->nodeChildExists(node, i)) {
        const RoughOcTreeNode* child = this->getNodeChild(node, i);
        if      (this->nodeHasChildren(child))  { children_access(i,0) = 1; children_access(i,1) = 1; }
        else if (this->isNodeOccupied(child)) {
          children_access(i,0) = 0; children_access(i,1) = 1;
          if (child->isRoughSet()) {
            float rough = child->getRough();
            // float *rough_float = new float(child->getRough());
            // int *rough_int = reinterpret_cast<int*>(&rough_float); // use reinterpret_cast function
            // printf("********\nnew bits for %f %d: ",*rough_float,*rough_int);
            // for (int k = 31; k >=0; k--) // for loop to print out binary pattern
            // {
            //   int bit = ((*rough_int >> k)&1); // get the copied bit value shift right k times, then and with a 1.
            //   printf("%d ",bit);
            // }
            // char *rough_char = reinterpret_cast<char*>(rough_float); // use reinterpret_cast function
            // printf("new bits for %f %x:\t\t",*rough_float,*rough_char);
            // for (int k = 31; k >=0; k--) // for loop to print out binary pattern
            // {
            //   // int bit = ((*rough_int >> k)&1); // get the copied bit value shift right k times, then and with a 1.
            //   // int bit = ((*rough_char >> k)&1);
            //   std::cout << ((*rough_char>>k)&1);
            //   // std::cout << (*rough_char&(1<<k));
            //   std::cout << " ";
            // }
            // union { float in; int out; } data;
            // data.in = child->getRough();
            // std::bitset<sizeof(float)*8> bits(data.out);
            // std::cout << "new bits " << data.in << " ";
            // std::cout << bits;
            double min=0.0, max=1.0; // max>1.0 to prevent overflow for rough=1.0
            double binsize = (max-min)/(num_binary_bins-1);
            int binidx = floor(rough/binsize);
            boost::dynamic_bitset<> rough_bits(num_rough_bits, binidx);
            // std::cout << "new bits " << rough << " ";
            // std::cout << bits;
            // printf("\n");
            for (uint j=0; j<num_rough_bits; j++) {
              children_access(i,2+j) = rough_bits[j];
            }
            // if (binidx==15) {
            //   std::cout << "old bits ";
            //   std::cout << rough_bits;
            //   printf(" = %d %f", binidx, rough);
            //   std::cout << "********************************";
            //   std::cout << std::endl;
            // }
          }
          if (this->isNodeStairs(child)) {
            children_access(i,2+num_rough_bits) = 1;
          }
        }
        else { children_access(i,0) = 1; children_access(i,1) = 0; }
      }
      else {
        children_access(i,0) = 0; children_access(i,1) = 0; // shouldn't be necessary since default value is 0? but probably removed by compiler anyways?
      }
    }

    //     std::cout << "wrote: "
    //        << child1to4.to_string<char,std::char_traits<char>,std::allocator<char> >() << " "
    //        << child5to8.to_string<char,std::char_traits<char>,std::allocator<char> >() << std::endl;

    // char childset1_char = (char) children[0].to_ulong();
    // char childset2_char = (char) children[1].to_ulong();
    // char childset3_char = (char) children[2].to_ulong();

    // s.write((char*)&childset1_char, sizeof(char));
    // s.write((char*)&childset2_char, sizeof(char));
    // s.write((char*)&childset3_char, sizeof(char));

    // char *children_char = &((char)(children.to_ulong()));
    // s.write(children_char, sizeof(children));
    // for (int i=0; i<num_bits_per_node; i++) {
    //   std::bitset<8> children_byte;
    //   for (int j=0; j<8; j++) {
    //     children_byte[j] = children_access(j,i);
    //   }
    //   char children_char = (char) children_byte.to_ulong();
    //   s.write((char*)&children_char, sizeof(char));
    // }
    std::bitset<8> children_byte;
    for (int i=0; i<children.size(); i++) {
      children_byte[i%8] = children[i];
      if (i%8==7) {
        char children_char = (char) children_byte.to_ulong();
        s.write((char*)&children_char, sizeof(char));
      }
    }
    // write children's children
    for (unsigned int i=0; i<8; i++) {
      if (this->nodeChildExists(node, i)) {
        const RoughOcTreeNode* child = this->getNodeChild(node, i);
        if (this->nodeHasChildren(child)) {
          writeBinaryNode(s, child);
        }
      }
    }

    return s;
  }

  void RoughOcTree::writeRoughHistogram(std::string filename) {
#ifdef _MSC_VER
    fprintf(stderr, "The rough histogram uses gnuplot, this is not supported under windows.\n");
#else
    // build rough histogram
    int num_bins = 5;
    std::vector<int> histogram_rough (num_bins,0);
    for(RoughOcTree::tree_iterator it = this->begin_tree(),
          end=this->end_tree(); it!= end; ++it) {
      if (!it.isLeaf() || !this->isNodeOccupied(*it)) continue;
      float c = it->getRough();
      ++histogram_rough[(int)std::min((int)floor(c*num_bins),(int)(num_bins-1))];
    }
    // plot data
    FILE *gui = popen("gnuplot ", "w");
    fprintf(gui, "set term postscript eps enhanced color\n");
    fprintf(gui, "set output \"%s\"\n", filename.c_str());
    fprintf(gui, "plot [-1:%d] ",num_bins);
    fprintf(gui,"'-' w filledcurve lt 1 lc 1 tit \"r\",");
    fprintf(gui, "'-' w l lt 1 lc 1 tit \"\",");

    for (int i=0; i<num_bins; ++i) fprintf(gui,"%d %d\n", i, histogram_rough[i]);
    fprintf(gui,"0 0\n"); fprintf(gui, "e\n");
    for (int i=0; i<num_bins; ++i) fprintf(gui,"%d %d\n", i, histogram_rough[i]);
    fprintf(gui, "e\n");
    fflush(gui);
#endif
  }

  // std::ostream& operator<<(std::ostream& out, float const& c) {
  //   return out << c ;
  // }


  RoughOcTree::StaticMemberInitializer RoughOcTree::roughOcTreeMemberInit;

} // end namespace
