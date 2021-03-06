#include "ReadyAction.h"
#include <lifter/Lifter.h>
#include <lifter/LifterGoToHeightAction.h>
#include "turntable/TurntableGoToAngleAction.h"
#include "carloshatch/CarlosHatchArmAction.h"
#include "phaserids.h"
#include <Robot.h>
#include <xeromath.h>
#include <MessageLogger.h>
#include <cmath>

using namespace xero::base ;
using namespace xero::misc ;

namespace xero {
    namespace phaser {
        ReadyAction::ReadyAction(GamePieceManipulator &subsystem, const std::string &height, const std::string &angle, bool leave):GamePieceAction(subsystem) {

            leave_ = leave ;
        
            auto lifter = getGamePiece().getLifter() ;
            auto turntable = getGamePiece().getTurntable();
            auto hatch_holder = getGamePiece().getHatchHolder() ;

            height_ = height ;
            angle_ = angle ;

            if (height == "@")
                height_value_ = lifter->getHeight() ;
            else
                height_value_ = subsystem.getRobot().getSettingsParser().getDouble(height) ;

            if (angle == "@")
                angle_value_ = turntable->getAngleValue() ;
            else
                angle_value_ = subsystem.getRobot().getSettingsParser().getDouble(angle) ;

            set_lifter_safe_height_ = std::make_shared<LifterGoToHeightAction>(*lifter, "turntable:safe_lifter_height") ;
            set_lifter_final_height_ = std::make_shared<LifterGoToHeightAction>(*lifter, height_value_) ;
            set_turntable_angle_ = std::make_shared<TurntableGoToAngleAction>(*turntable, angle_value_) ;
            extend_hatch_holder_ = std::make_shared<CarlosHatchArmAction>(*hatch_holder, CarlosHatchArmAction::Operation::EXTEND) ;
            retract_hatch_holder_ = std::make_shared<CarlosHatchArmAction>(*hatch_holder, CarlosHatchArmAction::Operation::RETRACT) ;

            turntable_velocity_threshold_ = 5.0 ;
        }

        ReadyAction::~ReadyAction(){
        }

        bool ReadyAction::alreadyOnCorrectSide() {
            auto turntable = getGamePiece().getTurntable() ;
            double diff = xero::math::normalizeAngleDegrees(turntable->getAngleValue() - set_turntable_angle_->getTargetAngle()) ;
            return std::fabs(diff) < 10.0 ;
        }

        void ReadyAction::start() {
            auto lifter = getGamePiece().getLifter() ;
            auto turntable = getGamePiece().getTurntable() ;
            auto hatch_holder = getGamePiece().getHatchHolder() ;
            MessageLogger &logger = getGamePiece().getRobot().getMessageLogger() ;

            oldstate_ = State::Idle ;

            if (turntable->getVelocity() > turntable_velocity_threshold_) {
                state_ = State::WaitForStop ;
            }
            //
            // We have to do two things here, rotate the turntable to the right angle and get the
            // lifter to the right height.  We cannot rotate the turntable until the lift is above the
            // safe height.  If the destination height is below the safe height, we need to go to the safe
            // height first, rotate, then drop down.
            //
            else if (hatch_holder->hasHatch() && !alreadyOnCorrectSide() && !hatch_holder->isArmDeployed() && !leave_) {
                logger.startMessage(MessageLogger::MessageType::debug, MSG_GROUP_READY_ACTION) ;                
                logger << "ReadyAction: start with extend hatch" ;
                logger.endMessage() ;

                //
                // We are going to rotate with a hatch, we always have to
                // extend the hatch holder
                //
                hatch_holder->setAction(extend_hatch_holder_) ;
                hatch_holder_extended_ = true ;
                state_ = State::ExtendHatchHolder ;
            }
            else {
                logger.startMessage(MessageLogger::MessageType::debug, MSG_GROUP_READY_ACTION) ;                
                logger << "ReadyAction: start with turntable/lift sequence" ;                   
                logger.endMessage() ;

                startTurnLiftSequence() ;
                hatch_holder_extended_ = false ;                             
            }

            logger.endMessage() ;
        }

        void ReadyAction::startTurnLiftSequence() {
            auto turntable = getGamePiece().getTurntable() ;  
            auto hatch_holder = getGamePiece().getHatchHolder() ;
            auto lifter = getGamePiece().getLifter() ;

            MessageLogger &logger = getGamePiece().getRobot().getMessageLogger() ;            
            logger.startMessage(MessageLogger::MessageType::debug, MSG_GROUP_READY_ACTION) ;
            logger << "ReadyAction: startTurnLiftSequence " ;            

            if (alreadyOnCorrectSide()) {
                logger << "already on correct side" ;
                logger.endMessage() ; 

                //
                // No rotation needed, just go to right height
                //
                lifter->setAction(set_lifter_final_height_) ;
                state_ = State::LifterFinalHeight ;
               
            }
            else {
                //
                // Need to rotate to the correct angle, if the hatch
                // holder needs to be extended, this is already done
                //
                if (turntable->isSafeToRotate()) {
                    //
                    // The turntable is already above the safe height to rotate so we
                    // start rotating right away. We also start any lift changes we can
                    // do concurrently.
                    //

                    if (height_value_ > turntable->getSafeRotateHeight()) {
                        logger << "need to turn, above safe height, target above safe height" ;
                        logger.endMessage() ;

                        //
                        // The start and destination heights are above the safe
                        // height so we can do everything concurrently
                        //
                        lifter->setAction(set_lifter_final_height_) ;
                        turntable->setAction(set_turntable_angle_) ;                        
                        state_ = State::TurntableAndLift ;

                    }
                    else {
                        logger << "need to turn, above safe height, target below safe height" ;                        
                        logger.endMessage() ;
                        //
                        // The destination height is below the safe lift height, so we
                        // rotate and go to the safe height conncurrently
                        //
                        lifter->setAction(set_lifter_safe_height_) ;
                        turntable->setAction(set_turntable_angle_) ;                        
                        state_ = State::TurntableAndSafeHeight ;
                    }
                }
                else {
                    logger << "need to turn, below safe height" ;
                    logger.endMessage() ;
                    //
                    // The turntable is below the safe height, so go to the safe height
                    // first.
                    //
                    lifter->setAction(set_lifter_safe_height_) ;
                    state_ = State::LifterSafeHeight ;
                }
            }    
        }

        void ReadyAction::run() {
            MessageLogger &logger = getGamePiece().getRobot().getMessageLogger() ;
            auto turntable = getGamePiece().getTurntable() ;
            auto hatch_holder = getGamePiece().getHatchHolder() ;

            if (oldstate_ != state_) {
                logger.startMessage(MessageLogger::MessageType::debug, MSG_GROUP_READY_ACTION) ;
                logger << "ReadyAction: run, state " << toString(oldstate_) << " -> " << toString(state_) ;
                logger.endMessage() ;

                oldstate_ = state_ ;
            }

            switch(state_) {
            case State::WaitForStop:
                if (turntable->getVelocity() < turntable_velocity_threshold_) {
                    if (hatch_holder->hasHatch() && !alreadyOnCorrectSide() && !leave_) {
                        logger.startMessage(MessageLogger::MessageType::debug, MSG_GROUP_READY_ACTION) ;                
                        logger << "ReadyAction: start with extend hatch" ;
                        logger.endMessage() ;

                        //
                        // We are going to rotate with a hatch, we always have to
                        // extend the hatch holder
                        //
                        hatch_holder->setAction(extend_hatch_holder_) ;
                        hatch_holder_extended_ = true ;
                        state_ = State::ExtendHatchHolder ;
                    }
                    else {
                        logger.startMessage(MessageLogger::MessageType::debug, MSG_GROUP_READY_ACTION) ;                
                        logger << "ReadyAction: start with turntable/lift sequence" ;                   
                        logger.endMessage() ;

                        startTurnLiftSequence() ;
                        hatch_holder_extended_ = false ;                             
                    }
                }
                break ;

            case State::ExtendHatchHolder:
                if (extend_hatch_holder_->isDone()) {
                    startTurnLiftSequence() ;
                }
                break ;

            case State::RetractHatchHolder:
                if (retract_hatch_holder_->isDone()) {
                    state_ = State::Idle ;
                }
                break ;
            
            case State::TurntableAndLift:
                if (set_turntable_angle_->isDone() && set_lifter_final_height_->isDone()) {
                    if (hatch_holder_extended_) {
                        if (leave_) {
                            logger.startMessage(MessageLogger::MessageType::debug, MSG_GROUP_READY_ACTION) ;                
                            logger << "ReadyAction: skipped retracting hatch, leave = true" ;
                            logger.endMessage() ;
                        }
                        else {
                            auto hatch_holder = getGamePiece().getHatchHolder() ;
                            hatch_holder->setAction(retract_hatch_holder_) ;
                            state_ = State::RetractHatchHolder ;

                            logger.startMessage(MessageLogger::MessageType::debug, MSG_GROUP_READY_ACTION) ;                
                            logger << "ReadyAction: retracting hatch" ;
                            logger.endMessage() ;                            
                        }
                    }
                    else {
                        state_ = State::Idle ;
                    }
                }
                break ;

            case State::TurntableAndSafeHeight:
                if (set_turntable_angle_->isDone() && set_lifter_safe_height_->isDone()) {
                    auto lifter = getGamePiece().getLifter() ;                    
                    lifter->setAction(set_lifter_final_height_) ;
                    state_ = State::LifterFinalHeight ;
                }
                break ;                
             
            case State::LifterFinalHeight:
                if(set_lifter_final_height_->isDone()){
                    if (hatch_holder_extended_) {
                        if (leave_) {
                            logger.startMessage(MessageLogger::MessageType::debug, MSG_GROUP_READY_ACTION) ;                
                            logger << "ReadyAction: skipping hatch retract, leave = true" ;
                            logger.endMessage() ;
                        }
                        else {
                            auto hatch_holder = getGamePiece().getHatchHolder() ;
                            hatch_holder->setAction(retract_hatch_holder_) ;
                            state_ = State::RetractHatchHolder ;

                            logger.startMessage(MessageLogger::MessageType::debug, MSG_GROUP_READY_ACTION) ;                
                            logger << "ReadyAction: retracting hatch" ;
                            logger.endMessage() ;                            
                        }
                    }
                    else {
                        state_ = State::Idle ;
                    }
                }            
                break ;  

            case State::LifterSafeHeight:
                if (set_lifter_safe_height_->isDone()) {
                    startTurnLiftSequence() ;
                }   
                break ;

            case State::Idle:
                break ; 
            }
        }

        bool ReadyAction::isDone() {
            return state_ == State::Idle ;
        }

        void ReadyAction::cancel() {
            if (state_ != State::Idle) {
                auto turntable = getGamePiece().getTurntable() ;
                turntable->cancelAction() ;

                auto lifter = getGamePiece().getLifter() ;
                lifter->cancelAction() ;

                state_ = State::Idle ;
            }
        }

        std::string ReadyAction::toString() {
            std::string ret("ReadyAction") ;
            ret += " " + height_ ;
            ret += " " + angle_ ;
            return ret ;
        }
    }
}