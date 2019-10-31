#pragma once

#include <boost/smart_ptr.hpp>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_set>

#include "fits.hpp"

#include <sqlite3.h>

// Forward declaration
class websocket_session;

// Represents the shared server state
class shared_state
{
    std::string const doc_root_;
    std::string const home_dir_;

    // This mutex synchronizes all access to sessions_
    std::mutex mutex_;

    // Keep a list of all the connected clients
    std::unordered_set<websocket_session*> sessions_;

    //FITS datasets
    std::unordered_map<std::string, std::shared_ptr<FITS>> DATASETS;

    //synchronize shared access to FITS datasets
    std::shared_mutex fits_mutex;

    //a local Splatalogue database
    sqlite3 *splat_db_;

public:
    explicit
    shared_state(std::string doc_root, std::string home_dir);
    ~shared_state();

    std::string const&
    doc_root() const noexcept
    {
        return doc_root_;
    }

    std::string const&
    home_dir() const noexcept
    {
        return home_dir_;
    }

    sqlite3 *
    splat_db() {
        return splat_db_;
    }

    void join  (websocket_session* session);
    void leave (websocket_session* session);
    void send  (std::string message);
};
