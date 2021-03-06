#include "TankDriveFollowPathAction.h"
#include "TankDrive.h"
#include "Robot.h"
#include <frc/smartdashboard/SmartDashboard.h>
#include <cassert>

using namespace xero::misc ;

namespace xero {
    namespace base {
        std::list<std::string> TankDriveFollowPathAction::plot_columns_ = {
            "time", 
            "ltpos", "lapos", "ltvel", "lavel", "ltaccel", "lout","lticks",
            "rtpos", "rapos", "rtvel", "ravel", "rtaccel", "rout","rticks",
            "thead", "ahead"
        } ;

        TankDriveFollowPathAction::TankDriveFollowPathAction(TankDrive &db, const std::string &name, bool reverse) : TankDriveAction(db)  {
            reverse_ = reverse;
            path_ = db.getRobot().getPathManager()->getPath(name) ;
            assert(path_ != nullptr) ;

            std::string pname = "tankdrive:follower:";
            
            left_follower_ = std::make_shared<PIDACtrl>(db.getRobot().getSettingsParser(), pname + "left:kv", 
                                pname + "left:ka", pname + "left:kp", pname + "left:kd") ;
            right_follower_ = std::make_shared<PIDACtrl>(db.getRobot().getSettingsParser(), pname + "right:kv", 
                                pname + "right:ka", pname + "right:kp", pname + "right:kd") ;

            turn_correction_ = db.getRobot().getSettingsParser().getDouble("tankdrive:follower:turn_correction") ;
            angle_correction_ = db.getRobot().getSettingsParser().getDouble("tankdrive:follower:angle_correction") ;
        }

        TankDriveFollowPathAction::~TankDriveFollowPathAction() {                
        }

        void TankDriveFollowPathAction::start() {
            left_start_ = getTankDrive().getLeftDistance() ;
            right_start_ = getTankDrive().getRightDistance() ;
            
            index_ = 0 ;         
            start_time_ = getTankDrive().getRobot().getTime() ;
            start_angle_ = getTankDrive().getAngle() ;
            target_start_angle_ = path_->getLeftSegment(0).getHeading() ;

            if (getTankDrive().hasGearShifter())
                getTankDrive().highGear() ;

            auto &logger = getTankDrive().getRobot().getMessageLogger() ;
            logger.startMessage(MessageLogger::MessageType::debug, MSG_GROUP_TANKDRIVE) ;
            logger << "runtime" ;
            logger << ",ltpos" ;
            logger << ",lapos" ;
            logger << ",ltvel" ;
            logger << ",lout" ;
            logger << "," ;
            logger << ",rtpos" ;
            logger << ",rapos" ;
            logger << ",rtvel" ;
            logger << ",rout" ;
            logger << "," ;
            logger << ",thead" ;
            logger << ",ahead" ;
            logger << ",angerr" ;
            logger << ",turn" ;
            logger.endMessage() ;
            plotid_ = getTankDrive().getRobot().startPlot(toString(), plot_columns_) ;
        }

        void TankDriveFollowPathAction::run() {
            auto &td = getTankDrive() ;
            auto &rb = td.getRobot() ;

            if (index_ < path_->size()) {
                auto &logger = td.getRobot().getMessageLogger() ;

                double dt = td.getRobot().getDeltaTime() ;
                const XeroSegment lseg = path_->getLeftSegment(index_) ;
                const XeroSegment rseg = path_->getRightSegment(index_) ;

                double laccel, lvel, lpos ;
                double raccel, rvel, rpos ;
                double thead , ahead ;

                if (reverse_) {
                    laccel = -rseg.getAccel() ;
                    lvel = -rseg.getVelocity() ;
                    lpos = -rseg.getPOS() ;
                    raccel = -lseg.getAccel() ;
                    rvel = -lseg.getVelocity() ;
                    rpos = -lseg.getPOS() ;
                    thead = xero::math::normalizeAngleDegrees(lseg.getHeading() - target_start_angle_) ;
                    ahead = xero::math::normalizeAngleDegrees(getTankDrive().getAngle() - start_angle_) ;                       
                }
                else {
                    laccel = lseg.getAccel() ;
                    lvel = lseg.getVelocity() ;
                    lpos = lseg.getPOS() ;
                    raccel = rseg.getAccel() ;
                    rvel = rseg.getVelocity() ;
                    rpos = rseg.getPOS() ;
                    thead = xero::math::normalizeAngleDegrees(lseg.getHeading() - target_start_angle_) ;  
                    ahead = xero::math::normalizeAngleDegrees(getTankDrive().getAngle() - start_angle_) ;                                      
                }

                double ldist, rdist ;

                ldist = td.getLeftDistance() - left_start_ ;
                rdist = td.getRightDistance() - right_start_ ;
                double lout = left_follower_->getOutput(laccel, lvel, lpos, ldist, dt) ;
                double rout = right_follower_->getOutput(raccel, rvel, rpos, rdist, dt) ;

#ifdef NOTYET
                //
                // This has not been tested yet
                //
                double dv = lseg.getVelocity() - rseg.getVelocity() ;
                double correct = dv * turn_correction_ ;
                lout += correct ;
                rout += correct ;
#endif
                double angerr = xero::math::normalizeAngleDegrees(thead - ahead) ;
                double turn = angle_correction_ * angerr ;
                lout += turn ;
                rout -= turn ;

                setMotorsToPercents(lout, rout) ;

                logger.startMessage(MessageLogger::MessageType::debug, MSG_GROUP_TANKDRIVE) ;
                logger << td.getRobot().getTime() - start_time_ ;
                logger << "," << lpos ;
                logger << "," << ldist ;
                logger << "," << lvel ;                
                logger << "," << lout ;
                logger << "," ;
                logger << "," << rpos ;
                logger << "," << rdist ;
                logger << "," << rvel ;                
                logger << "," << rout ;
                logger << "," ;
                logger << "," << thead ;
                logger << "," << ahead ;
                logger << "," << angerr ;
                logger << "," << turn ;
                logger.endMessage() ;

                rb.addPlotData(plotid_, index_, 0, rb.getTime() - start_time_) ;

                // Left side
                rb.addPlotData(plotid_, index_, 1, lpos) ;
                rb.addPlotData(plotid_, index_, 2, td.getLeftDistance() - left_start_) ;
                rb.addPlotData(plotid_, index_, 3, lvel) ;
                rb.addPlotData(plotid_, index_, 4, td.getLeftVelocity()) ;
                rb.addPlotData(plotid_, index_, 5, laccel) ;
                rb.addPlotData(plotid_, index_, 6, lout) ;
                rb.addPlotData(plotid_, index_, 7, td.getLeftTickCount()) ;

                // Right side
                rb.addPlotData(plotid_, index_, 8, rpos) ;
                rb.addPlotData(plotid_, index_, 9, td.getRightDistance()- right_start_) ;
                rb.addPlotData(plotid_, index_, 10, rvel) ;
                rb.addPlotData(plotid_, index_, 11, td.getRightVelocity()) ;
                rb.addPlotData(plotid_, index_, 12, raccel) ;
                rb.addPlotData(plotid_, index_, 13, rout) ;
                rb.addPlotData(plotid_, index_, 14, td.getRightTickCount()) ;

                // Angle data
                rb.addPlotData(plotid_, index_, 15, thead) ;
                rb.addPlotData(plotid_, index_, 16, ahead) ;
            }
            index_++ ;     
            if (index_ == path_->size())
                rb.endPlot(plotid_) ;                   
        }

        bool TankDriveFollowPathAction::isDone() {
            return index_ >= path_->size() ;
        }

        void TankDriveFollowPathAction::cancel()  {
            index_ = path_->size() ;
            getTankDrive().getRobot().endPlot(plotid_) ;            
        }

        std::string TankDriveFollowPathAction::toString() {
            return "TankDriveFollowPathAction-" + path_->getName() ;
        }
    }
}