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
#include "TrackBase.h"
#include "../../../../include/hpvm.h"

cv::Point2f undistort_point_brown_illixr(cv::Point2f pt_in, cv::Matx33d *camK, cv::Vec4d *camD) {
    // Convert to opencv format
    cv::Mat mat(1, 2, CV_32F);
    mat.at<float>(0, 0) = pt_in.x;
    mat.at<float>(0, 1) = pt_in.y;
    mat = mat.reshape(2); // Nx1, 2-channel
    // Undistort it!
    cv::undistortPoints(mat, mat, (*camK), (*camD));
    // Construct our return vector
    cv::Point2f pt_out;
    mat = mat.reshape(1); // Nx2, 1-channel
    pt_out.x = mat.at<float>(0, 0);
    pt_out.y = mat.at<float>(0, 1);
    return pt_out;
}

using namespace ov_core;

void set_calibration_undistort(ov_core::Feature *feat, size_t bytes_feat, cv::Matx33d *camK, size_t bytes_camK, cv::Vec4d *camD, size_t bytes_camD, size_t camid){
    __hpvm__hint(hpvm::DEVICE);
    __hpvm__attributes(3, feat, camK, camD, 1, feat);
    void *thisNode = __hpvm__getNode();
    int m = __hpvm__getNodeInstanceID_x(thisNode);
    //std::cout << "after getnode\n";

    cv::Point2f pt(feat->uvs.at(camid).at(m)(0), feat->uvs.at(camid).at(m)(1));
    cv::Point2f pt_n = undistort_point_brown_illixr(pt, camK, camD);
    feat->uvs_norm.at(camid).at(m)(0) = pt_n.x;
    feat->uvs_norm.at(camid).at(m)(1) = pt_n.y;
    
    __hpvm__return(1, bytes_feat);
}

void set_calibration_undistort_wrapper(ov_core::Feature *feat, size_t bytes_feat, cv::Matx33d *camK, size_t bytes_camK, cv::Vec4d *camD, size_t bytes_camD, size_t camid, size_t loop_size){
    __hpvm__hint(hpvm::DEVICE);
    __hpvm__attributes(3, feat, camK, camD, 1, feat);
    //__hpvm__attributes(7, state, H_order_p, H_p, res_p, R_p, H_id_p, M_a, 1, M_a);
    void *SCUloop = __hpvm__createNodeND(1, set_calibration_undistort, loop_size);
 
    __hpvm__bindIn(SCUloop, 0, 0, 0); //feat
    __hpvm__bindIn(SCUloop, 1, 1, 0);
    __hpvm__bindIn(SCUloop, 2, 2, 0); //camK
    __hpvm__bindIn(SCUloop, 3, 3, 0);
    __hpvm__bindIn(SCUloop, 4, 4, 0); //camD
    __hpvm__bindIn(SCUloop, 5, 5, 0);
    __hpvm__bindIn(SCUloop, 6, 6, 0); //camid
 
    __hpvm__bindOut(SCUloop, 0, 0, 0); //return feat
    //for(size_t m=0; m<loop_size; m++) {
    //   set_calibration_undistort(feat, camid, m);
    //}
}

typedef struct __attribute__((__packed__)) {
    ov_core::Feature *feat;
    size_t bytes_feat; 
    cv::Matx33d *camK; 
    size_t bytes_camK;
    cv::Vec4d *camD;
    size_t bytes_camD; 
    size_t camid;
    size_t loop_size;
} SCUIn;

void ov_core::set_calibration_undistort_graph(ov_core::Feature *feat, cv::Matx33d *camK, cv::Vec4d *camD, size_t camid){
    SCUIn *SCUgraphArgs = (SCUIn *) malloc(sizeof(SCUIn));

    SCUgraphArgs->feat = feat;
    SCUgraphArgs->bytes_feat = sizeof(*feat); 
    SCUgraphArgs->camK = camK; 
    SCUgraphArgs->bytes_camK = sizeof(*camK);
    SCUgraphArgs->camD = camD;
    SCUgraphArgs->bytes_camD = sizeof(*camD); 
    SCUgraphArgs->camid = camid;
    SCUgraphArgs->loop_size = feat->uvs.at(camid).size();

    llvm_hpvm_track_mem(SCUgraphArgs->feat, sizeof(*feat));
    llvm_hpvm_track_mem(SCUgraphArgs->camK, sizeof(*camK));
    llvm_hpvm_track_mem(SCUgraphArgs->camD, sizeof(*camD));

    //launch loop
    //std::cout << "loopsize: " << SCUgraphArgs->loop_size << "\n";
    //std::cerr << "launch 5\n";
    void *ScuGraph = __hpvm__launch(0, set_calibration_undistort_wrapper, (void *)SCUgraphArgs);
    __hpvm__wait(ScuGraph);

    llvm_hpvm_request_mem(SCUgraphArgs->feat, sizeof(*feat));

    llvm_hpvm_untrack_mem(SCUgraphArgs->feat);
    llvm_hpvm_untrack_mem(SCUgraphArgs->camK);
    llvm_hpvm_untrack_mem(SCUgraphArgs->camD);

    free(SCUgraphArgs);
}
//using namespace ov_core;

void TrackBase::display_active(cv::Mat &img_out, int r1, int g1, int b1, int r2, int g2, int b2) {

    // Cache the images to prevent other threads from editing while we viz (which can be slow)
    std::map<size_t, cv::Mat> img_last_cache;
    for(auto const& pair : img_last) {
        img_last_cache.insert({pair.first,pair.second.clone()});
    }

    // Get the largest width and height
    int max_width = -1;
    int max_height = -1;
    for(auto const& pair : img_last_cache) {
        if(max_width < pair.second.cols) max_width = pair.second.cols;
        if(max_height < pair.second.rows) max_height = pair.second.rows;
    }

    // Return if we didn't have a last image
    if(max_width==-1 || max_height==-1)
        return;

    // If the image is "new" then draw the images from scratch
    // Otherwise, we grab the subset of the main image and draw on top of it
    bool image_new = ((int)img_last_cache.size()*max_width != img_out.cols || max_height != img_out.rows);

    // If new, then resize the current image
    if(image_new) img_out = cv::Mat(max_height,(int)img_last_cache.size()*max_width,CV_8UC3,cv::Scalar(0,0,0));

    // Loop through each image, and draw
    int index_cam = 0;
    for(auto const& pair : img_last_cache) {
        // Lock this image
        std::unique_lock<std::mutex> lck(mtx_feeds.at(pair.first));
        // select the subset of the image
        cv::Mat img_temp;
        if(image_new) cv::cvtColor(img_last_cache[pair.first], img_temp, CV_GRAY2RGB);
        else img_temp = img_out(cv::Rect(max_width*index_cam,0,max_width,max_height));
        // draw, loop through all keypoints
        for(size_t i=0; i<pts_last[pair.first].size(); i++) {
            // Get bounding pts for our boxes
            cv::Point2f pt_l = pts_last[pair.first].at(i).pt;
            // Draw the extracted points and ID
            cv::circle(img_temp, pt_l, 2, cv::Scalar(r1,g1,b1), CV_FILLED);
            //cv::putText(img_out, std::to_string(ids_left_last.at(i)), pt_l, cv::FONT_HERSHEY_SIMPLEX,0.5,cv::Scalar(0,0,255),1,cv::LINE_AA);
            // Draw rectangle around the point
            cv::Point2f pt_l_top = cv::Point2f(pt_l.x-5,pt_l.y-5);
            cv::Point2f pt_l_bot = cv::Point2f(pt_l.x+5,pt_l.y+5);
            cv::rectangle(img_temp,pt_l_top,pt_l_bot, cv::Scalar(r2,g2,b2), 1);
        }
        // Draw what camera this is
        cv::putText(img_temp, "CAM:"+std::to_string((int)pair.first), cv::Point(30,60), cv::FONT_HERSHEY_COMPLEX_SMALL, 3.0, cv::Scalar(0,255,0),3);
        // Replace the output image
        img_temp.copyTo(img_out(cv::Rect(max_width*index_cam,0,img_last_cache[pair.first].cols,img_last_cache[pair.first].rows)));
        index_cam++;
    }

}


void TrackBase::display_history(cv::Mat &img_out, int r1, int g1, int b1, int r2, int g2, int b2) {

    // Cache the images to prevent other threads from editing while we viz (which can be slow)
    std::map<size_t, cv::Mat> img_last_cache;
    for(auto const& pair : img_last) {
        img_last_cache.insert({pair.first,pair.second.clone()});
    }

    // Get the largest width and height
    int max_width = -1;
    int max_height = -1;
    for(auto const& pair : img_last_cache) {
        if(max_width < pair.second.cols) max_width = pair.second.cols;
        if(max_height < pair.second.rows) max_height = pair.second.rows;
    }

    // Return if we didn't have a last image
    if(max_width==-1 || max_height==-1)
        return;

    // If the image is "new" then draw the images from scratch
    // Otherwise, we grab the subset of the main image and draw on top of it
    bool image_new = ((int)img_last_cache.size()*max_width != img_out.cols || max_height != img_out.rows);

    // If new, then resize the current image
    if(image_new) img_out = cv::Mat(max_height,(int)img_last_cache.size()*max_width,CV_8UC3,cv::Scalar(0,0,0));

    // Max tracks to show (otherwise it clutters up the screen)
    //size_t maxtracks = 10;
    size_t maxtracks = (size_t)-1;

    // Loop through each image, and draw
    int index_cam = 0;
    for(auto const& pair : img_last_cache) {
        // Lock this image
        std::unique_lock<std::mutex> lck(mtx_feeds.at(pair.first));
        // select the subset of the image
        cv::Mat img_temp;
        if(image_new) cv::cvtColor(img_last_cache[pair.first], img_temp, CV_GRAY2RGB);
        else img_temp = img_out(cv::Rect(max_width*index_cam,0,max_width,max_height));
        // draw, loop through all keypoints
        for(size_t i=0; i<ids_last[pair.first].size(); i++) {
            // Get the feature from the database
            Feature* feat = database->get_feature(ids_last[pair.first].at(i));
            // Skip if the feature is null
            if(feat == nullptr || feat->uvs[pair.first].empty())
                continue;
            // Draw the history of this point (start at the last inserted one)
            for(size_t z=feat->uvs[pair.first].size()-1; z>0; z--) {
                // Check if we have reached the max
                if(feat->uvs[pair.first].size()-z > maxtracks)
                    break;
                // Calculate what color we are drawing in
                bool is_stereo = (feat->uvs.size() > 1);
                int color_r = (is_stereo? b2 : r2)-(int)((is_stereo? b1 : r1)/feat->uvs[pair.first].size()*z);
                int color_g = (is_stereo? r2 : g2)-(int)((is_stereo? r1 : g1)/feat->uvs[pair.first].size()*z);
                int color_b = (is_stereo? g2 : b2)-(int)((is_stereo? g1 : b1)/feat->uvs[pair.first].size()*z);
                // Draw current point
                cv::Point2f pt_c(feat->uvs[pair.first].at(z)(0),feat->uvs[pair.first].at(z)(1));
                cv::circle(img_temp, pt_c, 2, cv::Scalar(color_r,color_g,color_b), CV_FILLED);
                // If there is a next point, then display the line from this point to the next
                if(z+1 < feat->uvs[pair.first].size()) {
                    cv::Point2f pt_n(feat->uvs[pair.first].at(z+1)(0),feat->uvs[pair.first].at(z+1)(1));
                    cv::line(img_temp, pt_c, pt_n, cv::Scalar(color_r,color_g,color_b));
                }
                // If the first point, display the ID
                if(z==feat->uvs[pair.first].size()-1) {
                    //cv::putText(img_out0, std::to_string(feat->featid), pt_c, cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 255), 1, cv::LINE_AA);
                    //cv::circle(img_out0, pt_c, 2, cv::Scalar(color,color,255), CV_FILLED);
                }
            }
        }
        // Draw what camera this is
        cv::putText(img_temp, "CAM:"+std::to_string((int)pair.first), cv::Point(30,60), cv::FONT_HERSHEY_COMPLEX_SMALL, 3.0, cv::Scalar(0,255,0),3);
        // Replace the output image
        img_temp.copyTo(img_out(cv::Rect(max_width*index_cam,0,img_last_cache[pair.first].cols,img_last_cache[pair.first].rows)));
        index_cam++;
    }

}


