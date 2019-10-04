/*
 * OpenVINS: An Open Platform for Visual-Inertial Research
 * Copyright (C) 2019 Patrick Geneva
 * Copyright (C) 2019 Kevin Eckenhoff
 * Copyright (C) 2019 Guoquan Huang
 * Copyright (C) 2019 OpenVINS Contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */


#include <ros/ros.h>
#include <rosbag/bag.h>
#include <rosbag/view.h>
#include <sensor_msgs/Image.h>
#include <sensor_msgs/Imu.h>
#include <std_msgs/Float64.h>
#include <cv_bridge/cv_bridge.h>
#include <opencv/cv.hpp>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>

#include "core/VioManager.h"
#include "core/RosVisualizer.h"
#include "utils/dataset_reader.h"

#include <fstream>
#include <iostream>
#include <algorithm>

using namespace ov_msckf;


VioManager* sys;

struct vel_acc_vector {
	double x, y, z;
};

struct imu_data {
	vel_acc_vector angular_velocity;
	vel_acc_vector linear_acceleration;
};

void load_images(const string &file_name, unordered_map<double, string> &rgb_images,
				 vector<double> &timestamps) {
	ifstream file_in;
    file_in.open(file_name.c_str());
	// skip first line
	string s;
	getline(file_in, s);
    while (!file_in.eof()) {
        getline(file_in, s);
        if (!s.empty()) {
            stringstream ss;
            ss << s;
            double t;
            string rgb_image;
            ss >> t;
            timestamps.push_back(t);
            ss >> rgb_image;
			rgb_image.erase(std::remove(rgb_image.begin(), rgb_image.end(), ','), rgb_image.end());
            rgb_images[t] = rgb_image;
        }
    }

}

void load_imu_data(const string &file_name, unordered_map<double, imu_data> &imu_data_vals,
				   vector<double> &timestamps) {
	ifstream file_in;
    file_in.open(file_name.c_str());
	// skip first line
	string s;
	getline(file_in, s);
    while (!file_in.eof()) {
        getline(file_in, s);
        if (!s.empty()) {
            stringstream ss;
            ss << s;
            double t;
			imu_data imu_vals;
            ss >> t;
            timestamps.push_back(t);
            ss >> imu_vals.angular_velocity.x;
			ss >> imu_vals.angular_velocity.y;
			ss >> imu_vals.angular_velocity.z;
			ss >> imu_vals.linear_acceleration.x;
			ss >> imu_vals.linear_acceleration.y;
			ss >> imu_vals.linear_acceleration.z;
            imu_data_vals[t] = imu_vals;
        }
    }
}

// Main function
int main(int argc, char** argv) {

	if (argc != 6) {
		cerr << "Usage: ./run_serial_msckf path_to_cam0 path_to_cam1 path_to_imu0 path_to_cam0_images path_to_cam1_images" << endl;
		return 1;
	}

	//vector<string> cam0_images;
	unordered_map<double, string> cam0_images;
	//vector<string> cam1_images;
	unordered_map<double, string> cam1_images;
	unordered_map<double, imu_data> imu0_vals;
    vector<double> cam0_timestamps;
	vector<double> cam1_timestamps;
	vector<double> imu0_timestamps;
	string cam0_filename = string(argv[1]);
	string cam1_filename = string(argv[2]);
	string imu0_filename = string(argv[3]);
	string cam0_images_path = string(argv[4]);
	string cam1_images_path = string(argv[5]);
	
	load_images(cam0_filename, cam0_images, cam0_timestamps);
	load_images(cam1_filename, cam1_images, cam1_timestamps);
	load_imu_data(imu0_filename, imu0_vals, imu0_timestamps);
	//cout << "WTF2" << endl;

	cout << "cam0 images: " << cam0_images.size() << "  cam1 images: " << cam1_images.size() << "  imu0 data: " << imu0_vals.size() << endl;
	if (cam0_images.size() != cam1_images.size()) {
		cerr << "Mismatched number of cam0 and cam1 images!" << endl;
		return 1;
	}

	cout << cam0_images.at(1403715273262142976) << endl;
    // Create our VIO system
    sys = new VioManager();

    // Read in what mode we should be processing in (1=mono, 2=stereo)
    int max_cameras;
	max_cameras = 2; // max_cameras

    // Buffer variables for our system (so we always have imu to use)
    bool has_left = false;
    bool has_right = false;
    cv::Mat img0, img1;
    cv::Mat img0_buffer, img1_buffer;
    // double time = time_init.toSec();
    // double time_buffer = time_init.toSec();

	// Loop through data files (camera and imu)
	for (auto timem : imu0_timestamps) {

        // Handle IMU measurement
		if (imu0_vals.find(timem) != imu0_vals.end()) {
            // convert into correct format
			imu_data m = imu0_vals.at(timem);
            Eigen::Matrix<double, 3, 1> wm, am;
            wm << m.angular_velocity.x, m.angular_velocity.y, m.angular_velocity.z;
            am << m.linear_acceleration.x, m.linear_acceleration.y, m.linear_acceleration.z;
            // send it to our VIO system
            sys->feed_measurement_imu(timem, wm, am);
        }

        // Handle LEFT camera
		if (cam0_images.find(timem) != cam0_images.end()) {
            // Get the image
			cout << timem << endl;
			cout << cam0_images.at(timem) << endl;
			img0 = cv::imread(cam0_images_path+ "/" +cam0_images.at(timem), cv::IMREAD_COLOR);
			if (img0.empty()) {
				cerr << endl << "Failed to load image at: "
					 << cam0_images_path << "/" << cam0_images.at(timem) << endl;
				return 1;
			}
			cout << "Width : " << img0.cols << "  Height: " << img0.rows << endl;


            // // Save to our temp variable
            // has_left = true;
            // img0 = cv_ptr->image.clone();
            // time = cv_ptr->header.stamp.toSec();
        }

        // // Handle RIGHT camera
		// if (cam1_images.find(timem) != cam1_images.end()) {
        //     // Get the image
		// 	// TODO: Change this to read image from file
        //     cv_bridge::CvImageConstPtr cv_ptr;
        //     try {
        //         cv_ptr = cv_bridge::toCvShare(s1, sensor_msgs::image_encodings::MONO8);
        //     } catch (cv_bridge::Exception &e) {
        //         ROS_ERROR("cv_bridge exception: %s", e.what());
        //         continue;
        //     }
        //     // Save to our temp variable (use a right image that is near in time)
        //     // TODO: fix this logic as the left will still advance instead of waiting
        //     // TODO: should implement something like here:
        //     // TODO: https://github.com/rpng/MARS-VINS/blob/master/example_ros/ros_driver.cpp
        //     //if(std::abs(cv_ptr->header.stamp.toSec()-time) < 0.02) {
        //     has_right = true;
        //     img1 = cv_ptr->image.clone();
        //     //}
        // }


        // // Fill our buffer if we have not
        // if(has_left && img0_buffer.rows == 0) {
        //     has_left = false;
        //     time_buffer = time;
        //     img0_buffer = img0.clone();
        // }

        // // Fill our buffer if we have not
        // if(has_right && img1_buffer.rows == 0) {
        //     has_right = false;
        //     img1_buffer = img1.clone();
        // }


        // // If we are in monocular mode, then we should process the left if we have it
        // if(max_cameras==1 && has_left) {
        //     // process once we have initialized with the GT
        //     Eigen::Matrix<double, 17, 1> imustate;
        //     if(!gt_states.empty() && !sys->intialized() && DatasetReader::get_gt_state(time_buffer,imustate,gt_states)) {
        //         //biases are pretty bad normally, so zero them
        //         //imustate.block(11,0,6,1).setZero();
        //         sys->initialize_with_gt(imustate);
        //     } else if(gt_states.empty() || sys->intialized()) {
        //         sys->feed_measurement_monocular(time_buffer, img0_buffer, 0);
        //     }
        //     // visualize
        //     //viz->visualize();
        //     // reset bools
        //     has_left = false;
        //     // move buffer forward
        //     time_buffer = time;
        //     img0_buffer = img0.clone();
        // }


        // // If we are in stereo mode and have both left and right, then process
        // if(max_cameras==2 && has_left && has_right) {
        //     // process once we have initialized with the GT
        //     Eigen::Matrix<double, 17, 1> imustate;
        //     if(!gt_states.empty() && !sys->intialized() && DatasetReader::get_gt_state(time_buffer,imustate,gt_states)) {
        //         //biases are pretty bad normally, so zero them
        //         //imustate.block(11,0,6,1).setZero();
        //         sys->initialize_with_gt(imustate);
        //     } else if(gt_states.empty() || sys->intialized()) {
        //         sys->feed_measurement_stereo(time_buffer, img0_buffer, img1_buffer, 0, 1);
        //     }
        //     // visualize
        //     //viz->visualize();
        //     // reset bools
        //     has_left = false;
        //     has_right = false;
        //     // move buffer forward
        //     time_buffer = time;
        //     img0_buffer = img0.clone();
        //     img1_buffer = img1.clone();
        // }

    }

    // Finally delete our system
    delete sys;

    // // Done!
    return EXIT_SUCCESS;
}


















