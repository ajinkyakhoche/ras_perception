/*
#begin: description
==========================================================================

	Description
	-----------
		This program
		1.	acquires frames from 1 Camera source/ Video source
		2.	processes them to detect cone
		3.	stores result (location of cones in form of bounding box and
			videos as well if camera is used for acquisition)
		4.	displays the video with cone highlighted.


==========================================================================

	History
	-------

		Version No.			Date			Author
		------------------------------------------

		3.x					2017/11/xx		Ajinkya
		4.x					2017/11/07		Ajinkya
		5.1					2018/02/03		Ajinkya
		5.2					2018/03/20		Ajinkya


	Revision Description
	--------------------
		3.x		---	STEPS
					--	Convert to HSV
					--	Threshold image in yellow range
					--	Detect surf features in entire image and compare it to ideal cone
					--	After comparison, cluster resulting features and to find region with
						maximum concentration
					--	Draw bounding box around region of max conc.
				---	COMMENTS
					--	Computational time high
					--	Can detect only one cone
					--	Not that reliable

		4.1		---	STEPS
					--	Convert to HSV
					--	Threshold image in yellow range
					--	Perform morphological Closing and Opening in order
					--	Detect canny contours
					--	Draw bounding box around canny contours
				---	COMMENTS
					--	Good, detects multiple cones
					--	False positives are present, although few
					--	Bounding box sizes are unrealiable and changing rapidly,
						suggesting that algorithm needs	to be more robust.

		4.4		---	STEPS
		and 4.5		--	Convert to HSV
					--	Threshold image in yellow range
					--	Find 10 largest contours according to area and find their bounding boxes (bBox1)
					--	Inflate the bounding boxes using a 'scaling factor' and try to find connected bounding boxes
						--	Assign same label to boxes which are intersecting. for eg if A & C intersect B but not
							each other, all three still have same label.
					--	Find bounding boxes encompassing all intersecting rectangles (bBox2)
					--	Cut ROI image from bBox2 and send it to detectCone function. The function
						returns bounding box of cone (if it exists) otherwise an empty rectangle.
						(NEED TO UPDATE FOR CASE WHEN detectCone FINDS MULTIPLE CONES IN AN ROI IMAGE
					--	Draw resulting bounding box on image and display/ store location of cone bounding box.

					detectCone function Steps:
					--	Convert to HSV
					--	Threshold image in yellow range
					--	Threshold image in black

		Discussion
		- When comparing Cone7 and Cone12.jpg, we see that in cone7, algo detects shadow as well
		if we put black value of 50 while HSV thresholding. In other words, the black part of cone
		in cone12 is same colour as shadow part of cone in cone7.But we do not need shadow. Thus we
		need to remove shadow from thresholded black image.


		5.1		=====> continued from version 4_5.2
				---	COMMENTS
					--	Wrote a cleaner code according to: http://www.edparrish.net/common/cppdoc.html#blockcomment
					--	Need to increase robustness. Given a video/live stream, program should be able to detect
						three types of cones:
						1. Yellow cones with black strips
						2. Blue cones with white strips
						3. Orange Cones with white strips
				---	CHANGES
					--	Deleted createTrackBars() and on_trackbar() function
					--	Removed unnecessary variables
					--	wrote a function to load color parameters
					--	Renamed detectCone to detectCone2
					--	Instead of selecting top 10 contours according to area, we :
						--	first cut out top 25% of image (to remove effects of sky, trees etc.)
						--	restrict maximum area of Rectangle to 3% of total image area
						After doing these 2 steps, top 10 contours are selected.

		5.2		---	CHANGES
					--	In detectCone1, before finding 10 largest contours in terms of area,
						first remove all contours whose area is > ~3-5% of frame size (they
						can't be cones)
					--	detectCone1 should return a std::vector of rectangles instead of an image.
						This is bc rectangles for orange, blue and yellow cones need to be combined
		5.3		---	CHANGES
					--	Define a class which stores cone parameters (color, depth, angle). this would be
						published in a local map on ros
					--	added code for zed camera (depth sensing)
*/

#include <sstream>
#include <string>
#include <iostream>
#include <opencv/highgui.h>
#include <opencv/cv.h>
#include <sl/Camera.hpp>
#include <SaveDepth.hpp>
#include <vector>
#include <ros/ros.h>
#include <image_transport/image_transport.h>
#include <opencv2/highgui/highgui.hpp>
#include <cv_bridge/cv_bridge.h>

using namespace cv;
using namespace sl;

//Pre-processor directives
#define DEBUG 0						//1 = display images at every step; 0 = don't
#define STREAMSOURCE 2				//0 = a single image; 1 = video feed; 2 = live feed
#define VERBOSE 2					//0 = show only discovered cone; 1 = show bBox2 and discovered cone; 2 = show bBox2, bBox2 and discovered cone

//Structure for color parameters in HSV range
struct Color
{
	int H_MIN;
	int H_MAX;
	int S_MIN;
	int S_MAX;
	int V_MIN;
	int V_MAX;
};

//default capture width and height
//const int FRAME_WIDTH = 640;
//const int FRAME_HEIGHT = 480;
//max number of objects to be detected in frame
const int MAX_NUM_OBJECTS=50;
//minimum and maximum object area
const int MIN_OBJECT_AREA = 20*20;
//const int MAX_OBJECT_AREA = FRAME_HEIGHT*FRAME_WIDTH/1.5;
// % of image to be cut from top
const float PER = 25;
const float MAX_CONE_PER = 7;
// For Morphology
cv::Mat morphImage;
int morph_operator = 1;		//CLOSING
int morph_elem = 2;			//ELLIPSE
int morph_size = 21	;		//SIZE of Strel

cv::Mat morphImage1;
int morph_operator1 = 0;		//OPENING
int morph_elem1 = 2;			//ELLIPSE
int morph_size1 = 3	;		//SIZE of Strel

//CLASS
class coneInfo
{
	enum coneColor
	{
		yellow,
		blue,
		orangeSmall,
		orangeBig
	};

	std::vector<double> angle;
	std::vector<double> depth;
};

//	FUNCTIONS
std::pair<std::vector<Rect>, cv::Mat> detectCone1(cv::Mat img, int col1, int col2, cv::Mat temp);
Rect detectCone2(cv::Mat img, int col1, int col2);
Color loadColorParameters(int col);
cv::Mat drawCones(std::vector<Rect> coneVect, cv::Mat img);
cv::Mat slMat2cvMat(sl::Mat& input);

std::string intToString(int number){


	std::stringstream ss;
	ss << number;
	return ss.str();
}


int main(int argc, char* argv[])
{

	ros::init(argc, argv, "image_publisher");
  	ros::NodeHandle nh;
  	image_transport::ImageTransport it(nh);
  	image_transport::Publisher pub = it.advertise("ajinkya/image", 1);
	sensor_msgs::ImagePtr msg;
	ros::Rate loop_rate(30);

	//cv::Matrix to store each frame of the webcam feed
	cv::Mat cameraFeed;
	//cv::Matrix to clone cameraFeed
	cv::Mat temp;
	//x and y values for the location of the object
	int x=0, y=0;
	//video capture object to acquire webcam feed
	VideoCapture capture;
	//Rectangle for cutting out roi
	Rect ROI;
	int FRAME_WIDTH, FRAME_HEIGHT;
	//std::vector of Rectangles/ Bounding Boxes
	std::pair<std::vector<Rect>, cv::Mat> yellowCones, orangeCones, blueCones;

#if(STREAMSOURCE == 0)
	//If you just want to work with one image only, load image here
	cameraFeed = imread("Cone2.jpg", CV_LOAD_IMAGE_COLOR);
#elif(STREAMSOURCE == 1)
	//If you want Video FEED, open capture object here
	capture.open("Formula Student Spain 2015 Endurance- DHBW Engineering with the eSleek15.mp4");
	//capture.open("FSG 2017 - AMZ (ETH Zurich).mp4");
	//capture.open("Autonomous Racing- AMZ Driverless.mp4");
	//capture.open("../../../Fluela video.mp4");
	//capture.open("RWTH Formula Student Driverless Split View 2017.mp4");
#elif(STREAMSOURCE == 2)
	/***
		If you want LIVE FEED, you can:
			1. open capture object at location zero (default location for webcam)
			2. open stereo camera
		@TODO: write code for opening stereo camera
	*/
	//// Uncomment line below to read from webcam
	//capture.open(0);

	// Create a ZED camera object
    Camera zed;

	// Set configuration parameters
    InitParameters init_params;
    init_params.camera_resolution = RESOLUTION_HD1080;
    init_params.depth_mode = DEPTH_MODE_PERFORMANCE;
    init_params.coordinate_units = UNIT_METER;

    // Open the camera
    ERROR_CODE err = zed.open(init_params);
    if (err != SUCCESS) {
        printf("%s\n", toString(err).c_str());
        zed.close();
        return 1; // Quit if an error occurred
    }

    // Set runtime parameters after opening the camera
    RuntimeParameters runtime_parameters;
    runtime_parameters.sensing_mode = SENSING_MODE_STANDARD; // Use STANDARD sensing mode

	// Containers for Capturing images and depth
    //int i = 0;
    //sl::cv::Mat image, depth, point_cloud;

#else
	/***
		@TODO: Display error and exit program
	*/
#endif

	//If video/live feed, start an infinite loop OR loop till camera is open.
	//The webcam feed is copied to cameraFeed cv::Matrix.
	//All of our operations will be performed within this loop

#if(STREAMSOURCE == 1)
	//set height and width of capture frame
	//capture.set(CV_CAP_PROP_FRAME_WIDTH,FRAME_WIDTH);
	//capture.set(CV_CAP_PROP_FRAME_HEIGHT,FRAME_HEIGHT);
	FRAME_WIDTH = capture.get(CV_CAP_PROP_FRAME_WIDTH);
	FRAME_HEIGHT = capture.get(CV_CAP_PROP_FRAME_HEIGHT);

	while(1)
	{
		/***	If you want to work with video, or camera, store image to cv::Matrix	***/
		capture.read(cameraFeed);
#elif(STREAMSOURCE == 0)
	FRAME_WIDTH = cameraFeed.cols;
	FRAME_HEIGHT = cameraFeed.rows;
#elif(STREAMSOURCE == 2)
	//set height and width of capture frame
	//FRAME_WIDTH = capture.get(CV_CAP_PROP_FRAME_WIDTH);
	//FRAME_HEIGHT = capture.get(CV_CAP_PROP_FRAME_HEIGHT);

	// Prepare new image size to retrieve images
    Resolution image_size = zed.getResolution();
    FRAME_WIDTH  = image_size.width;
    FRAME_HEIGHT = image_size.height;

	int new_width = FRAME_WIDTH;
	int new_height = FRAME_HEIGHT;

	// To share data between sl::cv::Mat and cv::Mat, use slMat2cvMat()
    // Only the headers and pointer to the sl::cv::Mat are copied, not the data itself
    sl::Mat image_zed(new_width, new_height, Mat_TYPE_8U_C4);
    sl::Mat image_ocv = slMat2cvMat(image_zed);
    sl::Mat depth_image_zed(new_width, new_height, Mat_TYPE_8U_C4);
    sl::Mat depth_image_ocv = slMat2cvMat(depth_image_zed);
    sl::Mat point_cloud;

	while(ros::ok())
	{
		/***	If you want to work with video, or camera, store image to cv::Matrix	***/
		//capture.read(cameraFeed);
		if (zed.grab(runtime_parameters) == SUCCESS) {

			// Retrieve the left image, depth image in half-resolution
			zed.retrieveImage(image_zed, VIEW_LEFT);
			zed.retrieveImage(depth_image_zed, VIEW_DEPTH);

			// Retrieve the RGBA point cloud in half-resolution
			// To learn how to manipulate and display point clouds, see Depth Sensing sample
			zed.retrieveMeasure(point_cloud, MEASURE_XYZRGBA);

			// Display image and depth using cv:cv::Mat which share sl:cv::Mat data
			//cv::imshow("Image", image_ocv);
			//cv::imshow("Depth", depth_image_ocv);
			cameraFeed = image_ocv;
#endif
		if (!cameraFeed.empty())
		{
			//Cut off upper ___% of image
			ROI.x = 0;
			ROI.y = FRAME_HEIGHT * PER/100;
			ROI.width = FRAME_WIDTH;
			ROI.height = FRAME_HEIGHT * (1-PER/100);

			//Resize if required
			//cv::resize(cameraFeed,cameraFeed,Size(),2,2);

			//clone original image
			temp = cameraFeed(ROI);
			cv::Mat temp_clone = temp.clone();
			//Detect Yellow Cones
			yellowCones = detectCone1(temp, 0,3, temp_clone);
			temp_clone = yellowCones.second;
			//Detect Blue Cones
			blueCones = detectCone1(temp, 1,4, temp_clone);
			temp_clone = blueCones.second;
			//Detect Orange Cones
			orangeCones = detectCone1(temp, 2,4, temp_clone);

			//Resize if required
			//cv::resize(cameraFeed,cameraFeed,Size(),0.5,0.5);

			//show frames
			imshow("Resulting Image",temp_clone);
			//imshow("HSV Image",HSV);

#if(STREAMSOURCE ==0)
			//delay indefinitely so that image can be displayed
			waitKey(0);
#elif(STREAMSOURCE == 1 || STREAMSOURCE == 2)
			//delay 30ms so that screen can refresh.
			//image will not appear without this waitKey() command
			waitKey(10);
			imshow("Depth", depth_image_ocv);
			waitKey(10);
			msg = cv_bridge::CvImage(std_msgs::Header(), "bgr8", temp_clone).toImageMsg();
			pub.publish(msg);
		    ros::spinOnce();
    		loop_rate.sleep();
		}
#endif
	}

	// Close the camera
    zed.close();
	return 0;
}


std::pair<std::vector<Rect>, cv::Mat> detectCone1(cv::Mat temp, int col1, int col2, cv::Mat temp_clone)
{
	//clone original image
	//cv::Mat temp = cameraFeed.clone();
	//cv::Mat temp = cameraFeed(ROI);
	//cv::Mat temp_clone = temp.clone();
	//cv::Matrix storage for HSV image
	cv::Mat HSV;
	//cv::Matrix storage for binary threshold image
	cv::Mat threshold;
	//cv::Matrix to extract ROI
	cv::Mat roiImage;
	//Color parameters for cone color and cone strip color
	Color ConeColor, StripColor;
	// std::vector to store resulting rectangles
	std::vector<Rect> resultingRect;

	//convert frame from BGR to HSV colorspace
	cvtColor(temp,HSV,COLOR_BGR2HSV);

	ConeColor = loadColorParameters(col1);

	if (col1 == 2)
	{
		//For orange color, HSV threshold is a bit tricky because H_MIN > H_MAX
		//So orange image intersection of
		// hmin< H <256; smin< S< smax; vmin< V< vmax) AND 0< H < hmax; smin< S< smax; vmin < V< vmax)
		cv::Mat threshold1, threshold2;
		inRange(HSV,Scalar(ConeColor.H_MIN, ConeColor.S_MIN, ConeColor.V_MIN),Scalar(256, ConeColor.S_MAX, ConeColor.V_MAX),threshold1);
		inRange(HSV,Scalar(0, ConeColor.S_MIN, ConeColor.V_MIN),Scalar(ConeColor.H_MAX, ConeColor.S_MAX, ConeColor.V_MAX),threshold2);
		threshold = threshold1 + threshold2;
	}
	else
	{
		//filter HSV image between values and store filtered image to threshold cv::Matrix
		inRange(HSV,Scalar(ConeColor.H_MIN, ConeColor.S_MIN, ConeColor.V_MIN),Scalar(ConeColor.H_MAX, ConeColor.S_MAX, ConeColor.V_MAX),threshold);
	}

#if DEBUG
	//Show thresholded image
	imshow("Thresholded Image",threshold);
	waitKey(30);
#endif

	//find countours ...
	std::vector<std::vector<Point> > contours;
	std::vector<Vec4i> hierarchy;
	std::vector<Rect> bBox1, bBox2;
	int count_largestContours = 10;
	int scalingFactor = 2;
	//std::std::vector<int> mergeIndex;
	int k = 0;
	int ind1 = 0;
	int ind2 = 0;
	int* magic_ind;

	/// Find contours
	findContours( threshold, contours, hierarchy, CV_RETR_TREE, CV_CHAIN_APPROX_SIMPLE, Point(0, 0) );

	if (contours.size() != 0)
	{
		int contourSize = (contours.size() < count_largestContours? contours.size() : count_largestContours);
		//std::std::vector<int> mergeIndex(contourSize, 1);
		std::vector<int> indices(contours.size());
		std::vector<double> ctrArea(contours.size());
		std::vector<Rect> bBox1(contourSize);
		std::vector<Rect> bBox2(contourSize);
		std::std::vector<std::std::vector<int>> mergeIndex(contourSize, std::std::vector<int>(contourSize, 0));
		std::std::vector<int> label(contourSize, 0);

		/// find ten largest contours in terms of area
		for( size_t i = 0; i< contours.size(); i++ )
		{
			ctrArea[i] = contourArea(contours[i]);
			indices[i] = i;
		}

		std::sort(  std::begin(indices),
			std::end(indices),
			[&](int i1, int i2) { return ctrArea[i1] > ctrArea[i2]; } );

		//if contours.size() < count_largestContours, i.e. there are too few contours
		//(this case occurs for eg when scene has no orange cones), then index going
		//into while loop is 'ind1', otherwise its 'ind2'
		if (contours.size() <= count_largestContours)
		{
			magic_ind = &ind1;
		}
		else
		{
			magic_ind = &ind2;
		}

		while( *magic_ind < contourSize && ind1 < contours.size())
		{
			Rect box = boundingRect(cv::Mat(contours[indices[ind1]]));
			if (box.width * box.height < MAX_CONE_PER/100* temp.rows * temp.cols )
			{
				bBox1[ind2].x = box.x - box.width * (scalingFactor - 1) * 0.5;
				//NOTE: height only will be added upwards bc image might end downwards!
				bBox1[ind2].y = box.y - box.height * (scalingFactor - 1) * 1.5;
				bBox1[ind2].width = box.width * scalingFactor;
				bBox1[ind2].height = box.height * (1 + (scalingFactor - 1)*1.5);

				//Check if bBox[i] is crossing the limits of image and if yes adjust it
				//int adjust = 0;
				if (bBox1[ind2].tl().x < 0)
				{
					int adjust = 0 - bBox1[ind2].tl().x;			//find the adjusting factor
					bBox1[ind2].x = min(bBox1[ind2].x + adjust, temp.rows);
					bBox1[ind2].width = min(bBox1[ind2].width + adjust, temp.rows);
				}
				if (bBox1[ind2].tl().y < 0)
				{
					int adjust = 0 - bBox1[ind2].tl().y;			//find the adjusting factor
					bBox1[ind2].y = min(bBox1[ind2].y + adjust, temp.cols);
					bBox1[ind2].height = min(bBox1[ind2].height+ adjust, temp.cols);
				}
				if (bBox1[ind2].br().x > temp.cols)
				{
					int adjust = bBox1[ind2].br().x - temp.cols;			//find the adjusting factor
					bBox1[ind2].x = max(bBox1[ind2].x - adjust, 0);
					bBox1[ind2].width = max(bBox1[ind2].width - adjust, 0);
				}
				if (bBox1[ind2].br().y > temp.rows)
				{
					int adjust = bBox1[ind2].br().y - temp.rows;			//find the adjusting factor
					bBox1[ind2].y = max(bBox1[ind2].y - adjust, 0);
					bBox1[ind2].height = max(bBox1[ind2].height - adjust, 0);
				}

				//if area of bBox1 > 3-5% of image size, it can't be a cone candidate!
				//if (bBox1[ind2].width * bBox1[ind2].height < MAX_CONE_PER/100* temp.rows * temp.cols)
				//{
#if(VERBOSE == 2)
				rectangle( temp_clone, bBox1[ind2].tl(), bBox1[ind2].br(), Scalar(255,0,0), 2, 8, 0 );
#if(STREAMSOURCE == 0)
				imshow("Blue Rect", temp_clone);
				waitKey(30);
#endif
#endif
				ind2++;
				ind1++;
			}
			else
			{
				ind1++;
			}
		}

		for ( int i = 0; i < contourSize; i++)
		{
			if (label[i] == 0)
			{
				label[i] = ++k;

				for ( int j = 0; j < contourSize; j++)
				{
					if (i != j)
					{
						Rect box = bBox1[i] & bBox1[j];
						if (box.width != 0)
						{
							label[j] = label[i];
							//bBox2[k] = box;
							mergeIndex[i][j] = 1;
							mergeIndex[j][i] = 1;
						}
					}
				}
			}
			/*else
			{

			}*/
		}


		for ( int i = 1; i <= k; i++)
		{
			Rect box, cone;
			for (int j = 0; j < contourSize; j++)
			{
				if (label[j] == i )
				{
					if (box.width == 0)			//we've found first bBox1 with label i
					{
						box = bBox1[j];
					}
					else
					{
						box = box | bBox1[j];
					}
				}
			}
			bBox2[i-1] = box;

			if (bBox2[i-1].width != 0 && bBox2[i-1].height !=0)
			{
				//if (bBox2[i-1].height >= bBox2[i-1].width)
				if((bBox2[i-1].width * bBox2[i-1].height < MAX_CONE_PER/100* temp.rows * temp.cols) && bBox2[i-1].height / bBox2[i-1].width >= 0.5)
				{
#if(VERBOSE == 1 || VERBOSE == 2)
					rectangle( temp_clone, bBox2[i-1].tl(), bBox2[i-1].br(), Scalar(0,255,0), 2, 8, 0 );
#if(STREAMSOURCE == 0)
					imshow("Green Rect", temp_clone);
					waitKey(30);
#endif
#endif
					roiImage = temp(bBox2[i-1]).clone();
					//imwrite("alpha" + std::to_string(i-1) + ".jpg", roiImage);
					cone = detectCone2(roiImage, col1, col2);
					if (cone.width > 0)
					{
						cone.x = cone.x + bBox2[i-1].x;
						cone.y = cone.y + bBox2[i-1].y;
						rectangle( temp_clone, cone.tl(), cone.br(), Scalar(0,0,255), 2, 8, 0 );
						resultingRect.push_back(cone);
					}
				}
			}
		}
	}

	return std::make_pair(resultingRect, temp_clone);
}


Rect detectCone2(cv::Mat img, int col1, int col2)
{
	Rect cone;
	cv::Mat hsv, thresh1, thresh2, thresh;
	Color coneColor, stripColor;

	//Load color parameters
	coneColor = loadColorParameters(col1);
	stripColor = loadColorParameters(col2);

	//perform morphological operations on thresholded image to eliminate noise
	//and emphasize the filtered object(s)
	cv::Mat element = getStructuringElement( morph_elem, Size( 2*morph_size + 1, 2*morph_size+1 ), Point( morph_size, morph_size ) );
	// Since MORPH_X : 2,3,4,5 and 6
	int operation = morph_operator + 2;			//CLOSING
	morph_size = 2;								//THIS WAS 4 before or 8

	//Variables for findContours
	std::vector<std::vector<Point> > contours;
	std::vector<Vec4i> hierarchy;
	//std::vector<int> hull;
	std::vector<Point> hull;

	/***
		CONE SHAPE DETECTION Algorithm- Take a convex hull and:
			STEP 1.	find highest and lowest points in hull, using that find vertical center
			STEP 2.	using vertical center make list of points above and below vertical center
			STEP 3.	find left most and right most points in bottom part of list
			STEP 4.	(a)	If height/ width of convex hull >=1.25
										AND
					(b)	if list of pts above vertical center are within left most and right most points

		=> we have a cone! otherwise not
	*/
	double verticalCenter = 0;			//vertical center is avg of extreme top and bottom y coordinates
	double aspectRatio = 0;				//aspectRatio = height/ width of convex hull

	//List of top half and bottom half of points in hull std::vector
	std::vector<Point> topHalf, bottomHalf;

	//convert frame from BGR to HSV colorspace
	cvtColor(img,hsv,COLOR_BGR2HSV);

	if (col1 == 2)
	{
		//For orange color, HSV threshold is a bit tricky because H_MIN > H_MAX
		//So orange image intersection of
		// hmin< H <256; smin< S< smax; vmin< V< vmax) AND 0< H < hmax; smin< S< smax; vmin < V< vmax)
		cv::Mat threshold1, threshold2;
		inRange(hsv,Scalar(coneColor.H_MIN, coneColor.S_MIN, coneColor.V_MIN),Scalar(256, coneColor.S_MAX, coneColor.V_MAX),threshold1);
		inRange(hsv,Scalar(0, coneColor.S_MIN, coneColor.V_MIN),Scalar(coneColor.H_MAX, coneColor.S_MAX, coneColor.V_MAX),threshold2);
		thresh1 = threshold1 + threshold2;
	}
	else
	{
		//filter HSV image between values and store filtered image to threshold cv::Matrix
		inRange(hsv,Scalar(coneColor.H_MIN, coneColor.S_MIN, coneColor.V_MIN),Scalar(coneColor.H_MAX, coneColor.S_MAX, coneColor.V_MAX),thresh1);
	}

	//filter HSV image for 'coneColor and store filtered image to threshold cv::Matrix
	//inRange(hsv,Scalar(coneColor.H_MIN, coneColor.S_MIN, coneColor.V_MIN),Scalar(coneColor.H_MAX, coneColor.S_MAX, coneColor.V_MAX),thresh1);
#if DEBUG
	imshow("detectCone2 input", img);
	waitKey(30);
	imshow("Threshold on coneColor" , thresh1);
	waitKey(30);
#endif

	// for black
	/*Previous values were 0,180; 0, 255 and 0,30, 0,0
	If we increase 30 to 50, even shadows can be classified as cone black marks*/
	inRange(hsv,Scalar(stripColor.H_MIN, stripColor.S_MIN, stripColor.V_MIN),Scalar(stripColor.H_MAX, stripColor.S_MAX, stripColor.V_MAX),thresh2);
	//cv::inRange(hsv, cv::Scalar(0, 0, 0, 0), cv::Scalar(180, 255, 50, 0), thresh2);
#if DEBUG
	imshow("Threshold on stripColor", thresh2);
	waitKey(30);
#endif

//For Black cone strips, we need to perform shadow removal
	if (col2 == 3)		//BLACK
	{
		//Removing shadows from black color component of HSV image
		cv::Mat thresh2_morph;
		morphologyEx(thresh2, thresh2_morph, operation, element);

		//find countours ...
		findContours(thresh2_morph, contours, hierarchy, CV_RETR_TREE, CV_CHAIN_APPROX_SIMPLE, Point(0, 0) );

		int isPartOfCone = 0;
		for (int i = 0; i < contours.size(); i++)
		{
			Rect box = boundingRect(contours[i]);
			aspectRatio = (double)box.height / (double)box.width;

			if (aspectRatio < 0.7)
			{
				rectangle(thresh2, box.tl(), box.br(), Scalar(0,0,0), CV_FILLED);
			}
		}

#if DEBUG
		imshow("black after shadow removal" , thresh2);
		waitKey(30);
#endif
	}

	//Combine results of threshold on coneColor and stripColor
	thresh = thresh1 + thresh2;

#if DEBUG
	//Show thresholded image
	imshow("Thresholded on coneColor & stripColor combined",thresh);
	waitKey(30);
#endif

	// Apply the specified morphology operation
	morphologyEx( thresh, morphImage, operation, element );
#if DEBUG
	imshow( "Morphology_CLOSE", morphImage);
	waitKey(30);
#endif

	/*
	int operation1 = morph_operator1 + 2 ;		//OPENING
	morph_size1 = 3;
	cv::Mat element1 = getStructuringElement( morph_elem, Size( 2*morph_size1 + 1, 2*morph_size1+1 ), Point( morph_size1, morph_size1 ) );
	morphologyEx( morphImage, morphImage1, operation1, element1 );
	imshow( "Morphology_OPEN", morphImage1);
	*/

	// find contour -> convex hull -> eliminate hulls <= a size -> check if hull is pointing up
	//find countours ...
	contours.clear();
	hierarchy.clear();
	findContours(morphImage, contours, hierarchy, CV_RETR_TREE, CV_CHAIN_APPROX_SIMPLE, Point(0, 0) );
	if (contours.size() != 0)
	{
		//Sort contours in decreasing order of size
		std::sort(  std::begin(contours),
			std::end(contours),
			[&](std::vector<Point> &a, std::vector<Point> &b) { return a.size() > b.size(); } );

		//NOTE: for now only use the biggest contour
		//TO DO LATER: INCLUDE CASE WHEN MORE THAN ONE CONE PRESENT
		convexHull(cv::Mat(contours[0]), hull, false, true);

		/*
			CONE SHAPE DETECTION ALGORITHM
		*/
		//a point will be of form contours[0].hull[i]
		/*	STEP - 1	*/
		//Indices of extreme points in hull std::vector
		Point extTop   = *std::min_element(hull.begin(), hull.end(),
			[](const Point& lhs, const Point& rhs) {
				return lhs.y < rhs.y;
		});
		Point extBot   = *std::max_element(hull.begin(), hull.end(),
			[](const Point& lhs, const Point& rhs) {
				return lhs.y < rhs.y;
		});

		verticalCenter = (extBot.y + extTop.y) / 2;

		/*	STEP - 2	*/
		for (int i = 0; i < hull.size(); i++)
		{
			if (hull[i].y >= verticalCenter)
			{
				bottomHalf.push_back(hull[i]);
			}
			else
			{
				topHalf.push_back(hull[i]);
			}
		}

		/*	STEP - 3	*/
		Point extLeft  = *std::min_element( bottomHalf.begin(), bottomHalf.end(),
			[](const Point& lhs, const Point& rhs) {
				return lhs.x < rhs.x;
		});
		Point extRight = *std::max_element(bottomHalf.begin(), bottomHalf.end(),
			[](const Point& lhs, const Point& rhs) {
				return lhs.x < rhs.x;
		});

		/*	STEP - 4	*/
		aspectRatio = (double)(extBot.y - extTop.y)/(double)(extRight.x - extLeft.x);
		if (aspectRatio >= 0.9)
		{
			int k = 0;
			int coneList = 0;
			for (int i = 0; i < topHalf.size(); i++)
			{
				if (topHalf[i].x > extLeft.x && topHalf[i].x < extRight.x)
				{
					k++;
				}
			}

			if (k == topHalf.size())
			{
				coneList ++;
			}

			if (coneList > 0)
			{
				cone = boundingRect(cv::Mat(hull));

				//cv::Mat drawing = cv::Mat::zeros( morphImage.size(), CV_8UC3 );

				for( int j = 0; j < hull.size(); j++ )
				{
					Point pt = hull[j];
					circle(img, pt, 5, Scalar(0, 255, 0), CV_FILLED, CV_AA);
				}
				rectangle( img, cone.tl(), cone.br(), Scalar(255,0,255), 2, 8, 0 );
			}
		}
		else
		{
			//Rect cone;
		}
	}

#if DEBUG
	//Visualize convex hull and bounding rectangle
	//namedWindow("detected", CV_WINDOW_AUTOSIZE);
	imshow("detected", img);
	waitKey(30);
#endif

	return cone;
}

Color loadColorParameters(int col)
{
	/***
		col == 0		YELLOW
		col == 1		BLUE
		col == 2		ORANGE
		col == 3		BLACK
		col == 4		WHITE
		possible combinations are :
		1. yellow + black
		2. blue + white
		3. orange + white

		NOTE!!!	For OPENCV:
			0 < H < 180
			0 < S < 256
			0 < V < 256
	*/
	Color temp;
	switch (col)
	{
		case 0:										//YELLOW
			temp.H_MIN = 20;
			temp.H_MAX = 30;
			temp.S_MIN = 100;
			temp.S_MAX = 256;
			temp.V_MIN = 100;
			temp.V_MAX = 256;
			break;
		case 1:										//BLUE
			temp.H_MIN = 85;
			temp.H_MAX = 128;
			temp.S_MIN = 70;
			temp.S_MAX = 256;
			temp.V_MIN = 21;
			temp.V_MAX = 256;
			break;
		case 2:										//ORANGE
			temp.H_MIN = 167;
			temp.H_MAX = 8;
			temp.S_MIN = 40;
			temp.S_MAX = 256;
			temp.V_MIN = 137;
			temp.V_MAX = 256;
			break;
		case 3:										//BLACK
			temp.H_MIN = 0;
			temp.H_MAX = 180;
			temp.S_MIN = 0;
			temp.S_MAX = 255;
			temp.V_MIN = 0;
			temp.V_MAX = 50;
			break;
		case 4:										//WHITE
			temp.H_MIN = 0;
			temp.H_MAX = 180;
			temp.S_MIN = 0;
			temp.S_MAX = 116;
			temp.V_MIN = 190;
			temp.V_MAX = 256;
			break;
	default:
		break;
	}
	return temp;
}

cv::Mat drawCones(std::vector<Rect> coneVec, cv::Mat img)
{
	for (int i = 0; i < coneVec.size(); i++)
	{
		rectangle( img, coneVec[i].tl(), coneVec[i].br(), Scalar(0,0,255), 2, 8, 0 );
	}

	return img;
}


/**
* Conversion function between sl::cv::Mat and cv::Mat
**/
cv::Mat slMat2cvMat(cv::Mat& input) {
    // Mapping between cv::Mat_TYPE and CV_TYPE
    int cv_type = -1;
    switch (input.getDataType()) {
        case cv::Mat_TYPE_32F_C1: cv_type = CV_32FC1; break;
        case cv::Mat_TYPE_32F_C2: cv_type = CV_32FC2; break;
        case cv::Mat_TYPE_32F_C3: cv_type = CV_32FC3; break;
        case cv::Mat_TYPE_32F_C4: cv_type = CV_32FC4; break;
        case cv::Mat_TYPE_8U_C1: cv_type = CV_8UC1; break;
        case cv::Mat_TYPE_8U_C2: cv_type = CV_8UC2; break;
        case cv::Mat_TYPE_8U_C3: cv_type = CV_8UC3; break;
        case cv::Mat_TYPE_8U_C4: cv_type = CV_8UC4; break;
        default: break;
    }

    // Since cv::Mat data requires a uchar* pointer, we get the uchar1 pointer from sl::cv::Mat (getPtr<T>())
    // cv::Mat and sl::cv::Mat will share a single memory structure
    return cv::Mat(input.getHeight(), input.getWidth(), cv_type, input.getPtr<sl::uchar1>(MEM_CPU));
}
