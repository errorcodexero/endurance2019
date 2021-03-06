﻿#include "XeroPathManager.h"
#include "CSVData.h"

namespace xero {
    namespace misc {
        //
        // Note, this reversal of left and right is intentional.  I am pretty sure that the
        // PathWeaver is reversing these when writing out the paths (or their nomemclature
        // is backward and I don't understand it).
        //
        constexpr const char* rightSuffix  = ".right.pf1.csv";
        constexpr const char* leftSuffix = ".left.pf1.csv";

        XeroPathManager::XeroPathManager(const std::string &basedir) : basedir_(basedir) {
        }

        bool XeroPathManager::loadPath(const std::string & pathName) {
            std::string filename = basedir_ + "/" + pathName + leftSuffix ;
            auto leftData = CSVData(filename);
            if (!leftData.isLoaded()) {
                return false;
            }

            filename = basedir_ + "/" + pathName + rightSuffix ;
            auto rightData = CSVData(filename);
            if (!rightData.isLoaded()) {
                return false;
            }

            if(rightData.size() != leftData.size()) {
                return false;
            }

            if (leftData.size() == 0)
                return false ;

            // Path weaver has the data wrong, swap the left and right data
            paths_[pathName] = std::make_shared<XeroPath>(pathName, rightData, leftData);

            return true;
        }

        bool XeroPathManager::hasPath(const std::string & pathName) {
            auto iter = paths_.find(pathName);
            return iter != std::end(paths_);
        }

        std::shared_ptr<XeroPath> XeroPathManager::getPath(const std::string &pathName) {
            if(hasPath(pathName)) {
                return paths_[pathName];
            } else {
                return nullptr;
            }
        }
    }
}
