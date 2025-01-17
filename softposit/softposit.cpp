#include "softposit.hpp"
#include <boost/format.hpp>

namespace bloody{

  using stat_info = arma::vec5;
  std::tuple<arma::umat, arma::mat> maxPosRatio(arma::mat assignMat);
  arma::mat sinkhornSlack(arma::mat M);
  arma::mat sinkhornImp(arma::mat M);
  int numMatches(arma::mat assignMat);

  boost::optional< std::tuple<Pose_type, match_type> > softposit(
    const std::vector<point2di_type>& imagePts,
    const std::vector<point3d_type>& worldPts,
    const Param_type& param,
    const Pose_type& initpose,
    boost::optional<const CamInfo_type&> maybe_caminfo)
  {

        using namespace std;

        std::vector<stat_info> stats;

        auto alpha = 5;//9.21*pow(param.noiseStd,2) + 1;
        auto maxDelta = 60;  //sqrt(alpha)/2; 20;         //  Max allowed error per world point.

        auto betaFinal = 0.5;                  // Terminate iteration when beta == betaFinal.
        auto betaUpdate = 1.05;                // Update rate on beta.
        auto epsilon0 = 0.01;                  // Used to initialize assignement matrix.

        auto maxCount = 5;
        auto minBetaCount = 1500;     
        auto nbImagePts = imagePts.size();
        auto nbWorldPts = worldPts.size();

        auto minNbPts = std::min(nbImagePts, nbWorldPts);
        auto maxNbPts = nbImagePts+nbWorldPts - minNbPts;

        auto scale = 1.0/(maxNbPts + 1);
        std::vector<bloody::point2di_type> imagePts_projected=imagePts;
        std::vector<point2d_type> _centeredImage(imagePts.size());
        arma::mat imageColors = arma::ones<arma::mat>(nbImagePts, 1)*2;
        arma::mat color_map = arma::join_rows(color_map, imageColors);
        double maxNumMatch=0;
        
        
        CamInfo_type caminfo;
        if (maybe_caminfo)
            caminfo = *maybe_caminfo;
        else{
            caminfo.focalLength = 1;
            caminfo.center = point2di_type{0, 0};
        }

        std::cout<<"init: "<<std::endl<<initpose.rot<<std::endl<<initpose.trans<<std::endl;

        std::transform(imagePts.begin(), imagePts.end(),
                    _centeredImage.begin(),
                    [&caminfo](const point2di_type & _pt){
                        return point2d_type((point2d_type{double(_pt[0]), double(_pt[1])} - caminfo.center));
                    });

        arma::mat centeredImage = arma::zeros<arma::mat>(_centeredImage.size(),2);

        for (int j=0; j<centeredImage.n_cols; ++j)
        {
            for (int i=0; i<centeredImage.n_rows; ++i){
                centeredImage(i,j) = _centeredImage[i][j]; //i->y,j->x
            }
        }

        //std::cout<<"centered image :"<<centeredImage<<std::endl;

        arma::mat homogeneousWorldPts = arma::zeros<arma::mat>(worldPts.size(), 4).eval();
        for (int i=0; i<worldPts.size(); ++i)
        {
            homogeneousWorldPts.row(i) = arma::rowvec{worldPts[i][0], worldPts[i][1], worldPts[i][2],1};
        }
        //std::cout<<"begin to make world point homogeneous:" << homogeneousWorldPts <<std::endl;

        auto pose = initpose;

        arma::mat wk = homogeneousWorldPts * arma::vec4 {pose.rot(2,0)/pose.trans[2], pose.rot(2,1)/pose.trans[2], pose.rot(2,2)/pose.trans[2],1};
        //std::cout <<"wk"<<wk<<std::endl;

        arma::vec4 r1T = {pose.rot(0,0)*caminfo.focalLength/pose.trans(2), pose.rot(0,1)*caminfo.focalLength/pose.trans(2), pose.rot(0,2)*caminfo.focalLength/pose.trans(2), pose.trans(0)*caminfo.focalLength/pose.trans(2)};  // Q1
        arma::vec4 r2T = {pose.rot(1,0)*caminfo.focalLength/pose.trans(2), pose.rot(1,1)*caminfo.focalLength/pose.trans(2), pose.rot(1,2)*caminfo.focalLength/pose.trans(2), pose.trans(1)*caminfo.focalLength/pose.trans(2)};  // Q2

        auto  betaCount = 0;
        auto poseConverged = 0;
        auto assignConverged = false;
        auto foundPose = 0;
        auto beta = param.beta0;

        arma::mat assignMat = arma::ones(nbImagePts+1,nbWorldPts+1) + epsilon0;

        auto imageOnes = arma::ones<arma::mat>(nbImagePts, 1);

        int debug_loop = 0;
        while (beta < betaFinal && !assignConverged)
        {
            std::cout<<boost::format("debug loop: %1%") % (debug_loop++)<<std::endl;

            arma::mat projectedU = homogeneousWorldPts * r1T;
            arma::mat projectedV = homogeneousWorldPts * r2T;

            arma::mat replicatedProjectedU = imageOnes * projectedU.t();
            arma::mat replicatedProjectedV = imageOnes * projectedV.t();

            //std::cout<<"r1T, r2T used:"<<std::endl<<r1T<<std::endl<<r2T<<std::endl;
            //std::cout<<"projected uv:"<<std::endl<<projectedU<<std::endl<<projectedV<<std::endl;
            //std::cout<<"reprojected uv"<<std::endl;
            //std::cout<<arma::mat(replicatedProjectedU)<<std::endl<<arma::mat(replicatedProjectedV)<<std::endl;

            //std::cout<<"SOP"<<std::endl;
            auto wkxj = centeredImage.col(0) * wk.t();
            auto wkyj = centeredImage.col(1) * wk.t();

            //std::cout<<"wkxj, wkyj"<<std::endl;
            //std::cout<<wkxj<<std::endl<<wkyj<<std::endl;

            //arma::mat distMat = caminfo.focalLength*caminfo.focalLength*(arma::square(replicatedProjectedU - wkxj) + arma::square (replicatedProjectedV - wkyj));
            arma::mat distMat = 1.0*1.0*(arma::square(replicatedProjectedU - wkxj) + arma::square (replicatedProjectedV - wkyj));
            std::cout<<"dist mat:"<<std::endl<<distMat<<std::endl;

            assignMat(arma::span(0, nbImagePts-1), arma::span(0, nbWorldPts-1)) = scale*arma::exp(-beta*(distMat - alpha));
            assignMat.col(nbWorldPts) = scale*arma::exp(-beta*arma::ones<arma::vec>(nbImagePts+1)*1e10-alpha);
            assignMat.row(nbImagePts) = scale*arma::exp(-beta*arma::ones<arma::vec>(nbWorldPts+1)*1e10-alpha).t(); //scale
            //std::cout<<"assign befor sinkhorn:"<<std::endl<<assignMat<<std::endl;

            //assignMat = sinkhornImp (assignMat);    // My "improved" Sinkhorn.
            assignMat = sinkhornSlack (assignMat);    
            std::cout<<"after sinkhorn Slack:"<<std::endl<<assignMat<<std::endl;

            auto numMatchPts = numMatches(assignMat);
            std::cout<<"num matches: "<<numMatchPts<<std::endl;

            auto sumNonslack = arma::accu(assignMat.submat(0,0,nbImagePts-1,nbWorldPts-1));
            //std::cout<<"sum non slack: "<<sumNonslack<<std::endl;

            arma::mat summedByColAssign = arma::sum(assignMat.submat(0, 0, nbImagePts-1, nbWorldPts-1));
            // std::cout<<summedByColAssign<<std::endl;
            arma::mat sumSkSkT = arma::zeros<arma::mat>(4, 4);
            
            for(auto  k = 0; k<nbWorldPts; ++k){
                sumSkSkT = sumSkSkT + summedByColAssign(k) * homogeneousWorldPts.row(k).t() * homogeneousWorldPts.row(k);
            }

            std::cout<<"check ill-condition"<<std::endl;
            if (arma::cond(sumSkSkT) > 1e40){
                std::cout<<"sumSkSkT is ill-conditioned, termininating search."<<std::endl;
                return boost::none;
            }
            arma::mat objectMat;
            try{
                objectMat = arma::inv(sumSkSkT);                           // Inv(L), a 4x4 matrix.
            }
            catch(runtime_error error){
                show_projected_img(imagePts_projected, color_map);
                throw runtime_error("can't get result!");
            }
            poseConverged = 0;                              // Initialize for POSIT loop.
            auto pose_iter_count = 0;

            // Save the previouse pose vectors for convergence checks.
            arma::vec4 r1Tprev = r1T;
            arma::vec4 r2Tprev = r2T;

            double Tx, Ty, Tz;
            arma::vec r1, r2, r3;
            double delta;

            std:cout<<"begin converge loop"<<std::endl;
            while (poseConverged == false & pose_iter_count < maxCount)
            {
                arma::vec weightedUi(4, arma::fill::zeros) ;
                arma::vec weightedVi(4, arma::fill::zeros) ;

                for (int j=0;j<nbImagePts; ++j){
                for (int k=0; k<nbWorldPts; ++k){
                    weightedUi = weightedUi + assignMat(j,k) * wk(k) * centeredImage(j,0) * homogeneousWorldPts.row(k).t();
                    weightedVi = weightedVi + assignMat(j,k) * wk(k) * centeredImage(j,1) * homogeneousWorldPts.row(k).t();
                }
                }

                r1T= objectMat * weightedUi;
                r2T = objectMat * weightedVi;

                //arma::mat U, V;
                arma::vec s;
                arma::mat X(3,2);

                X.col(0) = r1T(arma::span(0,2));
                X.col(1) = r2T(arma::span(0,2));

                //std::cout<<"svd"<<std::endl;
                //arma::svd(U,s,V, X);

                //arma::mat A = U * arma::mat("1 0; 0 1; 0 0") * V.t();
                //std::cout<<"r1T, r2T update: "<<r1T<< std::endl<<r2T<<std::endl;
                double s1 = -arma::sqrt(r1T(arma::span(0,2)).t()*r1T(arma::span(0,2))).eval()(0,0);
                // std::cout<<"S1   "<<s1<<std::endl;
                r1 = arma::vec3 {r1T(0)/s1, r1T(1)/s1, r1T(2)/s1};
                r2 = arma::vec3 {r2T(0)/s1, r2T(1)/s1, r2T(2)/s1};     
                r3 = arma::cross(r1,r2);
                Tz = caminfo.focalLength/s1;
                //Tz = 2 / (s(0) + s(1));
                Tx = r1T(3)/s1;
                Ty = r2T(3)/s1;
                auto r3T= arma::vec{r3[0], r3[1], r3[2], Tz};


                r1T = s1*arma::vec{r1[0], r1[1], r1[2], Tx};
                r2T = s1*arma::vec{r2[0], r2[1], r2[2], Ty};
                //std::cout<<"r1T, r2T update: "<<r1T<< std::endl<<r2T<<std::endl;

                wk = homogeneousWorldPts * r3T /Tz;

                std::cout<<"delta"<<std::endl;
                delta = sqrt(arma::accu(assignMat.submat(0, 0, nbImagePts-1, nbWorldPts-1) % distMat)/nbWorldPts);
                poseConverged = delta < maxDelta;
                std::cout<<"delta    "<<delta<<std::endl;
                std::cout<<"pose converged:"<<poseConverged<<std::endl;

                std::cout<<"generate trace"<<std::endl;

                auto trace = std::vector<double>{
                beta ,delta ,double(numMatchPts)/nbWorldPts ,
                double(sumNonslack)/nbWorldPts,
                arma::accu(arma::square(r1T-r1Tprev)) + arma::accu(arma::square(r2T-r2Tprev))
                };

                std::cout<<"keep log"<<std::endl;
                stats.push_back(arma::vec( trace));

                pose_iter_count = pose_iter_count + 1;
            }
            if(numMatchPts>maxNumMatch){
                maxNumMatch = double(numMatchPts);
                
            }
            
            if(numMatchPts==0 or maxNumMatch==numMatchPts){
                beta = betaUpdate * beta;
            }
            beta = betaUpdate * beta;
            std::cout<<"beta:  "<<beta<<std::endl;
            betaCount = betaCount + 1;
            assignConverged = (poseConverged && numMatchPts > 0.6*nbWorldPts);

            pose.trans = arma::vec{Tx, Ty, Tz};
            pose.rot.row(0) = r1.t();
            pose.rot.row(1) = r2.t();
            pose.rot.row(2) = r3.t();

            foundPose = (delta < maxDelta && numMatchPts > 0.6*nbWorldPts);

            std::cout<<"updated pose:"<<std::endl<<pose.rot<<std::endl<<pose.trans<<std::endl;
            //%% Log:
            std::cout<<"converge loop ends"<<std::endl;
            std::cout<<boost::format("pose found:%1%, delta exit:%2%, count exit:%3%")%foundPose%(delta<maxDelta)%(betaCount>minBetaCount)<<std::endl;
            int size1 = imagePts_projected.size();
            imagePts_projected = project_3DPoints(worldPts, imagePts_projected, pose.rot,  pose.trans, caminfo);
            int size2 = imagePts_projected.size();
            if(size2>size1)
            {
                if(beta < betaFinal && !assignConverged){
                    imageColors = arma::ones<arma::mat>(size2-size1, 1)*0;
                    color_map = arma::join_cols(color_map, imageColors);
                    show_projected_img(imagePts_projected, color_map);
                    color_map(arma::span(size1,size2-1),0)=arma::ones<arma::mat>(size2-size1, 1)*1;
                    
                }
                else{
                    imageColors = arma::ones<arma::mat>(size2-size1, 1)*0;
                    color_map = arma::join_cols(color_map, imageColors);
            
                }
                std::cout<<color_map.size()<<"   "<<imageColors.size()<<std::endl;
             
            }
            
        }
        std::cout<<"pose converged:"<<std::endl << pose.rot<<pose.trans<<std::endl;
        show_projected_img(imagePts_projected, color_map);
        return make_tuple(pose, match_type());
  }


  arma::mat sinkhornSlack(arma::mat M)
  {
    arma::mat normalizedMat ;
    auto iMaxIterSinkhorn=60;  // In PAMI paper
    auto fEpsilon2 = 0.001; // Used in termination of Sinkhorn Loop.
    auto alpha = 1e-200;
    auto iNumSinkIter = 0;
    int nbRows = M.n_rows;
    int nbCols = M.n_cols;

    auto fMdiffSum = fEpsilon2 + 1;

    while(fabs(fMdiffSum) > fEpsilon2 && iNumSinkIter < iMaxIterSinkhorn){
      auto Mprev = M; // % Save M from previous iteration to test for loop termination

      // Col normalization (except outlier row - do not normalize col slacks against each other)
      arma::rowvec McolSums = arma::sum(M); // Row vector.
      McolSums(nbCols-1) = 1; // Don't normalize slack col terms against each other.
      auto McolSumsRep = arma::ones<arma::vec>(nbRows) * McolSums ;
      M = M / (McolSumsRep+alpha);
      //std::cout<<"McolSumsRep: "<<McolSumsRep<<std::endl;
      // Row normalization (except outlier row - do not normalize col slacks against each other)
      arma::colvec MrowSums = arma::sum(M, 1); // Column vector.
      MrowSums(nbRows-1) = 1; // Don't normalize slack row terms against each other.
      auto MrowSumsRep = MrowSums * arma::ones<arma::rowvec>(nbCols);
      M = M / (MrowSumsRep+alpha);
      //std::cout<<"MrowSumsRep: "<<MrowSumsRep<<std::endl;
      iNumSinkIter=iNumSinkIter+1;
      fMdiffSum=arma::accu(arma::abs(M-Mprev));
    }

    normalizedMat = M;

    return normalizedMat;
  }


  int numMatches(arma::mat assignMat)
  {
    int num = 0;

    auto nimgpnts  = assignMat.n_rows - 1;
    auto nmodpnts = assignMat.n_cols - 1;

    for (int k = 0; k< nmodpnts; ++k){

      arma::uword imax;
      auto vmax = assignMat.col(k).max(imax);

      if(imax == assignMat.n_rows - 1) continue; // Slack value is maximum in this column.

      std::vector<arma::uword> other_cols;
      other_cols.reserve(assignMat.n_cols-1);
      for (int i=0; i<assignMat.n_cols; ++i)
        if (i!=k) other_cols.push_back(i);
      if (arma::all(arma::all(vmax > assignMat.submat(arma::uvec{imax},arma::uvec(other_cols)))))
        num = num + 1;              // This value is maximal in its row & column.
    }
    return num;
  }


  arma::mat sinkhornImp(arma::mat M){
    arma::mat normalizedMat;

    auto iMaxIterSinkhorn=60;          // In PAMI paper
    auto fEpsilon2 = 0.001;            // Used in termination of Sinkhorn Loop.

    auto iNumSinkIter = 0;
    auto nbRows = M.n_rows;
    auto nbCols = M.n_cols;

    auto fMdiffSum = fEpsilon2 + 1;    // Set "difference" from last M matrix above the loop termination threshold

// Get the positions and ratios to slack of the nonslack elements that are maximal in their row and column.
    arma::umat posmax;
    arma::mat ratios;
    std::tie(posmax, ratios) = maxPosRatio(M);
    std::cout<<"postmax, rations"<<posmax<<std::endl<<ratios<<std::endl;

    while(fabs(fMdiffSum) > fEpsilon2 && iNumSinkIter < iMaxIterSinkhorn)
    {
      auto Mprev = M;  // Save M from previous iteration to test for loop termination

      // Col normalization (except outlier row - do not normalize col slacks
      // against each other)
      arma::rowvec McolSums = arma::sum(M, 0);  // Row vector.
      McolSums(nbCols-1) = 1;  // Don't normalize slack col terms against each other.

      auto McolSumsRep = arma::ones<arma::vec>(nbRows) * McolSums ;
      M = M / McolSumsRep;

      // Fix values in the slack column.
      for (auto i=0; i< posmax.n_rows; ++i){
        M(posmax(i,0), nbCols-1) = ratios(i,0)*M(posmax(i,0),posmax(i,1));
      }
      // Row normalization (except outlier row - do not normalize col slacks against each other)
      arma::vec MrowSums = arma::sum(M, 1);  // Column vector.
      MrowSums(nbRows-1) = 1;  // Don't normalize slack row terms against each other.

      auto MrowSumsRep = MrowSums * arma::ones<arma::rowvec>(nbCols);
      M = M / MrowSumsRep;

      // Fix values in the slack row.
      for (auto i=0; i< posmax.n_rows; ++i){
        M(nbRows-1,posmax(i,1)) = ratios(i,1)*M(posmax(i,0),posmax(i,1));
      }
      iNumSinkIter=iNumSinkIter+1;
      fMdiffSum=arma::accu(arma::abs(M-Mprev));

    }
    normalizedMat = M;

    return normalizedMat;
  }


  std::tuple<arma::umat, arma::mat> maxPosRatio(arma::mat assignMat)
  {
//    function [pos, ratios] = maxPosRatio(assignMat)
    arma::umat pos;
    arma::mat ratios;

    auto nrows =  assignMat.n_rows; //size(assignMat,1);
    auto ncols = assignMat.n_cols; //size(assignMat,2);
    auto nimgpnts  = nrows - 1;
    auto nmodpnts = ncols - 1;

    for (auto k=0u; k<nmodpnts; ++k){
      arma::uword imax;
      auto vmax = assignMat.col(k).max(imax);

      if (imax == nrows-1) continue;                       // Slack value is maximum in this column.

// Check if the max value in the column is maximum within its row.
      std::vector<arma::uword> other_cols;
      for (int i=0; i<assignMat.n_cols; ++i) if (i!=k) other_cols.push_back(i);

      if (arma::all(arma::all(vmax > assignMat.submat(arma::uvec{imax},arma::uvec(other_cols))))){
        pos = arma::join_cols(pos, arma::umat(arma::urowvec{imax, k}));

        // Compute the ratios to row and column slack values.
        auto rr = assignMat(imax,ncols-1)/assignMat(imax,k);
        auto cr = assignMat(nrows-1,k)/assignMat(imax,k);
        ratios = arma::join_cols(ratios, arma::mat(arma::rowvec{rr, cr}));
      }
    }

    return std::make_tuple(pos, ratios);
  }

  void softposit(float* rot, float* trans, int* foundPose, int* _imagePts, float* _worldPts, int nbImagePts, int nbWorldPts, float beta0, float noiseStd, float* initRot, float* initTrans, float* focalLength, int* center)
  {

    std::vector<point2di_type> imagePts;
    std::vector<point3d_type> worldPts;

    for (uint i=0u; i<nbImagePts; ++i){
      imagePts.push_back(point2di_type{_imagePts[i*2], _imagePts[i*2+1]});
    }
    for (uint i=0u; i<nbWorldPts; ++i){
      worldPts.push_back(point3d_type{_worldPts[i*3], _worldPts[3*i+1], _worldPts[3*i+2]});
    }

    Pose_type initpose;
    for (int i=0,k=0; i<3; ++i){
      for (int j=0; j<3; ++j){
        initpose.rot(i,j) = initRot[k++];
      }
    }
    initpose.trans = point3d_type{initTrans[0], initTrans[1], initTrans[2]};

    Param_type param{beta0, noiseStd};
    if (focalLength){
      CamInfo_type caminfo{*focalLength, point2di_type{*center, *(center+1)}};
      auto maybe_result = softposit(imagePts, worldPts, param, initpose, caminfo);
    }else{
      auto maybe_result = softposit(imagePts, worldPts, param, initpose, boost::none);
    }
  }
}
