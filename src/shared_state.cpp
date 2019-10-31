//
// Copyright (c) 2016-2019 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/vinniefalco/CppCon2018
//

#include "shared_state.hpp"
#include "websocket_session.hpp"

shared_state::
shared_state(std::string doc_root, std::string home_dir)
    : doc_root_(std::move(doc_root)), home_dir_(std::move(home_dir))
{
    int rc = sqlite3_open_v2("splatalogue_v3.db", &splat_db_,
                           SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX, NULL);

  	if (rc) {
    	fprintf(stderr, "Can't open local splatalogue database: %s\n",
            sqlite3_errmsg(splat_db_));
    	sqlite3_close(splat_db_);
    	splat_db_ = NULL;
  	}
}

shared_state::~shared_state()
{
    if (splat_db_ != NULL)            
        sqlite3_close(splat_db_);    
}

void
shared_state::
join(websocket_session* session)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.insert(session);
}

void
shared_state::
leave(websocket_session* session)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.erase(session);
}

// Broadcast a message to all websocket client sessions
void
shared_state::
send(std::string message)
{
    // Put the message in a shared pointer so we can re-use it for each client
    auto const ss = boost::make_shared<std::string const>(std::move(message));

    // Make a local list of all the weak pointers representing
    // the sessions, so we can do the actual sending without
    // holding the mutex:
    std::vector<boost::weak_ptr<websocket_session>> v;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        v.reserve(sessions_.size());
        for(auto p : sessions_)
            v.emplace_back(p->weak_from_this());
    }

    // For each session in our local list, try to acquire a strong
    // pointer. If successful, then send the message on that session.
    for(auto const& wp : v)
        if(auto sp = wp.lock())
            sp->send(ss);
}
