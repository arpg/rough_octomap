/**
 * OctoMap ROS message conversions / [de-] serialization
 *
 * @author A. Hornung, University of Freiburg, Copyright (C) 2011-2013.
 * @see http://www.ros.org/wiki/octomap_ros
 * License: BSD
 */

/*
 * Copyright (c) 2011-2013, A. Hornung, University of Freiburg
 * All rights reserved.
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

#ifndef ROUGH_OCTOMAP_MSGS_CONVERT_MSGS_H
#define ROUGH_OCTOMAP_MSGS_CONVERT_MSGS_H

#include <octomap/octomap.h>
#include <octomap_msgs/Octomap.h>
#include <octomap/ColorOcTree.h>
#include <rough_octomap/RoughOcTree.h>

// new conversion functions
namespace octomap_msgs{
  // Note: fullMsgDataToMap() deleted, potentially causes confusion
  // and (silent) errors in deserialization

  /**
   * @brief Creates a new octree by deserializing from a message that contains the
   * full map information (i.e., binary is false) and returns an AbstractOcTree*
   * to it. You will need to free the memory when you're done.
   */
  static inline octomap::AbstractOcTree* fullMsgToMap(const Octomap& msg){
    octomap::AbstractOcTree* tree = octomap::AbstractOcTree::createTree(msg.id, msg.resolution);
    if (tree){
      std::stringstream datastream;
      if (msg.data.size() > 0){
        datastream.write((const char*) &msg.data[0], msg.data.size());
        tree->readData(datastream);
      }
      else
      {
        OCTOMAP_WARNING("fullMsgToMap received empty msg. Returning empty tree...\n");
      }
    }
    else
    {
      OCTOMAP_ERROR("Unable to createTree in fullMsgToMap.\n");
    }

    return tree;
  }


  template<class TreeType>
  void readTree(TreeType* octree, const Octomap& msg){
    // printf("readtree msgsize %d\n",msg.data.size());
    std::stringstream datastream;
    if (msg.data.size() > 0){
      datastream.write((const char*) &msg.data[0], msg.data.size());
      octree->readBinaryData(datastream);
    }
  }


  /**
   * @brief Creates a new octree by deserializing from msg,
   * e.g. from a message or service (binary: only free and occupied .bt file format).
   * This creates a new OcTree object and returns a pointer to it.
   * You will need to free the memory when you're done.
   */
   static inline octomap::AbstractOcTree* binaryMsgToMap(const Octomap& msg){
    // printf("binary msgtomap\n");
     if (!msg.binary)
       return NULL;

     // Check if this a stairs map first, then if not, just regular RoughOctree
     bool stairs = true;
     int bin_pos = 14;
     int str_pos = msg.id.find("RoughOcTree-S-");
     if (str_pos == std::string::npos) {
       str_pos = msg.id.find("RoughOcTree-");
       bin_pos = 12;
       stairs = false;
     }
     octomap::AbstractOcTree* tree;
     if (msg.id == "ColorOcTree"){
       octomap::ColorOcTree* octree = new octomap::ColorOcTree(msg.resolution);
       readTree(octree, msg);
       tree = octree;
     }
     else if (str_pos != std::string::npos){
       octomap::RoughOcTree* octree = new octomap::RoughOcTree(msg.resolution);
       octree->setStairsEnabled(stairs);
       // Set the number of bins, embedded in the id
       octree->setNumBins(stoi(msg.id.substr(bin_pos)));
       readTree(octree, msg);
       tree = octree;
     } else {
       octomap::OcTree* octree = new octomap::OcTree(msg.resolution);
       readTree(octree, msg);
       tree = octree;
     }
     return tree;
   }

  // Note: binaryMsgDataToMap() deleted, potentially causes confusion
  // and (silent) errors in deserialization


  /**
   * \brief Convert an octomap representation to a new octree (full probabilities
   * or binary). You will need to free the memory. Return NULL on error.
   **/
  static inline octomap::AbstractOcTree* msgToMap(const Octomap& msg){
    // printf("msgtomap\n");
    if (msg.binary)
      return binaryMsgToMap(msg);
    else
      return fullMsgToMap(msg);
  }

  // conversions via stringstream

  // TODO: read directly into buffer? see
  // http://stackoverflow.com/questions/132358/how-to-read-file-content-into-istringstream

  /**
   * @brief Serialization of an octree into binary data e.g. for messages and services.
   * Compact binary version (stores only max-likelihood free or occupied, .bt file format).
   * The data will be much smaller if you call octomap.toMaxLikelihood() and octomap.prune()
   * before.
   * @return success of serialization
   */
  template <class OctomapT>
  static inline bool binaryMapToMsgData(const OctomapT& octomap, std::vector<int8_t>& mapData){
    std::stringstream datastream;
    if (!octomap.writeBinaryConst(datastream))
      return false;

    std::string datastring = datastream.str();
    mapData = std::vector<int8_t>(datastring.begin(), datastring.end());
    return true;
  }

  /**
   * @brief Serialization of an octree into binary data e.g. for messages and services.
   * Full probability version (stores complete state of tree, .ot file format).
   * The data will be much smaller if you call octomap.toMaxLikelihood() and octomap.prune()
   * before.
   * @return success of serialization
   */
  template <class OctomapT>
  static inline bool fullMapToMsgData(const OctomapT& octomap, std::vector<int8_t>& mapData){
    std::stringstream datastream;
    if (!octomap.write(datastream))
      return false;

    std::string datastring = datastream.str();
    mapData = std::vector<int8_t>(datastring.begin(), datastring.end());
    return true;
  }

  // This is just to deal with the fact that only RoughOctomap has the getNumBins function
  template <typename T>
    class isRough {
      private:
        typedef char YesType[1];
        typedef char NoType[2];

        template <typename C> static YesType& test(decltype(&C::getNumBins));
        template <typename C> static NoType& test(...);

      public:
        enum { value = sizeof(test<T>(0)) == sizeof(YesType) };
    };

  template<typename T>
    typename std::enable_if<isRough<T>::value, std::string>::type
    Suffix(T* t) {
      std::string stairsPrefix;
      if (t->getStairsEnabled()) stairsPrefix = "-S";
      return stairsPrefix + "-" + std::to_string(t->getNumBins());
    }

  std::string Suffix(...) { return ""; }

  /**
   * @brief Serialization of an octree into binary data e.g. for messages and services.
   * Compact binary version (stores only max-likelihood free or occupied, .bt file format).
   * The data will be much smaller if you call octomap.toMaxLikelihood() and octomap.prune()
   * before.
   * @return success of serialization
   */
  template <class OctomapT>
  static inline bool binaryMapToMsg(OctomapT& octomap, Octomap& msg){
    msg.resolution = octomap.getResolution();
    msg.id = octomap.getTreeType() + Suffix(&octomap);
    msg.binary = true;

    std::stringstream datastream;
    // ROS_INFO("Writing binary data.");
    if (!octomap.writeBinaryData(datastream)) {
      ROS_ERROR("writeBinaryData failed.");
      return false;
    }

    std::string datastring = datastream.str();
    msg.data = std::vector<int8_t>(datastring.begin(), datastring.end());
    return true;
  }

  /**
   * @brief Serialization of an octree into binary data e.g. for messages and services.
   * Full probability version (stores complete state of tree, .ot file format).
   * The data will be much smaller if you call octomap.toMaxLikelihood() and octomap.prune()
   * before.
   * @return success of serialization
   */
  template <class OctomapT>
  static inline bool fullMapToMsg(const OctomapT& octomap, Octomap& msg){
    msg.resolution = octomap.getResolution();
    msg.id = octomap.getTreeType();
    msg.binary = false;

    std::stringstream datastream;
    if (!octomap.writeData(datastream))
      return false;

    std::string datastring = datastream.str();
    msg.data = std::vector<int8_t>(datastring.begin(), datastring.end());
    return true;
  }

}


#endif
