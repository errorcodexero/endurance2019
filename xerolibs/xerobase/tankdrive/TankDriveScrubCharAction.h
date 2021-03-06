#pragma once

#include "TankDriveAction.h"
#include "TankDrive.h"

/// \file


namespace xero {
    namespace base {
        /// \brief This action applies a fixed power to the drivebase for a given amount of time
        /// The same voltage is applied to the left and right side.  This action is generally used to characterize
        /// the drivebase for tuning.  The power to velocity ratio can be determined.
        class TankDriveScrubCharAction : public TankDriveAction {
        public:
            /// \brief create the action
            /// \param db the drivebase for the action
            /// \param duration the duraction to apply the power
            /// \param power the power to apply to the drive base.  This is applied to the right and left side.
            /// \param highgear if true shift to high gear if possible
            TankDriveScrubCharAction(TankDrive &db, double duration, double lpower, double rpower, bool highgear = true) ;

            /// \brief destroy the action
            virtual ~TankDriveScrubCharAction() ;

            /// \brief Start the action; called once per action when it starts
            virtual void start() ;

            /// \brief Manage the action; called each time through the robot loop
            virtual void run() ;

            /// \brief Cancel the action
            virtual void cancel() ;

            /// \brief Return true if the action is complete
            /// \returns True if the action is complete
            virtual bool isDone() ;

            /// \brief return a human readable string representing the action
            /// \returns a human readable string representing the action
            virtual std::string toString() ;

        private:
            double start_time_ ;
            double start_angle_ ;
            double duration_ ;
            bool is_done_ ;
            double lvoltage_;
            double rvoltage_;
            double start_right_ ;
            double start_left_ ;
            bool high_gear_ ;
            size_t index_ ;
            
            int plotid_ ;
            static std::list<std::string> plot_columns_ ;          
        } ;
    }
}

