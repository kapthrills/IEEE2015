/*
 * Copyright (C) 2008, Morgan Quigley and Willow Garage, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the names of Stanford University or Willow Garage, Inc. nor the names of its
 *     contributors may be used to endorse or promote products derived from
 *     this software without specific prior written permission.
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

// %Tag(FULLTEXT)%
#include "ros/ros.h"
#include "std_msgs/String.h"

#include <stdio.h>
#include <signal.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <string.h>
#include <time.h>


#include "high_level_commands.h"
#include "communications.h"


void main_loop(){
  while(1){
    SetVelocity(1,1000);
    SetPosition(1,500);
    sleep(1);
  }
}

void ListAllDynamixels(int usb2ax_index) {
  int baud_num, dxl_id;
  int model_num, firmware_version;
  for (baud_num = 3; baud_num >= 0; baud_num--) {
    if(InitDXL(usb2ax_index, baud_num) == 0 ) {
      printf("Failed to open device: %d, baud num: %d\n", usb2ax_index, baud_num);
      TerminateDXL();
      return;
    } else {
      printf("Searching on device: %d, baud num: %d\n", usb2ax_index, baud_num);
    }

    for (dxl_id = 0; dxl_id < 200; dxl_id++) {
      if (SendPing(dxl_id, &model_num, &firmware_version)) {
        
        printf("Device found| Baud rate %d, ID: %d, Model number: %d, Firmware version: %d\n", baud_num, dxl_id, model_num, firmware_version);
        SetID(1,2);
        main_loop();
      }
    }
    }
    // ID 200 is one of the controllers or something.
    // ID 253 is USB2AX
    // ID 254 is broadcast
    for (dxl_id = 201; dxl_id < 253; dxl_id++) {
      if (SendPing(dxl_id, &model_num, &firmware_version)) {
        printf("Device found| Baud rate: %d, ID: %d, Model number: %d, Firmware version: %d\n", baud_num, dxl_id, model_num, firmware_version);
      }
    }
  
}

void ListDevices() {
  DIR * dir;
  struct dirent * ent;
  char* devices_dir = "/dev/";
  char* expected_prefix = "ttyACM";
  int i;
  if ((dir = opendir (devices_dir)) != NULL) {
    while ((ent = readdir (dir)) != NULL) {
      if (strlen(expected_prefix) <= strlen(ent->d_name)) {
        printf("%s\n", ent->d_name);
      }
      else{
        printf("%s\n", "wrong" );
      }
    }
    closedir (dir);
  } else {
    printf("Couldn't find device directory: %s\n", devices_dir);
  }
}

/**
 * This tutorial demonstrates simple receipt of messages over the ROS system.
 */
// %Tag(CALLBACK)%
void chatterCallback(const std_msgs::String::ConstPtr& msg)
{
  ROS_INFO("I heard: [%s]", msg->data.c_str());
}
// %EndTag(CALLBACK)%

int main(int argc, char **argv)
{
  /**
   * The ros::init() function needs to see argc and argv so that it can perform
   * any ROS arguments and name remapping that were provided at the command line. For programmatic
   * remappings you can use a different version of init() which takes remappings
   * directly, but for most command-line programs, passing argc and argv is the easiest
   * way to do it.  The third argument to init() is the name of the node.
   *
   * You must call one of the versions of ros::init() before using any other
   * part of the ROS system.
   */
  ros::init(argc, argv, "listener");

  /**
   * NodeHandle is the main access point to communications with the ROS system.
   * The first NodeHandle constructed will fully initialize this node, and the last
   * NodeHandle destructed will close down the node.
   */
  ros::NodeHandle n;

  /**
   * The subscribe() call is how you tell ROS that you want to receive messages
   * on a given topic.  This invokes a call to the ROS
   * master node, which keeps a registry of who is publishing and who
   * is subscribing.  Messages are passed to a callback function, here
   * called chatterCallback.  subscribe() returns a Subscriber object that you
   * must hold on to until you want to unsubscribe.  When all copies of the Subscriber
   * object go out of scope, this callback will automatically be unsubscribed from
   * this topic.
   *
   * The second parameter to the subscribe() function is the size of the message
   * queue.  If messages are arriving faster than they are being processed, this
   * is the number of messages that will be buffered up before beginning to throw
   * away the oldest ones.
   */
// %Tag(SUBSCRIBER)%
  ros::Subscriber sub = n.subscribe("end_effector_servos", 1000, chatterCallback);
// %EndTag(SUBSCRIBER)%

  /**
   * ros::spin() will enter a loop, pumping callbacks.  With this version, all
   * callbacks will be called from within this thread (the main one).  ros::spin()
   * will exit when Ctrl-C is pressed, or the node is shutdown by the master.


   */

  int device_num = 0; // Default to device ID 0
  if (argc >= 2) {
    if (!(argv[2])) {
      ListAllDynamixels(device_num);
    } else {
      printf("Arg 1 (%s) was not recognized as an integer device number.\n", argv[1]);
    }
  } else {
    printf("Enter USB2AX ttyACM number as argument 1!\n");
    printf("Here are some possible device ID's from /dev/*.\n");
    ListDevices();
  }
  return 0;


// %Tag(SPIN)%
  ros::spin();
// %EndTag(SPIN)%

  return 0;
}
// %EndTag(FULLTEXT)%