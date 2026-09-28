// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include <exception>

namespace CNA::Internal::GamerServices {
void pumpRetainedCompletions(const std::shared_ptr<IGamerServicesBackend>& executor) {
    if(!executor || executor==backend()) return;
    auto events=executor->pump();
    std::exception_ptr firstError;
    for(auto& event:events) {
        if(event.type!=BackendEvent::Type::Completion || !event.completion) continue;
        try {event.completion();}
        catch(...) {if(!firstError)firstError=std::current_exception();}
    }
    if(firstError)std::rethrow_exception(firstError);
}
}
