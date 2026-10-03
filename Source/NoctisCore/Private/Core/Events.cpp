#include "Noctis/Core/Events.h"

#include <algorithm>

namespace noctis
{
const char* eventTypeName(EventType t)
{
    switch (t)
    {
    case EventType::Footfall: return "Footfall";
    case EventType::Vocalization: return "Vocalization";
    case EventType::Attack: return "Attack";
    case EventType::Injury: return "Injury";
    case EventType::Death: return "Death";
    case EventType::Birth: return "Birth";
    case EventType::Hatch: return "Hatch";
    case EventType::EggsLaid: return "EggsLaid";
    case EventType::Mating: return "Mating";
    case EventType::CarcassCreated: return "CarcassCreated";
    case EventType::CarcassDepleted: return "CarcassDepleted";
    case EventType::Drink: return "Drink";
    case EventType::Feed: return "Feed";
    case EventType::Flee: return "Flee";
    case EventType::VigilanceRaised: return "VigilanceRaised";
    case EventType::HuntSearch: return "HuntSearch";
    case EventType::HuntTargetSelected: return "HuntTargetSelected";
    case EventType::HuntStalk: return "HuntStalk";
    case EventType::HuntChase: return "HuntChase";
    case EventType::HuntAbandon: return "HuntAbandon";
    case EventType::HuntKill: return "HuntKill";
    case EventType::GroupFormationChange: return "GroupFormationChange";
    case EventType::GroupSplit: return "GroupSplit";
    case EventType::GroupMerge: return "GroupMerge";
    case EventType::Display: return "Display";
    case EventType::Fight: return "Fight";
    case EventType::Slip: return "Slip";
    case EventType::WeatherChange: return "WeatherChange";
    case EventType::Lightning: return "Lightning";
    case EventType::FireIgnition: return "FireIgnition";
    case EventType::SeasonChange: return "SeasonChange";
    case EventType::ResearcherNoticed: return "ResearcherNoticed";
    case EventType::ResearcherWarned: return "ResearcherWarned";
    case EventType::ResearcherAttacked: return "ResearcherAttacked";
    case EventType::DroneNoticed: return "DroneNoticed";
    case EventType::VehicleNoticed: return "VehicleNoticed";
    case EventType::NestBuilt: return "NestBuilt";
    case EventType::NestPredated: return "NestPredated";
    case EventType::Immigration: return "Immigration";
    case EventType::Emigration: return "Emigration";
    case EventType::ObservationLogged: return "ObservationLogged";
    case EventType::PhotoTaken: return "PhotoTaken";
    case EventType::RecordingMade: return "RecordingMade";
    case EventType::SensorTriggered: return "SensorTriggered";
    case EventType::ToothShed: return "ToothShed";
    case EventType::Defecation: return "Defecation";
    case EventType::Count: break;
    }
    return "Unknown";
}

void EventBus::emit(const SimEvent& e)
{
    std::lock_guard<std::mutex> lock(mutex_);
    pending_.push_back(e);
}

void EventBus::flush()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        lastFlushed_.swap(pending_);
        pending_.clear();
    }
    // Deterministic order regardless of which worker thread emitted first.
    std::stable_sort(lastFlushed_.begin(), lastFlushed_.end(), [](const SimEvent& a, const SimEvent& b) {
        if (a.time != b.time)
        {
            return a.time < b.time;
        }
        if (a.subject.value != b.subject.value)
        {
            return a.subject.value < b.subject.value;
        }
        if (a.type != b.type)
        {
            return a.type < b.type;
        }
        return a.param < b.param;
    });
    totalEmitted_ += lastFlushed_.size();
    for (const SimEvent& e : lastFlushed_)
    {
        for (auto& l : listeners_)
        {
            l.second(e);
        }
        if (e.type == EventType::Footfall)
        {
            continue; // too frequent for the chronicle; tracks keep their own record
        }
        if (chronicleRing_.size() < chronicleCapacity_)
        {
            chronicleRing_.push_back(e);
        }
        else if (chronicleCapacity_ > 0)
        {
            chronicleRing_[chronicleHead_] = e;
            chronicleHead_ = (chronicleHead_ + 1) % chronicleCapacity_;
        }
    }
}

int EventBus::subscribe(Listener l)
{
    const int h = nextHandle_++;
    listeners_.emplace_back(h, std::move(l));
    return h;
}

void EventBus::unsubscribe(int handle)
{
    listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(), [handle](const std::pair<int, Listener>& p) { return p.first == handle; }),
                     listeners_.end());
}

std::vector<SimEvent> EventBus::chronicle() const
{
    std::vector<SimEvent> out;
    out.reserve(chronicleRing_.size());
    if (chronicleRing_.size() < chronicleCapacity_)
    {
        out = chronicleRing_;
    }
    else
    {
        for (size_t i = 0; i < chronicleRing_.size(); ++i)
        {
            out.push_back(chronicleRing_[(chronicleHead_ + i) % chronicleRing_.size()]);
        }
    }
    return out;
}
} // namespace noctis
