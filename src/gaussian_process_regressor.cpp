#include "gaussian_process_regressor.h"
gaussian_process_regressor::gaussian_process_regressor(){
    gp_mutex = std::shared_ptr<std::mutex>(new std::mutex());
    GP = gaussian_process(3);
}

gaussian_process_regressor::gaussian_process_regressor(ros::NodeHandle &nh, std::string id, std::string frame_id){

    ap_id = std::string(id);
    fixed_frame_id = frame_id;

    gp_mutex = std::shared_ptr<std::mutex>(new std::mutex());

    int i = id.find_first_of(":");
    while (i>=0) {
        ap_id[i]='_';
        i=ap_id.find_first_of(":",i+1);
    }

    mean_publisher = nh.advertise<sensor_msgs::PointCloud2>("pcloud_"+ap_id+"_mean",1,true);
    variance_publisher = nh.advertise<sensor_msgs::PointCloud2>("pcloud_"+ap_id+"_var",1,true);

    GP = gaussian_process(3);
}

void gaussian_process_regressor::publish_clouds(){ 
    gp_mutex->lock();
    //if (gp_pcl_pub.getNumSubscribers()==0){
    //    return;
    //}
    ROS_INFO("Publishing point cloud for %s ",ap_id.c_str());

    pcl::PointCloud<pcl::PointXYZI> cloud_mean, cloud_var;

    double grid_size = 30; // 10 meters
    int n_points = 5000; 

    cloud_mean.header.frame_id = fixed_frame_id;
    cloud_var.header.frame_id = fixed_frame_id;

    int idx =0;
    double z_var;
    VectorXd z_mean;
    VectorXd X = VectorXd::Zero(GP.input_dimensions());

    for (idx=0; idx < n_points ; idx++){
            pcl::PointXYZI mean, variance;
            X = VectorXd::Random(2)*grid_size;
            mean.x = X[0]; variance.x = X[0];
            mean.y = X[1]; variance.y = X[1];

            // computed predicted singal measruement and variance
            GP.prediction(X,z_mean,z_var);

            mean.z = z_mean[0];
            variance.z = z_var;
            mean.intensity = z_mean[0]; variance.intensity = z_var;
            cloud_mean.push_back(mean);
            cloud_var.push_back(variance);
    }
    // publish point cloud
    mean_publisher.publish(cloud_mean);
    variance_publisher.publish(cloud_var);
    ROS_INFO("Published %d points ", idx);
    gp_mutex->unlock();
};

void gaussian_process_regressor::process_measurement(tf::StampedTransform &pose_transform, const wifi_mapping::wifi_measurement::ConstPtr &wifi_msg){
    // construct measurement vector, for the moment, we only use translation data
    Vector3d pos_x;
    tf::vectorTFToEigen(pose_transform.getOrigin(),pos_x);
    VectorXd x(pos_x);

    double ss = 100*wifi_msg->signal_strength/255.0; 
     
    ROS_INFO("========>\n New sample for %s (essid: %s)",ap_id.c_str(),wifi_msg->essid.c_str()); 
    ROS_INFO("pose_msg time: %f, wifi_msg time: %f", pose_transform.stamp_.toSec(), wifi_msg->header.stamp.toSec()); 
    ROS_INFO("Position [%f %f %f]",x[0],x[1],x[2]); 
    ROS_INFO("Signal Strength [%f] ",ss); 


    // TODO find a better way of adding samples
    double prediction_variance;
    VectorXd predicted_signal_strength;

    GP.prediction(x,predicted_signal_strength,prediction_variance);
    //ROS_INFO("Measured Signal Strength: %f, Prediction: %f, Variance: %f",wifi_msg->signal_strength,predicted_signal_strength[0],prediction_variance);
    bool add_measurement = (prediction_variance >= 0.5*variance_threshold) || (GP.dataset_size()<50);
    
    if(add_measurement){
        gp_mutex->lock();
        GP.add_sample(x,ss);
        gp_mutex->unlock();

        ROS_INFO("Added sample to GP estimator");
        ROS_INFO("Total data points: %d",GP.dataset_size());
        // optimize every 10 data points
        if(GP.dataset_size()%10 == 0){
            Vector4d init_params = VectorXd::Random(5);
            for (int i=0; i<init_params.size(); i++){
                init_params(i) += 1.0;
            }
            GP.set_opt_starting_point(init_params);
            gp_mutex->lock();
            GP.optimize_parameters();
            gp_mutex->unlock();
        }

        variance_threshold = GP.compute_maximum_variance();
        publish_clouds();
   }
}
