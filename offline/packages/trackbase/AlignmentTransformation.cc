#include "AlignmentTransformation.h"

#include "TrkrDefs.h"
#include "TpcDefs.h"
#include "ActsGeometry.h"

#include <cmath>
#include <fstream>

#include <Eigen/Dense>
#include <Eigen/Geometry>
#include <Eigen/LU>

#include <fun4all/Fun4AllReturnCodes.h>

#include <phool/PHCompositeNode.h>
#include <phool/getClass.h>
#include <phool/phool.h>
#include <phool/PHDataNode.h>
#include <phool/PHNode.h>
#include <phool/PHNodeIterator.h>
#include <phool/PHObject.h>
#include <phool/PHTimer.h>

#include <Acts/Surfaces/PerigeeSurface.hpp>
#include <Acts/Surfaces/PlaneSurface.hpp>
#include <Acts/Surfaces/Surface.hpp>

void AlignmentTransformation::createMap(PHCompositeNode* topNode)
{ 
std::cout << "Entering AlignmentTransformation::createMap..." << std::endl;

 createNodes(topNode);

 // Define Parsing Variables
 TrkrDefs::hitsetkey hitsetkey = 0;
 float alpha = 0.0, beta = 0.0, gamma = 0.0, dx = 0.0, dy = 0.0, dz = 0.0;
  
 // load alignment constants file
 std::ifstream datafile("data.txt");
      
 ActsSurfaceMaps surfMaps = m_tGeometry->maps();
 Surface surf;

 int fileLines = 1808;
 for (int i=0; i<fileLines; i++)
   {
     datafile >> hitsetkey >> alpha >> beta >> gamma >> dx >> dy >> dz;

     // Perturbation translations and angles for stave and sensor
     Eigen::Vector3d sensorAngles (alpha,beta,gamma);  
     Eigen::Vector3d millepedeTranslation(dx,dy,dz); 

     unsigned int trkrId = TrkrDefs::getTrkrId(hitsetkey); // specify between detectors

     if(trkrId == TrkrDefs::tpcId)
       {
	 unsigned int sector         = TpcDefs::getSectorId(hitsetkey);
	 unsigned int side           = TpcDefs::getSide(hitsetkey);
	 unsigned int subsurfkey_min = sector * 12 + (1-side) * 144;
	 unsigned int subsurfkey_max = subsurfkey_min + 12;

	 for(unsigned int subsurfkey = subsurfkey_min; subsurfkey<subsurfkey_max; subsurfkey++)
	   {
             surf = surfMaps.getTpcSurface(hitsetkey,subsurfkey);

	      Acts::Transform3 transform = makeTransform(surf, millepedeTranslation, sensorAngles);

             Acts::GeometryIdentifier id = surf->geometryId();
	     std::cout << " Add transform for surface GeometryIdentifier " << id << " trkrid " << trkrId << std::endl;
	     transformMap->addTransform(id,transform);
	   }
       }
     else 
       {
         surf = surfMaps.getSiliconSurface(hitsetkey);
	 Acts::Transform3 transform = makeTransform(surf, millepedeTranslation, sensorAngles);

         Acts::GeometryIdentifier id = surf->geometryId();
	 std::cout << " Add transform for surface GeometryIdentifier " << id << " trkrid " << trkrId << std::endl;
	 transformMap->addTransform(id,transform);
       }

     if(localVerbosity == true )
       {
	 std::cout << i << " " <<hitsetkey << " " <<alpha<< " " <<beta<< " " <<gamma<< " " <<dx<< " " <<dy<< " " <<dz << std::endl;
	 //transformMap->identify();
       }
   } 
 const auto map = transformMap->getMap();
 Acts::GeometryContext context(map);
 std::cout << " check:  get map2  " << std::endl;  
 const auto map2 = context.get<std::map<Acts::GeometryIdentifier, Acts::Transform3>>();
 std::cout << " check:  map2 size is " << map2.size() << std::endl; 
 m_tGeometry->geometry().geoContext = context.get<std::map<Acts::GeometryIdentifier, Acts::Transform3>>();

 // sPHENIXActsDetectorElement::use_alignment = true;
}

Eigen::Matrix3d AlignmentTransformation::rotateToGlobal(Surface surf)
{  
  /*
    Get ideal geometry rotation, by aligning surface to surface normal vector in global coordinates
    URL: https://math.stackexchange.com/questions/180418/calculate-rotation-matrix-to-align-vector-a-to-vector-b-in-3d
  */  
 
  Eigen::Vector3d ylocal(0,1,0);
  Eigen::Vector3d sensorNormal    = -surf->normal(m_tGeometry->geometry().geoContext);
  sensorNormal                    = sensorNormal/sensorNormal.norm(); // make unit vector 
  double cosTheta                 = ylocal.dot(sensorNormal);
  double sinTheta                 = (ylocal.cross(sensorNormal)).norm();
  Eigen::Vector3d vectorRejection = (sensorNormal - (ylocal.dot(sensorNormal))*ylocal)/(sensorNormal - (ylocal.dot(sensorNormal))*ylocal).norm();
  Eigen::Vector3d perpVector      =  sensorNormal.cross(ylocal);

  // Initialize and fill matrices (row,col)
  Eigen::Matrix3d fInverse;
  fInverse(0,0) = ylocal(0);
  fInverse(1,0) = ylocal(1);
  fInverse(2,0) = ylocal(2);
  fInverse(0,1) = vectorRejection(0);
  fInverse(1,1) = vectorRejection(1); 
  fInverse(2,1) = vectorRejection(2);
  fInverse(0,2) = perpVector(0);
  fInverse(1,2) = perpVector(1);
  fInverse(2,2) = perpVector(2);
  
  Eigen::Matrix3d G;
  G(0,0) =  cosTheta;
  G(0,1) = -sinTheta;
  G(0,2) =  0;
  G(1,0) =  sinTheta;
  G(1,1) =  cosTheta;
  G(1,2) =  0;
  G(2,0) =  0;
  G(2,1) =  0;
  G(2,2) =  1;

  Eigen::Matrix3d globalRotation = fInverse * G * (fInverse.inverse()); 

  if(localVerbosity == true)
    {
      std::cout<< " global rotation: "<< std::endl << globalRotation <<std::endl;
    }
  return globalRotation;
}

Acts::Transform3 AlignmentTransformation::makeAffineMatrix(Eigen::Matrix3d rotationMatrix, Eigen::Vector3d translationVector)
{
  // Creates 4x4 affine matrix given rotation matrix and translationVector 
  Acts::Transform3 affineMatrix;
  affineMatrix.linear() = rotationMatrix;
  affineMatrix.translation() = translationVector;
  return affineMatrix;
}

Acts::Transform3 AlignmentTransformation::makeTransform(Surface surf, Eigen::Vector3d millepedeTranslation, Eigen::Vector3d sensorAngles)
{
  // Create aligment rotation matrix
  Eigen::AngleAxisd alpha(sensorAngles(0), Eigen::Vector3d::UnitX());
  Eigen::AngleAxisd beta(sensorAngles(1), Eigen::Vector3d::UnitY());
  Eigen::AngleAxisd gamma(sensorAngles(2), Eigen::Vector3d::UnitZ());
  Eigen::Quaternion<double> q       = gamma*beta*alpha;
  Eigen::Matrix3d millepedeRotation = q.matrix();

  // Create ideal rotation matrix from ActsGeometry
  Eigen::Matrix3d globalRotation    = AlignmentTransformation::rotateToGlobal(surf);
  
  Eigen::Matrix3d combinedRotation  = globalRotation * millepedeRotation;
  Eigen::Vector3d sensorCenter      = surf->center(m_tGeometry->geometry().geoContext)*0.1;
  Eigen::Vector3d globalTranslation = sensorCenter + millepedeTranslation;
  Acts::Transform3 transformation    = AlignmentTransformation::makeAffineMatrix(combinedRotation,globalTranslation);

  if(localVerbosity == true)
    {
      std::cout << "sensor center: " << sensorCenter << " millepede translation: " << millepedeTranslation <<std::endl;
      std::cout << "Transform: "<< std::endl<< transformation.matrix()  <<std::endl;
    }

  return transformation;   
}


int AlignmentTransformation::createNodes(PHCompositeNode* topNode)
{
  m_tGeometry = findNode::getClass<ActsGeometry>(topNode, "ActsGeometry");
  if(!m_tGeometry)
    {
      std::cout << "ActsGeometry not on node tree. Exiting."
		<< std::endl;
      
      return Fun4AllReturnCodes::ABORTEVENT;
    }

  return 0; 
}

void AlignmentTransformation::createAlignmentTransformContainer(PHCompositeNode* topNode)
{
  //​ Get a pointer to the top of the node tree
  PHNodeIterator iter(topNode);
 
  PHCompositeNode *dstNode = dynamic_cast<PHCompositeNode*>(iter.findFirst("PHCompositeNode", "DST"));
  if (!dstNode)
    {
      std::cerr << "DST node is missing, quitting" << std::endl;
      throw std::runtime_error("Failed to find DST node in AlignmentTransformation::createNodes");
    }

  transformMap = findNode::getClass<alignmentTransformationContainer>(topNode, "alignmentTransformationContainer");
  if(!transformMap)
    {
      transformMap = new alignmentTransformationContainer;
      auto node    = new PHDataNode<alignmentTransformationContainer>(transformMap, "alignmentTransformationContainer");
      dstNode->addNode(node);
    }
}
