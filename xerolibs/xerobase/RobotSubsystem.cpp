#include "RobotSubsystem.h"
#include "tankdrive/TankDrive.h"

using namespace xero::misc ;

namespace xero {
    namespace base {
        RobotSubsystem::RobotSubsystem(xero::base::Robot &robot, const std::string &name) : Subsystem(robot, name) {
        }

        RobotSubsystem::~RobotSubsystem() {

        }

        bool RobotSubsystem::isDefinedAndTrue(const std::string &name) {
            auto &settings = getRobot().getSettingsParser() ;
            if (!settings.isDefined(name))
                return false ;

            if (settings.getBoolean(name) == false)
                return false ;

            return true ;
        }

        void RobotSubsystem::addTankDrive() {
            auto &settings = getRobot().getSettingsParser() ;
            auto &logger = getRobot().getMessageLogger() ;
            size_t index = 1 ;
            std::list<int> left ;
            std::list<int> right ;

            //
            // Search the params file for all of the motors on the left and right sides
            //
            while (true) {
                std::string motstr = "hw:tankdrive:leftmotor:" + std::to_string(index) ;
                if (!settings.isDefined(motstr))
                    break ;
                left.push_back(settings.getInteger(motstr)) ;

                motstr = "hw:tankdrive:rightmotor:" + std::to_string(index) ;
                if (!settings.isDefined(motstr))
                    break ;
                right.push_back(settings.getInteger(motstr)) ;
                index++;
            }

            if (left.size() != right.size()) {
                logger.startMessage(MessageLogger::MessageType::error) ;
                logger << "different motor count for drive base on left and right sides" ;
                logger << ", left " << left.size() ;
                logger << ", right " << right.size() ;
                logger.endMessage() ;
            }
            
            auto tank = std::make_shared<TankDrive>(getRobot(), left, right) ;

            if (settings.isDefined("hw:tankdrive:leftencoder:1") && settings.isDefined("hw:tankdrive:leftencoder:2") &&
                settings.isDefined("hw:tankdrive:rightencoder:1") && settings.isDefined("hw:tankdrive:rightencoder:2")) {
                
                int l1 = settings.getInteger("hw:tankdrive:leftencoder:1") ;
                int l2 = settings.getInteger("hw:tankdrive:leftencoder:2") ;
                int r1 = settings.getInteger("hw:tankdrive:rightencoder:1") ;
                int r2 = settings.getInteger("hw:tankdrive:rightencoder:2") ;

                tank->setEncoders(l1, l2, r1, r2) ;
                
            } else {
                logger.startMessage(MessageLogger::MessageType::error) ;
                logger << "encoders not found in robot data file" ;
                logger.endMessage() ;
                db_ = nullptr ;         
            }

            //
            // Check for a solenoid for a shifter
            //
            if (settings.isDefined("hw:tankdrive:shifter")) {
                int sol = settings.getInteger("hw:tankdrive:shifter") ;
                tank->setGearShifter(sol) ;
            }

            //
            // Depending on how the robot is wired, these reverse the motor and/or the
            // encoders such that a positive power on the robot moves the robot forward, and
            // forward motion on the robot results in positive encoder values.
            //
            if (isDefinedAndTrue("hw:tankdrive:invert:motors:left")) {
                tank->invertLeftMotors() ;
            }

            if (isDefinedAndTrue("hw:tankdrive:invert:motors:right")) {
                tank->invertRightMotors() ;
            }

            db_ = tank ;
            addChild(db_) ;
        }
    }
}
