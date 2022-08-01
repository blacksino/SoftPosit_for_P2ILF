#include "./read_image.cpp"
#include "./read_pcd.cpp"
#include <iostream>
#include <string>
#include <vector>
#include "./softposit/softposit.hpp"
#include "./softposit/softposit.cpp"

using namespace cv;



int main()
{
    vector<int> raw_imagePts;
    raw_imagePts = read_img_to_point(); // read_image_file
    vector<float> raw_worldPts;
    raw_worldPts = read_pcd_from_json(); // read json file
    
    std::vector<bloody::point2di_type> imagePts;
    std::vector<bloody::point3d_type> worldPts;
    
    for (uint i=0u; i<raw_imagePts.size(); i+=2){
    imagePts.push_back(bloody::point2di_type{raw_imagePts[i+1], raw_imagePts[i]});      // get image points in (x, y) format
    //cout<<raw_imagePts[i]<<" "<<raw_imagePts[i+1]<<endl;
  }
    for (uint i=0u; i<raw_imagePts.size(); i+=3){
    worldPts.push_back(bloody::point3d_type{raw_worldPts[i], raw_worldPts[i+1], raw_worldPts[i+2]});    // get world 3D points
  }

  bloody::Param_type param{ 0.0001, 10.0}; //0.001
  bloody::CamInfo_type caminfo{500.0f, bloody::point2di_type{640, 360}}; // inner parameter of camera
    
  bloody::Pose_type initpose, gt_pose;
  Matx31f Rvec = Matx31f(0, 0.3, 0.2);
  Matx33f dst;
  Rodrigues(Rvec, dst, noArray());
  arma::mat R_change(3,3);
  for(int i=0;i<3;i++){
      for(int j=0;j<3;j++){
          R_change(i,j)=dst(i,j);
      }
  }
  arma::mat rot1 = arma::mat("0.82311, -0.33426, -0.45906;0.44522, -0.122, 0.88705;-0.35258, -0.93459, 0.048413");
  arma::mat rot2 = arma::mat("0.8231, 0.4452, -0.3525, 136.5396; -0.3343, -0.122, -0.9345, -113.6996; -0.4591, 0.8871, 0.0484, 167.6742; 0,0,0,1");
  //std::cout<<arma::inv(rot2)<<std::endl;
  gt_pose.rot = arma::mat("0.82311, -0.33426, -0.45906;0.44522, -0.122, 0.88705;-0.35258, -0.93459, 0.048413");//("0.8231, 0.4452, -0.3525; -0.3343, -0.122, -0.9345; -0.4591, 0.8871, 0.0484");
  //gt_pose.rot = rot2*gt_pose.rot;
  //gt_pose.rot = arma::inv(gt_pose.rot);
  //std::cout<<gt_pose.rot<<std::endl;
  // std::cout<<initpose.rot<<std::endl;
  gt_pose.trans = bloody::point3d_type{-73.420, -223.40, -66.24};//{-136.539, 113.6996, -167.6742};
  
  std::vector<bloody::point2di_type> imagePts_projected;
  imagePts_projected = project_3DPoints(worldPts, imagePts_projected,  gt_pose.rot,  gt_pose.trans, caminfo);   // project worldPts to 2D imagePts_projected(at read_pcd.cpp)
  
  
  //---------------------------------------------------------create init pose----------------------------------------------------------------
  int nImagePts = imagePts_projected.size();
  initpose.rot = arma::mat("0.82311, -0.33426, -0.45906;0.44522, -0.122, 0.88705;-0.35258, -0.93459, 0.048413");
  initpose.rot = R_change*initpose.rot;
  // std::cout<<initpose.rot<<std::endl;
  initpose.trans = bloody::point3d_type{-100, -170.40, -20.24};
  //imagePts_projected = project_3DPoints(worldPts, imagePts_projected,  initpose.rot,  initpose.trans, caminfo);
  
  arma::mat imageOnes = arma::ones<arma::mat>(nImagePts, 1)*2;
  arma::mat color_map = arma::join_rows(color_map, imageOnes);
  //show_projected_img(imagePts_projected, color_map);
  

  //softposit
  auto maybe_pose = softposit(
    imagePts_projected,
    worldPts,
    param,
    initpose,
    caminfo
    );
  
  
  // show result
  if (maybe_pose){
    auto pose = std::get<0>(*maybe_pose);
    std::cout<<pose.rot<<std::endl;
    std::cout<<pose.trans<<std::endl;
    
    //imagePts_projected = project_3DPoints(worldPts, imagePts_projected,  initpose.rot,  initpose.trans, caminfo);
    //imageOnes = arma::ones<arma::mat>(nImagePts, 1)*1;
    //color_map = arma::join_rows(color_map, imageOnes);
  
    imagePts_projected = project_3DPoints(worldPts, imagePts_projected, pose.rot,  pose.trans, caminfo);
    imageOnes = arma::ones<arma::mat>(nImagePts, 1)*0;
    color_map = arma::join_rows(color_map, imageOnes);
    show_projected_img(imagePts_projected, color_map);
  }
  else{
    std::cout<<"failed"<<std::endl;
  }

  return 0;
}
