#include <iostream>
#include <string>
#include <stdio.h>
#include "ros.h"
#include <geometry_msgs\Point.h>
#include <geometry_msgs\Pose.h>
#include <std_msgs\String.h>
#include <std_msgs\Float64.h>
#include <geometry_msgs\Vector3.h>
#include <osiris\reaper_srv.h>
#include <osiris\ganesh_srv.h>
#include <osiris\comms_test_srv.h>
#include <osiris\Bone.h>
#include <osiris\GameObject.h>
#include <Windows.h>
#include <vector>
#include <map>

#define DllExport __declspec (dllexport)

extern "C" {
	DllExport struct unityPose {
		double px;
		double py;
		double pz;
		double ox;
		double oy;
		double oz;
		double ow;
	};
	DllExport struct gameObject {
		int unique_id;
		int frame_count;
		double time;
		const char * parent;
		unsigned int poses_length;
		unityPose * poses;
		// std::vector<unityPose> poses;
		bool has_event;
		const char * events;
		bool has_values;
		unsigned int values_length;
		double * values;
		// std::vector<double> values;
	};
	DllExport struct unityPoint {
		float px;
		float py;
		float pz;
	};
	DllExport struct accel {
		double x;
		double y;
		double z;
	};
}

// Maps (Dictionaries)
std::map<int, ros::Publisher> publishers;
std::map<int, std::string> topics;

// Shameful: We'll just constantly change these on run-time
geometry_msgs::Pose pose_msg;
geometry_msgs::Point point_msg;
geometry_msgs::Vector3 vector_msg;
osiris::Bone bone_msg;
osiris::GameObject go_msg;
std_msgs::String string_msg;
std_msgs::Float64 float_msg;

//// Publishers
//std::vector<ros::Publisher> publishers;
//typedef std::vector<ros::Publisher> vecPub;
//int numPubs;
//
//// Topics: Because Interop & Magical Disappearing Memory (Thanks C#!)
//std::vector<std::string> topics;
//typedef std::vector<std::string> vecStr;

// Initialization
char rosmaster[256] = "localhost";
bool init = false;
ros::NodeHandle nh;
const char *reaper = "reaper";
const char *comms_test = "comms_test";
const char *ganesh = "ganesh";

// Services
ros::ServiceClient<osiris::reaper_srvRequest, osiris::reaper_srvResponse> reaper_srv(reaper);
ros::ServiceClient<osiris::comms_test_srvRequest, osiris::comms_test_srvResponse> comms_test_srv(comms_test);
ros::ServiceClient<osiris::ganesh_srvRequest, osiris::ganesh_srvResponse> ganesh_srv(ganesh);

enum messages { pose, point, str, acc, flt, bone };

// Prototypes
int startROSSerial(char *ip);
void publishTopics();
void nodeHandleSpin();
void DebugInUnity(std::string message);


extern "C" {
	// Unity Debug
	typedef void(__stdcall * DebugCallback) (const char *str);
	DebugCallback gDebugCallback;

	DllExport void registerPublisher(int uid, char *_topic) {
		std::string topic = std::string(_topic);
		DebugInUnity("Registering " + topic + " with uid: " + std::to_string(uid));
		topics.insert(std::pair<int, std::string>(uid, topic));
		publishers.insert(std::pair<int, ros::Publisher>(uid, ros::Publisher(topics.at(uid).c_str(), &go_msg)));
	}

	DllExport int initNode(char *ip) {
		return startROSSerial(ip);
	}

	DllExport void advertise() {
		for (std::map<int, ros::Publisher>::iterator pb = publishers.begin(); pb != publishers.end(); ++pb)
		{
			DebugInUnity("Advertising uid: " + std::to_string(pb->first) + " with topic: " + std::string(pb->second.topic_));
			nh.advertise(pb->second);
		}

		nh.serviceClient(reaper_srv);
		nh.serviceClient(comms_test_srv);
		nh.serviceClient(ganesh_srv);
	}

	DllExport void spinOnce() {
		nh.spinOnce();
	}

	DllExport void closeNode() {
		publishers.clear();
	}

	DllExport void publish(int uid, gameObject _go) {
		osiris::GameObject go;
		go.unique_id = uid;
		go.frame_count = _go.frame_count;
		go.time = _go.time;
		go.parent = _go.parent;
		DebugInUnity(go.parent);
		go.poses_length = _go.poses_length;
		go.num_poses = _go.poses_length;
		go.poses = (geometry_msgs::Pose*)malloc(sizeof(geometry_msgs::Pose) * go.poses_length *2);
		for (int i = 0; i != go.poses_length; i++)
		{
			go.poses[i].position.x = _go.poses[i].px;
			go.poses[i].position.y = _go.poses[i].py;
			go.poses[i].position.z = _go.poses[i].pz;
			go.poses[i].orientation.x = _go.poses[i].ox;
			go.poses[i].orientation.y = _go.poses[i].oy;
			go.poses[i].orientation.z = _go.poses[i].oz;
			go.poses[i].orientation.w = _go.poses[i].ow;
		}
		
	    
		go.has_event = _go.has_event;
		if (go.has_event)
		{
			go.events = _go.events;
		}

		go.has_values = _go.has_values;
		
		if (go.has_values) 
		{
			go.values_length = _go.values_length;
			go.values = (double*)malloc(sizeof(double) * go.values_length);
			memcpy(&go.values, &_go.values, sizeof(double) * go.values_length);
		}

		DebugInUnity("ready to publish");
		publishers.at(uid).publish(&go);
		DebugInUnity("published");
	}

	DllExport void unityShutdown() {
		osiris::reaper_srvRequest req;
		osiris::reaper_srvResponse res;
		req.input = "unityshutdown";
		reaper_srv.call(req, res);
	}

	DllExport void shimmerShutdown() {
		osiris::reaper_srvRequest req;
		osiris::reaper_srvResponse res;
		req.input = "shimmershutdown";
		reaper_srv.call(req, res);
	}

	DllExport void commsTest(char* _text, char* ret) {
		std::string text = _text;
		osiris::comms_test_srvRequest req;
		osiris::comms_test_srvResponse res;
		req.input = text.c_str();
		comms_test_srv.call(req, res);
		strncpy(ret, res.hash, 32);
	}

	DllExport int beginRecord(char* _filename, char* _topics, char* ret) {
		std::string filename = _filename;
		std::string topics = _topics;
		osiris::ganesh_srvRequest req;
		osiris::ganesh_srvResponse res;
		req.command = "recordbegin";
		req.filename = filename.c_str();

		/* For another day
		std::vector<std::string> topicsSplit;
		std::string delim = " ";

		int numTopics = 0;
		auto start = 0U;
		auto end = topics.find(delim);
		while (end != std::string::npos) {
		topicsSplit.push_back(topics.substr(start, end - start));
		start = end + delim.length();
		end = topics.find(delim, start);
		numTopics++;
		}
		req.topics_length = numTopics;

		char** topicsOut = (char**)calloc((size_t)numTopics, sizeof(char*));

		req.topics = topicsOut;
		for (int i = 0; i < numTopics; i++) {

		}*/

		req.topics = topics.c_str();
		ganesh_srv.call(req, res);
		strncpy(ret, res.bag_output, 256);
		return res.success;
	}

	DllExport void endRecord() {
		osiris::ganesh_srvRequest req;
		osiris::ganesh_srvResponse res;
		req.command = "recordend";
		req.filename = "";
		req.topics = "";
		ganesh_srv.call(req, res);
	}

	// TODO? Maybe implement a way to get specify the bag we want scrapped?
	// instead of sorting by newest?
	DllExport void scrapRecord() {
		osiris::ganesh_srvRequest req;
		osiris::ganesh_srvResponse res;
		req.command = "recordend_scrap";
		req.filename = "";
		req.topics = "";
		ganesh_srv.call(req, res);
	}

	DllExport void RegisterDebugCallback(DebugCallback callback)
	{
		if (callback)
		{
			gDebugCallback = callback;
		}
	}
}

void nodeHandleSpin() {
	// Simply calls spinOnce on the nodeHandle after data transmission
	
}

void publishTopics() {

	// Tagging along for now
}

int startROSSerial(char *_ip) {
	// ROS Master @ Ubuntu on VM
	strcpy(rosmaster, _ip);
	// TODO: Add try-catch
	int result = 1;
	result = nh.initNode(rosmaster);

	DebugInUnity("HELLO FROM NATIVE CODE!");

	init = true;
	return result;

	// Since C++/C# Interop arrays are finicky, we will add topics using another function and let native/C++ handle it
	// Unity will call multiple addTopic()s and finally publishTopics()
}

void DebugInUnity(std::string message)
{
	if (gDebugCallback)
	{
		gDebugCallback(message.c_str());
	}
}

