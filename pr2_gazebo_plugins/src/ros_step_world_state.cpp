/*
 *  Gazebo - Outdoor Multi-Robot Simulator
 *  Copyright (C) 2003
 *     Nate Koenig & Andrew Howard
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
/*
 * Desc: Empty gazebo plugin
 * Author: John Hsu
 * Date: 24 July 2009
 * SVN info: $Id$
 */


#include <algorithm>
#include <assert.h>

#include <pr2_gazebo_plugins/ros_step_world_state.h>

#include <gazebo/Global.hh>
#include <gazebo/XMLConfig.hh>
#include <gazebo/gazebo.h>
#include <gazebo/GazeboError.hh>
#include <gazebo/ControllerFactory.hh>

using namespace gazebo;

GZ_REGISTER_DYNAMIC_CONTROLLER("ros_step_world_state", RosStepWorldState);

////////////////////////////////////////////////////////////////////////////////
// Constructor
RosStepWorldState::RosStepWorldState(Entity *parent)
    : Controller(parent)
{
  this->parent_model_ = dynamic_cast<Model*>(this->parent);

  if (!this->parent_model_)
    gzthrow("GazeboMechanismControl controller requires a Model as its parent");

  Param::Begin(&this->parameters);
  this->robotNamespaceP = new ParamT<std::string>("robotNamespace", "/", 0);
  this->topicNameP = new ParamT<std::string>("topicName", "gazebo_world_state", 0);
  this->frameNameP = new ParamT<std::string>("frameName", "base_link", 0);
  Param::End();

  this->worldStateConnectCount = 0;
}

////////////////////////////////////////////////////////////////////////////////
// Destructor
RosStepWorldState::~RosStepWorldState()
{
  delete this->robotNamespaceP;
  delete this->topicNameP;
  delete this->frameNameP;
  delete this->rosnode_;
}

////////////////////////////////////////////////////////////////////////////////
// Load the controller
void RosStepWorldState::LoadChild(XMLConfigNode *node)
{
  this->robotNamespaceP->Load(node);
  this->robotNamespace = this->robotNamespaceP->GetValue();

  int argc = 0;
  char** argv = NULL;
  ros::init(argc,argv,"gazebo");
  this->rosnode_ = new ros::NodeHandle(this->robotNamespace);

  this->topicNameP->Load(node);
  this->topicName = this->topicNameP->GetValue();
  this->frameNameP->Load(node);
  this->frameName = this->frameNameP->GetValue();

  this->pub_ = this->rosnode_->advertise<pr2_gazebo_plugins::WorldState>(this->topicName,1,
    boost::bind( &RosStepWorldState::WorldStateConnect, this),
    boost::bind( &RosStepWorldState::WorldStateDisconnect, this));
}

////////////////////////////////////////////////////////////////////////////////
// Someone subscribes to me
void RosStepWorldState::WorldStateConnect()
{
  this->worldStateConnectCount++;
}

////////////////////////////////////////////////////////////////////////////////
// Someone subscribes to me
void RosStepWorldState::WorldStateDisconnect()
{
  this->worldStateConnectCount--;
}

////////////////////////////////////////////////////////////////////////////////
// Initialize the controller
void RosStepWorldState::InitChild()
{
}

////////////////////////////////////////////////////////////////////////////////
// Update the controller
void RosStepWorldState::UpdateChild()
{
  /***************************************************************/
  /*                                                             */
  /*  this is called at every update simulation step             */
  /*                                                             */
  /***************************************************************/
  if (this->worldStateConnectCount == 0)
    return;

  /***************************************************************/
  /*                                                             */
  /*  publish                                                    */
  /*                                                             */
  /***************************************************************/
  double cur_time = Simulator::Instance()->GetSimTime();

  this->bodies = this->parent_model_->GetBodies();
  if (this->bodies)
  {
    this->lock.lock();

    // Add Frame Name
    this->worldStateMsg.header.frame_id = this->frameName;
    this->worldStateMsg.header.stamp.fromSec(cur_time);

    this->worldStateMsg.set_name_size(this->bodies->size());
    this->worldStateMsg.set_pose_size(this->bodies->size());
    this->worldStateMsg.set_twist_size(this->bodies->size());
    this->worldStateMsg.set_wrench_size(this->bodies->size());

    // Iterate through all bodies
    std::map<std::string, Body*>::const_iterator biter;
    int count = 0;
    for (biter=this->bodies->begin(); biter!=this->bodies->end(); biter++)
    {
      //ROS_ERROR("body name: %s",(biter->second)->GetName().c_str());
      // get name
      this->worldStateMsg.name[count] =  biter->second->GetName();

      // set pose
      // get pose from simulator
      Pose3d pose;
      Quatern rot;
      Vector3 pos;
      // Get Pose/Orientation ///@todo: verify correctness
      pose = (biter->second)->GetPose();

      // apply xyz offsets and get position and rotation components
      pos = pose.pos; // (add if there's offset) + this->xyzOffsets;
      rot = pose.rot;
      // apply rpy offsets
      /* add if there's offsets
      Quatern qOffsets;
      qOffsets.SetFromEuler(this->rpyOffsets);
      rot = qOffsets*rot;
      rot.Normalize();
      */
    
      this->worldStateMsg.pose[count].position.x    = pos.x;
      this->worldStateMsg.pose[count].position.y    = pos.y;
      this->worldStateMsg.pose[count].position.z    = pos.z;
      this->worldStateMsg.pose[count].orientation.x = rot.x;
      this->worldStateMsg.pose[count].orientation.y = rot.y;
      this->worldStateMsg.pose[count].orientation.z = rot.z;
      this->worldStateMsg.pose[count].orientation.w = rot.u;

      // set velocities
      // get Rates
      Vector3 vpos = (biter->second)->GetPositionRate(); // get velocity in gazebo frame
      Quatern vrot = (biter->second)->GetRotationRate(); // get velocity in gazebo frame
      Vector3 veul = (biter->second)->GetEulerRate(); // get velocity in gazebo frame

      // pass linear rates
      this->worldStateMsg.twist[count].linear.x        = vpos.x;
      this->worldStateMsg.twist[count].linear.y        = vpos.y;
      this->worldStateMsg.twist[count].linear.z        = vpos.z;
      // pass euler angular rates
      this->worldStateMsg.twist[count].angular.x    = veul.x;
      this->worldStateMsg.twist[count].angular.y    = veul.y;
      this->worldStateMsg.twist[count].angular.z    = veul.z;

      // get forces
      Vector3 force = (biter->second)->GetForce(); // get velocity in gazebo frame
      Vector3 torque = (biter->second)->GetTorque(); // get velocity in gazebo frame
      this->worldStateMsg.wrench[count].force.x = force.x;
      this->worldStateMsg.wrench[count].force.x = force.y;
      this->worldStateMsg.wrench[count].force.x = force.z;
      this->worldStateMsg.wrench[count].torque.x = torque.x;
      this->worldStateMsg.wrench[count].torque.x = torque.y;
      this->worldStateMsg.wrench[count].torque.x = torque.z;

      count++;
    }

    this->pub_.publish(this->worldStateMsg);
    this->lock.unlock();
  }
}

////////////////////////////////////////////////////////////////////////////////
// Finalize the controller
void RosStepWorldState::FiniChild()
{
}



