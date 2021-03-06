// C++ lib
#include <cstdio>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>
#include <iostream>

// C lib
#include <stdlib.h>    // For system(), getenv()
#include <math.h>
#include <assert.h>
#include <getopt.h>

// FRC
#include <networktables/NetworkTableInstance.h>
#include <cameraserver/CameraServer.h>
#include <vision/VisionPipeline.h>
#include <vision/VisionRunner.h>
#include <wpi/StringRef.h>
#include <wpi/json.h>
#include <wpi/raw_istream.h>
#include <wpi/raw_ostream.h>
#include <frc/Timer.h>
#include <frc/smartdashboard/SmartDashboard.h>
#include <frc/smartdashboard/SendableChooser.h>

// OpenCV
#include <opencv2/core/core.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/features2d.hpp>

// From xerolib
#include <FileUtils.h>
#include <StringUtils.h>
#include <GetHostIpAddresses.h>
//#include "SettingsParser.h"
#include "params_parser.h"



/*
   JSON format:
   {
       "team": <team number>,
       "ntmode": <"client" or "server", "client" if unspecified>
       "cameras": [
           {
               "name": <camera name>
               "path": <path, e.g. "/dev/video0">
               "pixel format": <"MJPEG", "YUYV", etc>   // optional
               "width": <video mode width>              // optional
               "height": <video mode height>            // optional
               "fps": <video mode fps>                  // optional
               "brightness": <percentage brightness>    // optional
               "white balance": <"auto", "hold", value> // optional
               "exposure": <"auto", "hold", value>      // optional
               "properties": [                          // optional
                   {
                       "name": <property name>
                       "value": <property value>
                   }
               ],
               "stream": {                              // optional
                   "properties": [
                       {
                           "name": <stream property name>
                           "value": <stream property value>
                       }
                   ]
               }
           }
       ]
   }
*/


namespace {

    typedef std::vector<cv::Point>       Contour;
    typedef std::vector<Contour>         Contours;
    typedef cv::RotatedRect              RRect;
    typedef std::vector<cv::RotatedRect> RRects;

    bool viewing_mode;        // Viewing mode if true, else tracking mode
    bool nobot_mode = false;  // When true, running off robot.  Set Network table in server mode, etc.
    int  selected_camera;     // Currently selected camera for viewing/tracking
    bool no_set_resolution;   // If set, don't explicitly set resolution from param file and use what's in frc.json.
    int  strategy = 0;        // Detection strategy.  0=rotated rect (default), 1=SolvePnP

    // Chooser(s) from SmartDashboard
    frc::SendableChooser<int> viewing_mode_chooser;
    frc::SendableChooser<int> camera_chooser;

    nt::NetworkTableInstance ntinst;
    const char* configFile = "/boot/frc.json";
    const cv::Scalar color_blue(255,0,0);
    const cv::Scalar color_green(0,255,0);
    const cv::Scalar color_red(0,0,255);
    const cv::Scalar color_cyan(255,255,0);
    const cv::Scalar color_orange(0,165,255);
    const cv::Scalar color_yellow(0,255,255);
    const cv::Scalar color_white(255,255,255);

    // Vision targets should be 8in apart at closest point.
    // 5.5in x 2in strips.
    // Angled about 14.5 degrees.
    double dist_bet_centers_inch = 11.5;  // APPROXIMATE.  TODO: Calculate accurately.

    paramsInput params;


    // Team number read from frc.json file
    unsigned int team;
    //bool nt_server = false;

    // Read from param file
    int width_pixels;
    int height_pixels;
    
    // Whether to stream camera's direct output
    static bool stream_camera = true;

    // Whether to stream the output image from the pipeline.
    // Should just be needed for debug.
    static bool stream_pipeline_output = false;

    // Normal pipeline is bypassed and we just pass through camera to pipeline output.
    // Should just be needed for debug.
    static bool passthru_pipe = false;

    // Network table entries where results from tracking will be posted.
    nt::NetworkTableEntry nt_pipe_fps;
    nt::NetworkTableEntry nt_pipe_runtime_ms;
    nt::NetworkTableEntry nt_target_dist_pixels;
    nt::NetworkTableEntry nt_target_dist_inch;
    nt::NetworkTableEntry nt_target_dist2_inch;
    nt::NetworkTableEntry nt_target_dist3_inch;
    nt::NetworkTableEntry nt_target_yaw_deg;
    nt::NetworkTableEntry nt_target_rect_ratio;
    nt::NetworkTableEntry nt_rect_l_angle_deg;
    nt::NetworkTableEntry nt_rect_r_angle_deg;
    nt::NetworkTableEntry nt_rect_l_height;
    nt::NetworkTableEntry nt_rect_r_height;
    nt::NetworkTableEntry nt_rect_l_width;
    nt::NetworkTableEntry nt_rect_r_width;
    nt::NetworkTableEntry nt_rect_l_dist_inch;
    nt::NetworkTableEntry nt_rect_r_dist_inch;
    nt::NetworkTableEntry nt_bot_x_offset_inch;
    nt::NetworkTableEntry nt_bot_z_offset_inch;
    nt::NetworkTableEntry nt_bot_angle2_deg;
    nt::NetworkTableEntry nt_target_valid;

    // Network table entries set by the robot
    nt::NetworkTableEntry nt_camera_number;
    nt::NetworkTableEntry nt_camera_mode;

    
    struct CameraConfig {
        std::string name;
        std::string path;
        wpi::json config;
        wpi::json streamConfig;
    };

    std::vector<CameraConfig> cameraConfigs;

    
    wpi::raw_ostream& ParseError() {
        return wpi::errs() << "config error in '" << configFile << "': ";
    }

    
    bool processCommandArgs(int argc, char* argv[]) {
        bool err = false;
        int viewing_mode_flag = 0;
        int nobot_mode_flag = 0;
        int nores_flag = 0;
        int set_strategy = -1;
        static struct option long_options[] =
            {
             /* These options set a flag. */
             {"view",     no_argument,       &viewing_mode_flag, 1},
             {"nobot",    no_argument,       &nobot_mode_flag, 1},
             {"nores",    no_argument,       &nores_flag, 1},
             {"strategy", required_argument, 0, 's'},
             {0, 0, 0, 0}
            };

        // Parse args
        while (1) {
            /* getopt_long stores the option index here. */
            int option_index = 0;

            int opt = getopt_long (argc, argv, "s:",
                                   long_options, &option_index);

            /* Detect the end of the options. */
            if (opt == -1)
                break;

            switch (opt) {
            case 0:
                /* If this option set a flag, do nothing else now. */
                if (long_options[option_index].flag != 0)
                    break;
                printf("option %s", long_options[option_index].name);
                if (optarg)
                    printf (" with arg %s", optarg);
                printf ("\n");
                break;
            case '?':
                /* getopt_long already printed an error message. */
                err = true;
                break;
            case 's':
                if (!xero::string::hasOnlyDigits(optarg)) {
                    err = true;
                    break;
                }
                set_strategy = std::atoi(optarg);
                break;
            }

        }

        if (err) {
            std::cout << "Usage: " << argv[0] << " [--view] [--nobot] [--nores] [--strategy <n>]\n";
            return false;
        }

        // Set options based on what was parsed
        viewing_mode      = (viewing_mode_flag != 0);
        nobot_mode        = (nobot_mode_flag != 0);
        no_set_resolution = (nores_flag != 0);

        if (viewing_mode_flag) {
            std::cout << "Enabled viewing mode\n" << std::flush;
        }
        if (nobot_mode_flag) {
            std::cout << "No-robot mode. NT in server mode. Controls from Shuffleboard choosers.\n" << std::flush;
        }
        if (nores_flag) {
            std::cout << "Disabling overriding of resolution that's in frc.json\n" << std::flush;
        }
        if (set_strategy != -1) {
            strategy = set_strategy;
            std::cout << "Set strategy to " << strategy << "\n" << std::flush;
        }
              
        return true;
    }

    
    bool ReadCameraConfig(const wpi::json& config) {
        CameraConfig c;

        // Read name from json config
        try {
            c.name = config.at("name").get<std::string>();
        } catch (const wpi::json::exception& e) {
            ParseError() << "Could not read camera name: " << e.what() << '\n';
            return false;
        }

        // Read path from json config
        try {
            c.path = config.at("path").get<std::string>();
        } catch (const wpi::json::exception& e) {
            ParseError() << "Camera '" << c.name
                         << "': Could not read path: " << e.what() << '\n';
            return false;
        }

        // Stream properties
        if (config.count("stream") != 0) c.streamConfig = config.at("stream");

        c.config = config;

        cameraConfigs.emplace_back(std::move(c));
        return true;
    }

    
    bool ReadConfig() {
        // Open config file
        std::error_code ec;
        wpi::raw_fd_istream is(configFile, ec);
        if (ec) {
            wpi::errs() << "Could not open '" << configFile << "': " << ec.message()
                        << '\n';
            return false;
        }

        // Parse file
        wpi::json j;
        try {
            j = wpi::json::parse(is);
        } catch (const wpi::json::parse_error& e) {
            ParseError() << "byte " << e.byte << ": " << e.what() << '\n';
            return false;
        }

        // Top level must be an object
        if (!j.is_object()) {
            ParseError() << "Must be JSON object\n";
            return false;
        }

        // Team number
        try {
            team = j.at("team").get<unsigned int>();
        } catch (const wpi::json::exception& e) {
            ParseError() << "Could not read team number: " << e.what() << '\n';
            return false;
        }

#if 0  // Network table server vs. client mode depends on detecting if running on robot.
        // ntmode (optional)
        if (j.count("ntmode") != 0) {
            try {
                auto str = j.at("ntmode").get<std::string>();
                wpi::StringRef s(str);
                if (s.equals_lower("client")) {
                    nt_server = false;
                } else if (s.equals_lower("server")) {
                    nt_server = true;
                } else {
                    ParseError() << "could not understand ntmode value '" << str << "'\n";
                }
            } catch (const wpi::json::exception& e) {
                ParseError() << "Could not read ntmode: " << e.what() << '\n';
            }
        }
#endif

        // Cameras
        try {
            for (auto&& camera : j.at("cameras")) {
                if (!ReadCameraConfig(camera)) return false;
            }
        } catch (const wpi::json::exception& e) {
            ParseError() << "Could not read cameras: " << e.what() << '\n';
            return false;
        }

        return true;
    }

    
    cs::UsbCamera StartCamera(const CameraConfig& config) {
        wpi::outs() << "Starting camera '" << config.name << "' on " << config.path
                    << '\n';
        
        auto inst = frc::CameraServer::GetInstance();
        cs::UsbCamera camera{config.name, config.path};
        cs::MjpegServer server;
        if (stream_camera) {
            server = inst->StartAutomaticCapture(camera);
        }

        camera.SetConfigJson(config.config);
        camera.SetConnectionStrategy(cs::VideoSource::kConnectionKeepOpen);

        if (config.streamConfig.is_object()) {
            if (stream_camera) {
                server.SetConfigJson(config.streamConfig);
            }
        }
            
        // Force resolution from param file
        if (!no_set_resolution) {
            camera.SetResolution(width_pixels, height_pixels);
        }

        return camera;
    }

    void waitForIpAddressOnRobot(bool verbose = false) {
        if (verbose) {
            std::cout << "Waiting for network interface..." << std::flush;
        }
        const double start_time = frc::Timer::GetFPGATimestamp();
        for (bool net_detected=false; !net_detected; ) {
            std::vector<std::string> ip_addresses = xero::misc::get_host_ip_addresses();
            for (auto addr : ip_addresses) {
                if (xero::string::startsWith(addr, "10.14.25")) {
                    // TODO: Don't hardcode IP address.  Derive from team number.
                    net_detected = true;
                    break;
                }
            }
            if (!net_detected) {
                // Wait before checking network interfaces again.
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
        const double wait_time = frc::Timer::GetFPGATimestamp() - start_time;
        if (verbose) {
            std::cout << "done. (" << wait_time << " sec wait time)\n" << std::flush;
        }
    }

    int setTrackingExposureForDevice(const std::string& device_name) {
        std::string command = std::string("v4l2-ctl -d ") + device_name + " -c exposure_auto=1 -c exposure_absolute=100 -c brightness=1 -c gain=30";
        int sysret = system(command.c_str());
        if (sysret != 0) {
            std::cout << "ERROR: Failed to call v4l2-ctl\n";
        }
        return sysret;
    }

    int setViewingExposureForDevice(const std::string& device_name) {
        std::string command = std::string("v4l2-ctl -d ") + device_name + " -c exposure_auto=3 -c brightness=128";
        int sysret = system(command.c_str());
        if (sysret != 0) {
            std::cout << "ERROR: Failed to call v4l2-ctl\n";
        }
        return sysret;
    }

    void setViewingExposure(bool viewing_mode) {
        std::cout << "Setting exposure for: " << (viewing_mode ? "viewing" : "tracking") << "\n";
        for (auto&& cameraConfig : cameraConfigs) {
            if (viewing_mode) {
                (void)setViewingExposureForDevice(cameraConfig.path);
            } else {
                (void)setTrackingExposureForDevice(cameraConfig.path);
            }
        }
    }

    // Post result of target being identified on network table, and flush immediately.
    void setTargetIsIdentified(bool target_identified) {
        nt_target_valid.SetBoolean(target_identified);
        frc::SmartDashboard::PutBoolean("TargetIdentified", target_identified);
        ntinst.Flush();
    }

    void processCameraParamChanges(std::vector<cs::VideoSource>& cameras, bool force_update_even_if_no_change = false) {
        bool new_viewing_mode = true;
        int  new_selected_camera = 0;
        
        // If data published by robot, use it and ignore SendableChooser so
        // Shuffleboard doesn't override software.
        double cam_no = nt_camera_number.GetDouble(-1);
        std::string cam_mode = nt_camera_mode.GetString("");
        
        if (cam_no != -1 && !cam_mode.empty()) {     // Selection published by robot ==> don't use chooser
            //std::cout << "From robot: " << cam_no << "    " << cam_mode << "\n";
            new_selected_camera = (cam_no == 0)? 0 : 1;
            new_viewing_mode = (cam_mode != "TargetTracking");
        } else {
            if (nobot_mode) {
                // Chooser for viewing mode
                int chooser_val = viewing_mode_chooser.GetSelected();
                if (chooser_val != 0) {  // Not unspecified
                    assert(chooser_val == 1 || chooser_val == 2);
                    new_viewing_mode = (chooser_val == 2);
                }

                // Chooser for camera
                chooser_val = camera_chooser.GetSelected();
                if (chooser_val != 0) {  // Not unspecified
                    assert(chooser_val == 1 || chooser_val == 2);
                    new_selected_camera = chooser_val - 1;
                }
            } else {
                // On robot but the 2 keys expected were not set.  Report values read.
                std::cout << "On bot but camera values invalid/not set.  cam_no='" << cam_no << "', cam_mode='" << cam_mode << "'\n"; 
                if (!force_update_even_if_no_change) {
                    // Unless initializing at the start, return rather than set default values when we can't read request from bot.
                    return;
                }
            }
        }
        
        if (force_update_even_if_no_change || (viewing_mode != new_viewing_mode)) {
            std::cout << "Setting viewing mode to " << (new_viewing_mode ? 1 : 0) << "\n" << std::flush;
            viewing_mode = new_viewing_mode;
            setViewingExposure(viewing_mode);
        }
        
        if (force_update_even_if_no_change || (selected_camera != new_selected_camera)) {
            std::cout << "Setting selected camera to " << new_selected_camera << "\n" << std::flush;
            selected_camera = new_selected_camera;
            cs::VideoSink server = frc::CameraServer::GetInstance()->GetServer();
            server.SetSource(cameras[selected_camera]);
            setTargetIsIdentified(false);
        }
    }

    // Return area of cv::RotatedRect
    double getRectArea(const cv::RotatedRect& rect) {
        cv::Size2f rect_size = rect.size;
        return (rect_size.width * rect_size.height);
    }

    // Return rectangle aspect ratio.  Always >= 1.
    double getRectAspectRatio(const cv::RotatedRect& rect) {
        cv::Size2f rect_size = rect.size;
        double aspect_ratio = 0;
        if (rect_size.height != 0) {
            aspect_ratio = rect_size.width / rect_size.height;
        }
        if (aspect_ratio < 1.0 && aspect_ratio != 0) {
            aspect_ratio = 1.0/aspect_ratio;
        }
        return aspect_ratio;
    }

    // Check if a number is approximately equal to another, +- some tolerance (percentage/100) of the second number
    bool isApproxEqual(double num1, double num2, double tolerance /*0->1*/, double tolerance_abs = 0) {
        double min = num2 * (1.0 - tolerance);
        double max = num2 * (1.0 + tolerance);
        if (min > max) {  // Needed to handle negative numbers
            std::swap(min, max);
        }
        min = std::min(min, num2 - tolerance_abs);
        max = std::max(max, num2 + tolerance_abs);
        int res = (num1 >= min) && (num1 <= max);
        return res;
    }

    // Ratio of difference between 2 numbers to the average of the two.
    // If one if 0, compare to the other.
    double ratioOfDifference(double num1, double num2) {
        double ref = (num1 + num2) / 2.0;
        if (ref == 0) {
            ref = (num1 == 0)? num2 : num1;
            if (ref == 0) {
                assert(num1 == 0);
                assert(num2 == 0);
                return 0;
            }
        }
        const double ratio = (num1 - num2) / ref;
        return fabs(ratio);
    }

    // Check if rotated rect is a left or right marker based on loose angle range
    const double rot_ang_tol = 0.3;  /*Tolerance as fraction*/
    const double rot_ang_tol_abs = 10;  /*+- that many degrees */
    bool isLeftMarker(const RRect& rect) {
        return isApproxEqual(rect.angle+90, 15, rot_ang_tol, rot_ang_tol_abs) && (rect.size.width > rect.size.height);
    }
    
    bool isRightMarker(const RRect& rect) {
        return isApproxEqual(rect.angle+90, 75, rot_ang_tol, rot_ang_tol_abs) && (rect.size.width < rect.size.height);
    }

    bool rectInTopOrBottomOfFrame(const RRect& rect) {
        const double half_height = height_pixels/2;
        const double pixels_from_vertical_middle = abs(rect.center.y - half_height);
        return (pixels_from_vertical_middle/half_height > 0.7);
    }

    bool rectTooSmall(const RRect& rect) {
        const double r_height = std::max(rect.size.height, rect.size.width);
        return ((double)r_height/height_pixels < 0.08);
    }

    bool expectedAspectRatio(const RRect& rect) {
        const double expected_aspect_of_target = 5.5/2;
        const double aspect_ratio_tolerance = 0.3;   //0.15;
        const double aspect_ratio = getRectAspectRatio(rect);
        return isApproxEqual(aspect_ratio, expected_aspect_of_target, aspect_ratio_tolerance);
    }

    bool isAMarker(const RRect& rect) {
        return isLeftMarker(rect) || isRightMarker(rect);
    }

    // Distance in pixels between centers or 2 rects
    double distBetweenRectCenters(const RRect& r1, const RRect& r2) {
        const double delta_x = fabs(r2.center.x - r1.center.x);
        const double delta_y = fabs(r2.center.y - r1.center.y);
        const double dist_bet_centers = hypot(delta_x, delta_y);
        return dist_bet_centers;
    }

    // Check if pair of rectangles is a potential pair based on ratio of dist-between-centers to height
    bool isPotentialPairBasedOnDistHeightRatio(const RRect& r1, const RRect& r2) {
        const double dist_bet_centers = distBetweenRectCenters(r1, r2);
        const double r1_height = std::max(r1.size.height, r1.size.width);
        const double r2_height = std::max(r2.size.height, r2.size.width);
        const double av_height = (r1_height + r2_height) / 2;
        
        const double meas_dist_to_height_ratio = dist_bet_centers / av_height;
        const double exp_dist_to_height_ratio = dist_bet_centers_inch / 5.5;
        return isApproxEqual(meas_dist_to_height_ratio, exp_dist_to_height_ratio, 0.3);
    }

    // Start network table in client vs. server mode depending whether running on robot or not
    // Also speed up update rate.
    void startNetworkTable(nt::NetworkTableInstance& ntinst, bool server_mode) {
        
        // If running in client mode, assume on the robot and wait until network interface is up.
        // (No Gaurantee the Rio is ready, but this is necessary to communicate with the Rio.)
        const bool running_on_robot = true;
        if (!server_mode) {
            waitForIpAddressOnRobot(true);
            
            // After network connected detected, wait a few more seconds
            // so NT server starts up on the Rio.
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    
        // Start NetworkTables
        ntinst = nt::NetworkTableInstance::GetDefault();
        if (server_mode) {
            wpi::outs() << "Setting up NetworkTables server\n";
            ntinst.StartServer();
        } else {
            wpi::outs() << "Setting up NetworkTables client for team " << team << '\n';
            ntinst.StartClientTeam(team);
        }

        // Change default update rate from 100ms to 10ms.
        // Will flush update anyway to try and reduce latency after posting set of results,
        // but it's rate-limited to prevent flooding newwork. Unclear whether this affects the cap.
        // Changing it anyway in case it does + in case flush() is not called.
        ntinst.SetUpdateRate(0.01);    // Allowed range per docs is 0.01 -> 1.0 (rate in seconds)
    }

    // Draw 1 ractangle
    void drawRectangle(cv::Mat&         frame,
                       const RRect&     rect,
                       const cv::Scalar color,
                       int              width) {
        cv::Point2f rect_points[4];
        rect.points(rect_points);
        for (int j = 0; j < 4; j++ ) {
            cv::line(frame, rect_points[j], rect_points[(j+1)%4], color, width);
        }
    }

    //Draw collection of rectangles
    void drawRectangles(cv::Mat&         frame,
                        const RRects&    rects,
                        const cv::Scalar color,
                        int              width) {
        for (auto& rect : rects) {
            drawRectangle(frame, rect, color, width);
        }
    }

    // analyze 3 rectangles at a time.  Find potential pairs, discard odd one if any.
    // Group by height of center.
    // Consider vertical dist between centers, areas, and ratio of dist-between-centers to heights.
    void filterBasedOnTriplets(RRects& rects, cv::Mat& frame) {
        if (rects.size() < 3) {
            return;
        }
        
        //drawRectangles(frame, rects, color_blue, 4);
        
        // Sort rectangles based on height of center
        std::sort(rects.begin(),
                  rects.end(),
                  [](const RRect& a, const RRect& b) -> bool
                  { 
                      return (a.center.y < b.center.y);
                  });

        const double h_tol = 0.05;
        const double a_tol = 0.3;
        for (int i=0; i<rects.size()-2; ++i) {
            RRects s_rects(rects.begin(), rects.begin()+3);
            assert(s_rects.size() == 3);
            
            // Sort triplet from left to right
            std::sort(s_rects.begin(),
                      s_rects.end(),
                      [](const RRect& a, const RRect& b) -> bool
                      { 
                          return (a.center.x < b.center.x);
                      });
            
            RRect& r0 = s_rects[i];
            RRect& r1 = s_rects[i+1];
            RRect& r2 = s_rects[i+2];

            int h0 = r0.center.y;
            int h1 = r1.center.y;
            int h2 = r2.center.y;

            double h01 = (fabs(h1-h0))/height_pixels;
            double h12 = (fabs(h2-h1))/height_pixels;
            double h02 = (fabs(h2-h0))/height_pixels;
            assert(h01 >= 0);
            assert(h12 >= 0);
            assert(h02 >= 0);

            double a0 = getRectArea(r0);
            double a1 = getRectArea(r1);
            double a2 = getRectArea(r2);

            // Figure out if each 2 is a potential pair based on multiple criteria
            bool pair01 = (h01 < h_tol) && (ratioOfDifference(a0, a1) < a_tol) && isPotentialPairBasedOnDistHeightRatio(r0, r1) && isLeftMarker(r0) && isRightMarker(r1);
            bool pair12 = (h12 < h_tol) && (ratioOfDifference(a1, a2) < a_tol) && isPotentialPairBasedOnDistHeightRatio(r1, r2) && isLeftMarker(r1) && isRightMarker(r2);
            bool pair02 = (h02 < h_tol) && (ratioOfDifference(a0, a2) < a_tol) && isPotentialPairBasedOnDistHeightRatio(r0, r2) && isLeftMarker(r0) && isRightMarker(r2);

            // If 2 are a potential pair and the third is not, discard it
            int odd_ix = -1;
            if (pair01 && !pair12 && !pair02) {
                odd_ix = 2;
            }
            else if (!pair01 && pair12 && !pair02) {
                odd_ix = 0;
            }
            else if (!pair01 && !pair12 && pair02) {
                odd_ix = 1;
            }
            if (odd_ix != -1) {
                drawRectangle(frame, rects[odd_ix], color_blue, 4);
                rects.erase(rects.begin() + odd_ix);
                --i;
                /*
                std::cout << "Odd rect #" << odd_ix << ": (h="
                          << h0 << ", " << h1 << ", " << h2
                          << ")     (hxy=";
                std::cout << h01 << ", " << h12 << ", " << h02
                          << ")     (a=";
                std::cout << a0 << ", " << a1 << ", " << a2
                          << ")\n";
                */
            } else if (false /*debug*/) {
                int sum = (int)pair01 + (int)pair12 + (int)pair02;
                if (sum > 1) {
                    std::cout << "Multiple potential pairs (" << sum << ")\n";
                }
            }
        }
    }
    
    // From list of filtered and sorted rectangles, find target pair.
    // Return empty container if no taret found, or pair where first element is the one on the left.
    RRects identifyTargetRectPair(RRects rects_in, cv::Mat& frame) {
        assert(rects_in.size() >= 2);

        // Sort rectangles on descending area
        std::sort(rects_in.begin(),
                  rects_in.end(),
                  [](const RRect& a, const RRect& b) -> bool
                  { 
                      return (getRectArea(a) > getRectArea(b));
                  });

        const RRect& largest = rects_in[0];
        const double largest_area = getRectArea(largest);

        // Only keep rects within some size difference.
        // We want this check even if we only have 2 rectangles.
        RRects filtered_rects {largest};
        for (int i=1; i<rects_in.size(); ++i) {
            const RRect& rect2 = rects_in[i];
            const double area2 = getRectArea(rect2);
            const double rel_area = ratioOfDifference(largest_area, area2);
            if (rel_area < 0.4) {
                filtered_rects.push_back(rect2);
            }
        }

        //drawRectangles(frame, filtered_rects, color_yellow, 4);

        // Sort rectangles from left to right
        std::sort(filtered_rects.begin(),
                  filtered_rects.end(),
                  [](const RRect& a, const RRect& b) -> bool
                  { 
                      return (a.center.x < b.center.x);
                  });

        // If left-most rectangle is not a left marker, discard it
        if (!filtered_rects.empty()) {
            RRect& left_rect = filtered_rects[0];
            if (!isLeftMarker(left_rect)) {
                drawRectangle(frame, left_rect, color_cyan, 4);
                filtered_rects.erase(filtered_rects.begin());
            }
        }

        // If right-most rectangle is not a right marker and we have an odd number of markers, discard it
        if (/*filtered_rects.size() > 2*/   !filtered_rects.empty()) {
            RRect& right_rect = filtered_rects[filtered_rects.size()-1];
            if (((filtered_rects.size() % 2) == 1) &&
                !isRightMarker(right_rect)) {
                drawRectangle(frame, right_rect, color_cyan, 4);
                filtered_rects.erase(filtered_rects.end()-1);
            }
        }

        // If fewer than 2 rects left or odd number, no target
        if (filtered_rects.size() < 2) {
            //std::cout << "FALSE: Didn't find at least 2 rectangles close in size (" << getRectArea(rects_in[0]) << ", " << getRectArea(rects_in[1]) << ") and have right orientation\n";
            return RRects {};
        }

        // If odd number of rects left, no target
        if ((filtered_rects.size()) % 2 == 1) {
            //std::cout << "FALSE: Odd number of filtered rects (" << filtered_rects.size() << ")\n";
            return RRects {};
        }

        // Check that we alternate left/right rectangles, else bail out.
        for (int i=0; i<filtered_rects.size(); ++i) {
            const RRect& rect = filtered_rects[i];
            if ((i % 2) == 0) {   // Expect left marker
                if (!isLeftMarker(rect)) {
                    //std::cout << "FALSE: Rect #" << i << " from left was not a left marker\n";
                    return RRects {};
                }
            } else {  // Expect right marker
                if (!isRightMarker(rect)) {
                    //std::cout << "FALSE: Rect #" << i << " from left was not a right marker\n";
                    return RRects {};
                }
            }
        }

        // Find the pair closest to the center
        int ix_of_best = -1;
        if (filtered_rects.size() == 2) {
            ix_of_best = 0;
        } else {
            int best_dist_from_center = width_pixels;  // Infinity
            for (int i=0; i<filtered_rects.size(); i+=2) {
                RRect l_rect(filtered_rects[i]);
                RRect r_rect(filtered_rects[i+1]);
                cv::Point2f center = (l_rect.center + r_rect.center) * 0.5;
                int dist_from_center = (center.x - width_pixels/2);
                if (abs(dist_from_center) < abs(best_dist_from_center)) {
                    best_dist_from_center = dist_from_center;
                    ix_of_best = i;
                }
            }
            assert(ix_of_best >= 0);
            assert(best_dist_from_center <= width_pixels/2);
        }

        RRect l_rect(filtered_rects[ix_of_best]);
        RRect r_rect(filtered_rects[ix_of_best+1]);

        assert(l_rect.center.x <= r_rect.center.x);
        RRects result {l_rect, r_rect};
        return result;            
    }

    // One component of a vision pipeline.
    // Takes in an image (cv::Mat) and produces a modified one.
    // Copy the image frame rather than modify it in place in case we later need to show the different pipeline element outputs.
    // Later, optimize to avoid copies in non-debug mode if there's a visible performance impact.
    class XeroPipelineElement {
    public:
        XeroPipelineElement(std::string name) : name_(name) {}
        virtual void Process(cv::Mat& frame_in) =0;
        virtual cv::Mat& getFrameOut() {
            return frame_out_;
        }
        
    protected:
        std::string name_;
        cv::Mat frame_out_;
    };

    // Pipeline element for: HSV Threshold
    class XeroPipelineElementHsvThreshold : public XeroPipelineElement {
        
    public:
        
        XeroPipelineElementHsvThreshold(std::string name) : XeroPipelineElement(name) {
            int h_min = params.getValue("vision:pipeline:hsv_threshold:h_min");
            int h_max = params.getValue("vision:pipeline:hsv_threshold:h_max");
            int s_min = params.getValue("vision:pipeline:hsv_threshold:s_min");
            int s_max = params.getValue("vision:pipeline:hsv_threshold:s_max");
            int v_min = params.getValue("vision:pipeline:hsv_threshold:v_min");
            int v_max = params.getValue("vision:pipeline:hsv_threshold:v_max");
            
            // Set threshold to only select green
            hsv_ranges = {h_min, h_max, s_min, s_max, v_min, v_max};
        }
        
        virtual void Process(cv::Mat& frame_in) {
            //frame_out_ = frame_in;

            cv::cvtColor(frame_in, hsv_image, cv::COLOR_BGR2HSV);

            cv::inRange(hsv_image,
                        cv::Scalar(hsv_ranges[0], hsv_ranges[2], hsv_ranges[4]),
                        cv::Scalar(hsv_ranges[1], hsv_ranges[3], hsv_ranges[5]),
                        frame_out_ /*green_only_image_*/);

#if 0       // Make frame_out a viewable image            
            cv::cvtColor(green_only_image_, frame_out_, cv::COLOR_GRAY2BGR);
#endif
        }

    private:

        std::vector<int> hsv_ranges;
        cv::Mat hsv_image;

    };

    
    // Pipeline element for: Contour detection
    class XeroPipelineElementFindContours : public XeroPipelineElement {
        
    public:
        
        XeroPipelineElementFindContours(std::string name) : XeroPipelineElement(name) {
        }
        
        virtual void Process(cv::Mat& frame_in) {
            // Convert input binary image to a viewable object.
            // Further detection will be added to this image.
            if (stream_pipeline_output) {
                cv::cvtColor(frame_in, frame_out_, cv::COLOR_GRAY2BGR);
            }
            
            // Perform contour detection
            contours.clear();
            const bool externalOnlyContours = true;
            const int mode = externalOnlyContours ? cv::RETR_EXTERNAL : cv::RETR_LIST;
            const int method = cv::CHAIN_APPROX_SIMPLE;
            cv::findContours(frame_in, contours, hierarchy, mode, method);

            // Unless we have at least 2 contours, nothing further to do
            if (contours.size() < 2) {
                setTargetIsIdentified(false);
                //std::cout << "FALSE: Fewer than 2 contours\n";
                return;
            }

            // Sort contours by keep largest ones for further processing
            // Disabled for now.  No visible performance benefit.
            /*
            std::sort(contours.begin(),
                      contours.end(),
                      [](const Contour& a, const Contour& b) -> bool
                      { 
                          return (cv::arcLength(a,true) > cv::arcLength(b,true));
                      });
            const int contour_size_lim = 12;  // Rather arbitrary
            if (contours.size() > contour_size_lim) {
                contours.resize(contour_size_lim);
            }
            */

            // Draw contours + find rectangles meeting aspect ratio requirement
            RRects min_rects(contours.size()), filtered_min_rects;

            for (int ix=0; ix < contours.size(); ++ix) {
                std::vector<cv::Point>& contour = contours[ix];

                // Find minimum rotated rectangle encapsulating the contour
                cv::RotatedRect min_rect = min_rects[ix];
                min_rect = cv::minAreaRect(contour);

                // Discard rectangles off vertical center.  Discard top and bottom of fov.
                if (rectInTopOrBottomOfFrame(min_rect)) {
                    continue;
                }

                // Discard rectangles that are too small
                if (rectTooSmall(min_rect)) {
                    continue;
                }
                
                // Draw rectangle (cyan) after excluding those in top or bottom of frame
                if (stream_pipeline_output) {
                    //drawRectangle(frame_out_, min_rect, color_cyan, 4);
                }

                // Discard rectangles that don't have the expected aspect ratio
                if (!expectedAspectRatio(min_rect)) {
                    continue;
                }

                // Draw rectangle (orange) after filtering based on aspect ratio
                if (stream_pipeline_output) {
                    drawRectangle(frame_out_, min_rect, color_orange, 4);
                }
                
                // Discard rectangles that don't have the expected angle
                // Expected angles are -15 for one and -75 for the other.
                if (!isAMarker(min_rect)) {
                    //std::cout << "FALSE: Rect angle = " << min_rect.angle << "\n";
                    continue;
                }
                
                // Draw rectangle (red) after filtering based on angle of rotation
                if (stream_pipeline_output) {
                    drawRectangle(frame_out_, min_rect, color_red, 4);
                }

                // Add filtered rectangle to list
                filtered_min_rects.push_back(min_rect);
            }

            // Only continue if we have at least 2 filtered rectangles
            if (filtered_min_rects.size() < 2) {
                setTargetIsIdentified(false);
                //std::cout << "FALSE: Fewer than 2 filtered rect\n";
                return;
            }

            // Analyze 3 rectangles at a time.
            // Find potential pairs, filter out odd one based on multiple criteria.
            //filterBasedOnTriplets(filtered_min_rects, frame_out_);
            //drawRectangles(frame_out_, filtered_min_rects, color_yellow, 4);

            // Find target pair of rectangles after pairing up and checking rectangles
            RRects rects = identifyTargetRectPair(filtered_min_rects, frame_out_);
            if (rects.empty()) {
                setTargetIsIdentified(false);
                //std::cout << "FALSE: No filtered rectangles\n";
                return;
            }
            RRect left_rect(rects[0]);
            RRect right_rect(rects[1]);

            // Draw potential target, before checking heights (yellow)
            if (stream_pipeline_output) {
                drawRectangle(frame_out_, left_rect, color_yellow, 4);
                drawRectangle(frame_out_, right_rect, color_yellow, 4);
            }

            // Top 2 rectangles must have centers almost at same height.
            // Note pixel origin (0,0) is top left corner so Y axis is reversed.
            cv::Point2f left_center = left_rect.center;
            cv::Point2f right_center = right_rect.center;
            const double delta_x = right_center.x - left_center.x;
            const double delta_y = -(right_center.y - left_center.y);
            assert(delta_x >= 0);
            double tangent = delta_y / delta_x;
            double angle_in_rad = atan(tangent);
            double angle_in_deg = angle_in_rad * 180.0 / M_PI;
            angle_in_deg = fabs(angle_in_deg);
            //std::cout << "    Angle (centers) in deg = " << angle_in_deg << "\n";
            if (angle_in_deg > 15) {
                setTargetIsIdentified(false);
                //std::cout << "FALSE: Angle in deg << " << angle_in_deg << "\n";
                return;
            }

            cv::Point2f center_point = (left_center + right_center) * 0.5;
            double dist_bet_centers = hypot(delta_x, delta_y);

            // Get dimensions of rects
            double l_rect_height = std::max(left_rect.size.height, left_rect.size.width);
            double r_rect_height = std::max(right_rect.size.height, right_rect.size.width);
            double l_rect_width = std::min(left_rect.size.height, left_rect.size.width);
            double r_rect_width = std::min(right_rect.size.height, right_rect.size.width);

            // Compare distance between centers to [average] rect height.
            // If outside of expected range, picking up invalid pair of rectangles
            const double meas_dist_to_height_ratio = dist_bet_centers / ((l_rect_height+r_rect_height)/2);
            const double exp_dist_to_height_ratio = dist_bet_centers_inch / 5.5;
            if (!isApproxEqual(meas_dist_to_height_ratio, exp_dist_to_height_ratio, 0.3)) {
                setTargetIsIdentified(false);
                //std::cout << "FALSE: Dist between centers/height not in expected range (" << meas_dist_to_height_ratio << " vs. exp. " << exp_dist_to_height_ratio << ")\n";
                return;
            }

            // Optionally use solvePnP() to find pose.
            // Note that rotated rect is approximation of actual shape so vertices may not be accurate.
            // Explore other options to find vertices.  (approxPolyDP(), goodFeaturesToTrack())
            if (strategy == 1) {
                findPose(left_rect, right_rect);
            }

            // Distance to target in inches
            // At 640x460 of C270 and 640x480 resolution, pixels_bet_centres * dist_to_target_in_FEET ~ 760
            // Product at 432x240 is 650.  So not proportional to width.
            double dist_to_target = (650.0/dist_bet_centers) * 12.0/*inches_per_foot*/ * static_cast<double>(width_pixels)/640.0;

            // At this point, top 2 rectangles meet all the criteria so likely have a valid target.
            // Draw them in green.
            if (stream_pipeline_output) {
                for (int ix=0; ix<2; ++ix) {
                    drawRectangle(frame_out_, left_rect, color_green, 2);
                    drawRectangle(frame_out_, right_rect, color_green, 2);
                }

                // Draw line between centres
                cv::line(frame_out_, left_center, right_center, color_green, 2);

                // Draw marker through center
                cv::drawMarker(frame_out_, center_point, color_green, cv::MARKER_CROSS, 20 /*marker size*/, 2 /*thickness*/);
            }

            // Calculate offset = ratio right rectangle / left rectangle.
            // If ratio > 1 ==> robot right of target
            // If ratio < 1 ==> robot left of target
            double rect_ratio = getRectArea(right_rect) / getRectArea(left_rect);
            nt_target_rect_ratio.SetDouble(rect_ratio);

            // Estimate yaw.  Assume both rectangles at equal height (among other things).
            //double yaw = pixels_off_center * (camera_hfov_deg / width_pixels);
            double pixels_off_center = center_point.x - (width_pixels/2);
            double pixels_per_inch = dist_bet_centers / dist_bet_centers_inch;
            double inches_off_center = pixels_off_center / pixels_per_inch;
            double yaw_in_rad = atan(inches_off_center / dist_to_target);
            double yaw_in_deg = yaw_in_rad * 180.0 / M_PI;
            nt_target_yaw_deg.SetDouble(yaw_in_deg);

            // Estimate distance to each rectangle based on its height + coordinate of bot rel to target
            double l_rect_dist_inch = 12.0 * (206.0/l_rect_height) * (height_pixels/240.0);
            double r_rect_dist_inch = 12.0 * (206.0/r_rect_height) * (height_pixels/240.0);
            double bot_x_offset_inch = (pow(r_rect_dist_inch,2) - pow(l_rect_dist_inch,2))/(2*dist_bet_centers_inch);
            double bot_z_offset_inch = sqrt(pow(l_rect_dist_inch,2) - pow(bot_x_offset_inch - 0.5*dist_bet_centers_inch,2));
            double dist2_inch = sqrt(pow(bot_x_offset_inch,2) + pow(bot_z_offset_inch,2));
            double dist3_inch = (l_rect_dist_inch + r_rect_dist_inch)/2;
            double bot_angle2_deg = atan2(bot_x_offset_inch, bot_z_offset_inch) * 180.0 / M_PI;
            bot_x_offset_inch = -bot_x_offset_inch;  // Flip X coordinate to negative if bot on left of target, not opposite.
            nt_rect_l_dist_inch.SetDouble(l_rect_dist_inch);
            nt_rect_r_dist_inch.SetDouble(r_rect_dist_inch);
            nt_bot_x_offset_inch.SetDouble(bot_x_offset_inch);
            nt_bot_z_offset_inch.SetDouble(bot_z_offset_inch);
            nt_bot_angle2_deg.SetDouble(bot_angle2_deg);
            
            // Publish info on the 2 rectangles.
            nt_rect_l_angle_deg.SetDouble(left_rect.angle);
            nt_rect_r_angle_deg.SetDouble(right_rect.angle);
            nt_rect_l_height.SetDouble(l_rect_height);
            nt_rect_r_height.SetDouble(r_rect_height);
            nt_rect_l_width.SetDouble(l_rect_width);
            nt_rect_r_width.SetDouble(r_rect_width);


            // TODO: Filter on vertical distance of rect from center?  Only keep pairs of rectangles meeting other critria that are at similar height.
            //       Measure angle vs. perpendicular to target
            //       Measure coordinates & orientation vs. target.

            // Publish other results on network table
            nt_target_dist_pixels.SetDouble(dist_bet_centers);
            nt_target_dist_inch.SetDouble(dist_to_target);
            nt_target_dist2_inch.SetDouble(dist2_inch);
            nt_target_dist3_inch.SetDouble(dist3_inch);

            //std::cout << "Rect angles: " << left_rect.angle << ", " << right_rect.angle << "\n";
            setTargetIsIdentified(true);  // Also flushed NT, so keep this call at the end of NT updates.
        }

    private:

        void findPose(const RRect& left_rect, const RRect& right_rect) const;

        Contours contours;
        std::vector<cv::Vec4i> hierarchy;

    };


    void XeroPipelineElementFindContours::findPose(const RRect& left_rect,
                                                   const RRect& right_rect) const {
        std::vector<cv::Point3f> object_points;
        cv::Mat                  object_points_mat;

        cv::Mat_<double> camera_matrix;
        cv::Mat_<double> distortion_coeffs;
        
        object_points.push_back(cv::Point3f(-5.93, -2.91, 0));
        object_points.push_back(cv::Point3f(-7.31,  2.41, 0));
        object_points.push_back(cv::Point3f(-5.38,  2.91, 0));
        object_points.push_back(cv::Point3f(-4   , -2.41, 0));
        object_points.push_back(cv::Point3f( 4   , -2.41, 0));
        object_points.push_back(cv::Point3f( 5.38,  2.91, 0));
        object_points.push_back(cv::Point3f( 7.31,  2.41, 0));
        object_points.push_back(cv::Point3f( 5.93, -2.91, 0));
        object_points_mat = cv::Mat(object_points);

        // To be initialized properly later after we add camera calibration.
        // For now, just allow solvePnP() to run.
        int focal_length = 1642;  // From example. Probably incorrect.
        int center_x = width_pixels / 2;
        int center_y = height_pixels / 2;

        camera_matrix.create(3, 3);
        camera_matrix = 0.;
        camera_matrix(0, 0) = focal_length;
        camera_matrix(1, 1) = focal_length;
        camera_matrix(0, 2) = center_x;
        camera_matrix(1, 2) = center_y;
        camera_matrix(2, 2) = 1.f;        

        distortion_coeffs.create(1, 4);
        distortion_coeffs = 0.f;

        distortion_coeffs(0, 0) = 0.09059;
        distortion_coeffs(0, 1) = -0.16979;
        distortion_coeffs(0, 2) = -0.00796;
        distortion_coeffs(0, 3) = -0.00078;

        std::vector<cv::Point2f> image_pts(8);
        cv::Point2f rect_points[4];
        left_rect.points(rect_points);
        for (int i=0; i<4; ++i) {
            image_pts[i] = rect_points[0];
            for (int j=1; j<4; ++j) {
                switch (i) {
                case 0:
                    if (rect_points[j].y < image_pts[i].y) {
                        image_pts[i] = rect_points[j];
                    }
                    break;
                case 1:
                    if (rect_points[j].x < image_pts[i].x) {
                        image_pts[i] = rect_points[j];
                    }
                    break;
                case 2:
                    if (rect_points[j].y > image_pts[i].y) {
                        image_pts[i] = rect_points[j];
                    }
                    break;
                case 3:
                    if (rect_points[j].x > image_pts[i].x) {
                        image_pts[i] = rect_points[j];
                    }
                    break;
                }
            }
        }        
        right_rect.points(rect_points);
        for (int i=4; i<8; ++i) {
            image_pts[i] = rect_points[0];
            for (int j=1; j<4; ++j) {
                switch (i) {
                case 4:
                    if (rect_points[j].x < image_pts[i].x) {
                        image_pts[i] = rect_points[j];
                    }
                    break;
                case 5:
                    if (rect_points[j].y > image_pts[i].y) {
                        image_pts[i] = rect_points[j];
                    }
                    break;
                case 6:
                    if (rect_points[j].x > image_pts[i].x) {
                        image_pts[i] = rect_points[j];
                    }
                    break;
                case 7:
                    if (rect_points[j].y < image_pts[i].y) {
                        image_pts[i] = rect_points[j];
                    }
                    break;
                }
            }
        }        
        
        cv::Mat_<double> rvec(cv::Size(3, 1));;
        cv::Mat_<double> tvec(cv::Size(3, 1));;
        bool res = cv::solvePnP(object_points_mat, image_pts, camera_matrix, distortion_coeffs, rvec, tvec, cv::SOLVEPNP_ITERATIVE /*cv::SOLVEPNP_AP3P*/);

        // Switch to perspective of camera relative to target
        cv::Mat_<double> rvec2(cv::Size(3, 1));;
        cv::Mat_<double> tvec2(cv::Size(3, 1));;
        cv::Mat R;
        cv::Rodrigues(rvec, R);
        R = R.t();
        tvec2 = -R*tvec;
        cv::Rodrigues(R, rvec2);
 
        double tx = tvec2.at<double>(0, 0);
        double ty = tvec2.at<double>(1, 0);
        double tz = tvec2.at<double>(2, 0);
        double dist = std::sqrt(tx*tx + ty*ty + tz*tz);

        std::cout << "solvePnP result = " << res << ",  tx/ty/tz/dist = " << tx << ", " << ty << ", " << tz << ", " << dist << "\n";
    }


    /*
    // Pipeline element for: using solvePnP() for pose detection
    class XeroPipelineElementSolvePnP : public XeroPipelineElement {
        
    public:
        
        XeroPipelineElementSolvePnP(std::string name);
        
        virtual void Process(cv::Mat& frame_in);
        
    private:
        
        std::vector<cv::Point3f> object_points;
        cv::Mat                  object_points_mat;

        cv::Mat_<double> camera_matrix;
        cv::Mat_<double> distortion_coeffs;
    };
        
        
    XeroPipelineElementSolvePnP::XeroPipelineElementSolvePnP(std::string name) : XeroPipelineElement(name) {
        object_points.push_back(cv::Point3f(-5.93, -2.91, 0));
        object_points.push_back(cv::Point3f(-7.31,  2.41, 0));
        object_points.push_back(cv::Point3f(-5.38,  2.91, 0));
        object_points.push_back(cv::Point3f(-4   , -2.41, 0));
        object_points.push_back(cv::Point3f( 4   , -2.41, 0));
        object_points.push_back(cv::Point3f( 5.38,  2.91, 0));
        object_points.push_back(cv::Point3f( 7.31,  2.41, 0));
        object_points.push_back(cv::Point3f( 5.93, -2.91, 0));
        object_points_mat = cv::Mat(object_points);

        // To be initialized properly later after we add camera calibration.
        // For now, just allow solvePnP() to run.
        camera_matrix.create(3, 3);
        camera_matrix = 0.;
        camera_matrix(0, 0) = 1; //3844.4400000000001f;
        camera_matrix(1, 1) = 1; //3841.0599999999999f;
        camera_matrix(0, 2) = 1; //640.f;
        camera_matrix(1, 2) = 1; //380.f;
        camera_matrix(2, 2) = 1.f;

        distortion_coeffs.create(1, 4);
        distortion_coeffs = 0.f;
    }
    
    void XeroPipelineElementSolvePnP::Process(cv::Mat& frame_in) {
        // Convert input binary image to a viewable object.
        // Further detection will be added to this image.
        if (stream_pipeline_output) {
            cv::cvtColor(frame_in, frame_out_, cv::COLOR_GRAY2BGR);
        }

        cv::Mat_<double> rvec(cv::Size(3, 1));;
        cv::Mat_<double> tvec(cv::Size(3, 1));;
        cv::solvePnP(object_points_mat, frame_in, camera_matrix, distortion_coeffs, rvec, tvec, cv::SOLVEPNP_ITERATIVE);
    }
    */
    
    // Pipeline that combines all pipeline elements and is called on every frame
    class XeroPipeline : public frc::VisionPipeline {
    public:
        const int frames_to_sample_per_report = 20;
        int frames_processed = 0;
        double total_processing_time = 0;

        XeroPipeline() {
            pipe_elements_.push_back(new XeroPipelineElementHsvThreshold("HSV Threshold"));
            pipe_elements_.push_back(new XeroPipelineElementFindContours("Find Contours"));

#if 0
            switch (strategy) {
            case 0:
                pipe_elements_.push_back(new XeroPipelineElementFindContours("Find Contours"));
                break;
            case 1:
                pipe_elements_.push_back(new XeroPipelineElementSolvePnP("solvePnP"));
                break;
            default:
                std::cout << "Invalid strategy\n";
                exit(-1);
            }
#endif
        }

        virtual ~XeroPipeline();

        virtual void Process(cv::Mat& mat) override {
            const double start_time = frc::Timer::GetFPGATimestamp();
            //std::cout << "Process  " << mat.cols << "   " << mat.rows << "\n";
            cv::Mat* frame_out_p = &mat;
            if (passthru_pipe) {
                // Debug mode only. Bypass normal pipeline.
                output_frame_ = mat;
            } else {
                for (auto& elem : pipe_elements_) {
                    elem->Process(*frame_out_p);
                    frame_out_p = &(elem->getFrameOut());
                }
                output_frame_ = *frame_out_p;
            }
            ++frames_processed;
            const double end_time = frc::Timer::GetFPGATimestamp();
            total_processing_time += (end_time - start_time);

            // Report average processing time every X calls,
            // then reset metrics for next window to measure and report
            if (frames_processed == frames_to_sample_per_report) {
                double runtime_ms = 1000.0 * total_processing_time / frames_processed;
                nt_pipe_runtime_ms.SetDouble(runtime_ms);
                //std::cout << "Average pipe processing time per frame (ms) = " << runtime_ms << " (" << frames_processed << " frames)\n";
                frames_processed = 0;
                total_processing_time = 0;
            }
        }

        cv::Mat output_frame_;

    private:
        std::vector<XeroPipelineElement*> pipe_elements_;
    };

    class VisionPipelineResultProcessor {
    public:

        VisionPipelineResultProcessor(bool stream_output) : stream_output_(stream_output) {
            if (stream_output_) {
                output_stream_ = frc::CameraServer::GetInstance()->PutVideo("Pipeline Output", width_pixels, height_pixels);
            }
            start_time = frc::Timer::GetFPGATimestamp();
            times_called = 0;
        }
        
        void operator()(XeroPipeline& pipe) {
            ++times_called;
            if (stream_output_) {
                output_stream_.PutFrame(pipe.output_frame_);
            }

            // For debug of false positives
            /*
            if (!nt_target_valid.GetBoolean(false)) {
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
            */
            
            if ((times_called % 20) == 0) {
                const double current_time = frc::Timer::GetFPGATimestamp();
                double elapsed_time = current_time - start_time;
                double fps = static_cast<double>(times_called) / elapsed_time;
                nt_pipe_fps.SetDouble(fps);
                //std::cout << "fps = " << fps << "\n";
                start_time = current_time;
                times_called = 0;
            }
        }
        
    private:

        cs::CvSource output_stream_;
        bool stream_output_;
        double start_time;
        int times_called;
    };


    XeroPipeline::~XeroPipeline() {
        for (auto& elem : pipe_elements_) {
            delete elem;
        }
    }


    void runPipelineFromCamera(/*std::vector<CameraConfig>& cameraConfigs*/) {
        
        // Start camera streaming
        std::vector<cs::VideoSource> cameras;
        for (auto&& cameraConfig : cameraConfigs) {
            // Wait for camera device to be readable otherwise starting camera will fail
            while (!xero::file::is_readable(cameraConfig.path)) {
                std::cout << "Waiting for camera device " << cameraConfig.path << " to become readable\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        
            cameras.emplace_back(StartCamera(cameraConfig));
        }

        // Start image processing if present.  First one only.
        if (cameras.size() >= 1) {
            std::cout << "Starting vision pipeline\n";

            std::thread t([&] {
                            auto pipe = std::make_shared<XeroPipeline>();
                            typedef frc::VisionRunner<XeroPipeline> PipeRunner;
                            VisionPipelineResultProcessor result_processor(stream_pipeline_output);
                            std::vector<std::shared_ptr<PipeRunner> > runners(2);
                            runners[0] = std::make_shared<PipeRunner>(cameras[0],
                                                                      pipe.get(),
                                                                      result_processor);
                            runners[1] = std::make_shared<PipeRunner>(cameras[1],
                                                                      pipe.get(),
                                                                      result_processor);

                            // Before starting loop, ensure exposure set consistent with the viewing mode.
                            // Manually set camera controls directly using v4l2-ctl. More granularity than cscore APIs.
                            // Apparently only works after starting the pipeline + small delay.
                            std::this_thread::sleep_for(std::chrono::seconds(2));
                            processCameraParamChanges(cameras, true /*force update*/);

                            while (1) {
                                processCameraParamChanges(cameras);
                                runners[selected_camera].get()->RunOnce();
                            }
                          });
            t.detach();
        }

        // Loop forever
        for (;;) std::this_thread::sleep_for(std::chrono::seconds(10));
    }

    void runPipelineFromVideo(const std::string& video_filename) {
        if (!xero::file::exists(video_filename)) {
            std::cout << "Video file '" << video_filename << "' does not exist.\n";
            exit(-1);
        }

        VisionPipelineResultProcessor pipe_result_processor(stream_pipeline_output);
        XeroPipeline pipe;

        bool playVideo = true;
        char key;
        std::cout << "DEBUG: Just before cv::VideoCapture\n";
        cv::VideoCapture capture(video_filename);
        std::cout << "DEBUG: Just after cv::VideoCapture\n";

        cv::Mat frame;
        while (capture.get(cv::CAP_PROP_POS_FRAMES) < capture.get(cv::CAP_PROP_FRAME_COUNT)) {

            if (!capture.isOpened()) {
                std::cout << "Could not read video\n";
                exit(-1);
            }
            if (playVideo) {
                capture >> frame;
                pipe.Process(frame);
                pipe_result_processor(pipe);
            }
#if 0
            key = cv::waitKey(10);
            if (key == 'p') {
                playVideo = !playVideo;
            } else if (key == 'q') {
                break;
            }
#endif
            //sleep(10);
        }
        capture.release();
        
    }


}  // namespace



int main(int argc, char* argv[]) {
    const std::string params_filename("vision_params.txt");

    if (!processCommandArgs(argc, argv)) {
        return 1;
    }


    // Read params file
    if (!params.readFile(params_filename)) {
        return EXIT_FAILURE;
    }

    // Read configuration
    if (!ReadConfig()) {
        return EXIT_FAILURE;
    }

    // Import global settings from param file
    const std::string stream_camera_param_name("vision:stream_camera");
    if (params.hasParam(stream_camera_param_name)) {
        stream_camera = (params.getValue(stream_camera_param_name) != 0);
    }
    const std::string stream_pipeline_output_param_name("vision:stream_pipeline_output");
    if (params.hasParam(stream_pipeline_output_param_name)) {
        stream_pipeline_output = (params.getValue(stream_pipeline_output_param_name) != 0);
    }
    const std::string passthru_pipe_param_name("vision:passthru_pipe");
    if (params.hasParam(passthru_pipe_param_name)) {
        passthru_pipe = (params.getValue(passthru_pipe_param_name) != 0);
    }
    width_pixels = params.getValue("vision:camera:width_pixels");
    height_pixels = params.getValue("vision:camera:height_pixels");

    // For solvePnP, temporarily set high resolution and ignore param file
    if (strategy == 1) {
        width_pixels = 960;
        height_pixels = 720;
        std::cout << "Setting resolution to " << width_pixels << "x" << height_pixels << " to support detection strategy\n";
    }

    // Start network table in client or server mode + configure it
    // nt_server_mode may have been configured via command-line args already. Envar overrides it.
    const char* nobot_mode_envar = getenv("NOBOT");
    if (nobot_mode_envar != nullptr) {
        nobot_mode = (std::string(nobot_mode_envar) == "1");
    }
    ntinst = nt::NetworkTableInstance::GetDefault();
    startNetworkTable(ntinst, nobot_mode);
    
    // Prepare network table variables that tracker will populate + those set by robot
    std::shared_ptr<NetworkTable> nt_table = ntinst.GetTable("TargetTracking");
    nt_pipe_fps = nt_table->GetEntry("pipe_fps");
    nt_pipe_fps.SetDefaultDouble(0);
    nt_pipe_runtime_ms = nt_table->GetEntry("pipe_runtime_ms");
    nt_pipe_runtime_ms.SetDefaultDouble(0);
    nt_target_dist_pixels = nt_table->GetEntry("dist_pixels");
    nt_target_dist_pixels.SetDefaultDouble(0);
    nt_target_dist_inch = nt_table->GetEntry("dist_inch");
    nt_target_dist_inch.SetDefaultDouble(0);
    nt_target_dist2_inch = nt_table->GetEntry("dist2_inch");
    nt_target_dist2_inch.SetDefaultDouble(0);
    nt_target_dist3_inch = nt_table->GetEntry("dist3_inch");
    nt_target_dist3_inch.SetDefaultDouble(0);
    nt_target_yaw_deg = nt_table->GetEntry("yaw_deg");
    nt_target_yaw_deg.SetDefaultDouble(0);
    nt_target_rect_ratio = nt_table->GetEntry("rect_ratio");
    nt_target_rect_ratio.SetDefaultDouble(0);
    nt_rect_l_angle_deg = nt_table->GetEntry("rect_l_angle_deg");
    nt_rect_l_angle_deg.SetDefaultDouble(0);
    nt_rect_r_angle_deg = nt_table->GetEntry("rect_r_angle_deg");
    nt_rect_r_angle_deg.SetDefaultDouble(0);
    nt_rect_l_height = nt_table->GetEntry("rect_l_height");
    nt_rect_l_height.SetDefaultDouble(0);
    nt_rect_r_height = nt_table->GetEntry("rect_r_height");
    nt_rect_r_height.SetDefaultDouble(0);
    nt_rect_l_width = nt_table->GetEntry("rect_l_width");
    nt_rect_l_width.SetDefaultDouble(0);
    nt_rect_r_width = nt_table->GetEntry("rect_r_width");
    nt_rect_r_width.SetDefaultDouble(0);
    nt_rect_l_dist_inch = nt_table->GetEntry("rect_l_dist_inch");
    nt_rect_l_dist_inch.SetDefaultDouble(0);
    nt_rect_r_dist_inch = nt_table->GetEntry("rect_r_dist_inch");
    nt_rect_r_dist_inch.SetDefaultDouble(0);
    nt_bot_x_offset_inch = nt_table->GetEntry("bot_x_offset_inch");
    nt_bot_x_offset_inch.SetDefaultDouble(0);
    nt_bot_z_offset_inch = nt_table->GetEntry("bot_z_offset_inch");
    nt_bot_z_offset_inch.SetDefaultDouble(0);
    nt_bot_angle2_deg = nt_table->GetEntry("bot_angle2_deg");
    nt_bot_angle2_deg.SetDefaultDouble(0);
    nt_target_valid = nt_table->GetEntry("valid");
    nt_target_valid.SetDefaultBoolean(false);
    setTargetIsIdentified(false);

    nt_camera_number = nt_table->GetEntry("camera_number");
    nt_camera_mode = nt_table->GetEntry("camera_mode");

    // Set choosers from SmartDashboard
    viewing_mode_chooser.SetDefaultOption("1. Unspecified", 0);
    viewing_mode_chooser.AddOption("2. Track", 1);
    viewing_mode_chooser.AddOption("3. View", 2);
    frc::SmartDashboard::PutData("ViewingMode", &viewing_mode_chooser);
    camera_chooser.SetDefaultOption("1. Unspecified", 0);
    camera_chooser.AddOption("2. Cam0", 1);
    camera_chooser.AddOption("3. Cam1", 2);
    frc::SmartDashboard::PutData("Camera", &camera_chooser);
    
    // Start camera streaming + image processing on last camera if present
    runPipelineFromCamera(/*cameraConfigs*/);

    // Start image processing from video file
    //runPipelineFromVideo("cube1.mp4");
    //runPipelineFromVideo("capout.avi");
}
