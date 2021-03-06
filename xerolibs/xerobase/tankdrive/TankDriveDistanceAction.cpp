#include "TankDriveDistanceAction.h"
#include "Robot.h"
#include "basegroups.h"
#include <SettingsParser.h>
#include <cmath>

using namespace xero::misc;

namespace xero {
namespace base {


const char *TankDriveDistanceAction::kvname_ = "tankdrive:follower:left:kv" ;
const char *TankDriveDistanceAction::kaname_ = "tankdrive:follower:left:ka" ;
const char *TankDriveDistanceAction::kpname_ = "tankdrive:follower:left:kp" ;
const char *TankDriveDistanceAction::kdname_ = "tankdrive:follower:left:kd" ; 

TankDriveDistanceAction::TankDriveDistanceAction(TankDrive &tank_drive, double target_distance) : TankDriveAction(tank_drive) {
    target_distance_ = target_distance; 
    is_done_ = false;
    double maxa = getTankDrive().getRobot().getSettingsParser().getDouble("tankdrive:distance_action:maxa") ;
    double maxd = getTankDrive().getRobot().getSettingsParser().getDouble("tankdrive:distance_action:maxd") ;
    double maxv = getTankDrive().getRobot().getSettingsParser().getDouble("tankdrive:distance_action:maxv") ;       
    profile_ = std::make_shared<TrapezoidalProfile>(maxa, maxd, maxv) ;

    auto &parser = tank_drive.getRobot().getSettingsParser() ;

    // Setup follower
       
    velocity_pid_ = std::make_shared<PIDACtrl>(parser, kvname_, kaname_, kpname_, kdname_) ;

    angle_pid_.initFromSettingsExtended(parser, "tankdrive:distance_action:angle_pid", true);    
}

TankDriveDistanceAction::TankDriveDistanceAction(TankDrive &tank_drive, const std::string &name) : TankDriveAction(tank_drive) {
    target_distance_ = tank_drive.getRobot().getSettingsParser().getDouble(name) ;
    is_done_ = false;
    double maxa = getTankDrive().getRobot().getSettingsParser().getDouble("tankdrive:distance_action:maxa") ;
    double maxd = getTankDrive().getRobot().getSettingsParser().getDouble("tankdrive:distance_action:maxd") ;
    double maxv = getTankDrive().getRobot().getSettingsParser().getDouble("tankdrive:distance_action:maxv") ;       
    profile_ = std::make_shared<TrapezoidalProfile>(maxa, maxd, maxv) ;

    auto &parser = tank_drive.getRobot().getSettingsParser() ;    

    // Setup follower
    velocity_pid_ = std::make_shared<PIDACtrl>(parser,kvname_, kaname_, kpname_, kdname_) ;    

    angle_pid_.initFromSettingsExtended(parser, "tankdrive:distance_action:angle_pid", true);    
}

TankDriveDistanceAction::TankDriveDistanceAction(TankDrive &tank_drive, double target_distance, double maxv) : TankDriveAction(tank_drive) {
    target_distance_ = target_distance; 
    is_done_ = false;
    double maxa = getTankDrive().getRobot().getSettingsParser().getDouble("tankdrive:distance_action:maxa") ;
    double maxd = getTankDrive().getRobot().getSettingsParser().getDouble("tankdrive:distance_action:maxd") ;
    profile_ = std::make_shared<TrapezoidalProfile>(maxa, maxd, maxv) ;

    auto &parser = tank_drive.getRobot().getSettingsParser() ;

    // Setup follower
    velocity_pid_ = std::make_shared<PIDACtrl>(parser, kvname_, kaname_, kpname_, kdname_) ;    

    angle_pid_.initFromSettingsExtended(parser, "tankdrive:distance_action:angle_pid", true);    
}

TankDriveDistanceAction::TankDriveDistanceAction(TankDrive &tank_drive, const std::string &name, const std::string &maxvname) : TankDriveAction(tank_drive) {
    target_distance_ = tank_drive.getRobot().getSettingsParser().getDouble(name) ;
    is_done_ = false;
    double maxa = getTankDrive().getRobot().getSettingsParser().getDouble("tankdrive:distance_action:maxa") ;
    double maxd = getTankDrive().getRobot().getSettingsParser().getDouble("tankdrive:distance_action:maxd") ;
    double maxv = getTankDrive().getRobot().getSettingsParser().getDouble(maxvname) ;       
    profile_ = std::make_shared<TrapezoidalProfile>(maxa, maxd, maxv) ;

    auto &parser = tank_drive.getRobot().getSettingsParser() ;    

    // Setup follower
    velocity_pid_ = std::make_shared<PIDACtrl>(parser, kvname_, kaname_, kpname_, kdname_) ;    

    angle_pid_.initFromSettingsExtended(parser, "tankdrive:distance_action:angle_pid", true);    
}

TankDriveDistanceAction::~TankDriveDistanceAction() {
}

void TankDriveDistanceAction::start() {

    profile_initial_dist_ = getTankDrive().getDist();

    xero::misc::SettingsParser &parser = getTankDrive().getRobot().getSettingsParser();

    distance_threshold_ = parser.getDouble("tankdrive:distance_action:distance_threshold");
    profile_outdated_error_long_ = parser.getDouble("tankdrive:distance_action:profile_outdated_error_long");
    profile_outdated_error_short_ = parser.getDouble("tankdrive:distance_action:profile_outdated_error_short");
    profile_outdated_error_dist_ = parser.getDouble("tankdrive:distance_action:profile_outdated_error_dist");

    profile_start_time_ = getTankDrive().getRobot().getTime();
    start_time_ = profile_start_time_ ;

    profile_->update(target_distance_, 0.0, 0.0);
    total_dist_so_far_ = 0.0 ;

    start_angle_ = getTankDrive().getAngle() ;

    if (getTankDrive().hasGearShifter())
        getTankDrive().lowGear() ;  
}

void TankDriveDistanceAction::addTriggeredAction(double dist, ActionPtr act) {
    // TODO: sort these based on distance
    auto p = std::make_pair(dist, act) ;
    triggered_actions_.push_back(p) ;
}

void TankDriveDistanceAction::addTriggeredAction(const std::string &distname, ActionPtr act) {
    double dist = getTankDrive().getRobot().getSettingsParser().getDouble(distname) ;
    addTriggeredAction(dist, act) ;
}

void TankDriveDistanceAction::checkTriggeredEvents(double dist) {
    while (triggered_actions_.size() > 0 && dist > triggered_actions_.front().first) {
        ActionPtr p = triggered_actions_.front().second ;
        triggered_actions_.pop_front() ;
        p->start() ;
        if (!p->isDone())
            running_.push_back(p) ;
    }

    bool removed = true ;
    while (removed) {
        removed = false ;
        for(auto it = running_.begin() ; it != running_.end() ; it++) {
            if ((*it)->isDone()) {
                running_.erase(it) ;
                removed = true ;
                break ;
            }
        }
    }

    for(auto act : running_)
        act->run() ;
}

void TankDriveDistanceAction::run() {
    MessageLogger &logger = getTankDrive().getRobot().getMessageLogger();

    if (!is_done_) {
        double dt = getTankDrive().getRobot().getDeltaTime() ;
        double current_distance = getTankDrive().getDist();
        double profile_distance_traveled = current_distance - profile_initial_dist_;
        double profile_remaining_distance = target_distance_ - profile_distance_traveled - total_dist_so_far_ ;
        double total_traveled = total_dist_so_far_ + profile_distance_traveled ;

        if (std::fabs(total_traveled - target_distance_) > distance_threshold_) {
            double profile_delta_time = getTankDrive().getRobot().getTime() - profile_start_time_; 
            double profile_target_distance = profile_->getDistance(profile_delta_time);
            double profile_error = std::fabs(profile_target_distance - profile_distance_traveled);

            double current_velocity = getTankDrive().getVelocity();

            bool redo = false ;

            if (profile_remaining_distance < profile_outdated_error_dist_ && profile_error > profile_outdated_error_short_)
                redo = true ;
            else if (profile_remaining_distance > profile_outdated_error_dist_ && profile_error > profile_outdated_error_long_)
                redo = true ;

            if (redo) {
                total_dist_so_far_ += profile_distance_traveled ;

                profile_start_time_ = getTankDrive().getRobot().getTime() ;
                profile_initial_dist_ = getTankDrive().getDist() ;
                profile_delta_time = 0.0 ;
                profile_->update(target_distance_ - total_traveled, current_velocity, 0.0);

                logger.startMessage(MessageLogger::MessageType::debug, MSG_GROUP_TANKDRIVE);
                logger << "Fell behind velocity profile, updating profile: " ;
                logger << profile_->toString() ;
                logger.endMessage();
            }

            double target_accel = profile_->getAccel(profile_delta_time) ;
            double target_velocity = profile_->getVelocity(profile_delta_time) ;
            double target_distance = profile_->getDistance(profile_delta_time) ;
            double base_power = velocity_pid_->getOutput(target_accel, target_velocity, target_distance, profile_distance_traveled, dt) ;

            double current_angle = xero::math::normalizeAngleDegrees(getTankDrive().getAngle() - start_angle_) ;
            double straightness_offset = angle_pid_.getOutput(0, current_angle, getTankDrive().getRobot().getDeltaTime());
            double left_power = base_power - straightness_offset;
            double right_power = base_power + straightness_offset;

            logger.startMessage(MessageLogger::MessageType::debug, MSG_GROUP_TANKDRIVE);
            logger << "TankDriveDistanceAction: time " << getTankDrive().getRobot().getTime() - start_time_ ;
            logger << ", dist " << total_traveled ;
            logger << ", profile " << profile_target_distance ;
            logger << ", target " << target_velocity;
            logger << ", actual " << current_velocity ;
            logger << ", angle " << current_angle ;
            logger << ", left " << left_power << ", right " << right_power ;
            logger.endMessage();            

            setMotorsToPercents(left_power, right_power);
        } else {
            is_done_ = true;

            logger.startMessage(MessageLogger::MessageType::debug, MSG_GROUP_TANKDRIVE);
            logger << "TankDriveDistanceAction complete";
            logger.startData("tankdrivedistanceaction_complete")
                .addData("time", getTankDrive().getRobot().getTime() - start_time_)
                .endData();
            logger.endMessage();
        }

        checkTriggeredEvents(total_traveled) ;
    }
    else {
        setMotorsToPercents(0, 0) ;  
    }
}

void TankDriveDistanceAction::cancel() {
    for(auto act : running_)
        act->cancel() ;

    running_.clear() ;
    is_done_ = true ;
}

bool TankDriveDistanceAction::isDone() {
    return is_done_;
}

std::string TankDriveDistanceAction::toString() {
    return "TankDriveDistanceAction " + std::to_string(target_distance_);
}

} // namespace base
} // namespace xero
