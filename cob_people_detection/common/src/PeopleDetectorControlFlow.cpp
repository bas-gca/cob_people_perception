/// @file PeopleDetectorControlFlow.cpp


#ifdef __LINUX__
	#include "cob_people_detection/PeopleDetectorControlFlow.h"
#else
	#include "cob_vision/cob_people_detection/common/include/cob_people_detection/PeopleDetectorControlFlow.h"
#endif


using namespace ipa_PeopleDetector;

PeopleDetectorControlFlow::PeopleDetectorControlFlow(void)
{	
	m_PeopleDetector = 0;
	m_filname = 0;
}

PeopleDetectorControlFlow::~PeopleDetectorControlFlow(void)
{
}

unsigned long PeopleDetectorControlFlow::Init(std::string directory, 
												ipa_CameraSensors::AbstractColorCameraPtr* colorCamera0,
												ipa_CameraSensors::AbstractColorCameraPtr* colorCamera1,
												ipa_CameraSensors::AbstractRangeImagingSensorPtr* rangeImagingSensor)
{
	std::string iniFileNameAndPath = directory + "peopleDetectorIni.xml";
	
	if (CameraSensorsControlFlow::Init(directory, "peopleDetectorIni.xml", colorCamera0, colorCamera1, rangeImagingSensor) & ipa_Utils::RET_FAILED)
	{
		std::cerr << "ERROR - CameraDataViewerControlFlow::Init:" << std::endl;
		std::cerr << "\t ... Could not initialize 'CameraSensorsControlFlow'" << std::endl;
		return ipa_Utils::RET_FAILED;	
	}

	m_PeopleDetector = new ipa_PeopleDetector::PeopleDetector();

	if (m_PeopleDetector->Init(directory) & ipa_Utils::RET_FAILED)
	{	
		std::cerr << "ERROR - PeopleDetectorControlFlow::Init:" << std::endl;
		std::cerr << "\t ... Could not initialize people detector library.\n";
		return ipa_Utils::RET_FAILED;
	}

	if(LoadParameters(iniFileNameAndPath.c_str()) & ipa_CameraSensors::RET_FAILED)
	{
		std::cerr << "ERROR - PeopleDetector::Init:" << std::endl;
		std::cerr << "\t ... Error while loading configuration file '" << std::endl;
		std::cerr << "\t ... " << iniFileNameAndPath << "'.\n";
		return ipa_Utils::RET_FAILED;	
	}

	m_runPCA = false;

	return ipa_Utils::RET_OK;
}

unsigned long PeopleDetectorControlFlow::DetectFaces(ipa_SensorFusion::ColoredPointCloudPtr pc)
{
	cv::Mat xyzImage_8U3;
	ipa_Utils::ConvertToShowImage(pc->GetXYZImage(), xyzImage_8U3, 3);

	std::set<size_t> colorToRangeDependency;
	if (m_PeopleDetector->DetectFaces(pc->GetColorImage(), xyzImage_8U3, m_colorFaces, m_rangeFaces, colorToRangeDependency, false/*(m_RangeImagingCameraType==ipa_CameraSensors::CAM_KINECT)*/) & ipa_Utils::RET_FAILED)
	{
		std::cerr << "ERROR - PeopleDetectorControlFlow::DetectFaces" << std::endl;
		std::cerr << "\t ... Could not detect faces.\n";
		return ipa_Utils::RET_FAILED;
	}

	return ipa_Utils::RET_OK;
}

unsigned long PeopleDetectorControlFlow::AddFace(cv::Mat& image, std::string id)
{
	// addFace should only be called if there is exactly one face found --> so we access it with m_colorFaces[0]
	if (m_PeopleDetector->AddFace(image, m_colorFaces[0], id, m_faceImages, m_id) & ipa_Utils::RET_FAILED)
	{	
		std::cerr << "ERROR - PeopleDetectorControlFlow::AddFace:" << std::endl;
		std::cerr << "\t ... Could not save face.\n";
		return ipa_Utils::RET_FAILED;
	}
	m_runPCA = true;
	
	return ipa_Utils::RET_OK;
}

unsigned long PeopleDetectorControlFlow::PCA()
{
	// only run PCA if new data has arrived after last computation
	if(!m_runPCA)
	{
		std::cout << "INFO - PeopleDetectorControlFlow::PCA:" << std::endl;
		std::cout << "\t ... PCA algorithm skipped.\n";
		return ipa_Utils::RET_OK;
	}

	if(m_faceImages.size() < 2)
	{
		std::cout << "WARNING - PeopleDetector::ConsoleGUI:" << std::endl;
		std::cout << "\t ... Less than two images are trained.\n";
		return ipa_Utils::RET_OK;
	}

	// Delete memory
	m_eigenVectors.clear();

	// Do PCA
	if (m_PeopleDetector->PCA(&m_nEigens, m_eigenVectors, m_eigenValMat, m_avgImage, m_faceImages, m_projectedTrainFaceMat) & ipa_Utils::RET_FAILED)
	{	
		std::cerr << "ERROR - PeopleDetectorControlFlow::PCA:" << std::endl;
		std::cerr << "\t ... Error while PCA.\n";
		return ipa_Utils::RET_FAILED;
	}

	// Calculate FaceClasses
	if (m_PeopleDetector->CalculateFaceClasses(m_projectedTrainFaceMat, m_id, &m_nEigens, m_faceClassAvgProjections, m_idUnique) & ipa_Utils::RET_FAILED)
	{
		std::cerr << "ERROR - PeopleDetectorControlFlow::PCA:" << std::endl;
		std::cerr << "\t ... Error while calculating FaceClasses.\n";
		return ipa_Utils::RET_FAILED;
	}

	m_runPCA = false;

	return ipa_Utils::RET_OK;
}

unsigned long PeopleDetectorControlFlow::RecognizeFace(ipa_SensorFusion::ColoredPointCloudPtr pc, std::vector<int>& index)
{
	if (m_PeopleDetector->RecognizeFace(pc->GetColorImage(), m_colorFaces, &m_nEigens, m_eigenVectors, m_avgImage, m_faceClassAvgProjections, index, &m_threshold, &m_threshold_FS, m_eigenValMat) & ipa_Utils::RET_FAILED)
	{
		std::cerr << "ERROR - PeopleDetectorControlFlow::RecognizeFace:" << std::endl;
		std::cerr << "\t ... Error while recognizing faces.\n";
		return ipa_Utils::RET_FAILED;
	}

	return ipa_Utils::RET_OK;
}

unsigned long PeopleDetectorControlFlow::SaveTrainingData()
{
	std::string path = "ConfigurationFiles/TrainingData/";
	std::string filename = "data.xml";
	std::string img_ext = ".bmp";

	std::ostringstream complete;
	complete << path << filename;

//	try
//	{
//		fs::remove_all(path.c_str());
//		fs::create_directory(path.c_str());
//	}
//	catch (const std::exception &ex)
//	{
//		std::cerr << "ERROR - PeopleDetectorControlFlow::SaveTrainingData():" << std::endl;
//		std::cerr << "\t ... Exception catch of '" << ex.what() << "'" << std::endl;
//	}
	
	cv::FileStorage fileStorage(complete.str().c_str(), cv::FileStorage::WRITE);
	if(!fileStorage.isOpened())
	{
		std::cout << "WARNING - PeopleDetectorControlFlow::SaveTrainingData:" << std::endl;
		std::cout << "\t ... Can't save training data.\n";
		return ipa_Utils::RET_OK;
	}

	// Ids
	fileStorage << "id_num" << (int)m_id.size();
	for(int i=0; i<(int)m_id.size(); i++)
	{
		std::ostringstream tag;
		tag << "id_" << i;
		fileStorage << tag.str().c_str() << m_id[i].c_str();
	}

	// Face images
	fileStorage << "faces_num" << (int)m_faceImages.size();
	for(int i=0; i<(int)m_faceImages.size(); i++)
	{
		std::ostringstream img;
		img << path << i << img_ext;
		std::ostringstream tag;
		tag << "img_" << i;
		fileStorage << tag.str().c_str() << img.str().c_str();
		//cvSaveImage(img.str().c_str(), &m_faceImages[i]);
		cv::imwrite(img.str().c_str(), m_faceImages[i]);
	}

	// Number eigenvalues/eigenvectors
	fileStorage << "eigens_num" << m_nEigens;

	// Eigenvectors
	for (int i=0; i<m_nEigens; i++)
	{
		std::ostringstream tag;
		tag << "ev_" << i;
		fileStorage << tag.str().c_str() << m_eigenVectors[i];
	}

	// Eigenvalue matrix
	fileStorage << "eigen_val_mat" << m_eigenValMat;

	// Average image
	fileStorage << "avg_image" << m_avgImage;

	// Projections of the training faces
	fileStorage << "projected_train_face_mat" << m_projectedTrainFaceMat;

	// Unique Ids (m_idUnique[i] stores the corresponding id to the average face coordinates in the face subspace in m_faceClassAvgProjections.row(i))
	fileStorage << "id_unique_num" << (int)m_idUnique.size();
	for(int i=0; i<(int)m_idUnique.size(); i++)
	{
		std::ostringstream tag;
		tag << "id_unique_" << i;
		fileStorage << tag.str().c_str() << m_idUnique[i].c_str();
	}

	// The average factors of the eigenvector decomposition from each face class
	fileStorage << "face_class_avg_projections" << m_faceClassAvgProjections;

	fileStorage.release();

	std::cout << "INFO - PeopleDetectorControlFlow::SaveTrainingData:" << std::endl;
	std::cout << "\t ... " << m_faceImages.size() << " images saved.\n";
	return ipa_Utils::RET_OK;
}

unsigned long PeopleDetectorControlFlow::LoadTrainingData()
{
	std::string path = "ConfigurationFiles/TrainingData/";
	std::string filename = "data.xml";
	
	std::ostringstream complete;
	complete << path << filename;
	
	if(fs::is_directory(path.c_str()))
	{
		cv::FileStorage fileStorage(complete.str().c_str(), cv::FileStorage::READ);
		if(!fileStorage.isOpened())
		{
			std::cout << "WARNING - PeopleDetectorControlFlow::LoadTrainingData:" << std::endl;
			std::cout << "\t ... Cant open " << complete.str() << ".\n";
			return ipa_Utils::RET_OK;
		}

		// Ids
		m_id.clear();
		int id_num = (int)fileStorage["id_num"];
		for(int i=0; i<id_num; i++)
		{
			std::ostringstream tag;
			tag << "id_" << i;
			m_id.push_back((std::string)fileStorage[tag.str().c_str()]);
		}

		// Images
		m_faceImages.clear();
		int faces_num = (int)fileStorage["faces_num"];
		for(int i=0; i<faces_num; i++)
		{
			std::ostringstream tag;
			tag << "img_" << i;
			std::string path = (std::string)fileStorage[tag.str().c_str()];
			//IplImage *tmp = cvLoadImage(path.c_str(),0);
			cv::Mat temp = cv::imread(path.c_str(),0);
			m_faceImages.push_back(temp);
		}

		// Number eigenvalues/eigenvectors
		m_nEigens = (int)fileStorage["eigens_num"];

		// Eigenvectors
		m_eigenVectors.clear();
		m_eigenVectors.resize(m_nEigens, cv::Mat());
		for (int i=0; i<m_nEigens; i++)
		{
			std::ostringstream tag;
			tag << "ev_" << i;
			fileStorage[tag.str().c_str()] >> m_eigenVectors[i];
		}

		// Eigenvalue matrix
		m_eigenValMat = cv::Mat();
		fileStorage["eigen_val_mat"] >> m_eigenValMat;

		// Average image
		m_avgImage = cv::Mat();
		fileStorage["avg_image"] >> m_avgImage;

		// Projections of the training faces
		m_projectedTrainFaceMat = cv::Mat();
		fileStorage["projected_train_face_mat"] >> m_projectedTrainFaceMat;

		// Unique Ids (m_idUnique[i] stores the corresponding id to the average face coordinates in the face subspace in m_faceClassAvgProjections.row(i))
		m_idUnique.clear();
		int idUniqueNum = (int)fileStorage["id_unique_num"];
		for(int i=0; i<idUniqueNum; i++)
		{
			std::ostringstream tag;
			tag << "id_unique_" << i;
			m_idUnique.push_back((std::string)fileStorage[tag.str().c_str()]);
		}

		// The average factors of the eigenvector decomposition from each face class
		m_faceClassAvgProjections = cv::Mat();
		fileStorage["face_class_avg_projections"] >> m_faceClassAvgProjections;

		fileStorage.release();

		// Run PCA
//		m_runPCA = true;
//		PCA();

		std::cout << "INFO - PeopleDetectorControlFlow::LoadTrainingData:" << std::endl;
		std::cout << "\t ... " << faces_num << " images loaded.\n";
		
		//cvReleaseFileStorage(&fileStorage);
	}
	else
	{
		std::cerr << "ERROR - PeopleDetectorControlFlow::LoadTrainingData():" << std::endl;
		std::cerr << "\t .... Path '" << path << "' is not a directory." << std::endl;
		return ipa_CameraSensors::RET_FAILED;
	}

	return ipa_Utils::RET_OK;
}


unsigned long PeopleDetectorControlFlow::GetEigenface(cv::Mat& eigenface, int index)
{
	//eigenface.create(100, 100, CV_8UC1);
	cv::normalize(m_eigenVectors[index], eigenface, 0, 255, cv::NORM_MINMAX, CV_8UC1);

	
	// Get the Eigenface Raw data
// 	float *Image;
// 	cvGetImageRawData(m_eigenVectArr[index], (uchar**)&Image);
// 
// 	// Create a new 8 bit single channel image and get the raw data
// 	*eigenface = cvCreateImage( cvSize( 100, 100), IPL_DEPTH_8U, 1);
// 	uchar *fImg;
// 	cvGetImageRawData( *eigenface, (uchar**)&fImg);
// 
// 	// Find the Maximum and Minimum of the pixel values of the eigenfaces
// 	float max, min;
// 	max = min = 0.0;
// 	for(int i=0; i< (*eigenface)->width * (*eigenface)->height; i++)
// 	{
// 		if(max < Image[i])
// 			max = Image[i];
// 		if(min > Image[i])
// 			min = Image[i];
// 	}
// 
// 	/// Normalize the eigenface values between 0 and 255
// 	for(int i = 0; i< (*eigenface)->width * (*eigenface)->height; i++) 
// 	{
// 		fImg[i] = (uchar)(( 255 * (( Image[i] - min)/ (max- min)) ));
// 	}

	return ipa_Utils::RET_OK;
}

unsigned long PeopleDetectorControlFlow::ShowAVGImage(cv::Mat& avgImage)
{
	if(!m_runPCA && m_faceImages.size()<2)
	{
		std::cerr << "PeopleDetector::showAvgImage()" << std::endl;
		std::cerr << "No AVG image calculated" << std::endl;
		return 0;
	}
	
	cv::normalize(m_avgImage, avgImage, 0, 255, cv::NORM_MINMAX, CV_8UC1);

	
	// Get the AVG Raw data
// 	float *Image;
// 	cvGetImageRawData(m_avgImage, (uchar**)&Image);
// 
// 	// Get the raw data
// 	uchar *fImg;
// 	cvGetImageRawData( avgImage, (uchar**)&fImg);
// 
// 	// Find the Maximum and Minimum of the pixel values of the avg image
// 	float max, min;
// 	max = min = 0.0;
// 	for(int i=0; i< avgImage->width * avgImage->height; i++)
// 	{
// 		if(max < Image[i])
// 		{
// 			max = Image[i];
// 		}
// 		if(min > Image[i])
// 		{
// 			min = Image[i];
// 		}
// 	}
// 
// 	// Normalize the avg values between 0 and 255
// 	for(int i = 0; i< avgImage->width * avgImage->height; i++) 
// 	{
// 		fImg[i] = (uchar)(( 255 * (( Image[i] - min)/ (max- min)) ));
// 	}

	return ipa_Utils::RET_OK;
}

unsigned long PeopleDetectorControlFlow::SaveRangeTrainImages(ipa_SensorFusion::ColoredPointCloudPtr pc)
{
	std::string path = "ConfigurationFiles/haartraining/";
	std::string img_ext = ".jpg";
	cv::Mat xyzImage_8U3(pc->GetXYZImage().size(), CV_8UC3);	//IplImage* xyzImage_8U3 = cvCreateImage(cvGetSize(pc->GetXYZImage()), 8, 3);
	ipa_Utils::ConvertToShowImage(pc->GetXYZImage(), xyzImage_8U3, 3);

	for(int i=0; i<(int)m_colorFaces.size(); i++)
	{
		cv::Mat xyzImage_8U3_resized(100, 100, CV_8UC3);    //cvCreateImage(cvSize(100,100), 8, 3); 8=IPL_DEPTH_8U

		double scale = 1.6;
		cv::Rect rangeFace;
		rangeFace.height = (int)(m_colorFaces[i].height*scale);
		rangeFace.width = (int)(m_colorFaces[i].width*scale);
		rangeFace.x = m_colorFaces[i].x - ((rangeFace.width - m_colorFaces[i].width)/2);
		rangeFace.y = m_colorFaces[i].y - ((rangeFace.height - m_colorFaces[i].height)/2)-10;

		cv::Mat xyzImage_8U3_roi = xyzImage_8U3(rangeFace);
		//cvSetImageROI(xyzImage_8U3, rangeFace);
		cv::resize(xyzImage_8U3_roi, xyzImage_8U3_resized, xyzImage_8U3_resized.size());
		
		std::ostringstream file;
		file << path << m_filname << img_ext;
		
		cv::imwrite(file.str().c_str(), xyzImage_8U3_resized);//cvSaveImage(file.str().c_str(), xyzImage_8U3_resized);
		m_filname++;
	}

	return ipa_Utils::RET_OK;
}

unsigned long PeopleDetectorControlFlow::LoadParameters(const char* iniFileName)
{
	/// Load parameters from file
	TiXmlDocument* p_configXmlDocument = new TiXmlDocument( iniFileName );
	if (!p_configXmlDocument->LoadFile())
	{
		std::cerr << "ERROR - PeopleDetector::LoadParameters:" << std::endl;
		std::cerr << "\t ... Error while loading xml configuration file" << std::endl;
		std::cerr << "\t ... (Check filename and syntax of the file):" << std::endl;
		std::cerr << "\t ... '" << iniFileName << "'" << std::endl;
		return ipa_Utils::RET_FAILED;
	}
	std::cout << "INFO - PeopleDetector::LoadParameters:" << std::endl;
	std::cout << "\t ... Parsing xml configuration file:" << std::endl;
	std::cout << "\t ... '" << iniFileName << "'" << std::endl;

	if ( p_configXmlDocument )
	{

//************************************************************************************
//	BEGIN PeopleDetector
//************************************************************************************
		// Tag element "PeopleDetector" of Xml Inifile
		TiXmlElement *p_xmlElement_Root = NULL;
		p_xmlElement_Root = p_configXmlDocument->FirstChildElement( "PeopleDetector" );

		if ( p_xmlElement_Root )
		{

//************************************************************************************
//	BEGIN PeopleDetector->adaBoost
//************************************************************************************
			// Tag element "adaBoost" of Xml Inifile
			TiXmlElement *p_xmlElement_Root_OD = NULL;
			p_xmlElement_Root_OD = p_xmlElement_Root->FirstChildElement( "adaBoost" );

			if ( p_xmlElement_Root_OD )
			{

//************************************************************************************
//	BEGIN PeopleDetector->adaBoost->Faces_increase_search_scale
//************************************************************************************
				// Subtag element "adaBoost" of Xml Inifile
				TiXmlElement *p_xmlElement_Child = NULL;
				p_xmlElement_Child = p_xmlElement_Root_OD->FirstChildElement( "Faces_increase_search_scale" );

				if ( p_xmlElement_Child )
				{
					// read and save value of attribute
					if ( p_xmlElement_Child->QueryValueAttribute( "value", &m_PeopleDetector->m_faces_increase_search_scale) != TIXML_SUCCESS)
					{
						std::cerr << "ERROR - PeopleDetector::LoadParameters:" << std::endl;
						std::cerr << "\t ... Can't find attribute 'value' of tag 'Faces_increase_search_scale'" << std::endl;
						return ipa_Utils::RET_FAILED;
					}
				}
				else
				{
					std::cerr << "ERROR - PeopleDetector::LoadParameters:" << std::endl;
					std::cerr << "\t ... Can't find tag 'Faces_increase_search_scale'" << std::endl;
					return ipa_Utils::RET_FAILED;
				}

//************************************************************************************
//	BEGIN PeopleDetector->adaBoost->Faces_drop_groups
//************************************************************************************
				// Subtag element "adaBoost" of Xml Inifile
				p_xmlElement_Child = NULL;
				p_xmlElement_Child = p_xmlElement_Root_OD->FirstChildElement( "Faces_drop_groups" );

				if ( p_xmlElement_Child )
				{
					// read and save value of attribute
					if ( p_xmlElement_Child->QueryValueAttribute( "value", &m_PeopleDetector->m_faces_drop_groups) != TIXML_SUCCESS)
					{
						std::cerr << "ERROR - PeopleDetector::LoadParameters:" << std::endl;
						std::cerr << "\t ... Can't find attribute 'value' of tag 'Faces_drop_groups'" << std::endl;
						return ipa_Utils::RET_FAILED;
					}
				}
				else
				{
					std::cerr << "ERROR - PeopleDetector::LoadParameters:" << std::endl;
					std::cerr << "\t ... Can't find tag 'Faces_drop_groups'" << std::endl;
					return ipa_Utils::RET_FAILED;
				}

//************************************************************************************
//	BEGIN PeopleDetector->adaBoost->Faces_min_search_scale_x
//************************************************************************************
				// Subtag element "adaBoost" of Xml Inifile
				p_xmlElement_Child = NULL;
				p_xmlElement_Child = p_xmlElement_Root_OD->FirstChildElement( "Faces_min_search_scale_x" );

				if ( p_xmlElement_Child )
				{
					// read and save value of attribute
					if ( p_xmlElement_Child->QueryValueAttribute( "value", &m_PeopleDetector->m_faces_min_search_scale_x) != TIXML_SUCCESS)
					{
						std::cerr << "ERROR - PeopleDetector::LoadParameters:" << std::endl;
						std::cerr << "\t ... Can't find attribute 'value' of tag 'Faces_min_search_scale_x'" << std::endl;
						return ipa_Utils::RET_FAILED;
					}
				}
				else
				{
					std::cerr << "ERROR - PeopleDetector::LoadParameters:" << std::endl;
					std::cerr << "\t ... Can't find tag 'Faces_min_search_scale_x'" << std::endl;
					return ipa_Utils::RET_FAILED;
				}

//************************************************************************************
//	BEGIN PeopleDetector->adaBoost->Faces_min_search_scale_y
//************************************************************************************
				// Subtag element "adaBoost" of Xml Inifile
				p_xmlElement_Child = NULL;
				p_xmlElement_Child = p_xmlElement_Root_OD->FirstChildElement( "Faces_min_search_scale_y" );

				if ( p_xmlElement_Child )
				{
					// read and save value of attribute
					if ( p_xmlElement_Child->QueryValueAttribute( "value", &m_PeopleDetector->m_faces_min_search_scale_y) != TIXML_SUCCESS)
					{
						std::cerr << "ERROR - PeopleDetector::LoadParameters:" << std::endl;
						std::cerr << "\t ... Can't find attribute 'value' of tag 'Faces_min_search_scale_y'" << std::endl;
						return ipa_Utils::RET_FAILED;
					}
				}
				else
				{
					std::cerr << "ERROR - PeopleDetector::LoadParameters:" << std::endl;
					std::cerr << "\t ... Can't find tag 'Faces_min_search_scale_y'" << std::endl;
					return ipa_Utils::RET_FAILED;
				}

//************************************************************************************
//	BEGIN PeopleDetector->adaBoost->Range_increase_search_scale
//************************************************************************************
				// Subtag element "adaBoost" of Xml Inifile
				p_xmlElement_Child = NULL;
				p_xmlElement_Child = p_xmlElement_Root_OD->FirstChildElement( "Range_increase_search_scale" );

				if ( p_xmlElement_Child )
				{
					// read and save value of attribute
					if ( p_xmlElement_Child->QueryValueAttribute( "value", &m_PeopleDetector->m_range_increase_search_scale) != TIXML_SUCCESS)
					{
						std::cerr << "ERROR - PeopleDetector::LoadParameters:" << std::endl;
						std::cerr << "\t ... Can't find attribute 'value' of tag 'Range_increase_search_scale'" << std::endl;
						return ipa_Utils::RET_FAILED;
					}
				}
				else
				{
					std::cerr << "ERROR - PeopleDetector::LoadParameters:" << std::endl;
					std::cerr << "\t ... Can't find tag 'Range_increase_search_scale'" << std::endl;
					return ipa_Utils::RET_FAILED;
				}

//************************************************************************************
//	BEGIN PeopleDetector->adaBoost->Range_drop_groups
//************************************************************************************
				// Subtag element "adaBoost" of Xml Inifile
				p_xmlElement_Child = NULL;
				p_xmlElement_Child = p_xmlElement_Root_OD->FirstChildElement( "Range_drop_groups" );

				if ( p_xmlElement_Child )
				{
					// read and save value of attribute
					if ( p_xmlElement_Child->QueryValueAttribute( "value", &m_PeopleDetector->m_range_drop_groups) != TIXML_SUCCESS)
					{
						std::cerr << "ERROR - PeopleDetector::LoadParameters:" << std::endl;
						std::cerr << "\t ... Can't find attribute 'value' of tag 'Range_drop_groups'" << std::endl;
						return ipa_Utils::RET_FAILED;
					}
				}
				else
				{
					std::cerr << "ERROR - PeopleDetector::LoadParameters:" << std::endl;
					std::cerr << "\t ... Can't find tag 'Range_drop_groups'" << std::endl;
					return ipa_Utils::RET_FAILED;
				}

//************************************************************************************
//	BEGIN PeopleDetector->adaBoost->Range_min_search_scale_x
//************************************************************************************
				// Subtag element "adaBoost" of Xml Inifile
				p_xmlElement_Child = NULL;
				p_xmlElement_Child = p_xmlElement_Root_OD->FirstChildElement( "Range_min_search_scale_x" );

				if ( p_xmlElement_Child )
				{
					// read and save value of attribute
					if ( p_xmlElement_Child->QueryValueAttribute( "value", &m_PeopleDetector->m_range_min_search_scale_x) != TIXML_SUCCESS)
					{
						std::cerr << "ERROR - PeopleDetector::LoadParameters:" << std::endl;
						std::cerr << "\t ... Can't find attribute 'value' of tag 'Range_min_search_scale_x'" << std::endl;
						return ipa_Utils::RET_FAILED;
					}
				}
				else
				{
					std::cerr << "ERROR - PeopleDetector::LoadParameters:" << std::endl;
					std::cerr << "\t ... Can't find tag 'Range_min_search_scale_x'" << std::endl;
					return ipa_Utils::RET_FAILED;
				}

//************************************************************************************
//	BEGIN PeopleDetector->adaBoost->Range_min_search_scale_y
//************************************************************************************
				// Subtag element "adaBoost" of Xml Inifile
				p_xmlElement_Child = NULL;
				p_xmlElement_Child = p_xmlElement_Root_OD->FirstChildElement( "Range_min_search_scale_y" );

				if ( p_xmlElement_Child )
				{
					// read and save value of attribute
					if ( p_xmlElement_Child->QueryValueAttribute( "value", &m_PeopleDetector->m_range_min_search_scale_y) != TIXML_SUCCESS)
					{
						std::cerr << "ERROR - PeopleDetector::LoadParameters:" << std::endl;
						std::cerr << "\t ... Can't find attribute 'value' of tag 'Range_min_search_scale_y'" << std::endl;
						return ipa_Utils::RET_FAILED;
					}
				}
				else
				{
					std::cerr << "ERROR - PeopleDetector::LoadParameters:" << std::endl;
					std::cerr << "\t ... Can't find tag 'Range_min_search_scale_y'" << std::endl;
					return ipa_Utils::RET_FAILED;
				}
			}
//************************************************************************************
//	END CameraDataViewerControlFlow->adaBoost
//************************************************************************************

//************************************************************************************
//	BEGIN PeopleDetector->eigenfaces
//************************************************************************************
			// Tag element "adaBoost" of Xml Inifile
			p_xmlElement_Root_OD = NULL;
			p_xmlElement_Root_OD = p_xmlElement_Root->FirstChildElement( "eigenfaces" );

			if ( p_xmlElement_Root_OD )
			{

//************************************************************************************
//	BEGIN PeopleDetector->eigenfaces->Threshold_Face_Class
//************************************************************************************
				// Subtag element "adaBoost" of Xml Inifile
				TiXmlElement *p_xmlElement_Child = NULL;
				p_xmlElement_Child = p_xmlElement_Root_OD->FirstChildElement( "Threshold_Face_Class" );

				if ( p_xmlElement_Child )
				{
					// read and save value of attribute
					if ( p_xmlElement_Child->QueryValueAttribute( "value", &m_threshold) != TIXML_SUCCESS)
					{
						std::cerr << "ERROR - PeopleDetector::LoadParameters:" << std::endl;
						std::cerr << "\t ... Can't find attribute 'value' of tag 'Threshold_Face_Class'" << std::endl;
						return ipa_Utils::RET_FAILED;
					}
				}
				else
				{
					std::cerr << "ERROR - PeopleDetector::LoadParameters:" << std::endl;
					std::cerr << "\t ... Can't find tag 'Threshold_Face_Class'" << std::endl;
					return ipa_Utils::RET_FAILED;
				}
//************************************************************************************
//	BEGIN PeopleDetector->eigenfaces->Threshold_Facespace
//************************************************************************************
				// Subtag element "adaBoost" of Xml Inifile
				p_xmlElement_Child = NULL;
				p_xmlElement_Child = p_xmlElement_Root_OD->FirstChildElement( "Threshold_Facespace" );

				if ( p_xmlElement_Child )
				{
					// read and save value of attribute
					if ( p_xmlElement_Child->QueryValueAttribute( "value", &m_threshold_FS) != TIXML_SUCCESS)
					{
						std::cerr << "ERROR - PeopleDetector::LoadParameters:" << std::endl;
						std::cerr << "\t ... Can't find attribute 'value' of tag 'Threshold_Facespace'" << std::endl;
						return ipa_Utils::RET_FAILED;
					}
				}
				else
				{
					std::cerr << "ERROR - PeopleDetector::LoadParameters:" << std::endl;
					std::cerr << "\t ... Can't find tag 'Threshold_Facespace'" << std::endl;
					return ipa_Utils::RET_FAILED;
				}
//************************************************************************************
//	END CameraDataViewerControlFlow->eigenfaces
//************************************************************************************

			}
			else
			{
				std::cerr << "ERROR - CameraDataViewerControlFlow::LoadParameters:" << std::endl;
				std::cerr << "\t ... Can't find tag 'ObjectDetectorParameters'" << std::endl;
				return ipa_Utils::RET_FAILED;
			}

		}


//************************************************************************************
//	END ObjectDetector
//************************************************************************************
		else
		{
			std::cerr << "ERROR - CameraDataViewerControlFlow::LoadParameters:" << std::endl;
			std::cerr << "\t ... Can't find tag 'ObjectDetector'" << std::endl;
			return ipa_Utils::RET_FAILED;
		}
	}
	return ipa_Utils::RET_OK;
}